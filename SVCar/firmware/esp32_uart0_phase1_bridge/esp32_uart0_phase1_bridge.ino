// ESP32 UART0 Phase 1 Bridge — v3 (MQTT drive control + HTTPS firmware update)
//
// Libraries required (all in Arduino Library Manager):
//   1. MQTTPubSubClient  (by hideakitai)      — search "MQTTPubSubClient"
//   2. WebSockets        (by Markus Sattler)  — search "WebSockets"
//
// IMPORTANT: There are TWO different WebSocket libraries in the Manager:
//   - "WebSockets" by Markus Sattler          ← THIS ONE (provides WebSocketsClient.h)
//   - "ArduinoWebsockets" by Gil Maimon       ← WRONG (different API)
//
// For PlatformIO: lib_deps =
//   hideakitai/MQTTPubSubClient
//   links2004/WebSockets

#include <Arduino.h>
#include <HTTPClient.h>
#include <WebSocketsClient.h>  // MUST be included before MQTTPubSubClient.h
#include <MQTTPubSubClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <cstring>
#include <cstdlib>
#include <vector>
#include <mbedtls/sha256.h>

#if __has_include("config.h")
#include "config.h"
#else
#include "config.example.h"
#endif

namespace {
enum FirmwareUpdateStage {
  FW_STAGE_IDLE = 0,
  FW_STAGE_PREPARING,
  FW_STAGE_DOWNLOADING,
  FW_STAGE_VERIFYING,
  FW_STAGE_FLASHING,
  FW_STAGE_REBOOTING,
};

WebSocketsClient wsClient;
MQTTPubSubClient mqttClient;
HardwareSerial linkSerial(2);

char claimTopic[96];
char releaseTopic[96];
char cmdTopic[96];
char stopTopic[96];
char telemetryTopic[96];
char statusTopic[96];
char fwStartTopic[96];
char fwStatusTopic[96];
char fwLogTopic[96];
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

bool firmwareUpdateActive = false;
bool firmwareUpdatePending = false;
FirmwareUpdateStage firmwareUpdateStage = FW_STAGE_IDLE;
String firmwareJobId;
String firmwareControllerId;
String firmwareUrl;
String firmwareSha256;
uint32_t firmwareSizeBytes = 0;
uint32_t firmwareBaud = config::FW_FLASH_BAUD;
String lastStkErr;
}  // namespace

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

void serviceNetworkStack() {
  wsClient.loop();
  mqttClient.update();
}

String jsonEscape(const String &value) {
  String out;
  out.reserve(value.length() + 8);
  for (size_t i = 0; i < value.length(); i++) {
    const char ch = value[i];
    if (ch == '"') out += "\\\"";
    else if (ch == '\\') out += "\\\\";
    else if (ch == '\n') out += "\\n";
    else if (ch == '\r') out += "\\r";
    else if (ch == '\t') out += "\\t";
    else out += ch;
  }
  return out;
}

int skipWs(const String &s, int index) {
  while (index < (int)s.length()) {
    const char ch = s[index];
    if (ch != ' ' && ch != '\t' && ch != '\r' && ch != '\n') break;
    index++;
  }
  return index;
}

bool scanFindKey(const String &json, const String &key, int &outPos) {
  const String needle = String("\"") + key + "\"";
  bool inString = false;
  bool escape = false;

  for (int i = 0; i <= (int)json.length() - (int)needle.length(); i++) {
    const char ch = json[i];
    if (inString) {
      if (escape) escape = false;
      else if (ch == '\\') escape = true;
      else if (ch == '"') inString = false;
      continue;
    }

    if (ch == '"') {
      if (json.substring(i, i + needle.length()) == needle) {
        outPos = i + (int)needle.length();
        return true;
      }
      inString = true;
    }
  }

  return false;
}

bool jsonGetString(const String &json, const String &key, String &out) {
  int pos = 0;
  if (!scanFindKey(json, key, pos)) return false;

  pos = skipWs(json, pos);
  if (pos >= (int)json.length() || json[pos] != ':') return false;

  pos = skipWs(json, pos + 1);
  if (pos >= (int)json.length() || json[pos] != '"') return false;

  pos++;
  String value;
  bool escape = false;

  while (pos < (int)json.length()) {
    const char ch = json[pos++];
    if (escape) {
      escape = false;
      if (ch == 'n') value += '\n';
      else if (ch == 'r') value += '\r';
      else if (ch == 't') value += '\t';
      else value += ch;
      continue;
    }
    if (ch == '\\') {
      escape = true;
      continue;
    }
    if (ch == '"') {
      out = value;
      return true;
    }
    value += ch;
  }

  return false;
}

bool jsonGetNumber(const String &json, const String &key, long &out) {
  int pos = 0;
  if (!scanFindKey(json, key, pos)) return false;

  pos = skipWs(json, pos);
  if (pos >= (int)json.length() || json[pos] != ':') return false;

  pos = skipWs(json, pos + 1);
  const int start = pos;
  while (pos < (int)json.length()) {
    const char ch = json[pos];
    if ((ch >= '0' && ch <= '9') || ch == '-' || ch == '+') {
      pos++;
      continue;
    }
    break;
  }

  if (pos == start) return false;
  out = atol(json.substring(start, pos).c_str());
  return true;
}

void resetFirmwareJob() {
  firmwareJobId = "";
  firmwareControllerId = "";
  firmwareUrl = "";
  firmwareSha256 = "";
  firmwareSizeBytes = 0;
  firmwareBaud = config::FW_FLASH_BAUD;
  firmwareUpdateActive = false;
  firmwareUpdatePending = false;
  firmwareUpdateStage = FW_STAGE_IDLE;
}

void buildTopics() {
  snprintf(claimTopic, sizeof(claimTopic), "robot/%s/session/claim", config::ROBOT_ID);
  snprintf(releaseTopic, sizeof(releaseTopic), "robot/%s/session/release", config::ROBOT_ID);
  snprintf(cmdTopic, sizeof(cmdTopic), "robot/%s/cmd", config::ROBOT_ID);
  snprintf(stopTopic, sizeof(stopTopic), "robot/%s/cmd/stop", config::ROBOT_ID);
  snprintf(telemetryTopic, sizeof(telemetryTopic), "robot/%s/telemetry", config::ROBOT_ID);
  snprintf(statusTopic, sizeof(statusTopic), "robot/%s/status", config::ROBOT_ID);
  snprintf(fwStartTopic, sizeof(fwStartTopic), "robot/%s/fw/start", config::ROBOT_ID);
  snprintf(fwStatusTopic, sizeof(fwStatusTopic), "robot/%s/fw/status", config::ROBOT_ID);
  snprintf(fwLogTopic, sizeof(fwLogTopic), "robot/%s/fw/log", config::ROBOT_ID);
  snprintf(clientId, sizeof(clientId), "esp32-uart0-bridge-%s", config::ROBOT_ID);
}

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

void restoreRuntimeSerial() {
  linkSerial.flush();
  linkSerial.end();
  delay(20);
  linkSerial.begin(config::LINK_UART_BAUD, SERIAL_8N1, config::LINK_UART_RX_PIN, config::LINK_UART_TX_PIN);
  serialLength = 0;
}

void publishStatus(const char *state, const char *detail) {
  if (!mqttReady) return;

  char payload[256];
  const char *owner = ownerExpired() ? "" : ownerId;
  snprintf(payload, sizeof(payload),
           "{\"robotId\":\"%s\",\"state\":\"%s\",\"detail\":\"%s\",\"owner\":\"%s\",\"wifiRssi\":%d,\"mqttConnected\":%s}",
           config::ROBOT_ID, state, detail, owner, WiFi.RSSI(), mqttReady ? "true" : "false");
  mqttClient.publish(statusTopic, payload, true);
}

void publishFwStatusForJob(const String &jobId, const char *state, const char *detail,
                           int progress = -1, int writtenPages = -1, int totalPages = -1) {
  if (!mqttReady) return;

  String payload = "{\"robotId\":\"" + jsonEscape(String(config::ROBOT_ID)) + "\"";
  if (jobId.length() > 0) payload += ",\"jobId\":\"" + jsonEscape(jobId) + "\"";
  if (firmwareControllerId.length() > 0) payload += ",\"controllerId\":\"" + jsonEscape(firmwareControllerId) + "\"";
  payload += ",\"state\":\"" + jsonEscape(String(state)) + "\"";
  payload += ",\"detail\":\"" + jsonEscape(String(detail)) + "\"";
  if (progress >= 0) payload += ",\"progress\":" + String(progress);
  if (writtenPages >= 0) payload += ",\"writtenPages\":" + String(writtenPages);
  if (totalPages >= 0) payload += ",\"totalPages\":" + String(totalPages);
  payload += "}";

  mqttClient.publish(fwStatusTopic, payload.c_str());
}

void publishFwStatus(const char *state, const char *detail,
                     int progress = -1, int writtenPages = -1, int totalPages = -1) {
  publishFwStatusForJob(firmwareJobId, state, detail, progress, writtenPages, totalPages);
}

void publishFwLogForJob(const String &jobId, const char *level, const String &message) {
  if (!mqttReady) return;

  String payload = "{\"robotId\":\"" + jsonEscape(String(config::ROBOT_ID)) + "\"";
  if (jobId.length() > 0) payload += ",\"jobId\":\"" + jsonEscape(jobId) + "\"";
  payload += ",\"level\":\"" + jsonEscape(String(level)) + "\"";
  payload += ",\"message\":\"" + jsonEscape(message) + "\"}";

  mqttClient.publish(fwLogTopic, payload.c_str());
}

void publishFwLog(const char *level, const String &message) {
  publishFwLogForJob(firmwareJobId, level, message);
}

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

bool parseFirmwareStartPayload(const char *rawPayload, String &outError) {
  String payload(rawPayload);
  String jobId;
  String controllerId;
  String url;
  String sha256;
  String target;
  String format;
  long sizeBytes = 0;
  long baud = config::FW_FLASH_BAUD;

  if (!jsonGetString(payload, "jobId", jobId) || jobId.length() == 0) {
    outError = "missing_job_id";
    return false;
  }
  if (!jsonGetString(payload, "controllerId", controllerId) || controllerId.length() == 0) {
    outError = "missing_controller_id";
    firmwareJobId = jobId;
    return false;
  }
  if (!jsonGetString(payload, "url", url) || url.length() == 0) {
    firmwareJobId = jobId;
    outError = "missing_url";
    return false;
  }
  if (!jsonGetString(payload, "sha256", sha256) || sha256.length() != 64) {
    firmwareJobId = jobId;
    outError = "bad_sha256";
    return false;
  }
  if (!jsonGetNumber(payload, "sizeBytes", sizeBytes) || sizeBytes <= 0 ||
      sizeBytes > (long)config::FW_MAX_HEX_SIZE) {
    firmwareJobId = jobId;
    outError = "bad_size";
    return false;
  }
  if (!jsonGetString(payload, "target", target) || !target.equalsIgnoreCase("atmega328p")) {
    firmwareJobId = jobId;
    outError = "bad_target";
    return false;
  }
  if (!jsonGetString(payload, "format", format) || !format.equalsIgnoreCase("intel_hex")) {
    firmwareJobId = jobId;
    outError = "bad_format";
    return false;
  }
  if (jsonGetNumber(payload, "baud", baud)) {
    baud = max(1200L, min(2000000L, baud));
  }

  firmwareJobId = jobId;
  firmwareControllerId = controllerId;
  firmwareUrl = url;
  firmwareSha256 = sha256;
  firmwareSizeBytes = (uint32_t)sizeBytes;
  firmwareBaud = (uint32_t)baud;
  return true;
}

void forwardDriveCommand(long seq, int left, int right, uint32_t ttlMs) {
  lastSentLeft = slewToward(lastSentLeft, clampWheel(left), config::MAX_SLEW_STEP);
  lastSentRight = slewToward(lastSentRight, clampWheel(right), config::MAX_SLEW_STEP);
  char line[64];
  snprintf(line, sizeof(line), "CMD,%ld,%d,%d,%lu", seq, lastSentLeft, lastSentRight, (unsigned long)ttlMs);
  writeRobotLine(line);
}

int hexNibble(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

bool hexByteAt(const char *s, int index, uint8_t &out) {
  if (s[index] == '\0' || s[index + 1] == '\0') return false;
  const int hi = hexNibble(s[index]);
  const int lo = hexNibble(s[index + 1]);
  if (hi < 0 || lo < 0) return false;
  out = (uint8_t)((hi << 4) | lo);
  return true;
}

bool parseIntelHexToImage(const char *hexText, std::vector<uint8_t> &image, uint32_t flashSize) {
  image.assign(flashSize, 0xFF);
  uint32_t base = 0;
  int pos = 0;

  while (hexText[pos] != '\0') {
    while (hexText[pos] == '\r' || hexText[pos] == '\n' ||
           hexText[pos] == ' ' || hexText[pos] == '\t') {
      pos++;
    }
    if (hexText[pos] == '\0') break;
    if (hexText[pos] != ':') return false;

    uint8_t len = 0;
    uint8_t aHi = 0;
    uint8_t aLo = 0;
    uint8_t type = 0;
    if (!hexByteAt(hexText, pos + 1, len) ||
        !hexByteAt(hexText, pos + 3, aHi) ||
        !hexByteAt(hexText, pos + 5, aLo) ||
        !hexByteAt(hexText, pos + 7, type)) {
      return false;
    }

    const uint16_t addr16 = (uint16_t)((aHi << 8) | aLo);
    uint8_t sum = len + aHi + aLo + type;
    const int dataStart = pos + 9;

    for (int i = 0; i < len; i++) {
      uint8_t b = 0;
      if (!hexByteAt(hexText, dataStart + i * 2, b)) return false;
      sum += b;
      if (type == 0x00) {
        const uint32_t absAddr = base + addr16 + i;
        if (absAddr < flashSize) image[absAddr] = b;
      }
    }

    uint8_t chk = 0;
    if (!hexByteAt(hexText, dataStart + len * 2, chk)) return false;
    if ((uint8_t)(sum + chk) != 0) return false;

    if (type == 0x01) return true;
    if (type == 0x04 && len == 2) {
      uint8_t d0 = 0;
      uint8_t d1 = 0;
      if (!hexByteAt(hexText, dataStart, d0) || !hexByteAt(hexText, dataStart + 2, d1)) return false;
      base = ((uint32_t)d0 << 24) | ((uint32_t)d1 << 16);
    }

    pos = dataStart + len * 2 + 2;
  }

  return true;
}

bool computeSha256Hex(const std::vector<uint8_t> &data, String &outHex) {
  mbedtls_sha256_context ctx;
  mbedtls_sha256_init(&ctx);
  if (mbedtls_sha256_starts(&ctx, 0) != 0) {
    mbedtls_sha256_free(&ctx);
    return false;
  }
  if (!data.empty() && mbedtls_sha256_update(&ctx, data.data(), data.size()) != 0) {
    mbedtls_sha256_free(&ctx);
    return false;
  }

  unsigned char digest[32];
  if (mbedtls_sha256_finish(&ctx, digest) != 0) {
    mbedtls_sha256_free(&ctx);
    return false;
  }
  mbedtls_sha256_free(&ctx);

  char hex[65];
  for (int i = 0; i < 32; i++) {
    snprintf(hex + i * 2, sizeof(hex) - i * 2, "%02x", digest[i]);
  }
  hex[64] = '\0';
  outHex = String(hex);
  return true;
}

bool downloadFirmwareHex(std::vector<uint8_t> &rawHex, String &outErr) {
  publishFwLog("info", "starting firmware download");
  publishFwStatus("downloading", "starting", 0);

  HTTPClient http;
  WiFiClient plainClient;
  WiFiClientSecure secureClient;
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http.setTimeout((uint16_t)min((uint32_t)65000, config::FW_DOWNLOAD_TIMEOUT_MS));

  int httpCode = 0;
  if (firmwareUrl.startsWith("https://")) {
    if (config::FW_TLS_INSECURE) secureClient.setInsecure();
    if (!http.begin(secureClient, firmwareUrl)) {
      outErr = "download_begin_failed";
      return false;
    }
    httpCode = http.GET();
  } else {
    if (!http.begin(plainClient, firmwareUrl)) {
      outErr = "download_begin_failed";
      return false;
    }
    httpCode = http.GET();
  }

  if (httpCode != HTTP_CODE_OK) {
    outErr = "download_http_" + String(httpCode);
    http.end();
    return false;
  }

  const int contentLength = http.getSize();
  if (firmwareSizeBytes > 0 && contentLength > 0 && (uint32_t)contentLength != firmwareSizeBytes) {
    outErr = "size_mismatch";
    http.end();
    return false;
  }

  auto *stream = http.getStreamPtr();
  rawHex.clear();
  rawHex.reserve(firmwareSizeBytes > 0 ? firmwareSizeBytes + 1 : config::FW_MAX_HEX_SIZE);

  mbedtls_sha256_context ctx;
  mbedtls_sha256_init(&ctx);
  mbedtls_sha256_starts(&ctx, 0);

  uint8_t buffer[1024];
  size_t totalRead = 0;
  unsigned long lastDataAt = millis();
  int lastProgress = -1;

  while (http.connected() || stream->available() > 0) {
    serviceNetworkStack();

    const size_t available = stream->available();
    if (available == 0) {
      if (millis() - lastDataAt > config::FW_DOWNLOAD_TIMEOUT_MS) {
        outErr = "download_timeout";
        http.end();
        mbedtls_sha256_free(&ctx);
        return false;
      }
      delay(10);
      continue;
    }

    const size_t want = min(available, sizeof(buffer));
    const size_t readNow = stream->readBytes(buffer, want);
    if (readNow == 0) {
      if (millis() - lastDataAt > config::FW_DOWNLOAD_TIMEOUT_MS) {
        outErr = "download_timeout";
        http.end();
        mbedtls_sha256_free(&ctx);
        return false;
      }
      delay(10);
      continue;
    }

    lastDataAt = millis();
    totalRead += readNow;
    if (totalRead > config::FW_MAX_HEX_SIZE) {
      outErr = "hex_too_large";
      http.end();
      mbedtls_sha256_free(&ctx);
      return false;
    }

    rawHex.insert(rawHex.end(), buffer, buffer + readNow);
    mbedtls_sha256_update(&ctx, buffer, readNow);

    if (firmwareSizeBytes > 0) {
      const int progress = (int)((totalRead * 100UL) / firmwareSizeBytes);
      if (progress != lastProgress) {
        lastProgress = progress;
        publishFwStatus("downloading", "receiving", progress);
      }
    }
  }

  http.end();

  if (firmwareSizeBytes > 0 && totalRead != firmwareSizeBytes) {
    outErr = "size_mismatch";
    mbedtls_sha256_free(&ctx);
    return false;
  }

  unsigned char digest[32];
  if (mbedtls_sha256_finish(&ctx, digest) != 0) {
    outErr = "sha256_finish_failed";
    mbedtls_sha256_free(&ctx);
    return false;
  }
  mbedtls_sha256_free(&ctx);

  char hex[65];
  for (int i = 0; i < 32; i++) {
    snprintf(hex + i * 2, sizeof(hex) - i * 2, "%02x", digest[i]);
  }
  hex[64] = '\0';
  const String actualSha256(hex);

  if (!firmwareSha256.equalsIgnoreCase(actualSha256)) {
    outErr = "sha256_mismatch";
    return false;
  }

  rawHex.push_back('\0');
  publishFwStatus("downloaded", "complete", 100);
  publishFwLog("info", "firmware download complete");
  return true;
}

void stkFlushInput() {
  int safeguard = 0;
  while (linkSerial.available() > 0 && safeguard++ < 500) {
    linkSerial.read();
  }
}

bool stkExpectInSyncOk(uint32_t timeoutMs) {
  uint8_t b = 0;
  const uint32_t startMs = millis();
  bool found14 = false;
  lastStkErr = "";

  while (millis() - startMs < timeoutMs) {
    if (linkSerial.available() > 0) {
      b = (uint8_t)linkSerial.read();
      if (b == 0x14) {
        found14 = true;
        break;
      }
      if (lastStkErr.length() < 50) lastStkErr += String(b, HEX) + " ";
    } else {
      delay(1);
    }
  }

  if (!found14) {
    lastStkErr += "(NO_14)";
    return false;
  }

  while (millis() - startMs < timeoutMs) {
    if (linkSerial.available() > 0) {
      b = (uint8_t)linkSerial.read();
      if (b == 0x10) return true;
      if (lastStkErr.length() < 50) lastStkErr += "(N10:" + String(b, HEX) + ")";
      return false;
    } else {
      delay(1);
    }
  }

  lastStkErr += "(NO_10)";
  return false;
}

bool stkCmd0(uint8_t cmd, uint32_t timeoutMs) {
  const uint8_t buf[2] = {cmd, 0x20};
  linkSerial.write(buf, sizeof(buf));
  linkSerial.flush();
  delay(2);
  return stkExpectInSyncOk(timeoutMs);
}

bool stkCmdLoadAddress(uint16_t wordAddr, uint32_t timeoutMs) {
  const uint8_t buf[4] = {0x55, (uint8_t)(wordAddr & 0xFF), (uint8_t)((wordAddr >> 8) & 0xFF), 0x20};
  linkSerial.write(buf, sizeof(buf));
  linkSerial.flush();
  delay(2);
  return stkExpectInSyncOk(timeoutMs);
}

bool flashWithStk500v1(const std::vector<uint8_t> &image, uint32_t flashSize, uint16_t pageSize,
                       uint32_t baud, int resetPin, String &outErr) {
  outErr = "";
  publishFwStatus("flashing", "resetting_target", 0);
  publishFwLog("info", "starting stk500 flashing sequence");

  linkSerial.flush();
  linkSerial.end();
  delay(20);
  linkSerial.begin(baud, SERIAL_8N1, config::LINK_UART_RX_PIN, config::LINK_UART_TX_PIN);

  bool synced = false;
  if (resetPin >= 0) {
    digitalWrite(resetPin, LOW);
    pinMode(resetPin, OUTPUT);
    digitalWrite(resetPin, HIGH);
    delay(50);
    stkFlushInput();
    linkSerial.flush();
    digitalWrite(resetPin, LOW);
    delay(400);
  }

  publishFwStatus("flashing", "syncing_bootloader", 0);
  for (int attempt = 0; attempt < 30; attempt++) {
    stkFlushInput();
    linkSerial.write((uint8_t)0x30);
    linkSerial.write((uint8_t)0x20);
    linkSerial.flush();
    if (stkExpectInSyncOk(40)) {
      synced = true;
      break;
    }
    delay(10);
  }

  if (!synced) {
    outErr = "stk_sync_failed";
    return false;
  }

  publishFwLog("info", "stk sync ok");
  delay(20);
  stkFlushInput();
  stkCmd0(0x50, 200);
  stkFlushInput();

  int totalPages = 0;
  for (uint32_t addr = 0; addr < flashSize; addr += pageSize) {
    bool any = false;
    for (uint16_t i = 0; i < pageSize; i++) {
      if (image[addr + i] != 0xFF) {
        any = true;
        break;
      }
    }
    if (any) totalPages++;
  }
  if (totalPages == 0) {
    outErr = "empty_image";
    return false;
  }

  std::vector<uint8_t> page(pageSize);
  int writtenPages = 0;
  int lastProgress = -1;

  for (uint32_t addr = 0; addr < flashSize; addr += pageSize) {
    serviceNetworkStack();

    bool any = false;
    for (uint16_t i = 0; i < pageSize; i++) {
      page[i] = image[addr + i];
      if (page[i] != 0xFF) any = true;
    }
    if (!any) continue;

    const uint16_t wordAddr = (uint16_t)((addr / 2) & 0xFFFF);
    if (!stkCmdLoadAddress(wordAddr, 200)) {
      outErr = "stk_load_address_failed";
      return false;
    }

    linkSerial.write((uint8_t)0x64);
    linkSerial.write((uint8_t)((pageSize >> 8) & 0xFF));
    linkSerial.write((uint8_t)(pageSize & 0xFF));
    linkSerial.write((uint8_t)'F');

    for (uint16_t i = 0; i < pageSize; i++) {
      linkSerial.write(page[i]);
      if ((i & 31) == 31) {
        linkSerial.flush();
        delay(1);
      }
    }

    linkSerial.write((uint8_t)0x20);
    linkSerial.flush();

    if (!stkExpectInSyncOk(500)) {
      outErr = "stk_page_write_failed";
      return false;
    }

    writtenPages++;
    const int progress = (writtenPages * 100) / totalPages;
    if (progress != lastProgress) {
      lastProgress = progress;
      publishFwStatus("flashing", "writing_pages", progress, writtenPages, totalPages);
    }
  }

  stkCmd0(0x51, 200);
  publishFwLog("info", "stk flashing complete");
  if (resetPin >= 0) digitalWrite(resetPin, LOW);
  return true;
}

void handleClaimMessage(const char *rawPayload) {
  if (firmwareUpdateActive || firmwareUpdatePending) {
    publishStatus("busy", "firmware_update_active");
    return;
  }

  char payload[128];
  strncpy(payload, rawPayload, sizeof(payload) - 1);
  payload[sizeof(payload) - 1] = '\0';

  char requestedOwner[32];
  long leaseMs = 0;
  if (!parseClaimPayload(payload, requestedOwner, sizeof(requestedOwner), leaseMs)) {
    publishStatus("error", "bad_claim_payload");
    return;
  }
  const uint32_t boundedLease = clampUInt32(leaseMs, config::MIN_OWNER_LEASE_MS, config::MAX_OWNER_LEASE_MS);
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
  if (firmwareUpdateActive || firmwareUpdatePending) {
    publishStatus("busy", "firmware_update_active");
    return;
  }

  if (ownerExpired()) {
    clearOwner();
    return;
  }
  if (strcmp(ownerId, rawPayload) == 0) {
    sendRobotStop("release");
    clearOwner();
    publishStatus("idle", "released");
  }
}

void handleStopMessage(const char *rawPayload) {
  if (firmwareUpdateActive || firmwareUpdatePending) {
    publishStatus("busy", "firmware_update_active");
    return;
  }

  if (ownerMatches(rawPayload)) {
    sendRobotStop("mqtt_stop");
    publishStatus("claimed", "stop");
  }
}

void handleCmdMessage(const char *rawPayload) {
  if (firmwareUpdateActive || firmwareUpdatePending) {
    publishStatus("busy", "firmware_update_active");
    return;
  }

  char payload[128];
  strncpy(payload, rawPayload, sizeof(payload) - 1);
  payload[sizeof(payload) - 1] = '\0';

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
  const uint32_t boundedTtl = clampUInt32(ttlMs, config::MIN_COMMAND_TTL_MS, config::MAX_COMMAND_TTL_MS);
  forwardDriveCommand(seq, (int)left, (int)right, boundedTtl);
}

void handleFwStartMessage(const char *rawPayload) {
  if (firmwareUpdateActive || firmwareUpdatePending) {
    publishFwStatusForJob(firmwareJobId, "busy", "already_flashing");
    return;
  }

  resetFirmwareJob();

  String parseError;
  if (!parseFirmwareStartPayload(rawPayload, parseError)) {
    publishFwStatusForJob(firmwareJobId, "error", parseError.c_str());
    publishFwLogForJob(firmwareJobId, "error", "invalid fw/start payload: " + parseError);
    resetFirmwareJob();
    return;
  }

  if (!ownerExpired() && strcmp(ownerId, firmwareControllerId.c_str()) != 0) {
    publishFwStatus("busy", "owner_mismatch");
    publishFwLog("error", "firmware update rejected because another controller owns the robot");
    resetFirmwareJob();
    return;
  }

  firmwareUpdateActive = true;
  firmwareUpdatePending = true;
  firmwareUpdateStage = FW_STAGE_PREPARING;
  publishFwStatus("accepted", "queued", 0);
  publishFwLog("info", "firmware job accepted");
  publishStatus("updating", "firmware_update_queued");
}

void dispatchMqttMessage(const char *topic, const char *rawPayload, const size_t size) {
  std::vector<char> payload(size + 1, '\0');
  if (size > 0) memcpy(payload.data(), rawPayload, size);

  if (size > 160) Serial.printf("[mqtt] %s (%u bytes)\n", topic, (unsigned)size);
  else Serial.printf("[mqtt] %s => %s\n", topic, payload.data());

  if (strcmp(topic, fwStartTopic) == 0)   { handleFwStartMessage(payload.data()); return; }
  if (strcmp(topic, claimTopic) == 0)     { handleClaimMessage(payload.data()); return; }
  if (strcmp(topic, releaseTopic) == 0)   { handleReleaseMessage(payload.data()); return; }
  if (strcmp(topic, cmdTopic) == 0)       { handleCmdMessage(payload.data()); return; }
  if (strcmp(topic, stopTopic) == 0)      { handleStopMessage(payload.data()); return; }
}

void mqttConnect() {
  if (mqttClient.isConnected()) return;
  if (millis() - lastReconnectAttemptAt < 2000) return;
  lastReconnectAttemptAt = millis();

  Serial.println("[mqtt] connecting ...");

  bool connected = false;
  if (config::MQTT_USERNAME[0] != '\0') {
    connected = mqttClient.connect(clientId, config::MQTT_USERNAME, config::MQTT_PASSWORD);
  } else {
    connected = mqttClient.connect(clientId);
  }
  if (!connected) {
    Serial.println("[mqtt] connect failed");
    return;
  }

  mqttClient.subscribe(claimTopic, [](const char *payload, const size_t size) {
    dispatchMqttMessage(claimTopic, payload, size);
  });
  mqttClient.subscribe(releaseTopic, [](const char *payload, const size_t size) {
    dispatchMqttMessage(releaseTopic, payload, size);
  });
  mqttClient.subscribe(cmdTopic, [](const char *payload, const size_t size) {
    dispatchMqttMessage(cmdTopic, payload, size);
  });
  mqttClient.subscribe(stopTopic, [](const char *payload, const size_t size) {
    dispatchMqttMessage(stopTopic, payload, size);
  });
  mqttClient.subscribe(fwStartTopic, [](const char *payload, const size_t size) {
    dispatchMqttMessage(fwStartTopic, payload, size);
  });

  mqttClient.publish(statusTopic, "{\"state\":\"online\",\"detail\":\"mqtt_connected\"}", true);
  mqttReady = true;
  linkFailSafeIssued = false;
  Serial.println("[mqtt] connected");
}

void ensureWifiConnected() {
  if (WiFi.status() == WL_CONNECTED) return;

  Serial.printf("[wifi] connecting to %s ...\n", config::WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.begin(config::WIFI_SSID, config::WIFI_PASSWORD);

  const unsigned long t0 = millis();
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

void handleOwnerLease() {
  if (firmwareUpdateActive || firmwareUpdatePending) return;

  if (ownerExpired() && ownerId[0] != '\0') {
    sendRobotStop("lease_expired");
    clearOwner();
    publishStatus("idle", "lease_expired");
  }
}

void handleLinkFailSafe() {
  if (firmwareUpdateActive || firmwareUpdatePending) return;

  const bool linkHealthy = WiFi.status() == WL_CONNECTED && mqttReady;
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
  Serial.printf("[robot] %s\n", line);
  if (!mqttReady || firmwareUpdateActive || firmwareUpdatePending) return;

  if (strncmp(line, "TEL,", 4) == 0) {
    strtok(line, ",");
    char *uptime = strtok(NULL, ",");
    char *left = strtok(NULL, ",");
    char *right = strtok(NULL, ",");
    char *state = strtok(NULL, ",");
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
  if (firmwareUpdateActive || firmwareUpdatePending) return;

  while (linkSerial.available() > 0) {
    const char incoming = (char)linkSerial.read();
    if (incoming == '\r') continue;
    if (incoming == '\n') {
      serialLine[serialLength] = '\0';
      if (serialLength > 0) handleRobotSerialLine(serialLine);
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

bool performPendingFirmwareUpdate(String &outErr) {
  firmwareUpdatePending = false;
  firmwareUpdateStage = FW_STAGE_PREPARING;
  publishFwStatus("accepted", "starting", 0);
  publishStatus("updating", "firmware_update_starting");

  clearOwner();
  sendRobotStop("fw_update");
  delay(50);

  firmwareUpdateStage = FW_STAGE_DOWNLOADING;
  std::vector<uint8_t> rawHex;
  if (!downloadFirmwareHex(rawHex, outErr)) return false;

  firmwareUpdateStage = FW_STAGE_VERIFYING;
  publishFwStatus("verifying", "parsing_hex", 100);
  publishFwLog("info", "verifying and parsing intel hex");

  std::vector<uint8_t> image;
  if (!parseIntelHexToImage((const char *)rawHex.data(), image, config::FW_FLASH_SIZE_BYTES)) {
    outErr = "bad_hex";
    return false;
  }
  rawHex.clear();
  rawHex.shrink_to_fit();

  firmwareUpdateStage = FW_STAGE_FLASHING;
  publishStatus("updating", "firmware_flashing");
  if (!flashWithStk500v1(image, config::FW_FLASH_SIZE_BYTES, config::FW_FLASH_PAGE_SIZE,
                         firmwareBaud, config::LINK_RESET_PIN, outErr)) {
    return false;
  }

  firmwareUpdateStage = FW_STAGE_REBOOTING;
  publishFwStatus("rebooting", "restoring_runtime_uart", 100);
  publishFwLog("info", "restoring runtime uart configuration");
  restoreRuntimeSerial();
  delay(250);
  return true;
}

void finalizeFirmwareUpdate(bool ok, const String &detail) {
  restoreRuntimeSerial();
  serialLength = 0;
  lastPingAt = millis();
  linkFailSafeIssued = false;

  if (ok) {
    publishFwStatus("done", "complete", 100);
    publishFwLog("info", "firmware update complete");
    publishStatus("idle", "firmware_update_complete");
  } else {
    publishFwStatus("error", detail.c_str());
    publishFwLog("error", detail);
    publishStatus("error", "firmware_update_failed");
  }

  resetFirmwareJob();
}

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("=== ESP32 UART0 bridge v3 (MQTT + HTTPS firmware update) ===");

  pinMode(config::LINK_RESET_PIN, OUTPUT);
  digitalWrite(config::LINK_RESET_PIN, LOW);
  if (config::PULSE_RESET_ON_BOOT) {
    digitalWrite(config::LINK_RESET_PIN, HIGH);
    delay(50);
    digitalWrite(config::LINK_RESET_PIN, LOW);
    delay(400);
  }

  buildTopics();
  restoreRuntimeSerial();
  ensureWifiConnected();

  if (config::MQTT_USE_SSL) {
    wsClient.beginSSL(config::MQTT_HOST, config::MQTT_PORT, config::MQTT_WS_PATH, "", "mqtt");
  } else {
    wsClient.begin(config::MQTT_HOST, config::MQTT_PORT, config::MQTT_WS_PATH, "mqtt");
  }
  wsClient.setReconnectInterval(2000);

  mqttClient.begin(wsClient);
  sendRobotStop("boot");
}

void loop() {
  ensureWifiConnected();
  serviceNetworkStack();

  if (!mqttReady && WiFi.status() == WL_CONNECTED) {
    mqttConnect();
  }

  if (mqttReady && !mqttClient.isConnected()) {
    Serial.println("[mqtt] disconnected");
    mqttReady = false;
  }

  if (firmwareUpdatePending) {
    String updateError;
    const bool ok = performPendingFirmwareUpdate(updateError);
    finalizeFirmwareUpdate(ok, updateError);
    return;
  }

  serviceRobotSerial();
  handleOwnerLease();
  handleLinkFailSafe();

  if (mqttReady && millis() - lastStatusPublishAt >= 1000) {
    lastStatusPublishAt = millis();
    if (firmwareUpdateActive) publishStatus("updating", "firmware_update_active");
    else publishStatus(ownerExpired() ? "idle" : "claimed", "heartbeat");
  }

  if (!firmwareUpdateActive && millis() - lastPingAt >= 3000) {
    lastPingAt = millis();
    writeRobotLine("PING");
  }
}
