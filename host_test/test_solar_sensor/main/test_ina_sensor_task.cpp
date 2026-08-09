#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "ina_sensor_task.hpp"
#include "mock_ina226_driver.hpp"
#include "mock_power_control.hpp"
#include "mock_espnow_manager.hpp"
#include "mock_hal_timer.hpp"
#include "mock_hal_freertos.hpp"

using ::testing::_;
using ::testing::NiceMock;
using ::testing::Return;
using ::testing::SetArgReferee;
using ::testing::DoAll;
using ::testing::InSequence;

using namespace ina;
using namespace ina226;
using namespace power_control;
using namespace espnow;
using namespace idf_hals;

class InaSensorTaskTest : public ::testing::Test
{
protected:
    NiceMock<MockIna226Driver> mock_driver_;
    NiceMock<MockPowerControl> mock_power_control_;
    NiceMock<MockEspNowManager> mock_espnow_;
    NiceMock<MockTimerHAL> mock_timer_;
    NiceMock<MockHalFreertos> mock_rtos_;

    QueueHandle_t dummy_queue_ = reinterpret_cast<QueueHandle_t>(0x1234);

    std::unique_ptr<InaSensorTask> sut_;

    void SetUp() override
    {
        sut_ = std::make_unique<InaSensorTask>(
            mock_driver_,
            mock_power_control_,
            mock_espnow_,
            mock_timer_,
            mock_rtos_,
            dummy_queue_
        );
    }
};

TEST_F(InaSensorTaskTest, InitSuccess)
{
    EXPECT_CALL(mock_power_control_, init()).WillOnce(Return(ESP_OK));
    EXPECT_CALL(mock_power_control_, turn_on()).WillOnce(Return(ESP_OK));
    EXPECT_CALL(mock_driver_, init()).WillOnce(Return(ESP_OK));
    EXPECT_CALL(mock_timer_, get_time_us()).WillOnce(Return(1000000));

    InaSensorConfig config{};
    EXPECT_EQ(sut_->init(config), ESP_OK);
}

TEST_F(InaSensorTaskTest, InitPowerControlFailure)
{
    EXPECT_CALL(mock_power_control_, init()).WillOnce(Return(ESP_FAIL));

    InaSensorConfig config{};
    EXPECT_EQ(sut_->init(config), ESP_FAIL);
}

TEST_F(InaSensorTaskTest, InitDriverFailure)
{
    EXPECT_CALL(mock_power_control_, init()).WillOnce(Return(ESP_OK));
    EXPECT_CALL(mock_power_control_, turn_on()).WillOnce(Return(ESP_OK));
    EXPECT_CALL(mock_driver_, init()).WillOnce(Return(ESP_ERR_TIMEOUT));

    InaSensorConfig config{};
    EXPECT_EQ(sut_->init(config), ESP_ERR_TIMEOUT);
}

TEST_F(InaSensorTaskTest, ProcessCycleNormalSampling)
{
    InaSensorConfig config{};
    EXPECT_CALL(mock_power_control_, init()).WillOnce(Return(ESP_OK));
    EXPECT_CALL(mock_power_control_, turn_on()).WillOnce(Return(ESP_OK));
    EXPECT_CALL(mock_driver_, init()).WillOnce(Return(ESP_OK));
    EXPECT_CALL(mock_timer_, get_time_us()).WillRepeatedly(Return(1000000));
    sut_->init(config);

    // Initial cycle with 500mA
    float read_current = 500.0f;
    uint16_t read_bus = 12000;
    EXPECT_CALL(mock_driver_, read_current_ma(_))
        .WillOnce(DoAll(SetArgReferee<0>(read_current), Return(ESP_OK)));
    EXPECT_CALL(mock_driver_, read_bus_voltage_mv(_))
        .WillOnce(DoAll(SetArgReferee<0>(read_bus), Return(ESP_OK)));

    EXPECT_CALL(mock_espnow_, send_data(ReservedIds::HUB, _, _, _, false))
        .WillOnce(Return(ESP_OK));

    EXPECT_CALL(mock_rtos_, queue_send(dummy_queue_, _, 0))
        .WillOnce(Return(pdTRUE));

    sut_->process_cycle();
}

TEST_F(InaSensorTaskTest, ProcessCycleReportingDisabledSuppressesEspNow)
{
    InaSensorConfig config{};
    EXPECT_CALL(mock_power_control_, init()).WillOnce(Return(ESP_OK));
    EXPECT_CALL(mock_power_control_, turn_on()).WillOnce(Return(ESP_OK));
    EXPECT_CALL(mock_driver_, init()).WillOnce(Return(ESP_OK));
    sut_->init(config);

    sut_->set_reporting_enabled(false);

    float read_current = 500.0f;
    EXPECT_CALL(mock_driver_, read_current_ma(_))
        .WillOnce(DoAll(SetArgReferee<0>(read_current), Return(ESP_OK)));

    // Should NOT call send_data when reporting is disabled
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

TEST_F(InaSensorTaskTest, ConsecutiveI2cErrorsTriggerHardReset)
{
    InaSensorConfig config{};
    EXPECT_CALL(mock_power_control_, init()).WillOnce(Return(ESP_OK));
    EXPECT_CALL(mock_power_control_, turn_on()).WillOnce(Return(ESP_OK));
    EXPECT_CALL(mock_driver_, init()).WillOnce(Return(ESP_OK));
    sut_->init(config);

    // Fail 1
    EXPECT_CALL(mock_driver_, read_current_ma(_)).WillOnce(Return(ESP_ERR_TIMEOUT));
    sut_->process_cycle();

    // Fail 2
    EXPECT_CALL(mock_driver_, read_current_ma(_)).WillOnce(Return(ESP_ERR_TIMEOUT));
    sut_->process_cycle();

    // Fail 3 -> Should trigger hard_reset_ina_power (turn_off, delay, turn_on, delay, init)
    EXPECT_CALL(mock_driver_, read_current_ma(_)).WillOnce(Return(ESP_ERR_TIMEOUT));
    EXPECT_CALL(mock_power_control_, turn_off()).WillOnce(Return(ESP_OK));
    EXPECT_CALL(mock_power_control_, turn_on()).WillOnce(Return(ESP_OK));
    EXPECT_CALL(mock_driver_, init()).WillOnce(Return(ESP_OK));

    sut_->process_cycle();
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
