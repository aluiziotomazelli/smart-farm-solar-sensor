// host_test/test_led_controller/main/test_led_controller.cpp
#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <memory>

#include "led_controller.hpp"
#include "mock_hal_gpio.hpp"
#include "mock_hal_freertos.hpp"

using ::testing::_;
using ::testing::DoAll;
using ::testing::InSequence;
using ::testing::NiceMock;
using ::testing::Return;
using ::testing::SetArgPointee;

class LedControllerTest : public ::testing::Test
{
protected:
    NiceMock<idf_hals::MockGpioHAL> hal_gpio_;
    NiceMock<idf_hals::MockHalFreertos> hal_rtos_;
    LedConfig config_{.gpio_num = GPIO_NUM_4, .task_stack_size = 2048, .task_priority = 1, .active_level = 1};
    std::unique_ptr<LedController> sut_;

    void SetUp() override
    {
        ON_CALL(hal_gpio_, set_direction(_, _)).WillByDefault(Return(ESP_OK));
        ON_CALL(hal_gpio_, set_level(_, _)).WillByDefault(Return(ESP_OK));
        ON_CALL(hal_rtos_, task_create(_, _, _, _, _, _)).WillByDefault(Return(pdPASS));

        sut_ = std::make_unique<LedController>(hal_gpio_, hal_rtos_, config_);
    }
};

TEST_F(LedControllerTest, InitSuccessConfiguresGpioAndSetsLevelLow)
{
    EXPECT_CALL(hal_gpio_, config(_)).WillOnce(Return(ESP_OK));
    EXPECT_CALL(hal_gpio_, set_level(GPIO_NUM_4, 0)).WillOnce(Return(ESP_OK));

    EXPECT_EQ(sut_->init(), ESP_OK);
}

TEST_F(LedControllerTest, InitFailsWithInvalidGpioNum)
{
    LedConfig invalid_config{.gpio_num = GPIO_NUM_NC};
    LedController invalid_sut(hal_gpio_, hal_rtos_, invalid_config);

    EXPECT_EQ(invalid_sut.init(), ESP_ERR_INVALID_ARG);
}

TEST_F(LedControllerTest, InitFailsWhenGpioConfigFails)
{
    EXPECT_CALL(hal_gpio_, config(_)).WillOnce(Return(ESP_FAIL));

    EXPECT_EQ(sut_->init(), ESP_FAIL);
}

TEST_F(LedControllerTest, StartCreatesTaskAndSetsRunning)
{
    EXPECT_CALL(hal_rtos_, task_create(_, _, 2048, sut_.get(), 1, _)).WillOnce(Return(pdPASS));

    EXPECT_EQ(sut_->start(), ESP_OK);
    EXPECT_TRUE(sut_->is_running());

    // Second start is idempotent
    EXPECT_EQ(sut_->start(), ESP_OK);
}

TEST_F(LedControllerTest, StartReturnsFailWhenTaskCreationFails)
{
    EXPECT_CALL(hal_rtos_, task_create(_, _, _, _, _, _)).WillOnce(Return(pdFAIL));

    EXPECT_EQ(sut_->start(), ESP_FAIL);
    EXPECT_FALSE(sut_->is_running());
}

TEST_F(LedControllerTest, StopDeletesTaskAndTurnsOffLed)
{
    TaskHandle_t dummy_handle = reinterpret_cast<TaskHandle_t>(0x1234);
    EXPECT_CALL(hal_rtos_, task_create(_, _, _, _, _, _))
        .WillOnce(DoAll(SetArgPointee<5>(dummy_handle), Return(pdPASS)));

    ASSERT_EQ(sut_->start(), ESP_OK);

    EXPECT_CALL(hal_gpio_, set_level(GPIO_NUM_4, 0)).Times(1);
    EXPECT_CALL(hal_rtos_, task_delete(dummy_handle)).Times(1);

    sut_->stop();
    EXPECT_FALSE(sut_->is_running());
}

TEST_F(LedControllerTest, PulseSetsGpioHighThenLowForDuration)
{
    sut_->pulse(50);

    InSequence seq;
    EXPECT_CALL(hal_gpio_, set_level(GPIO_NUM_4, 1)).Times(1);
    EXPECT_CALL(hal_rtos_, task_delay(pdMS_TO_TICKS(50))).Times(1);
    EXPECT_CALL(hal_gpio_, set_level(GPIO_NUM_4, 0)).Times(1);

    sut_->process_cycle();
}

TEST_F(LedControllerTest, BootSuccessPatternExecutesTwoPulsesAndReturnsToOff)
{
    sut_->set_pattern(BlinkPattern::BOOT_SUCCESS);
    EXPECT_EQ(sut_->get_current_pattern(), BlinkPattern::BOOT_SUCCESS);

    InSequence seq;
    // Pulse 1: ON + OFF
    EXPECT_CALL(hal_gpio_, set_level(GPIO_NUM_4, 1)).Times(1);
    EXPECT_CALL(hal_rtos_, task_delay(pdMS_TO_TICKS(100))).Times(1);
    EXPECT_CALL(hal_gpio_, set_level(GPIO_NUM_4, 0)).Times(1);
    EXPECT_CALL(hal_rtos_, task_delay(pdMS_TO_TICKS(100))).Times(1);

    // Pulse 2: ON + OFF
    EXPECT_CALL(hal_gpio_, set_level(GPIO_NUM_4, 1)).Times(1);
    EXPECT_CALL(hal_rtos_, task_delay(pdMS_TO_TICKS(100))).Times(1);
    EXPECT_CALL(hal_gpio_, set_level(GPIO_NUM_4, 0)).Times(1);
    EXPECT_CALL(hal_rtos_, task_delay(pdMS_TO_TICKS(100))).Times(1);

    sut_->process_cycle();
    EXPECT_EQ(sut_->get_current_pattern(), BlinkPattern::OFF);
}

TEST_F(LedControllerTest, EnterSleepPatternExecutesLongPulseAndReturnsToOff)
{
    sut_->set_pattern(BlinkPattern::ENTER_SLEEP);

    InSequence seq;
    EXPECT_CALL(hal_gpio_, set_level(GPIO_NUM_4, 1)).Times(1);
    EXPECT_CALL(hal_rtos_, task_delay(pdMS_TO_TICKS(300))).Times(1);
    EXPECT_CALL(hal_gpio_, set_level(GPIO_NUM_4, 0)).Times(1);

    sut_->process_cycle();
    EXPECT_EQ(sut_->get_current_pattern(), BlinkPattern::OFF);
}

TEST_F(LedControllerTest, ErrorBurstPatternExecutesFivePulsesAndReturnsToOff)
{
    sut_->set_pattern(BlinkPattern::ERROR_BURST);

    EXPECT_CALL(hal_gpio_, set_level(GPIO_NUM_4, 1)).Times(5);
    EXPECT_CALL(hal_gpio_, set_level(GPIO_NUM_4, 0)).Times(5);

    sut_->process_cycle();
    EXPECT_EQ(sut_->get_current_pattern(), BlinkPattern::OFF);
}

TEST_F(LedControllerTest, PairingModePatternExecutesBlinkCycle)
{
    sut_->set_pattern(BlinkPattern::PAIRING_MODE);

    InSequence seq;
    EXPECT_CALL(hal_gpio_, set_level(GPIO_NUM_4, 1)).Times(1);
    EXPECT_CALL(hal_rtos_, task_delay(pdMS_TO_TICKS(200))).Times(1);
    EXPECT_CALL(hal_gpio_, set_level(GPIO_NUM_4, 0)).Times(1);
    EXPECT_CALL(hal_rtos_, task_delay(pdMS_TO_TICKS(200))).Times(1);

    sut_->process_cycle();
    EXPECT_EQ(sut_->get_current_pattern(), BlinkPattern::PAIRING_MODE); // Retains looping state
}

TEST_F(LedControllerTest, IdleBeaconPatternExecutesDoubleFlash)
{
    sut_->set_pattern(BlinkPattern::IDLE_BEACON);

    EXPECT_CALL(hal_gpio_, set_level(GPIO_NUM_4, 1)).Times(2);
    EXPECT_CALL(hal_gpio_, set_level(GPIO_NUM_4, 0)).Times(2);

    sut_->process_cycle();
    EXPECT_EQ(sut_->get_current_pattern(), BlinkPattern::IDLE_BEACON);
}

TEST_F(LedControllerTest, ActiveLowConfigurationInvertsLevels)
{
    LedConfig active_low_config{
        .gpio_num = GPIO_NUM_4,
        .active_level = 0 // Active-Low
    };
    LedController active_low_sut(hal_gpio_, hal_rtos_, active_low_config);

    active_low_sut.pulse(20);

    InSequence seq;
    // On = 0 (Low), Off = 1 (High)
    EXPECT_CALL(hal_gpio_, set_level(GPIO_NUM_4, 0)).Times(1);
    EXPECT_CALL(hal_rtos_, task_delay(pdMS_TO_TICKS(20))).Times(1);
    EXPECT_CALL(hal_gpio_, set_level(GPIO_NUM_4, 1)).Times(1);

    active_low_sut.process_cycle();
}
