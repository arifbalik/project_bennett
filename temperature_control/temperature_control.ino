/*********************************************************************
Written in 19/1/2020 by Arif Ahmet Balik
*********************************************************************/

#include "ThingSpeak.h"
#include <Adafruit_GFX.h>
#include <Adafruit_PCD8544.h>
#include <DallasTemperature.h>
#include <EEPROM.h>
#include <ESP8266WiFi.h>
#include <OneWire.h>
#include <PID_v1.h>
#include <SPI.h>

#define WL_SSID "Superbox_WiFi_2.4GHz_3076"
#define PASS "yildizteknik2019"
const char *API_KEY = "35Q24V1QT4XY4TVC";
unsigned long CHANNEL_ID = 964989;

#define BUTTON_UP D5
#define BUTTON_DOWN D6
#define BUTTON_START_STOP D7
#define TEMP_SENSORS 3
#define MOSFET D8
#define HEAT_BED A0

#define MAX_TEMP 100
#define MIN_TEMP 10
#define HEAT_BED_TIMEOUT 5 * 60000 /* 5 mins */
#define PID_THRESHOLD                                                          \
  10 /* Pid is avtivated when actual reading is target - PID_THRESHOLD */

double Kp = 2;
double Ki = 5;
double Kd = 1;

typedef struct {
  double top, bottom, avg, set;

  double mosfet_out;
  uint8_t state; // 0: off, 1: on
} temp_t;

void handle_buttons();
void handle_client();
void get_temp();
void get_eeprom();

int c_millis, p_millis, button_scan_millis, pid_millis, pid_menu_count,
    pid_menu_active, pid_val, pick_param, is_attached, client_callback,
    temp_callback, heat_bed_timer;
temp_t temp;
OneWire oneWire(TEMP_SENSORS);
DallasTemperature sensors(&oneWire);
PID pid(&(temp.avg), &(temp.mosfet_out), &(temp.set), (double)Kp, (double)Ki,
        (double)Kd, DIRECT);
Adafruit_PCD8544 display = Adafruit_PCD8544(D4, D3, D2, D1, D0);
WiFiClient client;

void setup() {
  EEPROM.begin(4);

  get_eeprom();

  WiFi.mode(WIFI_STA);
  ThingSpeak.begin(client);

  display.begin();
  display.setContrast(60);

  sensors.begin();

  pinMode(BUTTON_UP, INPUT_PULLUP);
  pinMode(BUTTON_DOWN, INPUT_PULLUP);
  pinMode(BUTTON_START_STOP, INPUT_PULLUP);
}

void get_temp() {
  if (is_attached == 0)
    return;
  sensors.requestTemperatures();

  temp.top = sensors.getTempCByIndex(0);
  temp.bottom = sensors.getTempCByIndex(1);

  temp.avg = (temp.top + temp.bottom) / 2;
}
void handle_temp(temp_t *t) {
  if (t->state == 0) {
    t->mosfet_out = 0;
    analogWrite(MOSFET, 0);
    // digitalWrite(LED, LOW);
    pid.SetMode(MANUAL);
    return;
  }

  if (abs(t->set - t->avg) <= PID_THRESHOLD) {
    pid.SetTunings(Kp, Ki, Kd);
    pid.Compute();
    analogWrite(MOSFET, t->mosfet_out);
    pid.SetMode(AUTOMATIC);
  } else {
    t->mosfet_out = 255;
    analogWrite(MOSFET, 255);
  }
}

void get_eeprom() {
  Kp = EEPROM.read(0);
  Ki = EEPROM.read(1);
  Kd = EEPROM.read(2);
  temp.set = EEPROM.read(3);
}

void update_eeprom() {
  EEPROM.write(0, Kp);
  EEPROM.write(1, Ki);
  EEPROM.write(2, Kd);
  EEPROM.write(3, temp.set);
  EEPROM.commit();
}

void set_pid(int val) {
  switch (pick_param) {
  case 0:
    Kp += val;
    break;
  case 1:
    Ki += val;
    break;
  case 2:
    Kd += val;
    break;
  default:
    break;
  }
  update_eeprom();
}

void handle_buttons() {
  if ((millis() - button_scan_millis) < 200)
    return;

  if (!digitalRead(BUTTON_UP)) {
    if (pid_menu_active)
      set_pid(1);
    else
      temp.set += 5;
    p_millis = millis();
  } else if (!digitalRead(BUTTON_DOWN)) {
    if (pid_menu_active)
      set_pid(-1);
    else
      temp.set -= 5;
    p_millis = millis();
  }

  if (!digitalRead(BUTTON_START_STOP)) {
    if (pid_menu_active)
      pick_param = (pick_param == 0)
                       ? 1
                       : (pick_param == 1) ? 2 : (pick_param == 2) ? 0 : 0;
    else {
      temp.state = (temp.state == 0) ? 1 : 0;
      update_eeprom();
    }
    if ((millis() - pid_millis) < 500) {
      pid_menu_count++;
      if (pid_menu_count > 5) {
        pid_menu_count = 0;
        pid_menu_active = (pid_menu_active) ? 0 : 1;
      }
    } else {
      pid_menu_count = 0;
    }
    p_millis = millis();
    pid_millis = millis();
  }
  if (temp.set >= MAX_TEMP)
    temp.set = MAX_TEMP;
  else if (temp.set <= MIN_TEMP)
    temp.set = MIN_TEMP;

  if ((millis() - p_millis) > 2000 && is_attached == 0) {
    is_attached = 1;
  } else if (is_attached == 1) {
    is_attached = 0;
  }

  button_scan_millis = millis();
}
void kill(){
  analogWrite(MOSFET, 0);
  while(1);
}
void pid_menu() {

  display.clearDisplay();
  display.setCursor(0, 0);
  display.setTextSize(1);

  display.println((pick_param == 0) ? ">Kp:" + String(Kp)
                                    : " Kp:" + String(Kp));
  display.println((pick_param == 1) ? ">Ki:" + String(Ki)
                                    : " Ki:" + String(Ki));
  display.println((pick_param == 2) ? ">Kd:" + String(Kd)
                                    : " Kd:" + String(Kd));
  display.display();
}

void update_lcd(temp_t *t) {
  if (pid_menu_active) {
    pid_menu();
    return;
  }
  display.clearDisplay();
  display.setCursor(0, 0);

  if (t->avg < 0 || t->avg > (MAX_TEMP + 10)) {
    display.setTextSize(2);
    display.println("\n ERROR!");
    display.display();
    kill();
    return;
  }

  display.setTextSize(2);
  display.println(" " + String(t->avg));

  display.setTextSize(1);

  display.println("T:" + String(t->top) + "\nB:" + String(t->bottom) +
                  "\nSet:" + String(t->set) + "\nPower:" +
                  String(map(t->mosfet_out, 0, 255, 0, 100)) + "% "+ (t->state == 0) ? "OFF" : "ON" );

  display.display();
}

void handle_client() {
  if (is_attached == 0)
    return;

  if (WiFi.status() != WL_CONNECTED) {
    display.clearDisplay();
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.println("Connecting to WiFi");
    display.display();
    while (WiFi.status() != WL_CONNECTED) {
      WiFi.begin(WL_SSID, PASS);

      display.print(".");
      display.display();
      delay(5000);
    }
  }

  ThingSpeak.setField(1, (float)temp.top);
  ThingSpeak.setField(2, (float)temp.bottom);
  ThingSpeak.setField(3, (float)temp.avg);
  ThingSpeak.setField(4, (float)temp.set);
  ThingSpeak.setField(5, (float)temp.mosfet_out);

  ThingSpeak.writeFields(CHANNEL_ID, API_KEY);
}

void loop() {
  if (analogRead(HEAT_BED) > 50)
    heat_bed_timer = millis();

  if ((millis() - client_callback) > 10000) {
    client_callback = millis();
    handle_client();
  }
  if ((millis() - temp_callback) > 1000) {
    temp_callback = millis();
    get_temp();
  }
  handle_buttons();
  update_lcd(&temp);
  if ((millis() - heat_bed_timer) > HEAT_BED_TIMEOUT) {
    analogWrite(MOSFET, 0);
    temp.mosfet_out = 0;
  } else {
    handle_temp(&temp);
  }
}
