#pragma once

#include "gmock/gmock.h"
#include "interfaces/i_slow_sensors_task.hpp"

class MockSlowSensorsTask : public ISlowSensorsTask
{
public:
    MOCK_METHOD(esp_err_t, init, (), (override));
    MOCK_METHOD(esp_err_t, start, (), (override));
    MOCK_METHOD(void, stop, (), (override));
};
