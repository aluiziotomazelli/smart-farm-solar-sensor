#include "command_handler.hpp"
#include "esp_log.h"
#include "farm_protocol_types.hpp"

static const char* TAG = "CommandHandler";

CommandHandler::CommandHandler(
    QueueHandle_t rx_queue,
    espnow::IEspNowManager& espnow,
    time_manager::ITimeManager& time_manager,
    CoreStorage& core,
    idf_hals::IHalFreertos& hal_freertos)
    : rx_queue_(rx_queue)
    , espnow_(espnow)
    , time_manager_(time_manager)
    , core_(core)
    , hal_freertos_(hal_freertos)
{
}

CommandProcessResult CommandHandler::process()
{
    CommandProcessResult result{};

    if (rx_queue_ == nullptr) {
        return result;
    }

    espnow::AppMessage msg{};
    while (hal_freertos_.queue_receive(rx_queue_, &msg, 0) == pdTRUE) {
        if (msg.msg_type != espnow::MessageType::COMMAND) {
            ESP_LOGW(TAG, "Ignoring non-command message of type 0x%02X from 0x%02X",
                     static_cast<uint8_t>(msg.msg_type), msg.sender_id);
            if (msg.requires_ack) {
                espnow_.confirm_reception(msg.sender_id, msg.sequence_number, espnow::AckStatus::ERROR_INVALID_DATA);
            }
            continue;
        }

        uint8_t cmd_type = msg.payload_type;
        ESP_LOGI(TAG, "Processing command 0x%02X from 0x%02X", cmd_type, msg.sender_id);

        if (cmd_type == static_cast<uint8_t>(espnow::CommandType::START_OTA)) {
            result.ota_requested = true;
            if (msg.requires_ack) {
                espnow_.confirm_reception(msg.sender_id, msg.sequence_number, espnow::AckStatus::OK);
            }
        }
        else if (cmd_type == static_cast<uint8_t>(espnow::CommandType::REBOOT)) {
            result.reboot_requested = true;
            if (msg.requires_ack) {
                espnow_.confirm_reception(msg.sender_id, msg.sequence_number, espnow::AckStatus::OK);
            }
            ESP_LOGW(TAG, "REBOOT command received.");
        }
        else if (cmd_type == static_cast<uint8_t>(farm::CommandType::SYNC_TIME)) {
            if (msg.payload_len >= sizeof(time_manager::TimeSyncPacket)) {
                const auto* packet = reinterpret_cast<const time_manager::TimeSyncPacket*>(msg.payload);
                esp_err_t err = time_manager_.sync_from_time_packet(*packet);
                if (err == ESP_OK) {
                    core_.has_valid_time = time_manager_.is_synchronized();
                    core_.last_sync_unix_time_ms = time_manager_.get_timestamp_ms();
                    result.core_modified = true;
                    if (msg.requires_ack) {
                        espnow_.confirm_reception(msg.sender_id, msg.sequence_number, espnow::AckStatus::OK);
                    }
                } else {
                    ESP_LOGE(TAG, "Time sync failed: %s", esp_err_to_name(err));
                    if (msg.requires_ack) {
                        espnow_.confirm_reception(msg.sender_id, msg.sequence_number, espnow::AckStatus::ERROR_PROCESSING);
                    }
                }
            } else {
                ESP_LOGE(TAG, "Invalid payload length for SYNC_TIME: %zu", msg.payload_len);
                if (msg.requires_ack) {
                    espnow_.confirm_reception(msg.sender_id, msg.sequence_number, espnow::AckStatus::ERROR_INVALID_DATA);
                }
            }
        }
        else if (cmd_type == static_cast<uint8_t>(espnow::CommandType::SET_REPORT_INTERVAL) ||
                 cmd_type == static_cast<uint8_t>(farm::CommandType::SLEEP_OVERRIDE) ||
                 cmd_type == static_cast<uint8_t>(farm::CommandType::PUMP_TURN_ON) ||
                 cmd_type == static_cast<uint8_t>(farm::CommandType::PUMP_TURN_OFF)) {
            ESP_LOGW(TAG, "Command 0x%02X not supported by SolarSensor", cmd_type);
            if (msg.requires_ack) {
                espnow_.confirm_reception(msg.sender_id, msg.sequence_number, espnow::AckStatus::ERROR_INVALID_DATA);
            }
        }
        else {
            ESP_LOGE(TAG, "Unknown command 0x%02X", cmd_type);
            if (msg.requires_ack) {
                espnow_.confirm_reception(msg.sender_id, msg.sequence_number, espnow::AckStatus::ERROR_INVALID_DATA);
            }
        }
    }

    return result;
}
