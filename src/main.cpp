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
#include <FS.h>

#include "i2c.h"
#include "mpu6500.h"
#include "ms5611.h"
#include "rocket_assert.h"

#define USE_HEARTBEAT 0  // Used for debugging

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

static const uint16_t MAXIMUM_SAMPLE_RATE = 1000; // Maximum frequency supported by the IMU
static volatile uint16_t sample_rate = MAXIMUM_SAMPLE_RATE;

static File log_file;
static const char *log_filename = "/log.txt";

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
  server.sendContent(log_file ? F("stop") : F("start"));
  server.sendContent(F("\" method=\"POST\">"));
  if (!log_file)
    server.sendContent(F("<input style=\"width:50%;\" type=\"number\" name=\"sample_rate\" placeholder=\"Sample rate\"></br>"));
  server.sendContent(F("<input style=\"width:50%;\" type=\"submit\" value=\""));
  server.sendContent(log_file ? F("Stop") : F("Start"));
  server.sendContent(F(" logging\"></form>"));

  if (!log_file && SPIFFS.exists(log_filename)) {
#if 1
  // Create link to the log file
  server.sendContent(F("<a href=\"/log.txt\" target=\"_blank\">log.txt</a>"));
#else
    // Create a textbox with the logged data
    File f = SPIFFS.open(log_filename, "r");
    if (f) {
      Serial.print(F("File size: ")); Serial.println(f.size());
      server.sendContent(F("<textarea style=\"width:50%;height:10em;\">"));
      while (f.available()) {
        server.sendContent(f.readStringUntil('\n'));
        yield(); // Needed in order to prevent triggering the watchdog timer
      }
      f.close(); // Close file
      server.sendContent(F("</textarea>"));
    }
#endif
  }

  server.sendContent(F("</body></html>")); // Close the body and html tags
  server.sendContent(F("")); // Tells the web client that the transfer is done
  server.client().stop();

  Serial.println(F("Finished sending content"));
}

// See: https://tttapa.github.io/ESP8266/Chap11%20-%20SPIFFS.html
static void handleLogFileRead(void) {
  if (SPIFFS.exists(log_filename)) {
    File f = SPIFFS.open(log_filename, "r");
    ROCKET_ASSERT(f);
    Serial.print(F("File size: ")); Serial.println(f.size());
    size_t sent = server.streamFile(f, "text/plain"); // Send the file
    Serial.print(F("Bytes sent: ")); Serial.println(sent);
    f.close(); // Close file
  } else
    server.send(404, F("text/plain"), F("404: Not Found"));
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
  server.sendHeader(F("Location"), F("/")); // Add a header to respond with a new location for the browser to go to the home page again
  server.send(303); // Send it back to the browser with an HTTP status 303 (See Other) to redirect
}

static void loggingStart(void) {
  // Closed file it is is already open
  if (log_file) {
    Serial.println(F("Closed exiting logging file"));
    log_file.close();
  }

  // Open a file for writing
  if (SPIFFS.exists(log_filename)) {
    Serial.println(F("Removing existing file"));
    SPIFFS.remove(log_filename);
  }
  log_file = SPIFFS.open(log_filename, "w");
  ROCKET_ASSERT(log_file);
  log_file.println(F("Timestamp,pressure,gyroX,gyroY,gyroZ,accX,accY,accZ"));
  Serial.println(F("Opened logging file"));

  // Start logging
  logging(true);
  Serial.println(F("Logging started"));
}

static void loggingStop(void) {
  // Closed any existing file
  if (log_file) {
    Serial.println(F("Closed logging file"));
    log_file.close();
  }

  // Stop logging
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
  Serial.println(F("\nStarting RocketLogger"));

  // Initailize the file system
  ROCKET_ASSERT(SPIFFS.begin());
  //ROCKET_ASSERT(SPIFFS.format());
  Serial.println(F("File system was initailize"));

  // Initialize the I2C and configure the IMU and barometer
  I2C_Init(2, 3); // SDA: GPIO2 and SCL: GPIO3
  MPU6500_Init(&mpu6500, MAXIMUM_SAMPLE_RATE);
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
  server.on(log_filename, handleLogFileRead);
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
        if (log_file) {
          log_file.print(micros());
          log_file.write(',');
          log_file.print(ms5611.pressure);
          log_file.write(',');
          log_file.print(mpu6500.gyroRate.roll, 4);
          log_file.write(',');
          log_file.print(mpu6500.gyroRate.pitch, 4);
          log_file.write(',');
          log_file.print(mpu6500.gyroRate.yaw, 4);
          log_file.write(',');
          log_file.print(mpu6500.accSi.X, 4);
          log_file.write(',');
          log_file.print(mpu6500.accSi.Y, 4);
          log_file.write(',');
          log_file.println(mpu6500.accSi.Z, 4);

          static uint8_t check_files_info_counter = 0;
          if (++check_files_info_counter >= 10) {
            check_files_info_counter = 0;
            // Determine if the file system is full
            FSInfo fs_info;
            SPIFFS.info(fs_info);

            // TODO: Why does it stop working before it is actually full?
            // It seems to have something to do with the blocks
            if (fs_info.usedBytes + 2 * fs_info.blockSize >= fs_info.totalBytes) {
              log_file.close();
              Serial.println(F("Logging ended"));
            }
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
  static uint32_t timer = 0;
  uint32_t now = micros();
  uint32_t dt_us = now - timer;
  timer = now;
  uint32_t sleep_us = 1000000U / sample_rate;
  if (dt_us < sleep_us) {
    sleep_us -= dt_us;
    delayMicroseconds(sleep_us);
  }
}
