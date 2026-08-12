#pragma once

#include "interfaces/i_espnow_manager.hpp"
#include "interfaces/i_time_manager.hpp"
#include "interfaces/i_ota_trigger.hpp"
#include "interfaces/i_hal_system.hpp"
#include "interfaces/i_nvs_core.hpp"
#include "interfaces/i_hal_freertos.hpp"

/**
 * @class CommandHandler
 * @brief Processes incoming ESP-NOW commands for the SolarSensor node.
 *
 * Drains the incoming message queue, dispatches commands (START_OTA, REBOOT, SYNC_TIME),
 * updates system time and core state, and confirms reception back via ESP-NOW.
 */
class CommandHandler
{
public:
    CommandHandler(
        QueueHandle_t rx_queue,
        espnow::IEspNowManager& espnow,
        time_manager::ITimeManager& time_manager,
        IOtaTrigger& espnow_ota_trigger,
        idf_hals::ISystemHAL& hal_system,
        CoreStorage& core,
        idf_hals::IHalFreertos& hal_freertos);

    /**
     * @brief Drains all pending messages in rx_queue and processes commands.
     */
    void process();

private:
    QueueHandle_t rx_queue_;
    espnow::IEspNowManager& espnow_;
    time_manager::ITimeManager& time_manager_;
    IOtaTrigger& espnow_ota_trigger_;
    idf_hals::ISystemHAL& hal_system_;
    CoreStorage& core_;
    idf_hals::IHalFreertos& hal_freertos_;
};
