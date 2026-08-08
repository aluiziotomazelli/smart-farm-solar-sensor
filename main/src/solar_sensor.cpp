// main/src/solar_sensor.cpp
#include "solar_sensor.hpp"

#include "secrets.hpp"

#define LOG_LOCAL_LEVEL ESP_LOG_INFO
#include "esp_log.h"

static const char* TAG = "SolarSensor";

SolarSensor::SolarSensor(
    INvsCore& core_storage,
    idf_hals::ITimerHAL& hal_timer,
    IOtaManager& ota_manager,
    IOtaTrigger& btn_trigger,
    IOtaTrigger& espnow_trigger,
    espnow::IEspNowManager& espnow,
    QueueHandle_t rx_queue_,
    wifi_manager::IWiFiManager& wifi,
    idf_hals::ISleepHAL& hal_sleep_,
    idf_hals::ISystemHAL& hal_system_)
    : core_storage_(core_storage)
    , hal_timer_(hal_timer)
    , ota_manager_(ota_manager)
    , btn_trigger_(btn_trigger)
    , espnow_trigger_(espnow_trigger)
    , espnow_(espnow)
    , rx_queue_(rx_queue_)
    , wifi_(wifi)
    , hal_sleep_(hal_sleep_)
    , hal_system_(hal_system_)
{
}

esp_err_t SolarSensor::init()
{
    esp_err_t err;

    // 1. OTA Manager first to handle OTA updates
    if ((err = init_ota()) != ESP_OK) {
        ESP_LOGW(TAG, "Failed to initialize OTA: %s", esp_err_to_name(err));
        return err;
    }

    if (ota_manager_.check_pending_verify()) {
        pending_firmware_verify_ = true;
    }

    // 2. Wifi for esp-now and OTA
    if ((err = init_wifi()) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize WiFi: %s", esp_err_to_name(err));
        session_healthy_ = false;
    }

    // 3. Initialize storage
    if ((err = init_core_storage()) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize core storage: %s", esp_err_to_name(err));
        session_healthy_ = false;
    }

    // 4. Initialize esp-now
    if ((err = init_espnow()) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize esp-now: %s", esp_err_to_name(err));
        session_healthy_ = false;
    }

    // Roolback if session is not healthy
    if (!session_healthy_) {
        if (pending_firmware_verify_) {
            ESP_LOGE(TAG, "Session not healthy during OTA verification");
            check_firmware();
        }
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "SolarSensor initialized");
    return ESP_OK;
}

esp_err_t SolarSensor::run()
{
    ESP_LOGI(TAG, "SolarSensor running");
    return ESP_OK;
}

void SolarSensor::on_ota_triggered(OtaTriggerSource source)
{
    ESP_LOGI(TAG, "OTA triggered from source: %d", static_cast<int>(source));
    ota_triggered_ = true;
}

// ===================================================================
// Private Methods
// ===================================================================
esp_err_t SolarSensor::init_core_storage()
{
    CoreStorage default_core = {};
    default_core.reset();
    default_core.node_id = farm::NodeId::SOLAR_SENSOR;
    default_core.node_type = farm::NodeType::SENSOR;
    default_core.power_profile = farm::PowerProfile::ALWAYS_ON;

    return core_storage_.init(
        core_, default_core, hal_system_.reset_reason(), hal_sleep_.get_wakeup_cause(), pending_core_commit_);
}

esp_err_t SolarSensor::init_wifi()
{
    esp_err_t err;
    if ((err = wifi_.init()) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize WiFiManager: %s", esp_err_to_name(err));
        return err;
    }
    if ((err = wifi_.add_credentials(WIFI_SSID, WIFI_PASS)) != ESP_OK) {
        ESP_LOGW(TAG, "Failed to set WiFi credentials: %s", esp_err_to_name(err));
    }
    if ((err = wifi_.start()) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start WiFiManager: %s", esp_err_to_name(err));
        return err;
    }

    return ESP_OK;
}

esp_err_t SolarSensor::init_espnow()
{
    espnow::EspNowConfig config;
    config.node_id = static_cast<espnow::NodeId>(farm::NodeId::SOLAR_SENSOR);
    config.node_type = static_cast<espnow::NodeType>(farm::NodeType::SENSOR);
    config.app_rx_queue = rx_queue_;
    config.wifi_channel = 1;
    config.heartbeat_interval_ms = 0; // TODO: verify apropriate value for heartbeat

    esp_err_t err;
    if ((err = espnow_.init(config)) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize EspNowManager: %s", esp_err_to_name(err));
        return err;
    }

    return ESP_OK;
}

esp_err_t SolarSensor::init_ota()
{
    OtaConfig ota_config{
        .device_type = "solar_sensor",
        .manifest_url = SERVER_URL,
        .task_stack_size = 8192,
        .task_priority = 5,
        .transport = {.manifest_timeout_ms = 3000, .firmware_timeout_ms = 30000},
        .security = {.allow_http_during_development = true},
        .allow_same_version = false,
        .restart_on_success = false,
    };

    if (!ota_manager_.init(ota_config)) {
        ESP_LOGE(TAG, "Failed to initialize OTA Manager");
        return ESP_FAIL;
    }

    return ESP_OK;
}

static constexpr uint32_t NVS_PERIODIC_COMMIT_INTERVAL_MS = 15 * 60 * 1000; // 15 minutes
void SolarSensor::save_persistent_state()
{
    int64_t now_ms = hal_timer_.get_time_us() / 1000;

    bool periodic_commit =
        (last_nvs_commit_ts_ > 0) && ((now_ms - last_nvs_commit_ts_) >= NVS_PERIODIC_COMMIT_INTERVAL_MS);

    if (periodic_commit) {
        ESP_LOGI(TAG, "Periodic NVS commit triggered");
    }

    bool force_core = pending_core_commit_ || periodic_commit;
    bool force_node = pending_solar_commit_ || periodic_commit;

    if (core_storage_.save_core(core_, force_core) == ESP_OK) {
        pending_core_commit_ = false;
    }
    else {
        ESP_LOGE(TAG, "Failed to save stats to core storage");
    }

    // if (solar_storage_.save_app_data(stats_, force_node) == ESP_OK) {
    //     pending_solar_commit_ = false;
    // }
    // else {
    //     ESP_LOGE(TAG, "Failed to save stats to solar storage");
    // }

    last_nvs_commit_ts_ = now_ms;
}

void SolarSensor::check_firmware()
{
    if (!pending_firmware_verify_) {
        return;
    }

    if (!session_healthy_ || !ota_manager_.confirm_app_valid()) {
        farm::OtaErrorCode err =
            !session_healthy_ ? farm::OtaErrorCode::HEALTH_CHECK_FAILED : farm::OtaErrorCode::PARTITION_CONFIRM_FAILED;

        ESP_LOGE(TAG, "Failed to confirm firmware. Triggering rollback (reason: %d).", static_cast<int>(err));

        send_ota_report(farm::OtaExecResult::ROLLBACK_TRIGGERED, err);
        wifi_.disconnect(2000);
        espnow_.deinit();
        wifi_.stop();

        ota_manager_.rollback_and_reboot();
        return;
    }

    // If we get here, the firmware is valid and confirme
    pending_firmware_verify_ = false;

    auto version = ota_manager_.get_running_version();
    if (version.has_value()) {
        core_.fw_major = version->major;
        core_.fw_minor = version->minor;
        core_.fw_patch = version->patch;
    }
    ESP_LOGI(TAG, "Firmware confirmed successfully. Versio: %d.%d.%d", core_.fw_major, core_.fw_minor, core_.fw_patch);

    pending_core_commit_ = true; // save new version in storage

    send_ota_report(farm::OtaExecResult::CONFIRMED_SUCCESS, farm::OtaErrorCode::NONE);
}

esp_err_t SolarSensor::send_ota_report(farm::OtaExecResult result, farm::OtaErrorCode error_code)
{
    farm::OtaStatusReport report = {};
    report.power_profile = core_.power_profile;
    report.result = result;
    report.error_code = error_code;

    auto version = ota_manager_.get_running_version();
    if (version.has_value()) {
        report.fw_major = version->major;
        report.fw_minor = version->minor;
        report.fw_patch = version->patch;
    }

    ESP_LOGI(TAG, "Sending OTA status report: result=%u, error_code=%u", result, error_code);
    return espnow_.send_data(
        espnow::ReservedIds::HUB,
        static_cast<uint8_t>(farm::PayloadType::OTA_STATUS_REPORT),
        &report,
        sizeof(report),
        true // require_ack
    );
}