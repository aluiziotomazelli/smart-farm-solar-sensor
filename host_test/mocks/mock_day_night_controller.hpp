// host_test/mocks/mock_day_night_controller.hpp
#pragma once

#include "gmock/gmock.h"
#include "interfaces/i_day_night_controller.hpp"

class MockDayNightController : public IDayNightController
{
public:
    MOCK_METHOD(bool, should_enter_night_mode, (uint16_t current_ma, std::optional<time_t> unix_time), (override));
    MOCK_METHOD(
        WakeType,
        classify_wake,
        (bool is_gpio_wakeup, uint16_t current_ma, std::optional<time_t> unix_time),
        (const, override));
    MOCK_METHOD(uint64_t, calculate_night_sleep_time_us, (std::optional<time_t> unix_time), (const, override));
    MOCK_METHOD(void, reset_hysteresis, (), (override));
    MOCK_METHOD(SolarDayInfo, calculate_solar_day, (uint16_t day_of_year), (const, override));
};
