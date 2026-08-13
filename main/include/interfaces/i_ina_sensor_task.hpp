#pragma once

#include "esp_err.h"
#include "interfaces/i_hal_i2c.hpp"
#include "ina_sensor_types.hpp"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace ina {

class IInaSensorTask
{
public:
    virtual ~IInaSensorTask() = default;

    virtual esp_err_t init(const InaSensorConfig& config, i2c_master_bus_handle_t i2c_bus) = 0;
    virtual esp_err_t start() = 0;
    virtual void stop() = 0;

    virtual void set_reporting_enabled(bool enabled) = 0;
    virtual void set_sampling_enabled(bool enabled) = 0;
    virtual void set_shunt_zero_offset_uv(int16_t offset_uv) = 0;

    /**
     * @brief Prepares the sensor for the night deep-sleep regime.
     *
     * Disables sampling and reporting, applies the night conversion settings
     * (InaSensorConfig::night_config) and arms the dawn wakeup alert
     * (SHUNT_OVER_VOLTAGE with DEFAULT_DAWN_WAKEUP_ALERT_LIMIT) so the ALERT
     * pin wakes the MCU from deep sleep. Call it right before entering sleep.
     * @return ESP_OK on success, or ESP_ERR_* on I2C failure.
     */
    virtual esp_err_t prepare_for_sleep() = 0;

    /**
     * @brief Reports whether sampling is currently enabled.
     *
     * Sampling is the "active regime" signal: while enabled the app expects
     * periodic samples and keeps the watchdog armed; prepare_for_sleep()
     * disables it before deep sleep.
     * @return true when sampling is enabled.
     */
    virtual bool is_sampling_enabled() const = 0;

    virtual uint32_t get_expected_sample_period_ms() const = 0;
    virtual uint32_t get_watchdog_timeout_ms() const = 0;

    virtual TaskHandle_t get_task_handle() const = 0;
};

} // namespace ina
