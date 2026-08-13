#pragma once

#include <atomic>
#include "interfaces/i_ina_sensor_task.hpp"
#include "interfaces/i_ina226_driver.hpp"
#include "interfaces/i_espnow_manager.hpp"
#include "interfaces/i_hal_timer.hpp"
#include "interfaces/i_hal_freertos.hpp"
#include "interfaces/i_time_manager.hpp"
#include "telemetry_snapshot.hpp"

namespace ina {

class InaSensorTask : public IInaSensorTask
{
public:
    InaSensorTask(
        ina226::IIna226Driver& driver,
        espnow::IEspNowManager& espnow,
        idf_hals::ITimerHAL& timer,
        idf_hals::IHalFreertos& rtos,
        time_manager::ITimeManager& time_manager,
        TelemetrySnapshot& snapshot,
        QueueHandle_t sample_queue);

    ~InaSensorTask() override;

    esp_err_t init(const InaSensorConfig& config, i2c_master_bus_handle_t i2c_bus) override;
    esp_err_t start() override;
    void stop() override;

    void set_reporting_enabled(bool enabled) override { reporting_enabled_.store(enabled); }
    void set_sampling_enabled(bool enabled) override { sampling_enabled_.store(enabled); }
    void set_shunt_zero_offset_uv(int16_t offset_uv) override { shunt_zero_offset_uv_.store(offset_uv); }
    esp_err_t prepare_for_sleep() override;

    uint32_t get_expected_sample_period_ms() const override;
    uint32_t get_watchdog_timeout_ms() const override;

    /**
     * @brief Performs one single sampling and processing cycle (used by task loop and unit tests).
     */
    void process_cycle();

    bool is_reporting_enabled() const { return reporting_enabled_.load(); }
    bool is_sampling_enabled() const override { return sampling_enabled_.load(); }
    TaskHandle_t get_task_handle() const override { return task_handle_; }

private:
    ina226::IIna226Driver& driver_;
    espnow::IEspNowManager& espnow_;
    idf_hals::ITimerHAL& timer_;
    idf_hals::IHalFreertos& rtos_;
    time_manager::ITimeManager& time_manager_;
    TelemetrySnapshot& snapshot_;
    QueueHandle_t sample_queue_;

    InaSensorConfig config_{};
    std::atomic<bool> reporting_enabled_{false};
    std::atomic<bool> sampling_enabled_{true};
    std::atomic<bool> running_{false};
    std::atomic<int16_t> shunt_zero_offset_uv_{0};

    TaskHandle_t task_handle_ = nullptr;
    static void task_entry_point(void* arg);
    void ina_sensor_task();
    SemaphoreHandle_t task_done_semaphore_ = nullptr;

    float ema_current_ma_{0.0f};
    float last_reported_current_ma_{0.0f};
    int64_t last_report_timestamp_us_{0};
    float uv_per_ma_{100.0f};

    static constexpr float EMA_ALPHA = 0.8f;

    esp_err_t read_raw_sample(float& out_ma, int32_t& out_raw_vsh_uv);
    esp_err_t apply_night_config(const InaNightConfig& night_cfg);
    void apply_ema_filter(float raw_ma, InaSample& sample);
    void check_and_dispatch_telemetry(InaSample& sample);
    void enqueue_sample(const InaSample& sample);
    esp_err_t send_telemetry_report(uint16_t current_ma);
};

} // namespace ina
