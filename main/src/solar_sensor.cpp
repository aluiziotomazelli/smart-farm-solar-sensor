#define LOG_LOCAL_LEVEL ESP_LOG_INFO
#include "esp_log.h"

#include "solar_sensor.hpp"

#include <algorithm>
#include <cmath>

#include "esp_intr_alloc.h"

#include "secrets.hpp"
#include "ina_sensor_types.hpp"

static const char* TAG = "SolarSensor";

static constexpr uint16_t DEEP_SLEEP_TIME_MIN = 60;
static constexpr uint32_t IDLE_RECONNECT_INTERVAL_MS = 5 * 60 * 1000; // 5 minutes

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
            .avg_mode = ina226::AveragingMode::AVG_1024,
        },
};

SolarSensor::SolarSensor(
    ina::IInaSensorTask& ina_sensor_task,
    QueueHandle_t ina_sample_queue,
    TelemetrySnapshot& telemetry_snapshot,
    ISlowSensorsTask& slow_sensors_task,
    INvsCore& core_storage,
    ISolarSensorNvs& solar_storage,
    idf_hals::ITimerHAL& hal_timer,
    OtaController& ota_controller,
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
    idf_hals::II2cHAL& hal_i2c,
    ILedController& led)
    : ina_sensor_task_(ina_sensor_task)
    , ina_sample_queue_(ina_sample_queue)
    , telemetry_snapshot_(telemetry_snapshot)
    , slow_sensors_task_(slow_sensors_task)
    , core_storage_(core_storage)
    , solar_storage_(solar_storage)
    , hal_timer_(hal_timer)
    , ota_controller_(ota_controller)
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
    , led_(led)
    , command_handler_(rx_queue_, espnow_, time_manager_, core_, hal_rtos_)
{
}

esp_err_t SolarSensor::init()
{
    esp_err_t err;

    // 0. Initialize and start Status LED Controller
    if ((err = led_.init()) != ESP_OK) {
        ESP_LOGW(TAG, "Failed to initialize LedController: %s", esp_err_to_name(err));
    }
    led_.start();

    // Arm OTA triggers
    btn_trigger_.arm(*this);
    espnow_trigger_.arm(*this);

    // 1. Initialize OtaController
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

    if (!ota_controller_.init(ota_config)) {
        ESP_LOGW(TAG, "Failed to initialize OtaController config");
        led_.set_pattern(BlinkPattern::ERROR_BURST);
        return ESP_FAIL;
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

    // 5. Initialize and start SlowSensorsTask (Battery + DS18B20)
    if ((err = slow_sensors_task_.init()) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize SlowSensorsTask: %s", esp_err_to_name(err));
        session_healthy_ = false;
    }
    if ((err = slow_sensors_task_.start()) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start SlowSensorsTask: %s", esp_err_to_name(err));
        session_healthy_ = false;
    }

    // 6. Initialize esp-now
    if ((err = init_espnow()) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize esp-now: %s", esp_err_to_name(err));
        session_healthy_ = false;
    }

    // 7. Initialize INA VCC pin
    if ((err = init_ina_vcc_pin()) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize INA VCC pin: %s", esp_err_to_name(err));
        session_healthy_ = false;
    }

    // 8. Initialize INA task
    if ((err = init_ina_task(ina_config)) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize INA Sensor Task: %s", esp_err_to_name(err));
        session_healthy_ = false;
    }

    // 9. Attach the INA ALERT GPIO ISR
    if ((err = init_ina_alert_pin()) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize INA alert pin: %s", esp_err_to_name(err));
        session_healthy_ = false;
    }

    // 10. Perform post-boot firmware verification
    if (!is_firmware_healthy(session_healthy_)) {
        session_healthy_ = false;
    }

    if (!session_healthy_) {
        ESP_LOGE(TAG, "SolarSensor session not healthy during boot initialization");
        led_.set_pattern(BlinkPattern::ERROR_BURST);
        return ESP_FAIL;
    }

    led_.set_pattern(BlinkPattern::BOOT_SUCCESS);
    ESP_LOGI(TAG, "SolarSensor initialized");
    return ESP_OK;
}

bool SolarSensor::run()
{
    if (!wake_classified_) {
        wake_classified_ = true;
        WakeType wake = evaluate_boot_mode();

        if (wake == WakeType::CALIBRATION_TIMER) {
            process_night_calibration();
            return false;
        }
        if (wake == WakeType::SPURIOUS_TIMER) {
            process_spurious_wake();
            return false;
        }

        on_dawn_start();
    }

    return run_day_cycle();
}

bool SolarSensor::run_day_cycle()
{
    ESP_LOGD(TAG, "SolarSensor running day cycle");

    // 1. Check ESP-NOW connection and auto-reconnect if stuck in IDLE
    check_espnow_connection();

    // 2. Process pending ESP-NOW commands
    CommandProcessResult cmd_res = command_handler_.process();
    handle_command_process_result(cmd_res);

    // 3. Process pending OTA triggers
    if (ota_triggered_) {
        process_pending_ota();
    }

    // 4. Process INA samples and check dusk condition
    if (process_ina_samples(get_synced_time())) {
        return false; // Entered deep sleep
    }

    // 5. Routine persistent storage save
    save_persistent_state();

    return true;
}

void SolarSensor::on_ota_triggered(OtaTriggerSource source)
{
    ESP_LOGI(TAG, "OTA triggered from source: %d", static_cast<int>(source));
    ota_triggered_ = true;
}

// ===================================================================
// Private Methods
// ===================================================================

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
    CoreData default_core = {};
    default_core.reset();
    default_core.node_id = farm::NodeId::SOLAR_SENSOR;
    default_core.node_type = farm::NodeType::SENSOR;
    default_core.power_profile = farm::PowerProfile::ALWAYS_ON;

    esp_err_t ret = core_storage_.init(core_, default_core);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize core storage: %s", esp_err_to_name(ret));
        return ret;
    }

    core_storage_.process_boot_reasons(
        core_, hal_system_.reset_reason(), hal_sleep_.get_wakeup_cause(), pending_core_commit_);

    return ESP_OK;
}

esp_err_t SolarSensor::init_solar_storage()
{
    SolarStats default_stats = {};
    default_stats.reset();

    esp_err_t ret = solar_storage_.init_app_data(stats_, default_stats);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize solar storage: %s", esp_err_to_name(ret));
        return ret;
    }

    yield_umah_accumulator_ = static_cast<uint64_t>(stats_.daily_yield_mah) * 1000ULL;
    telemetry_snapshot_.update_stats(stats_.max_day_current_ma, stats_.daily_yield_mah);
    telemetry_snapshot_.set_night_mode(stats_.is_night_mode);
    telemetry_snapshot_.update_battery(stats_.last_battery_mv, stats_.last_battery_percent, stats_.last_battery_state);
    ina_sensor_task_.set_shunt_zero_offset_uv(stats_.shunt_zero_offset_uv);

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

static constexpr uint32_t NVS_PERIODIC_COMMIT_INTERVAL_MS = 15 * 60 * 1000; // 15 minutes
void SolarSensor::save_persistent_state()
{
    auto snap = telemetry_snapshot_.get();
    stats_.last_battery_mv = snap.battery_mv;
    stats_.last_battery_percent = snap.battery_percent;
    stats_.last_battery_state = snap.battery_state;

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

void SolarSensor::process_pending_ota()
{
    ota_triggered_ = false;

    ESP_LOGI(TAG, "Processing pending OTA update...");
    led_.set_pattern(BlinkPattern::OTA_UPDATING);

    btn_trigger_.disarm();
    espnow_trigger_.disarm();
    bool was_ina_sampling = ina_sensor_task_.is_sampling_enabled();
    bool was_ina_reporting = ina_sensor_task_.is_reporting_enabled();
    ina_sensor_task_.stop();

    bool previous_connected = (wifi_.get_state() == wifi_manager::State::CONNECTED_GOT_IP);
    bool wifi_ok = previous_connected;

    if (!previous_connected) {
        espnow_.set_channel_policy(espnow::ChannelPolicy::FIXED);
        wifi_ok = (connect_wifi_with_retry(3) == ESP_OK);
    }

    if (wifi_ok) {
        OtaVerifyResult dl = ota_controller_.execute_download();
        if (dl.success) {
            ESP_LOGI(TAG, "OTA download succeeded! Saving state and restarting...");
            pending_core_commit_ = true;
            pending_solar_commit_ = true;
            save_persistent_state();
            wifi_.disconnect(2000);
            wifi_.stop(2000);
            hal_system_.restart();
            return;
        }
        else {
            ESP_LOGE(TAG, "OTA download failed (error_code: %d)", static_cast<int>(dl.error_code));
            led_.set_pattern(BlinkPattern::ERROR_BURST);
            if (dl.version.has_value()) {
                core_.fw_major = dl.version->major;
                core_.fw_minor = dl.version->minor;
                core_.fw_patch = dl.version->patch;
            }
            send_ota_report(dl.exec_result, dl.error_code);
        }
    }
    else {
        ESP_LOGE(TAG, "Failed to connect to WiFi");
        led_.set_pattern(BlinkPattern::ERROR_BURST);
        send_ota_report(farm::OtaExecResult::DOWNLOAD_FAILED, farm::OtaErrorCode::WIFI_CONNECT_FAILED);
    }

    // Restore components if update did not result in reboot
    if (!previous_connected) {
        wifi_.disconnect(2000);
        espnow_.set_channel_policy(espnow::ChannelPolicy::SCAN);
    }
    btn_trigger_.arm(*this);
    espnow_trigger_.arm(*this);
    if (was_ina_sampling) {
        ina_sensor_task_.set_sampling_enabled(true);
    }
    if (was_ina_reporting) {
        ina_sensor_task_.set_reporting_enabled(true);
    }
}

esp_err_t SolarSensor::send_ota_report(farm::OtaExecResult result, farm::OtaErrorCode error_code)
{
    farm::OtaStatusReport report = {};
    report.power_profile = core_.power_profile;
    report.result = result;
    report.error_code = error_code;
    report.fw_major = core_.fw_major;
    report.fw_minor = core_.fw_minor;
    report.fw_patch = core_.fw_patch;

    ESP_LOGI(
        TAG,
        "Sending OTA status report: result=%u, error_code=%u",
        static_cast<uint8_t>(result),
        static_cast<uint8_t>(error_code));
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

std::optional<time_t> SolarSensor::get_synced_time() const
{
    if (!time_manager_.is_synchronized()) {
        return std::nullopt;
    }
    return static_cast<time_t>(time_manager_.get_timestamp_sec());
}

bool SolarSensor::process_ina_samples(std::optional<time_t> unix_time)
{
    if (ina_sample_queue_ == nullptr || !ina_sensor_task_.is_sampling_enabled()) {
        return false;
    }

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
                    return false;
                }
                else {
                    hal_system_.restart();
                }
            }
            return false;
        }

        consecutive_ina_errors_ = 0;

        update_current_stats(sample);

        telemetry_snapshot_.update_stats(stats_.max_day_current_ma, stats_.daily_yield_mah);

        if (day_night_controller_.should_enter_night_mode(sample.isc_current_ma, unix_time)) {
            ESP_LOGI(TAG, "Dusk detected (current: %u mA). Entering night sleep...", sample.isc_current_ma);
            stats_.is_night_mode = true;
            telemetry_snapshot_.set_night_mode(true);
            core_.power_profile = farm::PowerProfile::DEEP_SLEEP;

            ina_sensor_task_.stop();
            slow_sensors_task_.stop();

            send_night_transition_report(/*requires_ack=*/true);

            hal_rtos_.task_delay(pdMS_TO_TICKS(100));

            handle_command_process_result(command_handler_.process());

            enter_deep_sleep();
            return true;
        }
    }
    else if (sampling_active) {
        ESP_LOGE(TAG, "InaSensorTask watchdog timeout (>%ums without sample)! Resetting system...", timeout_ms);
        recover_ina_hardware();
        hal_system_.restart();
    }

    return false;
}

void SolarSensor::update_current_stats(const InaSample& sample)
{
    if (sample.isc_current_ma > stats_.max_day_current_ma) {
        stats_.max_day_current_ma = sample.isc_current_ma;
    }
    uint32_t sample_period_ms = ina_sensor_task_.get_expected_sample_period_ms();
    if (sample_period_ms > 0 && sample.isc_current_ma > 0) {
        uint64_t delta_umah = (static_cast<uint64_t>(sample.isc_current_ma) * sample_period_ms) / 3600ULL;
        yield_umah_accumulator_ += delta_umah;
        uint32_t new_yield_mah = static_cast<uint32_t>(yield_umah_accumulator_ / 1000ULL);
        if (new_yield_mah != stats_.daily_yield_mah) {
            stats_.daily_yield_mah = new_yield_mah;
        }
    }
}

esp_err_t SolarSensor::recover_ina_hardware()
{
    ina_sensor_task_.deinit();

    if (i2c_bus_handle_ != nullptr) {
        hal_i2c_.del_master_bus(i2c_bus_handle_);
        i2c_bus_handle_ = nullptr;
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
    led_.set_pattern(BlinkPattern::ENTER_SLEEP);
    hal_rtos_.task_delay(pdMS_TO_TICKS(350));
    led_.stop();

    espnow_.deinit();
    wifi_.disconnect(2000);
    wifi_.stop(2000);

    // Arm the night regime before sleeping. Without it the conversion-ready
    // alert (CNVR) stays armed, so the INA asserts the ALERT pin on every
    // conversion and the MCU would wake in a tight loop. On failure, abort
    esp_err_t err = ina_sensor_task_.prepare_for_sleep();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to prepare INA for sleep (%s), aborting deep sleep", esp_err_to_name(err));
        return;
    }

    auto unix_time = get_synced_time();
    uint64_t sleep_time_us = day_night_controller_.calculate_night_sleep_time_us(unix_time);
    uint32_t sleep_minutes = static_cast<uint32_t>(sleep_time_us / 60000000ULL);

    ESP_LOGI(
        TAG,
        "Entering deep sleep for %lu min (%llu us)...",
        static_cast<unsigned long>(sleep_minutes),
        static_cast<unsigned long long>(sleep_time_us));

    hal_sleep_.enable_timer_wakeup(sleep_time_us);

    // Keep INA VCC on while sleeping
    hal_gpio_.hold_en(INA_VCC_GPIO);

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
        ESP_LOGE(TAG, "Failed to configure INA alert pin: %s", esp_err_to_name(err));
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

    err = hal_gpio_.isr_handler_add(INA_ALERT_GPIO, ina_alert_isr_handler, this);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add INA alert ISR handler: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "INA alert pin (GPIO %d) configured", INA_ALERT_GPIO);
    return ESP_OK;
}

/**
 * @brief Alert interrupt service routine
 *
 * @param arg Pointer to SolarSensor instance
 *
 * @note This ISR runs in IRAM for fast execution and notifies the INA sensor
 * task on every completed conversion (CNVR).
 */
void IRAM_ATTR SolarSensor::ina_alert_isr_handler(void* arg)
{
    auto* self = static_cast<SolarSensor*>(arg);
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;

    if (self->ina_sensor_task_.get_task_handle() != nullptr) {
        vTaskNotifyGiveFromISR(self->ina_sensor_task_.get_task_handle(), &xHigherPriorityTaskWoken);
    }

    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

WakeType SolarSensor::evaluate_boot_mode()
{
    esp_sleep_wakeup_cause_t cause = hal_sleep_.get_wakeup_cause();
    bool is_gpio_wakeup = (cause == ESP_SLEEP_WAKEUP_GPIO || cause == ESP_SLEEP_WAKEUP_EXT0);

    // Power-on reset / cold boot starts in Day mode
    if (cause == ESP_SLEEP_WAKEUP_UNDEFINED) {
        ESP_LOGI(TAG, "Cold boot (ESP_SLEEP_WAKEUP_UNDEFINED). Starting in Day mode.");
        return WakeType::DAWN_TIMER;
    }

    uint16_t initial_current_ma = 0;
    if (ina_sample_queue_ != nullptr) {
        InaSample initial_sample{};
        if (hal_rtos_.queue_receive(ina_sample_queue_, &initial_sample, pdMS_TO_TICKS(200)) == pdTRUE) {
            if (initial_sample.status == ESP_OK) {
                initial_current_ma = initial_sample.isc_current_ma;
            }
        }
    }

    auto unix_time = get_synced_time();
    WakeType wake_type = day_night_controller_.classify_wake(is_gpio_wakeup, initial_current_ma, unix_time);

    ESP_LOGI(
        TAG,
        "Evaluated boot mode: %d (cause=%d, gpio=%d, current=%u mA, synced=%d)",
        static_cast<int>(wake_type),
        static_cast<int>(cause),
        is_gpio_wakeup,
        initial_current_ma,
        unix_time.has_value());

    return wake_type;
}

void SolarSensor::on_dawn_start()
{
    ESP_LOGI(TAG, "Dawn transition: resetting daily stats and enabling telemetry reporting");

    stats_.max_day_current_ma = 0;
    stats_.daily_yield_mah = 0;
    yield_umah_accumulator_ = 0;
    stats_.is_night_mode = false;
    core_.power_profile = farm::PowerProfile::ALWAYS_ON;

    telemetry_snapshot_.update_stats(stats_.max_day_current_ma, stats_.daily_yield_mah);
    telemetry_snapshot_.set_night_mode(false);

    ina_sensor_task_.set_shunt_zero_offset_uv(stats_.shunt_zero_offset_uv);
    ina_sensor_task_.set_reporting_enabled(true);
    pending_solar_commit_ = true;
    pending_core_commit_ = true;
}

void SolarSensor::process_night_calibration()
{
    ESP_LOGI(TAG, "Starting 03:00 AM UTC night calibration...");

    stats_.is_night_mode = true;
    telemetry_snapshot_.set_night_mode(true);
    core_.power_profile = farm::PowerProfile::DEEP_SLEEP;

    static constexpr uint8_t TARGET_SAMPLES = 9;
    static constexpr int32_t MAX_SANITY_OFFSET_UV = 100; // Max 100 uV (1.0 mA) nocturnal offset limit

    int32_t samples[TARGET_SAMPLES] = {0};
    uint8_t valid_samples = 0;

    if (ina_sample_queue_ != nullptr) {
        for (uint8_t i = 0; i < TARGET_SAMPLES; ++i) {
            InaSample sample{};
            if (hal_rtos_.queue_receive(ina_sample_queue_, &sample, pdMS_TO_TICKS(200)) == pdTRUE) {
                if (sample.status == ESP_OK && std::abs(sample.shunt_voltage_uv) <= MAX_SANITY_OFFSET_UV) {
                    samples[valid_samples++] = sample.shunt_voltage_uv;
                }
                else if (sample.status == ESP_OK) {
                    ESP_LOGW(
                        TAG,
                        "Calibration sample rejected due to light pulse / noise spike: %ld uV",
                        static_cast<long>(sample.shunt_voltage_uv));
                }
            }
        }
    }

    if (valid_samples >= 3) {
        std::sort(samples, samples + valid_samples);
        int16_t median_offset_uv = static_cast<int16_t>(samples[valid_samples / 2]);
        stats_.shunt_zero_offset_uv = median_offset_uv;
        ESP_LOGI(
            TAG,
            "Night zero-current calibration complete: median offset = %d uV (%u valid samples)",
            median_offset_uv,
            valid_samples);
        pending_solar_commit_ = true;
    }
    else {
        ESP_LOGW(
            TAG,
            "Night calibration skipped: insufficient valid samples (retaining previous offset = %d uV)",
            stats_.shunt_zero_offset_uv);
    }

    send_night_transition_report(/*requires_ack=*/true);

    hal_rtos_.task_delay(pdMS_TO_TICKS(100));

    handle_command_process_result(command_handler_.process());

    enter_deep_sleep();
}

void SolarSensor::process_spurious_wake()
{
    // TODO: Implement lightning flash detection and event logging when night wake is triggered by brief nocturnal light
    // pulses (e.g. lightning).
    ESP_LOGI(TAG, "Spurious night wakeup detected. Re-entering deep sleep...");
    enter_deep_sleep();
}

esp_err_t SolarSensor::send_night_transition_report(bool requires_ack)
{
    led_.pulse(30);

    TelemetrySnapshotData snap = telemetry_snapshot_.get();

    farm::SolarSensorReport report{};
    report.power_profile = core_.power_profile;
    report.isc_current_ma = 0;
    report.irradiance_wm2 = 0;
    report.panel_temp_c = (snap.temperature_celsius > -100.0f)
                              ? static_cast<int16_t>(std::round(snap.temperature_celsius * 10.0f))
                              : INT16_MIN;
    report.battery_mv = snap.battery_mv;
    report.battery_percent = snap.battery_percent;
    report.battery_state = snap.battery_state;
    report.status = farm::SensorStatus::OK;
    report.max_current_ma = stats_.max_day_current_ma;
    report.daily_yield_mah = stats_.daily_yield_mah;
    report.is_night_mode = stats_.is_night_mode;
    report.unix_time = time_manager_.is_synchronized() ? time_manager_.get_timestamp_ms() : 0;

    ESP_LOGI(
        TAG,
        "TX Night Telemetry Report: profile=%d, Bat=%u mV (%u%%), Temp=%d (0.1C), MaxDay=%u mA, Yield=%lu mAh, ACK=%d",
        static_cast<int>(report.power_profile),
        report.battery_mv,
        report.battery_percent,
        report.panel_temp_c,
        report.max_current_ma,
        static_cast<unsigned long>(report.daily_yield_mah),
        requires_ack);

    return espnow_.send_data(
        espnow::ReservedIds::HUB,
        static_cast<uint8_t>(farm::PayloadType::SOLAR_SENSOR_REPORT),
        &report,
        sizeof(report),
        requires_ack);
}

void SolarSensor::handle_command_process_result(const CommandProcessResult& cmd_res)
{
    if (cmd_res.core_modified) {
        pending_core_commit_ = true;
    }

    if (stats_.is_night_mode) {
        pending_core_commit_ = true;
        pending_solar_commit_ = true;
        save_persistent_state();
    }

    if (cmd_res.ota_requested) {
        process_pending_ota();
        return;
    }

    if (cmd_res.reboot_requested) {
        ESP_LOGW(TAG, "Reboot requested via command! Saving state and disconnecting WiFi...");
        pending_core_commit_ = true;
        pending_solar_commit_ = true;
        save_persistent_state();
        wifi_.disconnect(2000);
        espnow_.deinit();
        hal_system_.restart();
        return;
    }
}

void SolarSensor::check_espnow_connection()
{
    espnow::NodeState state = espnow_.get_node_state();
    if (state == espnow::NodeState::IDLE) {
        int64_t now_ms = hal_timer_.get_time_us() / 1000;
        if (last_idle_reconnect_ts_ms_ == 0 || (now_ms - last_idle_reconnect_ts_ms_) >= IDLE_RECONNECT_INTERVAL_MS) {
            last_idle_reconnect_ts_ms_ = now_ms;
            auto peers = espnow_.get_peers();
            if (!peers.empty()) {
                ESP_LOGW(TAG, "EspNow in IDLE with known Hub. Triggering reconnect scan...");
                led_.set_pattern(BlinkPattern::IDLE_BEACON);
                espnow_.reconnect();
            }
            else {
                ESP_LOGW(TAG, "EspNow in IDLE without peers (unpaired node). Starting pairing mode...");
                led_.set_pattern(BlinkPattern::PAIRING_MODE);
                espnow_.start_pairing(30000);
            }
        }
    }
    else if (state == espnow::NodeState::OPERATIONAL) {
        BlinkPattern pat = led_.get_current_pattern();
        if (pat == BlinkPattern::IDLE_BEACON || pat == BlinkPattern::PAIRING_MODE) {
            led_.set_pattern(BlinkPattern::OFF);
        }
    }
}

bool SolarSensor::is_firmware_healthy(bool healthy)
{
    OtaVerifyResult verify = ota_controller_.verify_firmware_on_boot(healthy);
    if (!verify.pending_verify) {
        return true;
    }

    if (verify.success) {
        if (verify.version.has_value()) {
            core_.fw_major = verify.version->major;
            core_.fw_minor = verify.version->minor;
            core_.fw_patch = verify.version->patch;
        }
        pending_core_commit_ = true;
        send_ota_report(verify.exec_result, verify.error_code);
        return true;
    }
    else {
        ESP_LOGE(
            TAG, "Post-boot OTA verification failed! Delaying for report transmission then triggering rollback...");
        led_.set_pattern(BlinkPattern::ERROR_BURST);
        send_ota_report(verify.exec_result, verify.error_code);
        hal_rtos_.task_delay(pdMS_TO_TICKS(500));
        ota_controller_.rollback_and_reboot();
        return false;
    }

    return false;
}