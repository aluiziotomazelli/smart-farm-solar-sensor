// main/include/ota_controller.hpp
#pragma once

#include <cstdint>
#include <optional>
#include "farm_protocol_types.hpp"
#include "interfaces/i_ota_controller.hpp"
#include "interfaces/i_ota_manager.hpp"
#include "interfaces/i_hal_freertos.hpp"

/**
 * @class OtaController
 * @brief Manages low-level OTA download worker execution and post-boot firmware verification.
 */
class OtaController : public IOtaController
{
public:
    OtaController(IOtaManager& ota_manager, idf_hals::IHalFreertos& hal_freertos);

    /** @copydoc IOtaController::init */
    bool init(const OtaConfig& config) override;

    /** @copydoc IOtaController::check_pending_verify */
    bool check_pending_verify() const override;

    /** @copydoc IOtaController::get_running_version */
    std::optional<OtaVersion> get_running_version() const override;

    /** @copydoc IOtaController::confirm_firmware */
    OtaActionResult confirm_firmware(bool session_healthy) override;

    /** @copydoc IOtaController::execute_download */
    OtaActionResult execute_download(uint32_t timeout_ms = 60000) override;

    /** @copydoc IOtaController::rollback_and_reboot */
    void rollback_and_reboot() override;

private:
    IOtaManager& ota_manager_;
    idf_hals::IHalFreertos& hal_freertos_;
};
