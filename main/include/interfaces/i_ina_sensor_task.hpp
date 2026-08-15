#pragma once

#include "esp_err.h"
#include "interfaces/i_hal_i2c.hpp"
#include "ina_sensor_types.hpp"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace ina {

/**
 * @class IInaSensorTask
 * @brief Interface for the INA226 solar sensor FreeRTOS task.
 *
 * This interface abstracts the sensor task responsible for:
 * - Periodic sampling of the INA226 current/power sensor
 * - EMA filtering and delta-based telemetry reporting
 * - Day/night regime configuration (conversion settings, alerts)
 * - Deep-sleep preparation with dawn wakeup alert arming
 *
 * Thread Safety:
 * - init(), start(), stop(), prepare_for_sleep(): NOT thread-safe; call from single init thread
 * - set_reporting_enabled(), set_sampling_enabled(), set_shunt_zero_offset_uv(): Thread-safe (atomic)
 * - is_sampling_enabled(), is_reporting_enabled(): Thread-safe (atomic)
 * - get_expected_sample_period_ms(), get_watchdog_timeout_ms(): Thread-safe (read-only after init)
 * - get_task_handle(): Thread-safe (read-only after start)
 *
 * Lifetime:
 * - Create implementation instance
 * - Call init() once with configuration and I2C bus handle
 * - Call start() to launch the FreeRTOS task
 * - Call prepare_for_sleep() before entering deep sleep
 * - Call stop() to gracefully terminate the task
 * - Destructor calls stop() if not already stopped
 */
class IInaSensorTask
{
public:
    virtual ~IInaSensorTask() = default;

    // ---------------------------------------------------------------------
    // Lifecycle (NOT thread-safe - call from single initialization thread)
    // ---------------------------------------------------------------------

    /**
     * @brief Initialize the sensor task with configuration and I2C bus.
     *
     * Configures the INA226 driver with daytime conversion settings, arms
     * the conversion-ready alert (ALERT_ON_CONVERSION_READY), and creates
     * the FreeRTOS processing task.
     *
     * @param config Sensor configuration (thresholds, intervals, night config)
     * @param i2c_bus Initialized I2C master bus handle
     * @return ESP_OK on success, ESP_ERR_* on I2C, driver, or task creation failure
     */
    virtual esp_err_t init(const InaSensorConfig& config, i2c_master_bus_handle_t i2c_bus) = 0;

    /**
     * @brief Start sampling and telemetry reporting.
     *
     * Enables sampling and reporting flags on the running FreeRTOS task.
     *
     * @return ESP_OK on success
     */
    virtual esp_err_t start() = 0;

    /**
     * @brief Pause sampling and telemetry reporting.
     *
     * Disables sampling and reporting flags. The FreeRTOS task remains
     * alive in passive wait state. Safe to call multiple times.
     */
    virtual void stop() = 0;

    /**
     * @brief Deinitialize the sensor task and detach the underlying driver.
     *
     * Signals and deletes the FreeRTOS processing task, cleans up synchronization
     * primitives, and detaches the INA226 device from the I2C master bus.
     * Safe to call multiple times.
     *
     * @return ESP_OK on success, or driver deinit error code.
     */
    virtual esp_err_t deinit() = 0;

    // ---------------------------------------------------------------------
    // Runtime Control (Thread-safe - atomic operations)
    // ---------------------------------------------------------------------

    /**
     * @brief Enable or disable telemetry reporting to the hub.
     *
     * When disabled, samples are still acquired and enqueued locally but
     * no ESP-NOW reports are sent. Useful during commissioning or when
     * the radio is powered down.
     *
     * @param enabled true to enable reporting, false to disable
     */
    virtual void set_reporting_enabled(bool enabled) = 0;

    /**
     * @brief Enable or disable periodic sampling.
     *
     * When disabled, the task loop exits early without reading the sensor.
     * prepare_for_sleep() calls this internally. The watchdog is still
     * armed based on the expected period; disabling sampling effectively
     * pauses the watchdog.
     *
     * @param enabled true to enable sampling, false to disable
     */
    virtual void set_sampling_enabled(bool enabled) = 0;

    /**
     * @brief Set the shunt zero-current offset in microvolts.
     *
     * This offset is subtracted from raw shunt voltage readings to
     * compensate for sensor/amplifier offset error. Typically calibrated
     * at startup with no panel current.
     *
     * @param offset_uv Offset in microvolts (signed, can be negative)
     */
    virtual void set_shunt_zero_offset_uv(int16_t offset_uv) = 0;

    // ---------------------------------------------------------------------
    // Deep Sleep Support (NOT thread-safe - call before sleep entry)
    // ---------------------------------------------------------------------

    /**
     * @brief Prepare the sensor for deep sleep (night regime).
     *
     * Performs the following:
     * 1. Disables sampling and reporting
     * 2. Applies night conversion settings (slower conversions, higher averaging)
     *    from InaSensorConfig::night_config to reduce INA226 power consumption
     * 3. Arms the SHUNT_OVER_VOLTAGE alert with DEFAULT_DAWN_WAKEUP_ALERT_LIMIT
     *    so the ALERT pin wakes the MCU when panel current rises at dawn
     *
     * Call this immediately before entering deep sleep. After wakeup,
     * a new init() cycle is expected (the INA226 state is reset by power cycle).
     *
     * @return ESP_OK on success, ESP_ERR_* on I2C failure
     */
    virtual esp_err_t prepare_for_sleep() = 0;

    // ---------------------------------------------------------------------
    // Status Queries (Thread-safe - atomic or read-only after init)
    // ---------------------------------------------------------------------

    /**
     * @brief Check if sampling is currently enabled.
     *
     * @return true if sampling is enabled, false otherwise
     */
    virtual bool is_sampling_enabled() const = 0;

    /**
     * @brief Check if telemetry reporting is currently enabled.
     *
     * @return true if reporting is enabled, false otherwise
     */
    virtual bool is_reporting_enabled() const = 0;

    /**
     * @brief Get the expected sampling period in milliseconds.
     *
     * Calculated from the currently active INA226 conversion settings
     * (conversion times + averaging count). Reflects the day regime after
     * init() or the night regime after prepare_for_sleep().
     *
     * @return Expected period between samples in milliseconds
     */
    virtual uint32_t get_expected_sample_period_ms() const = 0;

    /**
     * @brief Get the watchdog timeout in milliseconds.
     *
     * The watchdog timeout is derived from the expected sample period
     * (3x the period, minimum 500ms). Used by the task to detect stuck
     * conversions or I2C bus hangs.
     *
     * @return Watchdog timeout in milliseconds
     */
    virtual uint32_t get_watchdog_timeout_ms() const = 0;

    /**
     * @brief Get the FreeRTOS task handle.
     *
     * Valid after start() returns ESP_OK. Can be used for task
     * notifications, priority changes, or debugging.
     *
     * @return Task handle, or nullptr if task not started
     */
    virtual TaskHandle_t get_task_handle() const = 0;

    // ---------------------------------------------------------------------
    // Testing / Diagnostics (Thread-safe - read-only or atomic)
    // ---------------------------------------------------------------------

    /**
     * @brief Perform a single sampling and processing cycle.
     *
     * Primarily intended for unit testing to drive the task logic
     * without the FreeRTOS scheduler. In normal operation, the task
     * calls this internally from its loop.
     *
     * If sampling is disabled, the function returns immediately.
     * The sample is enqueued to the internal queue if a queue was provided.
     */
    virtual void process_cycle() = 0;
};

} // namespace ina