# Smart Farm Solar Sensor Node

[![ESP-IDF Build](https://github.com/aluiziotomazelli/smart-farm-solar-sensor/actions/workflows/build.yml/badge.svg)](https://github.com/aluiziotomazelli/smart-farm-solar-sensor/actions/workflows/build.yml)
[![Host Tests](https://github.com/aluiziotomazelli/smart-farm-solar-sensor/actions/workflows/host_test.yml/badge.svg)](https://github.com/aluiziotomazelli/smart-farm-solar-sensor/actions/workflows/host_test.yml)
[![Coverage](https://img.shields.io/badge/coverage-report-blue)](https://aluiziotomazelli.github.io/smart-farm-solar-sensor/index.html)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)

A modular, low-power IoT peripheral node based on the **Seeed Studio XIAO ESP32-C3**, designed to measure real-time solar irradiance ($W/m^2$), estimate photovoltaic generation capacity for a 2.64 kWp solar array, monitor ambient/panel temperature and battery health, and broadcast telemetry via **ESP-NOW** to the central Smart Farm Hub.

---

## 1. Physical Principle & Measurement Model

- **Sensor Element:** Dedicated **5W** polycrystalline photovoltaic panel installed at the same tilt, azimuth, and environmental conditions as the main array.
- **Quantity Measured:** Short-circuit current ($I_{sc}$). $I_{sc}$ is strictly proportional to solar irradiance ($W/m^2$) and virtually immune to temperature drift ($\approx +0.04\% / ^\circ\text{C}$).
- **Precision Current Sensing:** High-side **INA226** power monitor with a **$0.1\Omega$ (SMD R100)** shunt resistor.
- **Thermal Monitoring & MPPT Compensation:** **DS18B20** 1-Wire temperature sensor attached to the panel to account for the negative temperature coefficient of photovoltaic cells ($\gamma_{Pmax} \approx -0.35\% / ^\circ\text{C}$).
- **Battery Health:** 1S Li-Po / 18650 voltage divider connected to ADC with calibration and threshold classification.

### Scale Matching & Empirical Benchmarks
- Nominal Panel $I_{sc}$ (at STC 1000 W/m²): $600\text{ mA}$
- Midday Empirical Peak: $720\text{ mA}$ ($1200\text{ W/m}^2$)
- Shunt Voltage at Peak: $V_{shunt} = 720\text{ mA} \times 0.1\Omega = 72\text{ mV}$ (88% of INA226 $\pm 81.92\text{ mV}$ full-scale range).

$$\text{Irradiance } (W/m^2) = \frac{I_{sc\_ma} \times 1000}{600} = \frac{I_{sc\_ma} \times 5}{3}$$

$$\text{Estimated STC Array Capacity } (W) = \frac{I_{sc\_ma} \times 2640\text{ W}}{600\text{ mA}} = I_{sc\_ma} \times 4.4$$

---

## 2. Hardware Pinout (Seeed XIAO ESP32-C3)

| Pin | GPIO | Function | Description |
| :--- | :--- | :--- | :--- |
| **D1** | `GPIO 2` | `BATTERY_LEVEL_GPIO` | ADC battery divider input (240k / 240k) |
| **D3** | `GPIO 5` | `INA_VCC_GPIO` | Power gate control for INA226 power line |
| **D2** | `GPIO 3` | `INA_ALERT_GPIO` | Conversion-ready alert & deep sleep dawn wake trigger |
| **D4** | `GPIO 6` | `I2C_SDA_GPIO` | I2C Data line for INA226 |
| **D5** | `GPIO 7` | `I2C_SCL_GPIO` | I2C Clock line for INA226 |
| **D7** | `GPIO 20` | `DS18B20_GPIO` | 1-Wire data bus for DS18B20 digital temperature sensor |
| **D9** | `GPIO 9` | `BOOT_BUTTON_GPIO` | Boot button / manual OTA mode trigger |

---

## 3. Architecture & Task Concurrency

```
                        +---------------------------+
                        |     TelemetrySnapshot     | (Atomic thread-safe cache)
                        +---------------------------+
                               ^              ^
                updates battery|              |updates current & yield
                & temperature  |              |
                    +----------+--+        +--+---------------+
                    |SlowSensorsTask|       | InaSensorTask    |
                    | (Every 60s) |        | (Conversion sync)|
                    +------+------+        +--+---------------+
                           |                  |
                           v                  v
                 [DS18B20 + Battery]     [INA226 I2C]
                                              |
                                              v (ESP-NOW broadcast)
                                     +-----------------+
                                     | Smart Farm Hub  |
                                     +-----------------+
```

- **`InaSensorTask`**: High-frequency sampling task synchronized with INA226 conversion-ready interrupts. Updates exponential moving average (EMA) current and broadcasts `SolarSensorReport` via ESP-NOW.
- **`SlowSensorsTask`**: Low-frequency background task executing non-blocking battery ADC sampling (~16ms) and DS18B20 temperature conversions (~800ms) once per minute without stalling real-time control loops.
- **`DayNightController`**: Astronomical calculation engine determining solar noon, sunrise, sunset, and solar declination to dynamically orchestrate night deep-sleep modes.
- **`SolarSensorNvs`**: Dual-tier storage manager keeping runtime state in RTC Fast RAM across deep sleep cycles and committing to NVS Flash on dusk transitions.
- **`CommandHandler`**: Receives and executes remote commands via ESP-NOW (`SYNC_TIME`, `START_OTA`, `REBOOT`).

---

## 4. Telemetry Format (`SolarSensorReport`)

ESP-NOW payload is packed to **27 bytes** (`#pragma pack(push, 1)`):

```cpp
struct SolarSensorReport
{
    PowerProfile power_profile;   ///< Node energy mode (ALWAYS_ON, LOW_POWER, DEEP_SLEEP)
    uint16_t isc_current_ma;      ///< Instantaneous short-circuit current in mA (0 - 819 mA)
    uint16_t irradiance_wm2;      ///< Estimated solar irradiance in W/m² (0 - 1200 W/m²)
    int16_t  panel_temp_c;        ///< Sensor panel temperature in 0.1 °C (e.g. 255 = 25.5 °C)
    uint16_t battery_mv;          ///< Sensor node battery voltage in mV
    uint8_t  battery_percent;     ///< Sensor node battery percentage level (0-100%)
    BatteryState battery_state;   ///< Sensor node battery state (UNKNOWN, CRITICAL, LOW, NORMAL, FULL)
    SensorStatus status;          ///< Sensor health status (OK, ERROR_HARDWARE, etc.)
    uint16_t max_current_ma;      ///< Daily peak current recorded
    uint32_t daily_yield_mah;     ///< Accumulated daily current integral in mAh
    bool     is_night_mode;       ///< Night mode status flag
    uint64_t unix_time;           ///< UTC Epoch timestamp in milliseconds (0 if unsynced)
};
```

---

## 5. Building and Flashing Firmware

### Prerequisites
- ESP-IDF **v5.1+** (v5.5 recommended)

```bash
# Export ESP-IDF environment
source $HOME/dev/esp/esp-idf/export.sh

# Build target firmware
idf.py set-target esp32c3
idf.py build

# Flash to connected device
idf.py -p /dev/ttyACM0 flash monitor
```

---

## 6. Running Host-Based Tests (Linux)

All components are fully decoupled through abstract interfaces and testable on Linux host using GoogleTest and GoogleMock without physical hardware.

### Running Individual Test Suites
```bash
cd host_test/test_slow_sensors_task
idf.py build
./build/test_slow_sensors_task.elf
```

### Running All Test Suites with CTest
```bash
cd host_test
mkdir -p build && cd build
cmake ..
ctest --output-on-failure
```

### Generating Unified Code Coverage
```bash
cd host_test/build
cmake --build . --target run_all_tests
# HTML report generated in host_test/coverage/index.html
```

---

## 7. License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.

### Third-Party Acknowledgments

This project incorporates or links to the following third-party open-source libraries and drivers:

- **[espressif/onewire_bus](https://github.com/espressif/idf-extra-components/tree/master/onewire_bus)**: Copyright (c) Espressif Systems (Shanghai) CO LTD — Licensed under the [Apache License 2.0](https://www.apache.org/licenses/LICENSE-2.0).
- **[ESP-IDF](https://github.com/espressif/esp-idf)**: Copyright (c) Espressif Systems (Shanghai) CO LTD — Licensed under the [Apache License 2.0](https://www.apache.org/licenses/LICENSE-2.0).
- **[Embedded Template Library (ETL)](https://www.etlcpp.com/)**: Copyright (c) John Wellbelove — Licensed under the [MIT License](https://opensource.org/licenses/MIT).

