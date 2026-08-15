#include <gtest/gtest.h>
#include <memory>
#include "day_night_controller.hpp"

namespace {

time_t make_test_time(uint16_t day_of_year, uint8_t hour, uint8_t minute)
{
    // 2026-01-01 00:00:00 UTC = 1767225600
    constexpr time_t BASE_EPOCH_2026 = 1767225600;
    return BASE_EPOCH_2026 + static_cast<time_t>(day_of_year - 1) * 86400 +
           static_cast<time_t>(hour) * 3600 + static_cast<time_t>(minute) * 60;
}

} // namespace

class DayNightControllerTest : public ::testing::Test
{
protected:
    DayNightConfig config_{};
    std::unique_ptr<DayNightController> sut_;

    void SetUp() override
    {
        config_.dusk_current_threshold_ma = DEFAULT_DUSK_CURRENT_MA;
        config_.dawn_current_threshold_ma = DEFAULT_DAWN_CURRENT_THRESHOLD_MA;
        config_.calibration_wake_hour = DEFAULT_CALIBRATION_HOUR_UTC;
        config_.fallback_sleep_sec = DEFAULT_FALLBACK_NIGHT_SLEEP_SEC;
        config_.hysteresis_sample_count = 3; // Reduced for fast unit testing
        config_.unsynced_hysteresis_sample_count = 3; // Reduced for fast unit testing
        config_.latitude_deg = DEFAULT_LATITUDE_DEG; // -20.2074
        config_.tz_offset_hours = 0.0f; // UTC in tests for direct hour/minute matching

        sut_ = std::make_unique<DayNightController>(config_);
    }
};

TEST_F(DayNightControllerTest, CalculateSolarDayEquinox)
{
    // Day 81 (~March 22, Equinox): Day length near 12 hours everywhere
    SolarDayInfo info = sut_->calculate_solar_day(81);

    EXPECT_NEAR(info.day_length_hours, 12.0f, 0.5f);
    EXPECT_NEAR(info.sunrise_hour_local, 6.0f, 0.5f);
    EXPECT_NEAR(info.sunset_hour_local, 18.0f, 0.5f);
}

TEST_F(DayNightControllerTest, ShouldEnterNightModeUnsyncedRequiresHysteresis)
{
    // Unsynced clock: depends only on current < dusk threshold (1 mA) with unsynced hysteresis
    EXPECT_FALSE(sut_->should_enter_night_mode(0, std::nullopt));
    EXPECT_FALSE(sut_->should_enter_night_mode(0, std::nullopt));
    
    // Third consecutive sample below threshold triggers night mode (hysteresis = 3)
    EXPECT_TRUE(sut_->should_enter_night_mode(0, std::nullopt));
}

TEST_F(DayNightControllerTest, ShouldEnterNightModeUnsyncedResetsOnCurrentSpike)
{
    EXPECT_FALSE(sut_->should_enter_night_mode(0, std::nullopt));
    EXPECT_FALSE(sut_->should_enter_night_mode(0, std::nullopt));

    // Spike in current resets counter
    EXPECT_FALSE(sut_->should_enter_night_mode(100, std::nullopt));

    // Next 0 mA sample starts count from 1 again
    EXPECT_FALSE(sut_->should_enter_night_mode(0, std::nullopt));
}

TEST_F(DayNightControllerTest, ShouldEnterNightModeSyncedIgnoresNoonCloud)
{
    // Synced clock at 12:00 PM (Noon): Outside dusk onset (sunset is ~18:00)
    // 0 mA current (passing dark cloud) should NOT trigger night mode
    time_t noon = make_test_time(81, 12, 0);
    for (int i = 0; i < 5; ++i) {
        EXPECT_FALSE(sut_->should_enter_night_mode(0, noon));
    }
}

TEST_F(DayNightControllerTest, ShouldEnterNightModeSyncedTriggersAtSunsetWindow)
{
    // Synced clock at 18:30 PM: Past dusk onset (~17:30)
    time_t sunset_window = make_test_time(81, 18, 30);
    EXPECT_FALSE(sut_->should_enter_night_mode(0, sunset_window));
    EXPECT_FALSE(sut_->should_enter_night_mode(0, sunset_window));
    
    // 3rd consecutive sample in dusk window triggers night mode
    EXPECT_TRUE(sut_->should_enter_night_mode(0, sunset_window));
}

TEST_F(DayNightControllerTest, ShouldEnterNightModeSyncedTriggersLateAtNight)
{
    // Synced clock at 23:00 PM: Night time
    time_t late_night = make_test_time(81, 23, 0);
    EXPECT_FALSE(sut_->should_enter_night_mode(0, late_night));
    EXPECT_FALSE(sut_->should_enter_night_mode(0, late_night));
    EXPECT_TRUE(sut_->should_enter_night_mode(0, late_night));
}

TEST_F(DayNightControllerTest, ShouldEnterNightModeSyncedTriggersEarlyMorningBeforeSunrise)
{
    // Synced clock at 04:00 AM: Before sunrise (~06:00)
    time_t early_morning = make_test_time(81, 4, 0);
    EXPECT_FALSE(sut_->should_enter_night_mode(0, early_morning));
    EXPECT_FALSE(sut_->should_enter_night_mode(0, early_morning));
    EXPECT_TRUE(sut_->should_enter_night_mode(0, early_morning));
}

TEST_F(DayNightControllerTest, ClassifyWakeGpioTriggerAtDawnWindow)
{
    // Woken up by GPIO at 06:10 AM (within dawn window ~05:30 to 12:00) -> DAWN_GPIO
    time_t dawn_time = make_test_time(81, 6, 10);
    WakeType type = sut_->classify_wake(true, 0, dawn_time);
    EXPECT_EQ(type, WakeType::DAWN_GPIO);
}

TEST_F(DayNightControllerTest, ClassifyWakeGpioTriggerAtDuskIsSpurious)
{
    // Woken up by GPIO at 17:36 PM (dusk / outside dawn window) -> SPURIOUS_TIMER
    time_t dusk_time = make_test_time(81, 17, 36);
    WakeType type = sut_->classify_wake(true, 0, dusk_time);
    EXPECT_EQ(type, WakeType::SPURIOUS_TIMER);
}

TEST_F(DayNightControllerTest, ClassifyWakeGpioTriggerAtMidnightIsSpurious)
{
    // Woken up by GPIO at 01:00 AM -> SPURIOUS_TIMER
    time_t midnight_time = make_test_time(81, 1, 0);
    WakeType type = sut_->classify_wake(true, 0, midnight_time);
    EXPECT_EQ(type, WakeType::SPURIOUS_TIMER);
}

TEST_F(DayNightControllerTest, ClassifyWakeHighCurrentTimerTriggerAtDawn)
{
    // Woken up by timer at 06:10 AM with current 10 mA (> 5 mA dawn threshold) -> Day start
    time_t dawn_time = make_test_time(81, 6, 10);
    WakeType type = sut_->classify_wake(false, 10, dawn_time);
    EXPECT_EQ(type, WakeType::DAWN_TIMER);
}

TEST_F(DayNightControllerTest, ClassifyWakeCalibrationTimerTrigger)
{
    // Woken up by timer at 03:00 AM in darkness (0 mA) -> Calibration
    time_t calib_time = make_test_time(81, DEFAULT_CALIBRATION_HOUR_UTC, 0);
    WakeType type = sut_->classify_wake(false, 0, calib_time);
    EXPECT_EQ(type, WakeType::CALIBRATION_TIMER);
}

TEST_F(DayNightControllerTest, ClassifyWakeSpuriousTimerTrigger)
{
    // Woken up by timer at 01:00 AM in darkness (0 mA) -> Spurious
    time_t spurious_time = make_test_time(81, 1, 0);
    WakeType type = sut_->classify_wake(false, 0, spurious_time);
    EXPECT_EQ(type, WakeType::SPURIOUS_TIMER);
}

TEST_F(DayNightControllerTest, ClassifyWakeUnsyncedFallback)
{
    // When unsynced (std::nullopt), uses current threshold
    EXPECT_EQ(sut_->classify_wake(true, 10, std::nullopt), WakeType::DAWN_GPIO);
    EXPECT_EQ(sut_->classify_wake(false, 10, std::nullopt), WakeType::DAWN_TIMER);
    EXPECT_EQ(sut_->classify_wake(true, 0, std::nullopt), WakeType::SPURIOUS_TIMER);
    EXPECT_EQ(sut_->classify_wake(false, 0, std::nullopt), WakeType::SPURIOUS_TIMER);
}

TEST_F(DayNightControllerTest, CalculateNightSleepTimeUsUnsyncedFallback)
{
    // Unsynced clock: returns 3600 seconds (60 min) fallback sleep in us
    uint64_t sleep_us = sut_->calculate_night_sleep_time_us(std::nullopt);
    EXPECT_EQ(sleep_us, 3600ULL * 1000000ULL);
}

TEST_F(DayNightControllerTest, CalculateNightSleepTimeUsSyncedUntilCalibration)
{
    // Synced clock at 20:00 PM: sleeps until 03:00 AM next day (7 hours = 25200s)
    time_t dusk_time = make_test_time(81, 20, 0);
    uint64_t sleep_us = sut_->calculate_night_sleep_time_us(dusk_time);
    EXPECT_EQ(sleep_us, 25200ULL * 1000000ULL);
}

TEST_F(DayNightControllerTest, CalculateNightSleepTimeUsSyncedAfterCalibrationUntilSunrise)
{
    // Synced clock at 03:00 AM: sunrise is ~06:00 AM (3 hours = 10800s)
    time_t post_calib_time = make_test_time(81, 3, 0);
    uint64_t sleep_us = sut_->calculate_night_sleep_time_us(post_calib_time);
    EXPECT_EQ(sleep_us, 10800ULL * 1000000ULL);
}
