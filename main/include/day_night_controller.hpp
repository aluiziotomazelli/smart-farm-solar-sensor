// main/include/day_night_controller.hpp
#pragma once

#include <cmath>
#include <cstdint>
#include <ctime>
#include <optional>

#include "interfaces/i_day_night_controller.hpp"
#include "ina_sensor_types.hpp"
#include "solar_sensor_types.hpp"
#include "sun_schedule.hpp"

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
    uint8_t dusk_margin_before_sunset_min = DEFAULT_DUSK_MARGIN_BEFORE_SUNSET_MIN;
};

/**
 * @class DayNightController
 * @brief Pure domain logic for Day/Night transitions, solar calculations, and sleep timing.
 */
class DayNightController : public IDayNightController
{
public:
    explicit DayNightController(
        const SunSchedule& sun_schedule,
        const DayNightConfig& config = {});

    /** @copydoc IDayNightController::should_enter_night_mode */
    bool should_enter_night_mode(
        uint16_t current_ma,
        std::optional<time_t> unix_time) override;

    /** @copydoc IDayNightController::classify_wake */
    WakeType classify_wake(
        bool is_gpio_wakeup,
        uint16_t current_ma,
        std::optional<time_t> unix_time) const override;

    /** @copydoc IDayNightController::calculate_night_sleep_time_us */
    uint64_t calculate_night_sleep_time_us(
        std::optional<time_t> unix_time) const override;

    /** @copydoc IDayNightController::reset_hysteresis */
    void reset_hysteresis() override;

    const DayNightConfig& get_config() const { return config_; }
    void set_config(const DayNightConfig& config) { config_ = config; }

    const SunSchedule& get_sun_schedule() const { return sun_schedule_; }

private:
    struct LocalTime
    {
        uint8_t hour = 0;
        uint8_t minute = 0;
        uint16_t day_of_year = 81;
    };

    LocalTime decompose(time_t unix_time) const;

    const SunSchedule& sun_schedule_;
    DayNightConfig config_;
    uint16_t consecutive_dusk_samples_ = 0;
};
