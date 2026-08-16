#pragma once

#include <cmath>
#include <cstdint>
#include <ctime>
#include <optional>

#include "ina_sensor_types.hpp"
#include "solar_sensor_types.hpp"

/**
 * @struct SolarDayInfo
 * @brief Astronomical sun calculation results for a given day of year.
 */
struct SolarDayInfo
{
    float day_length_hours = 12.0f;   ///< Total day length in hours
    float sunrise_hour_local = 6.0f;  ///< Calculated local sunrise hour (0.0 - 24.0)
    float sunset_hour_local = 18.0f;  ///< Calculated local sunset hour (0.0 - 24.0)
};

/**
 * @enum WakeType
 * @brief Classification of deep sleep wakeup events.
 */
enum class WakeType : uint8_t
{
    DAWN_GPIO,         ///< Woken up by INA226 ALERT pin (current exceeded threshold)
    DAWN_TIMER,        ///< Woken up by timer during daylight (current > dawn threshold)
    CALIBRATION_TIMER, ///< Woken up by timer at calibration hour (3 AM) in darkness
    SPURIOUS_TIMER     ///< Woken up by timer at night outside calibration (stay in night sleep)
};

/**
 * @struct DayNightConfig
 * @brief Configuration parameters for DayNightController.
 */
struct DayNightConfig
{
    uint16_t dusk_current_threshold_ma = DEFAULT_DUSK_CURRENT_MA;
    uint16_t dawn_current_threshold_ma = DEFAULT_DAWN_CURRENT_THRESHOLD_MA;
    uint8_t calibration_wake_hour = DEFAULT_CALIBRATION_HOUR_UTC;
    uint32_t fallback_sleep_sec = DEFAULT_FALLBACK_NIGHT_SLEEP_SEC;
    uint16_t hysteresis_sample_count = DEFAULT_HYSTERESIS_SAMPLE_COUNT;
    uint16_t unsynced_hysteresis_sample_count = DEFAULT_UNSYNCED_HYSTERESIS_SAMPLE_COUNT;
    float latitude_deg = DEFAULT_LATITUDE_DEG;
    float tz_offset_hours = DEFAULT_TIMEZONE_OFFSET_HOURS;
    uint8_t dusk_margin_before_sunset_min = DEFAULT_DUSK_MARGIN_BEFORE_SUNSET_MIN;
};

/**
 * @class DayNightController
 * @brief Pure domain logic for Day/Night transitions, solar calculations, and sleep timing.
 */
class DayNightController
{
public:
    explicit DayNightController(const DayNightConfig& config = {});

    /**
     * @brief Computes solar day info (day length, sunrise hour, sunset hour) for a given day of year.
     * @param day_of_year Day of year (1-365).
     * @return SolarDayInfo struct.
     */
    SolarDayInfo calculate_solar_day(uint16_t day_of_year) const;

    /**
     * @brief Evaluates whether node should transition from DAY to NIGHT mode (Dusk).
     * @param current_ma Instantaneous current in mA.
     * @param unix_time Optional Unix timestamp in seconds (if clock is synchronized).
     * @return true if dusk conditions are met, false otherwise.
     */
    bool should_enter_night_mode(
        uint16_t current_ma,
        std::optional<time_t> unix_time);

    /**
     * @brief Classifies a sleep wakeup event.
     * @param is_gpio_wakeup True if woken by GPIO (INA ALERT pin).
     * @param current_ma Measured current in mA after wakeup.
     * @param unix_time Optional Unix timestamp in seconds (if clock is synchronized).
     * @return WakeType classification.
     */
    WakeType classify_wake(
        bool is_gpio_wakeup,
        uint16_t current_ma,
        std::optional<time_t> unix_time) const;

    /**
     * @brief Calculates next night deep sleep duration in microseconds.
     * @param unix_time Optional Unix timestamp in seconds (if clock is synchronized).
     * @return Sleep duration in microseconds.
     */
    uint64_t calculate_night_sleep_time_us(
        std::optional<time_t> unix_time) const;

    /**
     * @brief Resets hysteresis counter for dusk detection.
     */
    void reset_hysteresis();

    const DayNightConfig& get_config() const { return config_; }
    void set_config(const DayNightConfig& config) { config_ = config; }

private:
    struct LocalTime
    {
        uint8_t hour = 0;
        uint8_t minute = 0;
        uint16_t day_of_year = 81;
    };

    LocalTime decompose(time_t unix_time) const;

    DayNightConfig config_;
    uint16_t consecutive_dusk_samples_ = 0;
};
