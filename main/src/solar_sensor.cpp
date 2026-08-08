// main/src/solar_sensor.cpp
#include "solar_sensor.hpp"

#define LOG_LOCAL_LEVEL ESP_LOG_INFO
#include "esp_log.h"

static const char* TAG = "SolarSensor";

SolarSensor::SolarSensor(
    INvsCore& core_storage,
    idf_hals::ITimerHAL& hal_timer,
    IOtaManager& ota_manager,
    IOtaTrigger& btn_trigger,
    IOtaTrigger& espnow_trigger)
    : core_storage_(core_storage)
    , hal_timer_(hal_timer)
    , ota_manager_(ota_manager)
    , btn_trigger_(btn_trigger)
    , espnow_trigger_(espnow_trigger)
{
}

// ===================================================================
// Private Methods
// ===================================================================
static constexpr uint32_t NVS_PERIODIC_COMMIT_INTERVAL_MS = 15 * 60 * 1000; // 15 minutes
void SolarSensor::save_persistent_state()
{
    int64_t now_ms = hal_timer_.get_time_us() / 1000;

    bool periodic_commit =
        (last_nvs_commit_ts_ > 0) && ((now_ms - last_nvs_commit_ts_) >= NVS_PERIODIC_COMMIT_INTERVAL_MS);

    if (periodic_commit) {
        ESP_LOGI(TAG, "Periodic NVS commit triggered");
    }

    bool force_core = pending_core_commit_ || periodic_commit;
    bool force_node = pending_solar_commit_ || periodic_commit;

    if (core_storage_.save_core(core_, force_core) == ESP_OK) {
        pending_core_commit_ = false;
    }
    else {
        ESP_LOGE(TAG, "Failed to save stats to core storage");
    }

    // if (solar_storage_.save_app_data(stats_, force_node) == ESP_OK) {
    //     pending_solar_commit_ = false;
    // }
    // else {
    //     ESP_LOGE(TAG, "Failed to save stats to solar storage");
    // }

    last_nvs_commit_ts_ = now_ms;
}