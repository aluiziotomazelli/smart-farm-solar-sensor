// main/src/led_controller.cpp
#include "led_controller.hpp"

#define LOG_LOCAL_LEVEL ESP_LOG_INFO
#include "esp_log.h"

static const char* TAG = "LedController";

LedController::LedController(
    idf_hals::IGpioHAL& hal_gpio,
    idf_hals::IHalFreertos& hal_rtos,
    const LedConfig& config)
    : hal_gpio_(hal_gpio)
    , hal_rtos_(hal_rtos)
    , config_(config)
{
}

LedController::~LedController()
{
    stop();
}

esp_err_t LedController::init()
{
    if (config_.gpio_num == GPIO_NUM_NC) {
        ESP_LOGE(TAG, "Invalid GPIO pin configured for status LED");
        return ESP_ERR_INVALID_ARG;
    }

    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << config_.gpio_num),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    esp_err_t err = hal_gpio_.config(&io_conf);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure GPIO %d for status LED: %s", config_.gpio_num, esp_err_to_name(err));
        return err;
    }

    set_led_state(false);
    ESP_LOGI(TAG, "LedController initialized on GPIO %d (active_%s)", config_.gpio_num, config_.active_level ? "high" : "low");
    return ESP_OK;
}

esp_err_t LedController::start()
{
    if (is_running_.load()) {
        ESP_LOGW(TAG, "LedController already running");
        return ESP_OK;
    }

    is_running_.store(true);

    BaseType_t ret = hal_rtos_.task_create(
        task_entry,
        "LedTask",
        config_.task_stack_size,
        this,
        config_.task_priority,
        &task_handle_);

    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create LedTask");
        is_running_.store(false);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "LedTask started successfully");
    return ESP_OK;
}

void LedController::stop()
{
    if (!is_running_.load()) {
        return;
    }

    is_running_.store(false);
    current_pattern_.store(BlinkPattern::OFF);
    set_led_state(false);

    if (task_handle_ != nullptr) {
        hal_rtos_.task_delete(task_handle_);
        task_handle_ = nullptr;
    }

    ESP_LOGI(TAG, "LedController stopped");
}

void LedController::set_pattern(BlinkPattern pattern)
{
    current_pattern_.store(pattern);
}

void LedController::pulse(uint16_t duration_ms)
{
    pulse_duration_ms_.store(duration_ms > 0 ? duration_ms : 30);
}

void LedController::set_led_state(bool on)
{
    uint32_t level = on ? config_.active_level : (config_.active_level ? 0 : 1);
    hal_gpio_.set_level(config_.gpio_num, level);
}

void LedController::blink_pulse(uint16_t on_ms, uint16_t off_ms, uint8_t count)
{
    for (uint8_t i = 0; i < count; ++i) {
        if (!is_running_.load() && task_handle_ != nullptr) {
            break;
        }
        set_led_state(true);
        hal_rtos_.task_delay(pdMS_TO_TICKS(on_ms));
        set_led_state(false);
        if (off_ms > 0 && (i + 1 < count || off_ms >= 50)) {
            hal_rtos_.task_delay(pdMS_TO_TICKS(off_ms));
        }
    }
}

void LedController::execute_pattern(BlinkPattern pattern)
{
    switch (pattern) {
    case BlinkPattern::TX_PULSE:
        blink_pulse(30, 0, 1);
        current_pattern_.store(BlinkPattern::OFF);
        break;

    case BlinkPattern::BOOT_SUCCESS:
        blink_pulse(100, 100, 2);
        current_pattern_.store(BlinkPattern::OFF);
        break;

    case BlinkPattern::ENTER_SLEEP:
        blink_pulse(300, 0, 1);
        current_pattern_.store(BlinkPattern::OFF);
        break;

    case BlinkPattern::ERROR_BURST:
        blink_pulse(50, 50, 5);
        current_pattern_.store(BlinkPattern::OFF);
        break;

    case BlinkPattern::PAIRING_MODE:
        blink_pulse(200, 200, 1);
        break;

    case BlinkPattern::OTA_UPDATING:
        blink_pulse(100, 100, 1);
        break;

    case BlinkPattern::IDLE_BEACON:
        blink_pulse(40, 40, 2);
        if (current_pattern_.load() == BlinkPattern::IDLE_BEACON) {
            hal_rtos_.task_delay(pdMS_TO_TICKS(4840));
        }
        break;

    case BlinkPattern::OFF:
    default:
        set_led_state(false);
        hal_rtos_.task_delay(pdMS_TO_TICKS(100));
        break;
    }
}

void LedController::process_cycle()
{
    uint16_t pulse_dur = pulse_duration_ms_.exchange(0);
    if (pulse_dur > 0) {
        set_led_state(true);
        hal_rtos_.task_delay(pdMS_TO_TICKS(pulse_dur));
        set_led_state(false);
        return;
    }

    BlinkPattern pattern = current_pattern_.load();
    execute_pattern(pattern);
}

void LedController::task_entry(void* arg)
{
    auto* self = static_cast<LedController*>(arg);
    self->task_loop();
}

void LedController::task_loop()
{
    while (is_running_.load()) {
        process_cycle();
    }
}
