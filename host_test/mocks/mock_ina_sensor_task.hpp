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
    MOCK_METHOD(void, set_shunt_zero_offset_uv, (int16_t offset_uv), (override));
    MOCK_METHOD(esp_err_t, prepare_for_sleep, (), (override));
    MOCK_METHOD(bool, is_sampling_enabled, (), (const, override));
    MOCK_METHOD(uint32_t, get_expected_sample_period_ms, (), (const, override));
    MOCK_METHOD(uint32_t, get_watchdog_timeout_ms, (), (const, override));
    MOCK_METHOD(TaskHandle_t, get_task_handle, (), (const, override));
};

} // namespace ina
