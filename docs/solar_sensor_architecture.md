# Architecture and Testability Plan (Host-Test / TDD) - SolarSensor

## 1. Overview and Physical Model

The **SolarSensor** is a peripheral sensor node based on the **Seeed XIAO ESP32-C3** microcontroller, designed to monitor solar irradiance in real time and estimate the instantaneous power generation capacity of a **2.64 kWp (8 x 330W)** photovoltaic array operating with an inverter and no battery bank.

### 1.1 Measurement Principle
- **Sensor Element:** **5W** photovoltaic panel (polycrystalline), installed in the same location, tilt, orientation, temperature, and soiling conditions as the main array.
- **Measured Quantity:** Short-circuit current ($I_{sc}$). The $I_{sc}$ current is directly proportional to solar irradiance ($W/m^2$). Open-circuit voltage ($V_{oc}$) measurement was discarded because it varies little with irradiance.
- **Measurement Hardware:** **INA226** IC connected via **I2C** with a **$0.1\Omega$ (SMD R100)** shunt resistor.
- **Scale Matching:**
  - 5W Panel Specifications: $P_{max} = 5\text{W}$, $V_{oc} = 11.2\text{V}$, $V_{mp} = 9.25\text{V}$, $I_{sc\_nominal} = 0.6\text{A}$ (measured up to $0.7\text{A}$ under full sun).
  - Shunt voltage at $0.7\text{A}$: $V_{shunt} = 0.7\text{A} \times 0.1\Omega = 70\text{mV}$.
  - INA226 limit: $\pm 81.92\text{mV}$ (full scale of $819.2\text{mA}$ with $25\mu\text{A}/LSB$ resolution).
  - The measured peak of $0.7\text{A}$ operates at **~85% of full scale**, providing excellent resolution and 15% safety margin against overcurrent.

---

## 2. Data Structures and Protocol

### 2.1 Local Persistence (`SolarStats` in `solar_sensor_stats.hpp`)
Maintained in **RTC RAM** (survives *deep sleep*) and synchronized periodically with **NVS**.

| Field | Type | Description |
| :--- | :--- | :--- |
| `magic` | `uint16_t` | Validation identifier (`0x534F` = "SO") |
| `version` | `uint8_t` | Persistence structure version |
| `gpio_wakeup_enabled` | `bool` | External wakeup interrupt enable status |
| `is_night_mode` | `bool` | Flag indicating whether the node is operating in night mode |
| `last_battery_mv` | `uint16_t` | Last measured node battery voltage in mV |
| `last_battery_percent` | `uint8_t` | Last node battery percentage (0-100%) |
| `last_battery_state` | `BatteryState` | Battery classification (NORMAL, LOW, CRITICAL) |
| `max_current_ma` | `uint16_t` | Highest short-circuit current recorded during the day |
| `min_day_current_ma` | `uint16_t` | Lowest current recorded during the daytime period |
| `daily_yield_mah` | `uint32_t` | Accumulated daily current integral in mAh |
| `shunt_zero_offset_uv` | `int16_t` | INA226 zero offset measured during the night |
| `crc` | `uint32_t` | CRC32 for memory integrity validation |

### 2.2 ESP-NOW Telemetry (`SolarSensorReport` in `farm_protocol_types.hpp`)
Packed binary payload (`#pragma pack(push, 1)`), totaling **27 bytes** (ESP-NOW maximum: 230 bytes).

```cpp
struct SolarSensorReport
{
    PowerProfile power_profile;   ///< Node energy mode (ALWAYS_ON, LOW_POWER, DEEP_SLEEP)
    uint16_t isc_current_ma;      ///< Instantaneous short-circuit current in mA (0 - 819 mA)
    uint16_t irradiance_wm2;      ///< Estimated solar irradiance in W/m² (0 - 1200 W/m²)
    uint16_t estimated_power_w;   ///< Estimated generation capacity of the 2.64 kWp array in W
    uint16_t battery_mv;          ///< Sensor node battery voltage in mV
    uint8_t  battery_percent;     ///< Sensor node battery percentage level (0-100%)
    BatteryState battery_state;   ///< Sensor node battery classification
    SensorStatus status;          ///< Sensor health status (OK, ERROR_HARDWARE, etc.)
    uint16_t max_current_ma;      ///< Current peak recorded during the current day
    uint32_t daily_yield_mah;     ///< Accumulated daily current integral in mAh
    bool     is_night_mode;       ///< Indicates whether the node is in night mode
    uint64_t unix_time;           ///< UTC Epoch timestamp in ms (0 if not synchronized)
};
```

---

## 3. Software Architecture and Tasks

```
               +-------------------------------------------------------+
               |                   Hardware / INA226                   |
               +-------------------------------------------------------+
                                           | I2C Master (400kHz)
                                           v
               +-------------------------------------------------------+
               |                  ina_sensor_task                      |
               |  * Runs at 4-8 Hz                                     |
               |  * Reads INA226 registers via II2cHAL                 |
               |  * Hardware Averaging (16x) + Software EMA Filter     |
               |  * Delta Detection (Delta I > 3% or 10mA)             |
               +-------------------------------------------------------+
                                           |
                    +----------------------+----------------------+
                    | (Publish sample / event)                    |
                    v                                             v
+----------------------------------------+   +----------------------------------------+
|               Main Task                |   |             EspNowManager              |
| * System State Machine (Day/Night/OTA) |   | * Internal Queue & Worker Task         |
| * Battery Monitor (Periodic, e.g. 60s) |   | * Non-blocking Fire-and-Forget TX      |
| * NVS/RTC Persistence Commits          |   | * Delta-triggered + 1s Heartbeat       |
| * Deep Sleep Management                |   +----------------------------------------+
+----------------------------------------+
```

1. **`ina_sensor_task` (Frequency: 4 to 8 Hz):**
   - Performs periodic INA226 readings through `II2cHAL`.
   - Uses the INA226 16-sample hardware average together with a software Exponential Moving Average (EMA) digital filter.
   - Detects abrupt irradiance changes ($\Delta I_{sc} > 3\%$).

2. **`Main Task` (System Coordinator):**
   - Manages the main state machine (`INIT`, `DAY_ACTIVE`, `DUSK_CHECK`, `NIGHT_SLEEP`, `OTA_MODE`).
   - Samples the board battery at long intervals (every 60s).
   - Commands entry into night *Deep Sleep* when $I_{sc} < 3\text{mA}$ for an extended period.

3. **ESP-NOW Radio Strategy:**
   - **Non-blocking** send (*fire-and-forget*) queued in the internal `EspNowManager` task (without waiting for a logical ACK, so the real-time flow is not blocked).
   - **Delta + Heartbeat Report:** Sends a packet when there is a significant current change or every 1 second (heartbeat).

---

## 4. Host Testability Strategy (`host_test`) & TDD

The repository adopts **Test-Driven Development (TDD)** in the native x86 test environment (`host_test`) using **GoogleTest** and **GoogleMock**. No logic or driver functionality should be added without a corresponding unit test.

### 4.1 Dependency Injection (DI) and Hardware Abstraction
All interaction with hardware, file systems, time, and the OS is performed through **pure C++ interfaces**:

- **I2C:** [`idf_hals::II2cHAL`](file:///home/german/dev/workspaces/smart-farm/smart-farm-solar-sensor/components/idf_hals/include/interfaces/i_hal_i2c.hpp) (Mock: [`MockI2cHAL`](file:///home/german/dev/workspaces/smart-farm/smart-farm-solar-sensor/components/idf_hals/mocks/mock_hal_i2c.hpp))
- **INA226 Driver:** `IIna226Driver` (Mock: `MockIna226Driver`)
- **Timing / RTOS:** [`ITimerHAL`](file:///home/german/dev/workspaces/smart-farm/smart-farm-solar-sensor/components/idf_hals/include/interfaces/i_hal_timer.hpp), [`IHalFreertos`](file:///home/german/dev/workspaces/smart-farm/smart-farm-solar-sensor/components/idf_hals/include/interfaces/i_hal_freertos.hpp)
- **Power / Sleep:** [`ISleepHAL`](file:///home/german/dev/workspaces/smart-farm/smart-farm-solar-sensor/components/idf_hals/include/interfaces/i_hal_sleep.hpp), `IBatteryMonitor`
- **Storage:** [`INvsCore`](file:///home/german/dev/workspaces/smart-farm/smart-farm-solar-sensor/main/include/interfaces/i_nvs_core.hpp)

### 4.2 Modules and Test Suites in `host_test`

| Test Suite | Test Target | What is tested / asserted |
| :--- | :--- | :--- |
| `Ina226DriverTest` | `Ina226Driver` | Register writes/reads through `MockI2cHAL`, correct $mV \to mA$ conversion calculation, ALERT register configuration, response to I2C failures (NACK/timeout). |
| `SolarMathTest` | Solar calculation functions | Conversion from $I_{sc} \to \text{Irradiance } (W/m^2)$, 2.64 kWp array power estimation, daily integral in mAh, Exponential Moving Average (EMA) filter. |
| `SolarSensorTaskTest` | Sampling Task | Sampling rate, report triggering by $\Delta I$ variation, instantaneous noise handling, shared state update. |
| `SolarSensorAppTest` | `SolarSensor` App Class | State machine transitions (Day $\leftrightarrow$ Night), statistics saving to NVS/RTC, OTA trigger dispatch. |

### 4.3 TDD Development Cycle (Red-Green-Refactor)

1. **Step 1 (Red - Write the Test):**
   Create the test file or add a `TEST_F` to the appropriate suite in `host_test/test_solar_sensor/main/`. Define mock expectations (`EXPECT_CALL(...)`) and call the method of the class under test.
2. **Step 2 (Green - Implement the Minimum):**
   Write only the code strictly required in the component/main so the test passes. Run `idf.py host_test` or the host CMake build.
3. **Step 3 (Refactor - Improve Safely):**
   Clean up and optimize the implementation with the assurance that the test suite guarantees no regression has been introduced.

---

## 5. Next Implementation Steps

1. Create the `IIna226Driver` interface and its `MockIna226Driver` mock.
2. Develop the `host_test` tests for `Ina226Driver` based on `MockI2cHAL`.
3. Implement the `Ina226Driver` class until all unit tests pass.
4. Implement the tests for the solar math functions (`SolarMathTest`).
5. Integrate sampling and `SolarSensorReport` generation into `SolarSensor`.
