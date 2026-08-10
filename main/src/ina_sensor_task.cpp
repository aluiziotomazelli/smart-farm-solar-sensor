// main/src/ina_sensor_task.cpp
#include "ina_sensor_task.hpp"

#include <algorithm>
#include <cmath>

#define LOG_LOCAL_LEVEL ESP_LOG_INFO
#include "esp_log.h"
#include "farm_protocol_types.hpp"

static const char* TAG = "InaSensorTask";

namespace ina {

InaSensorTask::InaSensorTask(
    ina226::IIna226Driver& driver,
    espnow::IEspNowManager& espnow,
    idf_hals::ITimerHAL& timer,
    idf_hals::IHalFreertos& rtos,
    QueueHandle_t sample_queue)
    : driver_(driver)
    , espnow_(espnow)
    , timer_(timer)
    , rtos_(rtos)
    , sample_queue_(sample_queue)
{
}

InaSensorTask::~InaSensorTask()
{
    stop();
}

esp_err_t InaSensorTask::init(const InaSensorConfig& config, i2c_master_bus_handle_t i2c_bus)
{
    config_ = config;

    esp_err_t err = driver_.init(i2c_bus);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize INA226 driver: %s", esp_err_to_name(err));
        return err;
    }

    last_report_timestamp_us_ = timer_.get_time_us();
    ESP_LOGI(TAG, "InaSensorTask initialized successfully");
    return ESP_OK;
}

esp_err_t InaSensorTask::start()
{
    running_.store(true);

    task_done_semaphore_ = rtos_.semaphore_create_binary();
    if (task_done_semaphore_ == nullptr) {
        return ESP_ERR_NO_MEM;
    }

    BaseType_t task_ret = rtos_.task_create(
        task_entry_point, "InaSensorTask", config_.task_stack_size, this, config_.task_priority, &task_handle_);

    if (task_ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create INA Sensor Task");
        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}

void InaSensorTask::stop()
{
    running_.store(false);

    if (task_handle_ != nullptr) {
        rtos_.task_notify_give(task_handle_);
        // Wait for task to exit
        uint8_t delay_ms = 10;
        for (int timeout = 1000; timeout > 0; timeout -= delay_ms) {
            if (rtos_.semaphore_take(task_done_semaphore_, delay_ms) == pdPASS)
                break;
        }

        // Forcing deleting task
        if (task_handle_ != nullptr) {
            ESP_LOGW(TAG, "Forcing deletion of tx manager task");
            rtos_.task_delete(task_handle_);
            task_handle_ = nullptr;
        }
    }
    if (task_done_semaphore_ != nullptr) {
        rtos_.semaphore_delete(task_done_semaphore_);
        task_done_semaphore_ = nullptr;
    }
}

esp_err_t InaSensorTask::set_operating_mode(SolarNodeState mode)
{
    mode_ = mode;
    esp_err_t err = ESP_OK;

    switch (mode) {
    case SolarNodeState::DAY_ACTIVE:
    {
        const auto& cfg = config_.day_config;
        err = driver_.configure_alert(static_cast<uint16_t>(cfg.alert_flag), cfg.alert_limit);
        sampling_enabled_.store(true);
        reporting_enabled_.store(true);
        ESP_LOGI(
            TAG,
            "InaSensorTask set to DAY_ACTIVE (AlertFlag: 0x%04X, limit: %u)",
            static_cast<uint16_t>(cfg.alert_flag),
            cfg.alert_limit);
        break;
    }

    case SolarNodeState::NIGHT_SLEEP:
    {
        const auto& cfg = config_.night_config;
        err = driver_.configure_alert(static_cast<uint16_t>(cfg.alert_flag), cfg.alert_limit);
        sampling_enabled_.store(false);
        reporting_enabled_.store(false);
        ESP_LOGI(
            TAG,
            "InaSensorTask set to NIGHT_SLEEP (AlertFlag: 0x%04X, limit: %u)",
            static_cast<uint16_t>(cfg.alert_flag),
            cfg.alert_limit);
        break;
    }

    case SolarNodeState::OTA_UPDATE:
        sampling_enabled_.store(false);
        reporting_enabled_.store(false);
        ESP_LOGI(TAG, "InaSensorTask set to OTA_UPDATE (Sampling paused)");
        break;
    }

    return err;
}

uint32_t InaSensorTask::get_expected_sample_period_ms() const
{
    const auto& mode_cfg = (mode_ == SolarNodeState::DAY_ACTIVE) ? config_.day_config : config_.night_config;

    uint32_t vsh_us = ina226::conversion_time_to_us(mode_cfg.vsh_ct);
    uint32_t vbus_us = ina226::conversion_time_to_us(mode_cfg.vbus_ct);
    uint32_t avg_count = ina226::averaging_mode_to_count(mode_cfg.avg_mode);

    return ((vsh_us + vbus_us) * avg_count) / 1000;
}

uint32_t InaSensorTask::get_watchdog_timeout_ms() const
{
    uint32_t period_ms = get_expected_sample_period_ms();
    return std::max<uint32_t>(500, period_ms * 3);
}

void InaSensorTask::process_cycle()
{
    if (!sampling_enabled_.load()) {
        return;
    }

    float raw_ma = 0.0f;
    uint16_t bus_mv = 0;
    esp_err_t read_err = read_raw_sample(raw_ma, bus_mv);

    InaSample sample{};
    sample.timestamp_us = timer_.get_time_us();
    sample.status = read_err;

    if (read_err != ESP_OK) {
        ESP_LOGW(TAG, "INA226 read failed: %s", esp_err_to_name(read_err));
        enqueue_sample(sample);
        return;
    }

    apply_ema_filter(raw_ma, sample);
    sample.bus_voltage_mv = bus_mv;

    if (reporting_enabled_.load()) {
        check_and_dispatch_telemetry(sample);
    }

    enqueue_sample(sample);
}

esp_err_t InaSensorTask::read_raw_sample(float& out_ma, uint16_t& out_bus_mv)
{
    esp_err_t err = driver_.read_current_ma(out_ma);
    if (err == ESP_OK) {
        driver_.read_bus_voltage_mv(out_bus_mv);
    }
    return err;
}

void InaSensorTask::apply_ema_filter(float raw_ma, InaSample& sample)
{
    if (ema_current_ma_ == 0.0f && raw_ma > 0.0f) {
        ema_current_ma_ = raw_ma;
    }
    else {
        ema_current_ma_ = (EMA_ALPHA * raw_ma) + ((1.0f - EMA_ALPHA) * ema_current_ma_);
    }

    sample.isc_current_ma = static_cast<uint16_t>(ema_current_ma_ > 0.0f ? ema_current_ma_ : 0.0f);
}

void InaSensorTask::check_and_dispatch_telemetry(InaSample& sample)
{
    int64_t now_us = sample.timestamp_us;
    float delta_ma = std::abs(ema_current_ma_ - last_reported_current_ma_);
    float rel_delta = last_reported_current_ma_ > 0 ? (delta_ma / last_reported_current_ma_) : 0.0f;

    bool delta_triggered = (delta_ma >= config_.delta_threshold_ma) || (rel_delta >= config_.delta_threshold_percent);
    bool heartbeat_triggered = (now_us - last_report_timestamp_us_) >= (config_.heartbeat_interval_ms * 1000LL);

    sample.delta_detected = delta_triggered;

    if (delta_triggered || heartbeat_triggered) {
        if (send_telemetry_report(sample.isc_current_ma) == ESP_OK) {
            last_reported_current_ma_ = ema_current_ma_;
            last_report_timestamp_us_ = now_us;
        }
    }
}

void InaSensorTask::enqueue_sample(const InaSample& sample)
{
    if (sample_queue_ != nullptr) {
        rtos_.queue_send(sample_queue_, &sample, 0);
    }
}

esp_err_t InaSensorTask::send_telemetry_report(uint16_t current_ma)
{
    farm::SolarSensorReport report{};
    report.power_profile = farm::PowerProfile::ALWAYS_ON;
    report.isc_current_ma = current_ma;

    float ratio = (static_cast<float>(current_ma) / 700.0f);
    report.irradiance_wm2 = static_cast<uint16_t>(ratio * 1000.0f);
    report.estimated_power_w = static_cast<uint16_t>(ratio * 2640.0f);
    report.status = farm::SensorStatus::OK;
    report.unix_time = timer_.get_time_us() / 1000;

    return espnow_.send_data(
        espnow::ReservedIds::HUB,
        static_cast<uint8_t>(farm::PayloadType::SOLAR_SENSOR_REPORT),
        &report,
        sizeof(report),
        /*require_ack=*/false);
}

void InaSensorTask::task_entry_point(void* arg)
{
    static_cast<InaSensorTask*>(arg)->ina_sensor_task();
}

void InaSensorTask::ina_sensor_task()
{
    uint32_t timeout_ms = get_watchdog_timeout_ms();
    while (running_) {
        if (rtos_.task_notify_take(pdTRUE, timeout_ms)) {
            process_cycle();
        }
    }

    ESP_LOGI(TAG, "INA sensor task stopped");

    task_handle_ = nullptr;
    rtos_.semaphore_give(task_done_semaphore_);
    rtos_.task_delete(nullptr);
}

} // namespace ina
