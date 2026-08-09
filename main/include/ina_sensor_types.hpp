#pragma once

#include <cstdint>
#include "esp_err.h"
#include "ina226_types.hpp"

/**
 * @brief Default Shunt Over Voltage raw threshold for dawn wakeup detection.
 *
 * Calculation:
 *   - Shunt resistor: R_shunt = 0.1 Ohm (100 mOhm)
 *   - Target dawn wakeup current: I_dawn = ~0.3 mA (300 uA)
 *   - Shunt Voltage: V_sh = I_dawn * R_shunt = 300 uA * 0.1 Ohm = 30 uV
 *   - INA226 Shunt Voltage LSB: 2.5 uV per LSB
 *   - Raw ALERT_LIMIT = 30 uV / 2.5 uV = 12
 */
static constexpr uint16_t DEFAULT_DAWN_WAKEUP_ALERT_LIMIT = 12;

struct InaSample
{
    uint16_t isc_current_ma{0};   ///< Instantaneous short-circuit current in mA
    uint16_t bus_voltage_mv{0};   ///< Measured bus/shunt voltage in mV
    bool delta_detected{false};   ///< Flag indicating change exceeding delta threshold
    esp_err_t status{ESP_OK};     ///< ESP_OK or I2C/INA226 hardware error status
    int64_t timestamp_us{0};      ///< Microsecond timestamp of reading
};

/**
 * @struct InaModeConfig
 * @brief Hardware configuration parameters for a specific operating regime (Day/Night).
 */
struct InaModeConfig
{
    ina226::ConversionTime vsh_ct{ina226::ConversionTime::CT_1100US};
    ina226::ConversionTime vbus_ct{ina226::ConversionTime::CT_1100US};
    ina226::AveragingMode avg_mode{ina226::AveragingMode::AVG_64};
    ina226::AlertFlag alert_flag{ina226::AlertFlag::CONVERSION_READY};
    uint16_t alert_limit{0};
};

struct InaSensorConfig
{
    uint16_t sample_interval_ms{125};       ///< Sampling interval in ms (default ~8Hz)
    uint16_t delta_threshold_ma{10};        ///< Absolute current delta threshold in mA
    float delta_threshold_percent{0.03f};   ///< Relative current delta threshold (3%)
    uint16_t heartbeat_interval_ms{1000};   ///< Heartbeat report interval in ms

    InaModeConfig day_config{
        ina226::ConversionTime::CT_1100US,
        ina226::ConversionTime::CT_1100US,
        ina226::AveragingMode::AVG_64,
        ina226::AlertFlag::CONVERSION_READY,
        0
    };

    InaModeConfig night_config{
        ina226::ConversionTime::CT_8244US,
        ina226::ConversionTime::CT_8244US,
        ina226::AveragingMode::AVG_64,
        ina226::AlertFlag::SHUNT_OVER_VOLTAGE,
        DEFAULT_DAWN_WAKEUP_ALERT_LIMIT
    };
};
