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

#include "solar_sensor.hpp"
#include "farm_protocol_types.hpp"
#include "persistence_backend.hpp"
#include "nvs_core.hpp"
#include "battery_monitor.hpp"
#include "adc_battery_reader.hpp"
#include "ota_manager.hpp"
#include "button_ota_trigger.hpp"
#include "espnow_ota_trigger.hpp"
#include "espnow_manager.hpp"
#include "wifi_manager.hpp"

#include "secrets.hpp"

#include "freertos/ringbuf.h"
#include "lwip/sockets.h"

static const char* TAG = "main";

static constexpr bool IS_LOGGING = false;

// Production Configuration for XIAO-ESP32-C3 Mini Board
static constexpr gpio_num_t BATTERY_LEVEL_GPIO = GPIO_NUM_3; // D1
static constexpr gpio_num_t BOOT_BUTTON_GPIO = GPIO_NUM_9;   // Boot button has no external pad

static constexpr const char* CORE_NVS_KEY = "core";
static constexpr const char* STATS_NVS_KEY = "solar_stats";

// HAL instances for sharing across components
static idf_hals::TimerHAL hal_timer;
static idf_hals::NvsHAL nvs_hal;
static idf_hals::SysRomHAL hal_sys_rom;
static idf_hals::HalAdcOneshot oneshot_hal;  // Batery monitor ADC
static idf_hals::HalAdcCalibration cali_hal; // Batery monitor ADC
static idf_hals::GpioHAL hal_gpio;
static idf_hals::HalFreertos hal_freertos;
static idf_hals::SleepHAL hal_sleep;

// BatteryMonitor
static battery_monitor::BatteryAdcConfig adc_config = {
    .gpio_num = static_cast<int>(BATTERY_LEVEL_GPIO),
    .sample_count = 16,
    .sample_delay_us = 1000,
    .enable_calibration = true};

static battery_monitor::BatteryMonitorConfig monitor_config = {
    .divider_top_ohms = 240000,
    .divider_bottom_ohms = 240000};

static battery_monitor::AdcBatteryReader adc_reader{oneshot_hal, cali_hal, hal_sys_rom, adc_config};
static battery_monitor::BatteryMonitor bat_monitor{adc_reader, monitor_config};

// Persistence and App instantiation
static RTC_DATA_ATTR CoreStorage g_rtc_core;
static RtcBackend rtc_core_backend(&g_rtc_core, sizeof(CoreStorage));
static NvsBackend nvs_core_backend{nvs_hal, CORE_NVS_KEY};
static NvsCore nvs_core{rtc_core_backend, nvs_core_backend};

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

static OtaConfig ota_config{
    .device_type = "water_tank",
    .manifest_url = SERVER_URL,
    .task_stack_size = 8192,
    .task_priority = 5,
    .transport = {.manifest_timeout_ms = 30000, .firmware_timeout_ms = 30000},
    .security = {.allow_http_during_development = true},
    .allow_same_version = false,
    .restart_on_success = false,
};
static OtaManager ota_manager(ota_deps);

// OTA triggers: boot button + espnow
static ButtonOtaTrigger btn_trigger(hal_gpio, hal_freertos, BOOT_BUTTON_GPIO, 200);
static EspNowOtaTrigger espnow_ota_trigger;

extern "C" void app_main()
{
    ESP_LOGW(TAG, "Initializing Smart Farm Solar Sensor...");
    // hal_freertos.task_delay(pdMS_TO_TICKS(3000));

    // Create ESP-NOW receive queue
    QueueHandle_t rx_queue = hal_freertos.queue_create(30, sizeof(espnow::AppMessage));

    // Retrieve singleton references for DI
    auto& wifi = wifi_manager::WiFiManager::get_instance();
    auto& espnow = espnow::EspNowManager::instance();

    // Instantiate app with dependencies
    SolarSensor solar(nvs_core, hal_timer, ota_manager, btn_trigger, espnow_ota_trigger, espnow, rx_queue, wifi);

    // Initialize application state (enable remote logging for field tests)

    solar.init();

    // Run the main application flow in a loop if not sleeping
    solar.run();
}
