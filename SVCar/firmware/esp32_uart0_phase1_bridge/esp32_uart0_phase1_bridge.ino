// ESP32 UART0 Phase 1 Bridge — v2 (MQTT over WebSocket + TLS)
//
// Libraries required (all in Arduino Library Manager):
//   1. MQTTPubSubClient  (by hideakitai)      — search "MQTTPubSubClient"
//   2. WebSockets         (by Markus Sattler)  — search "WebSockets"
//
// IMPORTANT: There are TWO different WebSocket libraries in the Manager:
//   - "WebSockets" by Markus Sattler          ← THIS ONE (provides WebSocketsClient.h)
//   - "ArduinoWebsockets" by Gil Maimon       ← WRONG (different API)
//
// For PlatformIO: lib_deps =
//   hideakitai/MQTTPubSubClient
//   links2004/WebSockets

#include <Arduino.h>
#include <WiFi.h>
#include <WebSocketsClient.h>       // MUST be included before MQTTPubSubClient.h
#include <MQTTPubSubClient.h>
#include <cstring>
#include <cstdlib>

#if __has_include("config.h")
#include "config.h"
#else
#include "config.example.h"
#endif

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------
namespace {
WebSocketsClient wsClient;
MQTTPubSubClient mqttClient;
HardwareSerial linkSerial(2);

char claimTopic[96];
char releaseTopic[96];
char cmdTopic[96];
char stopTopic[96];
char telemetryTopic[96];
char statusTopic[96];
char clientId[64];

char ownerId[32] = "";
unsigned long ownerExpiresAt = 0;
bool linkFailSafeIssued = false;
bool mqttReady = false;
unsigned long lastStatusPublishAt = 0;
unsigned long lastPingAt = 0;
unsigned long lastReconnectAttemptAt = 0;

int lastSentLeft = 0;
int lastSentRight = 0;

char serialLine[96];
size_t serialLength = 0;
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
int clampWheel(int value) {
  return constrain(value, -255, 255);
}

uint32_t clampUInt32(long value, uint32_t low, uint32_t high) {
  if (value < (long)low) return low;
  if (value > (long)high) return high;
  return (uint32_t)value;
}

int slewToward(int current, int target, int maxStep) {
  if (target > current + maxStep) return current + maxStep;
  if (target < current - maxStep) return current - maxStep;
  return target;
}

bool ownerExpired() {
  return ownerId[0] == '\0' || millis() > ownerExpiresAt;
}

bool ownerMatches(const char *candidate) {
  return ownerId[0] != '\0' && strcmp(ownerId, candidate) == 0 && !ownerExpired();
}

void clearOwner() {
  ownerId[0] = '\0';
  ownerExpiresAt = 0;
}

// ---------------------------------------------------------------------------
// Topic / ID builders
// ---------------------------------------------------------------------------
void buildTopics() {
  snprintf(claimTopic, sizeof(claimTopic), "robot/%s/session/claim", config::ROBOT_ID);
  snprintf(releaseTopic, sizeof(releaseTopic), "robot/%s/session/release", config::ROBOT_ID);
  snprintf(cmdTopic, sizeof(cmdTopic), "robot/%s/cmd", config::ROBOT_ID);
  snprintf(stopTopic, sizeof(stopTopic), "robot/%s/cmd/stop", config::ROBOT_ID);
  snprintf(telemetryTopic, sizeof(telemetryTopic), "robot/%s/telemetry", config::ROBOT_ID);
  snprintf(statusTopic, sizeof(statusTopic), "robot/%s/status", config::ROBOT_ID);
  snprintf(clientId, sizeof(clientId), "esp32-uart0-bridge-%s", config::ROBOT_ID);
}

// ---------------------------------------------------------------------------
// Robot UART
// ---------------------------------------------------------------------------
void writeRobotLine(const char *line) {
  linkSerial.print(line);
  linkSerial.print('\n');
  Serial.println(line);
}

void sendRobotStop(const char *reason) {
  char line[48];
  snprintf(line, sizeof(line), "STOP,%s", reason);
  lastSentLeft = 0;
  lastSentRight = 0;
  writeRobotLine(line);
}

// ---------------------------------------------------------------------------
// MQTT publish wrapper
// ---------------------------------------------------------------------------
void publishStatus(const char *state, const char *detail) {
  if (!mqttReady) return;

  char payload[256];
  const char *owner = ownerExpired() ? "" : ownerId;
  snprintf(payload, sizeof(payload),
    "{\"robotId\":\"%s\",\"state\":\"%s\",\"detail\":\"%s\",\"owner\":\"%s\",\"wifiRssi\":%d,\"mqttConnected\":%s}",
    config::ROBOT_ID, state, detail, owner, WiFi.RSSI(), mqttReady ? "true" : "false");
  mqttClient.publish(statusTopic, payload, true);  // retain
}

// ---------------------------------------------------------------------------
// Payload parsers
// ---------------------------------------------------------------------------
bool parseLongToken(char *token, long &outValue) {
  if (token == NULL || *token == '\0') return false;
  char *endPtr = NULL;
  outValue = strtol(token, &endPtr, 10);
  return endPtr != token && *endPtr == '\0';
}

bool parseClaimPayload(char *payload, char *controllerIdOut, size_t sz, long &leaseMsOut) {
  char *controllerId = strtok(payload, ",");
  char *leaseMs = strtok(NULL, ",");
  if (!controllerId || !leaseMs || strlen(controllerId) == 0) return false;
  if (!parseLongToken(leaseMs, leaseMsOut)) return false;
  strncpy(controllerIdOut, controllerId, sz - 1);
  controllerIdOut[sz - 1] = '\0';
  return true;
}

bool parseCmdPayload(char *payload, char *controllerIdOut, size_t sz,
                     long &seqOut, long &leftOut, long &rightOut, long &ttlMsOut) {
  char *cid = strtok(payload, ",");
  char *seq = strtok(NULL, ",");
  char *left = strtok(NULL, ",");
  char *right = strtok(NULL, ",");
  char *ttl = strtok(NULL, ",");
  if (!cid || !seq || !left || !right || !ttl) return false;
  if (!parseLongToken(seq, seqOut) || !parseLongToken(left, leftOut) ||
      !parseLongToken(right, rightOut) || !parseLongToken(ttl, ttlMsOut)) return false;
  strncpy(controllerIdOut, cid, sz - 1);
  controllerIdOut[sz - 1] = '\0';
  return true;
}

void forwardDriveCommand(long seq, int left, int right, uint32_t ttlMs) {
  lastSentLeft = slewToward(lastSentLeft, clampWheel(left), config::MAX_SLEW_STEP);
  lastSentRight = slewToward(lastSentRight, clampWheel(right), config::MAX_SLEW_STEP);
  char line[64];
  snprintf(line, sizeof(line), "CMD,%ld,%d,%d,%lu", seq, lastSentLeft, lastSentRight, (unsigned long)ttlMs);
  writeRobotLine(line);
}

// ---------------------------------------------------------------------------
// MQTT message handlers
// ---------------------------------------------------------------------------
void handleClaimMessage(const char *rawPayload) {
  char payload[128];
  strncpy(payload, rawPayload, sizeof(payload) - 1);
  payload[sizeof(payload) - 1] = '\0';

  char requestedOwner[32];
  long leaseMs = 0;
  if (!parseClaimPayload(payload, requestedOwner, sizeof(requestedOwner), leaseMs)) {
    publishStatus("error", "bad_claim_payload");
    return;
  }
  uint32_t boundedLease = clampUInt32(leaseMs, config::MIN_OWNER_LEASE_MS, config::MAX_OWNER_LEASE_MS);
  if (ownerExpired() || strcmp(ownerId, requestedOwner) == 0) {
    strncpy(ownerId, requestedOwner, sizeof(ownerId) - 1);
    ownerId[sizeof(ownerId) - 1] = '\0';
    ownerExpiresAt = millis() + boundedLease;
    publishStatus("claimed", "owner_updated");
    return;
  }
  publishStatus("busy", "already_claimed");
}

void handleReleaseMessage(const char *rawPayload) {
  if (ownerExpired()) { clearOwner(); return; }
  if (strcmp(ownerId, rawPayload) == 0) {
    sendRobotStop("release");
    clearOwner();
    publishStatus("idle", "released");
  }
}

void handleStopMessage(const char *rawPayload) {
  if (ownerMatches(rawPayload)) {
    sendRobotStop("mqtt_stop");
    publishStatus("claimed", "stop");
  }
}

void handleCmdMessage(const char *rawPayload) {
  char payload[128];
  strncpy(payload, rawPayload, sizeof(payload) - 1);
  payload[sizeof(payload) - 1] = '\0';

  char requestedOwner[32];
  long seq = 0, left = 0, right = 0, ttlMs = 0;
  if (!parseCmdPayload(payload, requestedOwner, sizeof(requestedOwner), seq, left, right, ttlMs)) {
    publishStatus("error", "bad_cmd_payload");
    return;
  }
  if (!ownerMatches(requestedOwner)) {
    publishStatus("busy", "owner_mismatch");
    return;
  }
  ownerExpiresAt = millis() + config::DEFAULT_OWNER_LEASE_MS;
  uint32_t boundedTtl = clampUInt32(ttlMs, config::MIN_COMMAND_TTL_MS, config::MAX_COMMAND_TTL_MS);
  forwardDriveCommand(seq, (int)left, (int)right, boundedTtl);
}

// ---------------------------------------------------------------------------
// MQTT per-topic callback (called for every message)
// ---------------------------------------------------------------------------
void onMqttMessage(const String &topic, const String &payload, const size_t size) {
  Serial.printf("[mqtt] %s => %s\n", topic.c_str(), payload.c_str());

  if (topic == claimTopic)   { handleClaimMessage(payload.c_str());  return; }
  if (topic == releaseTopic) { handleReleaseMessage(payload.c_str()); return; }
  if (topic == cmdTopic)     { handleCmdMessage(payload.c_str());    return; }
  if (topic == stopTopic)    { handleStopMessage(payload.c_str());   return; }
}

// ---------------------------------------------------------------------------
// MQTT connection management
// ---------------------------------------------------------------------------
void mqttConnect() {
  if (mqttClient.isConnected()) return;
  if (millis() - lastReconnectAttemptAt < 2000) return;
  lastReconnectAttemptAt = millis();

  Serial.println("[mqtt] connecting ...");

  mqttClient.begin(config::MQTT_USERNAME, config::MQTT_PASSWORD);

  // Subscribe to topics
  mqttClient.subscribe([](const String &topic, const String &payload, const size_t size) {
    // Global wildcard handler — we use per-topic subscribe below
  });
  mqttClient.subscribe(claimTopic,   onMqttMessage);
  mqttClient.subscribe(releaseTopic, onMqttMessage);
  mqttClient.subscribe(cmdTopic,     onMqttMessage);
  mqttClient.subscribe(stopTopic,    onMqttMessage);

  mqttClient.publish(statusTopic, "{\"state\":\"online\",\"detail\":\"mqtt_connected\"}", true);
  mqttReady = true;
  linkFailSafeIssued = false;
  Serial.println("[mqtt] connected");
}

// ---------------------------------------------------------------------------
// WiFi
// ---------------------------------------------------------------------------
void ensureWifiConnected() {
  if (WiFi.status() == WL_CONNECTED) return;

  Serial.printf("[wifi] connecting to %s ...\n", config::WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.begin(config::WIFI_SSID, config::WIFI_PASSWORD);

  unsigned long t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 15000) {
    delay(250);
    Serial.print('.');
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("[wifi] connected, IP=%s\n", WiFi.localIP().toString().c_str());
  } else {
    Serial.printf("[wifi] FAILED, status=%d\n", (int)WiFi.status());
  }
}

// ---------------------------------------------------------------------------
// Safety
// ---------------------------------------------------------------------------
void handleOwnerLease() {
  if (ownerExpired() && ownerId[0] != '\0') {
    sendRobotStop("lease_expired");
    clearOwner();
    publishStatus("idle", "lease_expired");
  }
}

void handleLinkFailSafe() {
  bool linkHealthy = WiFi.status() == WL_CONNECTED && mqttReady;
  if (linkHealthy) { linkFailSafeIssued = false; return; }
  if (!linkFailSafeIssued) {
    sendRobotStop("link_loss");
    clearOwner();
    linkFailSafeIssued = true;
  }
}

// ---------------------------------------------------------------------------
// Robot serial
// ---------------------------------------------------------------------------
void handleRobotSerialLine(char *line) {
  Serial.printf("[robot] %s\n", line);
  if (!mqttReady) return;

  if (strncmp(line, "TEL,", 4) == 0) {
    strtok(line, ",");
    char *uptime = strtok(NULL, ",");
    char *left    = strtok(NULL, ",");
    char *right   = strtok(NULL, ",");
    char *state   = strtok(NULL, ",");
    if (uptime && left && right && state) {
      char payload[256];
      snprintf(payload, sizeof(payload),
        "{\"robotId\":\"%s\",\"uptimeMs\":%s,\"left\":%s,\"right\":%s,\"runtimeState\":\"%s\"}",
        config::ROBOT_ID, uptime, left, right, state);
      mqttClient.publish(telemetryTopic, payload);
    }
    return;
  }
  if (strncmp(line, "ERR,", 4) == 0)     { publishStatus("error", line + 4); return; }
  if (strncmp(line, "READY,", 6) == 0)   { publishStatus("linked", line + 6); return; }
  if (strncmp(line, "ACK,STOP", 8) == 0) { publishStatus(ownerExpired() ? "idle" : "claimed", "stop_ack"); return; }
  if (strncmp(line, "ACK,", 4) == 0)     { publishStatus(ownerExpired() ? "idle" : "claimed", "cmd_ack"); return; }
  if (strncmp(line, "PONG,", 5) == 0)    { publishStatus(ownerExpired() ? "idle" : "claimed", "pong"); }
}

void serviceRobotSerial() {
  while (linkSerial.available() > 0) {
    char incoming = (char)linkSerial.read();
    if (incoming == '\r') continue;
    if (incoming == '\n') {
      serialLine[serialLength] = '\0';
      if (serialLength > 0) handleRobotSerialLine(serialLine);
      serialLength = 0;
      continue;
    }
    if (serialLength >= sizeof(serialLine) - 1) { serialLength = 0; continue; }
    serialLine[serialLength++] = incoming;
  }
}

// ---------------------------------------------------------------------------
// Setup / Loop
// ---------------------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("=== ESP32 UART0 bridge v2 (MQTT over WSS) ===");

  // Reset pin
  pinMode(config::LINK_RESET_PIN, OUTPUT);
  digitalWrite(config::LINK_RESET_PIN, LOW);
  if (config::PULSE_RESET_ON_BOOT) {
    digitalWrite(config::LINK_RESET_PIN, HIGH);
    delay(50);
    digitalWrite(config::LINK_RESET_PIN, LOW);
    delay(400);
  }

  buildTopics();

  // UART to ATmega
  linkSerial.begin(config::LINK_UART_BAUD, SERIAL_8N1, config::LINK_UART_RX_PIN, config::LINK_UART_TX_PIN);

  // WiFi
  ensureWifiConnected();

  // WebSocket → MQTT transport layer
  // Note: beginSSL without CA cert = ESP32 skips cert verification
  // (no RTC/NTP on boot, can't check expiry). Fine for POC — Cloudflare cert is valid.
  // The Markus Sattler WebSockets lib calls setInsecure() internally on ESP32.
  if (config::MQTT_USE_SSL) {
    wsClient.beginSSL(config::MQTT_HOST, config::MQTT_PORT, config::MQTT_WS_PATH, "", "mqtt");
  } else {
    wsClient.begin(config::MQTT_HOST, config::MQTT_PORT, config::MQTT_WS_PATH, "mqtt");
  }
  wsClient.setReconnectInterval(2000);

  // MQTT client sits on top of WebSocket
  mqttClient.begin(wsClient);

  sendRobotStop("boot");
}

void loop() {
  ensureWifiConnected();

  // Feed the WebSocket + MQTT stack
  wsClient.loop();
  mqttClient.update();

  // Auto-connect MQTT once WebSocket is up
  if (!mqttReady && WiFi.status() == WL_CONNECTED) {
    mqttConnect();
  }

  // Detect MQTT disconnection
  if (mqttReady && !mqttClient.isConnected()) {
    Serial.println("[mqtt] disconnected");
    mqttReady = false;
  }

  serviceRobotSerial();
  handleOwnerLease();
  handleLinkFailSafe();

  if (mqttReady && millis() - lastStatusPublishAt >= 1000) {
    lastStatusPublishAt = millis();
    publishStatus(ownerExpired() ? "idle" : "claimed", "heartbeat");
  }

  if (millis() - lastPingAt >= 3000) {
    lastPingAt = millis();
    writeRobotLine("PING");
  }
}
