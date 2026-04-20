#pragma once

namespace config {

// Copy this file to config.h and fill in the local values there.

// --- WiFi ---
static const char *WIFI_SSID = "YOUR_WIFI_SSID";
static const char *WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";

// --- MQTT (over WebSocket Secure via Cloudflare) ---
static const char *MQTT_HOST = "mqtt.example.com";
static const uint16_t MQTT_PORT = 443;
static const char *MQTT_WS_PATH = "/";
static const char *MQTT_USERNAME = "YOUR_MQTT_USERNAME";
static const char *MQTT_PASSWORD = "YOUR_MQTT_PASSWORD";
static const bool MQTT_USE_SSL = true;  // false for plain WS (local testing)

// For local LAN testing (no WSS), use:
// static const char *MQTT_HOST = "192.168.1.10";
// static const uint16_t MQTT_PORT = 1883;
// static const bool MQTT_USE_SSL = false;

static const char *ROBOT_ID = "car-01";

// --- UART link to ATmega ---
static const int LINK_UART_RX_PIN = 6;
static const int LINK_UART_TX_PIN = 7;
static const uint32_t LINK_UART_BAUD = 115200;
static const int LINK_RESET_PIN = 4;

// Leave reset idle-low by default. Set true only if you intentionally want a boot pulse.
static const bool PULSE_RESET_ON_BOOT = false;

// --- Session ownership ---
static const uint32_t DEFAULT_OWNER_LEASE_MS = 3000;
static const uint32_t MIN_OWNER_LEASE_MS = 1000;
static const uint32_t MAX_OWNER_LEASE_MS = 10000;

// --- Motor safety ---
static const int MAX_SLEW_STEP = 60;
static const uint32_t DEFAULT_COMMAND_TTL_MS = 250;
static const uint32_t MIN_COMMAND_TTL_MS = 120;
static const uint32_t MAX_COMMAND_TTL_MS = 1000;

// --- Remote firmware update ---
static const uint32_t FW_DOWNLOAD_TIMEOUT_MS = 45000;
static const uint32_t FW_STATUS_INTERVAL_MS = 1000;
static const uint32_t FW_MAX_HEX_SIZE = 98304;
static const uint32_t FW_FLASH_BAUD = 115200;
static const uint32_t FW_FLASH_SIZE_BYTES = 32768;
static const uint16_t FW_FLASH_PAGE_SIZE = 128;
static const bool FW_TLS_INSECURE = true;
}
