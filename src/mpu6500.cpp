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

#include "rocket_assert.h"
#include "i2c.h"
#include "mpu6500.h"

#define MPU6500_ADDRESS                     0x68
#define MPU6500_WHO_AM_I_ID                 0x70

#define MPU6500_SMPLRT_DIV                  0x19 /*!< Sample Rate Divider register */
#define MPU6500_INT_PIN_CFG                 0x37 /*!< INT Pin / Bypass Enable Configuration register */
#define MPU6500_INT_STATUS                  0x3A /*!< Interrupts status register */
#define MPU6500_ACCEL_XOUT_H                0x3B /*!< Start of Accelerometer Measurements registers */
#define MPU6500_GYRO_XOUT_H                 0x43 /*!< Start of Gyroscope Measurements registers */
#define MPU6500_USER_CTRL                   0x6A /*!< User Control register */
#define MPU6500_PWR_MGMT_1                  0x6B /*!< Power Management 1 register */
#define MPU6500_WHO_AM_I                    0x75 /*!< Who Am I register */

// MPU-6500 scale factors
#define MPU6500_GYRO_SCALE_FACTOR_250       131.0f /*!< Gyroscope scale factor of +-250 deg/s */
#define MPU6500_GYRO_SCALE_FACTOR_500       65.5f /*!< Gyroscope scale factor of +-500 deg/s */
#define MPU6500_GYRO_SCALE_FACTOR_1000      32.8f /*!< Gyroscope scale factor of +-1000 deg/s */
#define MPU6500_GYRO_SCALE_FACTOR_2000      16.4f /*!< Gyroscope scale factor of +-2000 deg/s */

#define MPU6500_ACC_SCALE_FACTOR_2          16384.0f /*!< Accelerometer scale factor of +-2 g */
#define MPU6500_ACC_SCALE_FACTOR_4          8192.0f /*!< Accelerometer scale factor of +-4 g */
#define MPU6500_ACC_SCALE_FACTOR_8          4096.0f /*!< Accelerometer scale factor of +-8 g */
#define MPU6500_ACC_SCALE_FACTOR_16         2048.0f /*!< Accelerometer scale factor of +-16 g */

void MPU6500_Init(mpu6500_t *mpu6500, uint16_t sample_rate) {
  uint8_t buf[5]; // Buffer for I2C data
  ROCKET_ASSERT(I2C_ReadData(MPU6500_ADDRESS, MPU6500_WHO_AM_I, buf, 1) == 0);
  ROCKET_ASSERT(buf[0] == MPU6500_WHO_AM_I_ID || buf[0] == MPU6500_WHO_AM_I_ID); // Read "WHO_AM_I" register

  // Reset device, this resets all internal registers to their default values
  ROCKET_ASSERT(I2C_WriteData(MPU6500_ADDRESS, MPU6500_PWR_MGMT_1, 1U << 7) == 0);
  delay(100); // The power on reset time is specified to 100 ms. It seems to be the case with a software reset as well
  do {
    ROCKET_ASSERT(I2C_ReadData(MPU6500_ADDRESS, MPU6500_PWR_MGMT_1, buf, 1) == 0);
    delay(1);
  } while (buf[0] & (1U << 7)); // Wait for the bit to clear
  ROCKET_ASSERT(I2C_WriteData(MPU6500_ADDRESS, MPU6500_PWR_MGMT_1, (1U << 3) | (1U << 0)) == 0); // Disable sleep mode, disable temperature sensor and use PLL as clock reference

  ROCKET_ASSERT(sample_rate < 1000); // 1 kHz the maximum supported by the sensor
  buf[0] = 1000U / sample_rate - 1; // Set the sample rate in Hz - frequency = 1000/(register + 1) Hz
  buf[1] = 0x01; // Disable FSYNC and set 184 Hz Gyro filtering, 1 kHz sampling rate
  buf[2] = 0U << 3; // Set Gyro Full Scale Range to +-250 deg/s
  buf[3] = 3U << 3; // Set Accelerometer Full Scale Range to +-16 g
  buf[4] = 0x00; // 218.1 Hz Acc filtering, 1 kHz sampling rate
  ROCKET_ASSERT(I2C_WriteData(MPU6500_ADDRESS, MPU6500_SMPLRT_DIV, buf, 5) == 0); // Write to all five registers at once

  // Set accelerometer and gyroscope scale factor from datasheet
  mpu6500->gyroScaleFactor = MPU6500_GYRO_SCALE_FACTOR_250;
  mpu6500->accScaleFactor = MPU6500_ACC_SCALE_FACTOR_16;

#if 0
  // Enable Raw Data Ready Interrupt on INT pin
  buf[0] = (1U << 5) | (1U << 4); // Enable LATCH_INT_EN and INT_ANYRD_2CLEAR
                                  // When this bit is equal to 1, the INT pin is held high until the interrupt is cleared
                                  // When this bit is equal to 1, interrupt status is cleared if any read operation is performed
  buf[1] = 1U << 0;               // Enable RAW_RDY_EN - When set to 1, Enable Raw Sensor Data Ready interrupt to propagate to interrupt pin
  ROCKET_ASSERT(I2C_Write(MPU6500_ADDRESS, MPU6500_INT_PIN_CFG, buf, 2) == 0); // Write to both registers at once
#endif

  delay(10); // Wait for sensor to stabilize
}

uint8_t MPU6500_DateReady(bool *ready) {
  uint8_t buf[14]; // Buffer for the SPI data
  uint8_t rcode = I2C_ReadData(MPU6500_ADDRESS, MPU6500_INT_STATUS, buf, 1);
  if (rcode != 0)
    return rcode;
  *ready = buf[0] & 0x01;
  return 0;
}

// Returns accelerometer and gyro data with zero values subtracted
uint8_t MPU6500_GetData(mpu6500_t *mpu6500) {
  uint8_t buf[14]; // Buffer for the SPI data
  uint8_t rcode = I2C_ReadData(MPU6500_ADDRESS, MPU6500_ACCEL_XOUT_H, buf, 14);
  if (rcode != 0)
    return rcode;

  sensorRaw_t acc, gyro; /*!< Raw accelerometer and gyroscope readings */
  acc.X = (int16_t)((buf[0] << 8) | buf[1]);
  acc.Y = (int16_t)((buf[2] << 8) | buf[3]);
  acc.Z = (int16_t)((buf[4] << 8) | buf[5]);
  /*int16_t tempRaw = (int16_t)((buf[6] << 8) | buf[7]);*/
  gyro.X = (int16_t)((buf[8] << 8) | buf[9]);
  gyro.Y = (int16_t)((buf[10] << 8) | buf[11]);
  gyro.Z = (int16_t)((buf[12] << 8) | buf[13]);

  for (uint8_t axis = 0; axis < 3; axis++) {
    mpu6500->accSi.data[axis] = (float)acc.data[axis] / mpu6500->accScaleFactor * GRAVITATIONAL_ACCELERATION; // Convert to m/s^2
    mpu6500->gyroRate.data[axis] = (float)gyro.data[axis] / mpu6500->gyroScaleFactor * DEG_TO_RADf; // Convert to rad/s
  }

  return 0;
}