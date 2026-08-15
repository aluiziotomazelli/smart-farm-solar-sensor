#include "day_night_controller.hpp"
#include <algorithm>

DayNightController::DayNightController(const DayNightConfig& config)
    : config_(config)
{
}

SolarDayInfo DayNightController::calculate_solar_day(uint16_t day_of_year) const
{
    constexpr float DEG_TO_RAD = 3.14159265358979323846f / 180.0f;
    constexpr float RAD_TO_DEG = 180.0f / 3.14159265358979323846f;

    // Solar declination (Cooper's equation)
    float declination_deg = 23.45f * std::sin((360.0f / 365.0f) * static_cast<float>(day_of_year - 81) * DEG_TO_RAD);

    float lat_rad = config_.latitude_deg * DEG_TO_RAD;
    float dec_rad = declination_deg * DEG_TO_RAD;

    float cos_h0 = -std::tan(lat_rad) * std::tan(dec_rad);

    SolarDayInfo info;
    if (cos_h0 >= 1.0f) {
        info.day_length_hours = 0.0f;
        info.sunrise_hour_local = 12.0f;
        info.sunset_hour_local = 12.0f;
        return info;
    }
    if (cos_h0 <= -1.0f) {
        info.day_length_hours = 24.0f;
        info.sunrise_hour_local = 0.0f;
        info.sunset_hour_local = 24.0f;
        return info;
    }

    float h0_deg = std::acos(cos_h0) * RAD_TO_DEG;
    info.day_length_hours = (2.0f * h0_deg) / 15.0f;

    float solar_noon = 12.0f;
    info.sunrise_hour_local = solar_noon - (info.day_length_hours / 2.0f);
    info.sunset_hour_local = solar_noon + (info.day_length_hours / 2.0f);

    return info;
}

DayNightController::LocalTime DayNightController::decompose(time_t unix_time) const
{
    // Apply timezone offset configured in config_
    int64_t offset_sec = static_cast<int64_t>(config_.tz_offset_hours * 3600.0f);
    time_t local_unix = unix_time + offset_sec;

    struct tm tm_info{};
    gmtime_r(&local_unix, &tm_info);

    return {
        static_cast<uint8_t>(tm_info.tm_hour),
        static_cast<uint8_t>(tm_info.tm_min),
        static_cast<uint16_t>(tm_info.tm_yday + 1)
    };
}

bool DayNightController::should_enter_night_mode(
    uint16_t current_ma,
    std::optional<time_t> unix_time)
{
    if (current_ma >= config_.dusk_current_threshold_ma) {
        consecutive_dusk_samples_ = 0;
        return false;
    }

    uint16_t required_samples = config_.hysteresis_sample_count;

    if (unix_time.has_value()) {
        LocalTime lt = decompose(*unix_time);
        SolarDayInfo day_info = calculate_solar_day(lt.day_of_year);
        float current_time_float = static_cast<float>(lt.hour) + (static_cast<float>(lt.minute) / 60.0f);
        float dusk_start = day_info.sunset_hour_local - (static_cast<float>(config_.dusk_margin_before_sunset_min) / 60.0f);
        float sunrise = day_info.sunrise_hour_local;

        // In broad daylight (between sunrise and dusk onset), a current drop is treated as transient cloud/shadow
        bool is_broad_daylight = (current_time_float >= sunrise && current_time_float < dusk_start);
        if (is_broad_daylight) {
            consecutive_dusk_samples_ = 0;
            return false;
        }
    } else {
        required_samples = config_.unsynced_hysteresis_sample_count;
    }

    consecutive_dusk_samples_++;
    return consecutive_dusk_samples_ >= required_samples;
}

WakeType DayNightController::classify_wake(
    bool is_gpio_wakeup,
    uint16_t current_ma,
    std::optional<time_t> unix_time) const
{
    if (unix_time.has_value()) {
        LocalTime lt = decompose(*unix_time);
        SolarDayInfo day_info = calculate_solar_day(lt.day_of_year);
        float current_time = static_cast<float>(lt.hour) + (static_cast<float>(lt.minute) / 60.0f);

        // Dawn window: from 30 min before calculated sunrise until noon (12:00)
        float dawn_window_start = day_info.sunrise_hour_local - 0.5f;
        bool in_dawn_window = (current_time >= dawn_window_start && current_time < 12.0f);

        if (is_gpio_wakeup) {
            // GPIO outside the dawn window (e.g. at dusk or midnight) is noise -> spurious wake
            return in_dawn_window ? WakeType::DAWN_GPIO : WakeType::SPURIOUS_TIMER;
        }

        if (in_dawn_window && current_ma >= config_.dawn_current_threshold_ma) {
            return WakeType::DAWN_TIMER;
        }

        if (lt.hour == config_.calibration_wake_hour) {
            return WakeType::CALIBRATION_TIMER;
        }

        return WakeType::SPURIOUS_TIMER;
    }

    // No time sync: fall back to threshold heuristic
    if (is_gpio_wakeup && current_ma >= config_.dawn_current_threshold_ma) {
        return WakeType::DAWN_GPIO;
    }

    if (current_ma >= config_.dawn_current_threshold_ma) {
        return WakeType::DAWN_TIMER;
    }

    return WakeType::SPURIOUS_TIMER;
}

uint64_t DayNightController::calculate_night_sleep_time_us(
    std::optional<time_t> unix_time) const
{
    if (!unix_time.has_value()) {
        return static_cast<uint64_t>(config_.fallback_sleep_sec) * 1000000ULL;
    }

    LocalTime lt = decompose(*unix_time);
    uint32_t current_sec = (static_cast<uint32_t>(lt.hour) * 3600U) + (static_cast<uint32_t>(lt.minute) * 60U);
    uint32_t calib_sec = static_cast<uint32_t>(config_.calibration_wake_hour) * 3600U;

    SolarDayInfo day_info = calculate_solar_day(lt.day_of_year);
    uint32_t sunrise_sec = static_cast<uint32_t>(day_info.sunrise_hour_local * 3600.0f);

    uint32_t target_sleep_sec = config_.fallback_sleep_sec;

    if (current_sec < calib_sec) {
        // Before 03:00 AM (e.g. late night), sleep until 03:00 AM calibration
        target_sleep_sec = calib_sec - current_sec;
    }
    else if (current_sec >= calib_sec && current_sec < sunrise_sec) {
        // Between 03:00 AM and sunrise, sleep until sunrise
        target_sleep_sec = sunrise_sec - current_sec;
    }
    else {
        // At or after dusk (e.g. 19:00), sleep until 03:00 AM next day
        uint32_t sec_until_midnight = (24U * 3600U) - current_sec;
        target_sleep_sec = sec_until_midnight + calib_sec;
    }

    if (target_sleep_sec == 0) {
        target_sleep_sec = config_.fallback_sleep_sec;
    }

    return static_cast<uint64_t>(target_sleep_sec) * 1000000ULL;
}

void DayNightController::reset_hysteresis()
{
    consecutive_dusk_samples_ = 0;
}
