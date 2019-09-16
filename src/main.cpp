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

// ESP-01 pinout:
//  GPIO0: Low: bootloader, High: run from flash
//  GPIO1: TX
//  GPIO2: SDA
//  GPIO3: RX/SCL

#include <Arduino.h>
#include <DNSServer.h>
#include <ESPAsyncWebServer.h>
#include <FS.h>

#include "i2c.h"
#include "mpu6500.h"
#include "ms5611.h"
#include "rocket_assert.h"

#define USE_HEARTBEAT 0  // Used for debugging

static AsyncWebServer server(80);
static DNSServer dnsServer;

const char *ssid = "Rocket";
const char *password = "rocketsrocks";

static mpu6500_t mpu6500;
static ms5611_t ms5611;

static const uint16_t MAXIMUM_SAMPLE_RATE = 1000; // Maximum frequency supported by the IMU
static volatile uint16_t sample_rate = MAXIMUM_SAMPLE_RATE;

static uint32_t start_timestamp = 0;

struct log_t {
  uint32_t timestamp;
  int32_t pressure;
  float gyroX, gyroY, gyroZ;
  float accX, accY, accZ;
} __attribute__((packed));

static File log_file;
static constexpr const char *log_filename = "/log.bin";

static void handleRoot(AsyncWebServerRequest *request) {
  Serial.println(F("Sending root content"));

  AsyncResponseStream *response = request->beginResponseStream(F("text/html"));
  response->addHeader(F("Cache-Control"), F("no-cache,no-store,must-revalidate"));
  response->addHeader(F("Pragma"), F("no-cache"));
  response->addHeader(F("Expires"), F("-1"));

  // Format the HTML response
  response->print(F("<html><head><meta name=\"viewport\" content=\"width=device-width,initial-scale=1.0,minimum-scale=1.0,maximum-scale=1.0,user-scalable=no,viewport-fit=cover\"></head>"));
  response->print(F("<body style=\"margin:50px auto;text-align:center;\">"));
  response->print(F("<span>Sample rate: "));
  response->print(String(sample_rate));
  response->print(F(" Hz (max: "));
  response->print(String(MAXIMUM_SAMPLE_RATE));
  response->print(F(" Hz) </span>"));
  response->print(F("<form action=\"/"));
  response->print(log_file ? F("stop") : F("start")); // Check if the file is open
  response->print(F("\" method=\"POST\">"));
  if (!log_file) // Check if the file is closed
    response->print(F("<input style=\"width:50%;\" type=\"number\" name=\"sample_rate\" placeholder=\"Sample rate\"></br>"));
  response->print(F("<input style=\"width:50%;\" type=\"submit\" value=\""));
  response->print(log_file ? F("Stop") : F("Start"));
  response->print(F(" logging\"></form>"));
  if (!log_file && SPIFFS.exists(log_filename)) // Make sure the log file is closed and exist
    response->print(F("<a href=\"/log.txt\" target=\"_blank\">log.txt</a>")); // Create link to the log file
  response->print(F("</body></html>")); // Close the body and html tags

  request->send(response); // Send the response
  Serial.println(F("Finished sending root content"));
}

// See: https://tttapa.github.io/ESP8266/Chap11%20-%20SPIFFS.html
static void handleLogFileRead(AsyncWebServerRequest *request) {
  static size_t row_count = 0;
  // Make sure the log file is closed and exist
  // and make sure that we are not already sending the file
  if (!log_file && SPIFFS.exists(log_filename) && row_count == 0) {
    // Send the binary data as a normal CSV text file
    AsyncWebServerResponse *response = request->beginChunkedResponse("text/plain", [](uint8_t *buffer, size_t maxLen, size_t index) -> size_t {
      // Write up to "maxLen" bytes into "buffer" and return the amount written.
      // index equals the amount of bytes that have been already sent
      // You will be asked for more data until 0 is returned
      // Keep in mind that you can not delay or yield waiting for more data!

      //Serial.printf("maxLen: %u, index: %u\n", maxLen, index);
      size_t len = 0;
      if (index == 0) { // This is the first response, so copy over the header
        // Write the header
        Serial.println(F("Sending log file"));
        int copied = snprintf((char*)buffer, maxLen, // Make sure we do not overflow the buffer
          "Timestamp,pressure,altitude,gyroX,gyroY,gyroZ,accX,accY,accZ\n");
        ROCKET_ASSERT(copied >= 0); // Make sure snprintf does not fail
        //Serial.printf("Bytes copied: %u\n", copied);
        len += copied; // Add the number of bytes we just wrote to the buffer
      } else {
        File f = SPIFFS.open(log_filename, "r");
        ROCKET_ASSERT(f);
        ROCKET_ASSERT(f.size() % sizeof(log_t) == 0); // If this fails, then the file is corrupted

        //Serial.printf("File size: %u, row count: %u, log size: %u\n", f.size(), row_count, sizeof(log_t));
        while (row_count * sizeof(log_t) < f.size()) { // Stop when we are done reading the file
          ROCKET_ASSERT(f.seek(row_count * sizeof(log_t), SeekSet)); // Go to the current row
          row_count++; // Increment the row counter
          log_t log;
          ROCKET_ASSERT((int)f.read((uint8_t*)&log, sizeof(log_t)) != -1); // Now read one log of data from the file

          // Convert the binary data into a CSV format and copy it into the output buffer
          // This code assumes that we have at least room for one row of data in each response or the string will be truncated
          int copied = snprintf((char*)&buffer[len], maxLen - len, // Make sure we do not overflow the buffer
            "%u,%d,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f\n",
            log.timestamp, log.pressure, MS5611_GetAbsoluteAltitude(log.pressure),
            log.gyroX, log.gyroY, log.gyroZ,
            log.accX, log.accY, log.accZ);
          ROCKET_ASSERT(copied >= 0); // Make sure snprintf does not fail
          //Serial.printf("Bytes copied: %u\n", copied);
          len += copied; // Add the number of bytes we just wrote to the buffer
          if (len + 2 * copied > maxLen) // Make sure we have room for at least two more rows of data
            break;
        }
        f.close();
      }

      // Check if we are done reading the file
      if (len == 0) {
        Serial.println(F("Done sending log file"));
        row_count = 0; // Reset the row count and allow another request to access the file
      }

      //Serial.printf("Total bytes copied: %u\n", len);
      return len;
    });
    request->send(response);
  } else
    request->send(404, F("text/plain"), F("404: Not Found"));
}

static void loggingRedirect(AsyncWebServerRequest *request) {
  if (request->hasArg("sample_rate")) {
    int new_sample_rate = request->arg("sample_rate").toInt();
    if (new_sample_rate > 0) { // Make sure it was not an empty string
      if (new_sample_rate > MAXIMUM_SAMPLE_RATE)
        new_sample_rate = MAXIMUM_SAMPLE_RATE;
      sample_rate = new_sample_rate;
      Serial.print(F("New sample rate: "));
      Serial.println(sample_rate);
    }
  }
  request->redirect(F("/")); // Redirect to the root
}

static void loggingStart(AsyncWebServerRequest *request) {
  // Closed file it is is already open
  if (log_file) { // Check if the file is open
    Serial.println(F("Closed exiting logging file"));
    log_file.close();
  }

  // Delete the existing file
  if (SPIFFS.exists(log_filename)) {
    Serial.println(F("Removing existing file"));
    SPIFFS.remove(log_filename);
  }

  start_timestamp = micros(); // Reset the start timestamp
  log_file = SPIFFS.open(log_filename, "w"); // Open a file for writing
  ROCKET_ASSERT(log_file);
  Serial.println(F("Logging started"));

  // Automatically redirect the user to the root page
  loggingRedirect(request);
}

static void loggingStop(AsyncWebServerRequest *request) {
  // Closed any existing file
  if (log_file) { // Check if the file is open
    Serial.println(F("Closed logging file"));
    log_file.close();
  }
  Serial.println(F("Logging stopped"));

  // Automatically redirect the user to the root page
  loggingRedirect(request);
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
  Serial.println(F("File system was initailize"));

  // Initialize the I2C and configure the IMU and barometer
  I2C_Init(2, 3); // SDA: GPIO2 and SCL: GPIO3
  MPU6500_Init(&mpu6500, MAXIMUM_SAMPLE_RATE);
  Serial.println(F("MPU6500 configured"));

  MS5611_Init(&ms5611, MS5611_OSR_256); // Sample as fast as possible
  Serial.println(F("MS5611 configured"));

  // Configure the hotspot
  // Note that we set the maximum number of connection to 1, as access to the log file is not thread safe
  int channel = 1, ssid_hidden = 0, max_connection = 1;
  ROCKET_ASSERT(WiFi.softAP(ssid, password, channel, ssid_hidden, max_connection));
  IPAddress myIP = WiFi.softAPIP();
  Serial.print(F("AP IP address: "));
  Serial.println(myIP);

  if (dnsServer.start(53, "*", myIP)) // Redirect all requests to the logger
    Serial.println(F("DNS server started"));
  else
    Serial.println(F("Failed to start DNS server"));

  // Start the websever
  server.on("/", HTTP_GET, handleRoot);
  server.on("/log.txt", HTTP_GET, handleLogFileRead); // This will convert the binary log file into a CSV format
  server.on(log_filename, HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(SPIFFS, log_filename, "application/octet-stream"); // Send the log file in binary format
  });
  //server.serveStatic(LogFile::Filename, SPIFFS, LogFile::Filename);
  server.on("/start", HTTP_POST, loggingStart);
  server.on("/stop", HTTP_POST, loggingStop);
  server.on("/format", HTTP_GET, [](AsyncWebServerRequest *request) {
    ROCKET_ASSERT(SPIFFS.format());
    request->send(200, F("text/plain"), F("Filesystem successfully formatted"));
  });
  //server.serveStatic("/fs", SPIFFS, "/"); // Attach filesystem root at URL /fs
  server.onNotFound([](AsyncWebServerRequest *request) {
    request->send(404, F("text/plain"), F("404: Not Found"));
  });
  server.begin();
  Serial.println(F("HTTP server started"));
}

void loop() {
  dnsServer.processNextRequest();

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
        if (log_file) { // Check if the file is open
          log_t log = {
            .timestamp = micros() - start_timestamp,
            .pressure = ms5611.pressure,
            .gyroX = mpu6500.gyroRate.roll * RAD_TO_DEGf,
            .gyroY = mpu6500.gyroRate.pitch * RAD_TO_DEGf,
            .gyroZ = mpu6500.gyroRate.yaw * RAD_TO_DEGf,
            .accX = mpu6500.accSi.X,
            .accY = mpu6500.accSi.Y,
            .accZ = mpu6500.accSi.Z,
          };

          // Note that this might fail, but we do not care, as we will just write as fast as possible
          log_file.write((uint8_t*)&log, sizeof(log));

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
              Serial.print(F("Logging ended after: "));
              Serial.print((float)(micros() - start_timestamp) * 1e-6f);
              Serial.println(F(" s"));
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
  } else
    yield(); // Make sure we allow the RTOS to run other tasks
}
