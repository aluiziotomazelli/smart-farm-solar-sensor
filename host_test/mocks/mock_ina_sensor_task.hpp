#pragma once

#include "gmock/gmock.h"
#include "interfaces/i_ina_sensor_task.hpp"

namespace ina {

class MockInaSensorTask : public IInaSensorTask
{
public:
    MOCK_METHOD(esp_err_t, init, (const InaSensorConfig& config), (override));
    MOCK_METHOD(esp_err_t, start, (), (override));
    MOCK_METHOD(void, stop, (), (override));
    MOCK_METHOD(void, set_reporting_enabled, (bool enabled), (override));
    MOCK_METHOD(void, set_sampling_enabled, (bool enabled), (override));
    MOCK_METHOD(esp_err_t, hard_reset_ina_power, (), (override));
};

} // namespace ina
