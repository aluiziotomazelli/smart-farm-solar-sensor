// main/include/interfaces/i_slow_sensors_task.hpp
#pragma once

#include "esp_err.h"

/**
 * @class ISlowSensorsTask
 * @brief Interface for the low-frequency sensor sampling FreeRTOS task.
 *
 * Manages periodic (nominal 60 seconds) reading of non-critical sensors:
 * - Battery voltage and state (via IBatteryMonitor)
 * - DS18B20 ambient temperature (via IDs18b20Driver)
 *
 * Results are published directly to TelemetrySnapshot (thread-safe).
 *
 * Lifecycle:
 *   init() -> start() -> [running in background] -> stop()
 *
 * Thread Safety:
 *   init(), start(), stop(): NOT thread-safe; call from single init thread.
 */
class ISlowSensorsTask
{
public:
    virtual ~ISlowSensorsTask() = default;

    /**
     * @brief Initializes both sensor drivers (Battery Monitor and DS18B20).
     *
     * @return ESP_OK on success, ESP_ERR_* on driver failure.
     */
    virtual esp_err_t init() = 0;

    /**
     * @brief Launches the background FreeRTOS worker task.
     *
     * @return ESP_OK on success, ESP_ERR_NO_MEM if task creation fails.
     */
    virtual esp_err_t start() = 0;

    /**
     * @brief Gracefully terminates the FreeRTOS task and cleans up resources.
     */
    virtual void stop() = 0;
};
