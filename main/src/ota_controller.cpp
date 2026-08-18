#include "ota_controller.hpp"
#include "esp_log.h"

static const char* TAG = "OtaController";

static farm::OtaErrorCode map_fail_reason(OtaFailReason reason);

OtaController::OtaController(IOtaManager& ota_manager, idf_hals::IHalFreertos& hal_freertos)
    : ota_manager_(ota_manager)
    , hal_freertos_(hal_freertos)
{
}

bool OtaController::init(const OtaConfig& config)
{
    return ota_manager_.init(config);
}

bool OtaController::check_pending_verify() const
{
    return ota_manager_.check_pending_verify();
}

std::optional<OtaVersion> OtaController::get_running_version() const
{
    return ota_manager_.get_running_version();
}

void OtaController::rollback_and_reboot()
{
    ESP_LOGE(TAG, "Triggering rollback and reboot...");
    ota_manager_.rollback_and_reboot();
}

OtaActionResult OtaController::confirm_firmware(bool session_healthy)
{
    OtaActionResult result = {};

    if (!session_healthy || !ota_manager_.confirm_app_valid()) {
        result.success = false;
        result.exec_result = farm::OtaExecResult::ROLLBACK_TRIGGERED;
        result.error_code =
            !session_healthy ? farm::OtaErrorCode::HEALTH_CHECK_FAILED : farm::OtaErrorCode::PARTITION_CONFIRM_FAILED;

        ESP_LOGE(TAG, "Firmware verification failed (reason: %d)", static_cast<int>(result.error_code));
        return result;
    }

    // App is healthy and was successfully confirmed,
    result.success = true;
    result.exec_result = farm::OtaExecResult::CONFIRMED_SUCCESS;
    result.error_code = farm::OtaErrorCode::NONE;

    return result;
}

OtaActionResult OtaController::execute_download(uint32_t timeout_ms)

{
    OtaActionResult result = {};

    if (!ota_manager_.start_ota()) {
        ESP_LOGE(TAG, "Failed to start OTA download session");
        result.success = false;
        result.exec_result = farm::OtaExecResult::DOWNLOAD_FAILED;
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
        result.exec_result = farm::OtaExecResult::CONFIRMED_SUCCESS;
        result.error_code = farm::OtaErrorCode::NONE;
    }
    else {
        result.success = false;
        result.exec_result = farm::OtaExecResult::DOWNLOAD_FAILED;
        if (status == OtaStatus::FAILED) {
            OtaFailReason reason = ota_manager_.get_last_error();
            result.error_code = map_fail_reason(reason);
            ESP_LOGE(
                TAG,
                "OTA download failed (reason: %d, error_code: %d)",
                static_cast<int>(reason),
                static_cast<int>(result.error_code));
        }
        else if (elapsed_ms >= timeout_ms) {
            result.error_code = farm::OtaErrorCode::WATCHDOG_TIMEOUT;
            ESP_LOGE(TAG, "OTA global watchdog timeout (>%u ms)", timeout_ms);
        }
        ota_manager_.cancel_ota();
    }
    return result;
}

static farm::OtaErrorCode map_fail_reason(OtaFailReason reason)
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
