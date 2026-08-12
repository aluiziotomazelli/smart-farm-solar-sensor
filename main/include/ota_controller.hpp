#pragma once

#include <cstdint>
#include <optional>
#include "farm_protocol_types.hpp"
#include "interfaces/i_ota_manager.hpp"
#include "interfaces/i_hal_freertos.hpp"

/**
 * @struct OtaVerifyResult
 * @brief Result of firmware verification on boot.
 */
struct OtaVerifyResult
{
    bool pending_verify{false};               ///< True if a post-OTA verification was pending on boot
    bool success{false};                      ///< True if app was valid and confirmed
    std::optional<OtaVersion> version;        ///< Confirmed firmware version (present if success == true)
    farm::OtaExecResult exec_result{farm::OtaExecResult::CONFIRMED_SUCCESS};
    farm::OtaErrorCode error_code{farm::OtaErrorCode::NONE};
};

/**
 * @struct OtaDownloadResult
 * @brief Result of executing an active OTA download.
 */
struct OtaDownloadResult
{
    bool success{false};
    farm::OtaErrorCode error_code{farm::OtaErrorCode::NONE};
};

/**
 * @class OtaController
 * @brief Manages low-level OTA download worker execution and post-boot firmware verification.
 */
class OtaController
{
public:
    OtaController(IOtaManager& ota_manager, idf_hals::IHalFreertos& hal_freertos);

    /**
     * @brief Initializes the underlying OTA manager with configuration.
     */
    bool init(const OtaConfig& config);

    /**
     * @brief Performs post-boot firmware verification.
     * @param session_healthy True if system startup/sessions were healthy.
     * @return OtaVerifyResult struct.
     */
    OtaVerifyResult verify_firmware_on_boot(bool session_healthy);

    /**
     * @brief Executes active OTA download polling until completed or failed.
     * @param timeout_ms Maximum time to wait for download to finish.
     * @return OtaDownloadResult struct.
     */
    OtaDownloadResult execute_download(uint32_t timeout_ms = 60000);

    /**
     * @brief Helper to map OtaFailReason from OtaManager to protocol OtaErrorCode.
     */
    static farm::OtaErrorCode map_fail_reason(OtaFailReason reason);

private:
    IOtaManager& ota_manager_;
    idf_hals::IHalFreertos& hal_freertos_;
};
