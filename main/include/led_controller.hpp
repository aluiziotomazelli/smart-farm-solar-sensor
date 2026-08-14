// main/include/led_controller.hpp
#pragma once

#include <atomic>
#include <cstdint>

#include "interfaces/i_led_controller.hpp"
#include "interfaces/i_hal_gpio.hpp"
#include "interfaces/i_hal_freertos.hpp"

/**
 * @class LedController
 * @brief Thread-safe visual LED status feedback controller with pattern execution.
 */
class LedController : public ILedController
{
public:
    /**
     * @brief Constructs LedController with required HAL dependencies and configuration.
     *
     * @param hal_gpio GPIO HAL interface
     * @param hal_rtos FreeRTOS HAL interface
     * @param config Configuration parameters (GPIO pin, stack size, priority, active level)
     */
    LedController(
        idf_hals::IGpioHAL& hal_gpio,
        idf_hals::IHalFreertos& hal_rtos,
        const LedConfig& config = LedConfig{});

    ~LedController() override;

    /** @copydoc ILedController::init */
    esp_err_t init() override;

    /** @copydoc ILedController::start */
    esp_err_t start() override;

    /** @copydoc ILedController::stop */
    void stop() override;

    /** @copydoc ILedController::set_pattern */
    void set_pattern(BlinkPattern pattern) override;

    /** @copydoc ILedController::pulse */
    void pulse(uint16_t duration_ms = 30) override;

    /** @copydoc ILedController::get_current_pattern */
    BlinkPattern get_current_pattern() const override { return current_pattern_.load(); }

    /**
     * @brief Checks whether the background blink task is running.
     */
    bool is_running() const { return is_running_.load(); }

    /**
     * @brief Executes one pattern iteration or pulse (accessible for deterministic unit testing).
     */
    void process_cycle();

private:
    idf_hals::IGpioHAL& hal_gpio_;
    idf_hals::IHalFreertos& hal_rtos_;
    LedConfig config_;

    TaskHandle_t task_handle_{nullptr};
    std::atomic<bool> is_running_{false};
    std::atomic<BlinkPattern> current_pattern_{BlinkPattern::OFF};
    std::atomic<uint16_t> pulse_duration_ms_{0};

    static void task_entry(void* arg);
    void task_loop();
    void set_led_state(bool on);
    void execute_pattern(BlinkPattern pattern);
    void blink_pulse(uint16_t on_ms, uint16_t off_ms, uint8_t count);
};
