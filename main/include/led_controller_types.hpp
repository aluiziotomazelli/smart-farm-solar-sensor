// main/include/led_controller_types.hpp
#pragma once

#include <cstdint>
#include "driver/gpio.h"

/**
 * @enum BlinkPattern
 * @brief Visual feedback patterns for the status LED.
 */
enum class BlinkPattern : uint8_t
{
    OFF = 0,      ///< LED off / cancels any active pattern
    TX_PULSE,     ///< One-shot: quick 30ms blip when transmitting a packet
    BOOT_SUCCESS, ///< One-shot: 2 pulses (100ms on / 100ms off) confirming healthy boot
    ENTER_SLEEP,  ///< One-shot: 1 long pulse (300ms) before deep sleep shutdown
    ERROR_BURST,  ///< One-shot: 5 rapid pulses (50ms on / 50ms off) signaling critical failure
    PAIRING_MODE, ///< Continuous: 200ms on / 200ms off while searching/pairing with Hub
    OTA_UPDATING, ///< Continuous: 100ms on / 100ms off while downloading/flashing OTA firmware
    IDLE_BEACON   ///< Continuous: double flash (40ms on/off) every 5 seconds when in IDLE
};

/**
 * @struct LedConfig
 * @brief Configuration parameters for LedController.
 */
struct LedConfig
{
    gpio_num_t gpio_num{GPIO_NUM_4}; ///< GPIO pin connected to status LED
    uint32_t task_stack_size{2048};  ///< FreeRTOS task stack size in bytes
    uint8_t task_priority{1};        ///< FreeRTOS task priority
    uint8_t active_level{1};         ///< 1 for active-high, 0 for active-low
};
