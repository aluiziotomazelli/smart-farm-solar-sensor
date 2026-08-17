#pragma once

#include "app_storage.hpp"
#include "interfaces/i_persistence_backend.hpp"
#include "interfaces/i_solar_sensor_nvs.hpp"
#include "solar_sensor_stats.hpp"

/**
 * @class SolarSensorNvs
 * @brief Persistent storage handler for the Solar Sensor application.
 */
class SolarSensorNvs : public ISolarSensorNvs,
                       public AppStorage<SolarStats, SOLAR_STATS_MAGIC, SOLAR_STATS_VERSION>
{
public:
    SolarSensorNvs(IPersistenceBackend& rtc_stats, IPersistenceBackend& nvs_stats)
        : AppStorage<SolarStats, SOLAR_STATS_MAGIC, SOLAR_STATS_VERSION>(rtc_stats, nvs_stats, "SolarSensorNvs")
    {
    }

    /** @brief Initializes the application statistics and state. Loads from storage or persists default stats if empty/invalid.
     * @param[out] stats The struct to populate with loaded/default data.
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