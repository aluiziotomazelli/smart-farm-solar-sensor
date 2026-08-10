#pragma once

#include "gmock/gmock.h"
#include "interfaces/i_ina_sensor_task.hpp"

namespace ina {

class MockInaSensorTask : public IInaSensorTask
{
public:
    MOCK_METHOD(esp_err_t, init, (const InaSensorConfig& config, i2c_master_bus_handle_t i2c_bus), (override));
    MOCK_METHOD(esp_err_t, start, (), (override));
    MOCK_METHOD(void, stop, (), (override));
    MOCK_METHOD(void, set_reporting_enabled, (bool enabled), (override));
    MOCK_METHOD(void, set_sampling_enabled, (bool enabled), (override));
    MOCK_METHOD(esp_err_t, set_operating_mode, (SolarNodeState mode), (override));
    MOCK_METHOD(SolarNodeState, get_operating_mode, (), (const, override));
    MOCK_METHOD(uint32_t, get_expected_sample_period_ms, (), (const, override));
    MOCK_METHOD(uint32_t, get_watchdog_timeout_ms, (), (const, override));
};

} // namespace ina
