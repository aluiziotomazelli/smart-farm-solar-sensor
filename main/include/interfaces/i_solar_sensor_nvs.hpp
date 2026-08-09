#pragma once
#include "esp_err.h"

#include "solar_sensor_stats.hpp"

/**
 * @class ISolarSensorNvs
 * @brief Interface for persisting water tank application state and statistics.
 *
 * Abstracts both NVS and RTC memory storage, allowing the logic to be tested
 * without physical non-volatile storage.
 */
class ISolarSensorNvs
{
public:
    virtual ~ISolarSensorNvs() = default;

    /**
     * @brief Loads the application statistics and state.
     * @param[out] stats The struct to populate with loaded data.
     * @return ESP_OK on success, or an error code.
     */
    virtual esp_err_t load_app_data(SolarStats& stats) = 0;

    /**
     * @brief Persists the application statistics and state.
     * @param[in] stats The struct containing the data to save.
     * @return ESP_OK on success, or an error code.
     */
    virtual esp_err_t save_app_data(const SolarStats& stats, bool force_nvs_commit = false) = 0;
};
