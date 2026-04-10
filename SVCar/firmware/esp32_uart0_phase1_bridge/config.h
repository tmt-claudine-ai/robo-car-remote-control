#pragma once

namespace config {
static const char *WIFI_SSID = "Blackout02";
static const char *WIFI_PASSWORD = "19920326081x";

static const char *MQTT_HOST = "192.168.50.88";
static const uint16_t MQTT_PORT = 1883;
static const char *MQTT_USERNAME = "";
static const char *MQTT_PASSWORD = "";

static const char *ROBOT_ID = "car-01";

// Confirmed from the uploader gateway reference sketch.
static const int LINK_UART_RX_PIN = 6;
static const int LINK_UART_TX_PIN = 7;
static const uint32_t LINK_UART_BAUD = 115200;
static const int LINK_RESET_PIN = 4;

// Leave reset idle-low by default. Set true only if you intentionally want a boot pulse.
static const bool PULSE_RESET_ON_BOOT = false;

static const uint32_t DEFAULT_OWNER_LEASE_MS = 3000;
static const uint32_t MIN_OWNER_LEASE_MS = 1000;
static const uint32_t MAX_OWNER_LEASE_MS = 10000;

static const int MAX_SLEW_STEP = 60;
static const uint32_t DEFAULT_COMMAND_TTL_MS = 250;
static const uint32_t MIN_COMMAND_TTL_MS = 120;
static const uint32_t MAX_COMMAND_TTL_MS = 1000;
}
