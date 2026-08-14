#include <cstdint>

#define LOG_LOCAL_LEVEL ESP_LOG_INFO
#include "esp_log.h"

#include "hal_timer.hpp"
#include "hal_nvs.hpp"
#include "hal_sys_rom.hpp"
#include "hal_sys_rom.hpp"
#include "hal_adc_oneshot.hpp"
#include "hal_adc_calibration.hpp"
#include "hal_gpio.hpp"
#include "hal_freertos.hpp"
#include "hal_sleep.hpp"
#include "hal_system.hpp"
#include "hal_system_time.hpp"
#include "hal_sntp.hpp"

#include "solar_sensor.hpp"
#include "farm_protocol_types.hpp"
#include "persistence_backend.hpp"
#include "nvs_core.hpp"
#include "solar_sensor_nvs.hpp"
#include "battery_monitor.hpp"
#include "adc_battery_reader.hpp"
#include "ota_manager.hpp"
#include "ota_controller.hpp"
#include "button_ota_trigger.hpp"
#include "espnow_ota_trigger.hpp"
#include "espnow_manager.hpp"
#include "wifi_manager.hpp"
#include "time_manager.hpp"

#include "hal_i2c.hpp"
#include "ina226_driver.hpp"
#include "ina_sensor_task.hpp"
#include "ds18b20_driver.hpp"
#include "hal_onewire_bus.hpp"
#include "slow_sensors_task.hpp"
#include "telemetry_snapshot.hpp"

#include "secrets.hpp"

#include "freertos/ringbuf.h"
#include "lwip/sockets.h"

static const char* TAG = "main";

static constexpr bool IS_LOGGING = false;

static constexpr const char* CORE_NVS_KEY = "core";
static constexpr const char* STATS_NVS_KEY = "solar_stats";

// HAL instances for sharing across components
static idf_hals::TimerHAL hal_timer;
static idf_hals::NvsHAL nvs_hal;
static idf_hals::SysRomHAL hal_sys_rom;
static idf_hals::HalAdcOneshot hal_oneshot;  // Batery monitor ADC
static idf_hals::HalAdcCalibration hal_cali; // Batery monitor ADC
static idf_hals::GpioHAL hal_gpio;
static idf_hals::I2cHAL hal_i2c;
static idf_hals::HalFreertos hal_freertos;
static idf_hals::SleepHAL hal_sleep;
static idf_hals::SystemHAL hal_system;
static idf_hals::HalSystemTime hal_sys_time;
static idf_hals::HalSntp hal_sntp;
static ds18b20::OnewireBusHAL hal_onewire;

// INA226 Driver
// The daytime regime is the default: build the driver with the day conversion
// settings so init() applies them on every boot (and after INA recovery).
// InaSensorTask::init() arms the conversion-ready alert (CNVR); the night
// regime is applied only by InaSensorTask::prepare_for_sleep().
static constexpr ina226::Ina226Config ina_day_config = {
    .r_shunt_ohms = 0.1f,
    .avg_mode = ina226::AveragingMode::AVG_64,
    .vbus_ct = ina226::ConversionTime::CT_1100US,
    .vsh_ct = ina226::ConversionTime::CT_1100US,
    .mode = ina226::OperatingMode::SHUNT_AND_BUS_CONTINUOUS};

static ina226::Ina226Driver ina_driver{hal_i2c, ina_day_config};

// BatteryMonitor
static battery_monitor::BatteryAdcConfig adc_config = {
    .gpio_num = static_cast<int>(BATTERY_LEVEL_GPIO),
    .sample_count = 16,
    .sample_delay_us = 1000,
    .enable_calibration = true};

static battery_monitor::BatteryMonitorConfig monitor_config = {
    .divider_top_ohms = 240000,
    .divider_bottom_ohms = 240000};

static battery_monitor::AdcBatteryReader adc_reader{hal_oneshot, hal_cali, hal_sys_rom, adc_config};
static battery_monitor::BatteryMonitor bat_monitor{adc_reader, monitor_config};

// DS18B20 1-Wire Driver
static ds18b20::Ds18b20Config ds18b20_config{
    .gpio_num = static_cast<int>(DS18B20_GPIO),
    .max_rx_bytes = 10,
    .enable_pullup = true,
    .initial_resolution = ds18b20::Resolution::BITS_12,
};
static ds18b20::Ds18b20Driver ds18b20_driver{hal_onewire, ds18b20_config};

// Telemetry Snapshot
static TelemetrySnapshot g_telemetry_snapshot;

// SlowSensorsTask (Battery + DS18B20)
static SlowSensorsConfig slow_sensors_config{
    .sample_interval_ms = 60000,
    .task_stack_size = 3072,
    .task_priority = 2,
};
static SlowSensorsTask slow_sensors_task{
    bat_monitor, ds18b20_driver, hal_freertos, g_telemetry_snapshot, slow_sensors_config};

// Persistence and App instantiation
static RTC_DATA_ATTR CoreStorage g_rtc_core;
static RtcBackend rtc_core_backend(&g_rtc_core, sizeof(CoreStorage));
static NvsBackend nvs_core_backend{nvs_hal, CORE_NVS_KEY};
static NvsCore nvs_core{rtc_core_backend, nvs_core_backend};

static RTC_DATA_ATTR SolarStats g_rtc_stats;
static RtcBackend rtc_stats_backend(&g_rtc_stats, sizeof(SolarStats));
static NvsBackend nvs_stats_backend{nvs_hal, STATS_NVS_KEY};
static SolarSensorNvs nvs_solar{rtc_stats_backend, nvs_stats_backend};

// OtaManager — HAL implementations
static HttpClient http_client;
static ManifestParser manifest_parser;
static OtaSession ota_session;
static System ota_system;
static TaskScheduler task_scheduler;
static RollbackManager rollback_manager;
static OtaDependencies ota_deps = {
    .http_client = http_client,
    .manifest_parser = manifest_parser,
    .ota_session = ota_session,
    .system = ota_system,
    .task_scheduler = task_scheduler,
    .rollback_manager = rollback_manager,
};
static OtaManager ota_manager(ota_deps);
static OtaController ota_controller(ota_manager, hal_freertos);

// OTA triggers: boot button + espnow
static ButtonOtaTrigger btn_trigger(hal_gpio, hal_freertos, BOOT_BUTTON_GPIO, 200);
static EspNowOtaTrigger espnow_ota_trigger;

static time_manager::TimeManager time_mgr{hal_sntp, hal_sys_time};

extern "C" void app_main()
{
    ESP_LOGW(TAG, "Initializing Smart Farm Solar Sensor...");

    // Create ESP-NOW receive queue & INA sample queue
    QueueHandle_t rx_queue = hal_freertos.queue_create(30, sizeof(espnow::AppMessage));
    QueueHandle_t ina_sample_queue = hal_freertos.queue_create(10, sizeof(InaSample));

    // Retrieve singleton references for DI
    auto& wifi = wifi_manager::WiFiManager::get_instance();
    auto& espnow = espnow::EspNowManager::instance();

    // Instantiate INA Sensor Task (power control managed by the app, not the task)
    ina::InaSensorTask ina_task{
        ina_driver, espnow, hal_timer, hal_freertos, time_mgr, g_telemetry_snapshot, ina_sample_queue};

    // Instantiate app with dependencies
    SolarSensor solar(
        ina_task,
        ina_sample_queue,
        g_telemetry_snapshot,
        slow_sensors_task,
        nvs_core,
        nvs_solar,
        hal_timer,
        ota_controller,
        btn_trigger,
        espnow_ota_trigger,
        espnow,
        rx_queue,
        wifi,
        hal_sleep,
        hal_system,
        time_mgr,
        hal_freertos,
        hal_gpio,
        hal_i2c);

    // Initialize application state
    solar.init();

    // Run the main application flow in a loop while active
    while (solar.run()) {
        hal_freertos.task_delay(pdMS_TO_TICKS(100));
    }
}
