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
    power_control::IPowerControl& power_control,
    espnow::IEspNowManager& espnow,
    idf_hals::ITimerHAL& timer,
    idf_hals::IHalFreertos& rtos,
    QueueHandle_t sample_queue)
    : driver_(driver)
    , power_control_(power_control)
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

esp_err_t InaSensorTask::init(const InaSensorConfig& config)
{
    config_ = config;

    esp_err_t err = power_control_.init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize INA VCC power control: %s", esp_err_to_name(err));
        return err;
    }

    err = power_control_.turn_on();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to turn on INA VCC power: %s", esp_err_to_name(err));
        return err;
    }

    err = driver_.init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize INA226 driver: %s", esp_err_to_name(err));
        return err;
    }

    consecutive_errors_ = 0;
    last_report_timestamp_us_ = timer_.get_time_us();
    ESP_LOGI(TAG, "InaSensorTask initialized successfully");
    return ESP_OK;
}

esp_err_t InaSensorTask::start()
{
    running_.store(true);
    return ESP_OK;
}

void InaSensorTask::stop()
{
    running_.store(false);
}

esp_err_t InaSensorTask::set_operating_mode(SolarNodeState mode)
{
    mode_ = mode;
    esp_err_t err = ESP_OK;

    switch (mode) {
    case SolarNodeState::DAY_ACTIVE: {
        const auto& cfg = config_.day_config;
        err = driver_.configure_alert(static_cast<uint16_t>(cfg.alert_flag), cfg.alert_limit);
        sampling_enabled_.store(true);
        reporting_enabled_.store(true);
        ESP_LOGI(TAG, "InaSensorTask set to DAY_ACTIVE (AlertFlag: 0x%04X, limit: %u)",
                 static_cast<uint16_t>(cfg.alert_flag), cfg.alert_limit);
        break;
    }

    case SolarNodeState::NIGHT_SLEEP: {
        const auto& cfg = config_.night_config;
        err = driver_.configure_alert(static_cast<uint16_t>(cfg.alert_flag), cfg.alert_limit);
        sampling_enabled_.store(false);
        reporting_enabled_.store(false);
        ESP_LOGI(TAG, "InaSensorTask set to NIGHT_SLEEP (AlertFlag: 0x%04X, limit: %u)",
                 static_cast<uint16_t>(cfg.alert_flag), cfg.alert_limit);
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

esp_err_t InaSensorTask::hard_reset_ina_power()
{
    ESP_LOGW(TAG, "Performing INA226 hardware power cycle (50ms off)...");
    esp_err_t err = power_control_.turn_off();
    if (err != ESP_OK) {
        return err;
    }

    rtos_.task_delay(pdMS_TO_TICKS(50));

    err = power_control_.turn_on();
    if (err != ESP_OK) {
        return err;
    }

    rtos_.task_delay(pdMS_TO_TICKS(10));
    err = driver_.init();
    if (err == ESP_OK) {
        consecutive_errors_ = 0;
        ESP_LOGI(TAG, "INA226 re-initialized successfully after power cycle");
    }
    return err;
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
        handle_read_error(sample, read_err);
        return;
    }

    consecutive_errors_ = 0;
    apply_ema_filter(raw_ma, sample);
    sample.bus_voltage_mv = bus_mv;

    check_and_dispatch_telemetry(sample);
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

void InaSensorTask::handle_read_error(InaSample& sample, esp_err_t read_err)
{
    consecutive_errors_++;
    ESP_LOGW(
        TAG,
        "INA226 read failed (attempt %u/%u): %s",
        consecutive_errors_,
        MAX_CONSECUTIVE_ERRORS_BEFORE_HARD_RESET,
        esp_err_to_name(read_err));

    if (consecutive_errors_ >= MAX_CONSECUTIVE_ERRORS_BEFORE_HARD_RESET) {
        hard_reset_ina_power();
    }

    enqueue_sample(sample);
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

    if ((delta_triggered || heartbeat_triggered) && reporting_enabled_.load()) {
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

} // namespace ina
