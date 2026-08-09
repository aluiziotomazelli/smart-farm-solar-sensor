// main/src/solar_sensor_nvs.cpp
#include <type_traits>
#include <cstdint>
#include <cstring>

#include "esp_log.h"
#include "esp_attr.h"
#include "esp_rom_crc.h"

#include "solar_sensor_nvs.hpp"

static const char* TAG = "SolarSensorNvs";

SolarSensorNvs::SolarSensorNvs(IPersistenceBackend& rtc_stats, IPersistenceBackend& nvs_stats)
    : rtc_stats_(rtc_stats)
    , nvs_stats_(nvs_stats)
{
}

esp_err_t SolarSensorNvs::load_app_data(SolarStats& stats)
{
    SolarStats temp_stats = {};

    esp_err_t ret = load_raw_app_data(temp_stats);
    if (ret == ESP_OK) {
        stats = temp_stats;
    }

    return ret;
}

esp_err_t SolarSensorNvs::save_app_data(const SolarStats& stats, bool force_nvs_commit)
{
    SolarStats new_stats = stats;
    new_stats.magic = SolarStats::MAGIC;

    bool is_dirty = is_app_data_dirty(new_stats);

    // Calculate CRC to save if data is dirty
    new_stats.crc = calculate_crc(new_stats);

    // If data is not dirty and force_nvs_commit is false, return
    if (!is_dirty && !force_nvs_commit) {
        return ESP_OK;
    }

    // If is dirty, save to RTC
    if (is_dirty) {
        rtc_stats_.save(&new_stats, sizeof(new_stats));
        ESP_LOGI(TAG, "Saved tank stats to RTC");
    }

    // If NVS commit is forced, save to nvs
    if (force_nvs_commit) {
        esp_err_t err = nvs_stats_.save(&new_stats, sizeof(new_stats));
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "Saved tank stats to NVS");
            return ESP_OK;
        }
        else {
            ESP_LOGE(TAG, "Failed to save tank stats to NVS: %s", esp_err_to_name(err));
            return err;
        }
    }
    return ESP_OK;
}

// =================================================
// Private methods
// =================================================

esp_err_t SolarSensorNvs::load_raw_app_data(SolarStats& data_out)
{
    esp_err_t ret;

    ret = rtc_stats_.load(&data_out, sizeof(data_out));
    if (ret == ESP_OK) {
        ret = validate_app_data(data_out);
        if (ret == ESP_OK) {
            ESP_LOGD(TAG, "Loaded tank stats from RTC memory");
            return ESP_OK;
        }
    }

    ret = nvs_stats_.load(&data_out, sizeof(data_out));
    if (ret == ESP_OK) {
        ret = validate_app_data(data_out);
        if (ret == ESP_OK) {
            // Sync valid NVS data back to RTC
            rtc_stats_.save(&data_out, sizeof(data_out));
            ESP_LOGD(TAG, "Loaded tank stats from NVS flash");
            return ESP_OK;
        }
    }
    return ret;
}

esp_err_t SolarSensorNvs::validate_app_data(const SolarStats& data)
{
    if (data.magic != SolarStats::MAGIC) {
        ESP_LOGW(TAG, "Invalid magic number in tank stats: 0x%04X", data.magic);
        return ESP_ERR_INVALID_ARG;
    }

    if (data.version != SolarStats::VERSION) {
        ESP_LOGW(TAG, "Invalid version in tank stats: %d", data.version);
        return ESP_ERR_INVALID_VERSION;
    }

    if (data.crc != calculate_crc<SolarStats>(data)) {
        ESP_LOGW(TAG, "CRC mismatch in tank stats: 0x%08X", data.crc);
        return ESP_ERR_INVALID_CRC;
    }
    return ESP_OK;
}

bool SolarSensorNvs::is_app_data_dirty(const SolarStats& new_data) const
{
    SolarStats current_data;

    if (rtc_stats_.load(&current_data, sizeof(current_data)) != ESP_OK) {
        return true; // Assume dirty if we can't load current data from RTC
    }

    return (current_data != new_data);
}