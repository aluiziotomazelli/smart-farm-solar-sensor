// main/include/interfaces/i_led_controller.hpp
#pragma once

#include "esp_err.h"
#include "led_controller_types.hpp"

/**
 * @interface ILedController
 * @brief Abstract interface for visual LED status feedback.
 */
class ILedController
{
public:
    virtual ~ILedController() = default;

    /**
     * @brief Configures GPIO pin direction and ensures LED is initialised in OFF state.
     *
     * @return ESP_OK on success, or ESP-IDF error code.
     */
    virtual esp_err_t init() = 0;

    /**
     * @brief Spawns the background FreeRTOS task handling LED blink animations.
     *
     * @return ESP_OK on success, or ESP_FAIL if task creation fails.
     */
    virtual esp_err_t start() = 0;

    /**
     * @brief Stops the blink task and forces the LED to OFF state.
     */
    virtual void stop() = 0;

    /**
     * @brief Sets or updates the active blink pattern.
     *
     * @param pattern Desired BlinkPattern (one-shot or looping).
     */
    virtual void set_pattern(BlinkPattern pattern) = 0;

    /**
     * @brief Triggers a single quick pulse without overriding persistent looping patterns.
     *
     * @param duration_ms Duration in milliseconds for the LED to remain on.
     */
    virtual void pulse(uint16_t duration_ms = 30) = 0;

    /**
     * @brief Returns currently active blink pattern.
     */
    virtual BlinkPattern get_current_pattern() const = 0;
};
