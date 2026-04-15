#pragma once

namespace config {

// --- WiFi ---
static const char *WIFI_SSID = "YOUR_WIFI_SSID";
static const char *WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";

// --- MQTT (over WebSocket Secure via Cloudflare) ---
static const char *MQTT_HOST = "mqtt-battlebots.stayspeed.com";
static const uint16_t MQTT_PORT = 443;
static const char *MQTT_WS_PATH = "/";
static const char *MQTT_USERNAME = "car-01";
static const char *MQTT_PASSWORD = "bb-car01-2026";
static const bool MQTT_USE_SSL = true;  // false for plain WS (local testing)

// For local LAN testing (no WSS), use:
// static const char *MQTT_HOST = "192.168.50.88";
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
}
