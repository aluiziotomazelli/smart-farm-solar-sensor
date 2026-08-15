// main/include/solar_sensor_types.hpp
#pragma once
#include <cstdint>

#include "driver/gpio.h"

#include "farm_protocol_types.hpp"

// Production Configuration for XIAO-ESP32-C3 Mini Board
static constexpr gpio_num_t BATTERY_LEVEL_GPIO = GPIO_NUM_2; // D0 - Battery ADC (ADC1_CH2)
static constexpr gpio_num_t INA_ALERT_GPIO = GPIO_NUM_3;     // D1 - INA ALERT (RTC GPIO3, Deep Sleep Wakeup)
static constexpr gpio_num_t STATUS_LED_GPIO = GPIO_NUM_4;    // D2 - Status LED indicator
static constexpr gpio_num_t INA_VCC_GPIO = GPIO_NUM_5;       // D3 - INA VCC Power Control
static constexpr gpio_num_t I2C_SDA_GPIO = GPIO_NUM_6;       // D4 - I2C SDA (INA226)
static constexpr gpio_num_t I2C_SCL_GPIO = GPIO_NUM_7;       // D5 - I2C SCL (INA226)
static constexpr gpio_num_t DS18B20_GPIO = GPIO_NUM_8;       // D8 - DS18B20 1-Wire (Pull-up matches strapping HIGH)
static constexpr gpio_num_t BOOT_BUTTON_GPIO = GPIO_NUM_9;   // D9 - Boot button / Download Mode

// DayNightController Constants
static constexpr uint32_t DEFAULT_FALLBACK_NIGHT_SLEEP_SEC =
    3600;                                                  ///< 60 minutes fallback sleep when clock is not synced
static constexpr uint8_t DEFAULT_CALIBRATION_HOUR_UTC = 3; ///< 03:00 AM calibration hour
static constexpr uint8_t DEFAULT_DUSK_MARGIN_BEFORE_SUNSET_MIN = 30;
static constexpr uint16_t DEFAULT_HYSTERESIS_SAMPLE_COUNT = 15;        ///< ~2s confirmed darkness (clock synced)
static constexpr uint16_t DEFAULT_UNSYNCED_HYSTERESIS_SAMPLE_COUNT = 215; ///< ~30s confirmed darkness (unsynced)
static constexpr float DEFAULT_LATITUDE_DEG = -20.2074f;      ///< Default latitude
static constexpr float DEFAULT_TIMEZONE_OFFSET_HOURS = -4.0f; ///< Default timezone offset in hours (UTC-4)
