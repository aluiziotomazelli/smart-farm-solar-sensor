#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <memory>

#include "command_handler.hpp"
#include "mocks/mock_espnow_manager.hpp"
#include "mock_time_manager.hpp"
#include "mock_hal_freertos.hpp"

using ::testing::_;
using ::testing::DoAll;
using ::testing::NiceMock;
using ::testing::Return;

class CommandHandlerTest : public ::testing::Test
{
protected:
    QueueHandle_t rx_queue_{nullptr};
    NiceMock<espnow::MockEspNowManager> mock_espnow_;
    NiceMock<time_manager::MockTimeManager> mock_time_;
    NiceMock<idf_hals::MockHalFreertos> mock_freertos_;
    CoreData core_{};

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
    CommandProcessResult result = sut_->process();
    EXPECT_FALSE(result.core_modified);
    EXPECT_FALSE(result.ota_requested);
    EXPECT_FALSE(result.reboot_requested);
}

TEST_F(CommandHandlerTest, ProcessStartOtaConfirmsAckAndReturnsOtaRequested)
{
    espnow::AppMessage msg{};
    msg.sender_id = 0x01;
    msg.msg_type = espnow::MessageType::COMMAND;
    msg.payload_type = static_cast<uint8_t>(espnow::CommandType::START_OTA);
    msg.sequence_number = 42;
    msg.requires_ack = true;

    ASSERT_EQ(xQueueSend(rx_queue_, &msg, 0), pdTRUE);

    EXPECT_CALL(mock_espnow_, confirm_reception(0x01, 42, espnow::AckStatus::OK)).WillOnce(Return(ESP_OK));

    CommandProcessResult result = sut_->process();
    EXPECT_TRUE(result.ota_requested);
    EXPECT_FALSE(result.core_modified);
    EXPECT_FALSE(result.reboot_requested);
}

TEST_F(CommandHandlerTest, ProcessRebootConfirmsAckAndReturnsRebootRequested)
{
    espnow::AppMessage msg{};
    msg.sender_id = 0x01;
    msg.msg_type = espnow::MessageType::COMMAND;
    msg.payload_type = static_cast<uint8_t>(espnow::CommandType::REBOOT);
    msg.sequence_number = 100;
    msg.requires_ack = true;

    ASSERT_EQ(xQueueSend(rx_queue_, &msg, 0), pdTRUE);

    EXPECT_CALL(mock_espnow_, confirm_reception(0x01, 100, espnow::AckStatus::OK)).WillOnce(Return(ESP_OK));

    CommandProcessResult result = sut_->process();
    EXPECT_TRUE(result.reboot_requested);
    EXPECT_FALSE(result.core_modified);
    EXPECT_FALSE(result.ota_requested);
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

    CommandProcessResult result = sut_->process();

    EXPECT_TRUE(result.core_modified);
    EXPECT_FALSE(result.ota_requested);
    EXPECT_FALSE(result.reboot_requested);
    EXPECT_TRUE(core_.has_valid_time);
    EXPECT_EQ(core_.last_sync_unix_time_ms, 1700000000000ULL);
}

TEST_F(CommandHandlerTest, ProcessFailedSyncTimeDoesNotSetCoreModified)
{
    espnow::AppMessage msg{};
    msg.sender_id = 0x01;
    msg.msg_type = espnow::MessageType::COMMAND;
    msg.payload_type = static_cast<uint8_t>(farm::CommandType::SYNC_TIME);
    msg.sequence_number = 201;
    msg.requires_ack = true;

    time_manager::TimeSyncPacket sync_packet{};
    std::memcpy(msg.payload, &sync_packet, sizeof(sync_packet));
    msg.payload_len = sizeof(sync_packet);

    ASSERT_EQ(xQueueSend(rx_queue_, &msg, 0), pdTRUE);

    EXPECT_CALL(mock_time_, sync_from_time_packet(_)).WillOnce(Return(ESP_FAIL));
    EXPECT_CALL(mock_espnow_, confirm_reception(0x01, 201, espnow::AckStatus::ERROR_PROCESSING)).WillOnce(Return(ESP_OK));

    CommandProcessResult result = sut_->process();

    EXPECT_FALSE(result.core_modified);
    EXPECT_FALSE(result.ota_requested);
    EXPECT_FALSE(result.reboot_requested);
}

TEST_F(CommandHandlerTest, ProcessMultipleCommandsAccumulatesResultFlags)
{
    espnow::AppMessage msg1{};
    msg1.sender_id = 0x01;
    msg1.msg_type = espnow::MessageType::COMMAND;
    msg1.payload_type = static_cast<uint8_t>(farm::CommandType::SYNC_TIME);
    msg1.sequence_number = 202;
    msg1.requires_ack = false;

    time_manager::TimeSyncPacket sync_packet{};
    std::memcpy(msg1.payload, &sync_packet, sizeof(sync_packet));
    msg1.payload_len = sizeof(sync_packet);

    espnow::AppMessage msg2{};
    msg2.sender_id = 0x01;
    msg2.msg_type = espnow::MessageType::COMMAND;
    msg2.payload_type = static_cast<uint8_t>(espnow::CommandType::REBOOT);
    msg2.sequence_number = 203;
    msg2.requires_ack = false;

    ASSERT_EQ(xQueueSend(rx_queue_, &msg1, 0), pdTRUE);
    ASSERT_EQ(xQueueSend(rx_queue_, &msg2, 0), pdTRUE);

    EXPECT_CALL(mock_time_, sync_from_time_packet(_)).WillOnce(Return(ESP_OK));
    EXPECT_CALL(mock_time_, is_synchronized()).WillOnce(Return(true));
    EXPECT_CALL(mock_time_, get_timestamp_ms()).WillOnce(Return(1700000000000ULL));

    CommandProcessResult result = sut_->process();

    EXPECT_TRUE(result.core_modified);
    EXPECT_TRUE(result.reboot_requested);
    EXPECT_FALSE(result.ota_requested);
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

    CommandProcessResult result = sut_->process();
    EXPECT_FALSE(result.core_modified);
    EXPECT_FALSE(result.ota_requested);
    EXPECT_FALSE(result.reboot_requested);
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

    CommandProcessResult result = sut_->process();
    EXPECT_FALSE(result.core_modified);
    EXPECT_FALSE(result.ota_requested);
    EXPECT_FALSE(result.reboot_requested);
}
