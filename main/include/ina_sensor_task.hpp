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

/**
 * @class InaSensorTask
 * @brief Concrete implementation of IInaSensorTask for INA226 solar current monitoring.
 */
class InaSensorTask : public IInaSensorTask
{
public:
    /**
     * @brief Constructs the InaSensorTask with required dependencies.
     *
     * @param driver INA226 hardware driver instance
     * @param espnow ESP-NOW radio manager instance
     * @param timer Hardware timer HAL instance
     * @param rtos FreeRTOS HAL instance
     * @param time_manager Time synchronization manager instance
     * @param snapshot Telemetry snapshot reference
     * @param sample_queue Optional FreeRTOS queue handle for enqueuing raw samples
     */
    InaSensorTask(
        ina226::IIna226Driver& driver,
        espnow::IEspNowManager& espnow,
        idf_hals::ITimerHAL& timer,
        idf_hals::IHalFreertos& rtos,
        time_manager::ITimeManager& time_manager,
        TelemetrySnapshot& snapshot,
        QueueHandle_t sample_queue);

    ~InaSensorTask() override;

    /** @copydoc IInaSensorTask::init */
    esp_err_t init(const InaSensorConfig& config, i2c_master_bus_handle_t i2c_bus) override;

    /** @copydoc IInaSensorTask::start */
    esp_err_t start() override;

    /** @copydoc IInaSensorTask::stop */
    void stop() override;

    /** @copydoc IInaSensorTask::deinit */
    esp_err_t deinit() override;

    /** @copydoc IInaSensorTask::set_reporting_enabled */
    void set_reporting_enabled(bool enabled) override { reporting_enabled_.store(enabled); }

    /** @copydoc IInaSensorTask::set_sampling_enabled */
    void set_sampling_enabled(bool enabled) override { sampling_enabled_.store(enabled); }

    /** @copydoc IInaSensorTask::set_shunt_zero_offset_uv */
    void set_shunt_zero_offset_uv(int16_t offset_uv) override { shunt_zero_offset_uv_.store(offset_uv); }

    /** @copydoc IInaSensorTask::prepare_for_sleep */
    esp_err_t prepare_for_sleep() override;

    /** @copydoc IInaSensorTask::is_sampling_enabled */
    bool is_sampling_enabled() const override { return sampling_enabled_.load(); }

    /** @copydoc IInaSensorTask::is_reporting_enabled */
    bool is_reporting_enabled() const override { return reporting_enabled_.load(); }

    /** @copydoc IInaSensorTask::get_expected_sample_period_ms */
    uint32_t get_expected_sample_period_ms() const override;

    /** @copydoc IInaSensorTask::get_watchdog_timeout_ms */
    uint32_t get_watchdog_timeout_ms() const override;

    /** @copydoc IInaSensorTask::get_task_handle */
    TaskHandle_t get_task_handle() const override { return task_handle_; }

    /** @copydoc IInaSensorTask::process_cycle */
    void process_cycle() override;

private:
    ina226::IIna226Driver& driver_;            ///< INA226 hardware driver dependency
    espnow::IEspNowManager& espnow_;           ///< ESP-NOW radio manager dependency
    idf_hals::ITimerHAL& timer_;               ///< Hardware timer HAL dependency
    idf_hals::IHalFreertos& rtos_;             ///< FreeRTOS HAL dependency
    time_manager::ITimeManager& time_manager_; ///< Time synchronization manager dependency
    TelemetrySnapshot& snapshot_;              ///< Telemetry snapshot reference
    QueueHandle_t sample_queue_;               ///< Optional sample output queue handle

    InaSensorConfig config_{};                     ///< Active task configuration
    std::atomic<bool> reporting_enabled_{false};   ///< Telemetry reporting state
    std::atomic<bool> sampling_enabled_{true};     ///< Active sampling state
    std::atomic<bool> running_{false};             ///< Task execution loop state
    std::atomic<int16_t> shunt_zero_offset_uv_{0}; ///< Zero-current shunt offset calibration in uV
    bool initialized_{false};                      ///< Driver/task initialization state

    TaskHandle_t task_handle_ = nullptr;              ///< FreeRTOS task handle
    static void task_entry_point(void* arg);          ///< FreeRTOS static entry point wrapper
    void ina_sensor_task();                           ///< Internal FreeRTOS task processing loop
    SemaphoreHandle_t task_done_semaphore_ = nullptr; ///< Task exit synchronization semaphore

    float ema_current_ma_{0.0f};           ///< Filtered EMA current accumulator in mA
    float last_reported_current_ma_{0.0f}; ///< Last transmitted current value in mA
    int64_t last_report_timestamp_us_{0};  ///< Timestamp of last transmitted telemetry report
    float uv_per_ma_{100.0f};              ///< Cached uV to mA conversion factor (r_shunt * 1000)

    esp_err_t read_raw_sample(float& out_ma, int32_t& out_raw_vsh_uv); ///< Reads raw shunt voltage and converts to mA
    esp_err_t apply_night_config(const InaNightConfig& night_cfg);     ///< Applies conversion settings for deep sleep
    void apply_ema_filter(float raw_ma, InaSample& sample);            ///< Applies EMA filter or raw current to sample
    void check_and_dispatch_telemetry(InaSample& sample); ///< Evaluates delta/heartbeat and dispatches report
    void enqueue_sample(const InaSample& sample);         ///< Enqueues sample to internal queue if provided
    esp_err_t send_telemetry_report(uint16_t current_ma); ///< Packs and sends SolarSensorReport over ESP-NOW
};

} // namespace ina