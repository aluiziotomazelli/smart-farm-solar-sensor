// main/include/interfaces/i_ota_controller.hpp
#pragma once

#include <cstdint>
#include <optional>
#include "farm_protocol_types.hpp"
#include "ota_types.hpp"

/**
 * @struct OtaActionResult
 * @brief Result of firmware verification on boot or download execution.
 */
struct OtaActionResult
{
    bool success{false}; ///< True if app was valid and confirmed / downloaded successfully
    farm::OtaExecResult exec_result{farm::OtaExecResult::CONFIRMED_SUCCESS};
    farm::OtaErrorCode error_code{farm::OtaErrorCode::NONE};
};

/**
 * @class IOtaController
 * @brief Interface managing low-level OTA download worker execution and post-boot firmware verification.
 */
class IOtaController
{
public:
    virtual ~IOtaController() = default;

    /**
     * @brief Initializes the underlying OTA manager with configuration.
     * @param config The OTA configuration parameters.
     * @return True if initialized successfully, false otherwise.
     */
    virtual bool init(const OtaConfig& config) = 0;

    /**
     * @brief Checks if there is a pending OTA operation / verification after reboot.
     * @return True if there is a pending operation, false otherwise.
     */
    virtual bool check_pending_verify() const = 0;

    /**
     * @brief Returns the currently running firmware version.
     * @return Optional OtaVersion struct.
     */
    virtual std::optional<OtaVersion> get_running_version() const = 0;

    /**
     * @brief Performs post-boot firmware verification.
     * @param session_healthy True if system startup and all critical subsystems initialized healthy.
     * @return OtaActionResult struct.
     */
    virtual OtaActionResult confirm_firmware(bool session_healthy) = 0;

    /**
     * @brief Executes active OTA download polling until completed or failed.
     * @param timeout_ms Maximum time in milliseconds to wait for download to finish.
     * @return OtaActionResult struct.
     */
    virtual OtaActionResult execute_download(uint32_t timeout_ms = 60000) = 0;

    /**
     * @brief Triggers rollback to previous firmware partition and reboots.
     */
    virtual void rollback_and_reboot() = 0;
};
