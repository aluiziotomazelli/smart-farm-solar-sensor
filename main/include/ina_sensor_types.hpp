#pragma once

#include <cstdint>
#include "esp_err.h"

struct InaSample
{
    uint16_t isc_current_ma{0};   ///< Instantaneous short-circuit current in mA
    uint16_t bus_voltage_mv{0};   ///< Measured bus/shunt voltage in mV
    bool delta_detected{false};   ///< Flag indicating change exceeding delta threshold
    esp_err_t status{ESP_OK};     ///< ESP_OK or I2C/INA226 hardware error status
    int64_t timestamp_us{0};      ///< Microsecond timestamp of reading
};

struct InaSensorConfig
{
    uint16_t sample_interval_ms{125};       ///< Sampling interval in ms (default ~8Hz)
    uint16_t delta_threshold_ma{10};        ///< Absolute current delta threshold in mA
    float delta_threshold_percent{0.03f};   ///< Relative current delta threshold (3%)
    uint16_t heartbeat_interval_ms{1000};   ///< Heartbeat report interval in ms
};
