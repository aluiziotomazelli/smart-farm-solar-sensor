#pragma once

#include <gmock/gmock.h>
#include "interfaces/i_time_manager.hpp"

namespace time_manager {

class MockTimeManager : public ITimeManager
{
public:
    MOCK_METHOD(esp_err_t, init, (const TimeManagerConfig& config), (override));
    MOCK_METHOD(esp_err_t, start_sntp, (), (override));
    MOCK_METHOD(esp_err_t, stop_sntp, (), (override));
    MOCK_METHOD(esp_err_t, request_sync, (), (override));
    MOCK_METHOD(bool, is_synchronized, (), (const, override));
    MOCK_METHOD(time_t, get_timestamp_sec, (), (const, override));
    MOCK_METHOD(uint64_t, get_timestamp_ms, (), (const, override));
    MOCK_METHOD(bool, get_formatted_time, (char* buf, size_t max_len, const char* format), (const, override));
    MOCK_METHOD(void, set_timezone, (const char* tz), (override));
    MOCK_METHOD(TimeSyncPacket, create_time_packet, (), (const, override));
    MOCK_METHOD(esp_err_t, sync_from_time_packet, (const TimeSyncPacket& packet), (override));
};

} // namespace time_manager
