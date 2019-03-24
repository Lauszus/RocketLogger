/* Copyright (C) 2019 Kristian Lauszus and Mads Bornebusch. All rights reserved.

 This software may be distributed and modified under the terms of the GNU
 General Public License version 2 (GPL2) as published by the Free Software
 Foundation and appearing in the file GPL2.TXT included in the packaging of
 this file. Please note that GPL2 Section 2[b] requires that all works based
 on this software must also be made publicly available under the terms of
 the GPL2 ("Copyleft").

 Contact information
 -------------------

 Kristian Lauszus
 Web      :  https://lauszus.com
 e-mail   :  lauszus@gmail.com
*/

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>

#include "i2c.h"
#include "mpu6500.h"
#include "ms5611.h"

#define USE_HEARTBEAT 0  // Used for debugging
#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0])) // Handy macro for getting the size of an array

// ESP-01 pinout:
//  GPIO0: Low: bootloader, High: run from flash
//  GPIO1: TX
//  GPIO2: SDA
//  GPIO3: RX/SCL

ESP8266WebServer server(80);

const char *ssid = "Rocket";
const char *password = "rocketsrocks";

static mpu6500_t mpu6500;
static ms5611_t ms5611;

typedef struct {
  uint16_t timestamp; // The timestamp in ms
  int32_t pressure; // Pressure in pascal
  float mag_acceleration; // The magnitude of the acceleration in m/s^2
} logging_data_t;

static volatile bool logging_is_running = false;
static volatile size_t logging_idx = 0;
static volatile logging_data_t logging_data[3500]; // x seconds worth of data
static const uint16_t MAXIMUM_SAMPLE_RATE = 200;
static volatile uint16_t sample_rate = MAXIMUM_SAMPLE_RATE;

static void handleRoot(void) {
  Serial.println(F("Sending content"));

  server.sendHeader(F("Cache-Control"), F("no-cache,no-store,must-revalidate"));
  server.sendHeader(F("Pragma"), F("no-cache"));
  server.sendHeader(F("Expires"), F("-1"));
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);

  // Begin chunked transfer
  server.send(200, F("text/html"), F(""));
  server.sendContent(F("<html><head><meta name=\"viewport\" content=\"width=device-width,initial-scale=1.0,minimum-scale=1.0,maximum-scale=1.0,user-scalable=no,viewport-fit=cover\"></head>"));
  server.sendContent(F("<body style=\"margin:50px auto;text-align:center;\">"));
  server.sendContent(F("<span>Sample rate: "));
  server.sendContent(String(sample_rate));
  server.sendContent(F(" Hz (max: "));
  server.sendContent(String(MAXIMUM_SAMPLE_RATE));
  server.sendContent(F(" Hz) </span>"));
  server.sendContent(F("<form action=\"/"));
  server.sendContent(logging_is_running ? F("stop") : F("start"));
  server.sendContent(F("\" method=\"POST\">"));
  if (!logging_is_running)
    server.sendContent(F("<input style=\"width:50%;\" type=\"number\" name=\"sample_rate\" placeholder=\"Sample rate\"></br>"));
  server.sendContent(F("<input style=\"width:50%;\" type=\"submit\" value=\""));
  server.sendContent(logging_is_running ? F("Stop") : F("Start"));
  server.sendContent(F(" logging\"></form>"));

  // Create a textbox with the logged data
  if (!logging_is_running) {
    server.sendContent(F("<textarea style=\"width:50%;height:10em;\">"));
    for (size_t i = 0; i < logging_idx; i++) {
      String data;
      data.concat(logging_data[i].timestamp);
      data.concat(',');
      data.concat(logging_data[i].pressure);
      data.concat(',');
      data.concat(String(logging_data[i].mag_acceleration, 4));
      data.concat('\n');
      server.sendContent(data);
    }
    server.sendContent(F("</textarea>"));
  }

  server.sendContent(F("</body></html>")); // Close the body and html tags
  server.sendContent(F("")); // Tells the web client that the transfer is done
  server.client().stop();

  Serial.println(F("Finished sending content"));
}

static void logging(bool start) {
  if(server.hasArg("sample_rate")) {
    int new_sample_rate = server.arg("sample_rate").toInt();
    if (new_sample_rate > 0) { // Make sure it was not an empty string
      if (new_sample_rate > MAXIMUM_SAMPLE_RATE)
        new_sample_rate = MAXIMUM_SAMPLE_RATE;
      sample_rate = new_sample_rate;
      Serial.print(F("New sample rate: "));
      Serial.println(sample_rate);
    }
  }
  if (start)
    logging_idx = 0;
  logging_is_running = start;
  server.sendHeader(F("Location"), F("/")); // Add a header to respond with a new location for the browser to go to the home page again
  server.send(303); // Send it back to the browser with an HTTP status 303 (See Other) to redirect
}

static void loggingStart(void) {
  logging(true);
  Serial.println(F("Logging started"));
}

static void loggingStop(void) {
  logging(false);
  Serial.println(F("Logging stopped"));
}

#if USE_HEARTBEAT
#include <os_type.h>
static const uint8_t led_pin = 1; // The builtin LED is active low
static os_timer_t heartbeat_timer;
static void heartbeat_timerfunc(void *arg) {
  static bool led_state = false;
  led_state = !led_state;
  digitalWrite(led_pin, led_state);
}
#endif

void setup() {
#if USE_HEARTBEAT
  pinMode(led_pin, OUTPUT);
  os_timer_setfn(&heartbeat_timer, (os_timer_func_t*)&heartbeat_timerfunc, NULL);
  os_timer_arm(&heartbeat_timer, 100 /*ms*/, 1 /*repeating*/);
#endif

  // This has to be enabled before the I2C, as we won't be using the RX pin
  Serial.begin(74880);

  // Initialize the I2C and configure the IMU and barometer
  I2C_Init(2, 3);
  MPU6500_Init(&mpu6500, MAXIMUM_SAMPLE_RATE); // Sample at 200 Hz
  Serial.println(F("MPU6500 configured"));

  MS5611_Init(&ms5611, MS5611_OSR_256); // Sample as fast as possible
  Serial.println(F("MS5611 configured"));

  // Configure the hotspot
  WiFi.softAP(ssid, password);
  IPAddress myIP = WiFi.softAPIP();
  Serial.print(F("AP IP address: "));
  Serial.println(myIP);

  // Start the websever
  server.on(F("/"), handleRoot);
  server.on(F("/start"), HTTP_POST, loggingStart);
  server.on(F("/stop"), HTTP_POST, loggingStop);
  server.begin();
  Serial.println(F("HTTP server started"));
}

void loop() {
  server.handleClient();

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

      rcode = MS5611_GetData(&ms5611);
      if (rcode == 0) {
#if 0
        Serial.print(ms5611.pressure); Serial.print(F(" Pa,"));
        Serial.print(ms5611.altitude); Serial.print(F(" m, "));
        Serial.print(ms5611.temperature); Serial.print(F(" C\n"));
#endif
        if (logging_is_running) {
          logging_data[logging_idx].timestamp = millis() % UINT16_MAX;
          logging_data[logging_idx].pressure = ms5611.pressure;
          logging_data[logging_idx].mag_acceleration = sqrtf(mpu6500.accSi.X * mpu6500.accSi.X + mpu6500.accSi.Y * mpu6500.accSi.Y + mpu6500.accSi.Z * mpu6500.accSi.Z);

          if (++logging_idx >= ARRAY_SIZE(logging_data)) {
            logging_is_running = false;
            Serial.println(F("Logging ended"));
          }
        }
      } else {
        Serial.print(F("Failed reading MS5611: "));
        Serial.println(rcode);
      }
    }
  } else {
    Serial.print(F("Failed reading MS6500: "));
    Serial.println(rcode);
  }

  // Sample according to the sample rate
  // This won't be accorate,
  // as it does not take into account the time it takes to read the sensors
  delay(1000 / sample_rate);
}
