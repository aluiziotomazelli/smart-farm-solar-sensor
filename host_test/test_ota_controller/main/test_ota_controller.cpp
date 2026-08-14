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

    void SetUp() override
    {
        sut_ = std::make_unique<OtaController>(mock_ota_manager_, mock_freertos_);
    }
};

TEST_F(OtaControllerTest, VerifyFirmwareOnBootNoPending)
{
    EXPECT_CALL(mock_ota_manager_, check_pending_verify()).WillOnce(Return(false));

    OtaVerifyResult result = sut_->verify_firmware_on_boot(true);
    EXPECT_FALSE(result.pending_verify);
}

TEST_F(OtaControllerTest, VerifyFirmwareOnBootHealthySuccess)
{
    EXPECT_CALL(mock_ota_manager_, check_pending_verify()).WillOnce(Return(true));
    EXPECT_CALL(mock_ota_manager_, confirm_app_valid()).WillOnce(Return(true));
    
    OtaVersion ver{1, 2, 3};
    EXPECT_CALL(mock_ota_manager_, get_running_version()).WillOnce(Return(ver));

    OtaVerifyResult result = sut_->verify_firmware_on_boot(true);
    EXPECT_TRUE(result.pending_verify);
    EXPECT_TRUE(result.success);
    ASSERT_TRUE(result.version.has_value());
    EXPECT_EQ(result.version->major, 1);
    EXPECT_EQ(result.version->minor, 2);
    EXPECT_EQ(result.version->patch, 3);
    EXPECT_EQ(result.exec_result, farm::OtaExecResult::CONFIRMED_SUCCESS);
}

TEST_F(OtaControllerTest, VerifyFirmwareOnBootUnhealthyTriggersRollback)
{
    EXPECT_CALL(mock_ota_manager_, check_pending_verify()).WillOnce(Return(true));

    OtaVerifyResult result = sut_->verify_firmware_on_boot(false);
    EXPECT_TRUE(result.pending_verify);
    EXPECT_FALSE(result.success);
    EXPECT_EQ(result.exec_result, farm::OtaExecResult::ROLLBACK_TRIGGERED);
    EXPECT_EQ(result.error_code, farm::OtaErrorCode::HEALTH_CHECK_FAILED);
}

TEST_F(OtaControllerTest, RollbackAndRebootDelegatesToManager)
{
    EXPECT_CALL(mock_ota_manager_, rollback_and_reboot()).Times(1);
    sut_->rollback_and_reboot();
}

TEST_F(OtaControllerTest, ExecuteDownloadStartOtaFail)
{
    EXPECT_CALL(mock_ota_manager_, start_ota()).WillOnce(Return(false));

    OtaDownloadResult result = sut_->execute_download();
    EXPECT_FALSE(result.success);
    EXPECT_EQ(result.error_code, farm::OtaErrorCode::DOWNLOAD_SESSION_FAIL);
}

TEST_F(OtaControllerTest, ExecuteDownloadSuccess)
{
    EXPECT_CALL(mock_ota_manager_, start_ota()).WillOnce(Return(true));
    EXPECT_CALL(mock_ota_manager_, get_status()).WillOnce(Return(OtaStatus::READY_TO_RESTART));

    OtaDownloadResult result = sut_->execute_download();
    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.error_code, farm::OtaErrorCode::NONE);
}

TEST_F(OtaControllerTest, ExecuteDownloadFailedStatus)
{
    EXPECT_CALL(mock_ota_manager_, start_ota()).WillOnce(Return(true));
    EXPECT_CALL(mock_ota_manager_, get_status()).WillOnce(Return(OtaStatus::FAILED));
    EXPECT_CALL(mock_ota_manager_, get_last_error()).WillOnce(Return(OtaFailReason::HASH_MISMATCH));
    EXPECT_CALL(mock_ota_manager_, cancel_ota()).Times(1);

    OtaDownloadResult result = sut_->execute_download();
    EXPECT_FALSE(result.success);
    EXPECT_EQ(result.error_code, farm::OtaErrorCode::IMAGE_HASH_MISMATCH);
}

TEST_F(OtaControllerTest, ExecuteDownloadTimeout)
{
    EXPECT_CALL(mock_ota_manager_, start_ota()).WillOnce(Return(true));
    EXPECT_CALL(mock_ota_manager_, get_status()).WillRepeatedly(Return(OtaStatus::DOWNLOADING));
    EXPECT_CALL(mock_ota_manager_, cancel_ota()).Times(1);

    OtaDownloadResult result = sut_->execute_download(1000); // 1s timeout
    EXPECT_FALSE(result.success);
    EXPECT_EQ(result.error_code, farm::OtaErrorCode::WATCHDOG_TIMEOUT);
}
