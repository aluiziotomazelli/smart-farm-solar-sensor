#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <memory>

#include "solar_sensor.hpp"
#include "mock_ina_sensor_task.hpp"
#include "mock_battery_monitor.hpp"
#include "mock_espnow_manager.hpp"
#include "mock_time_manager.hpp"
#include "mock_persistence_backend.hpp"
#include "nvs_core.hpp"
#include "solar_sensor_nvs.hpp"

#include "mock_hal_timer.hpp"
#include "mock_hal_sleep.hpp"
#include "mock_hal_system.hpp"
#include "mock_hal_freertos.hpp"
#include "mock_hal_gpio.hpp"
#include "mock_hal_i2c.hpp"
#include "mock_ota_manager.hpp"

using ::testing::_;
using ::testing::DoAll;
using ::testing::NiceMock;
using ::testing::Return;
using ::testing::SetArgReferee;

class MockOtaTrigger : public IOtaTrigger
{
public:
    MOCK_METHOD(esp_err_t, arm, (IOtaTriggerListener& listener), (override));
    MOCK_METHOD(void, disarm, (), (override));
    MOCK_METHOD(void, notify, (), (override));
};

class MockWiFiManager : public wifi_manager::IWiFiManager
{
public:
    MOCK_METHOD(esp_err_t, init, (const wifi_manager::Config& config), (override));
    MOCK_METHOD(esp_err_t, deinit, (), (override));
    MOCK_METHOD(esp_err_t, start, (uint32_t timeout_ms), (override));
    MOCK_METHOD(esp_err_t, start, (), (override));
    MOCK_METHOD(esp_err_t, stop, (uint32_t timeout_ms), (override));
    MOCK_METHOD(esp_err_t, stop, (), (override));
    MOCK_METHOD(esp_err_t, connect, (uint32_t timeout_ms), (override));
    MOCK_METHOD(esp_err_t, connect, (), (override));
    MOCK_METHOD(esp_err_t, disconnect, (uint32_t timeout_ms), (override));
    MOCK_METHOD(esp_err_t, disconnect, (), (override));
    MOCK_METHOD(wifi_manager::State, get_state, (), (const, override));
    MOCK_METHOD(esp_err_t, add_credentials, (const std::string& ssid, const std::string& password), (override));
    MOCK_METHOD(esp_err_t, set_credentials, (const std::string& ssid, const std::string& password), (override));
    MOCK_METHOD(esp_err_t, get_credentials, (std::string& ssid, std::string& password), (override));
    MOCK_METHOD(esp_err_t, clear_credentials, (), (override));
    MOCK_METHOD(esp_err_t, factory_reset, (), (override));
    MOCK_METHOD(bool, is_credentials_valid, (), (const, override));
    MOCK_METHOD(TaskHandle_t, get_task_handle, (), (const, override));
    MOCK_METHOD(esp_err_t, get_ap_info, (wifi_ap_record_t& info), (override));
    MOCK_METHOD(esp_err_t, get_rssi, (int8_t& rssi), (override));
};

class SolarSensorTest : public ::testing::Test
{
protected:
    NiceMock<ina::MockInaSensorTask> mock_ina_task_;
    QueueHandle_t dummy_queue_ = reinterpret_cast<QueueHandle_t>(0x5678);
    TelemetrySnapshot snapshot_;
    NiceMock<battery_monitor::MockBatteryMonitor> mock_bat_monitor_;

    NiceMock<MockPersistenceBackend> rtc_core_backend_;
    NiceMock<MockPersistenceBackend> nvs_core_backend_;
    NvsCore core_storage_{rtc_core_backend_, nvs_core_backend_};

    NiceMock<MockPersistenceBackend> rtc_solar_backend_;
    NiceMock<MockPersistenceBackend> nvs_solar_backend_;
    SolarSensorNvs solar_storage_{rtc_solar_backend_, nvs_solar_backend_};

    NiceMock<idf_hals::MockTimerHAL> hal_timer_;
    NiceMock<MockOtaManager> ota_manager_;
    OtaController ota_controller_{ota_manager_, hal_rtos_};
    NiceMock<MockOtaTrigger> btn_trigger_;
    NiceMock<MockOtaTrigger> espnow_trigger_;
    NiceMock<espnow::MockEspNowManager> espnow_;
    QueueHandle_t rx_queue_ = reinterpret_cast<QueueHandle_t>(0x9999);
    NiceMock<MockWiFiManager> wifi_;
    NiceMock<idf_hals::MockSleepHAL> hal_sleep_;
    NiceMock<idf_hals::MockSystemHAL> hal_system_;
    NiceMock<time_manager::MockTimeManager> time_manager_;
    NiceMock<idf_hals::MockHalFreertos> hal_rtos_;
    NiceMock<idf_hals::MockGpioHAL> hal_gpio_;
    NiceMock<idf_hals::MockI2cHAL> hal_i2c_;

    std::unique_ptr<SolarSensor> sut_;

    void SetUp() override
    {
        rtc_core_backend_.UseRealStorage();
        nvs_core_backend_.UseRealStorage();
        rtc_solar_backend_.UseRealStorage();
        nvs_solar_backend_.UseRealStorage();

        ON_CALL(hal_rtos_, queue_receive(rx_queue_, _, _)).WillByDefault(Return(pdFALSE));
        ON_CALL(hal_rtos_, queue_receive(dummy_queue_, _, _)).WillByDefault(Return(pdFALSE));

        sut_ = std::make_unique<SolarSensor>(
            mock_ina_task_,
            dummy_queue_,
            snapshot_,
            mock_bat_monitor_,
            core_storage_,
            solar_storage_,
            hal_timer_,
            ota_controller_,
            btn_trigger_,
            espnow_trigger_,
            espnow_,
            rx_queue_,
            wifi_,
            hal_sleep_,
            hal_system_,
            time_manager_,
            hal_rtos_,
            hal_gpio_,
            hal_i2c_);
    }
};

TEST_F(SolarSensorTest, UpdateBatterySnapshotReadsBatteryAndPopulatesSnapshot)
{
    battery_monitor::BatteryReading reading{};
    reading.voltage_mv = 3900;
    reading.adc_mv = 1950;
    reading.percent = 66;
    reading.state = battery_monitor::BatteryState::NORMAL;

    EXPECT_CALL(mock_bat_monitor_, read(_)).WillOnce(DoAll(SetArgReferee<0>(reading), Return(ESP_OK)));

    EXPECT_EQ(sut_->update_battery_snapshot(), ESP_OK);

    TelemetrySnapshotData snap = snapshot_.get();
    EXPECT_EQ(snap.battery_mv, 3900);
    EXPECT_EQ(snap.battery_percent, 66);
    EXPECT_EQ(snap.battery_state, farm::BatteryState::NORMAL);
}

TEST_F(SolarSensorTest, RunProcessesOtaTriggerWhenSet)
{
    sut_->on_ota_triggered(OtaTriggerSource::BUTTON);

    EXPECT_CALL(btn_trigger_, disarm()).Times(1);
    EXPECT_CALL(espnow_trigger_, disarm()).Times(1);
    EXPECT_CALL(mock_ina_task_, stop()).Times(1);

    EXPECT_TRUE(sut_->run());
}

TEST_F(SolarSensorTest, RunProcessesInaSamplesAndEntersNightSleepOnDusk)
{
    DayNightConfig cfg{};
    cfg.hysteresis_sample_count = 1;
    sut_->get_day_night_controller().set_config(cfg);

    InaSample sample{};
    sample.isc_current_ma = 0;
    sample.status = ESP_OK;

    EXPECT_CALL(hal_rtos_, queue_receive(rx_queue_, _, _))
        .WillRepeatedly(Return(pdFALSE));

    EXPECT_CALL(hal_rtos_, queue_receive(dummy_queue_, _, _))
        .WillOnce(::testing::Invoke([sample](QueueHandle_t, void* data, TickType_t) {
            if (data) {
                *reinterpret_cast<InaSample*>(data) = sample;
            }
            return pdTRUE;
        }))
        .WillRepeatedly(Return(pdFALSE));

    EXPECT_CALL(mock_ina_task_, prepare_for_sleep()).WillOnce(Return(ESP_OK));
    EXPECT_CALL(hal_sleep_, enable_timer_wakeup(_)).WillOnce(Return(ESP_OK));
    EXPECT_CALL(hal_sleep_, deep_sleep_enable_gpio_wakeup(_, _)).WillOnce(Return(ESP_OK));
    EXPECT_CALL(hal_sleep_, deep_sleep_start()).Times(1);

    EXPECT_FALSE(sut_->run());

    TelemetrySnapshotData snap = snapshot_.get();
    EXPECT_TRUE(snap.is_night_mode);
}

TEST_F(SolarSensorTest, ProcessInaSampleUpdatesMaxDayCurrentAndAccumulatesYield)
{
    InaSample sample{};
    sample.isc_current_ma = 1000;
    sample.status = ESP_OK;

    EXPECT_CALL(mock_ina_task_, get_expected_sample_period_ms()).WillRepeatedly(Return(3600000u));

    EXPECT_CALL(hal_rtos_, queue_receive(rx_queue_, _, _))
        .WillRepeatedly(Return(pdFALSE));

    EXPECT_CALL(hal_rtos_, queue_receive(dummy_queue_, _, _))
        .WillOnce(::testing::Invoke([sample](QueueHandle_t, void* data, TickType_t) {
            if (data) {
                *reinterpret_cast<InaSample*>(data) = sample;
            }
            return pdTRUE;
        }))
        .WillRepeatedly(Return(pdFALSE));

    EXPECT_TRUE(sut_->run());

    TelemetrySnapshotData snap = snapshot_.get();
    EXPECT_EQ(snap.max_current_ma, 1000);
    EXPECT_EQ(snap.daily_yield_mah, 1000);
}
