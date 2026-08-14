// main/src/slow_sensors_task.cpp
#include "slow_sensors_task.hpp"

#define LOG_LOCAL_LEVEL ESP_LOG_INFO
#include "esp_log.h"

static const char* TAG = "SlowSensorsTask";

SlowSensorsTask::SlowSensorsTask(
    battery_monitor::IBatteryMonitor& bat_monitor,
    ds18b20::IDs18b20Driver& ds18b20,
    idf_hals::IHalFreertos& rtos,
    TelemetrySnapshot& snapshot,
    const SlowSensorsConfig& config)
    : bat_monitor_(bat_monitor)
    , ds18b20_(ds18b20)
    , rtos_(rtos)
    , snapshot_(snapshot)
    , config_(config)
{
}

SlowSensorsTask::~SlowSensorsTask()
{
    stop();
}

esp_err_t SlowSensorsTask::init()
{
    ESP_LOGI(TAG, "Initializing slow sensors drivers...");

    esp_err_t err = bat_monitor_.init();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Battery monitor initialization returned: %s", esp_err_to_name(err));
    }

    err = ds18b20_.init();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "DS18B20 driver initialization returned: %s", esp_err_to_name(err));
    }

    return ESP_OK;
}

esp_err_t SlowSensorsTask::start()
{
    if (running_.load()) {
        ESP_LOGW(TAG, "SlowSensorsTask is already running");
        return ESP_OK;
    }

    running_.store(true);

    task_done_semaphore_ = rtos_.semaphore_create_binary();
    if (task_done_semaphore_ == nullptr) {
        ESP_LOGE(TAG, "Failed to create task synchronization semaphore");
        running_.store(false);
        return ESP_ERR_NO_MEM;
    }

    BaseType_t ret = rtos_.task_create(
        task_entry_point,
        "SlowSensorsTask",
        config_.task_stack_size,
        this,
        config_.task_priority,
        &task_handle_);

    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create SlowSensorsTask");
        rtos_.semaphore_delete(task_done_semaphore_);
        task_done_semaphore_ = nullptr;
        running_.store(false);
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "SlowSensorsTask started successfully (interval=%lu ms)", static_cast<unsigned long>(config_.sample_interval_ms));
    return ESP_OK;
}

void SlowSensorsTask::stop()
{
    if (!running_.load()) {
        return;
    }

    running_.store(false);

    if (task_handle_ != nullptr) {
        rtos_.task_notify_give(task_handle_);

        if (task_done_semaphore_ != nullptr) {
            uint8_t delay_ms = 20;
            for (int timeout = 1000; timeout > 0; timeout -= delay_ms) {
                if (rtos_.semaphore_take(task_done_semaphore_, delay_ms) == pdPASS) {
                    break;
                }
            }
        }

        if (task_handle_ != nullptr) {
            rtos_.task_delete(task_handle_);
            task_handle_ = nullptr;
        }
    }

    if (task_done_semaphore_ != nullptr) {
        rtos_.semaphore_delete(task_done_semaphore_);
        task_done_semaphore_ = nullptr;
    }

    ESP_LOGI(TAG, "SlowSensorsTask stopped");
}

void SlowSensorsTask::process_cycle()
{
    // 1. Read Battery Monitor (~16ms)
    battery_monitor::BatteryReading bat_reading{};
    esp_err_t err = bat_monitor_.read(bat_reading);
    if (err == ESP_OK) {
        consecutive_battery_errors_ = 0;
        farm::BatteryState bat_state = static_cast<farm::BatteryState>(bat_reading.state);
        snapshot_.update_battery(bat_reading.voltage_mv, bat_reading.percent, bat_state);
        ESP_LOGD(TAG, "Battery: %u mV (%u%%, state=%d)", bat_reading.voltage_mv, bat_reading.percent, static_cast<int>(bat_state));
    } else {
        consecutive_battery_errors_++;
        if (consecutive_battery_errors_ >= config_.max_consecutive_errors) {
            ESP_LOGE(TAG, "Repeated failure reading battery (%u consecutive errors): %s", consecutive_battery_errors_, esp_err_to_name(err));
        } else {
            ESP_LOGW(TAG, "Failed to read battery: %s", esp_err_to_name(err));
        }
    }

    // 2. Read DS18B20 Temperature (~800ms conversion delay blocking this task only)
    float temp_c = -127.0f;
    err = ds18b20_.read_temperature(&temp_c);
    if (err == ESP_OK) {
        consecutive_temp_errors_ = 0;
        snapshot_.update_temperature(temp_c);
        ESP_LOGI(TAG, "Temperature: %.2f °C | Battery: %u mV (%u%%)", temp_c, bat_reading.voltage_mv, bat_reading.percent);
    } else {
        consecutive_temp_errors_++;
        if (consecutive_temp_errors_ >= config_.max_consecutive_errors) {
            ESP_LOGE(TAG, "Repeated failure reading DS18B20 (%u consecutive errors): %s", consecutive_temp_errors_, esp_err_to_name(err));
        } else {
            ESP_LOGW(TAG, "Failed to read DS18B20: %s", esp_err_to_name(err));
        }
    }
}

void SlowSensorsTask::task_entry_point(void* arg)
{
    auto* self = static_cast<SlowSensorsTask*>(arg);
    self->task_loop();
}

void SlowSensorsTask::task_loop()
{
    ESP_LOGI(TAG, "SlowSensorsTask loop entered");

    while (running_.load()) {
        process_cycle();

        // Sleep for the configured interval OR wake immediately if stop() notifies us
        TickType_t ticks_to_wait = pdMS_TO_TICKS(config_.sample_interval_ms);
        rtos_.task_notify_take(pdTRUE, ticks_to_wait);
    }

    if (task_done_semaphore_ != nullptr) {
        rtos_.semaphore_give(task_done_semaphore_);
    }

    task_handle_ = nullptr;
    rtos_.task_delete(nullptr);
}
