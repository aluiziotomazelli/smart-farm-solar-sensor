#pragma once

#include "gmock/gmock.h"
#include "interfaces/i_espnow_manager.hpp"

namespace espnow {

class MockEspNowManager : public IEspNowManager
{
public:
    MOCK_METHOD(esp_err_t, init, (const EspNowConfig& config), (override));
    MOCK_METHOD(void, deinit, (), (override));
    MOCK_METHOD(void, set_channel_policy, (ChannelPolicy policy), (override));

    MOCK_METHOD(
        esp_err_t,
        send_data,
        (NodeId dest_node_id, PayloadType payload_type, const void* payload, size_t len, bool require_ack),
        (override));

    MOCK_METHOD(
        esp_err_t,
        send_command,
        (NodeId dest_node_id, CommandType command_type, const void* payload, size_t len, bool require_ack),
        (override));

    MOCK_METHOD(
        esp_err_t,
        confirm_reception,
        (NodeId sender_id, uint16_t sequence_number, AckStatus status),
        (override));

    MOCK_METHOD(
        esp_err_t,
        add_peer,
        (NodeId node_id, const uint8_t* mac, NodeType type, uint32_t heartbeat_interval_ms),
        (override));

    MOCK_METHOD(esp_err_t, remove_peer, (NodeId node_id), (override));

    MOCK_METHOD((etl::vector<PeerInfo, MAX_PEERS>), get_peers, (), (override));
    MOCK_METHOD(bool, get_peer_stats, (NodeId node_id, PeerStatistics& out), (const, override));
    MOCK_METHOD((etl::vector<PeerStatistics, MAX_PEERS>), get_all_peer_stats, (), (const, override));
    MOCK_METHOD((etl::vector<NodeId, MAX_PEERS>), get_offline_peers, (), (const, override));

    MOCK_METHOD(esp_err_t, start_pairing, (uint32_t timeout_ms), (override));
    MOCK_METHOD(esp_err_t, reconnect, (), (override));

    MOCK_METHOD(NodeState, get_node_state, (), (const, override));
    MOCK_METHOD(bool, is_initialized, (), (const, override));
};

} // namespace espnow
