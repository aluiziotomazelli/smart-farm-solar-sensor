// main/include/solar_sensor_stats.hpp
#pragma once

#include "farm_protocol_types.hpp"
#include "solar_sensor_types.hpp"

/**
 * @struct SolarStats
 * @brief Persistent and runtime statistics for the Solar Sensor application.
 *
 * This structure tracks the current state of the sensor and accumulated
 * measurement statistics to help with diagnostics and logic decisions.
 */
struct SolarStats
{
    static constexpr uint16_t MAGIC = 0x534F; ///< For CRC validation | 0x534F = "SO"
    static constexpr uint8_t VERSION = 1;

    // Magic first (validation)
    uint16_t magic = MAGIC;
    uint8_t version = VERSION;

    // Wake / sleep info
    bool gpio_wakeup_enabled = false;
    bool is_night_mode = false;

    // --- Battery Stats ---
    uint16_t last_battery_mv = 0;
    uint8_t last_battery_percent = 0;
    farm::BatteryState last_battery_state = farm::BatteryState::UNKNOWN;

    // --- Solar Metrics & Daily Accumulators ---
    uint16_t max_current_ma = 0;
    uint16_t min_day_current_ma = 0;
    uint32_t daily_yield_mah = 0;
    int16_t  shunt_zero_offset_uv = 0;

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
        return magic == other.magic &&
               version == other.version &&
               gpio_wakeup_enabled == other.gpio_wakeup_enabled &&
               is_night_mode == other.is_night_mode &&
               last_battery_mv == other.last_battery_mv &&
               last_battery_percent == other.last_battery_percent &&
               last_battery_state == other.last_battery_state &&
               max_current_ma == other.max_current_ma &&
               min_day_current_ma == other.min_day_current_ma &&
               daily_yield_mah == other.daily_yield_mah &&
               shunt_zero_offset_uv == other.shunt_zero_offset_uv &&
               crc == other.crc;
    }

    bool operator!=(const SolarStats& other) const { return !(*this == other); }
};
