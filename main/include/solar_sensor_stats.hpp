// main/include/solar_sensor_stats.hpp
#pragma once

#include "farm_protocol_types.hpp"
#include "solar_sensor_types.hpp"

/**
 * @struct SolarSensorStats
 * @brief Persistent and runtime statistics for the Solar Sensor application.
 *
 * This structure tracks the current state of the sensor and accumulated
 * measurement statistics to help with diagnostics and logic decisions.
 */
struct SolarStats
{
    static constexpr uint16_t MAGIC = 0x534F; ///< For CRC validation | 0x534F4C = "SOL"
    static constexpr uint8_t VERSION = 1;

    // Magic first (validation)
    uint16_t magic = MAGIC;
    uint8_t version = VERSION;

    // Wake / sleep info
    bool gpio_wakeup_enabled = false;

    // --- Battery Stats ---
    uint16_t last_battery_mv = 0;
    uint8_t last_battery_percent = 0;
    farm::BatteryState last_battery_state = farm::BatteryState::UNKNOWN;

    // CRC MUST BE LAST of the validated fields
    uint32_t crc = 0;

    void reset()
    {
        *this = {};
        magic = MAGIC;
        version = VERSION;
    }

    bool operator==(const SolarStats& other) const
    {
        return magic == other.magic && version == other.version && gpio_wakeup_enabled == other.gpio_wakeup_enabled &&
               last_battery_mv == other.last_battery_mv && last_battery_percent == other.last_battery_percent &&
               last_battery_state == other.last_battery_state && crc == other.crc;
    }

    bool operator!=(const SolarStats& other) const { return !(*this == other); }
};
