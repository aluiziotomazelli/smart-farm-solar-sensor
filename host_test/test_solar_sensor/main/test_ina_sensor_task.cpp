#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "ina_sensor_task.hpp"
#include "mock_ina226_driver.hpp"
#include "mock_espnow_manager.hpp"
#include "mock_hal_timer.hpp"
#include "mock_hal_freertos.hpp"

using ::testing::_;
using ::testing::NiceMock;
using ::testing::Return;
using ::testing::SetArgReferee;
using ::testing::DoAll;

using namespace ina;
using namespace ina226;
using namespace espnow;
using namespace idf_hals;

// Sentinel null bus handle used in unit tests (mock driver never dereferences it)
static constexpr i2c_master_bus_handle_t NULL_BUS = nullptr;

class InaSensorTaskTest : public ::testing::Test
{
protected:
    NiceMock<MockIna226Driver> mock_driver_;
    NiceMock<MockEspNowManager> mock_espnow_;
    NiceMock<MockTimerHAL> mock_timer_;
    NiceMock<MockHalFreertos> mock_rtos_;

    QueueHandle_t dummy_queue_ = reinterpret_cast<QueueHandle_t>(0x1234);

    std::unique_ptr<InaSensorTask> sut_;

    void SetUp() override
    {
        sut_ = std::make_unique<InaSensorTask>(
            mock_driver_,
            mock_espnow_,
            mock_timer_,
            mock_rtos_,
            dummy_queue_
        );
    }

    void init_sut(InaSensorConfig config = {})
    {
        // init(bus_handle) binds the driver to the I2C bus — mocked, handle is ignored
        EXPECT_CALL(mock_driver_, init(NULL_BUS)).WillOnce(Return(ESP_OK));
        EXPECT_CALL(mock_timer_, get_time_us()).WillRepeatedly(Return(1000000));
        sut_->init(config, NULL_BUS);
    }
};

TEST_F(InaSensorTaskTest, InitSuccess)
{
    EXPECT_CALL(mock_driver_, init(NULL_BUS)).WillOnce(Return(ESP_OK));
    EXPECT_CALL(mock_timer_, get_time_us()).WillOnce(Return(1000000));

    InaSensorConfig config{};
    EXPECT_EQ(sut_->init(config, NULL_BUS), ESP_OK);
}

TEST_F(InaSensorTaskTest, InitDriverFailure)
{
    EXPECT_CALL(mock_driver_, init(NULL_BUS)).WillOnce(Return(ESP_ERR_TIMEOUT));

    InaSensorConfig config{};
    EXPECT_EQ(sut_->init(config, NULL_BUS), ESP_ERR_TIMEOUT);
}

TEST_F(InaSensorTaskTest, ProcessCycleNormalSampling)
{
    init_sut();

    float read_current = 500.0f;
    uint16_t read_bus = 12000;
    EXPECT_CALL(mock_driver_, read_current_ma(_))
        .WillOnce(DoAll(SetArgReferee<0>(read_current), Return(ESP_OK)));
    EXPECT_CALL(mock_driver_, read_bus_voltage_mv(_))
        .WillOnce(DoAll(SetArgReferee<0>(read_bus), Return(ESP_OK)));
    EXPECT_CALL(mock_timer_, get_time_us()).WillRepeatedly(Return(1000000));
    EXPECT_CALL(mock_espnow_, send_data(ReservedIds::HUB, _, _, _, false))
        .WillOnce(Return(ESP_OK));
    EXPECT_CALL(mock_rtos_, queue_send(dummy_queue_, _, 0))
        .WillOnce(Return(pdTRUE));

    sut_->process_cycle();
}

TEST_F(InaSensorTaskTest, ProcessCycleReportingDisabledSuppressesEspNow)
{
    init_sut();
    sut_->set_reporting_enabled(false);

    float read_current = 500.0f;
    EXPECT_CALL(mock_driver_, read_current_ma(_))
        .WillOnce(DoAll(SetArgReferee<0>(read_current), Return(ESP_OK)));
    EXPECT_CALL(mock_espnow_, send_data(_, _, _, _, _)).Times(0);
    EXPECT_CALL(mock_rtos_, queue_send(dummy_queue_, _, 0))
        .WillOnce(Return(pdTRUE));

    sut_->process_cycle();
}

TEST_F(InaSensorTaskTest, ProcessCycleSamplingDisabledDoesNothing)
{
    sut_->set_sampling_enabled(false);

    EXPECT_CALL(mock_driver_, read_current_ma(_)).Times(0);
    EXPECT_CALL(mock_rtos_, queue_send(_, _, _)).Times(0);

    sut_->process_cycle();
}

TEST_F(InaSensorTaskTest, ReadErrorEnqueuesSampleWithErrorStatus)
{
    // Task just enqueues the error sample — no power cycling, no reset.
    // Recovery is the app's responsibility.
    init_sut();

    EXPECT_CALL(mock_driver_, read_current_ma(_)).WillOnce(Return(ESP_ERR_TIMEOUT));

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

TEST_F(InaSensorTaskTest, SetOperatingModeDayActiveConfiguresAlertConversionReady)
{
    EXPECT_CALL(mock_driver_, configure_alert(static_cast<uint16_t>(AlertFlag::CONVERSION_READY), 0))
        .WillOnce(Return(ESP_OK));

    EXPECT_EQ(sut_->set_operating_mode(SolarNodeState::DAY_ACTIVE), ESP_OK);
    EXPECT_EQ(sut_->get_operating_mode(), SolarNodeState::DAY_ACTIVE);
    EXPECT_TRUE(sut_->is_sampling_enabled());
}

TEST_F(InaSensorTaskTest, SetOperatingModeNightSleepConfiguresAlertShuntOverVoltage)
{
    EXPECT_CALL(mock_driver_, configure_alert(static_cast<uint16_t>(AlertFlag::SHUNT_OVER_VOLTAGE), 12))
        .WillOnce(Return(ESP_OK));

    EXPECT_EQ(sut_->set_operating_mode(SolarNodeState::NIGHT_SLEEP), ESP_OK);
    EXPECT_EQ(sut_->get_operating_mode(), SolarNodeState::NIGHT_SLEEP);
    EXPECT_FALSE(sut_->is_sampling_enabled());
}

TEST_F(InaSensorTaskTest, DynamicSamplePeriodAndWatchdogTimeoutCalculation)
{
    InaSensorConfig config{};
    config.day_config.vsh_ct = ConversionTime::CT_1100US;
    config.day_config.vbus_ct = ConversionTime::CT_1100US;
    config.day_config.avg_mode = AveragingMode::AVG_64;

    EXPECT_CALL(mock_driver_, init(NULL_BUS)).WillOnce(Return(ESP_OK));
    EXPECT_CALL(mock_timer_, get_time_us()).WillOnce(Return(0));
    sut_->init(config, NULL_BUS);

    // Period: (1100 + 1100) * 64 / 1000 = 140 ms
    EXPECT_EQ(sut_->get_expected_sample_period_ms(), 140u);
    // Timeout: max(500, 140 * 3) = 500 ms
    EXPECT_EQ(sut_->get_watchdog_timeout_ms(), 500u);
}
