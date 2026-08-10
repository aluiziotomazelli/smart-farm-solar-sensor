#pragma once

#include <atomic>
#include "interfaces/i_ina_sensor_task.hpp"
#include "interfaces/i_ina226_driver.hpp"
#include "interfaces/i_espnow_manager.hpp"
#include "interfaces/i_hal_timer.hpp"
#include "interfaces/i_hal_freertos.hpp"

namespace ina {

class InaSensorTask : public IInaSensorTask
{
public:
    InaSensorTask(
        ina226::IIna226Driver& driver,
        espnow::IEspNowManager& espnow,
        idf_hals::ITimerHAL& timer,
        idf_hals::IHalFreertos& rtos,
        QueueHandle_t sample_queue);

    ~InaSensorTask() override;

    esp_err_t init(const InaSensorConfig& config, i2c_master_bus_handle_t i2c_bus) override;
    esp_err_t start() override;
    void stop() override;

    void set_reporting_enabled(bool enabled) override { reporting_enabled_.store(enabled); }
    void set_sampling_enabled(bool enabled) override { sampling_enabled_.store(enabled); }
    esp_err_t set_operating_mode(SolarNodeState mode) override;

    uint32_t get_expected_sample_period_ms() const override;
    uint32_t get_watchdog_timeout_ms() const override;

    /**
     * @brief Performs one single sampling and processing cycle (used by task loop and unit tests).
     */
    void process_cycle();

    bool is_reporting_enabled() const { return reporting_enabled_.load(); }
    bool is_sampling_enabled() const { return sampling_enabled_.load(); }
    TaskHandle_t get_task_handle() const override { return task_handle_; }
    SolarNodeState get_operating_mode() const override { return mode_; }

private:
    ina226::IIna226Driver& driver_;
    espnow::IEspNowManager& espnow_;
    idf_hals::ITimerHAL& timer_;
    idf_hals::IHalFreertos& rtos_;
    QueueHandle_t sample_queue_;

    InaSensorConfig config_{};
    SolarNodeState mode_{SolarNodeState::DAY_ACTIVE};
    std::atomic<bool> reporting_enabled_{true};
    std::atomic<bool> sampling_enabled_{true};
    std::atomic<bool> running_{false};

    TaskHandle_t task_handle_ = nullptr;
    static void task_entry_point(void* arg);
    void ina_sensor_task();
    SemaphoreHandle_t task_done_semaphore_ = nullptr;

    float ema_current_ma_{0.0f};
    float last_reported_current_ma_{0.0f};
    int64_t last_report_timestamp_us_{0};

    static constexpr float EMA_ALPHA = 0.2f;

    esp_err_t read_raw_sample(float& out_ma, uint16_t& out_bus_mv);
    void apply_ema_filter(float raw_ma, InaSample& sample);
    void check_and_dispatch_telemetry(InaSample& sample);
    void enqueue_sample(const InaSample& sample);
    esp_err_t send_telemetry_report(uint16_t current_ma);
};

} // namespace ina
