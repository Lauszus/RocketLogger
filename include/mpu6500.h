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

#ifndef __mpu6500_h__
#define __mpu6500_h__

#include <stdint.h>

#define GRAVITATIONAL_ACCELERATION          (9.80665f) // https://en.wikipedia.org/wiki/Gravitational_acceleration
#define MPU_INT_FREQ_HZ                     (200U) // Sample frequency
#define DEG_TO_RADf                         (0.017453292519943295769236907684886f)
#define RAD_TO_DEGf                         (57.295779513082320876798154814105f)

typedef union {
  struct {
    int16_t X, Y, Z;
  } __attribute__((packed));
  int16_t data[3];
} sensorRaw_t;

typedef union {
  struct {
    float X, Y, Z;
  } __attribute__((packed));
  float data[3];
} sensor_t;

typedef union {
  struct {
    float roll, pitch, yaw;
  } __attribute__((packed));
  float data[3];
} angle_t;

/** Struct for MPU-6500 data */
typedef struct {
  float gyroScaleFactor; /*!< Gyroscope scale factor */
  float accScaleFactor; /*!< Accelerometer scale factor */
  angle_t gyroRate; /*!< Gyroscope readings in rad/s */
  sensor_t accSi; /*!< Accelerometer readings in m/s^2 */
} mpu6500_t;

void MPU6500_Init(mpu6500_t *mpu6500);

uint8_t MPU6500_DateReady(bool *ready);

uint8_t MPU6500_GetData(mpu6500_t *mpu6500);

#endif // __mpu6500_h__
