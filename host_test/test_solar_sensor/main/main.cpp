#include <cstdlib>
#include <chrono>
#include "gtest/gtest.h"

extern "C" int64_t esp_timer_get_time(void)
{
    static auto start_time = std::chrono::steady_clock::now();
    auto now = std::chrono::steady_clock::now();
    auto us = std::chrono::duration_cast<std::chrono::microseconds>(now - start_time).count();
    return static_cast<int64_t>(us);
}

extern "C" void app_main(void)
{
    testing::InitGoogleTest();
    int result = RUN_ALL_TESTS();
    exit(result);
}
