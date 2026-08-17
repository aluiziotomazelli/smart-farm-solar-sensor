#pragma once

#include "interfaces/i_solar_sensor_nvs.hpp"
#include "interfaces/i_persistence_backend.hpp"
#include "app_storage.hpp"

/**
 * @class SolarSensorNvs
 * @brief Persistent storage handler for the Solar Sensor application.
 */
class SolarSensorNvs : public ISolarSensorNvs, public AppStorage<SolarStats>

{
public:
    SolarSensorNvs(IPersistenceBackend& rtc_stats, IPersistenceBackend& nvs_stats)
        : AppStorage<SolarStats>(rtc_stats, nvs_stats, "SolarSensorNvs")
    {
    }

    /** @brief Initializes the application statistics and state. To be used in boot, load from NVS and saves default
     * values if NVS is empty or contains data with different CRC
     * @param[out] stats The struct to populate with default data.
     * @param[in] default_stats The struct containing the default data.
     * @return ESP_OK on success, or an error code.
     */
    esp_err_t init_app_data(SolarStats& stats, const SolarStats& default_stats) override
    {
        return init_app_data_impl(stats, default_stats);
    }

    esp_err_t load_app_data(SolarStats& stats) override { return load_app_data_impl(stats); }

    esp_err_t save_app_data(const SolarStats& stats, bool force_nvs_commit = false) override
    {
        return save_app_data_impl(stats, force_nvs_commit);
    }
};