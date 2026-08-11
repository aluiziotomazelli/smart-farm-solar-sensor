#pragma once

#include <cstdint>
#include <mutex>
#include "farm_protocol_types.hpp"

struct TelemetrySnapshotData
{
    uint16_t battery_mv{0};
    uint8_t battery_percent{0};
    farm::BatteryState battery_state{farm::BatteryState::UNKNOWN};
    uint16_t max_current_ma{0};
    uint32_t daily_yield_mah{0};
    bool is_night_mode{false};
};

class TelemetrySnapshot
{
public:
    void update_battery(uint16_t mv, uint8_t percent, farm::BatteryState state)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        data_.battery_mv = mv;
        data_.battery_percent = percent;
        data_.battery_state = state;
    }

    void update_stats(uint16_t max_current_ma, uint32_t daily_yield_mah)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        data_.max_current_ma = max_current_ma;
        data_.daily_yield_mah = daily_yield_mah;
    }

    void set_night_mode(bool is_night)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        data_.is_night_mode = is_night;
    }

    TelemetrySnapshotData get() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return data_;
    }

private:
    mutable std::mutex mutex_;
    TelemetrySnapshotData data_{};
};
