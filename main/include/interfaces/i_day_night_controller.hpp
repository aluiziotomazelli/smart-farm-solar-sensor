// main/include/interfaces/i_day_night_controller.hpp
#pragma once

#include <cstdint>
#include <ctime>
#include <optional>

#include "sun_schedule.hpp"

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
 * @class IDayNightController
 * @brief Interface for Day/Night transitions, solar calculations, and sleep timing.
 */
class IDayNightController
{
public:
    virtual ~IDayNightController() = default;

    /**
     * @brief Evaluates whether node should transition from DAY to NIGHT mode (Dusk).
     * @param current_ma Instantaneous current in mA.
     * @param unix_time Optional Unix timestamp in seconds (if clock is synchronized).
     * @return true if dusk conditions are met, false otherwise.
     */
    virtual bool should_enter_night_mode(
        uint16_t current_ma,
        std::optional<time_t> unix_time) = 0;

    /**
     * @brief Classifies a sleep wakeup event.
     * @param is_gpio_wakeup True if woken by GPIO (INA ALERT pin).
     * @param current_ma Measured current in mA after wakeup.
     * @param unix_time Optional Unix timestamp in seconds (if clock is synchronized).
     * @return WakeType classification.
     */
    virtual WakeType classify_wake(
        bool is_gpio_wakeup,
        uint16_t current_ma,
        std::optional<time_t> unix_time) const = 0;

    /**
     * @brief Calculates next night deep sleep duration in microseconds.
     * @param unix_time Optional Unix timestamp in seconds (if clock is synchronized).
     * @return Sleep duration in microseconds.
     */
    virtual uint64_t calculate_night_sleep_time_us(
        std::optional<time_t> unix_time) const = 0;

    /**
     * @brief Resets hysteresis counter for dusk detection.
     */
    virtual void reset_hysteresis() = 0;

    /**
     * @brief Computes solar day info (day length, sunrise hour, sunset hour) for a given day of year.
     * @param day_of_year Day of year (1-365).
     * @return SolarDayInfo struct.
     */
    virtual SolarDayInfo calculate_solar_day(uint16_t day_of_year) const = 0;
};
