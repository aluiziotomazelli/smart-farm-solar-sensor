// host_test/mocks/mock_ota_controller.hpp
#pragma once

#include "gmock/gmock.h"
#include "interfaces/i_ota_controller.hpp"

class MockOtaController : public IOtaController
{
public:
    MOCK_METHOD(bool, init, (const OtaConfig& config), (override));
    MOCK_METHOD(bool, check_pending_verify, (), (const, override));
    MOCK_METHOD(std::optional<OtaVersion>, get_running_version, (), (const, override));
    MOCK_METHOD(OtaActionResult, confirm_firmware, (bool session_healthy), (override));
    MOCK_METHOD(OtaActionResult, execute_download, (uint32_t timeout_ms), (override));
    MOCK_METHOD(void, rollback_and_reboot, (), (override));
};
