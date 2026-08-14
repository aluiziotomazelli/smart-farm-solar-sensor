#include <gtest/gtest.h>
#include <memory>
#include "day_night_controller.hpp"

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
        config_.latitude_deg = DEFAULT_LATITUDE_DEG; // -23.5505 (São Paulo)

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
    // Unsynced clock: depends only on current < dusk threshold (1 mA) with hysteresis
    EXPECT_FALSE(sut_->should_enter_night_mode(0, false, 12, 0, 81));
    EXPECT_FALSE(sut_->should_enter_night_mode(0, false, 12, 0, 81));
    
    // Third consecutive sample below threshold triggers night mode (hysteresis = 3)
    EXPECT_TRUE(sut_->should_enter_night_mode(0, false, 12, 0, 81));
}

TEST_F(DayNightControllerTest, ShouldEnterNightModeUnsyncedResetsOnCurrentSpike)
{
    EXPECT_FALSE(sut_->should_enter_night_mode(0, false, 12, 0, 81));
    EXPECT_FALSE(sut_->should_enter_night_mode(0, false, 12, 0, 81));

    // Spike in current resets counter
    EXPECT_FALSE(sut_->should_enter_night_mode(100, false, 12, 0, 81));

    // Next 0 mA sample starts count from 1 again
    EXPECT_FALSE(sut_->should_enter_night_mode(0, false, 12, 0, 81));
}

TEST_F(DayNightControllerTest, ShouldEnterNightModeSyncedIgnoresNoonCloud)
{
    // Synced clock at 12:00 PM (Noon): Outside dusk window (sunset is ~18:00)
    // 0 mA current (passing dark cloud) should NOT trigger night mode
    for (int i = 0; i < 5; ++i) {
        EXPECT_FALSE(sut_->should_enter_night_mode(0, true, 12, 0, 81));
    }
}

TEST_F(DayNightControllerTest, ShouldEnterNightModeSyncedTriggersAtSunsetWindow)
{
    // Synced clock at 18:30 PM: Within dusk window (~18:00 sunset + margins)
    EXPECT_FALSE(sut_->should_enter_night_mode(0, true, 18, 30, 81));
    EXPECT_FALSE(sut_->should_enter_night_mode(0, true, 18, 30, 81));
    
    // 3rd consecutive sample in dusk window triggers night mode
    EXPECT_TRUE(sut_->should_enter_night_mode(0, true, 18, 30, 81));
}

TEST_F(DayNightControllerTest, ClassifyWakeGpioTrigger)
{
    WakeType type = sut_->classify_wake(true, 0, true, 2);
    EXPECT_EQ(type, WakeType::DAWN_GPIO);
}

TEST_F(DayNightControllerTest, ClassifyWakeHighCurrentTimerTrigger)
{
    // Woken up by timer but current is 10 mA (> 5 mA dawn threshold) -> Day start
    WakeType type = sut_->classify_wake(false, 10, true, 2);
    EXPECT_EQ(type, WakeType::DAWN_TIMER);
}

TEST_F(DayNightControllerTest, ClassifyWakeCalibrationTimerTrigger)
{
    // Woken up by timer at 03:00 AM in darkness (0 mA) -> Calibration
    WakeType type = sut_->classify_wake(false, 0, true, DEFAULT_CALIBRATION_HOUR_UTC);
    EXPECT_EQ(type, WakeType::CALIBRATION_TIMER);
}

TEST_F(DayNightControllerTest, ClassifyWakeSpuriousTimerTrigger)
{
    // Woken up by timer at 01:00 AM in darkness (0 mA) -> Spurious
    WakeType type = sut_->classify_wake(false, 0, true, 1);
    EXPECT_EQ(type, WakeType::SPURIOUS_TIMER);
}

TEST_F(DayNightControllerTest, CalculateNightSleepTimeUsUnsyncedFallback)
{
    // Unsynced clock: returns 3600 seconds (60 min) fallback sleep in us
    uint64_t sleep_us = sut_->calculate_night_sleep_time_us(false, 12, 0, 81);
    EXPECT_EQ(sleep_us, 3600ULL * 1000000ULL);
}

TEST_F(DayNightControllerTest, CalculateNightSleepTimeUsSyncedUntilCalibration)
{
    // Synced clock at 20:00 PM: sleeps until 03:00 AM next day (7 hours = 25200s)
    uint64_t sleep_us = sut_->calculate_night_sleep_time_us(true, 20, 0, 81);
    EXPECT_EQ(sleep_us, 25200ULL * 1000000ULL);
}

TEST_F(DayNightControllerTest, CalculateNightSleepTimeUsSyncedAfterCalibrationUntilSunrise)
{
    // Synced clock at 03:00 AM: sunrise is ~06:00 AM (3 hours = 10800s)
    uint64_t sleep_us = sut_->calculate_night_sleep_time_us(true, 3, 0, 81);
    EXPECT_EQ(sleep_us, 10800ULL * 1000000ULL);
}
