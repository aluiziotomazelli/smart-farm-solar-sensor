// host_test/mocks/mock_led_controller.hpp
#pragma once

#include "gmock/gmock.h"
#include "interfaces/i_led_controller.hpp"

class MockLedController : public ILedController
{
public:
    MOCK_METHOD(esp_err_t, init, (), (override));
    MOCK_METHOD(esp_err_t, start, (), (override));
    MOCK_METHOD(void, stop, (), (override));
    MOCK_METHOD(void, set_pattern, (BlinkPattern pattern), (override));
    MOCK_METHOD(void, pulse, (uint16_t duration_ms), (override));
    MOCK_METHOD(BlinkPattern, get_current_pattern, (), (const, override));
};
