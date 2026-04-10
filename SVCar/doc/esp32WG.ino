#include <WiFi.h>
#include <Preferences.h>
#include <mbedtls/base64.h>
#include <vector>
#include <ESPAsyncWebServer.h>

static const char *MAMA_AP_SSID = "MAMA-GW";
static const char *MAMA_AP_PASS = "12345678";
static const uint16_t MAMA_HTTP_PORT = 20180;
static const char *MAMA_WS_PATH = "/ws";

static const char *MAMA_PREF_NS = "mama";
static const char *MAMA_PREF_STA_SSID = "UINO-IoT";
static const char *MAMA_PREF_STA_PASS = "Thing@123";

static const char *MAMA_DEVICE_ID = "mama-328pb-001";
static const char *MAMA_CAPS_HASH = "328pb-uart";

static const int LINK_UART_NUM = 2;
static const int LINK_UART_RX = 6;
static const int LINK_UART_TX = 7;
static const uint32_t LINK_UART_BAUD = 115200;

static const int LINK_RESET_PIN = 4;
static const uint16_t LINK_REPLY_TIMEOUT_MS = 1200;

AsyncWebServer server(MAMA_HTTP_PORT);
AsyncWebSocket ws(MAMA_WS_PATH);
HardwareSerial LinkSerial(LINK_UART_NUM);
Preferences prefs;

// ==========================================
// 全局固件缓存
// ==========================================
std::vector<uint8_t> cachedFirmwareImage(32768, 0xFF);
bool hasCachedFirmware = false;

// ==========================================
// 基础工具与 JSON 解析
// ==========================================
static uint32_t nowMs() {
  return millis();
}

static String trimSpaces(const String &s) {
  int i = 0, j = (int)s.length() - 1;
  while (i <= j && isspace(s[i])) i++;
  while (j >= i && isspace(s[j])) j--;
  if (j < i) return String();
  return s.substring(i, j + 1);
}

static bool startsWith(const String &s, const String &prefix) {
  if (s.length() < prefix.length()) return false;
  return s.substring(0, prefix.length()) == prefix;
}

static String jsonEscape(const String &s) {
  String out; out.reserve(s.length() + 8);
  for (size_t i = 0; i < s.length(); i++) {
    const char ch = s[i];
    if (ch == '"') out += "\\\""; else if (ch == '\\') out += "\\\\";
    else if (ch == '\n') out += "\\n"; else if (ch == '\r') out += "\\r";
    else if (ch == '\t') out += "\\t"; else out += ch;
  }
  return out;
}

static bool scanFindKey(const String &json, const String &key, int &outPos) {
  const String needle = String("\"") + key + "\"";
  bool inString = false, escape = false;
  for (int i = 0; i <= (int)json.length() - (int)needle.length(); i++) {
    const char ch = json[i];
    if (inString) {
      if (escape) escape = false;
      else if (ch == '\\') escape = true; else if (ch == '"') inString = false;
      continue;
    }
    if (ch == '"') {
      if (json.substring(i, i + needle.length()) == needle) {
        outPos = i + (int)needle.length();
        return true;
      }
      inString = true; continue;
    }
  }
  return false;
}

static int skipWs(const String &s, int i) {
  while (i < (int)s.length()) {
    const char ch = s[i];
    if (ch != ' ' && ch != '\t' && ch != '\r' && ch != '\n') break;
    i++;
  }
  return i;
}

static bool jsonGetString(const String &json, const String &key, String &out) {
  int pos = 0; if (!scanFindKey(json, key, pos)) return false;
  pos = skipWs(json, pos); if (pos >= (int)json.length() || json[pos] != ':') return false;
  pos = skipWs(json, pos + 1); if (pos >= (int)json.length() || json[pos] != '"') return false;
  pos++;
  int endQuote = json.indexOf('"', pos);
  if (endQuote > pos) {
    String raw = json.substring(pos, endQuote);
    if (raw.indexOf('\\') < 0) {
      out = raw;
      return true;
    }
  }
  String v; bool escape = false;
  while (pos < (int)json.length()) {
    const char ch = json[pos++];
    if (escape) {
      escape = false;
      if (ch == 'n') v += '\n'; else if (ch == 'r') v += '\r';
      else if (ch == 't') v += '\t'; else v += ch;
      continue;
    }
    if (ch == '\\') {
      escape = true;
      continue;
    }
    if (ch == '"') {
      out = v;
      return true;
    }
    v += ch;
  }
  return false;
}

static bool jsonGetNumber(const String &json, const String &key, long &out) {
  int pos = 0; if (!scanFindKey(json, key, pos)) return false;
  pos = skipWs(json, pos); if (pos >= (int)json.length() || json[pos] != ':') return false;
  pos = skipWs(json, pos + 1); int start = pos;
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

// ==========================================
// 极限内存优化的 STK500 烧录引擎 (0 碎片分配)
// ==========================================
static bool base64Decode(const String &in, std::vector<uint8_t> &out) {
  out.clear();
  if (!in.length()) return true;
  size_t outLen = 0;
  int rc = mbedtls_base64_decode(nullptr, 0, &outLen, (const unsigned char *)in.c_str(), in.length());
  if (rc != MBEDTLS_ERR_BASE64_BUFFER_TOO_SMALL || outLen == 0) return false;
  out.resize(outLen);
  rc = mbedtls_base64_decode(out.data(), out.size(), &outLen, (const unsigned char *)in.c_str(), in.length());
  if (rc != 0) {
    out.clear();
    return false;
  }
  out.resize(outLen); return true;
}

static int hexNibble(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

static bool hexByteAt(const char *s, int i, uint8_t &out) {
  if (s[i] == '\0' || s[i + 1] == '\0') return false;
  int hi = hexNibble(s[i]);
  int lo = hexNibble(s[i + 1]);
  if (hi < 0 || lo < 0) return false;
  out = (uint8_t)((hi << 4) | lo);
  return true;
}

static bool parseIntelHexToImage(const char* hexText, std::vector<uint8_t> &image, uint32_t flashSize) {
  image.assign(flashSize, 0xFF);
  uint32_t base = 0; int pos = 0;

  while (hexText[pos] != '\0') {
    while (hexText[pos] == '\r' || hexText[pos] == '\n' || hexText[pos] == ' ' || hexText[pos] == '\t') pos++;
    if (hexText[pos] == '\0') break;
    if (hexText[pos] != ':') return false;

    uint8_t len, aHi, aLo, type;
    if (!hexByteAt(hexText, pos + 1, len) || !hexByteAt(hexText, pos + 3, aHi) ||
        !hexByteAt(hexText, pos + 5, aLo) || !hexByteAt(hexText, pos + 7, type)) return false;

    uint16_t addr16 = (uint16_t)((aHi << 8) | aLo);
    uint8_t sum = len + aHi + aLo + type;
    int dataStart = pos + 9;

    for (int i = 0; i < len; i++) {
      uint8_t b;
      if (!hexByteAt(hexText, dataStart + i * 2, b)) return false;
      sum += b;
      if (type == 0x00) {
        uint32_t absAddr = base + addr16 + i;
        if (absAddr < flashSize) image[absAddr] = b;
      }
    }
    uint8_t chk;
    if (!hexByteAt(hexText, dataStart + len * 2, chk)) return false;
    if ((uint8_t)(sum + chk) != 0) return false;

    if (type == 0x01) return true;
    if (type == 0x04 && len == 2) {
      uint8_t d0, d1;
      hexByteAt(hexText, dataStart, d0); hexByteAt(hexText, dataStart + 2, d1);
      base = ((uint32_t)d0 << 24) | ((uint32_t)d1 << 16);
    }
    pos = dataStart + len * 2 + 2;
  }
  return true;
}

// ==========================================
// 终极幽灵免疫与 NPN 自动复位 STK500 引擎
// ==========================================
static String lastStkErr = "";

static bool stkReadByte(uint8_t &out, uint32_t timeoutMs) {
  const uint32_t deadline = nowMs() + timeoutMs;
  while (nowMs() < deadline) {
    if (LinkSerial.available() > 0) {
      out = (uint8_t)LinkSerial.read();
      return true;
    }
    delay(1);
  }
  return false;
}

static void stkFlushInput() {
  int safeguard = 0;
  while (LinkSerial.available() > 0 && safeguard++ < 500) {
    LinkSerial.read();
  }
}

// 【终极防炸内存修复】：限制记录垃圾日志的长度，防止 UNO 疯狂输出脏数据撑爆 ESP32 内存
static bool stkExpectInSyncOk(uint32_t timeoutMs) {
  uint8_t b = 0;
  uint32_t startMs = millis();
  bool found14 = false;
  lastStkErr = "";

  while (millis() - startMs < timeoutMs) {
    if (LinkSerial.available() > 0) {
      b = (uint8_t)LinkSerial.read();
      if (b == 0x14) {
        found14 = true;
        break;
      }
      // 只要收集了前 50 个字符的证据就停手，防止内存爆炸
      else if (lastStkErr.length() < 50) {
        lastStkErr += String(b, HEX) + " ";
      }
    } else {
      delay(1);
    }
  }

  if (!found14) {
    lastStkErr += "(NO_14)";
    return false;
  }

  while (millis() - startMs < timeoutMs) {
    if (LinkSerial.available() > 0) {
      b = (uint8_t)LinkSerial.read();
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

static bool stkCmd0(uint8_t cmd, uint32_t timeoutMs) {
  const uint8_t buf[2] = {cmd, 0x20};
  LinkSerial.write(buf, sizeof(buf));
  LinkSerial.flush(); delay(2);
  return stkExpectInSyncOk(timeoutMs);
}

static bool stkCmdLoadAddress(uint16_t wordAddr, uint32_t timeoutMs) {
  const uint8_t buf[4] = {0x55, (uint8_t)(wordAddr & 0xFF), (uint8_t)((wordAddr >> 8) & 0xFF), 0x20};
  LinkSerial.write(buf, sizeof(buf));
  LinkSerial.flush(); delay(2);
  return stkExpectInSyncOk(timeoutMs);
}

static bool flashWithStk500v1(const std::vector<uint8_t> &image, uint32_t flashSize, uint16_t pageSize, uint32_t baud, int resetPin, String &outErr) {
  outErr = ""; Serial.println("[STK500] 启动全自动 NPN 复位烧录序列...");

  LinkSerial.flush(); LinkSerial.end(); delay(20);
  LinkSerial.begin(baud, SERIAL_8N1, LINK_UART_RX, LINK_UART_TX);

  bool synced = false;

// ========================================================
  // 【终极完美版】：NPN 三极管硬件复位 + Optiboot 防打扰时序
  // ========================================================
  if (resetPin >= 0) {
    digitalWrite(resetPin, LOW);
    pinMode(resetPin, OUTPUT);

    // 1. 强力按下复位键 (死死拉到 0V)
    digitalWrite(resetPin, HIGH);
    delay(50); // 保持按压 50ms，确保硬件彻底断电复位

    // 2. 【极其关键】：清空 ESP32 串口里可能残留的任何垃圾信号
    // 防止在松开复位键的瞬间，串口引脚产生电平波动干扰 UNO
    stkFlushInput(); 
    LinkSerial.flush(); 

    // 3. 松开复位键，让 UNO 苏醒 (拉回 5V)
    digitalWrite(resetPin, LOW);
    
    // 4. 【灵魂 400 毫秒】：保持绝对的寂静！
    // 此时 UNO 刚刚醒来，正在初始化内部硬件并准备闪烁 L 灯。
    // 我们绝不能在这个时候发数据，否则 UNO 会读到乱码并当场退出 Bootloader！
    delay(400); 
  }

  Serial.println("[STK500] 启动极速握手，与看门狗赛跑...");

  // 尝试 30 次，每次等约 50ms。总耗时 1.5 秒。
  // 既能完美抓准 UNO 张开耳朵的瞬间，又绝不会触发 ESP32 的 3 秒看门狗！
  for (int attempt = 0; attempt < 30; attempt++) {
    stkFlushInput();
    LinkSerial.write((uint8_t)0x30);
    LinkSerial.write((uint8_t)0x20);
    LinkSerial.flush();

    if (stkExpectInSyncOk(40)) {
      synced = true;
      break;
    }
    delay(10);
  }

  if (!synced) {
    outErr = "sync failed | DBG: " + lastStkErr;
    return false;
  }
  
  Serial.println("[STK500] ✅ 自动握手成功！");
  delay(20);
  stkFlushInput();

  stkCmd0(0x50, 200);
  stkFlushInput();

  std::vector<uint8_t> page(pageSize);
  int writeCount = 0;

  for (uint32_t addr = 0; addr < flashSize; addr += pageSize) {
    bool any = false;
    for (uint16_t i = 0; i < pageSize; i++) {
      uint8_t b = image[addr + i]; page[i] = b;
      if (b != 0xFF) any = true;
    }
    if (!any) continue;

    uint16_t wordAddr = (uint16_t)((addr / 2) & 0xFFFF);

    if (!stkCmdLoadAddress(wordAddr, 200)) {
      outErr = "load address failed at 0x" + String(addr, HEX) + " | DBG: " + lastStkErr;
      return false;
    }

    LinkSerial.write((uint8_t)0x64);
    LinkSerial.write((uint8_t)((pageSize >> 8) & 0xFF));
    LinkSerial.write((uint8_t)(pageSize & 0xFF));
    LinkSerial.write((uint8_t)'F');

    for (uint16_t i = 0; i < pageSize; i++) {
      LinkSerial.write(page[i]);
      // 防溢出机制：每 32 字节喘息 1ms
      if ((i & 31) == 31) {
        LinkSerial.flush();
        delay(1);
      }
    }

    LinkSerial.write((uint8_t)0x20);
    LinkSerial.flush();

    if (!stkExpectInSyncOk(500)) {
      outErr = "program page failed at 0x" + String(addr, HEX) + " | DBG: " + lastStkErr;
      return false;
    }
    writeCount++;
  }

  stkCmd0(0x51, 200);
  Serial.printf("[STK500] 🎉 全自动烧录彻底完成！共写入 %d 页数据。\n", writeCount);

  // 烧录完成后，确保持续输出 LOW，不干扰目标板运行
  if (resetPin >= 0) digitalWrite(resetPin, LOW);

  return true;
}

// ==========================================
// Async WebServer 路由配置
// ==========================================
void setupWebServers() {
  DefaultHeaders::Instance().addHeader("Access-Control-Allow-Origin", "*");
  DefaultHeaders::Instance().addHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
  DefaultHeaders::Instance().addHeader("Access-Control-Allow-Headers", "*");

  server.onNotFound([](AsyncWebServerRequest * request) {
    if (request->method() == HTTP_OPTIONS) request->send(200);
    else request->send(404, "application/json", "{\"error\":\"Not Found\"}");
  });

  server.on("/", HTTP_GET, [](AsyncWebServerRequest * request) {
    request->send(200, "text/plain", "mama-esp32-gateway");
  });

  server.on("/api/v1/health", HTTP_GET, [](AsyncWebServerRequest * request) {
    request->send(200, "application/json", "{\"ok\":true, \"hasFirmware\":" + String(hasCachedFirmware ? "true" : "false") + "}");
  });

  auto handleBody = [](AsyncWebServerRequest * request, uint8_t *data, size_t len, size_t index, size_t total) {
    if (!request->_tempObject) {
      request->_tempObject = new String();
      ((String*)request->_tempObject)->reserve(total + 1);
    }
    String* bodyStr = (String*)request->_tempObject;
    for (size_t i = 0; i < len; i++) *bodyStr += (char)data[i];
  };

  server.on("/api/v1/gw/upload_fw", HTTP_POST, [](AsyncWebServerRequest * request) {
    String* bodyStr = (String*)request->_tempObject;
    if (!bodyStr) {
      request->send(400, "application/json", "{\"ok\":false}");
      return;
    }

    String hexB64; jsonGetString(*bodyStr, "hexB64", hexB64);
    delete bodyStr; request->_tempObject = nullptr;

    if (!hexB64.length()) {
      request->send(400, "application/json", "{\"ok\":false,\"error\":\"missing hexB64\"}");
      return;
    }

    std::vector<uint8_t> decoded;
    if (!base64Decode(hexB64, decoded)) {
      request->send(400, "application/json", "{\"ok\":false,\"error\":\"bad base64\"}");
      return;
    }
    hexB64.clear(); decoded.push_back('\0');

    hasCachedFirmware = false;
    if (!parseIntelHexToImage((const char*)decoded.data(), cachedFirmwareImage, 32768)) {
      request->send(400, "application/json", "{\"ok\":false,\"error\":\"bad hex format\"}"); return;
    }

    hasCachedFirmware = true;
    request->send(200, "application/json", "{\"ok\":true, \"msg\":\"Firmware cached in RAM\"}");
  }, NULL, handleBody);

  server.on("/api/v1/gw/flash_local", HTTP_POST, [](AsyncWebServerRequest * request) {
    String* bodyStr = (String*)request->_tempObject;
    String body = bodyStr ? *bodyStr : "{}";
    if (bodyStr) {
      delete bodyStr;
      request->_tempObject = nullptr;
    }

    if (!hasCachedFirmware) {
      request->send(400, "application/json", "{\"ok\":false,\"error\":\"No firmware cached. Upload first.\"}");
      return;
    }

    long baud = 115200; jsonGetNumber(body, "baud", baud); baud = max(1200L, min(2000000L, baud));
    long resetPin = LINK_RESET_PIN; jsonGetNumber(body, "resetPin", resetPin);

    String err;
    bool ok = flashWithStk500v1(cachedFirmwareImage, 32768, 128, (uint32_t)baud, (int)resetPin, err);

    if (!ok) request->send(200, "application/json", "{\"ok\":false,\"error\":\"" + jsonEscape(err) + "\"}");
    else request->send(200, "application/json", "{\"ok\":true}");
  }, NULL, handleBody);

  // 处理前端发来的 WiFi 配置请求
    server.on("/api/v1/gw/wifi", HTTP_POST, [](AsyncWebServerRequest *request){
        String* bodyStr = (String*)request->_tempObject;
        String body = bodyStr ? *bodyStr : "{}";
        if (bodyStr) { delete bodyStr; request->_tempObject = nullptr; }

        // 解析前端发来的 JSON 里的 ssid 和 pwd (注意根据你实际的 json 解析逻辑调整)
        String ssid = "";
        String pwd = "";
        // 假设你代码里有类似 jsonGetString 的函数，如果没有，请换成你自己的 JSON 解析
        // jsonGetString(body, "ssid", ssid);
        // jsonGetString(body, "pwd", pwd); 

        // 临时粗暴解析法（如果你没写 JSON 库）：
        int ssidIdx = body.indexOf("\"ssid\":\"");
        if (ssidIdx > 0) {
            int endIdx = body.indexOf("\"", ssidIdx + 8);
            ssid = body.substring(ssidIdx + 8, endIdx);
        }
        int pwdIdx = body.indexOf("\"pwd\":\""); // 密码字段名以你前端为准，可能是 password
        if (pwdIdx > 0) {
            int endIdx = body.indexOf("\"", pwdIdx + 7);
            pwd = body.substring(pwdIdx + 7, endIdx);
        }

        Serial.printf("[HTTP] 收到新的 WiFi 配置请求, SSID: %s\n", ssid.c_str());

        if (ssid.length() == 0) {
            request->send(400, "application/json", "{\"ok\":false,\"error\":\"SSID 不能为空\"}");
            return;
        }

        // ==========================================
        // 在这里加入把你 WiFi 保存到 Preferences (NVS) 
        // 并尝试连接新 WiFi 的代码逻辑
        // ==========================================

        request->send(200, "application/json", "{\"ok\":true,\"message\":\"配置已保存\"}");
        
    }, NULL, handleBody);

  server.addHandler(&ws);
}

void setup() {
  // 确保引脚上电时是 LOW，防止目标板被意外复位
  pinMode(LINK_RESET_PIN, OUTPUT);
  digitalWrite(LINK_RESET_PIN, LOW);

  delay(50);
  Serial.begin(115200);
  WiFi.mode(WIFI_AP_STA);
  if (strlen(MAMA_AP_PASS) >= 8) WiFi.softAP(MAMA_AP_SSID, MAMA_AP_PASS);
  else WiFi.softAP(MAMA_AP_SSID);

  setupWebServers();
  server.begin();

  LinkSerial.begin(LINK_UART_BAUD, SERIAL_8N1, LINK_UART_RX, LINK_UART_TX);
}

void loop() {
  ws.cleanupClients();
}
