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

### 1.2 Physical Irradiance Model and Empirical Benchmarks

- **Datasheet Specifications (STC: 1000 W/m² @ 25°C)**:
  - Nominal Short-Circuit Current: $I_{sc\_nominal} = 600\text{ mA}$ ($0.6\text{ A}$).
  - Nominal Solar Irradiance: $1000\text{ W/m}^2$.

- **Empirical Field Measurement Benchmark**:
  - Measured at midday (clear sky, panel perpendicular to sun): $I_{sc\_measured} = 720\text{ mA}$.
  - Equivalent Solar Irradiance:
    $$\text{Irradiance} = \frac{720\text{ mA}}{600\text{ mA}} \times 1000\text{ W/m}^2 = 1200\text{ W/m}^2$$
  - INA226 Full Scale Verification ($R_{shunt} = 0.1\Omega$):
    $$V_{shunt} = 720\text{ mA} \times 0.1\Omega = 72\text{ mV}$$
    Operates at **88% of INA226 full-scale** ($\pm 81.92\text{ mV}$ / $819.2\text{ mA}$ max), providing a 100 mA (12%) safety margin.

- **Irradiance and STC Power Conversion Formulas**:
  1. **Irradiance Calculation ($W/m^2$)**:
     $$\text{irradiance\_wm2} = \frac{I_{sc\_ma} \times 1000}{600} = \frac{I_{sc\_ma} \times 5}{3}$$
  2. **Nominal Main Array STC Capacity ($W$)**:
     $$\text{estimated\_power\_w} = \frac{I_{sc\_ma} \times 2640\text{ W}}{600\text{ mA}} = I_{sc\_ma} \times 4.4$$

### 1.3 Temperature Effects and High-Precision MPPT Thermal Compensation

- **Why Short-Circuit Current ($I_{sc}$) Measures Pure Irradiance**:
  - The temperature coefficient of short-circuit current ($\alpha_{Isc}$) is negligible ($\approx +0.04\% / ^\circ\text{C}$).
  - Variations in cell temperature (10°C to 60°C) affect $I_{sc}$ by less than 1.5%. Therefore, $I_{sc}$ provides a temperature-immune measurement of pure solar irradiance ($W/m^2$).

- **Why a Panel Temperature Sensor Is Required for Maximum MPPT Precision**:
  - Photovoltaic cell open-circuit voltage ($V_{oc}$) and maximum power ($P_{max}$) have a strong **negative temperature coefficient** ($\gamma_{Pmax} \approx -0.35\% / ^\circ\text{C}$).
  - **Hot Conditions (55°C cell temp)**: The main array's MPPT output drops by $\sim 10.5\%$ ($2362\text{ W}$ instead of $2640\text{ W}$ nominal at 1000 W/m²).
  - **Cold Conditions (15°C cell temp)**: The main array's MPPT output increases by $\sim +3.5\%$ ($2732\text{ W}$ at 1000 W/m²).
  - **Future Hardware Addendum**: Adding a panel temperature sensor (e.g., DS18B20 / NTC attached to the back of the sensor panel) will allow applying the thermal correction factor:
    $$\text{Thermal\_Factor} = 1.0 + (T_{panel} - 25^\circ\text{C}) \times (-0.0035)$$
    $$P_{mppt\_compensated} = \text{estimated\_power\_w} \times \text{Thermal\_Factor}$$

---

## 2. Data Structures and Protocol

### 2.1 Local Persistence (`SolarStats` in `solar_sensor_stats.hpp`)
Maintained in **RTC RAM** (survives *deep sleep*) and synchronized periodically with **NVS**.

| Field                  | Type           | Description                                                 |
| :--------------------- | :------------- | :---------------------------------------------------------- |
| `magic`                | `uint16_t`     | Validation identifier (`0x534F` = "SO")                     |
| `version`              | `uint8_t`      | Persistence structure version                               |
| `gpio_wakeup_enabled`  | `bool`         | External wakeup interrupt enable status                     |
| `is_night_mode`        | `bool`         | Flag indicating whether the node is operating in night mode |
| `last_battery_mv`      | `uint16_t`     | Last measured node battery voltage in mV                    |
| `last_battery_percent` | `uint8_t`      | Last node battery percentage (0-100%)                       |
| `last_battery_state`   | `BatteryState` | Battery classification (NORMAL, LOW, CRITICAL)              |
| `max_current_ma`       | `uint16_t`     | Highest short-circuit current recorded during the day       |
| `min_day_current_ma`   | `uint16_t`     | Lowest current recorded during the daytime period           |
| `daily_yield_mah`      | `uint32_t`     | Accumulated daily current integral in mAh                   |
| `shunt_zero_offset_uv` | `int16_t`      | INA226 zero offset measured during the night                |
| `crc`                  | `uint32_t`     | CRC32 for memory integrity validation                       |

### 2.2 ESP-NOW Telemetry (`SolarSensorReport` in `farm_protocol_types.hpp`)
Packed binary payload (`#pragma pack(push, 1)`), totaling **27 bytes** (ESP-NOW maximum: 230 bytes).

```cpp
struct SolarSensorReport
{
    PowerProfile power_profile;   ///< Node energy mode (ALWAYS_ON, LOW_POWER, DEEP_SLEEP)
    uint16_t isc_current_ma;      ///< Instantaneous short-circuit current in mA (0 - 819 mA)
    uint16_t irradiance_wm2;      ///< Estimated solar irradiance in W/m² (0 - 1200 W/m²)
    int16_t  panel_temp_c;        ///< Sensor panel temperature in 0.1 °C resolution (e.g. 255 = 25.5 °C, INT16_MIN if no sensor)
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
                                           | I2C Master (400kHz via IIna226Driver)
                                           v
               +-------------------------------------------------------+
               |                  ina_sensor_task                      |
               |  * Runs at 4-8 Hz                                     |
               |  * Reads INA226 registers via IIna226Driver           |
               |  * Exponential Moving Average (EMA) Digital Filter     |
               |  * Delta Detection (Delta I > 3% or 10mA)             |
               |  * Direct non-blocking ESP-NOW Telemetry Dispatch      |
               +-------------------------------------------------------+
                                 |                     |
             (Direct ESP-NOW TX) |                     | (Queue: InaSample)
                                 v                     v
+----------------------------------+   +----------------------------------+
|          EspNowManager           |   |            Main Task             |
| * Internal Queue & Worker Task   |   | * System State Machine & Storage |
| * Fire-and-Forget Radio TX       |   | * InaSample Consumer & Stats     |
| * Delta-triggered + 1s Heartbeat |   | * Read Error Counter & Watchdog  |
+----------------------------------+   | * Auto-Reboot on Hardware Error  |
                                       +----------------------------------+
```

1. **`InaSample` Data Contract (`ina_sensor_types.hpp`):**
   ```cpp
   struct InaSample {
       uint16_t  isc_current_ma;    ///< Instantaneous short-circuit current in mA
       uint16_t  bus_voltage_mv;    ///< Measured shunt/bus voltage
       bool      delta_detected;    ///< Flag indicating change exceeding delta threshold
       esp_err_t status;            ///< ESP_OK or I2C/INA226 hardware error status
       int64_t   timestamp_us;      ///< Microsecond timestamp of reading
   };
   ```

2. **`ina_sensor_task` (Frequency: 4 to 8 Hz):**
   - Performs periodic INA226 readings through `IIna226Driver`.
   - Applies Exponential Moving Average (EMA) digital filtering.
   - Detects abrupt irradiance changes ($\Delta I_{sc} > 3\%$ or $> 10\text{mA}$).
   - **Telemetry Dispatch:** On delta detection or 1s heartbeat, directly calls non-blocking `espnow_.send_data()` if reporting is enabled.
   - **Main Task Notification:** Enqueues `InaSample` (containing reading or `status` error) to `ina_sample_queue_`.

3. **`Main Task` (System Coordinator & Health Monitor):**
   - Consumes `InaSample` from `ina_sample_queue_`.
   - **Error Handling & Auto-Reboot:** 
     - Tracks consecutive I2C/hardware errors (`status != ESP_OK`).
     - **Watchdog Timeout:** Waits on `xQueueReceive` with a 2-second timeout. If `ina_sensor_task` freezes or fails 5 consecutive times, sets `stats_.status = SensorStatus::ERROR_HARDWARE` and triggers a clean system restart (`hal_system_.restart()`).
   - Manages state machine (`INIT`, `DAY_ACTIVE`, `DUSK_CHECK`, `NIGHT_SLEEP`, `OTA_MODE`).
   - Toggles `ina_sensor_task.set_reporting_enabled(false)` during `OTA_MODE` or `NIGHT_SLEEP`.

4. **INA226 Power & Hardware Control (High-Side GPIO Power Switching):**
   - **VCC Powering:** INA226 consumes only $\sim 0.33\text{mA}$ during measurement. Powered directly from a high-side ESP32-C3 GPIO pin (`OUTPUT HIGH`).
   - **Hardware Hard Reset:** Driving the VCC GPIO `LOW` for 50ms forces a complete Power-On Reset (POR) on the INA226, clearing stuck I2C bus lines without requiring a full MCU system reboot.
   - **Zero Parasitic Current:** During night `NIGHT_SLEEP`, VCC GPIO is set to `INPUT/LOW` and SDA/SCL lines are disabled, achieving absolute zero ($0.00\mu\text{A}$) parasitic draw.

5. **INA226 Dual-Mode ALERT Pin Strategy:**
   A single physical GPIO pin connected to the INA226 `ALERT` output operates in two distinct modes depending on system state:

   | Phase / State | INA226 ALERT Mode | ESP32-C3 Pin Configuration | Behavior / Function |
   | :--- | :--- | :--- | :--- |
   | **Day (`DAY_ACTIVE`)** | **Conversion Ready (`CVRF`)** | GPIO Interrupt (`gpio_isr_handler_add`) | INA226 triggers LOW on each completed hardware average. ISR issues `vTaskNotifyGiveFromISR`, waking `ina_sensor_task` with 0% idle CPU usage. |
   | **Night (`NIGHT_SLEEP`)** | **Shunt Over Limit (`SOL`)** | Deep Sleep Wakeup (`esp_deep_sleep_enable_gpio_wakeup`) | INA226 triggers LOW when $V_{shunt} > 0.3\text{mV}$ ($\sim 3\text{mA}$). Wakes the ESP32-C3 precisely at dawn. |

6. **ESP-NOW Radio Strategy:**
   - **Non-blocking** send (*fire-and-forget*) queued in the internal `EspNowManager` task (without waiting for a logical ACK, so the real-time flow is not blocked).
   - **Delta + Heartbeat Report:** Sends a packet when there is a significant current change or every 1 second (heartbeat).

---

## 4. Host Testability Strategy (`host_test`) & TDD

The repository adopts **Test-Driven Development (TDD)** in the native x86 test environment (`host_test`) using **GoogleTest** and **GoogleMock**. No logic or driver functionality should be added without a corresponding unit test.

### 4.1 Dependency Injection (DI) and Hardware Abstraction
All interaction with hardware, file systems, time, and the OS is performed through **pure C++ interfaces**:

- **I2C:** [`idf_hals::II2cHAL`](components/idf_hals/include/interfaces/i_hal_i2c.hpp) (Mock: [`MockI2cHAL`](components/idf_hals/mocks/mock_hal_i2c.hpp))
- **INA226 Driver:** `IIna226Driver` (Mock: `MockIna226Driver`)
- **Timing / RTOS:** [`ITimerHAL`](components/idf_hals/include/interfaces/i_hal_timer.hpp), [`IHalFreertos`](components/idf_hals/include/interfaces/i_hal_freertos.hpp)
- **Power / Sleep:** [`ISleepHAL`](components/idf_hals/include/interfaces/i_hal_sleep.hpp), `IBatteryMonitor`
- **Storage:** [`INvsCore`](main/include/interfaces/i_nvs_core.hpp)

### 4.2 Modules and Test Suites in `host_test`

| Test Suite            | Test Target                 | What is tested / asserted                                                                                                                                      |
| :-------------------- | :-------------------------- | :------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `Ina226DriverTest`    | `Ina226Driver`              | Register writes/reads through `MockI2cHAL`, correct $mV \to mA$ conversion calculation, ALERT register configuration, response to I2C failures (NACK/timeout). |
| `SolarMathTest`       | Solar calculation functions | Conversion from $I_{sc} \to \text{Irradiance } (W/m^2)$, 2.64 kWp array power estimation, daily integral in mAh, Exponential Moving Average (EMA) filter.      |
| `SolarSensorTaskTest` | Sampling Task               | Sampling rate, report triggering by $\Delta I$ variation, instantaneous noise handling, shared state update.                                                   |
| `SolarSensorAppTest`  | `SolarSensor` App Class     | State machine transitions (Day $\leftrightarrow$ Night), statistics saving to NVS/RTC, OTA trigger dispatch.                                                   |

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
