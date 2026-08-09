#pragma once

#include "esp_err.h"
#include "ina_sensor_types.hpp"

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

    virtual esp_err_t hard_reset_ina_power() = 0;
};

} // namespace ina
