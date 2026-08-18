#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <memory>

#include "ota_controller.hpp"
#include "mock_ota_manager.hpp"
#include "mock_hal_freertos.hpp"

using ::testing::_;
using ::testing::NiceMock;
using ::testing::Return;

class OtaControllerTest : public ::testing::Test
{
protected:
    NiceMock<MockOtaManager> mock_ota_manager_;
    NiceMock<idf_hals::MockHalFreertos> mock_freertos_;

    std::unique_ptr<OtaController> sut_;

    void SetUp() override { sut_ = std::make_unique<OtaController>(mock_ota_manager_, mock_freertos_); }
};

TEST_F(OtaControllerTest, CheckPendingVerifyReturnsTrue)
{
    EXPECT_CALL(mock_ota_manager_, check_pending_verify()).WillOnce(Return(true));
    EXPECT_TRUE(sut_->check_pending_verify());
}

TEST_F(OtaControllerTest, CheckPendingVerifyReturnsFalse)
{
    EXPECT_CALL(mock_ota_manager_, check_pending_verify()).WillOnce(Return(false));
    EXPECT_FALSE(sut_->check_pending_verify());
}

TEST_F(OtaControllerTest, ConfirmFirmwareHealthySuccess)
{
    EXPECT_CALL(mock_ota_manager_, confirm_app_valid()).WillOnce(Return(true));

    OtaActionResult result = sut_->confirm_firmware(true);
    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.exec_result, farm::OtaExecResult::CONFIRMED_SUCCESS);
    EXPECT_EQ(result.error_code, farm::OtaErrorCode::NONE);
}

TEST_F(OtaControllerTest, ConfirmFirmwareUnhealthyTriggersRollback)
{
    // When session is unhealthy, confirm_app_valid should NOT even be called
    EXPECT_CALL(mock_ota_manager_, confirm_app_valid()).Times(0);

    OtaActionResult result = sut_->confirm_firmware(false);
    EXPECT_FALSE(result.success);
    EXPECT_EQ(result.exec_result, farm::OtaExecResult::ROLLBACK_TRIGGERED);
    EXPECT_EQ(result.error_code, farm::OtaErrorCode::HEALTH_CHECK_FAILED);
}

TEST_F(OtaControllerTest, ConfirmFirmwareHealthyPartitionConfirmFailed)
{
    EXPECT_CALL(mock_ota_manager_, confirm_app_valid()).WillOnce(Return(false));

    OtaActionResult result = sut_->confirm_firmware(true);
    EXPECT_FALSE(result.success);
    EXPECT_EQ(result.exec_result, farm::OtaExecResult::ROLLBACK_TRIGGERED);
    EXPECT_EQ(result.error_code, farm::OtaErrorCode::PARTITION_CONFIRM_FAILED);
}

TEST_F(OtaControllerTest, GetRunningVersionReturnsVersionWhenPresent)
{
    OtaVersion ver{1, 2, 3};
    EXPECT_CALL(mock_ota_manager_, get_running_version()).WillOnce(Return(ver));

    auto result = sut_->get_running_version();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->major, 1);
    EXPECT_EQ(result->minor, 2);
    EXPECT_EQ(result->patch, 3);
}

TEST_F(OtaControllerTest, GetRunningVersionReturnsNulloptWhenEmpty)
{
    EXPECT_CALL(mock_ota_manager_, get_running_version()).WillOnce(Return(std::nullopt));

    auto result = sut_->get_running_version();
    EXPECT_FALSE(result.has_value());
}

TEST_F(OtaControllerTest, InitDelegatesToManager)
{
    OtaConfig config{};
    EXPECT_CALL(mock_ota_manager_, init(_)).WillOnce(Return(true));
    EXPECT_TRUE(sut_->init(config));
}

TEST_F(OtaControllerTest, RollbackAndRebootDelegatesToManager)
{
    EXPECT_CALL(mock_ota_manager_, rollback_and_reboot()).Times(1);
    sut_->rollback_and_reboot();
}

TEST_F(OtaControllerTest, ExecuteDownloadStartOtaFail)
{
    EXPECT_CALL(mock_ota_manager_, start_ota()).WillOnce(Return(false));

    OtaActionResult result = sut_->execute_download();
    EXPECT_FALSE(result.success);
    EXPECT_EQ(result.exec_result, farm::OtaExecResult::DOWNLOAD_FAILED);
    EXPECT_EQ(result.error_code, farm::OtaErrorCode::DOWNLOAD_SESSION_FAIL);
}

TEST_F(OtaControllerTest, ExecuteDownloadSuccess)
{
    EXPECT_CALL(mock_ota_manager_, start_ota()).WillOnce(Return(true));
    EXPECT_CALL(mock_ota_manager_, get_status()).WillOnce(Return(OtaStatus::READY_TO_RESTART));

    OtaActionResult result = sut_->execute_download();
    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.exec_result, farm::OtaExecResult::CONFIRMED_SUCCESS);
    EXPECT_EQ(result.error_code, farm::OtaErrorCode::NONE);
}

TEST_F(OtaControllerTest, ExecuteDownloadFailedStatus)
{
    EXPECT_CALL(mock_ota_manager_, start_ota()).WillOnce(Return(true));
    EXPECT_CALL(mock_ota_manager_, get_status()).WillOnce(Return(OtaStatus::FAILED));
    EXPECT_CALL(mock_ota_manager_, get_last_error()).WillOnce(Return(OtaFailReason::HASH_MISMATCH));
    EXPECT_CALL(mock_ota_manager_, cancel_ota()).Times(1);

    OtaActionResult result = sut_->execute_download();
    EXPECT_FALSE(result.success);
    EXPECT_EQ(result.exec_result, farm::OtaExecResult::DOWNLOAD_FAILED);
    EXPECT_EQ(result.error_code, farm::OtaErrorCode::IMAGE_HASH_MISMATCH);
}

TEST_F(OtaControllerTest, ExecuteDownloadTimeout)
{
    EXPECT_CALL(mock_ota_manager_, start_ota()).WillOnce(Return(true));
    EXPECT_CALL(mock_ota_manager_, get_status()).WillRepeatedly(Return(OtaStatus::DOWNLOADING));
    EXPECT_CALL(mock_ota_manager_, cancel_ota()).Times(1);

    OtaActionResult result = sut_->execute_download(1000); // 1s timeout
    EXPECT_FALSE(result.success);
    EXPECT_EQ(result.exec_result, farm::OtaExecResult::DOWNLOAD_FAILED);
    EXPECT_EQ(result.error_code, farm::OtaErrorCode::WATCHDOG_TIMEOUT);
}
