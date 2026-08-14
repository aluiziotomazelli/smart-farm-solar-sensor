#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <memory>

#include "slow_sensors_task.hpp"
#include "mock_battery_monitor.hpp"
#include "mock_ds18b20_driver.hpp"
#include "mock_hal_freertos.hpp"

using ::testing::_;
using ::testing::DoAll;
using ::testing::NiceMock;
using ::testing::Return;
using ::testing::SetArgPointee;
using ::testing::SetArgReferee;

class SlowSensorsTaskTest : public ::testing::Test
{
protected:
    NiceMock<battery_monitor::MockBatteryMonitor> mock_bat_monitor_;
    NiceMock<ds18b20::MockDs18b20Driver> mock_ds18b20_;
    NiceMock<idf_hals::MockHalFreertos> mock_rtos_;
    TelemetrySnapshot snapshot_;

    SlowSensorsConfig config_{
        .sample_interval_ms = 60000,
        .task_stack_size = 3072,
        .task_priority = 2,
        .max_consecutive_errors = 3,
    };

    std::unique_ptr<SlowSensorsTask> sut_;

    void SetUp() override
    {
        ON_CALL(mock_bat_monitor_, init()).WillByDefault(Return(ESP_OK));
        ON_CALL(mock_ds18b20_, init()).WillByDefault(Return(ESP_OK));

        sut_ = std::make_unique<SlowSensorsTask>(
            mock_bat_monitor_,
            mock_ds18b20_,
            mock_rtos_,
            snapshot_,
            config_);
    }
};

TEST_F(SlowSensorsTaskTest, InitInitializesBothDrivers)
{
    EXPECT_CALL(mock_bat_monitor_, init()).WillOnce(Return(ESP_OK));
    EXPECT_CALL(mock_ds18b20_, init()).WillOnce(Return(ESP_OK));

    EXPECT_EQ(sut_->init(), ESP_OK);
}

TEST_F(SlowSensorsTaskTest, InitToleratesDriverErrorsAndReturnsOk)
{
    EXPECT_CALL(mock_bat_monitor_, init()).WillOnce(Return(ESP_ERR_NOT_FOUND));
    EXPECT_CALL(mock_ds18b20_, init()).WillOnce(Return(ESP_FAIL));

    EXPECT_EQ(sut_->init(), ESP_OK);
}

TEST_F(SlowSensorsTaskTest, StartCreatesFreeRTOSTask)
{
    SemaphoreHandle_t dummy_sem = reinterpret_cast<SemaphoreHandle_t>(0x1234);
    EXPECT_CALL(mock_rtos_, semaphore_create_binary()).WillOnce(Return(dummy_sem));

    EXPECT_CALL(mock_rtos_, task_create(_, ::testing::StrEq("SlowSensorsTask"), 3072, _, 2, _))
        .WillOnce(Return(pdPASS));

    EXPECT_EQ(sut_->start(), ESP_OK);
}

TEST_F(SlowSensorsTaskTest, StartAlreadyRunningIsNoOp)
{
    SemaphoreHandle_t dummy_sem = reinterpret_cast<SemaphoreHandle_t>(0x1234);
    EXPECT_CALL(mock_rtos_, semaphore_create_binary()).WillOnce(Return(dummy_sem));
    EXPECT_CALL(mock_rtos_, task_create(_, _, _, _, _, _)).WillOnce(Return(pdPASS));

    EXPECT_EQ(sut_->start(), ESP_OK);
    // Second call should return ESP_OK immediately without creating another task
    EXPECT_EQ(sut_->start(), ESP_OK);
}

TEST_F(SlowSensorsTaskTest, StartFailsWhenSemaphoreCreationFails)
{
    EXPECT_CALL(mock_rtos_, semaphore_create_binary()).WillOnce(Return(nullptr));

    EXPECT_EQ(sut_->start(), ESP_ERR_NO_MEM);
}

TEST_F(SlowSensorsTaskTest, ProcessCycleReadsBatteryAndDs18b20AndUpdatesSnapshot)
{
    battery_monitor::BatteryReading bat_reading{};
    bat_reading.voltage_mv = 3950;
    bat_reading.percent = 72;
    bat_reading.state = battery_monitor::BatteryState::NORMAL;

    EXPECT_CALL(mock_bat_monitor_, read(_))
        .WillOnce(DoAll(SetArgReferee<0>(bat_reading), Return(ESP_OK)));

    EXPECT_CALL(mock_ds18b20_, read_temperature(_))
        .WillOnce(DoAll(SetArgPointee<0>(27.5f), Return(ESP_OK)));

    sut_->process_cycle();

    TelemetrySnapshotData snap = snapshot_.get();
    EXPECT_EQ(snap.battery_mv, 3950);
    EXPECT_EQ(snap.battery_percent, 72);
    EXPECT_EQ(snap.battery_state, farm::BatteryState::NORMAL);
    EXPECT_FLOAT_EQ(snap.temperature_celsius, 27.5f);
}

TEST_F(SlowSensorsTaskTest, ProcessCycleHandlesBatteryErrorAndRecovers)
{
    // 1. First two readings fail
    EXPECT_CALL(mock_bat_monitor_, read(_)).WillOnce(Return(ESP_FAIL));
    sut_->process_cycle();

    EXPECT_CALL(mock_bat_monitor_, read(_)).WillOnce(Return(ESP_FAIL));
    sut_->process_cycle();

    // 2. Third reading reaches max_consecutive_errors (3)
    EXPECT_CALL(mock_bat_monitor_, read(_)).WillOnce(Return(ESP_FAIL));
    sut_->process_cycle();

    // 3. Fourth reading succeeds and resets error counter
    battery_monitor::BatteryReading recovered_reading{};
    recovered_reading.voltage_mv = 4100;
    recovered_reading.percent = 90;
    recovered_reading.state = battery_monitor::BatteryState::FULL;

    EXPECT_CALL(mock_bat_monitor_, read(_))
        .WillOnce(DoAll(SetArgReferee<0>(recovered_reading), Return(ESP_OK)));
    sut_->process_cycle();

    TelemetrySnapshotData snap = snapshot_.get();
    EXPECT_EQ(snap.battery_mv, 4100);
    EXPECT_EQ(snap.battery_percent, 90);
    EXPECT_EQ(snap.battery_state, farm::BatteryState::FULL);
}

TEST_F(SlowSensorsTaskTest, ProcessCycleHandlesDs18b20ErrorAndRecovers)
{
    // Temperature read fails
    EXPECT_CALL(mock_ds18b20_, read_temperature(_)).WillOnce(Return(ESP_ERR_INVALID_CRC));
    sut_->process_cycle();

    // Recover on next cycle
    EXPECT_CALL(mock_ds18b20_, read_temperature(_))
        .WillOnce(DoAll(SetArgPointee<0>(23.25f), Return(ESP_OK)));
    sut_->process_cycle();

    TelemetrySnapshotData snap = snapshot_.get();
    EXPECT_FLOAT_EQ(snap.temperature_celsius, 23.25f);
}
