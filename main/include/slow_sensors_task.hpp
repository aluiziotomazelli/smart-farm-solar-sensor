// main/include/slow_sensors_task.hpp
#pragma once

#include <atomic>
#include <cstdint>

#include "interfaces/i_slow_sensors_task.hpp"
#include "interfaces/i_battery_monitor.hpp"
#include "interfaces/i_ds18b20_driver.hpp"
#include "interfaces/i_hal_freertos.hpp"
#include "telemetry_snapshot.hpp"

/**
 * @struct SlowSensorsConfig
 * @brief Configuration parameters for the SlowSensorsTask.
 */
struct SlowSensorsConfig
{
    uint32_t sample_interval_ms{60000}; ///< Nominal sampling interval in ms (default 60s)
    uint32_t task_stack_size{3072};     ///< FreeRTOS task stack size in bytes
    uint8_t task_priority{2};           ///< FreeRTOS task priority
    uint8_t max_consecutive_errors{5};  ///< Error count threshold before escalating log level
};

/**
 * @class SlowSensorsTask
 * @brief Dedicated background FreeRTOS task for non-critical slow sensors.
 */
class SlowSensorsTask : public ISlowSensorsTask
{
public:
    /**
     * @brief Constructs SlowSensorsTask with required dependencies.
     *
     * @param bat_monitor Battery monitor interface
     * @param ds18b20 DS18B20 temperature driver interface
     * @param rtos FreeRTOS HAL interface
     * @param snapshot Shared telemetry snapshot reference
     * @param config Task scheduling and execution configuration
     */
    SlowSensorsTask(
        battery_monitor::IBatteryMonitor& bat_monitor,
        ds18b20::IDs18b20Driver& ds18b20,
        idf_hals::IHalFreertos& rtos,
        TelemetrySnapshot& snapshot,
        const SlowSensorsConfig& config = SlowSensorsConfig{});

    ~SlowSensorsTask() override;

    /** @copydoc ISlowSensorsTask::init */
    esp_err_t init() override;

    /** @copydoc ISlowSensorsTask::start */
    esp_err_t start() override;

    /** @copydoc ISlowSensorsTask::stop */
    void stop() override;

    /**
     * @brief Executes a single sampling cycle for battery and temperature.
     * Useful for direct testing without running the FreeRTOS loop.
     */
    void process_cycle();

private:
    battery_monitor::IBatteryMonitor& bat_monitor_;
    ds18b20::IDs18b20Driver& ds18b20_;
    idf_hals::IHalFreertos& rtos_;
    TelemetrySnapshot& snapshot_;
    SlowSensorsConfig config_;

    std::atomic<bool> running_{false};
    TaskHandle_t task_handle_{nullptr};
    SemaphoreHandle_t task_done_semaphore_{nullptr};

    uint8_t consecutive_battery_errors_{0};
    uint8_t consecutive_temp_errors_{0};

    static void task_entry_point(void* arg);
    void task_loop();
};
