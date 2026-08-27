// main/include/command_handler.hpp
#pragma once

#include "interfaces/i_command_handler.hpp"
#include "interfaces/i_espnow_manager.hpp"
#include "interfaces/i_time_manager.hpp"
#include "interfaces/i_hal_freertos.hpp"

/**
 * @class CommandHandler
 * @brief Processes incoming ESP-NOW commands for the SolarSensor node.
 *
 * Drains the incoming message queue, dispatches commands (START_OTA, REBOOT, SYNC_TIME),
 * synchronizes system time, confirms reception back via ESP-NOW, and returns result flags.
 */
class CommandHandler : public ICommandHandler
{
public:
    CommandHandler(
        QueueHandle_t rx_queue,
        espnow::IEspNowManager& espnow,
        time_manager::ITimeManager& time_manager,
        idf_hals::IHalFreertos& hal_freertos);

    /** @copydoc ICommandHandler::process */
    CommandProcessResult process() override;

private:
    QueueHandle_t rx_queue_;
    espnow::IEspNowManager& espnow_;
    time_manager::ITimeManager& time_manager_;
    idf_hals::IHalFreertos& hal_freertos_;
};
