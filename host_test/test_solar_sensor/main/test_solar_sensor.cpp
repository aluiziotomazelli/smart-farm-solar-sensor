#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "solar_sensor_stats.hpp"
#include "farm_protocol_types.hpp"

using ::testing::_;
using ::testing::NiceMock;
using ::testing::Return;

TEST(SolarStatsTest, DefaultValuesAndReset)
{
    SolarStats stats;
    EXPECT_EQ(stats.magic, SolarStats::MAGIC);
    EXPECT_EQ(stats.version, SolarStats::VERSION);
    EXPECT_FALSE(stats.gpio_wakeup_enabled);
    EXPECT_FALSE(stats.is_night_mode);
    EXPECT_EQ(stats.last_battery_mv, 0);
    EXPECT_EQ(stats.last_battery_percent, 0);
    EXPECT_EQ(stats.last_battery_state, farm::BatteryState::UNKNOWN);
    EXPECT_EQ(stats.max_current_ma, 0);
    EXPECT_EQ(stats.min_day_current_ma, 0);
    EXPECT_EQ(stats.daily_yield_mah, 0);
    EXPECT_EQ(stats.shunt_zero_offset_uv, 0);

    // Modify fields
    stats.is_night_mode = true;
    stats.max_current_ma = 750;
    stats.daily_yield_mah = 1200;
    stats.shunt_zero_offset_uv = -15;

    // Reset and verify defaults restored
    stats.reset();
    EXPECT_EQ(stats.magic, SolarStats::MAGIC);
    EXPECT_EQ(stats.version, SolarStats::VERSION);
    EXPECT_FALSE(stats.is_night_mode);
    EXPECT_EQ(stats.max_current_ma, 0);
    EXPECT_EQ(stats.daily_yield_mah, 0);
    EXPECT_EQ(stats.shunt_zero_offset_uv, 0);
}

TEST(SolarStatsTest, EqualityOperators)
{
    SolarStats stats1;
    SolarStats stats2;
    EXPECT_EQ(stats1, stats2);

    stats1.max_current_ma = 500;
    EXPECT_NE(stats1, stats2);

    stats2.max_current_ma = 500;
    EXPECT_EQ(stats1, stats2);
}

TEST(SolarSensorReportTest, SizeAndPacking)
{
    EXPECT_EQ(sizeof(farm::SolarSensorReport), 27);
    EXPECT_LE(sizeof(farm::SolarSensorReport), APP_MAX_PAYLOAD_SIZE);
}