#pragma once

#include "esp_err.h"
#include "ina_sensor_types.hpp"
#include "solar_sensor_types.hpp"

namespace ina {

class IInaSensorTask
{
public:
    virtual ~IInaSensorTask() = default;

    virtual esp_err_t init(const InaSensorConfig& config) = 0;
    virtual esp_err_t start() = 0;
    virtual void stop() = 0;

    virtual void set_reporting_enabled(bool enabled) = 0;
    virtual void set_sampling_enabled(bool enabled) = 0;
    virtual esp_err_t set_operating_mode(SolarNodeState mode) = 0;
    virtual SolarNodeState get_operating_mode() const = 0;

    virtual uint32_t get_expected_sample_period_ms() const = 0;
    virtual uint32_t get_watchdog_timeout_ms() const = 0;

    virtual esp_err_t hard_reset_ina_power() = 0;
};

} // namespace ina
