#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <cstring>
#include <cstdlib>

#include "config.h"

namespace {
WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);
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

int lastSentLeft = 0;
int lastSentRight = 0;
unsigned long lastStatusPublishAt = 0;
unsigned long lastReconnectAttemptAt = 0;
unsigned long lastPingAt = 0;

char serialLine[96];
size_t serialLength = 0;
}

int clampWheel(int value) {
  return constrain(value, -255, 255);
}

uint32_t clampUInt32(long value, uint32_t low, uint32_t high) {
  if (value < (long)low) {
    return low;
  }
  if (value > (long)high) {
    return high;
  }
  return (uint32_t)value;
}

int slewToward(int current, int target, int maxStep) {
  if (target > current + maxStep) {
    return current + maxStep;
  }
  if (target < current - maxStep) {
    return current - maxStep;
  }
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

void buildTopics() {
  snprintf(claimTopic, sizeof(claimTopic), "robot/%s/session/claim", config::ROBOT_ID);
  snprintf(releaseTopic, sizeof(releaseTopic), "robot/%s/session/release", config::ROBOT_ID);
  snprintf(cmdTopic, sizeof(cmdTopic), "robot/%s/cmd", config::ROBOT_ID);
  snprintf(stopTopic, sizeof(stopTopic), "robot/%s/cmd/stop", config::ROBOT_ID);
  snprintf(telemetryTopic, sizeof(telemetryTopic), "robot/%s/telemetry", config::ROBOT_ID);
  snprintf(statusTopic, sizeof(statusTopic), "robot/%s/status", config::ROBOT_ID);
  snprintf(clientId, sizeof(clientId), "esp32-uart0-bridge-%s", config::ROBOT_ID);
}

void writeRobotLine(const char *line) {
  linkSerial.print(line);
  linkSerial.print('\n');
  Serial.println(line);
}

void pulseResetIfEnabled() {
  pinMode(config::LINK_RESET_PIN, OUTPUT);
  digitalWrite(config::LINK_RESET_PIN, LOW);
  if (!config::PULSE_RESET_ON_BOOT) {
    return;
  }

  digitalWrite(config::LINK_RESET_PIN, HIGH);
  delay(50);
  digitalWrite(config::LINK_RESET_PIN, LOW);
  delay(400);
}

void sendRobotStop(const char *reason) {
  char line[48];
  snprintf(line, sizeof(line), "STOP,%s", reason);
  lastSentLeft = 0;
  lastSentRight = 0;
  writeRobotLine(line);
}

void publishStatus(const char *state, const char *detail) {
  if (!mqttClient.connected()) {
    return;
  }

  char payload[256];
  const char *owner = ownerExpired() ? "" : ownerId;
  snprintf(
      payload,
      sizeof(payload),
      "{\"robotId\":\"%s\",\"state\":\"%s\",\"detail\":\"%s\",\"owner\":\"%s\",\"wifiRssi\":%d,\"mqttConnected\":%s}",
      config::ROBOT_ID,
      state,
      detail,
      owner,
      WiFi.RSSI(),
      mqttClient.connected() ? "true" : "false");
  mqttClient.publish(statusTopic, payload, true);
}

bool parseLongToken(char *token, long &outValue) {
  if (token == NULL || *token == '\0') {
    return false;
  }
  char *endPtr = NULL;
  outValue = strtol(token, &endPtr, 10);
  return endPtr != token && *endPtr == '\0';
}

bool parseClaimPayload(char *payload, char *controllerIdOut, size_t controllerIdOutSize, long &leaseMsOut) {
  char *controllerId = strtok(payload, ",");
  char *leaseMs = strtok(NULL, ",");
  if (controllerId == NULL || leaseMs == NULL || strlen(controllerId) == 0) {
    return false;
  }

  if (!parseLongToken(leaseMs, leaseMsOut)) {
    return false;
  }

  strncpy(controllerIdOut, controllerId, controllerIdOutSize - 1);
  controllerIdOut[controllerIdOutSize - 1] = '\0';
  return true;
}

bool parseCmdPayload(
    char *payload,
    char *controllerIdOut,
    size_t controllerIdOutSize,
    long &seqOut,
    long &leftOut,
    long &rightOut,
    long &ttlMsOut) {
  char *controllerId = strtok(payload, ",");
  char *seq = strtok(NULL, ",");
  char *left = strtok(NULL, ",");
  char *right = strtok(NULL, ",");
  char *ttl = strtok(NULL, ",");

  if (controllerId == NULL || seq == NULL || left == NULL || right == NULL || ttl == NULL) {
    return false;
  }

  if (!parseLongToken(seq, seqOut) ||
      !parseLongToken(left, leftOut) ||
      !parseLongToken(right, rightOut) ||
      !parseLongToken(ttl, ttlMsOut)) {
    return false;
  }

  strncpy(controllerIdOut, controllerId, controllerIdOutSize - 1);
  controllerIdOut[controllerIdOutSize - 1] = '\0';
  return true;
}

void forwardDriveCommand(long seq, int left, int right, uint32_t ttlMs) {
  lastSentLeft = slewToward(lastSentLeft, clampWheel(left), config::MAX_SLEW_STEP);
  lastSentRight = slewToward(lastSentRight, clampWheel(right), config::MAX_SLEW_STEP);

  char line[64];
  snprintf(line, sizeof(line), "CMD,%ld,%d,%d,%lu", seq, lastSentLeft, lastSentRight, (unsigned long)ttlMs);
  writeRobotLine(line);
}

void handleClaimMessage(char *payload) {
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

void handleReleaseMessage(char *payload) {
  if (ownerExpired()) {
    clearOwner();
    return;
  }

  if (strcmp(ownerId, payload) == 0) {
    sendRobotStop("release");
    clearOwner();
    publishStatus("idle", "released");
  }
}

void handleStopMessage(char *payload) {
  if (ownerMatches(payload)) {
    sendRobotStop("mqtt_stop");
    publishStatus("claimed", "stop");
  }
}

void handleCmdMessage(char *payload) {
  char requestedOwner[32];
  long seq = 0;
  long left = 0;
  long right = 0;
  long ttlMs = 0;

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

void mqttCallback(char *topic, uint8_t *payload, unsigned int length) {
  char message[128];
  size_t maxCopyLength = sizeof(message) - 1;
  size_t copyLength = length < maxCopyLength ? length : maxCopyLength;
  memcpy(message, payload, copyLength);
  message[copyLength] = '\0';

  Serial.print("MQTT ");
  Serial.print(topic);
  Serial.print(" => ");
  Serial.println(message);

  if (strcmp(topic, claimTopic) == 0) {
    handleClaimMessage(message);
    return;
  }
  if (strcmp(topic, releaseTopic) == 0) {
    handleReleaseMessage(message);
    return;
  }
  if (strcmp(topic, cmdTopic) == 0) {
    handleCmdMessage(message);
    return;
  }
  if (strcmp(topic, stopTopic) == 0) {
    handleStopMessage(message);
  }
}

void ensureWifiConnected() {
  if (WiFi.status() == WL_CONNECTED) {
    return;
  }

  Serial.print("WIFI connect to SSID: ");
  Serial.println(config::WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.begin(config::WIFI_SSID, config::WIFI_PASSWORD);

  unsigned long startedAt = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startedAt < 15000) {
    delay(250);
    Serial.print('.');
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("WIFI connected, IP=");
    Serial.println(WiFi.localIP());
  } else {
    Serial.print("WIFI connect failed, status=");
    Serial.println((int)WiFi.status());
  }
}

void ensureMqttConnected() {
  if (mqttClient.connected()) {
    return;
  }

  if (millis() - lastReconnectAttemptAt < 2000) {
    return;
  }
  lastReconnectAttemptAt = millis();

  Serial.print("MQTT connect to ");
  Serial.print(config::MQTT_HOST);
  Serial.print(":");
  Serial.println(config::MQTT_PORT);

  if (mqttClient.connect(clientId, config::MQTT_USERNAME, config::MQTT_PASSWORD, statusTopic, 0, true, "offline")) {
    mqttClient.subscribe(claimTopic);
    mqttClient.subscribe(releaseTopic);
    mqttClient.subscribe(cmdTopic);
    mqttClient.subscribe(stopTopic);
    Serial.println("MQTT connected");
    publishStatus("online", "mqtt_connected");
    linkFailSafeIssued = false;
  } else {
    Serial.print("MQTT connect failed, state=");
    Serial.println(mqttClient.state());
  }
}

void handleOwnerLease() {
  if (ownerExpired()) {
    if (ownerId[0] != '\0') {
      sendRobotStop("lease_expired");
      clearOwner();
      publishStatus("idle", "lease_expired");
    }
  }
}

void handleLinkFailSafe() {
  bool linkHealthy = WiFi.status() == WL_CONNECTED && mqttClient.connected();
  if (linkHealthy) {
    linkFailSafeIssued = false;
    return;
  }

  if (!linkFailSafeIssued) {
    sendRobotStop("link_loss");
    clearOwner();
    linkFailSafeIssued = true;
  }
}

void handleRobotSerialLine(char *line) {
  Serial.print("ROBOT ");
  Serial.println(line);

  if (!mqttClient.connected()) {
    return;
  }

  if (strncmp(line, "TEL,", 4) == 0) {
    char *token = strtok(line, ",");
    (void)token;
    char *uptime = strtok(NULL, ",");
    char *left = strtok(NULL, ",");
    char *right = strtok(NULL, ",");
    char *state = strtok(NULL, ",");

    if (uptime != NULL && left != NULL && right != NULL && state != NULL) {
      char payload[256];
      snprintf(
          payload,
          sizeof(payload),
          "{\"robotId\":\"%s\",\"uptimeMs\":%s,\"left\":%s,\"right\":%s,\"runtimeState\":\"%s\"}",
          config::ROBOT_ID,
          uptime,
          left,
          right,
          state);
      mqttClient.publish(telemetryTopic, payload, false);
    }
    return;
  }

  if (strncmp(line, "ERR,", 4) == 0) {
    publishStatus("error", line + 4);
    return;
  }

  if (strncmp(line, "READY,", 6) == 0) {
    publishStatus("linked", line + 6);
    return;
  }

  if (strncmp(line, "ACK,STOP", 8) == 0) {
    publishStatus(ownerExpired() ? "idle" : "claimed", "stop_ack");
    return;
  }

  if (strncmp(line, "ACK,", 4) == 0) {
    publishStatus(ownerExpired() ? "idle" : "claimed", "cmd_ack");
    return;
  }

  if (strncmp(line, "PONG,", 5) == 0) {
    publishStatus(ownerExpired() ? "idle" : "claimed", "pong");
  }
}

void serviceRobotSerial() {
  while (linkSerial.available() > 0) {
    char incoming = (char)linkSerial.read();

    if (incoming == '\r') {
      continue;
    }
    if (incoming == '\n') {
      serialLine[serialLength] = '\0';
      if (serialLength > 0) {
        handleRobotSerialLine(serialLine);
      }
      serialLength = 0;
      continue;
    }
    if (serialLength >= sizeof(serialLine) - 1) {
      serialLength = 0;
      continue;
    }
    serialLine[serialLength++] = incoming;
  }
}

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("ESP32 UART0 phase1 bridge boot");

  pulseResetIfEnabled();
  buildTopics();
  linkSerial.begin(config::LINK_UART_BAUD, SERIAL_8N1, config::LINK_UART_RX_PIN, config::LINK_UART_TX_PIN);

  mqttClient.setServer(config::MQTT_HOST, config::MQTT_PORT);
  mqttClient.setCallback(mqttCallback);

  ensureWifiConnected();
  ensureMqttConnected();
  sendRobotStop("boot");
}

void loop() {
  ensureWifiConnected();
  ensureMqttConnected();

  mqttClient.loop();
  serviceRobotSerial();
  handleOwnerLease();
  handleLinkFailSafe();

  if (mqttClient.connected() && millis() - lastStatusPublishAt >= 1000) {
    lastStatusPublishAt = millis();
    publishStatus(ownerExpired() ? "idle" : "claimed", "heartbeat");
  }

  if (millis() - lastPingAt >= 3000) {
    lastPingAt = millis();
    writeRobotLine("PING");
  }
}
