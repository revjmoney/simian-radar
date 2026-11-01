#include <Arduino.h>
#include <Wire.h>
#include <U8g2lib.h>
#include "Config.h"

U8G2_SSD1306_128X64_NONAME_F_HW_I2C oled(U8G2_R1, kPinOledRst);

void setup() {
  pinMode(kPinVext, OUTPUT);
  digitalWrite(kPinVext, HIGH);
  delay(50);
  Wire.begin(kPinSda, kPinScl);
  oled.begin();
  oled.clearBuffer();
  oled.setFont(u8g2_font_7x13B_tr);
  oled.drawStr(11, 30, "SIMIAN");
  oled.drawStr(15, 46, "RADAR");
  oled.setFont(u8g2_font_4x6_tr);
  oled.drawStr(2, 100, "(c) OpenStreet");
  oled.drawStr(2, 108, "Map contributors");
  oled.sendBuffer();
  pinMode(kPinLed, OUTPUT);
  digitalWrite(kPinLed, HIGH);
  delay(200);
  digitalWrite(kPinLed, LOW);
}

void loop() { delay(1000); }
