#pragma once

#include <cstdint>
#include "esp_err.h"
#include "freertos/FreeRTOS.h"

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

/** Current below which the node considers it to be dusk */
static constexpr uint16_t DEFAULT_DUSK_CURRENT_MA = 1;

/** Current above which the node considers it to be day */
static constexpr uint16_t DEFAULT_DAWN_CURRENT_THRESHOLD_MA = 5;

struct InaSample
{
    uint16_t isc_current_ma = 0; ///< Instantaneous short-circuit current in mA
    int32_t shunt_voltage_uv = 0;///< Raw measured shunt voltage in uV
    bool delta_detected = false; ///< Flag indicating change exceeding delta threshold
    esp_err_t status = ESP_OK;   ///< ESP_OK or I2C/INA226 hardware error status
    int64_t timestamp_us = 0;    ///< Microsecond timestamp of reading
};

/**
 * @struct InaNightConfig
 * @brief Conversion settings applied to the INA226 when preparing for sleep.
 *
 * The daytime regime is the default and therefore lives in the Ina226Driver
 * construction (main.cpp): InaSensorTask::init() arms ALERT_ON_CONVERSION_READY
 * (CNVR) so the ALERT pin asserts on every completed conversion. During the
 * night regime only the conversion settings change (slower conversions to save
 * power); the dawn wakeup alert (SHUNT_OVER_VOLTAGE with
 * DEFAULT_DAWN_WAKEUP_ALERT_LIMIT) is applied by InaSensorTask::prepare_for_sleep().
 */
struct InaNightConfig
{
    ina226::ConversionTime vsh_ct{ina226::ConversionTime::CT_8244US};
    ina226::ConversionTime vbus_ct{ina226::ConversionTime::CT_8244US};
    ina226::AveragingMode avg_mode{ina226::AveragingMode::AVG_64};
};

struct InaSensorConfig
{
    uint16_t delta_threshold_ma = 10;      ///< Absolute current delta threshold in mA
    float delta_threshold_percent = 0.03f; ///< Relative current delta threshold (3%)
    uint16_t heartbeat_interval_ms = 1000; ///< Heartbeat report interval in ms
    uint32_t task_stack_size = 4096;       ///< FreeRTOS task stack size in bytes
    UBaseType_t task_priority = 5;         ///< FreeRTOS task priority

    InaNightConfig night_config{};
};
