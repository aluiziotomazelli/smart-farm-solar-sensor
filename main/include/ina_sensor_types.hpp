#pragma once

#include <cstdint>
#include "esp_err.h"
#include "freertos/FreeRTOS.h"

#include "ina226_types.hpp"

/**
 * @brief Default dawn wakeup current threshold in mA.
 *
 * The INA226 SHUNT_OVER_VOLTAGE alert fires when the shunt current
 * exceeds this value, waking the MCU from deep sleep at dawn.
 */
static constexpr uint16_t DEFAULT_DAWN_ALERT_CURRENT_MA = 5;

/** Current below which the node considers it to be dusk */
static constexpr uint16_t DEFAULT_DUSK_CURRENT_MA = 1;

/** Current above which the node considers it to be day */
static constexpr uint16_t DEFAULT_DAWN_CURRENT_THRESHOLD_MA = 5;

struct InaSample
{
    uint16_t isc_current_ma = 0;  ///< Instantaneous short-circuit current in mA
    int32_t shunt_voltage_uv = 0; ///< Raw measured shunt voltage in uV
    bool delta_detected = false;  ///< Flag indicating change exceeding delta threshold
    esp_err_t status = ESP_OK;    ///< ESP_OK or I2C/INA226 hardware error status
    int64_t timestamp_us = 0;     ///< Microsecond timestamp of reading
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
 * dawn_alert_current_ma) is applied by InaSensorTask::prepare_for_sleep().
 */
struct InaNightConfig
{
    ina226::ConversionTime vsh_ct{ina226::ConversionTime::CT_8244US};
    ina226::ConversionTime vbus_ct{ina226::ConversionTime::CT_8244US};
    ina226::AveragingMode avg_mode{ina226::AveragingMode::AVG_1024};
};

struct InaSensorConfig
{
    uint16_t delta_threshold_ma = 10;      ///< Absolute current delta threshold in mA
    float delta_threshold_percent = 0.03f; ///< Relative current delta threshold (3%)
    uint16_t heartbeat_interval_ms = 5000; ///< Heartbeat report interval in ms (5s)
    bool enable_ema_filter = true;         ///< Enables Exponential Moving Average (EMA) filtering on raw current
    float ema_alpha = 0.8f;          ///< EMA smoothing factor (0.0f = static, 1.0f = raw current, 0.8f = fast response)
    uint32_t task_stack_size = 4096; ///< FreeRTOS task stack size in bytes
    UBaseType_t task_priority = 5;   ///< FreeRTOS task priority
    uint16_t dawn_alert_current_ma = DEFAULT_DAWN_ALERT_CURRENT_MA; ///< INA dawn wakeup current threshold in mA

    InaNightConfig night_config{};
};
