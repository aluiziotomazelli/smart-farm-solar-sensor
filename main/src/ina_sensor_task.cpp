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
    time_manager::ITimeManager& time_manager,
    TelemetrySnapshot& snapshot,
    QueueHandle_t sample_queue)
    : driver_(driver)
    , espnow_(espnow)
    , timer_(timer)
    , rtos_(rtos)
    , time_manager_(time_manager)
    , snapshot_(snapshot)
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

    // Daytime regime: arm ALERT_ON_CONVERSION_READY (CNVR, bit 10) — the only
    // conversion-complete flag that asserts the ALERT pin, so the GPIO ISR
    // wakes the task on every completed conversion. The alert limit is unused
    // for CNVR. prepare_for_sleep() re-arms the pin for the dawn wakeup.
    err = driver_.configure_alert(static_cast<uint16_t>(ina226::AlertFlag::ALERT_ON_CONVERSION_READY), 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure conversion-ready alert: %s", esp_err_to_name(err));
        return err;
    }

    last_report_timestamp_us_ = timer_.get_time_us();
    float r_shunt = driver_.get_config().r_shunt_ohms;
    uv_per_ma_ = (r_shunt > 0.0f) ? (r_shunt * 1000.0f) : 100.0f;
    ESP_LOGI(TAG, "InaSensorTask initialized successfully (R_shunt=%.3f Ohm, uV_per_mA=%.1f)", r_shunt, uv_per_ma_);
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
    sampling_enabled_ = false;
    reporting_enabled_ = false;

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

esp_err_t InaSensorTask::prepare_for_sleep()
{
    // Stop producing samples and reports: the app is about to enter deep sleep.
    sampling_enabled_.store(false);
    reporting_enabled_.store(false);

    // Slow down the conversions to cut power consumption for the night regime.
    esp_err_t err = apply_night_config(config_.night_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to apply night conversion config: %s", esp_err_to_name(err));
        return err;
    }

    // Arm the dawn wakeup alert: SHUNT_OVER_VOLTAGE asserts the ALERT pin when
    // the panel current rises above DEFAULT_DAWN_WAKEUP_ALERT_LIMIT, waking the
    // MCU from deep sleep via the GPIO.
    err = driver_.configure_alert(
        static_cast<uint16_t>(ina226::AlertFlag::SHUNT_OVER_VOLTAGE), DEFAULT_DAWN_WAKEUP_ALERT_LIMIT);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure dawn wakeup alert: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(
        TAG,
        "InaSensorTask prepared for sleep (avg=%u, vbus=%luus, vsh=%luus, alert=SHUNT_OVER_VOLTAGE, limit=%u)",
        static_cast<unsigned>(ina226::averaging_mode_to_count(config_.night_config.avg_mode)),
        static_cast<unsigned long>(ina226::conversion_time_to_us(config_.night_config.vbus_ct)),
        static_cast<unsigned long>(ina226::conversion_time_to_us(config_.night_config.vsh_ct)),
        DEFAULT_DAWN_WAKEUP_ALERT_LIMIT);
    return ESP_OK;
}

uint32_t InaSensorTask::get_expected_sample_period_ms() const
{
    // The period depends on the conversion settings currently active on the
    // hardware, which the driver owns. During normal (day) operation this is
    // the day config applied at init; after prepare_for_sleep() it reflects
    // the slower night regime.
    const ina226::Ina226Config& hw_cfg = driver_.get_config();

    uint32_t vsh_us = ina226::conversion_time_to_us(hw_cfg.vsh_ct);
    uint32_t vbus_us = ina226::conversion_time_to_us(hw_cfg.vbus_ct);
    uint32_t avg_count = ina226::averaging_mode_to_count(hw_cfg.avg_mode);

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
    int32_t raw_vsh_uv = 0;
    esp_err_t read_err = read_raw_sample(raw_ma, raw_vsh_uv);

    InaSample sample{};
    sample.timestamp_us = timer_.get_time_us();
    sample.status = read_err;
    sample.shunt_voltage_uv = raw_vsh_uv;

    if (read_err != ESP_OK) {
        ESP_LOGW(TAG, "INA226 read failed: %s", esp_err_to_name(read_err));
        enqueue_sample(sample);
        return;
    }

    // Acknowledge the alert flags (MASK_ENABLE is read-to-clear). With
    // ALERT_ON_CONVERSION_READY (CNVR) the ALERT pin stays asserted until this
    // read, so it re-arms the next conversion interrupt. A failure is logged but
    // does not fail the sample: the watchdog timeout restarts the system if the
    // pin remains stuck.
    uint16_t alert_flags = 0;
    esp_err_t flags_err = driver_.read_alert_flags(alert_flags);
    if (flags_err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to acknowledge INA226 alert flags: %s", esp_err_to_name(flags_err));
    }

    apply_ema_filter(raw_ma, sample);

    if (reporting_enabled_.load()) {
        check_and_dispatch_telemetry(sample);
    }

    enqueue_sample(sample);
}

esp_err_t InaSensorTask::read_raw_sample(float& out_ma, int32_t& out_raw_vsh_uv)
{
    esp_err_t err = driver_.read_shunt_voltage_uv(out_raw_vsh_uv);
    if (err == ESP_OK) {
        int32_t corrected_vsh_uv = out_raw_vsh_uv - static_cast<int32_t>(shunt_zero_offset_uv_.load());
        // Shunt voltage (uV) to current (mA) conversion using cached r_shunt_ohms * 1000.0f
        float current_ma = static_cast<float>(corrected_vsh_uv) / uv_per_ma_;
        out_ma = std::max(0.0f, current_ma);
    }
    return err;
}

esp_err_t InaSensorTask::apply_night_config(const InaNightConfig& night_cfg)
{
    // Start from the active driver configuration to preserve the I2C address,
    // shunt and calibration parameters, and only override the conversion
    // settings that are regime-specific (day/night).
    ina226::Ina226Config hw_cfg = driver_.get_config();
    hw_cfg.avg_mode = night_cfg.avg_mode;
    hw_cfg.vbus_ct = night_cfg.vbus_ct;
    hw_cfg.vsh_ct = night_cfg.vsh_ct;
    return driver_.set_config(hw_cfg);
}

void InaSensorTask::apply_ema_filter(float raw_ma, InaSample& sample)
{
    if (!config_.enable_ema_filter) {
        ema_current_ma_ = raw_ma;
    }
    else if (ema_current_ma_ == 0.0f && raw_ma > 0.0f) {
        ema_current_ma_ = raw_ma;
    }
    else {
        float alpha = config_.ema_alpha;
        ema_current_ma_ = (alpha * raw_ma) + ((1.0f - alpha) * ema_current_ma_);
    }

    sample.isc_current_ma = static_cast<uint16_t>(ema_current_ma_ > 0.0f ? ema_current_ma_ : 0.0f);
}

void InaSensorTask::check_and_dispatch_telemetry(InaSample& sample)
{
    int64_t now_us = sample.timestamp_us;
    float delta_ma = std::abs(ema_current_ma_ - last_reported_current_ma_);
    float rel_delta = last_reported_current_ma_ > 0 ? (delta_ma / last_reported_current_ma_) : 0.0f;

    bool delta_triggered =
        (delta_ma >= config_.delta_threshold_ma) || (last_reported_current_ma_ >= DEFAULT_DAWN_CURRENT_THRESHOLD_MA &&
                                                     rel_delta >= config_.delta_threshold_percent);
    bool heartbeat_triggered = (now_us - last_report_timestamp_us_) >= (config_.heartbeat_interval_ms * 1000LL);

    sample.delta_detected = delta_triggered;

    if (delta_triggered || heartbeat_triggered) {
        last_reported_current_ma_ = ema_current_ma_;
        last_report_timestamp_us_ = now_us;
        if (send_telemetry_report(sample.isc_current_ma) != ESP_OK) {
            ESP_LOGE(TAG, "Failed to send telemetry report");
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
    TelemetrySnapshotData snap = snapshot_.get();

    farm::SolarSensorReport report{};
    report.power_profile = farm::PowerProfile::ALWAYS_ON;
    report.isc_current_ma = current_ma;

    float current_f = static_cast<float>(current_ma);
    report.irradiance_wm2 = static_cast<uint16_t>((current_f * 5.0f) / 3.0f);
    report.panel_temp_c = INT16_MIN; ///< INT16_MIN until DS18B20 driver reading is integrated
    report.battery_mv = snap.battery_mv;
    report.battery_percent = snap.battery_percent;
    report.battery_state = snap.battery_state;
    report.status = farm::SensorStatus::OK;
    report.max_current_ma = snap.max_current_ma;
    report.daily_yield_mah = snap.daily_yield_mah;
    report.is_night_mode = snap.is_night_mode;
    report.unix_time = time_manager_.is_synchronized() ? time_manager_.get_timestamp_ms() : 0;

    ESP_LOGI(
        TAG,
        "TX Telemetry: Isc=%u mA, Irradiance=%u W/m2, MaxDay=%u mA, Yield=%lu mAh, Bat=%u mV (%u%%)",
        report.isc_current_ma,
        report.irradiance_wm2,
        report.max_current_ma,
        static_cast<unsigned long>(report.daily_yield_mah),
        report.battery_mv,
        report.battery_percent);

    return espnow_.send_data(
        espnow::ReservedIds::HUB,
        static_cast<uint8_t>(farm::PayloadType::SOLAR_SENSOR_REPORT),
        &report,
        sizeof(report),
        false); // require_ack = false
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
