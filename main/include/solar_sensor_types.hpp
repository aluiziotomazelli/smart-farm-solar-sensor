// main/include/solar_sensor_types.hpp
#pragma once
#include <cstdint>

#include "driver/gpio.h"

#include "farm_protocol_types.hpp"

// Production Configuration for XIAO-ESP32-C3 Mini Board
static constexpr gpio_num_t BATTERY_LEVEL_GPIO = GPIO_NUM_2; // D1
static constexpr gpio_num_t BOOT_BUTTON_GPIO = GPIO_NUM_9;   // D9 - Boot button
static constexpr gpio_num_t INA_VCC_GPIO = GPIO_NUM_5;       // D3 - INA VCC Power Control
static constexpr gpio_num_t INA_ALERT_GPIO = GPIO_NUM_10;    // D10
static constexpr gpio_num_t I2C_SDA_GPIO = GPIO_NUM_6;       // D4
static constexpr gpio_num_t I2C_SCL_GPIO = GPIO_NUM_7;       // D5

/**
 * @enum SolarNodeState
 * @brief High-level operational states for the Solar Sensor Node.
 */
enum class SolarNodeState : uint8_t
{
    DAY_ACTIVE = 0,  ///< Active daytime sampling (~8Hz) & ESP-NOW telemetry
    NIGHT_SLEEP = 1, ///< Low power night sleep, INA226 in Shunt Over Voltage wakeup mode
    OTA_UPDATE = 2   ///< Over-The-Air firmware update mode (high activity, WiFi connected)
};
