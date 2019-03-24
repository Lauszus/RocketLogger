/* Copyright (C) 2019 Kristian Lauszus. All rights reserved.

 This software may be distributed and modified under the terms of the GNU
 General Public License version 2 (GPL2) as published by the Free Software
 Foundation and appearing in the file GPL2.TXT included in the packaging of
 this file. Please note that GPL2 Section 2[b] requires that all works based
 on this software must also be made publicly available under the terms of
 the GPL2 ("Copyleft").

 Contact information
 -------------------

 Kristian Lauszus
 Web      :  http://www.tkjelectronics.com
 e-mail   :  kristianl@tkjelectronics.com
*/

#include <Arduino.h>
#include <os_type.h>

#include "i2c.h"
#include "mpu6500.h"
#include "ms5611.h"

// ESP-01 pinout:
//  GPIO0: NA
//  GPIO1: TX
//  GPIO2: SDA
//  GPIO3: RX/SCL

// Used for debugging
#define USE_HEARTBEAT 0

#if USE_HEARTBEAT
static const uint8_t led_pin = 1; // The builtin LED is active low
static os_timer_t heartbeat_timer;
static void heartbeat_timerfunc(void *arg) {
  static bool led_state = false;
  led_state = !led_state;
  digitalWrite(led_pin, led_state);
}
#endif

static mpu6500_t mpu6500;
static ms5611_t ms5611;

void setup() {
#if USE_HEARTBEAT
  pinMode(led_pin, OUTPUT);
  os_timer_setfn(&heartbeat_timer, (os_timer_func_t*)&heartbeat_timerfunc, NULL);
  os_timer_arm(&heartbeat_timer, 100 /*ms*/, 1 /*repeating*/);
#endif

  // This has to be enabled before the I2C, as we won't be using the RX pin
  Serial.begin(74880);
  Serial.println(F("Started"));

  I2C_Init(2, 3);
  MPU6500_Init(&mpu6500);
  Serial.println(F("MPU6500 configured"));

  MS5611_Init(&ms5611, MS5611_OSR_4096);
  Serial.println(F("MS5611 configured"));
}

void loop() {
  bool ready;
  uint8_t rcode = MPU6500_DateReady(&ready);
  if (rcode == 0) {
    if (ready) {
      MPU6500_GetData(&mpu6500);
#if 0
      Serial.print(mpu6500.gyroRate.roll * RAD_TO_DEGf); Serial.write(',');
      Serial.print(mpu6500.gyroRate.pitch * RAD_TO_DEGf); Serial.write(',');
      Serial.print(mpu6500.gyroRate.yaw * RAD_TO_DEGf); Serial.write(',');
      Serial.print(mpu6500.accSi.X); Serial.write(',');
      Serial.print(mpu6500.accSi.Y); Serial.write(',');
      Serial.println(mpu6500.accSi.Z);
#endif
    }
  } else {
    Serial.write("Failed reading MS6500: ");
    Serial.println(rcode);
  }

  rcode = MS5611_GetData(&ms5611);
  if (rcode == 0) {
    Serial.print(ms5611.pressure); Serial.write(" Pa,");
    Serial.print(ms5611.altitude); Serial.write(" m, ");
    Serial.print(ms5611.temperature); Serial.write(" C\n");
    delay(1000);
  } else {
    Serial.write("Failed reading MS5611: ");
    Serial.println(rcode);
  }
}
