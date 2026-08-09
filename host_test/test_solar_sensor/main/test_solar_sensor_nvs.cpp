#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "solar_sensor_stats.hpp"
#include "mock_persistence_backend.hpp"
#include "solar_sensor_nvs.hpp"

#include "esp_rom_crc.h"

#include <memory>
#include <cstring>
#include <type_traits>

using ::testing::_;
using ::testing::AtLeast;
using ::testing::NiceMock;
using ::testing::Return;

/**
 * Helper: Calculate CRC for a struct with crc field
 */
template <typename T> inline uint32_t test_calculate_crc(const T& data)
{
    static_assert(std::is_standard_layout_v<T>, "T must be standard_layout for safe offset calculation");
    static_assert(offsetof(T, crc) != 0, "T must have a non-first crc field");

    return esp_rom_crc32_le(0, reinterpret_cast<const uint8_t*>(&data), offsetof(T, crc));
}

/**
 * Fixture for SolarSensorNvsTest tests.
 * Manages mock backends and provides utility methods.
 */
class SolarSensorNvsTest : public ::testing::Test
{
protected:
    NiceMock<MockPersistenceBackend> rtc_backend_;
    NiceMock<MockPersistenceBackend> nvs_backend_;

    std::unique_ptr<SolarSensorNvs> sut_;

    void SetUp() override
    {
        rtc_backend_.UseRealStorage();
        nvs_backend_.UseRealStorage();

        sut_ = std::make_unique<SolarSensorNvs>(rtc_backend_, nvs_backend_);
    }

    void TearDown() override { sut_.reset(); }

    /**
     * Helper: Create a valid SolarStats with all fields set.
     */
    SolarStats create_valid_stats()
    {
        SolarStats stats;
        stats.magic = SolarStats::MAGIC;
        stats.version = SolarStats::VERSION;
        stats.gpio_wakeup_enabled = true;
        stats.is_night_mode = false;

        stats.last_battery_mv = 3700;
        stats.last_battery_percent = 85;
        stats.last_battery_state = farm::BatteryState::NORMAL;

        stats.max_current_ma = 720;
        stats.min_day_current_ma = 15;
        stats.daily_yield_mah = 1500;
        stats.shunt_zero_offset_uv = -10;

        stats.crc = test_calculate_crc(stats);

        return stats;
    }

    void set_rtc_data(const SolarStats& stats) { rtc_backend_.save(&stats, sizeof(stats)); }
    void set_nvs_data(const SolarStats& stats) { nvs_backend_.save(&stats, sizeof(stats)); }

    SolarStats get_stored_rtc_data() const
    {
        SolarStats stats;
        memcpy(&stats, rtc_backend_.GetStoredData(), sizeof(stats));
        return stats;
    }

    SolarStats get_stored_nvs_data() const
    {
        SolarStats stats;
        memcpy(&stats, nvs_backend_.GetStoredData(), sizeof(stats));
        return stats;
    }
};

// =============================================================
// Tests
// =============================================================

TEST_F(SolarSensorNvsTest, LoadFromRtcWhenValid)
{
    SolarStats expected = create_valid_stats();
    set_rtc_data(expected);

    EXPECT_CALL(rtc_backend_, load(_, _)).Times(1);
    EXPECT_CALL(nvs_backend_, load(_, _)).Times(0);

    SolarStats loaded;
    esp_err_t ret = sut_->load_app_data(loaded);

    EXPECT_EQ(ret, ESP_OK);
    EXPECT_EQ(loaded, expected);
    EXPECT_EQ(nvs_backend_.GetStoredSize(), 0);
}

TEST_F(SolarSensorNvsTest, LoadFromNvsWhenRtcInvalid)
{
    SolarStats expected = create_valid_stats();
    set_nvs_data(expected);

    EXPECT_CALL(rtc_backend_, load(_, _)).Times(1);
    EXPECT_CALL(nvs_backend_, load(_, _)).Times(1);
    EXPECT_CALL(rtc_backend_, save(_, _)).Times(1);

    SolarStats loaded;
    esp_err_t ret = sut_->load_app_data(loaded);

    EXPECT_EQ(ret, ESP_OK);
    EXPECT_EQ(loaded, expected);

    SolarStats rtc_data = get_stored_rtc_data();
    EXPECT_EQ(rtc_data, expected);
}

TEST_F(SolarSensorNvsTest, LoadFailsWhenBothInvalid)
{
    EXPECT_CALL(rtc_backend_, load(_, _)).Times(1);
    EXPECT_CALL(nvs_backend_, load(_, _)).Times(1);

    SolarStats loaded;
    esp_err_t ret = sut_->load_app_data(loaded);

    EXPECT_NE(ret, ESP_OK);
}

TEST_F(SolarSensorNvsTest, SaveToRtcOnlyWhenNotForcingNvs)
{
    SolarStats to_save = create_valid_stats();
    to_save.max_current_ma = 750;

    EXPECT_CALL(rtc_backend_, save(_, _)).Times(1);
    EXPECT_CALL(nvs_backend_, save(_, _)).Times(0);

    esp_err_t ret = sut_->save_app_data(to_save, /*force_nvs_commit=*/false);

    EXPECT_EQ(ret, ESP_OK);

    SolarStats rtc_stored = get_stored_rtc_data();
    EXPECT_EQ(rtc_stored.max_current_ma, 750);
}

TEST_F(SolarSensorNvsTest, SaveToBothRtcAndNvsWhenForcing)
{
    SolarStats to_save = create_valid_stats();
    to_save.max_current_ma = 780;

    EXPECT_CALL(rtc_backend_, save(_, _)).Times(1);
    EXPECT_CALL(nvs_backend_, save(_, _)).Times(1);

    esp_err_t ret = sut_->save_app_data(to_save, /*force_nvs_commit=*/true);

    EXPECT_EQ(ret, ESP_OK);

    SolarStats rtc_stored = get_stored_rtc_data();
    SolarStats nvs_stored = get_stored_nvs_data();
    EXPECT_EQ(rtc_stored.max_current_ma, 780);
    EXPECT_EQ(nvs_stored.max_current_ma, 780);
}

TEST_F(SolarSensorNvsTest, SaveToNvsFailsWhenNvsError)
{
    SolarStats to_save = create_valid_stats();
    to_save.max_current_ma = 800;

    EXPECT_CALL(nvs_backend_, save(_, _)).Times(1).WillOnce(Return(ESP_ERR_NVS_NOT_INITIALIZED));

    esp_err_t ret = sut_->save_app_data(to_save, /*force_nvs_commit=*/true);

    EXPECT_NE(ret, ESP_OK);
}

TEST_F(SolarSensorNvsTest, RoundTripSaveAndLoad)
{
    SolarStats original = create_valid_stats();
    original.max_current_ma = 710;
    original.daily_yield_mah = 2000;
    original.crc = test_calculate_crc(original);

    esp_err_t save_ret = sut_->save_app_data(original, /*force_nvs_commit=*/true);
    EXPECT_EQ(save_ret, ESP_OK);

    SolarStats loaded = {};
    esp_err_t load_ret = sut_->load_app_data(loaded);

    EXPECT_EQ(load_ret, ESP_OK);
    EXPECT_EQ(loaded, original);
    EXPECT_EQ(loaded.max_current_ma, 710);
    EXPECT_EQ(loaded.daily_yield_mah, 2000);
}

TEST_F(SolarSensorNvsTest, LoadFailsWithBadCrc)
{
    SolarStats corrupted = create_valid_stats();
    corrupted.crc = 0xDEAD;
    set_nvs_data(corrupted);

    SolarStats loaded = {};
    esp_err_t ret = sut_->load_app_data(loaded);

    EXPECT_NE(ret, ESP_OK);
}

TEST_F(SolarSensorNvsTest, LoadFailsWithWrongMagic)
{
    SolarStats wrong_magic = create_valid_stats();
    wrong_magic.magic = 0xDEAD;
    wrong_magic.crc = test_calculate_crc(wrong_magic);
    set_nvs_data(wrong_magic);

    SolarStats loaded = {};
    esp_err_t ret = sut_->load_app_data(loaded);

    EXPECT_NE(ret, ESP_OK);
}

TEST_F(SolarSensorNvsTest, NoDoubleSaveWhenUnchanged)
{
    SolarStats data = create_valid_stats();

    sut_->save_app_data(data, true);

    rtc_backend_.Clear();
    nvs_backend_.Clear();
    rtc_backend_.UseRealStorage();
    nvs_backend_.UseRealStorage();

    SolarStats same_data = data;
    esp_err_t ret = sut_->save_app_data(same_data, false);

    EXPECT_EQ(ret, ESP_OK);
}

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