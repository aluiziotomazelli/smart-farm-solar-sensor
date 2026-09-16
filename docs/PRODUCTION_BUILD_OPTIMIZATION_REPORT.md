# Production Build Optimization Report via `sdkconfig` (ESP-IDF / ESP32-C3)

---

## Quick Start: How to Build for Production & Development

### 1. Build for Development / Testing (Default Profile)
Running standard `idf.py build` automatically uses `sdkconfig.defaults.dev` (with `INFO` logs, `-Og` debug optimization, and console output enabled):
```bash
source $HOME/dev/esp/esp-idf/export.sh
idf.py build
```

### 2. Build for Production (Field Release)
To build an optimized production binary (silenced `WARN` logs, `-Os` size optimization, silent assertions, no console output):
```bash
source $HOME/dev/esp/esp-idf/export.sh
idf.py -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.defaults.esp32c3;sdkconfig.defaults.prod" reconfigure build
```
Tip: You may need to clean your build directory, sdconfig and dependencies.lock 
```bash
rm -rf build sdkconfig dependencies.lock
```

---

## 1. Executive Summary

The **Smart Farm Solar Sensor** firmware operates on battery power using a *run-to-completion* design pattern paired with *Deep Sleep*.

In a **Production** environment:
- **Shorter CPU active time = Extended battery lifespan.**
- **Smaller binary image = Faster and safer OTA firmware updates.**
- **Log level reduction = Elimination of UART/USB I/O blocking and reduced Flash footprint.**

This document details optimization recommendations and their performance impact when configuring the build system (`sdkconfig.defaults`) for production releases.

---

## 2. Logging and Console (I/O) Optimizations

### A. Application Default Log Level (`CONFIG_LOG_DEFAULT_LEVEL`)
- **Current State**: `CONFIG_LOG_DEFAULT_LEVEL_INFO=y` (`Level 3 - INFO`).
- **Production Recommendation**: `CONFIG_LOG_DEFAULT_LEVEL_WARN=y` (`Level 2 - WARN`) or `CONFIG_LOG_DEFAULT_LEVEL_ERROR=y` (`Level 1 - ERROR`).
- **Impact**:
  1. **Binary Footprint Reduction**: Removes hundreds of `ESP_LOGI` format string constants from Flash memory.
  2. **Execution Speed**: Eliminates CPU cycles spent formatting strings and transmitting via UART/USB during every wake cycle (~5 ms to ~15 ms saved per cycle).

### B. Bootloader Log Level (`CONFIG_BOOTLOADER_LOG_LEVEL`)
- **Current State**: `CONFIG_BOOTLOADER_LOG_LEVEL_INFO=y` (`Level 3`).
- **Production Recommendation**: `CONFIG_BOOTLOADER_LOG_LEVEL_WARN=y` or `CONFIG_BOOTLOADER_LOG_LEVEL_NONE=y`.
- **Impact**: Reduces Bootloader execution time by approximately **10 ms to 20 ms** on every CPU reset (deep sleep wakeup).

### C. Console Output (`CONFIG_ESP_CONSOLE_DEV_NUM` / `CONFIG_ESP_CONSOLE_NONE`)
- **Current State**: `CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y`.
- **Production Recommendation**: If field devices do not require physical USB-Serial console debugging, set `CONFIG_ESP_CONSOLE_NONE=y` or disable log colors (`CONFIG_LOG_COLORS=n`).
- **Impact**: Prevents CPU execution stalls when attempting to transmit over USB-Serial JTAG when no host PC is connected.

---

## 3. Compiler and Binary Size Optimizations

### A. GCC Optimization Level (`CONFIG_COMPILER_OPTIMIZATION`)
- **Current State**: `CONFIG_COMPILER_OPTIMIZATION_DEBUG=y` (`-Og`).
- **Production Recommendation**: `CONFIG_COMPILER_OPTIMIZATION_SIZE=y` (`-Os`).
- **Impact**:
  - Enables aggressive GCC code size optimizations.
  - Reduces the `.bin` firmware image size by **20% to 35%**.
  - Improves ESP32-C3 I-Cache efficiency and reduces OTA download transfer time over Wi-Fi.

### B. Assertion Handling (`CONFIG_COMPILER_OPTIMIZATION_ASSERTIONS`)
- **Current State**: `CONFIG_COMPILER_OPTIMIZATION_ASSERTIONS_ENABLE=y`.
- **Production Recommendation**: `CONFIG_COMPILER_OPTIMIZATION_ASSERTIONS_SILENT=y`.
- **Impact**: Preserves system safety (resets the CPU on assertion failure) while **stripping text filenames and line numbers**, saving Flash memory without compromising runtime safety.

---

## 4. PHY Radio and Power Optimizations

### A. Wi-Fi / ESP-NOW Maximum TX Power (`CONFIG_ESP_PHY_MAX_TX_POWER`)
- **Production Recommendation**: Consider lowering maximum PHY radio transmit power from default 20 dBm to ~15–17 dBm (e.g. `CONFIG_ESP_PHY_MAX_TX_POWER=72`).
- **Impact**:
  - Peak radio transmission current drops from **~340 mA to ~190 mA**.
  - Significantly extends battery health and capacity for nodes located within close/medium range of the Hub.

### B. Serial Flash Access Mode (`CONFIG_ESPTOOLPY_FLASHMODE`)
- **Production Recommendation**: If the target hardware board supports 4 data lines (Quad SPI), configure `CONFIG_ESPTOOLPY_FLASHMODE_QIO=y` at 80 MHz instead of Dual I/O (`DIO`).
- **Impact**: Doubles Flash fetch throughput during code execution, reducing total active uptime prior to entering Deep Sleep.

---

## 5. Security and OTA Production Settings

### A. Disable Insecure HTTP Support
- **Current State**: `CONFIG_ESP_HTTPS_OTA_ALLOW_HTTP=y`.
- **Production Recommendation**: Set `CONFIG_ESP_HTTPS_OTA_ALLOW_HTTP=n` (enforce HTTPS once migrating to production cloud/server infrastructure).

---

## 6. Recommended `sdkconfig.defaults.production` Profile

```ini
# =============================================================================
# Production Build Defaults - Smart Farm Solar Sensor
# =============================================================================

# 1. Compiler Optimization for Size (-Os)
CONFIG_COMPILER_OPTIMIZATION_SIZE=y
# CONFIG_COMPILER_OPTIMIZATION_DEBUG is not set
CONFIG_COMPILER_OPTIMIZATION_ASSERTIONS_SILENT=y

# 2. Logging Optimization (WARN Level & Silence Bootloader)
CONFIG_LOG_DEFAULT_LEVEL_WARN=y
# CONFIG_LOG_DEFAULT_LEVEL_INFO is not set
CONFIG_LOG_DEFAULT_LEVEL=2
CONFIG_BOOTLOADER_LOG_LEVEL_WARN=y
# CONFIG_BOOTLOADER_LOG_LEVEL_INFO is not set
CONFIG_BOOTLOADER_LOG_LEVEL=2
CONFIG_LOG_COLORS=n

# 3. System Task & Rollback Settings
CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y
CONFIG_PARTITION_TABLE_CUSTOM=y
CONFIG_ESP_MAIN_TASK_STACK_SIZE=8192

# 4. Target & Flash
CONFIG_IDF_TARGET="esp32c3"
CONFIG_FREERTOS_UNICORE=y
CONFIG_PM_ENABLE=y
```
