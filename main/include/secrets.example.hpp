// main/include/secrets.example.hpp
#pragma once

#include <cstdint>

/**
 * @file secrets.example.hpp
 * @brief Template for credentials and network configuration.
 *
 * Copy this file to `secrets.hpp` and fill in your actual private credentials.
 * The `secrets.hpp` file is ignored by Git to avoid leaking secrets.
 */

// =============================================================================
// WiFi Credentials (used for OTA firmware download)
// =============================================================================
#define WIFI_SSID "your_wifi_ssid_here"
#define WIFI_PASS "your_wifi_password_here"

// =============================================================================
// OTA Firmware Server Configuration
// =============================================================================
// URL of the HTTP OTA manifest file used by the ota_manager component.
// For detailed manifest structure and HTTP server setup, refer to the ota_manager documentation.
#define SERVER_URL "http://ota-server.local:8070/manifests/solar_sensor.json"

// =============================================================================
// Geographical Location & Timezone (SunSchedule & Astronomy)
// =============================================================================
static constexpr float LOCATION_LATITUDE_DEG = -23.55f; // Geographical Latitude (e.g., -23.55 for São Paulo)
static constexpr float LOCATION_TZ_OFFSET_HOURS =
    -3.0f; // Local Timezone offset from UTC (e.g., -3.0 for Brasília / BRT)

// =============================================================================
// Hub MAC Address (Optional / Peer Registration)
// =============================================================================
// Note: In standard operation, the Solar Sensor uses automatic discovery or broadcast
// to reach the Hub. These definitions are NOT currently active in the default flow.
// However, if the application needs to explicitly and statically register the Hub
// as a known peer before communication, use the Hub's MAC address with:
//
//   esp_err_t add_peer(NodeId node_id, const uint8_t* mac, NodeType type, uint32_t heartbeat_interval_ms);
//
// Example usage:
//   espnow.add_peer(ReservedIds::HUB, HUB_MAC, NodeType::HUB, 60000);
//
static constexpr uint8_t HUB_MAC[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};