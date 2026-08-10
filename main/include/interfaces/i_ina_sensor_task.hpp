#pragma once

#include "esp_err.h"
#include "interfaces/i_hal_i2c.hpp"
#include "ina_sensor_types.hpp"
#include "solar_sensor_types.hpp"

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
    virtual esp_err_t set_operating_mode(SolarNodeState mode) = 0;
    virtual SolarNodeState get_operating_mode() const = 0;

    virtual uint32_t get_expected_sample_period_ms() const = 0;
    virtual uint32_t get_watchdog_timeout_ms() const = 0;

    virtual TaskHandle_t get_task_handle() const = 0;
};

} // namespace ina
