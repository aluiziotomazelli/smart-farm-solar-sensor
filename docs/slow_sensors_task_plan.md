# Implementation Plan: `SlowSensorsTask`

**Component:** `main/`
**Target:** ESP32-C3 (XIAO)
**Date:** 2026-08-13

---

## 1. Context & Motivation

### Current Situation

| Sensor | Where read | Frequency | Problem |
|--------|-----------|-----------|---------|
| Battery (ADC) | `SolarSensor::update_battery_snapshot()` | Every `run()` tick (~100 ms) | 16x ADC samples x 1 ms delay = ~16 ms blocking `main` per tick |
| DS18B20 (1-Wire) | Not integrated yet | — | 800 ms `vTaskDelay` at 12-bit resolution would stall `main` entirely |

### Goal

Create a single dedicated FreeRTOS task — `SlowSensorsTask` — that:
- Coordinates periodic sampling (every 60 s) of **Battery** and **DS18B20 Temperature**.
- Receives `IBatteryMonitor&` and `IDs18b20Driver&` via constructor Dependency Injection (pure abstract interfaces).
- Publishes results directly into `TelemetrySnapshot` (thread-safe, mutex-protected).
- Completely decouples slow I/O and blocking delays from the `SolarSensor` main loop.

---

## 2. Architecture & Design Decisions

### 2.1 Pure Dependency Injection & Composition Root
`SlowSensorsTask` depends **only** on the abstract interfaces:
- `battery_monitor::IBatteryMonitor&`
- `ds18b20::IDs18b20Driver&`
- `idf_hals::IHalFreertos&`
- `TelemetrySnapshot&`

All hardware pinouts, dividers, and resolution configs stay in `main.cpp` (the Composition Root) where drivers are constructed.

### 2.2 Minimal `SlowSensorsConfig`
Because sensor configurations are handled by their respective drivers, `SlowSensorsConfig` contains only task scheduling parameters:
```cpp
struct SlowSensorsConfig {
    uint32_t sample_interval_ms{60000};  ///< Sampling period in ms (nominal 1 minute)
    uint32_t task_stack_size{3072};      ///< FreeRTOS task stack size in bytes
    uint8_t task_priority{2};            ///< FreeRTOS task priority
    uint8_t max_consecutive_errors{5};   ///< Threshold for error escalation
};
```

### 2.3 `TelemetrySnapshot` as Single Source of Truth
```
SlowSensorsTask  -->  snapshot_.update_battery()
                 -->  snapshot_.update_temperature()
InaSensorTask    <--  snapshot_.get()
SolarSensor      <--  snapshot_.get() (e.g. for NVS persistence)
```

---

## 3. Task Execution Flow

```
SlowSensorsTask::task_loop()
│
├── [on start] bat_monitor_.init()
│              ds18b20_.init()
│
└── loop forever:
    │
    ├── [1] bat_monitor_.read(reading)            ← ~16 ms (fast, performed first)
    │         └── snapshot_.update_battery(reading.voltage_mv, reading.percent, state)
    │
    ├── [2] ds18b20_.read_temperature(&temp_c)    ← ~800 ms (blocks task, NOT main)
    │         └── snapshot_.update_temperature(temp_c)
    │
    ├── [on error] log warning + increment consecutive error counters
    │
    └── [3] vTaskDelay(pdMS_TO_TICKS(interval_ms - elapsed_ms))
```

---

## 4. File Changes

### 4.1 New Files
- `main/include/interfaces/i_slow_sensors_task.hpp`: Abstract lifecycle interface (`init()`, `start()`, `stop()`).
- `main/include/slow_sensors_task.hpp`: Concrete class declaration and `SlowSensorsConfig`.
- `main/src/slow_sensors_task.cpp`: FreeRTOS task implementation loop.

### 4.2 Modified Files
- `main/include/telemetry_snapshot.hpp`: Add `float temperature_celsius{-127.0f}` and `update_temperature(float)`.
- `main/include/solar_sensor.hpp`: Replace `battery_monitor::IBatteryMonitor&` with `ISlowSensorsTask&`.
- `main/src/solar_sensor.cpp`: Remove `update_battery_snapshot()`; initialize `slow_sensors_task_`; extract battery from snapshot in `save_persistent_state()`.
- `main/main.cpp`: Instantiate `ds18b20::OnewireBusHAL`, `ds18b20::Ds18b20Driver`, and `SlowSensorsTask`; inject `SlowSensorsTask` into `SolarSensor`.
- `main/CMakeLists.txt`: Add `slow_sensors_task.cpp` to `SRCS` and `ds18b20_driver` to `REQUIRES`.

---

## 5. Proposed Interfaces

### `ISlowSensorsTask`
```cpp
// main/include/interfaces/i_slow_sensors_task.hpp
#pragma once
#include "esp_err.h"

class ISlowSensorsTask
{
public:
    virtual ~ISlowSensorsTask() = default;

    /**
     * @brief Initializes the battery monitor and DS18B20 drivers.
     * @return ESP_OK on success, ESP_ERR_* on driver failure.
     */
    virtual esp_err_t init() = 0;

    /**
     * @brief Launches the FreeRTOS worker task.
     * @return ESP_OK on success, ESP_ERR_NO_MEM on task creation failure.
     */
    virtual esp_err_t start() = 0;

    /**
     * @brief Stops the FreeRTOS task and releases task handles.
     */
    virtual void stop() = 0;
};
```

---

## 6. Implementation Steps

1. **Update `TelemetrySnapshot`**: Add `temperature_celsius` and `update_temperature()`.
2. **Create `ISlowSensorsTask` Interface**: Save in `main/include/interfaces/i_slow_sensors_task.hpp`.
3. **Implement `SlowSensorsTask`**: In `main/include/slow_sensors_task.hpp` and `main/src/slow_sensors_task.cpp`.
4. **Refactor `SolarSensor`**: Remove direct battery monitor dependency; delegate slow sensing to `ISlowSensorsTask`.
5. **Update `main.cpp`**: Construct DS18B20 driver and `SlowSensorsTask`, pass dependencies into `SolarSensor`.
6. **Update `main/CMakeLists.txt`**: Register component dependencies.
7. **Validate Builds & Tests**:
   - `idf.py build` for ESP32-C3.
   - Run host tests to ensure 0 regressions.
