// main/include/solar_sensor_types.hpp
#pragma once
#include <cstdint>

#include "farm_protocol_types.hpp"

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
