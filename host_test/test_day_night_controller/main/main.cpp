#include <gtest/gtest.h>

extern "C" void app_main(void)
{
    ::testing::InitGoogleTest();
    int ret = RUN_ALL_TESTS();
    exit(ret);
}
