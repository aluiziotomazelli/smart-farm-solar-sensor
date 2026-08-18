#pragma once

#include <cstdint>
#include <optional>
#include "farm_protocol_types.hpp"
#include "interfaces/i_ota_manager.hpp"
#include "interfaces/i_hal_freertos.hpp"

/**
 * @struct OtaActionResult
 * @brief Result of firmware verification on boot.
 */
struct OtaActionResult
{
    bool success{false}; ///< True if app was valid and confirmed
    farm::OtaExecResult exec_result{farm::OtaExecResult::CONFIRMED_SUCCESS};
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
     * @brief Checks if there is a pending OTA operation.
     * @return True if there is a pending operation, false otherwise.
     */
    bool check_pending_verify() const;

    /**
     * @brief Returns the currently running firmware version.
     * @return Optional OtaVersion struct.
     */
    std::optional<OtaVersion> get_running_version() const;

    /**
     * @brief Performs post-boot firmware verification.
     * @param session_healthy True if system startup/sessions were healthy.
     * @return OtaVerifyResult struct.
     */
    OtaActionResult confirm_firmware(bool session_healthy);

    /**
     * @brief Executes active OTA download polling until completed or failed.
     * @param timeout_ms Maximum time to wait for download to finish.
     * @return OtaVerifyResult struct.
     */
    OtaActionResult execute_download(uint32_t timeout_ms = 60000);

    /**
     * @brief Triggers rollback to previous firmware partition and reboots.
     */
    void rollback_and_reboot();

private:
    IOtaManager& ota_manager_;
    idf_hals::IHalFreertos& hal_freertos_;
};
