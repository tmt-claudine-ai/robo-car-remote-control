#ifndef _TAI_FINDER_X1_H_
#define _TAI_FINDER_X1_H_

#include "LiquidCrystal_I2C.h"
#include "Arduino.h"
#include "TAI_finder_X1_config.h"
#include "Wire.h"
#include "Adafruit_NeoPixel.h"
#include "FaBoPWM_PCA9685.h"
#include "EEPROM.h"
#include "Adafruit_GFX.h"
#include "Adafruit_LEDBackpack.h"
#include "TAI_finder_X1_array.h"
#include "WirePacker.h"
#include <Adafruit_PCF8591.h>
#include <Adafruit_PCF8592.h>

extern volatile int32_t left_coder_count;
extern volatile int32_t right_coder_count;
extern volatile boolean left_motion_state;
extern volatile boolean right_motion_state;
extern volatile boolean left_dir_state;
extern volatile boolean right_dir_state;

class TAI_finder_X1 {
  public: 
    TAI_finder_X1();
    void init();
    void camera_init();
    void ai_stick_init();
    void Servos_init();
    void set_Servo(int num, int degree);
    void show_all_led(int R, int G, int B);
    void set_pwm(int pwm_pin_num, int pwm);
    void set_four_pwm(int left_front_pwm, int left_back_pwm, int right_front_pwm, int right_back_pwm);
    void forward(int32_t dis_cm, uint8_t speed_pwm, boolean stop_state);
    void back(int32_t dis_cm, uint8_t speed_pwm, boolean stop_state);
    void left(int32_t dis_cm, uint8_t speed_pwm, boolean stop_state);
    void right(int32_t dis_cm, uint8_t speed_pwm, boolean stop_state);
    void turn_left(int32_t degree, uint8_t speed_pwm, boolean stop_state);
    void turn_right(int32_t degree, uint8_t speed_pwm, boolean stop_state);
    void motion(int motion_num, int dis_cm, int speed_pwm, boolean brake_state);
    void turn_motion(int motion_num, int degree, int speed_pwm, boolean brake_state);
    int get_front_sonar_dis_cm();
    int get_ir_value(int ir_num);
    int get_left_ir_value();
    int get_right_ir_value();
    int get_double_line_value();
    boolean get_color(int color_num);
    int get_color_info(int color_num, int info_num);
    boolean is_sign(int num);
    int get_finger_number();
    int get_polygon_number(int polygon_Type);
    int get_polygon();
    int readGyrZ();
    float read_Z_speed();
    void clear_coder_count();
    float get_distance_cm();
    float get_horizontal_distance_cm();
    void play_sound(int sound_num);
    void set_image_range(float num);
    void set_line_threshold(float num);
    void func1(int motion_num, int speed_pwm);
    void func2(int motion_num);
    void start_serial_command();
    int get_serial_command_value(int num);
    boolean is_serial_num(int num);
    void rc_control();
    void rc_control_24XingKuang();
    void set_rc_default_value(int num, float value);
    boolean get_self_lock_state();
    float get_steering_angle_degree();
    void set_gyro_z_init();
    void matrix_show(uint8_t *a, uint8_t *b, boolean state);
    void matrix_clear();
    Adafruit_8x16minimatrix matrix = Adafruit_8x16minimatrix();
    void transmission(int text, int row, int list);
	void servopulse(int servopin,int angle) ;
	void Bluetooth_Connect();
	int get_bluetooth_button_pressed(int port);
	void get_bluetooth_data();
	int get_bluetooth_remote_sensing_cmd(int port);
	int get_I2C_Gray(int pin) ;
	void initialize_gray();
    void get_initialize_gray();
	void init_DRV8830();
	void setMotor(int speed);
	void clearFaults();
	byte readRegister(byte reg);
	void writeRegister(byte reg, byte value);
	void moter_drive(int speed_pwm, uint32_t movement_time) ;
    void lcd_show(String text1, String text2);
    void lcd_clear();
    void get_pic(uint8_t pin,uint32_t time);
    void end_pic(uint8_t pin);
    void sendByte2pic(byte data);
    int gray_Threshold_value_1 = 0;
    int gray_Threshold_value_2 = 0;
    int gray_Threshold_value_3 = 0;
    int gray_Threshold_value_4 = 0;
    int gray_Threshold_value_5 = 0;
    int gray_Threshold_value_6 = 0;
    int gray_Threshold_value_7 = 0;
  private:
    float z_angle = 0;
    float z_angle_i = 0;
    int32_t delta = 0;
    int roll = 0;
    int pitch = 0;
    int thr = 0;
    int yaw = 0;
    int aux1 = 0;
    int aux2 = 0;
    int aux3 = 0;
    int aux4 = 0;
    int auto_state = 0;
    int servo0_default_value = 100;
    int servo1_default_value = 80;
    int servo2_default_value = 165;
    int servo0_min_value = 90;
    int servo1_min_value = 35;
    int servo2_min_value = 70;
    int servo0_max_value = 130;
    int servo1_max_value = 90;
    int servo2_max_value = 150;
    float roll_default_kp = 0.3;
    float pitch_default_kp = 0.3;
    float yaw_default_kp = 0.3;
    void WriteMPUReg(int nReg,unsigned char nVal);
    void AccGyr_init();
    Adafruit_NeoPixel LR_Strip = Adafruit_NeoPixel(LED_NUM, LED_PIN, NEO_GRB + NEO_KHZ800);
    FaBoPWM Servos;
	Adafruit_PCF8591 pcf = Adafruit_PCF8591();
	Adafruit_PCF8592 pcf2 = Adafruit_PCF8592();
    LiquidCrystal_I2C lcd = LiquidCrystal_I2C(0x00, 16, 2);
    long color_MAX_Waiting_Time = 0;
    int color_MAX_Data[5] = {0, 0, 0, 0, 0};
    int color_MAX_cmd_Flag = 0;
    uint8_t bluetooth_data[10] = {0x80, 0x80, 0x80, 0x80, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00};
	const byte DRV8830_Address = 0x64;

	// 寄存器地址
	const byte CONTROL_REGISTER = 0x00;
	const byte FAULT_REGISTER = 0x01;
};

#endif
