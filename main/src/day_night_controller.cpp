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

bool DayNightController::should_enter_night_mode(
    uint16_t current_ma,
    bool is_time_synced,
    uint8_t current_hour_local,
    uint8_t current_minute_local,
    uint16_t day_of_year)
{
    if (current_ma >= config_.dusk_current_threshold_ma) {
        consecutive_dusk_samples_ = 0;
        return false;
    }

    if (is_time_synced) {
        SolarDayInfo day_info = calculate_solar_day(day_of_year);
        float current_time_float = static_cast<float>(current_hour_local) + (static_cast<float>(current_minute_local) / 60.0f);

        float window_start = day_info.sunset_hour_local - (static_cast<float>(config_.dusk_margin_before_sunset_min) / 60.0f);
        float window_end = day_info.sunset_hour_local + (static_cast<float>(config_.dusk_margin_after_sunset_min) / 60.0f);

        if (current_time_float < window_start || current_time_float > window_end) {
            // Outside dusk window (e.g. passing cloud at noon)
            consecutive_dusk_samples_ = 0;
            return false;
        }
    }

    consecutive_dusk_samples_++;
    return consecutive_dusk_samples_ >= config_.hysteresis_sample_count;
}

WakeType DayNightController::classify_wake(
    bool is_gpio_wakeup,
    uint16_t current_ma,
    bool is_time_synced,
    uint8_t current_hour_local) const
{
    if (is_gpio_wakeup) {
        return WakeType::DAWN_GPIO;
    }

    if (current_ma >= config_.dawn_current_threshold_ma) {
        return WakeType::DAWN_TIMER;
    }

    if (is_time_synced && current_hour_local == config_.calibration_wake_hour) {
        return WakeType::CALIBRATION_TIMER;
    }

    return WakeType::SPURIOUS_TIMER;
}

uint64_t DayNightController::calculate_night_sleep_time_us(
    bool is_time_synced,
    uint8_t current_hour_local,
    uint8_t current_minute_local,
    uint16_t day_of_year) const
{
    if (!is_time_synced) {
        return static_cast<uint64_t>(config_.fallback_sleep_sec) * 1000000ULL;
    }

    uint32_t current_sec = (static_cast<uint32_t>(current_hour_local) * 3600U) + (static_cast<uint32_t>(current_minute_local) * 60U);
    uint32_t calib_sec = static_cast<uint32_t>(config_.calibration_wake_hour) * 3600U;

    SolarDayInfo day_info = calculate_solar_day(day_of_year);
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
