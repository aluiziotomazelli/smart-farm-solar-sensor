#pragma once

#include <atomic>

#include "interfaces/i_nvs_core.hpp"
#include "interfaces/i_hal_timer.hpp"
#include "interfaces/i_ota_manager.hpp"
#include "interfaces/i_ota_trigger.hpp"
#include "interfaces/i_wifi_manager.hpp"
#include "interfaces/i_espnow_manager.hpp"
#include "interfaces/i_hal_sleep.hpp"
#include "interfaces/i_hal_system.hpp"

#include "solar_sensor_stats.hpp"

/**
 * @class SolarSensor
 * @brief Interface for the solar sensor.
 */
class SolarSensor : public IOtaTriggerListener
{
public:
    SolarSensor(
        INvsCore& core_storage,
        idf_hals::ITimerHAL& hal_timer,
        IOtaManager& ota_manager,
        IOtaTrigger& btn_trigger,
        IOtaTrigger& espnow_trigger,
        espnow::IEspNowManager& espnow,
        QueueHandle_t rx_queue,
        wifi_manager::IWiFiManager& wifi,
        idf_hals::ISleepHAL& hal_sleep_,
        idf_hals::ISystemHAL& hal_system_);

    virtual ~SolarSensor() = default;

    esp_err_t init();
    esp_err_t run();

    /** @copydoc IOtaTriggerListener::on_ota_triggered */
    void on_ota_triggered(OtaTriggerSource source) override;

protected:
    CoreStorage core_;
    SolarStats stats_;

    bool session_healthy_ = true;
    bool pending_firmware_verify_ = false;
    bool pending_core_commit_ = false;
    bool pending_solar_commit_ = false;

private:
    INvsCore& core_storage_;
    idf_hals::ITimerHAL& hal_timer_;
    IOtaManager& ota_manager_;
    IOtaTrigger& btn_trigger_;
    IOtaTrigger& espnow_trigger_;
    espnow::IEspNowManager& espnow_;
    QueueHandle_t rx_queue_;
    wifi_manager::IWiFiManager& wifi_;
    idf_hals::ISleepHAL& hal_sleep_;
    idf_hals::ISystemHAL& hal_system_;

    std::atomic<bool> ota_triggered_{false};
    int64_t last_nvs_commit_ts_ = 0;

    esp_err_t init_core_storage();
    esp_err_t init_wifi();
    esp_err_t init_espnow();
    esp_err_t init_ota();

    void save_persistent_state();
};