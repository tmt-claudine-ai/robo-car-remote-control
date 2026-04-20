#include <Arduino.h>

namespace {
const long BRIDGE_BAUD = 115200;
const char *RUNTIME_ID = "nano_phase1_uart0_runtime_v2";

const uint8_t LEFT_FRONT_DIR_PIN = 7;
const uint8_t LEFT_FRONT_PWM_PIN = 9;
const uint8_t LEFT_FRONT_DIR_FORWARD = 0;
const uint8_t LEFT_FRONT_DIR_REVERSE = 1;

const uint8_t LEFT_BACK_DIR_PIN = 8;
const uint8_t LEFT_BACK_PWM_PIN = 10;
const uint8_t LEFT_BACK_DIR_FORWARD = 0;
const uint8_t LEFT_BACK_DIR_REVERSE = 1;

const uint8_t RIGHT_FRONT_DIR_PIN = 13;
const uint8_t RIGHT_FRONT_PWM_PIN = 3;
const uint8_t RIGHT_FRONT_DIR_FORWARD = 1;
const uint8_t RIGHT_FRONT_DIR_REVERSE = 0;

const uint8_t RIGHT_BACK_DIR_PIN = 12;
const uint8_t RIGHT_BACK_PWM_PIN = 11;
const uint8_t RIGHT_BACK_DIR_FORWARD = 1;
const uint8_t RIGHT_BACK_DIR_REVERSE = 0;

const unsigned long TELEMETRY_INTERVAL_MS = 250;
const unsigned long MIN_COMMAND_TTL_MS = 120;
const unsigned long MAX_COMMAND_TTL_MS = 1000;
const size_t INPUT_BUFFER_SIZE = 96;

enum RuntimeState : uint8_t {
  STATE_IDLE = 0,
  STATE_ACTIVE,
  STATE_TIMEOUT,
  STATE_PARSE_ERROR
};

char inputBuffer[INPUT_BUFFER_SIZE];
size_t inputLength = 0;

int currentLeftSpeed = 0;
int currentRightSpeed = 0;
unsigned long lastCommandAt = 0;
unsigned long commandTtlMs = 250;
unsigned long lastTelemetryAt = 0;
RuntimeState runtimeState = STATE_IDLE;
}

void writeMotorChannel(uint8_t dirPin, uint8_t pwmPin, uint8_t forwardDir, uint8_t reverseDir, int pwm) {
  int bounded = constrain(pwm, -255, 255);
  if (bounded > 0) {
    digitalWrite(dirPin, forwardDir);
    analogWrite(pwmPin, bounded);
  } else if (bounded < 0) {
    digitalWrite(dirPin, reverseDir);
    analogWrite(pwmPin, -bounded);
  } else {
    analogWrite(pwmPin, 0);
  }
}

void applyDrive(int leftSpeed, int rightSpeed) {
  currentLeftSpeed = constrain(leftSpeed, -255, 255);
  currentRightSpeed = constrain(rightSpeed, -255, 255);

  writeMotorChannel(LEFT_FRONT_DIR_PIN, LEFT_FRONT_PWM_PIN, LEFT_FRONT_DIR_FORWARD, LEFT_FRONT_DIR_REVERSE, currentLeftSpeed);
  writeMotorChannel(LEFT_BACK_DIR_PIN, LEFT_BACK_PWM_PIN, LEFT_BACK_DIR_FORWARD, LEFT_BACK_DIR_REVERSE, currentLeftSpeed);
  writeMotorChannel(RIGHT_FRONT_DIR_PIN, RIGHT_FRONT_PWM_PIN, RIGHT_FRONT_DIR_FORWARD, RIGHT_FRONT_DIR_REVERSE, currentRightSpeed);
  writeMotorChannel(RIGHT_BACK_DIR_PIN, RIGHT_BACK_PWM_PIN, RIGHT_BACK_DIR_FORWARD, RIGHT_BACK_DIR_REVERSE, currentRightSpeed);
}

void stopDrive() {
  applyDrive(0, 0);
}

void sendLine(const char *line) {
  Serial.println(line);
}

void sendAck(long seq) {
  char line[24];
  snprintf(line, sizeof(line), "ACK,%ld", seq);
  sendLine(line);
}

void sendError(const char *reason) {
  char line[32];
  snprintf(line, sizeof(line), "ERR,%s", reason);
  sendLine(line);
}

void sendTelemetry() {
  const char *runtimeLabel =
      runtimeState == STATE_ACTIVE      ? "ACTIVE_V2"
      : runtimeState == STATE_TIMEOUT   ? "TIMEOUT_V2"
      : runtimeState == STATE_PARSE_ERROR ? "PARSE_ERROR_V2"
                                          : "IDLE_V2";

  char line[64];
  snprintf(
      line,
      sizeof(line),
      "TEL,%lu,%d,%d,%s",
      millis(),
      currentLeftSpeed,
      currentRightSpeed,
      runtimeLabel);
  sendLine(line);
}

bool parseLongToken(char *token, long &outValue) {
  if (token == NULL || *token == '\0') {
    return false;
  }

  char *endPtr = NULL;
  outValue = strtol(token, &endPtr, 10);
  return endPtr != token && *endPtr == '\0';
}

void handleCommand(char *line) {
  char *command = strtok(line, ",");
  if (command == NULL) {
    runtimeState = STATE_PARSE_ERROR;
    sendError("empty");
    return;
  }

  if (strcmp(command, "CMD") == 0) {
    long seq = 0;
    long left = 0;
    long right = 0;
    long ttl = 0;

    if (!parseLongToken(strtok(NULL, ","), seq) ||
        !parseLongToken(strtok(NULL, ","), left) ||
        !parseLongToken(strtok(NULL, ","), right) ||
        !parseLongToken(strtok(NULL, ","), ttl)) {
      runtimeState = STATE_PARSE_ERROR;
      sendError("bad_cmd");
      return;
    }

    commandTtlMs = constrain(ttl, MIN_COMMAND_TTL_MS, MAX_COMMAND_TTL_MS);
    lastCommandAt = millis();
    applyDrive((int)left, (int)right);
    runtimeState = (currentLeftSpeed == 0 && currentRightSpeed == 0) ? STATE_IDLE : STATE_ACTIVE;
    sendAck(seq);
    return;
  }

  if (strcmp(command, "STOP") == 0) {
    stopDrive();
    runtimeState = STATE_IDLE;
    sendLine("ACK,STOP");
    return;
  }

  if (strcmp(command, "PING") == 0) {
    char pong[24];
    snprintf(pong, sizeof(pong), "PONG,%lu", millis());
    sendLine(pong);
    return;
  }

  runtimeState = STATE_PARSE_ERROR;
  sendError("unknown");
}

void serviceSerial() {
  while (Serial.available() > 0) {
    char incoming = (char)Serial.read();

    if (incoming == '\r') {
      continue;
    }

    if (incoming == '\n') {
      inputBuffer[inputLength] = '\0';
      if (inputLength > 0) {
        handleCommand(inputBuffer);
      }
      inputLength = 0;
      continue;
    }

    if (inputLength >= INPUT_BUFFER_SIZE - 1) {
      inputLength = 0;
      runtimeState = STATE_PARSE_ERROR;
      sendError("overflow");
      continue;
    }

    inputBuffer[inputLength++] = incoming;
  }
}

void setup() {
  pinMode(LEFT_FRONT_DIR_PIN, OUTPUT);
  pinMode(LEFT_FRONT_PWM_PIN, OUTPUT);
  pinMode(LEFT_BACK_DIR_PIN, OUTPUT);
  pinMode(LEFT_BACK_PWM_PIN, OUTPUT);
  pinMode(RIGHT_FRONT_DIR_PIN, OUTPUT);
  pinMode(RIGHT_FRONT_PWM_PIN, OUTPUT);
  pinMode(RIGHT_BACK_DIR_PIN, OUTPUT);
  pinMode(RIGHT_BACK_PWM_PIN, OUTPUT);

  stopDrive();

  Serial.begin(BRIDGE_BAUD);
  delay(100);

  char readyLine[48];
  snprintf(readyLine, sizeof(readyLine), "READY,%s", RUNTIME_ID);
  sendLine(readyLine);
}

void loop() {
  serviceSerial();

  if ((currentLeftSpeed != 0 || currentRightSpeed != 0) &&
      millis() - lastCommandAt > commandTtlMs) {
    stopDrive();
    runtimeState = STATE_TIMEOUT;
  }

  if (millis() - lastTelemetryAt >= TELEMETRY_INTERVAL_MS) {
    lastTelemetryAt = millis();
    sendTelemetry();
  }
}
