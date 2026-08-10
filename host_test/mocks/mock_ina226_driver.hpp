#pragma once

#include "gmock/gmock.h"
#include "interfaces/i_ina226_driver.hpp"

namespace ina226 {

class MockIna226Driver : public IIna226Driver
{
public:
    MOCK_METHOD(esp_err_t, init, (i2c_master_bus_handle_t bus_handle), (override));
    MOCK_METHOD(esp_err_t, init, (), (override));
    MOCK_METHOD(esp_err_t, reset, (), (override));
    MOCK_METHOD(esp_err_t, read_shunt_voltage_uv, (int32_t & out_uv), (override));
    MOCK_METHOD(esp_err_t, read_bus_voltage_mv, (uint16_t & out_mv), (override));
    MOCK_METHOD(esp_err_t, read_current_ma, (float& out_ma), (override));
    MOCK_METHOD(esp_err_t, read_power_mw, (float& out_mw), (override));
    MOCK_METHOD(esp_err_t, calibrate, (float r_shunt_ohms, float max_expected_current_a), (override));
    MOCK_METHOD(esp_err_t, configure_alert, (uint16_t alert_mask, uint16_t alert_limit), (override));
    MOCK_METHOD(esp_err_t, is_conversion_ready, (bool& out_ready), (override));
    MOCK_METHOD(const Ina226Config&, get_config, (), (const, override));
};

} // namespace ina226
