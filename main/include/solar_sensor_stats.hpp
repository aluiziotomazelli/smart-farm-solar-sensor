// main/include/solar_sensor_stats.hpp
#pragma once

#include <cstdint>

#include "app_storage.hpp"
#include "farm_protocol_types.hpp"
#include "solar_sensor_types.hpp"

// =============================
//  Solar Sensor Storage Constants
// =============================
static constexpr uint32_t SOLAR_STATS_MAGIC = 0x534F4C52; ///< "SOLR"
static constexpr uint8_t SOLAR_STATS_VERSION = 1;

/**
 * @struct SolarStats
 * @brief Pure domain struct representing statistics and state for the Solar Sensor application.
 *
 * Contains no storage metadata (magic, version, crc) which are managed by the AppStorage envelope.
 */
struct SolarStats
{
    // Wake / sleep info
    bool gpio_wakeup_enabled = false;
    bool is_night_mode = false;

    // --- Battery Stats ---
    uint16_t last_battery_mv = 0;
    uint8_t last_battery_percent = 0;
    farm::BatteryState last_battery_state = farm::BatteryState::UNKNOWN;

    // --- Solar Metrics & Daily Accumulators ---
    uint16_t max_day_current_ma = 0;
    uint32_t daily_yield_mah = 0;
    int16_t shunt_zero_offset_uv = 0;

    void reset() { *this = {}; }

    bool operator==(const SolarStats& other) const
    {
        return gpio_wakeup_enabled == other.gpio_wakeup_enabled && is_night_mode == other.is_night_mode &&
               last_battery_mv == other.last_battery_mv &&
               last_battery_percent == other.last_battery_percent &&
               last_battery_state == other.last_battery_state &&
               max_day_current_ma == other.max_day_current_ma && daily_yield_mah == other.daily_yield_mah &&
               shunt_zero_offset_uv == other.shunt_zero_offset_uv;
    }

    bool operator!=(const SolarStats& other) const { return !(*this == other); }
};

/**
 * @brief Storage envelope alias used for allocating physical RTC/NVS storage buffers.
 */
using SolarStorage = StorageEnvelope<SolarStats, SOLAR_STATS_MAGIC, SOLAR_STATS_VERSION>;
