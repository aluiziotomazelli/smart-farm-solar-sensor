#pragma once

#include <atomic>

#include "interfaces/i_nvs_core.hpp"
#include "interfaces/i_solar_sensor_nvs.hpp"
#include "interfaces/i_hal_timer.hpp"
#include "interfaces/i_ota_trigger.hpp"
#include "interfaces/i_wifi_manager.hpp"
#include "interfaces/i_espnow_manager.hpp"
#include "interfaces/i_hal_sleep.hpp"
#include "interfaces/i_hal_system.hpp"
#include "interfaces/i_time_manager.hpp"
#include "interfaces/i_hal_freertos.hpp"
#include "interfaces/i_hal_gpio.hpp"
#include "interfaces/i_hal_i2c.hpp"
#include "interfaces/i_led_controller.hpp"

#include "interfaces/i_ina_sensor_task.hpp"
#include "interfaces/i_slow_sensors_task.hpp"
#include "interfaces/i_command_handler.hpp"
#include "day_night_controller.hpp"
#include "interfaces/i_ota_controller.hpp"

#include "solar_sensor_stats.hpp"
#include "telemetry_snapshot.hpp"

/**
 * @class SolarSensor
 * @brief Interface for the solar sensor.
 */
class SolarSensor : public IOtaTriggerListener
{
public:
    SolarSensor(
        ina::IInaSensorTask& ina_sensor_task,
        QueueHandle_t ina_sample_queue,
        TelemetrySnapshot& telemetry_snapshot,
        ISlowSensorsTask& slow_sensors_task,
        INvsCore& core_storage,
        ISolarSensorNvs& solar_storage,
        idf_hals::ITimerHAL& hal_timer,
        IOtaController& ota_controller,
        IOtaTrigger& btn_trigger,
        IOtaTrigger& espnow_trigger,
        espnow::IEspNowManager& espnow,
        QueueHandle_t rx_queue,
        wifi_manager::IWiFiManager& wifi,
        idf_hals::ISleepHAL& hal_sleep,
        idf_hals::ISystemHAL& hal_system,
        time_manager::ITimeManager& time_manager,
        idf_hals::IHalFreertos& hal_freertos,
        idf_hals::IGpioHAL& hal_gpio,
        idf_hals::II2cHAL& hal_i2c,
        ILedController& led,
        IDayNightController& day_night_controller,
        ICommandHandler& command_handler);

    virtual ~SolarSensor() = default;

    esp_err_t init();

    /**
     * @brief Executes a single application run tick.
     * @return true to continue the loop, false if application should stop (Deep Sleep or Reboot).
     */
    bool run();

    /** @copydoc IOtaTriggerListener::on_ota_triggered */
    void on_ota_triggered(OtaTriggerSource source) override;

    ICommandHandler& get_command_handler() { return command_handler_; }
    IDayNightController& get_day_night_controller() { return day_night_controller_; }
    const SolarStats& get_solar_stats() const { return stats_; }

protected:
    CoreData core_;
    SolarStats stats_;

    bool session_healthy_ = true;
    bool pending_core_commit_ = false;
    bool pending_solar_commit_ = false;

private:
    ina::IInaSensorTask& ina_sensor_task_;
    QueueHandle_t ina_sample_queue_;
    TelemetrySnapshot& telemetry_snapshot_;
    ISlowSensorsTask& slow_sensors_task_;
    INvsCore& core_storage_;
    ISolarSensorNvs& solar_storage_;
    idf_hals::ITimerHAL& hal_timer_;
    IOtaController& ota_controller_;
    IOtaTrigger& btn_trigger_;
    IOtaTrigger& espnow_trigger_;
    espnow::IEspNowManager& espnow_;
    QueueHandle_t rx_queue_;
    wifi_manager::IWiFiManager& wifi_;
    idf_hals::ISleepHAL& hal_sleep_;
    idf_hals::ISystemHAL& hal_system_;
    time_manager::ITimeManager& time_manager_;
    idf_hals::IHalFreertos& hal_rtos_;
    idf_hals::IGpioHAL& hal_gpio_;
    idf_hals::II2cHAL& hal_i2c_;
    ILedController& led_;
    IDayNightController& day_night_controller_;
    ICommandHandler& command_handler_;

    std::atomic<bool> ota_triggered_{false};
    bool wake_classified_ = false;
    int64_t last_nvs_commit_ts_ = 0;
    uint8_t consecutive_ina_errors_ = 0;
    uint64_t yield_umah_accumulator_ = 0;
    i2c_master_bus_handle_t i2c_bus_handle_{nullptr};
    TaskHandle_t ina_task_handle_{nullptr};

    esp_err_t init_ina_task(InaSensorConfig config);
    esp_err_t init_ina_vcc_pin();
    esp_err_t init_wifi();
    esp_err_t init_time();
    esp_err_t init_core_storage();
    esp_err_t init_solar_storage();
    esp_err_t init_espnow();
    esp_err_t init_i2c_master_bus(i2c_master_bus_handle_t& i2c_bus_handle);

    void update_running_version();
    void process_pending_ota();
    esp_err_t send_ota_report(farm::OtaExecResult result, farm::OtaErrorCode error_code = farm::OtaErrorCode::NONE);
    bool is_firmware_healthy(bool healthy);

    esp_err_t connect_wifi_with_retry(uint8_t max_attempts);
    std::optional<time_t> get_synced_time() const;
    void save_persistent_state();
    bool process_ina_samples(std::optional<time_t> unix_time);
    void update_current_stats(const InaSample& sample);
    esp_err_t recover_ina_hardware();
    WakeType evaluate_boot_mode();
    void on_dawn_start();
    void process_night_calibration();
    void process_spurious_wake();
    bool run_day_cycle();
    void enter_deep_sleep();
    esp_err_t send_night_transition_report(bool requires_ack);
    bool handle_command_process_result(const CommandProcessResult& cmd_res);

    esp_err_t init_ina_alert_pin();
    static void IRAM_ATTR ina_alert_isr_handler(void* arg);
};