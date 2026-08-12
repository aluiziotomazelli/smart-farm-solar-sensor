#include "ota_controller.hpp"
#include "esp_log.h"

static const char* TAG = "OtaController";

OtaController::OtaController(IOtaManager& ota_manager, idf_hals::IHalFreertos& hal_freertos)
    : ota_manager_(ota_manager)
    , hal_freertos_(hal_freertos)
{
}

bool OtaController::init(const OtaConfig& config)
{
    return ota_manager_.init(config);
}

OtaVerifyResult OtaController::verify_firmware_on_boot(bool session_healthy)
{
    OtaVerifyResult result{};
    result.pending_verify = ota_manager_.check_pending_verify();

    if (!result.pending_verify) {
        return result;
    }

    if (!session_healthy || !ota_manager_.confirm_app_valid()) {
        result.success = false;
        result.exec_result = farm::OtaExecResult::ROLLBACK_TRIGGERED;
        result.error_code = !session_healthy ? farm::OtaErrorCode::HEALTH_CHECK_FAILED
                                              : farm::OtaErrorCode::PARTITION_CONFIRM_FAILED;

        ESP_LOGE(TAG, "Firmware verification failed! Triggering rollback (reason: %d)...", static_cast<int>(result.error_code));
        ota_manager_.rollback_and_reboot();
        return result;
    }

    result.success = true;
    result.exec_result = farm::OtaExecResult::CONFIRMED_SUCCESS;
    result.error_code = farm::OtaErrorCode::NONE;
    result.version = ota_manager_.get_running_version();

    if (result.version.has_value()) {
        ESP_LOGI(TAG, "Firmware confirmed successfully. Running version: %u.%u.%u",
                 result.version->major, result.version->minor, result.version->patch);
    }

    return result;
}

OtaDownloadResult OtaController::execute_download(uint32_t timeout_ms)
{
    OtaDownloadResult result{};

    if (!ota_manager_.start_ota()) {
        ESP_LOGE(TAG, "Failed to start OTA download session");
        result.success = false;
        result.error_code = farm::OtaErrorCode::DOWNLOAD_SESSION_FAIL;
        return result;
    }

    uint32_t elapsed_ms = 0;
    OtaStatus status = ota_manager_.get_status();

    while (status != OtaStatus::READY_TO_RESTART && status != OtaStatus::FAILED && elapsed_ms < timeout_ms) {
        hal_freertos_.task_delay(pdMS_TO_TICKS(500));
        elapsed_ms += 500;
        status = ota_manager_.get_status();
    }

    if (status == OtaStatus::READY_TO_RESTART) {
        ESP_LOGI(TAG, "OTA download completed successfully. Ready to restart.");
        result.success = true;
        result.error_code = farm::OtaErrorCode::NONE;
    }
    else {
        result.success = false;
        if (status == OtaStatus::FAILED) {
            OtaFailReason reason = ota_manager_.get_last_error();
            result.error_code = map_fail_reason(reason);
            ESP_LOGE(TAG, "OTA download failed (reason: %d, error_code: %d)",
                     static_cast<int>(reason), static_cast<int>(result.error_code));
        }
        else if (elapsed_ms >= timeout_ms) {
            result.error_code = farm::OtaErrorCode::WATCHDOG_TIMEOUT;
            ESP_LOGE(TAG, "OTA download watchdog timeout (>%u ms)", timeout_ms);
        }

        ota_manager_.cancel_ota();
    }

    return result;
}

farm::OtaErrorCode OtaController::map_fail_reason(OtaFailReason reason)
{
    switch (reason) {
    case OtaFailReason::MANIFEST_URL_INVALID:
    case OtaFailReason::MANIFEST_INVALID:
        return farm::OtaErrorCode::MANIFEST_PARSE_ERROR;

    case OtaFailReason::MANIFEST_HTTP_FAIL:
    case OtaFailReason::FIRMWARE_URL_INVALID:
    case OtaFailReason::DOWNLOAD_HTTP_FAIL:
        return farm::OtaErrorCode::HTTP_DOWNLOAD_FAILED;

    case OtaFailReason::DEVICE_TYPE_MISMATCH:
        return farm::OtaErrorCode::DEVICE_TYPE_MISMATCH;

    case OtaFailReason::CURRENT_VERSION_PARSE_FAIL:
    case OtaFailReason::VERSION_NOT_NEWER:
    case OtaFailReason::DOWNLOAD_IMAGE_VERSION_FAIL:
        return farm::OtaErrorCode::VERSION_NOT_NEWER;

    case OtaFailReason::DOWNLOAD_SESSION_FAIL:
    case OtaFailReason::DOWNLOAD_IMAGE_DESC_FAIL:
        return farm::OtaErrorCode::DOWNLOAD_SESSION_FAIL;

    case OtaFailReason::DOWNLOAD_FINISH_FAIL:
    case OtaFailReason::HASH_PARTITION_FAIL:
        return farm::OtaErrorCode::FLASH_WRITE_ERROR;

    case OtaFailReason::HASH_MISMATCH:
        return farm::OtaErrorCode::IMAGE_HASH_MISMATCH;

    case OtaFailReason::NONE:
    default:
        return farm::OtaErrorCode::UNKNOWN_ERROR;
    }
}
