#include "TAI_finder_X1.h"
#include "PinChangeInt.h"

void left_coder() {
  static uint32_t left_coder_previous_time = micros();
  if(micros() - left_coder_previous_time >= 1000 && left_motion_state) {
    if(left_dir_state) {
      left_coder_count++;
    } else {
      left_coder_count--;
    }
    left_coder_previous_time = micros();
  }
}

void right_coder() {
  static uint32_t right_coder_previous_time = micros();
  if(micros() - right_coder_previous_time >= 1000 && right_motion_state) {
    if(right_dir_state) {
      right_coder_count++;
    } else {
      right_coder_count--;
    }
    right_coder_previous_time = micros();
  }
}

TAI_finder_X1::TAI_finder_X1() {
  
}

void TAI_finder_X1::init() {
  //Serial.begin(115200);
  delay(1000);
  pinMode(LEFT_FRONT_DIR_PIN, OUTPUT);
  pinMode(LEFT_FRONT_PWM_PIN, OUTPUT);
  pinMode(LEFT_BACK_DIR_PIN, OUTPUT);
  pinMode(LEFT_BACK_PWM_PIN, OUTPUT);
  pinMode(RIGHT_FRONT_DIR_PIN, OUTPUT);
  pinMode(RIGHT_FRONT_PWM_PIN, OUTPUT);
  pinMode(RIGHT_BACK_DIR_PIN, OUTPUT);
  pinMode(RIGHT_BACK_PWM_PIN, OUTPUT);
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  attachInterrupt(digitalPinToInterrupt(RIGHT_INT_PIN), right_coder, CHANGE);
  PCintPort::attachInterrupt(LEFT_INT_PIN, left_coder, CHANGE);
  LR_Strip.begin();
  LR_Strip.show();
  Wire.begin();
  Wire.setWireTimeout(30000,true);
  delay(1000);
  uint8_t lcd_address;
  if(Wire.endTransmission() == 0) {
    lcd_address = 0x3F;
  } else {
    Wire.beginTransmission(0x27);
    if(Wire.endTransmission() == 0) {
      lcd_address = 0x27;
    } else {
      Wire.beginTransmission(0x22);
      if(Wire.endTransmission() == 0) {
        lcd_address = 0x22;
      }
    }
  }
  lcd.set_addr(lcd_address);
  lcd.init();
  lcd.backlight();
  AccGyr_init();
  String check = "";
  for(int i = 0; i < 3; i++) {
    check += (char)EEPROM.read(200 + i);
  }
  if(check == "TAI") {
    delta = EEPROM.read(214) << 8 | EEPROM.read(213);
  }
  delay(100);
  pcf.begin() ;
  delay(100) ;
  pcf.enableDAC(true);
  delay(100);
  pcf2.begin() ;
  delay(100) ;
  pcf2.enableDAC(true);
  pcf.analogWrite(255);
  pcf2.analogWrite(255);
  get_initialize_gray();
}

void TAI_finder_X1::matrix_show(uint8_t *a, uint8_t *b, boolean state) {
  if(state) {
    matrix.clear();
  }
  matrix.drawBitmap(0, 0, a, 8, 8, LED_ON);
  matrix.drawBitmap(0, 8, b, 8, 8, LED_ON);
  matrix.writeDisplay();
}

void TAI_finder_X1::matrix_clear() {
  matrix.clear();
  matrix.drawBitmap(0, 0, clear_array, 8, 8, LED_ON);
  matrix.drawBitmap(0, 8, clear_array, 8, 8, LED_ON);
  matrix.writeDisplay();
}

void TAI_finder_X1::transmission(int text, int row, int list) {
  WirePacker packer;
  packer.write(text);
  packer.write(row);
  packer.write(list);
  packer.end();
  Wire.beginTransmission(0x04);
  while (packer.available()) {
    Wire.write(packer.read());
  }
  Wire.endTransmission();
  delay(50);
}

void TAI_finder_X1::show_all_led(int R, int G, int B) { 
  for(int i = 0; i < LED_NUM; i++) {
    LR_Strip.setPixelColor(i, R, G, B);
  }
  LR_Strip.show();
  delay(1);
}

void TAI_finder_X1::camera_init() {
	Serial.begin(115200) ;
  while(!Serial.available()) {
    Serial.print("angle?");
    int i = 0;
    for(i = 0; i < LED_NUM; i++) {
      LR_Strip.setPixelColor(i, 255, 0, 255);
      LR_Strip.show();
    }
    delay(500);
    for(i = 0; i < LED_NUM; i++) {
      LR_Strip.setPixelColor(i, 0, 0, 0);
      LR_Strip.show();
    }
    delay(500) ;
  }
  int i = 0;
  for(i = 0; i < LED_NUM; i++) {
    LR_Strip.setPixelColor(i, 255, 0, 255);
    LR_Strip.show();
  }
  delay(3000);
  for(i = 0; i < LED_NUM; i++) {
    LR_Strip.setPixelColor(i, 0, 0, 0);
    LR_Strip.show();
  }
  while(Serial.available()) {
    char clear_Serial = Serial.read();
  }
  delay(500);
}

void TAI_finder_X1::ai_stick_init() {
  Serial.print("init_sign_model");
  while(!Serial.available()) {
    int i = 0;
    for(i = 0; i < LED_NUM; i++) {
      LR_Strip.setPixelColor(i, 0, 255, 255);
      LR_Strip.show();
    }
    delay(500);
    for(i = 0; i < LED_NUM; i++) {
      LR_Strip.setPixelColor(i, 0, 0, 0);
      LR_Strip.show();
    }
    delay(500);
  }
  delay(500);
}

void TAI_finder_X1::Servos_init() {
  Servos.begin();
  Servos.init(360);
  Servos.set_hz(50);
  delay(500);
}

void TAI_finder_X1::set_Servo(int num, int degree) {
  int arm_Angle = constrain(degree,0,180);
	Servos.set_channel_value(num, map(arm_Angle, 0, 180, 110, 530));
}

void TAI_finder_X1::set_pwm(int pwm_pin_num, int pwm) {
  pwm = constrain(pwm, -255, 255);
  if(pwm_pin_num == 1 && pwm >= 0) {
    digitalWrite(LEFT_FRONT_DIR_PIN, LEFT_FRONT_DIR_FRONT);
    analogWrite(LEFT_FRONT_PWM_PIN, pwm);
  } else if(pwm_pin_num == 1) {
    digitalWrite(LEFT_FRONT_DIR_PIN, LEFT_FRONT_DIR_BACK);
    analogWrite(LEFT_FRONT_PWM_PIN, -pwm);
  }
  if(pwm_pin_num == 2 && pwm >= 0) {
    if(pwm == 0) {
      left_motion_state = false;
    } else {
      left_motion_state = true;
    }
    left_dir_state = true;
    digitalWrite(LEFT_BACK_DIR_PIN, LEFT_BACK_DIR_FRONT);
    analogWrite(LEFT_BACK_PWM_PIN, pwm);
  } else if(pwm_pin_num == 2) {
    left_motion_state = true;
    left_dir_state = false;
    digitalWrite(LEFT_BACK_DIR_PIN, LEFT_BACK_DIR_BACK);
    analogWrite(LEFT_BACK_PWM_PIN, -pwm);
  }
  if(pwm_pin_num == 3 && pwm >= 0) {
    digitalWrite(RIGHT_FRONT_DIR_PIN, RIGHT_FRONT_DIR_FRONT);
    analogWrite(RIGHT_FRONT_PWM_PIN, pwm);
  } else if(pwm_pin_num == 3) {
    digitalWrite(RIGHT_FRONT_DIR_PIN, RIGHT_FRONT_DIR_BACK);
    analogWrite(RIGHT_FRONT_PWM_PIN, -pwm);
  }
  if(pwm_pin_num == 4 && pwm >= 0) {
    if(pwm == 0) {
      right_motion_state = false;
    } else {
      right_motion_state = true;
    }
    right_dir_state = true;
    digitalWrite(RIGHT_BACK_DIR_PIN, RIGHT_BACK_DIR_FRONT);
    analogWrite(RIGHT_BACK_PWM_PIN, pwm);
  } else if(pwm_pin_num == 4) {
    right_motion_state = true;
    right_dir_state = false;
    digitalWrite(RIGHT_BACK_DIR_PIN, RIGHT_BACK_DIR_BACK);
    analogWrite(RIGHT_BACK_PWM_PIN, -pwm);
  }
}

void TAI_finder_X1::set_four_pwm(int left_front_pwm, int left_back_pwm, int right_front_pwm, int right_back_pwm) {
  left_front_pwm = constrain(left_front_pwm, -255, 255);
  left_back_pwm = constrain(left_back_pwm, -255, 255);
  right_front_pwm = constrain(right_front_pwm, -255, 255);
  right_back_pwm = constrain(right_back_pwm, -255, 255);
  if(left_front_pwm == 0) {
    left_motion_state = false;
  } else {
    left_motion_state = true;
  }
  if(right_front_pwm == 0) {
    right_motion_state = false;
  } else {
    right_motion_state = true;
  }
  if(left_front_pwm >= 0) {
    left_dir_state = true;
    digitalWrite(LEFT_FRONT_DIR_PIN, LEFT_FRONT_DIR_FRONT);
    analogWrite(LEFT_FRONT_PWM_PIN, left_front_pwm);
  } else {
    left_dir_state = false;
    digitalWrite(LEFT_FRONT_DIR_PIN, LEFT_FRONT_DIR_BACK);
    analogWrite(LEFT_FRONT_PWM_PIN, -left_front_pwm);
  }
  if(left_back_pwm >= 0) {
    digitalWrite(LEFT_BACK_DIR_PIN, LEFT_BACK_DIR_FRONT);
    analogWrite(LEFT_BACK_PWM_PIN, left_back_pwm);
  } else {
    digitalWrite(LEFT_BACK_DIR_PIN, LEFT_BACK_DIR_BACK);
    analogWrite(LEFT_BACK_PWM_PIN, -left_back_pwm);
  }
  if(right_front_pwm >= 0) {
    right_dir_state = true;
    digitalWrite(RIGHT_FRONT_DIR_PIN, RIGHT_FRONT_DIR_FRONT);
    analogWrite(RIGHT_FRONT_PWM_PIN, right_front_pwm);
  } else {
    right_dir_state = false;
    digitalWrite(RIGHT_FRONT_DIR_PIN, RIGHT_FRONT_DIR_BACK);
    analogWrite(RIGHT_FRONT_PWM_PIN, -right_front_pwm);
  }
  if(right_back_pwm >= 0) {
    digitalWrite(RIGHT_BACK_DIR_PIN, RIGHT_BACK_DIR_FRONT);
    analogWrite(RIGHT_BACK_PWM_PIN, right_back_pwm);
  } else {
    digitalWrite(RIGHT_BACK_DIR_PIN, RIGHT_BACK_DIR_BACK);
    analogWrite(RIGHT_BACK_PWM_PIN, -right_back_pwm);
  }
}

void TAI_finder_X1::forward(int32_t dis_cm, uint8_t speed_pwm, boolean stop_state) {
  int32_t coders = (left_coder_count + right_coder_count) / 2.0 + (dis_cm / (WHEEL_SIZE_MM / 10.0) / PI) * CODERS + 0.5;
  speed_pwm = constrain(speed_pwm, 0, 255);
  uint32_t current_time = millis();
  if(stop_state) {
    z_angle = 0;
    z_angle_i = 0;
  }
  float kp = 15.0, ki = 0.05;
  while(coders >= ((left_coder_count + right_coder_count) / 2 + 0.5)) {
    if(millis() - current_time >= 10) {
      z_angle += read_Z_speed() * 10000;
      z_angle_i += z_angle;
      z_angle_i = constrain(z_angle_i, -1000, 1000);
      int left_pwm = speed_pwm - (z_angle * kp + z_angle_i * ki);
      int right_pwm = speed_pwm + (z_angle * kp + z_angle_i * ki);
      left_pwm = constrain(left_pwm, -255, 255);
      right_pwm = constrain(right_pwm, -255, 255);
      set_four_pwm(left_pwm, left_pwm, right_pwm, right_pwm);
      current_time = millis();
    }
  }
  if(speed_pwm > 60 && stop_state) {
    set_four_pwm(-255, -255, -255, -255);
    delay(20);
  }
  set_four_pwm(0, 0, 0, 0);
}

void TAI_finder_X1::back(int32_t dis_cm, uint8_t speed_pwm, boolean stop_state) {
  int32_t coders = (left_coder_count + right_coder_count) / 2.0 - (dis_cm / (WHEEL_SIZE_MM / 10.0) / PI) * CODERS + 0.5;
  speed_pwm = constrain(speed_pwm, 0, 255);
  uint32_t current_time = millis();
  if(stop_state) {
    z_angle = 0;
    z_angle_i = 0;
  }
  float kp = 15.0, ki = 0.05;
  while(coders <= (left_coder_count + right_coder_count) / 2) {
    if(millis() - current_time >= 10) {
      z_angle += read_Z_speed() * 10000;
      z_angle_i += z_angle;
      z_angle_i = constrain(z_angle_i, -1000, 1000);
      int left_pwm = speed_pwm + (z_angle * kp + z_angle_i * ki);
      int right_pwm = speed_pwm - (z_angle * kp + z_angle_i * ki);
      left_pwm = constrain(left_pwm, -255, 255);
      right_pwm = constrain(right_pwm, -255, 255);
      set_four_pwm(-left_pwm, -left_pwm, -right_pwm, -right_pwm);
      current_time = millis();
    }
  }
  if(speed_pwm > 60 && stop_state) {
    set_four_pwm(255, 255, 255, 255);
    delay(20);
  }
  set_four_pwm(0, 0, 0, 0);
}

void TAI_finder_X1::left(int32_t dis_cm, uint8_t speed_pwm, boolean stop_state) {
  int32_t coders = (right_coder_count - left_coder_count) / 2.0 + (dis_cm / (WHEEL_SIZE_MM / 10.0) / PI) * CODERS + 0.5;
  speed_pwm = constrain(speed_pwm, 0, 255);
  uint32_t current_time = millis();
  if(stop_state) {
    z_angle = 0;
    z_angle_i = 0;
  }
  float kp = 15.0, ki = 0.05;
  while(coders >= ((right_coder_count - left_coder_count) / 2 + 0.5)) {
    if(millis() - current_time >= 10) {
      z_angle += read_Z_speed() * 10000;
      z_angle_i += z_angle;
      z_angle_i = constrain(z_angle_i, -1000, 1000);
      int left_front_pwm = speed_pwm + (z_angle * kp + z_angle_i * ki);
      int left_back_pwm = speed_pwm - (z_angle * kp + z_angle_i * ki);
      int right_front_pwm = speed_pwm + (z_angle * kp + z_angle_i * ki);
      int right_back_pwm = speed_pwm - (z_angle * kp + z_angle_i * ki);
      left_front_pwm = constrain(left_front_pwm, -255, 255);
      left_back_pwm = constrain(left_back_pwm, -255, 255);
      right_front_pwm = constrain(right_front_pwm, -255, 255);
      right_back_pwm = constrain(right_back_pwm, -255, 255);
      set_four_pwm(-left_front_pwm, left_back_pwm, right_front_pwm, -right_back_pwm);
      current_time = millis();
    }
  }
  if(speed_pwm > 60 && stop_state) {
    set_four_pwm(255, -255, -255, 255);
    delay(20);
  }
  set_four_pwm(0, 0, 0, 0);
}

void TAI_finder_X1::right(int32_t dis_cm, uint8_t speed_pwm, boolean stop_state) {
  int32_t coders = (left_coder_count - right_coder_count) / 2.0 + (dis_cm / (WHEEL_SIZE_MM / 10.0) / PI) * CODERS + 0.5;
  speed_pwm = constrain(speed_pwm, 0, 255);
  uint32_t current_time = millis();
  if(stop_state) {
    z_angle = 0;
    z_angle_i = 0;
  }
  float kp = 15.0, ki = 0.05;
  while(coders >= ((left_coder_count - right_coder_count) / 2 + 0.5)) {
    if(millis() - current_time >= 10) {
      z_angle += read_Z_speed() * 10000;
      z_angle_i += z_angle;
      z_angle_i = constrain(z_angle_i, -1000, 1000);
      int left_front_pwm = speed_pwm - (z_angle * kp + z_angle_i * ki);
      int left_back_pwm = speed_pwm + (z_angle * kp + z_angle_i * ki);
      int right_front_pwm = speed_pwm - (z_angle * kp + z_angle_i * ki);
      int right_back_pwm = speed_pwm + (z_angle * kp + z_angle_i * ki);
      left_front_pwm = constrain(left_front_pwm, -255, 255);
      left_back_pwm = constrain(left_back_pwm, -255, 255);
      right_front_pwm = constrain(right_front_pwm, -255, 255);
      right_back_pwm = constrain(right_back_pwm, -255, 255);
      set_four_pwm(left_front_pwm, -left_back_pwm, -right_front_pwm, right_back_pwm);
      current_time = millis();
    }
  }
  if(speed_pwm > 60 && stop_state) {
    set_four_pwm(-255, 255, 255, -255);
    delay(20);
  }
  set_four_pwm(0, 0, 0, 0);
}

void TAI_finder_X1::turn_left(int32_t degree, uint8_t speed_pwm, boolean stop_state) {
  if(degree < 0) {
    degree = 0;
  }
  speed_pwm = constrain(speed_pwm, 0, 255);
  uint32_t current_time = millis();
  z_angle = 0;
  z_angle_i = 0;
  while(degree > 0 && z_angle > -degree) {
    if(millis() - current_time >= 10) {
      z_angle += read_Z_speed() * 10000;
      set_four_pwm(-speed_pwm, -speed_pwm, speed_pwm, speed_pwm);
      current_time = millis();
    }
  }
  if(speed_pwm > 60 && stop_state) {
    set_four_pwm(255, 255, -255, -255);
    delay(20);
  }
  set_four_pwm(0, 0, 0, 0);
  z_angle = 0;
}

void TAI_finder_X1::turn_right(int32_t degree, uint8_t speed_pwm, boolean stop_state) {
  if(degree < 0) {
    degree = 0;
  }
  speed_pwm = constrain(speed_pwm, 0, 255);
  uint32_t current_time = millis();
  z_angle = 0;
  z_angle_i = 0;
  while(degree > 0 && z_angle < degree) {
    if(millis() - current_time >= 10) {
      z_angle += read_Z_speed() * 10000;
      set_four_pwm(speed_pwm, speed_pwm, -speed_pwm, -speed_pwm);
      current_time = millis();
    }
  }
  if(speed_pwm > 60 && stop_state) {
    set_four_pwm(-255, -255, 255, 255);
    delay(20);
  }
  set_four_pwm(0, 0, 0, 0);
  z_angle = 0;
}

int TAI_finder_X1::get_front_sonar_dis_cm() {
  double distance = 0;
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);
  distance = pulseIn(ECHO_PIN, HIGH) / 58.00;
  return distance;
}

int TAI_finder_X1::get_left_ir_value() {
  return analogRead(LEFT_IR_PIN);
}

int TAI_finder_X1::get_right_ir_value() {
  return analogRead(RIGHT_IR_PIN);
}

void TAI_finder_X1::WriteMPUReg(int nReg,unsigned char nVal) {
  Wire.beginTransmission(0x68);
  Wire.write(nReg);
  Wire.write(nVal);
  Wire.endTransmission(true);
}

void TAI_finder_X1::AccGyr_init() {
  WriteMPUReg(0x6B,0x80);
  delay(50);
  WriteMPUReg(0x6B,0x03);
  WriteMPUReg(0x1A,0x03);
  WriteMPUReg(0x1B,0x18);
  WriteMPUReg(0x1C,0x10);
  WriteMPUReg(0x24,0x0D);
}

int TAI_finder_X1::readGyrZ() {
  Wire.beginTransmission(0x68);
  Wire.write(0x47);
  Wire.requestFrom(0x68, 2, true);
  Wire.endTransmission(true);
  return (Wire.read() << 8 | Wire.read());
}

float TAI_finder_X1::read_Z_speed() {
  Wire.beginTransmission(0x68);
  Wire.write(0x47);
  Wire.requestFrom(0x68, 2, true);
  Wire.endTransmission(true);
  int z_value = (Wire.read() << 8 | Wire.read());
  return (z_value - delta) * GYRORATE * 180 / PI / 4;
}

int TAI_finder_X1::get_double_line_value() {
  while(Serial.available()) {
    Serial.read();
  }
  Serial.print("angle?");
  uint32_t t = millis();
  while(!Serial.available()) {
    if(millis() - t > 1000) {
      break;
    }
  }
  String receive = "";
  while(Serial.available()){
    receive += (char)Serial.read();
    delayMicroseconds(100);
  }
  return receive.toInt();
}

boolean TAI_finder_X1::get_color(int color_num) {
  while(Serial.available()) {
    Serial.read();
  }
  switch(color_num) {
    case 1: Serial.print("color?mutli 27 140 107 197 255 177"); break;
    case 2: Serial.print("color?mutli 27 0 87 197 110 197"); break;
    case 3: Serial.print("color?mutli 27 97 0 197 167 110"); break;
  }
  int error_count = 0;
  while(error_count <= 5) {
    uint32_t t = millis();
    while(!Serial.available()) {
      if(millis() - t > 200)
          break;
    }
    String cmd = "";
    while(Serial.available()) {
      cmd += (char)Serial.read();
      delayMicroseconds(100);
    }
    if(cmd.length() <= 3) {
      error_count++ ;
    } else {
      error_count = 0;
      if(cmd == "None") {
        return false;
      }
      while(cmd.length() > 0) {
        if(cmd.substring(0, cmd.indexOf(" ")).toInt() <= 110 && cmd.substring(0, cmd.indexOf(" ")).toInt() >= 50) {
            return true;
        }
        cmd = cmd.substring(cmd.indexOf(";") + 1, cmd.length());
      }
      return false;
    }
  }
  return false;
}

int TAI_finder_X1::get_color_info(int color_num, int info_num) {
  if(millis() - color_MAX_Waiting_Time >= 10) {
    while(Serial.available()) {
      Serial.read();
    }
    switch(color_num) {
      case 1: Serial.print("color?max 27 140 107 197 255 177"); break;
      case 2: Serial.print("color?max 27 0 87 197 110 197"); break;
      case 3: Serial.print("color?max 27 97 0 197 167 110"); break;
    }
    int error_Count = 0;
    while(error_Count <= 5) {
      uint32_t t = millis();
      while(!Serial.available()) {
        if(millis() - t > 200)
            break;
      }
      String cmd = "";
      while(Serial.available()) {
        cmd += (char)Serial.read();
        delayMicroseconds(100);
      }
      color_MAX_Waiting_Time = millis();
      if(cmd.length() <= 3) {
        error_Count++ ;
      }
      else {
        error_Count = 0;
        if(cmd == "None") {
          int i = 0;
          for(i = 0; i < 5 ; i++) {
            color_MAX_Data[i] = 0;
          }
        }
        color_MAX_Data[0] = cmd.substring(0,cmd.indexOf(" ")).toInt();
        cmd = cmd.substring(cmd.indexOf(" ") + 1, cmd.length());
        color_MAX_Data[1] = cmd.substring(0,cmd.indexOf(",")).toInt();
        cmd = cmd.substring(cmd.indexOf(",") + 1, cmd.length());
        color_MAX_Data[2] = cmd.substring(0,cmd.indexOf(" ")).toInt();
        cmd = cmd.substring(cmd.indexOf(" ") + 1, cmd.length());
        color_MAX_Data[3] = cmd.substring(0,cmd.indexOf(",")).toInt();
        cmd = cmd.substring(cmd.indexOf(",") + 1, cmd.length());
        color_MAX_Data[4] = cmd.substring(0,cmd.indexOf(";")).toInt();
        return color_MAX_Data[info_num - 1];
      }
    }
    int i = 0;
    for(i = 0; i < 5; i++) {
      color_MAX_Data[i] = 0;
    }
  }
  return color_MAX_Data[info_num - 1];
}

boolean TAI_finder_X1::is_sign(int num) {
  while(Serial.available()) {
    Serial.read();
  }
  Serial.print("sign?");
  int error_Count = 0;
  while(error_Count <= 5) {
    uint32_t t = millis();
    while(!Serial.available()) {
      if(millis() - t > 1000)
          break;
    }
    String cmd = "";
    while(Serial.available()) {
      cmd += (char)Serial.read();
      delayMicroseconds(100);
    }
    if(cmd.length() <= 3) {
      error_Count++;
    } else {
      error_Count = 0;
      if(cmd == "None") {
        return false;
      }
      int which_Sign = cmd.substring(0, cmd.indexOf(",")).toInt();
      switch(num) {
        case 1: if(which_Sign == 5) {return true;} else {return false;} break;
        case 2: if(which_Sign == 6) {return true;} else {return false;} break;
        case 3: if(which_Sign == 7) {return true;} else {return false;} break;
        case 4: if(which_Sign == 8) {return true;} else {return false;} break;
        case 5: if(which_Sign == 3) {return true;} else {return false;} break;
        case 6: if(which_Sign == 4) {return true;} else {return false;} break;
        case 7: if(which_Sign == 1) {return true;} else {return false;} break;
        case 8: if(which_Sign == 2) {return true;} else {return false;} break;
        case 9: if(which_Sign == 0) {return true;} else {return false;} break;
        default: return false;
      }
    }
    return false;
  }
  return false;
}

int TAI_finder_X1::get_finger_number() {
  while(Serial.available()) {
    Serial.read();
  }
  Serial.print("hand?");
  uint32_t t = millis();
  while(!Serial.available()) {
    if(millis() - t > 200)
        break;
  }
  String cmd = "";
  while(Serial.available()) {
    cmd += (char)Serial.read();
    delayMicroseconds(100);
  }
  return cmd.toInt();
}

int TAI_finder_X1::get_polygon_number(int polygon_Type) {
  while(Serial.available()) {
    Serial.read();
  }
  Serial.print("shape?mutli ");
  Serial.print(polygon_Type);
  uint32_t t = millis();
  while(!Serial.available()) {
    if(millis() - t > 200)
        break;
  }
  String cmd = "";
  while(Serial.available()) {
    cmd += (char)Serial.read();
    delayMicroseconds(100);
  }
  return cmd.toInt();
}

int TAI_finder_X1::get_polygon() {
  while(Serial.available()) {
    Serial.read();
  }
  Serial.print("shape?max 0");
  uint32_t t = millis();
  while(!Serial.available()) {
    if(millis() - t > 200)
        break;
  }
  String cmd = "";
  while(Serial.available()) {
    cmd += (char)Serial.read();
    delayMicroseconds(100);
  }
  return cmd.toInt();
}

void TAI_finder_X1::clear_coder_count() {
  left_coder_count = 0;
  right_coder_count = 0;
}

float TAI_finder_X1::get_distance_cm() {
  return (left_coder_count + right_coder_count) / 2.0 / CODERS * (WHEEL_SIZE_MM / 10) * PI;
}

float TAI_finder_X1::get_horizontal_distance_cm() {
  return -(right_coder_count - left_coder_count) / 2.0 / CODERS * (WHEEL_SIZE_MM / 10) * PI;
}

void TAI_finder_X1::motion(int motion_num, int dis_cm, int speed_pwm, boolean brake_state) {
  switch(motion_num) {
    case 1: forward(dis_cm, speed_pwm, brake_state); break;
    case 2: left(dis_cm, speed_pwm, brake_state); break;
    case 3: right(dis_cm, speed_pwm, brake_state); break;
    case 4: back(dis_cm, speed_pwm, brake_state); break;
  }
}

void TAI_finder_X1::turn_motion(int motion_num, int degree, int speed_pwm, boolean brake_state) {
  switch(motion_num) {
    case 1: turn_left(degree, speed_pwm, brake_state); break;
    case 2: turn_right(degree, speed_pwm, brake_state); break;
  }
}

void TAI_finder_X1::func1(int motion_num, int speed_pwm) {
  switch(motion_num) {
    case 1: forward(1, speed_pwm, false); break;
    case 2: left(1, speed_pwm, false); break;
    case 3: right(1, speed_pwm, false); break;
    case 4: back(1, speed_pwm, false); break;
  }
}

void TAI_finder_X1::func2(int motion_num) {
  switch(motion_num) {
    case 1: set_four_pwm(-255, -255, -255, -255); delay(20); set_four_pwm(0, 0, 0, 0); break;
    case 2: set_four_pwm(255, -255, -255, 255); delay(20); set_four_pwm(0, 0, 0, 0); break;
    case 3: set_four_pwm(-255, 255, 255, -255); delay(20); set_four_pwm(0, 0, 0, 0); break;
    case 4: set_four_pwm(255, 255, 255, 255); delay(20); set_four_pwm(0, 0, 0, 0); break;
  }
}

int TAI_finder_X1::get_ir_value(int ir_num) {
  int value = 0;
  switch(ir_num) {
    case 1: value = get_left_ir_value(); break;
    case 2: value = get_right_ir_value(); break;
  }
  return value;
}

void TAI_finder_X1::play_sound(int sound_num) {
  Serial.print("SOUND=" + String(sound_num));
  delay(500);
}

void TAI_finder_X1::set_image_range(float num) {
  Serial.print("IMGRANGE=" + String(num));
  delay(1000);
}

void TAI_finder_X1::set_line_threshold(float num) {
  Serial.print("LINETHRESHOLD=" + String(num));
  delay(1000);
}

void TAI_finder_X1::start_serial_command() {
  static uint32_t rc_time;
  if(millis() - rc_time > 200) {
    roll = 0;
    pitch = 0;
    thr = 0;
    yaw = 0;
    aux1 = 0;
    aux2 = 0;
    aux3 = 0;
    aux4 = 0;
    while(Serial.available()) {
      Serial.read();
    }
    rc_time = millis();
  }
  if(Serial.available()) {
    String cmd = "";
    while(Serial.available()) {
      cmd += (char)Serial.read();
      delayMicroseconds(100);
    }
    if(cmd == "auto") {
      auto_state = 1;
    } else if(cmd == "normal") {
      auto_state = 0;
    } else if(cmd.substring(0, cmd.indexOf(" ")) == "tmd") {
      int data[8] = {50, 50, 50, 50, 50, 50, 50, 50};
      rc_time = millis();
      cmd = cmd.substring(cmd.indexOf(" ") + 1, cmd.length());
      int i = 0;
      while(cmd.indexOf(" ") > 0 && i < 7) {
        data[i] = cmd.substring(0, cmd.indexOf(" ")).toInt();
        cmd = cmd.substring(cmd.indexOf(" ") + 1, cmd.length());
        i++;
      }
      data[i] = cmd.toInt();
      for(int j = 0; j < 8; j++) {
        if(j != 4) {
          data[j] = map(data[j], 0, 100, -100, 100);
        }
      }
      roll = data[0];
      pitch = data[1];
      thr = data[2];
      yaw = data[3];
      aux1 = data[4];
      aux2 = data[5];
      aux3 = data[6];
      aux4 = data[7];
    }
  }
}

int TAI_finder_X1::get_serial_command_value(int num) {
  switch(num) {
    case 1: return roll;
    case 2: return pitch;
    case 3: return thr;
    case 4: return yaw;
    case 5: return aux1;
    case 6: return aux2;
    case 7: return aux3;
    case 8: return aux4;
    case 9: return auto_state;
    default: return 0;
  }
}

boolean TAI_finder_X1::is_serial_num(int num) {
  switch(num) {
    case 0: if(aux1 == 10) {return 1;} else {return 0;};
    case 1: if(aux1 == 15) {return 1;} else {return 0;};
    case 2: if(aux1 == 20) {return 1;} else {return 0;};
    case 3: if(aux1 == 25) {return 1;} else {return 0;};
    case 4: if(aux1 == 30) {return 1;} else {return 0;};
    case 5: if(aux1 == 35) {return 1;} else {return 0;};
    case 6: if(aux1 == 40) {return 1;} else {return 0;};
    case 7: if(aux1 == 45) {return 1;} else {return 0;};
    case 8: if(aux1 == 50) {return 1;} else {return 0;};
    case 9: if(aux1 == 55) {return 1;} else {return 0;};
  }
}

void TAI_finder_X1::rc_control() {
  auto_state = true;
  int lf_pwm, lb_pwm, rf_pwm, rb_pwm;
  int servo0_degree = 90;
  int servo1_degree = 80;
  int servo2_degree = 80;
  set_Servo(0, 90);
  set_Servo(1, 80);
  set_Servo(2, 80);
  delay(500);
  set_Servo(1, 80);
  set_Servo(2, 80);
  while(1) {
    start_serial_command();
    if(auto_state == false) {
      set_four_pwm(0, 0, 0, 0);
      break;
    }
    if(roll < 30 && roll > -30) {
      roll = 0;
    }
    if(pitch < 30 && pitch > -30) {
      pitch = 0;
    }
    if(yaw < 30 && yaw > -30) {
      yaw = 0;
    }
    roll = map(roll, -100, 100, -255, 255);
    pitch = map(pitch, -100, 100, -255, 255);
    yaw = map(yaw, -100, 100, -255, 255);
    lf_pwm = pitch * pitch_default_kp + yaw * yaw_default_kp + roll * roll_default_kp;
    lb_pwm = pitch * pitch_default_kp + yaw * yaw_default_kp - roll * roll_default_kp;
    rf_pwm = pitch * pitch_default_kp - yaw * yaw_default_kp - roll * roll_default_kp;
    rb_pwm = pitch * pitch_default_kp - yaw * yaw_default_kp + roll * roll_default_kp;
    lf_pwm = constrain(lf_pwm, -255, 255);
    lb_pwm = constrain(lb_pwm, -255, 255);
    rf_pwm = constrain(rf_pwm, -255, 255);
    rb_pwm = constrain(rb_pwm, -255, 255);
    set_four_pwm(lf_pwm, lb_pwm, rf_pwm, rb_pwm);
    if(aux2 > 50) {
      if(servo2_degree < 120) {
        servo2_degree++;
      } else if(servo2_degree < servo2_max_value) {
        servo1_degree--;
        servo2_degree++;
      } else {
        servo1_degree--;
      }
    } else if(aux2 < -50) {
      if(servo1_degree < 50) {
        servo1_degree++;
      } else if(servo1_degree < servo1_max_value) {
        servo1_degree++;
        servo2_degree--;
      } else {
        servo2_degree--;
      }
    }
    if(aux4 > 50) {
      servo0_degree++;
    } else if(aux4 < -50) {
      servo0_degree--;
    }
    servo0_degree = constrain(servo0_degree, 90, 130);
    servo1_degree = constrain(servo1_degree, servo1_min_value, servo1_max_value);
    servo2_degree = constrain(servo2_degree, servo2_min_value, servo2_max_value);
    set_Servo(0, servo0_degree);
    set_Servo(1, servo1_degree);
    set_Servo(2, servo2_degree);
    delay(10);
  }
}


void TAI_finder_X1::rc_control_24XingKuang() {
  auto_state = true;
  int lf_pwm, lb_pwm, rf_pwm, rb_pwm;
  int servo0_degree = 90;
  int servo1_degree = 90;
  int servo2_degree = 90;
  set_Servo(0, 90);
  set_Servo(1, 90);
  set_Servo(2, 90);
  while(1) {
    start_serial_command();
    if(auto_state == false) {
      set_four_pwm(0, 0, 0, 0);
      break;
    }
    if(roll < 30 && roll > -30) {
      roll = 0;
    }
    if(pitch < 30 && pitch > -30) {
      pitch = 0;
    }
    if(yaw < 30 && yaw > -30) {
      yaw = 0;
    }
    roll = map(roll, -100, 100, -255, 255);
    pitch = map(pitch, -100, 100, -255, 255);
    yaw = map(yaw, -100, 100, -255, 255);
    lf_pwm = pitch * pitch_default_kp + yaw * yaw_default_kp + roll * roll_default_kp;
    lb_pwm = pitch * pitch_default_kp + yaw * yaw_default_kp - roll * roll_default_kp;
    rf_pwm = pitch * pitch_default_kp - yaw * yaw_default_kp - roll * roll_default_kp;
    rb_pwm = pitch * pitch_default_kp - yaw * yaw_default_kp + roll * roll_default_kp;
    lf_pwm = constrain(lf_pwm, -255, 255);
    lb_pwm = constrain(lb_pwm, -255, 255);
    rf_pwm = constrain(rf_pwm, -255, 255);
    rb_pwm = constrain(rb_pwm, -255, 255);
    set_four_pwm(lf_pwm, lb_pwm, rf_pwm, rb_pwm);
    if(aux2 > 50) {
      servo1_degree++;
    } else if(aux2 < -50) {
      servo1_degree--;
    }
    if(aux4 > 50) {
      servo0_degree++;
    } else if(aux4 < -50) {
      servo0_degree--;
    }
    servo0_degree = constrain(servo0_degree, 90	, 120);
    servo1_degree = constrain(servo1_degree, 140, 60);
    set_Servo(0, servo0_degree);
    set_Servo(1, servo1_degree);
    delay(10);
  }
}

void TAI_finder_X1::set_rc_default_value(int num, float value) {
  switch(num) {
    case 1: value = constrain(value, 0.0, 1.0); roll_default_kp = value; break;
    case 2: value = constrain(value, 0.0, 1.0); pitch_default_kp = value; break;
    case 3: value = constrain(value, 0.0, 1.0); yaw_default_kp = value; break;
    case 4: value = constrain(value, -15, 15); servo0_max_value = servo0_default_value - value; break;
    case 5: value = constrain(value, -15, 15); servo2_max_value = servo2_default_value - value; break;
  }
}

boolean TAI_finder_X1::get_self_lock_state() {
  return digitalRead(SELF_LOCK_PIN);
}

float TAI_finder_X1::get_steering_angle_degree() {
  float angle = 0;
  angle = (left_coder_count - right_coder_count) / 2.0;
  angle = (((angle * (((WHEEL_SIZE_MM / 10.0) * PI) / CODERS)) * (2.635/sqrt(18))) / (20 * PI)) * 360;
  return angle;
}

void TAI_finder_X1::set_gyro_z_init() {
  int gyro_z_offset = 0;
  int32_t count = 0;
  for(int i = 0; i < 100; i++) {
    readGyrZ();
    delay(1);
  }
  for(int i = 0; i < 100; i++) {
    count += readGyrZ();
    delay(1);
  }
  gyro_z_offset = count / 100;
  delta = gyro_z_offset;
  EEPROM.write(213, gyro_z_offset & 0xff);
  EEPROM.write(214, gyro_z_offset >> 8);
  EEPROM.write(200, 'T');
  EEPROM.write(201, 'A');
  EEPROM.write(202, 'I');
}


void TAI_finder_X1::servopulse(int servopin,int angle) {
	int pulsewidth = (angle*11)+500 ;
	digitalWrite(servopin , HIGH);
	delayMicroseconds(pulsewidth) ;
	digitalWrite(servopin , LOW);
	delayMicroseconds(20000-pulsewidth) ;
}


void TAI_finder_X1::Bluetooth_Connect(){
  Serial.begin(115200) ;
  show_all_led(255, 255, 255) ;
  delay(500);
  Serial.print("AT+BONDC\r\n");
  delay(1000);
  Serial.print("AT+MODE=1\r\n");
  delay(500);
  Serial.print("AT+ROLE=1\r\n");
  delay(500);
  while(Serial.available()) {
    Serial.read();
  }
  show_all_led(255, 255, 0) ;
  while(1) {
    boolean state = false;
    String str = "";
    delay(500);
    Serial.print("AT+SCAN\r\n");
    delay(50);
    while(Serial.available()) {
      Serial.read();
    }
    while(!Serial.available()) {}
    delay(1);
    while(Serial.available()) {
      str += (char)Serial.read();
      delayMicroseconds(100);
    }
    int c = str.substring(str.indexOf(":") + 1, str.indexOf(":") + 2).toInt();
    for(int i = 0; i < c; i++) {
      str = str.substring(str.indexOf(",") + 1, str.length());
      if(str.substring(0, str.indexOf(",")) == "GamePadPlus V3") {
        str = str.substring(str.indexOf(",") + 1, str.length());
        str = str.substring(0, str.indexOf(","));
        Serial.print("AT+BONDMAC=" + str + "\r\n");
        state = true;
      } else {
        str = str.substring(str.indexOf(",") + 1, str.length());
        str = str.substring(str.indexOf(",") + 1, str.length());
      }
    }
    if(state) {
      break;
    }
  }
  delay(100);
  show_all_led(0, 255, 0) ;
}

void TAI_finder_X1::get_bluetooth_data() {
  if (Serial.available()) {
    int i = 0;
    while(Serial.available()) {
      bluetooth_data[i % 10] = Serial.read();
      delayMicroseconds(100);
      i++;
    }
    if(i > 10) {
      bluetooth_data[0] = 0x80;
      bluetooth_data[1] = 0x80;
      bluetooth_data[2] = 0x80;
      bluetooth_data[3] = 0x80;
      bluetooth_data[4] = 0xff;
      bluetooth_data[5] = 0x00;
      bluetooth_data[6] = 0x00;
      bluetooth_data[7] = 0x00;
      bluetooth_data[8] = 0x00;
      bluetooth_data[9] = 0x00;
    }
  }
}



int TAI_finder_X1::get_bluetooth_remote_sensing_cmd(int port) {
  switch(port) {
    case 1: return bluetooth_data[0];
    case 2: return bluetooth_data[1];
    case 3: return bluetooth_data[2];
    case 4: return bluetooth_data[3];
    default: return 0;
  }
}

int TAI_finder_X1::get_bluetooth_button_pressed(int port) {
  switch(port) {
    case 1:
    {
      if(bluetooth_data[4] == 0x00) {
        return 1;
      } else {
        return 0;
      }
    }
    case 2:
    {
      if(bluetooth_data[4] == 0x04) {
        return 1;
      } else {
        return 0;
      }
    }
    case 3:
    {
      if(bluetooth_data[4] == 0x06) {
        return 1;
      } else {
        return 0;
      }
    }
    case 4:
    {
      if(bluetooth_data[4] == 0x02) {
        return 1;
      } else {
        return 0;
      }
    }
    case 5:
    {
      if(bluetooth_data[5] == 0x01) {
        return 1;
      } else {
        return 0;
      }
    }
    case 6:
    {
      if(bluetooth_data[5] == 0x02) {
        return 1;
      } else {
        return 0;
      }
    }
    case 7:
    {
      if(bluetooth_data[5] == 0x08) {
        return 1;
      } else {
        return 0;
      }
    }
    case 8:
    {
      if(bluetooth_data[5] == 0x10) {
        return 1;
      } else {
        return 0;
      }
    }
    case 9:
    {
      if(bluetooth_data[5] == 0x40) {
        return 1;
      } else {
        return 0;
      }
    }
    case 10:
    {
      if(bluetooth_data[5] == 0x80) {
        return 1;
      } else {
        return 0;
      }
    }
    case 11:
    {
      if(bluetooth_data[6] == 0x01) {
        return 1;
      } else {
        return 0;
      }
    }
    case 12:
    {
      if(bluetooth_data[6] == 0x02) {
        return 1;
      } else {
        return 0;
      }
    }
    default: return 0;
  }
}


int TAI_finder_X1::get_I2C_Gray(int pin){
	if(pin<=3){
		switch(pin){
			case 0 : return pcf.analogRead(0);
			case 1 : return pcf.analogRead(1);
			case 2 : return pcf.analogRead(2);
			case 3 : return pcf.analogRead(3);
		}
	}
	else{
		switch(pin-4){
			case 0 : return pcf2.analogRead(0);
			case 1 : return pcf2.analogRead(1);
			case 2 : return pcf2.analogRead(2);
		}
	}
}


void TAI_finder_X1::initialize_gray() {
  int threshold_value_1 = 255;
  int threshold_value_2 = 255;
  int threshold_value_3 = 255;
  int threshold_value_4 = 255;
  int threshold_value_5 = 255;
  int threshold_value_6 = 255;
  int threshold_value_7 = 255;
  long black_value_1 = 0;
  long black_value_2 = 0;
  long black_value_3 = 0;
  long black_value_4 = 0;
  long black_value_5 = 0;
  long black_value_6 = 0;
  long black_value_7 = 0;
  long white_value_1 = 0;
  long white_value_2 = 0;
  long white_value_3 = 0;
  long white_value_4 = 0;
  long white_value_5 = 0;
  long white_value_6 = 0;
  long white_value_7 = 0;
  for(int i = 0; i < LED_NUM; i++) {
    LR_Strip.setPixelColor(i, 255, 0, 0);
  }
  LR_Strip.show();
  long i = 0;
  delay(300);
  while(!digitalRead(SELF_LOCK_PIN)) {}
  delay(300);
  for(int i = 0; i < LED_NUM; i++) {
    LR_Strip.setPixelColor(i, 0, 0, 255);
  }
  LR_Strip.show();
  long scan_Time_Initial = 0;
      scan_Time_Initial = millis();
      while(millis() - scan_Time_Initial <= 5000) {
        black_value_1 += pcf.analogRead(0);
        black_value_2 += pcf.analogRead(1);
        black_value_3 += pcf.analogRead(2);
        black_value_4 += pcf.analogRead(3);
        black_value_5 += pcf2.analogRead(0);
        black_value_6 += pcf2.analogRead(1);
        black_value_7 += pcf2.analogRead(2);
        i++;
      }
  black_value_1 = black_value_1 / i;
  black_value_2 = black_value_2 / i;
  black_value_3 = black_value_3 / i;
  black_value_4 = black_value_4 / i;
  black_value_5 = black_value_5 / i;
  black_value_6 = black_value_6 / i;
  black_value_7 = black_value_7 / i;
  for(int i = 0; i < LED_NUM; i++) {
    LR_Strip.setPixelColor(i, 255, 255, 0);
  }
  LR_Strip.show();
  i = 0;
  while(digitalRead(SELF_LOCK_PIN)) {}
  delay(300);
  while(!digitalRead(SELF_LOCK_PIN)) {}
  delay(300);
  for(int i = 0; i < LED_NUM; i++) {
    LR_Strip.setPixelColor(i, 255, 255, 255);
  }
  LR_Strip.show();
  scan_Time_Initial = 0;
      scan_Time_Initial = millis();
      while(millis() - scan_Time_Initial <= 5000) {
        white_value_1 += pcf.analogRead(0);
        white_value_2 += pcf.analogRead(1);
        white_value_3 += pcf.analogRead(2);
        white_value_4 += pcf.analogRead(3);
        white_value_5 += pcf2.analogRead(0);
        white_value_6 += pcf2.analogRead(1);
        white_value_7 += pcf2.analogRead(2);
        i++;
      }
  white_value_1 = white_value_1 / i;
  white_value_2 = white_value_2 / i;
  white_value_3 = white_value_3 / i;
  white_value_4 = white_value_4 / i;
  white_value_5 = white_value_5 / i;
  white_value_6 = white_value_6 / i;
  white_value_7 = white_value_7 / i;
  threshold_value_1 = (black_value_1 + white_value_1) / 2;
  threshold_value_2 = (black_value_2 + white_value_2) / 2;
  threshold_value_3 = (black_value_3 + white_value_3) / 2;
  threshold_value_4 = (black_value_4 + white_value_4) / 2;
  threshold_value_5 = (black_value_5 + white_value_5) / 2;
  threshold_value_6 = (black_value_6 + white_value_6) / 2;
  threshold_value_7 = (black_value_7 + white_value_7) / 2;
  EEPROM.write(1, threshold_value_1);
  EEPROM.write(2, threshold_value_2);
  EEPROM.write(3, threshold_value_3);
  EEPROM.write(4, threshold_value_4);
  EEPROM.write(5, threshold_value_5);
  EEPROM.write(6, threshold_value_6);
  EEPROM.write(7, threshold_value_7);
  for(int i = 0; i < LED_NUM; i++) {
    LR_Strip.setPixelColor(i, 0, 0, 0);
  }
  LR_Strip.show();
  while(digitalRead(SELF_LOCK_PIN)) {}
  while(1);
}

void TAI_finder_X1::get_initialize_gray() {
  gray_Threshold_value_1 = EEPROM.read(1);
  gray_Threshold_value_2 = EEPROM.read(2);
  gray_Threshold_value_3 = EEPROM.read(3);
  gray_Threshold_value_4 = EEPROM.read(4);
  gray_Threshold_value_5 = EEPROM.read(5);
  gray_Threshold_value_6 = EEPROM.read(6);
  gray_Threshold_value_7 = EEPROM.read(7);
}


void TAI_finder_X1::init_DRV8830()
{
  // 清除错误标志
  clearFaults();

  // 设置电机为停止状态
  setMotor(0);
}

void TAI_finder_X1::setMotor(int speed)
{
  byte controlValue;
  speed = map(speed,-255,255,-63,63) ; 
  int direction = 1 ;
  if(speed>= 0){direction = 1;}else{direction = -1 ;speed = -speed ;}
  // 限制速度值
  if (speed > 63)
  {
    speed = 63;
  }

  // 设置速度和方向
  if (direction == 1)
  {
    controlValue = (speed << 2) | 0x01;
  }
  else if (direction == -1)
  {
    controlValue = (speed << 2) | 0x02;
  }
  else
  {
    controlValue = 0;
  }

  // 将控制值写入控制寄存器
  writeRegister(CONTROL_REGISTER, controlValue);
}

void TAI_finder_X1::clearFaults()
{
  byte faultValue = readRegister(FAULT_REGISTER);
  faultValue &= 0x80;
  writeRegister(FAULT_REGISTER, faultValue);
}

byte TAI_finder_X1::readRegister(byte reg)
{
  byte value;
  Wire.beginTransmission(DRV8830_Address);
  Wire.write(reg);
  Wire.endTransmission();
  Wire.requestFrom(DRV8830_Address, (byte)1);
  value = Wire.read();
  return value;
}

void TAI_finder_X1::writeRegister(byte reg, byte value)
{
  Wire.beginTransmission(DRV8830_Address);
  Wire.write(reg);
  Wire.write(value);
  Wire.endTransmission();
}


void TAI_finder_X1::moter_drive(int speed_pwm, uint32_t movement_time) {
  setMotor(speed_pwm);
  if(movement_time >= 0) {
    delay(movement_time * 1000);
	setMotor(0);
  }
}

void TAI_finder_X1::lcd_show(String text1, String text2) {
  lcd.setCursor(0, 0);
  lcd.print(text1);
  lcd.setCursor(0, 1);
  lcd.print(text2);
}

void TAI_finder_X1::lcd_clear() {
  lcd.clear();
}

void TAI_finder_X1::get_pic(uint8_t pin,uint32_t time) {
  digitalWrite(pin,HIGH);
  delay(200);
  digitalWrite(pin,LOW);
  delay(time);
}

void TAI_finder_X1::end_pic(uint8_t pin) {
  digitalWrite(pin,HIGH);
  delay(200);
  digitalWrite(pin,LOW);
  delay(100);
}