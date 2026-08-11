// main/src/solar_sensor.cpp
#include "solar_sensor.hpp"

#include "secrets.hpp"
#include "ina_sensor_types.hpp"

#define LOG_LOCAL_LEVEL ESP_LOG_INFO
#include "esp_log.h"
#include "esp_intr_alloc.h"

static const char* TAG = "SolarSensor";

static constexpr uint16_t DEEP_SLEEP_TIME_MIN = 60;

// ============== INA226 Config ==============
// The daytime regime is the default and lives in the Ina226Driver construction
// (main.cpp); InaSensorTask::init() arms the conversion-ready alert (CNVR).
// Only the night (sleep) conversion settings are overridden here.
static constexpr InaSensorConfig ina_config = {
    .delta_threshold_ma = 10,
    .delta_threshold_percent = 0.03f,
    .heartbeat_interval_ms = 1000,
    .night_config =
        {
            .vsh_ct = ina226::ConversionTime::CT_8244US,
            .vbus_ct = ina226::ConversionTime::CT_8244US,
            .avg_mode = ina226::AveragingMode::AVG_64,
        },
};

SolarSensor::SolarSensor(
    ina::IInaSensorTask& ina_sensor_task,
    QueueHandle_t ina_sample_queue,
    INvsCore& core_storage,
    ISolarSensorNvs& solar_storage,
    idf_hals::ITimerHAL& hal_timer,
    IOtaManager& ota_manager,
    IOtaTrigger& btn_trigger,
    IOtaTrigger& espnow_trigger,
    espnow::IEspNowManager& espnow,
    QueueHandle_t rx_queue_,
    wifi_manager::IWiFiManager& wifi,
    idf_hals::ISleepHAL& hal_sleep,
    idf_hals::ISystemHAL& hal_system,
    time_manager::ITimeManager& time_manager,
    idf_hals::IHalFreertos& hal_freertos,
    idf_hals::IGpioHAL& hal_gpio,
    idf_hals::II2cHAL& hal_i2c)
    : ina_sensor_task_(ina_sensor_task)
    , ina_sample_queue_(ina_sample_queue)
    , core_storage_(core_storage)
    , solar_storage_(solar_storage)
    , hal_timer_(hal_timer)
    , ota_manager_(ota_manager)
    , btn_trigger_(btn_trigger)
    , espnow_trigger_(espnow_trigger)
    , espnow_(espnow)
    , rx_queue_(rx_queue_)
    , wifi_(wifi)
    , hal_sleep_(hal_sleep)
    , hal_system_(hal_system)
    , time_manager_(time_manager)
    , hal_rtos_(hal_freertos)
    , hal_gpio_(hal_gpio)
    , hal_i2c_(hal_i2c)
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

    if ((err = init_time()) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize TimeManager: %s", esp_err_to_name(err));
        session_healthy_ = false;
    }

    // 3. Initialize storage
    if ((err = init_core_storage()) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize core storage: %s", esp_err_to_name(err));
        session_healthy_ = false;
    }
    if ((err = init_solar_storage()) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize solar storage: %s", esp_err_to_name(err));
        session_healthy_ = false;
    }

    // 4. Initialize esp-now
    if ((err = init_espnow()) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize esp-now: %s", esp_err_to_name(err));
        session_healthy_ = false;
    }

    // 5. Initialize INA VCC pin
    if ((err = init_ina_vcc_pin()) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize INA VCC pin: %s", esp_err_to_name(err));
        session_healthy_ = false;
    }

    // 6. Initialize INA task
    if ((err = init_ina_task(ina_config)) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize INA Sensor Task: %s", esp_err_to_name(err));
        session_healthy_ = false;
    }

    // 7. Attach the INA ALERT GPIO ISR: the task wakes on every completed
    // conversion (CNVR armed by InaSensorTask::init()). Must run after start()
    // so the ISR receives a valid task handle.
    if ((err = init_ina_alert_pin()) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize INA alert pin: %s", esp_err_to_name(err));
        session_healthy_ = false;
    }

    // Rollback if session is not healthy
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

esp_err_t SolarSensor::init_time()
{
    time_manager::TimeManagerConfig time_config;
    time_config.use_dhcp_sntp = false;
    time_config.timezone = "<-04>4";

    esp_err_t err = time_manager_.init(time_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize TimeManager: %s", esp_err_to_name(err));
        return err;
    }

    return ESP_OK;
}

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

esp_err_t SolarSensor::init_solar_storage()
{
    esp_err_t ret = solar_storage_.load_app_data(stats_);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Loaded solar stats from storage");
        return ESP_OK;
    }

    ESP_LOGW(TAG, "Solar storage load failed (%s), recreating default storage", esp_err_to_name(ret));
    stats_.reset();
    ret = solar_storage_.save_app_data(stats_, /*force_nvs_commit=*/true);
    if (ret == ESP_OK) {
        return ESP_OK;
    }

    ESP_LOGE(TAG, "Failed to initialize solar storage: %s", esp_err_to_name(ret));
    return ret;
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

    if (solar_storage_.save_app_data(stats_, force_node) == ESP_OK) {
        pending_solar_commit_ = false;
    }
    else {
        ESP_LOGE(TAG, "Failed to save stats to solar storage");
    }

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

esp_err_t SolarSensor::connect_wifi_with_retry(uint8_t max_attempts)
{
    if (wifi_.get_state() == wifi_manager::State::CONNECTED_GOT_IP) {
        return ESP_OK;
    }

    static constexpr uint16_t DELAY_BETWEEN_ATTEMPTS_MS = 1500;
    esp_err_t err = ESP_FAIL;
    for (uint8_t attempt = 1; attempt <= max_attempts; ++attempt) {
        ESP_LOGI(TAG, "Connecting to WiFi (attempt %u/%u)...", attempt, max_attempts);
        err = wifi_.connect(10000);
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "WiFi connected successfully");
            return ESP_OK;
        }

        ESP_LOGW(TAG, "WiFi connection attempt %u failed: %s", attempt, esp_err_to_name(err));
        if (attempt < max_attempts) {
            wifi_.disconnect(2000);
            uint32_t delay_ms = DELAY_BETWEEN_ATTEMPTS_MS * attempt;
            hal_rtos_.task_delay(pdMS_TO_TICKS(delay_ms));
        }
    }

    ESP_LOGE(TAG, "Failed to connect to WiFi after %u attempts: %s", max_attempts, esp_err_to_name(err));
    return err;
}

esp_err_t SolarSensor::init_ina_task(InaSensorConfig config)
{
    i2c_bus_handle_ = nullptr;
    esp_err_t err = init_i2c_master_bus(i2c_bus_handle_);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create I2C master bus: %s", esp_err_to_name(err));
        return err;
    }

    err = ina_sensor_task_.init(config, i2c_bus_handle_);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize INA Sensor Task config: %s", esp_err_to_name(err));
        return err;
    }

    return ina_sensor_task_.start();
}

void SolarSensor::process_ina_samples()
{
    if (ina_sample_queue_ == nullptr) {
        return;
    }

    // Sampling enabled == active regime: the app only expects (and watches)
    // periodic samples while sampling is on. prepare_for_sleep() disables it.
    bool sampling_active = ina_sensor_task_.is_sampling_enabled();
    uint32_t timeout_ms = sampling_active ? ina_sensor_task_.get_watchdog_timeout_ms() : 0;

    InaSample sample{};
    if (hal_rtos_.queue_receive(ina_sample_queue_, &sample, pdMS_TO_TICKS(timeout_ms)) == pdTRUE) {
        if (sample.status != ESP_OK) {
            consecutive_ina_errors_++;
            ESP_LOGW(
                TAG,
                "Received INA sample error status: %s (consecutive=%u)",
                esp_err_to_name(sample.status),
                consecutive_ina_errors_);

            if (consecutive_ina_errors_ >= 5) {
                ESP_LOGE(TAG, "5 consecutive INA errors! Resetting system...");
                if (recover_ina_hardware() == ESP_OK) {
                    ESP_LOGI(TAG, "Successfully recovered INA hardware");
                    consecutive_ina_errors_ = 0;
                    return;
                }
                else {
                    hal_system_.restart();
                }
            }
            return;
        }

        consecutive_ina_errors_ = 0;
        if (sample.isc_current_ma > stats_.max_current_ma) {
            stats_.max_current_ma = sample.isc_current_ma;
            pending_solar_commit_ = true;
        }
    }
    else if (sampling_active) {
        ESP_LOGE(TAG, "InaSensorTask watchdog timeout (>%ums without sample)! Resetting system...", timeout_ms);
        hal_system_.restart();
    }
}

esp_err_t SolarSensor::recover_ina_hardware()
{
    ina_sensor_task_.stop();

    if (i2c_bus_handle_ != nullptr) {
        hal_i2c_.del_master_bus(i2c_bus_handle_);
    }

    // SDA/SCL pins on 0V to avoid parasitic power
    hal_gpio_.set_direction(I2C_SDA_GPIO, GPIO_MODE_OUTPUT);
    hal_gpio_.set_level(I2C_SDA_GPIO, 0);
    hal_gpio_.set_direction(I2C_SCL_GPIO, GPIO_MODE_OUTPUT);
    hal_gpio_.set_level(I2C_SCL_GPIO, 0);

    // Restart INA VCC
    hal_gpio_.set_level(INA_VCC_GPIO, 0);
    hal_rtos_.task_delay(pdMS_TO_TICKS(100));
    hal_gpio_.set_level(INA_VCC_GPIO, 1);
    hal_rtos_.task_delay(pdMS_TO_TICKS(10));

    esp_err_t err = init_ina_task(ina_config);
    if (err != ESP_OK) {
        return err;
    }

    // The task was recreated with a new handle: re-attach the ALERT ISR so it
    // notifies the new task (init_ina_alert_pin() drops any stale registration).
    return init_ina_alert_pin();
}

esp_err_t SolarSensor::init_i2c_master_bus(i2c_master_bus_handle_t& i2c_bus_handle)
{
    i2c_master_bus_config_t bus_cfg = {};
    bus_cfg.i2c_port = I2C_NUM_0;
    bus_cfg.sda_io_num = I2C_SDA_GPIO;
    bus_cfg.scl_io_num = I2C_SCL_GPIO;
    bus_cfg.clk_source = I2C_CLK_SRC_DEFAULT;
    bus_cfg.glitch_ignore_cnt = 7;
    bus_cfg.flags.enable_internal_pullup = true;

    return hal_i2c_.new_master_bus(&bus_cfg, &i2c_bus_handle);
}

esp_err_t SolarSensor::init_ina_vcc_pin()
{
    constexpr gpio_config_t vcc_gpio_cfg = {
        .pin_bit_mask = (1ULL << INA_VCC_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_ENABLE, // Pull-down on reset/boot (not floating)
        .intr_type = GPIO_INTR_DISABLE,
    };

    esp_err_t err;
    if ((err = hal_gpio_.config(&vcc_gpio_cfg)) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize GPIO: %s", esp_err_to_name(err));
        return err;
    }
    if ((err = hal_gpio_.set_drive_capability(INA_VCC_GPIO, GPIO_DRIVE_CAP_3)) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set INA VCC GPIO drive capability: %s", esp_err_to_name(err));
        return err;
    }
    if ((err = hal_gpio_.set_level(INA_VCC_GPIO, 1)) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set INA VCC GPIO level: %s", esp_err_to_name(err));
        return err;
    }
    // Disable hold after deep sleep
    if ((err = hal_gpio_.hold_dis(INA_VCC_GPIO)) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to disable INA VCC GPIO hold: %s", esp_err_to_name(err));
        return err;
    }

    return ESP_OK;
}

void SolarSensor::enter_deep_sleep()
{
    // Arm the night regime before sleeping. Without it the conversion-ready
    // alert (CNVR) stays armed, so the INA asserts the ALERT pin on every
    // conversion and the MCU would wake in a tight loop. On failure, abort
    // the sleep instead — the app watchdog handles the error.
    esp_err_t err = ina_sensor_task_.prepare_for_sleep();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to prepare INA for sleep (%s), aborting deep sleep", esp_err_to_name(err));
        return;
    }

    ESP_LOGI(TAG, "Entering deep sleep...");

    constexpr uint64_t sleep_time = DEEP_SLEEP_TIME_MIN * 60ULL * 1000000ULL;
    hal_sleep_.enable_timer_wakeup(sleep_time);

    // GPIO wakeup on ALERT going LOW: the INA (SHUNT_OVER_VOLTAGE alert armed
    // by prepare_for_sleep()) asserts the pin at dawn. The timer is the
    // fallback if the GPIO wakeup cannot be armed.
    err = hal_sleep_.deep_sleep_enable_gpio_wakeup(1ULL << INA_ALERT_GPIO, idf_hals::GpioWakeupMode::LOW_LEVEL);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to enable GPIO wakeup (%s), timer fallback only", esp_err_to_name(err));
    }

    hal_sleep_.deep_sleep_start();
}

esp_err_t SolarSensor::init_ina_alert_pin()
{
    gpio_config_t alert_cfg = {
        .pin_bit_mask = (1ULL << INA_ALERT_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_NEGEDGE,
    };

    esp_err_t err = hal_gpio_.config(&alert_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize INA alert GPIO: %s", esp_err_to_name(err));
        return err;
    }

    // Allocate the ISR from IRAM so it can fire while the flash cache is
    // disabled (e.g. during NVS commits) — the handler itself is IRAM_ATTR.
    err = hal_gpio_.install_isr_service(ESP_INTR_FLAG_IRAM);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "Failed to install INA alert GPIO ISR service: %s", esp_err_to_name(err));
        return err;
    }

    // Drop any previous registration first: after a recovery the task is
    // recreated with a new handle, so the ISR must be re-attached with the
    // fresh handle instead of notifying a deleted task.
    err = hal_gpio_.isr_handler_remove(INA_ALERT_GPIO);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "Failed to remove stale INA alert GPIO ISR: %s", esp_err_to_name(err));
        return err;
    }

    err = hal_gpio_.isr_handler_add(INA_ALERT_GPIO, ina_alert_isr_handler, ina_sensor_task_.get_task_handle());
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to attach INA alert GPIO ISR handler: %s", esp_err_to_name(err));
        return err;
    }

    return ESP_OK;
}

/**
 * @brief Alert interrupt service routine
 *
 * @param arg Task handle to be notified
 *
 * @note This ISR runs in IRAM for fast execution and notifies the INA sensor
 * task on every completed conversion (CNVR). It uses DRAM logging to avoid
 * flash access during interrupt handling.
 */
void IRAM_ATTR SolarSensor::ina_alert_isr_handler(void* arg)
{
    TaskHandle_t task_handle = static_cast<TaskHandle_t>(arg);
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;

    if (task_handle != nullptr) {
        vTaskNotifyGiveFromISR(task_handle, &xHigherPriorityTaskWoken);
    }

    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}
