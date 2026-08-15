#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "ina_sensor_task.hpp"
#include "mock_ina226_driver.hpp"
#include "mock_espnow_manager.hpp"
#include "mock_hal_timer.hpp"
#include "mock_hal_freertos.hpp"
#include "mock_time_manager.hpp"
#include "telemetry_snapshot.hpp"

using ::testing::_;
using ::testing::DoAll;
using ::testing::NiceMock;
using ::testing::Return;
using ::testing::ReturnRef;
using ::testing::SetArgPointee;
using ::testing::SetArgReferee;

using namespace ina;
using namespace ina226;
using namespace espnow;
using namespace idf_hals;

// Sentinel null bus handle used in unit tests (mock driver never dereferences it)
static constexpr i2c_master_bus_handle_t NULL_BUS = nullptr;

// CNVR (ALERT_ON_CONVERSION_READY, bit 10) — the day-regime alert mask armed at init.
// Unlike CVRF (bit 3) it actually asserts the ALERT pin on every conversion.
static constexpr uint16_t CNVR_ALERT_MASK = static_cast<uint16_t>(AlertFlag::ALERT_ON_CONVERSION_READY);

class InaSensorTaskTest : public ::testing::Test
{
protected:
    NiceMock<MockIna226Driver> mock_driver_;
    NiceMock<MockEspNowManager> mock_espnow_;
    NiceMock<MockTimerHAL> mock_timer_;
    NiceMock<MockHalFreertos> mock_rtos_;
    NiceMock<time_manager::MockTimeManager> mock_time_;
    TelemetrySnapshot snapshot_;

    ina226::Ina226Config base_config_;

    QueueHandle_t dummy_queue_ = reinterpret_cast<QueueHandle_t>(0x1234);

    std::unique_ptr<InaSensorTask> sut_;

    void SetUp() override
    {
        ON_CALL(mock_driver_, get_config()).WillByDefault(ReturnRef(base_config_));
        ON_CALL(mock_rtos_, semaphore_create_binary()).WillByDefault(Return(reinterpret_cast<SemaphoreHandle_t>(0x5678)));
        ON_CALL(mock_rtos_, task_create(_, _, _, _, _, _))
            .WillByDefault(DoAll(SetArgPointee<5>(reinterpret_cast<TaskHandle_t>(0x9ABC)), Return(pdPASS)));
        sut_ = std::make_unique<InaSensorTask>(
            mock_driver_,
            mock_espnow_,
            mock_timer_,
            mock_rtos_,
            mock_time_,
            snapshot_,
            dummy_queue_
        );
    }

    void init_sut(InaSensorConfig config = {})
    {
        // init(bus_handle) binds the driver to the I2C bus — mocked, handle is ignored.
        // On success it must also arm the day-regime conversion-ready alert (CNVR).
        EXPECT_CALL(mock_driver_, init(NULL_BUS)).WillOnce(Return(ESP_OK));
        EXPECT_CALL(mock_driver_, configure_alert(CNVR_ALERT_MASK, 0)).WillOnce(Return(ESP_OK));
        EXPECT_CALL(mock_timer_, get_time_us()).WillRepeatedly(Return(1000000));
        sut_->init(config, NULL_BUS);
        sut_->set_reporting_enabled(true);
    }
};

TEST_F(InaSensorTaskTest, InitSuccess)
{
    EXPECT_CALL(mock_driver_, init(NULL_BUS)).WillOnce(Return(ESP_OK));
    EXPECT_CALL(mock_driver_, configure_alert(CNVR_ALERT_MASK, 0)).WillOnce(Return(ESP_OK));
    EXPECT_CALL(mock_timer_, get_time_us()).WillOnce(Return(1000000));

    InaSensorConfig config{};
    EXPECT_EQ(sut_->init(config, NULL_BUS), ESP_OK);
    EXPECT_TRUE(sut_->is_sampling_enabled());
    EXPECT_FALSE(sut_->is_reporting_enabled());
}

TEST_F(InaSensorTaskTest, InitDriverFailure)
{
    EXPECT_CALL(mock_driver_, init(NULL_BUS)).WillOnce(Return(ESP_ERR_TIMEOUT));
    EXPECT_CALL(mock_driver_, configure_alert(_, _)).Times(0);

    InaSensorConfig config{};
    EXPECT_EQ(sut_->init(config, NULL_BUS), ESP_ERR_TIMEOUT);
}

TEST_F(InaSensorTaskTest, InitAlertConfigFailure)
{
    EXPECT_CALL(mock_driver_, init(NULL_BUS)).WillOnce(Return(ESP_OK));
    EXPECT_CALL(mock_driver_, configure_alert(CNVR_ALERT_MASK, 0)).WillOnce(Return(ESP_ERR_INVALID_STATE));

    InaSensorConfig config{};
    EXPECT_EQ(sut_->init(config, NULL_BUS), ESP_ERR_INVALID_STATE);
}

TEST_F(InaSensorTaskTest, ProcessCycleNormalSampling)
{
    init_sut();

    int32_t read_shunt_uv = 50000;
    EXPECT_CALL(mock_driver_, read_shunt_voltage_uv(_))
        .WillRepeatedly(DoAll(SetArgReferee<0>(read_shunt_uv), Return(ESP_OK)));
    EXPECT_CALL(mock_driver_, read_alert_flags(_))
        .WillOnce(DoAll(SetArgReferee<0>(0), Return(ESP_OK)));
    EXPECT_CALL(mock_timer_, get_time_us()).WillRepeatedly(Return(1000000));
    EXPECT_CALL(mock_espnow_, send_data(ReservedIds::HUB, _, _, _, false))
        .WillOnce(Return(ESP_OK));
    EXPECT_CALL(mock_rtos_, queue_send(dummy_queue_, _, 0))
        .WillOnce(Return(pdTRUE));

    sut_->process_cycle();
}

TEST_F(InaSensorTaskTest, ProcessCyclePopulatesTelemetryFromSnapshotAndSyncTime)
{
    init_sut();

    snapshot_.update_battery(3700, 85, farm::BatteryState::NORMAL);
    snapshot_.update_stats(650, 1200);
    snapshot_.set_night_mode(false);

    EXPECT_CALL(mock_time_, is_synchronized()).WillOnce(Return(true));
    EXPECT_CALL(mock_time_, get_timestamp_ms()).WillOnce(Return(1700000000000ULL));

    int32_t read_shunt_uv = 50000;
    EXPECT_CALL(mock_driver_, read_shunt_voltage_uv(_))
        .WillRepeatedly(DoAll(SetArgReferee<0>(read_shunt_uv), Return(ESP_OK)));
    EXPECT_CALL(mock_driver_, read_alert_flags(_))
        .WillOnce(DoAll(SetArgReferee<0>(0), Return(ESP_OK)));

    farm::SolarSensorReport captured_report{};
    EXPECT_CALL(mock_espnow_, send_data(ReservedIds::HUB, static_cast<uint8_t>(farm::PayloadType::SOLAR_SENSOR_REPORT), _, sizeof(farm::SolarSensorReport), false))
        .WillOnce([&](NodeId, uint8_t, const void* data, size_t, bool) {
            captured_report = *static_cast<const farm::SolarSensorReport*>(data);
            return ESP_OK;
        });

    EXPECT_CALL(mock_rtos_, queue_send(dummy_queue_, _, 0)).WillOnce(Return(pdTRUE));

    sut_->process_cycle();

    EXPECT_EQ(captured_report.battery_mv, 3700);
    EXPECT_EQ(captured_report.battery_percent, 85);
    EXPECT_EQ(captured_report.battery_state, farm::BatteryState::NORMAL);
    EXPECT_EQ(captured_report.max_current_ma, 650);
    EXPECT_EQ(captured_report.daily_yield_mah, 1200u);
    EXPECT_FALSE(captured_report.is_night_mode);
    EXPECT_EQ(captured_report.unix_time, 1700000000000ULL);
}

TEST_F(InaSensorTaskTest, ProcessCycleSendsZeroTimeWhenNotSynchronized)
{
    init_sut();

    EXPECT_CALL(mock_time_, is_synchronized()).WillOnce(Return(false));

    int32_t read_shunt_uv = 50000;
    EXPECT_CALL(mock_driver_, read_shunt_voltage_uv(_))
        .WillRepeatedly(DoAll(SetArgReferee<0>(read_shunt_uv), Return(ESP_OK)));
    EXPECT_CALL(mock_driver_, read_alert_flags(_)).WillOnce(Return(ESP_OK));

    farm::SolarSensorReport captured_report{};
    EXPECT_CALL(mock_espnow_, send_data(ReservedIds::HUB, _, _, _, false))
        .WillOnce([&](NodeId, uint8_t, const void* data, size_t, bool) {
            captured_report = *static_cast<const farm::SolarSensorReport*>(data);
            return ESP_OK;
        });

    EXPECT_CALL(mock_rtos_, queue_send(dummy_queue_, _, 0)).WillOnce(Return(pdTRUE));

    sut_->process_cycle();

    EXPECT_EQ(captured_report.unix_time, 0u);
}

TEST_F(InaSensorTaskTest, ProcessCycleReportingDisabledSuppressesEspNow)
{
    init_sut();
    sut_->set_reporting_enabled(false);

    int32_t read_shunt_uv = 50000;
    EXPECT_CALL(mock_driver_, read_shunt_voltage_uv(_))
        .WillRepeatedly(DoAll(SetArgReferee<0>(read_shunt_uv), Return(ESP_OK)));
    EXPECT_CALL(mock_driver_, read_alert_flags(_))
        .WillOnce(DoAll(SetArgReferee<0>(0), Return(ESP_OK)));
    EXPECT_CALL(mock_espnow_, send_data(_, _, _, _, _)).Times(0);
    EXPECT_CALL(mock_rtos_, queue_send(dummy_queue_, _, 0))
        .WillOnce(Return(pdTRUE));

    sut_->process_cycle();
}

TEST_F(InaSensorTaskTest, ProcessCycleSamplingDisabledAcknowledgesAlertWithoutProcessing)
{
    sut_->set_sampling_enabled(false);

    EXPECT_CALL(mock_driver_, read_alert_flags(_)).WillOnce(Return(ESP_OK));
    EXPECT_CALL(mock_driver_, read_shunt_voltage_uv(_)).Times(0);
    EXPECT_CALL(mock_espnow_, send_data(_, _, _, _, _)).Times(0);
    EXPECT_CALL(mock_rtos_, queue_send(_, _, _)).Times(0);

    sut_->process_cycle();
}

TEST_F(InaSensorTaskTest, ReadErrorEnqueuesSampleWithErrorStatus)
{
    // Task just enqueues the error sample — no power cycling, no reset.
    // Recovery is the app's responsibility.
    init_sut();

    EXPECT_CALL(mock_driver_, read_shunt_voltage_uv(_)).WillOnce(Return(ESP_ERR_TIMEOUT));

    // Alert flags are only acknowledged after a successful read.
    EXPECT_CALL(mock_driver_, read_alert_flags(_)).Times(0);

    // Sample must be enqueued even on error, so the app can count failures
    InaSample captured_sample{};
    EXPECT_CALL(mock_rtos_, queue_send(dummy_queue_, _, 0))
        .WillOnce([&](QueueHandle_t, const void* item, TickType_t) {
            captured_sample = *static_cast<const InaSample*>(item);
            return pdTRUE;
        });

    sut_->process_cycle();

    EXPECT_EQ(captured_sample.status, ESP_ERR_TIMEOUT);
}

TEST_F(InaSensorTaskTest, PrepareForSleepAppliesNightConfigAndArmsDawnAlert)
{
    // Base values that must be preserved through the night switch
    base_config_.r_shunt_ohms = 0.05f;
    base_config_.max_expected_current_a = 1.0f;
    base_config_.mode = OperatingMode::SHUNT_AND_BUS_CONTINUOUS;
    base_config_.avg_mode = AveragingMode::AVG_64;
    base_config_.vbus_ct = ConversionTime::CT_1100US;
    base_config_.vsh_ct = ConversionTime::CT_1100US;

    ina226::Ina226Config captured{};
    EXPECT_CALL(mock_driver_, get_config()).WillRepeatedly(ReturnRef(base_config_));
    EXPECT_CALL(mock_driver_, set_config(_))
        .WillOnce([&](const ina226::Ina226Config& cfg) {
            captured = cfg;
            return ESP_OK;
        });
    EXPECT_CALL(mock_driver_, configure_alert(
        static_cast<uint16_t>(AlertFlag::SHUNT_OVER_VOLTAGE), DEFAULT_DAWN_WAKEUP_ALERT_LIMIT))
        .WillOnce(Return(ESP_OK));
    EXPECT_CALL(mock_driver_, read_alert_flags(_)).WillOnce(Return(ESP_OK));

    EXPECT_EQ(sut_->prepare_for_sleep(), ESP_OK);
    EXPECT_FALSE(sut_->is_sampling_enabled());
    EXPECT_FALSE(sut_->is_reporting_enabled());

    // Night regime uses slow conversions (CT_8244US) to save power; base fields preserved
    EXPECT_EQ(captured.avg_mode, AveragingMode::AVG_1024);
    EXPECT_EQ(captured.vbus_ct, ConversionTime::CT_8244US);
    EXPECT_EQ(captured.vsh_ct, ConversionTime::CT_8244US);
    EXPECT_EQ(captured.r_shunt_ohms, 0.05f);
    EXPECT_EQ(captured.max_expected_current_a, 1.0f);
    EXPECT_EQ(captured.mode, OperatingMode::SHUNT_AND_BUS_CONTINUOUS);
}

TEST_F(InaSensorTaskTest, SamplePeriodAndWatchdogTimeoutFromActiveDriverConfig)
{
    // Day regime conversion settings as configured in the driver (main.cpp)
    base_config_.avg_mode = AveragingMode::AVG_64;
    base_config_.vbus_ct = ConversionTime::CT_1100US;
    base_config_.vsh_ct = ConversionTime::CT_1100US;

    EXPECT_CALL(mock_driver_, init(NULL_BUS)).WillOnce(Return(ESP_OK));
    EXPECT_CALL(mock_driver_, configure_alert(CNVR_ALERT_MASK, 0)).WillOnce(Return(ESP_OK));
    EXPECT_CALL(mock_driver_, get_config()).WillRepeatedly(ReturnRef(base_config_));
    EXPECT_CALL(mock_timer_, get_time_us()).WillOnce(Return(0));
    sut_->init(InaSensorConfig{}, NULL_BUS);

    // Period: (1100 + 1100) * 64 / 1000 = 140 ms
    EXPECT_EQ(sut_->get_expected_sample_period_ms(), 140u);
    // Timeout: max(500, 140 * 3) = 500 ms
    EXPECT_EQ(sut_->get_watchdog_timeout_ms(), 500u);
}

TEST_F(InaSensorTaskTest, ProcessCycleBypassesEmaWhenDisabled)
{
    InaSensorConfig config{};
    config.enable_ema_filter = false;

    EXPECT_CALL(mock_driver_, init(NULL_BUS)).WillOnce(Return(ESP_OK));
    EXPECT_CALL(mock_driver_, configure_alert(CNVR_ALERT_MASK, 0)).WillOnce(Return(ESP_OK));
    EXPECT_CALL(mock_timer_, get_time_us()).WillRepeatedly(Return(1000000));

    sut_->init(config, NULL_BUS);
    sut_->set_reporting_enabled(true);

    int32_t read_shunt_uv = 50000; // 500 mA
    EXPECT_CALL(mock_driver_, read_shunt_voltage_uv(_))
        .WillRepeatedly(DoAll(SetArgReferee<0>(read_shunt_uv), Return(ESP_OK)));
    EXPECT_CALL(mock_driver_, read_alert_flags(_)).WillOnce(DoAll(SetArgReferee<0>(0), Return(ESP_OK)));

    InaSample captured_sample{};
    EXPECT_CALL(mock_rtos_, queue_send(dummy_queue_, _, 0))
        .WillOnce([&](QueueHandle_t, const void* item, TickType_t) {
            captured_sample = *static_cast<const InaSample*>(item);
            return pdTRUE;
        });

    sut_->process_cycle();

    EXPECT_EQ(captured_sample.isc_current_ma, 500u);
}

TEST_F(InaSensorTaskTest, StartAndStopToggleFlags)
{
    init_sut();

    sut_->stop();
    EXPECT_FALSE(sut_->is_sampling_enabled());
    EXPECT_FALSE(sut_->is_reporting_enabled());

    EXPECT_EQ(sut_->start(), ESP_OK);
    EXPECT_TRUE(sut_->is_sampling_enabled());
    EXPECT_TRUE(sut_->is_reporting_enabled());
}

TEST_F(InaSensorTaskTest, DeinitCallsStopAndDriverDeinit)
{
    init_sut();

    EXPECT_CALL(mock_driver_, deinit()).WillOnce(Return(ESP_OK));

    EXPECT_EQ(sut_->deinit(), ESP_OK);
    EXPECT_FALSE(sut_->is_sampling_enabled());
    EXPECT_FALSE(sut_->is_reporting_enabled());
}

