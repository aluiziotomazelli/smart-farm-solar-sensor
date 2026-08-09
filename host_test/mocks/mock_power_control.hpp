#pragma once

#include "gmock/gmock.h"
#include "interfaces/i_power_control.hpp"

namespace power_control {

class MockPowerControl : public IPowerControl
{
public:
    MOCK_METHOD(esp_err_t, init, (), (override));
    MOCK_METHOD(esp_err_t, deinit, (), (override));
    MOCK_METHOD(esp_err_t, turn_on, (), (override));
    MOCK_METHOD(esp_err_t, turn_off, (), (override));
    MOCK_METHOD(esp_err_t, toggle, (), (override));
    MOCK_METHOD(esp_err_t, set_drive_capability, (gpio_drive_cap_t strength), (override));
    MOCK_METHOD(bool, is_on, (), (const, override));
    MOCK_METHOD(bool, is_initialized, (), (const, override));
    MOCK_METHOD(gpio_num_t, get_pin, (), (const, override));
};

} // namespace power_control
