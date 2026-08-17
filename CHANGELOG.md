# Changelog

All notable changes to the `smart-farm-solar-sensor` project will be documented in this file.

## [0.1.3] - 2026-08-17

### Changed
- Refactored `SolarSensorNvs` to inherit from the generic `AppStorage<SolarStats, Magic, Version>` CRTP base class in `smart-farm-common`, eliminating local NVS boilerplate and implementation files.
- Decoupled domain struct `SolarStats` from storage metadata (`magic`, `version`, `crc`), wrapping it automatically with the new `StorageEnvelope` pattern.
- Migrated `CoreStorage` usage to pure `CoreData` and separated `process_boot_reasons()` from storage initialization.
- Simplified `init_solar_storage()` and `init_core_storage()` logic utilizing `init_app_data()` / `init()` with automatic fallback to defaults.
- Migrated dedicated NVS unit tests to the generic test suite `test_app_storage` in `smart-farm-common`.
- Bumped firmware version to `0.1.3`.

## [0.1.0] - 2026-08-13

### Added
- Initial firmware release for the Smart Farm Solar Sensor Node based on ESP32-C3.
- High-frequency short-circuit current (Isc) and irradiance measurement via `InaSensorTask` integrating `ina226_driver` with hardware conversion-ready alert synchronization.
- Low-frequency background telemetry worker task (`SlowSensorsTask`) periodically reading battery status (`battery_monitor`) and ambient temperature (`ds18b20_driver`) without blocking the main control loop.
- Thread-safe telemetry snapshot mechanism (`TelemetrySnapshot`) synchronizing sensor data between acquisition tasks and communication loops.
- Real-time ESP-NOW telemetry broadcasting to the central Hub (`farm::SolarSensorReport`).
- Astronomical day/night detection and power management (`DayNightController`) featuring solar declination algorithms, dusk/dawn threshold filtering, and deep sleep scheduling.
- Dual-tier persistence engine (`SolarSensorNvs`) leveraging RTC Fast Memory and Flash NVS with CRC32 integrity checks.
- Reliable day/night power profile handshake: broadcasts final telemetry report with mandatory ESP-NOW ACK before deep sleep and night calibration, allowing Hub to queue downstream commands.
- Post-transmission command draining window (100ms) during night cycles ensuring time synchronization (`SYNC_TIME`) and remote commands are processed and committed prior to deep sleep.
- Dynamic `PowerProfile` synchronization (`ALWAYS_ON` $\leftrightarrow$ `DEEP_SLEEP`) reflected across `CoreStorage` and `TelemetrySnapshot`.
- Remote ESP-NOW command processing (`CommandHandler`) supporting clock synchronization (`SYNC_TIME`), OTA update initiation (`START_OTA`), and remote reboot (`REBOOT`).
- Over-The-Air (OTA) firmware upgrade management and rollback protection (`OtaController`) integrated with `ota_manager`.
- Modular host-based testing architecture with GoogleTest/GoogleMock across 7 dedicated subprojects (66 unit tests).
- Automated GitHub Actions CI pipelines for ESP32-C3 target firmware compilation and host test execution with unified LCOV coverage reporting published to GitHub Pages.
