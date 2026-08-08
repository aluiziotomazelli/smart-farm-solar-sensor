#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "water_tank_app.hpp"
#include "espnow_ota_trigger.hpp"
#include "mock_i_level_sensor.hpp"
#include "mock_i_float_switch.hpp"
#include "mock_i_water_tank_storage.hpp"
#include "mock_i_espnow_manager.hpp"
#include "mock_i_wifi_manager.hpp"
#include "mock_i_power_control.hpp"
#include "mock_hal_sleep.hpp"
#include "mock_i_battery_monitor.hpp"
#include "tank_geometry.hpp"
#include "mock_hal_timer.hpp"
#include "mock_i_ota_manager.hpp"
#include "mock_hal_freertos.hpp"
#include "mock_i_ota_trigger.hpp"
#include "mock_hal_system.hpp"
#include "mock_nvs_core.hpp"
#include "mock_i_time_manager.hpp"

using ::testing::_;
using ::testing::AnyNumber;
using ::testing::DoAll;
using ::testing::InSequence;
using ::testing::Invoke;
using ::testing::NiceMock;
using ::testing::Return;
using ::testing::SetArgReferee;

// ---------------------------------------------------------------------------
// Testable subclass — exposes protected members for verification
// ---------------------------------------------------------------------------
class TestableWaterTankApp : public WaterTankApp
{
public:
    using WaterTankApp::WaterTankApp;

    const CoreStorage& get_core_data() const { return core_; }
    const WaterTankStats& get_stats() const { return stats_; }
    bool is_session_healthy() const { return session_healthy_; }
    bool is_pending_firmware_verify() const { return pending_firmware_verify_; }
    bool is_pending_core_commit() const { return pending_core_commit_; }

    void set_session_healthy(bool healthy) { session_healthy_ = healthy; }
    void set_pending_firmware_verify(bool pending) { pending_firmware_verify_ = pending; }

    bool call_wait_for_comm_ready(uint32_t timeout_ms) { return wait_for_comm_ready(timeout_ms); }
    void call_process_pending_ota() { process_pending_ota(); }
    void call_check_firmware() { check_firmware(); }
};

/**
 * Fixture for WaterTankApp tests.
 * Manages mock dependencies and injects them into the WaterTankApp.
 */
class WaterTankAppTest : public ::testing::Test
{
protected:
    NiceMock<MockNvsCore> mock_core_storage;
    NiceMock<MockWaterTankStorage> mock_tank_storage;
    NiceMock<MockLevelSensor> mock_sensor;
    NiceMock<floatswitch::MockFloatSwitch> mock_float_switch;
    NiceMock<espnow::MockEspNowManager> mock_comm;
    NiceMock<power_control::MockPowerControl> mock_power;
    NiceMock<idf_hals::MockSleepHAL> mock_sleep;
    NiceMock<battery_monitor::MockBatteryMonitor> mock_battery;
    NiceMock<idf_hals::MockTimerHAL> mock_sys_timer;
    NiceMock<MockOtaManager> mock_ota;
    NiceMock<idf_hals::MockHalFreertos> mock_rtos;
    NiceMock<MockOtaTrigger> mock_btn_trigger;
    NiceMock<MockOtaTrigger> mock_espnow_trigger;
    NiceMock<idf_hals::MockSystemHAL> mock_system_hal;
    NiceMock<wifi_manager::MockWiFiManager> mock_wifi;
    NiceMock<MockTimeManager> mock_time_manager;

    TankGeometry geometry{10}; // offset 10cm (uint8_t)
    WaterTankLogic logic{geometry, mock_float_switch};
    QueueHandle_t dummy_queue = nullptr;
    uint64_t fake_time_us = 1000ULL;

    std::unique_ptr<TestableWaterTankApp> sut;

    void SetUp() override
    {
        // Default behaviors to ensure tests don't crash by default if left unconfigured
        ultrasonic::Reading default_reading{};
        default_reading.result = ultrasonic::UsResult::OK;
        default_reading.cm = 50.0f;
        ON_CALL(mock_sensor, read_level(_)).WillByDefault(Return(default_reading));

        ON_CALL(mock_core_storage, load_core(_)).WillByDefault(Return(ESP_OK));
        ON_CALL(mock_core_storage, save_core(_, _)).WillByDefault(Return(ESP_OK));
        ON_CALL(mock_tank_storage, load_app_data(_)).WillByDefault(Return(ESP_OK));
        ON_CALL(mock_tank_storage, save_app_data(_, _)).WillByDefault(Return(ESP_OK));

        ON_CALL(mock_power, turn_off()).WillByDefault(Return(ESP_OK));
        ON_CALL(mock_power, turn_on()).WillByDefault(Return(ESP_OK));

        ON_CALL(mock_sleep, disable_wakeup_source(_)).WillByDefault(Return(ESP_OK));
        ON_CALL(mock_sleep, enable_timer_wakeup(_)).WillByDefault(Return(ESP_OK));

        ON_CALL(mock_battery, init()).WillByDefault(Return(ESP_OK));
        ON_CALL(mock_battery, read(_)).WillByDefault(Return(ESP_OK));
        ON_CALL(mock_battery, deinit()).WillByDefault(Return(ESP_OK));

        ON_CALL(mock_float_switch, should_enable_wakeup()).WillByDefault(Return(false));
        ON_CALL(mock_float_switch, is_tank_full()).WillByDefault(Return(false));

        ON_CALL(mock_sys_timer, get_time_us()).WillByDefault(Invoke([this]() {
            uint64_t ret = fake_time_us;
            fake_time_us += 50000ULL; // Advance 50ms per call
            return ret;
        }));

        ON_CALL(mock_ota, init(_)).WillByDefault(Return(true));
        ON_CALL(mock_wifi, init(_)).WillByDefault(Return(ESP_OK));
        ON_CALL(mock_time_manager, init(_)).WillByDefault(Return(ESP_OK));
        ON_CALL(Const(mock_time_manager), is_synchronized()).WillByDefault(Return(true));
        ON_CALL(mock_wifi, add_credentials(_, _)).WillByDefault(Return(ESP_OK));
        ON_CALL(mock_wifi, start()).WillByDefault(Return(ESP_OK));
        ON_CALL(mock_comm, init(_)).WillByDefault(Return(ESP_OK));
        ON_CALL(mock_float_switch, init()).WillByDefault(Return(ESP_OK));
        ON_CALL(mock_sensor, init()).WillByDefault(Return(ESP_OK));

        // Create the system under test
        sut = create_app_with_queue(dummy_queue, nullptr, /*auto_init=*/false);
    }

    std::unique_ptr<TestableWaterTankApp>
    create_app_with_queue(QueueHandle_t rx_queue, IOtaTrigger* espnow_trigger_override = nullptr, bool auto_init = true)
    {
        IOtaTrigger& espnow_trig = espnow_trigger_override ? *espnow_trigger_override : mock_espnow_trigger;
        auto app = std::make_unique<TestableWaterTankApp>(
            mock_core_storage,
            mock_tank_storage,
            mock_sensor,
            mock_float_switch,
            mock_comm,
            rx_queue,
            mock_power,
            mock_sleep,
            mock_battery,
            mock_sys_timer,
            mock_rtos,
            logic,
            mock_wifi,
            mock_ota,
            mock_btn_trigger,
            espnow_trig,
            mock_system_hal,
            mock_time_manager);
        if (auto_init) {
            app->init(false);
        }
        return app;
    }

    void TearDown() override { sut.reset(); }
};