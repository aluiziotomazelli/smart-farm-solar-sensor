#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <memory>

#include "command_handler.hpp"
#include "mock_espnow_manager.hpp"
#include "mock_time_manager.hpp"
#include "mock_hal_system.hpp"
#include "mock_hal_freertos.hpp"

using ::testing::_;
using ::testing::DoAll;
using ::testing::NiceMock;
using ::testing::Return;

class MockOtaTriggerInHandler : public IOtaTrigger
{
public:
    MOCK_METHOD(esp_err_t, arm, (IOtaTriggerListener& listener), (override));
    MOCK_METHOD(void, disarm, (), (override));
    MOCK_METHOD(void, notify, (), (override));
};

class CommandHandlerTest : public ::testing::Test
{
protected:
    QueueHandle_t rx_queue_{nullptr};
    NiceMock<espnow::MockEspNowManager> mock_espnow_;
    NiceMock<time_manager::MockTimeManager> mock_time_;
    NiceMock<MockOtaTriggerInHandler> mock_ota_trigger_;
    NiceMock<idf_hals::MockSystemHAL> mock_hal_system_;
    NiceMock<idf_hals::MockHalFreertos> mock_freertos_;
    CoreStorage core_{};

    std::unique_ptr<CommandHandler> sut_;

    void SetUp() override
    {
        rx_queue_ = xQueueCreate(10, sizeof(espnow::AppMessage));
        ASSERT_NE(rx_queue_, nullptr);

        ON_CALL(mock_freertos_, queue_receive(_, _, _))
            .WillByDefault([](QueueHandle_t q, void* data, TickType_t ticks) {
                return xQueueReceive(q, data, ticks);
            });

        sut_ = std::make_unique<CommandHandler>(
            rx_queue_,
            mock_espnow_,
            mock_time_,
            mock_ota_trigger_,
            mock_hal_system_,
            core_,
            mock_freertos_);
    }

    void TearDown() override
    {
        if (rx_queue_) {
            vQueueDelete(rx_queue_);
            rx_queue_ = nullptr;
        }
    }
};

TEST_F(CommandHandlerTest, ProcessDrainsEmptyQueue)
{
    sut_->process();
}

TEST_F(CommandHandlerTest, ProcessStartOtaNotifiesTriggerAndConfirmsAck)
{
    espnow::AppMessage msg{};
    msg.sender_id = 0x01;
    msg.msg_type = espnow::MessageType::COMMAND;
    msg.payload_type = static_cast<uint8_t>(espnow::CommandType::START_OTA);
    msg.sequence_number = 42;
    msg.requires_ack = true;

    ASSERT_EQ(xQueueSend(rx_queue_, &msg, 0), pdTRUE);

    EXPECT_CALL(mock_ota_trigger_, notify()).Times(1);
    EXPECT_CALL(mock_espnow_, confirm_reception(0x01, 42, espnow::AckStatus::OK)).WillOnce(Return(ESP_OK));

    sut_->process();
}

TEST_F(CommandHandlerTest, ProcessRebootConfirmsAckAndRestarts)
{
    espnow::AppMessage msg{};
    msg.sender_id = 0x01;
    msg.msg_type = espnow::MessageType::COMMAND;
    msg.payload_type = static_cast<uint8_t>(espnow::CommandType::REBOOT);
    msg.sequence_number = 100;
    msg.requires_ack = true;

    ASSERT_EQ(xQueueSend(rx_queue_, &msg, 0), pdTRUE);

    EXPECT_CALL(mock_espnow_, confirm_reception(0x01, 100, espnow::AckStatus::OK)).WillOnce(Return(ESP_OK));
    EXPECT_CALL(mock_hal_system_, restart()).Times(1);

    sut_->process();
}

TEST_F(CommandHandlerTest, ProcessSyncTimeUpdatesTimeAndCoreState)
{
    espnow::AppMessage msg{};
    msg.sender_id = 0x01;
    msg.msg_type = espnow::MessageType::COMMAND;
    msg.payload_type = static_cast<uint8_t>(farm::CommandType::SYNC_TIME);
    msg.sequence_number = 200;
    msg.requires_ack = true;

    time_manager::TimeSyncPacket sync_packet{};
    sync_packet.timestamp_ms = 1700000000000ULL;
    sync_packet.tz_offset_min = -240;
    sync_packet.sync_source = time_manager::TimeSyncSource::ESP_NOW;
    sync_packet.flags = 0x01;

    std::memcpy(msg.payload, &sync_packet, sizeof(sync_packet));
    msg.payload_len = sizeof(sync_packet);

    ASSERT_EQ(xQueueSend(rx_queue_, &msg, 0), pdTRUE);

    EXPECT_CALL(mock_time_, sync_from_time_packet(_)).WillOnce(Return(ESP_OK));
    EXPECT_CALL(mock_time_, is_synchronized()).WillOnce(Return(true));
    EXPECT_CALL(mock_time_, get_timestamp_ms()).WillOnce(Return(1700000000000ULL));
    EXPECT_CALL(mock_espnow_, confirm_reception(0x01, 200, espnow::AckStatus::OK)).WillOnce(Return(ESP_OK));

    sut_->process();

    EXPECT_TRUE(core_.has_valid_time);
    EXPECT_EQ(core_.last_sync_unix_time_ms, 1700000000000ULL);
}

TEST_F(CommandHandlerTest, ProcessUnsupportedCommandRejectsWithInvalidData)
{
    espnow::AppMessage msg{};
    msg.sender_id = 0x01;
    msg.msg_type = espnow::MessageType::COMMAND;
    msg.payload_type = static_cast<uint8_t>(espnow::CommandType::SET_REPORT_INTERVAL);
    msg.sequence_number = 300;
    msg.requires_ack = true;

    ASSERT_EQ(xQueueSend(rx_queue_, &msg, 0), pdTRUE);

    EXPECT_CALL(mock_espnow_, confirm_reception(0x01, 300, espnow::AckStatus::ERROR_INVALID_DATA)).WillOnce(Return(ESP_OK));

    sut_->process();
}

TEST_F(CommandHandlerTest, ProcessNonCommandMessageRejectsWithInvalidData)
{
    espnow::AppMessage msg{};
    msg.sender_id = 0x01;
    msg.msg_type = espnow::MessageType::DATA;
    msg.payload_type = 0x01;
    msg.sequence_number = 400;
    msg.requires_ack = true;

    ASSERT_EQ(xQueueSend(rx_queue_, &msg, 0), pdTRUE);

    EXPECT_CALL(mock_espnow_, confirm_reception(0x01, 400, espnow::AckStatus::ERROR_INVALID_DATA)).WillOnce(Return(ESP_OK));

    sut_->process();
}
