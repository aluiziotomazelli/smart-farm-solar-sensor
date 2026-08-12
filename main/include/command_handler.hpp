#pragma once

#include "interfaces/i_espnow_manager.hpp"
#include "interfaces/i_time_manager.hpp"
#include "interfaces/i_nvs_core.hpp"
#include "interfaces/i_hal_freertos.hpp"

/**
 * @struct CommandProcessResult
 * @brief Result of draining and processing ESP-NOW commands.
 */
struct CommandProcessResult
{
    bool core_modified{false};     ///< True if system clock or core state was updated (requires pending_core_commit_)
    bool ota_requested{false};    ///< True if START_OTA command was received
    bool reboot_requested{false}; ///< True if REBOOT command was received
};

/**
 * @class CommandHandler
 * @brief Processes incoming ESP-NOW commands for the SolarSensor node.
 *
 * Drains the incoming message queue, dispatches commands (START_OTA, REBOOT, SYNC_TIME),
 * updates system time and core state, confirms reception back via ESP-NOW, and returns result flags.
 */
class CommandHandler
{
public:
    CommandHandler(
        QueueHandle_t rx_queue,
        espnow::IEspNowManager& espnow,
        time_manager::ITimeManager& time_manager,
        CoreStorage& core,
        idf_hals::IHalFreertos& hal_freertos);

    /**
     * @brief Drains all pending messages in rx_queue and processes commands.
     * @return CommandProcessResult struct with status flags.
     */
    CommandProcessResult process();

private:
    QueueHandle_t rx_queue_;
    espnow::IEspNowManager& espnow_;
    time_manager::ITimeManager& time_manager_;
    CoreStorage& core_;
    idf_hals::IHalFreertos& hal_freertos_;
};
