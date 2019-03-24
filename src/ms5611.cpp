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

// Inspired by: https://github.com/jarzebski/Arduino-MS5611

#include <Arduino.h>

#include "assert.h"
#include "i2c.h"
#include "ms5611.h"

#define MS5611_ADDRESS                  0x77

#define MS5611_CMD_ADC_READ             0x00
#define MS5611_CMD_RESET                0x1E
#define MS5611_CMD_CONV_D1              0x40
#define MS5611_CMD_CONV_D2              0x50
#define MS5611_CMD_READ_PROM            0xA2

#define pow2(x)                         ((x)*(x))

void MS5611_Init(ms5611_t *ms5611, ms5611_osr_mask_e ms5611_osr_mask) {
    ASSERT(I2C_Write(MS5611_ADDRESS, MS5611_CMD_RESET) == 0);

    // Set the OSR value and set the delay required after a measurement
    ms5611->osr_mask = ms5611_osr_mask;
    switch (ms5611->osr_mask) {
        case MS5611_OSR_4096:
            ms5611->osr_delay_micros = 9040;
            break;
        case MS5611_OSR_2048:
            ms5611->osr_delay_micros = 4540;
            break;
        case MS5611_OSR_1024:
            ms5611->osr_delay_micros = 2280;
            break;
        case MS5611_OSR_512:
            ms5611->osr_delay_micros = 1170;
            break;
        case MS5611_OSR_256:
            ms5611->osr_delay_micros = 600;
            break;
        default:
            ASSERT(false && "Invalid OSR value");
    }

    delay(100);

    // Read calibration data (factory calibrated) from PROM
    // Note that the registers has to be read one at a time
    for (uint8_t i = 0; i < 6; i++) {
        uint8_t buf[2];
        ASSERT(I2C_ReadData(MS5611_ADDRESS, MS5611_CMD_READ_PROM + 2 * i, buf, 2, true) == 0);
        ms5611->prom_c[i] = (uint16_t)((buf[0] << 8) | buf[1]);
    }
}

uint8_t MS5611_GetData(ms5611_t *ms5611) {
    uint8_t buf[3];

    // Read digital pressure and temperature data
    uint8_t rcode = I2C_Write(MS5611_ADDRESS, MS5611_CMD_CONV_D1 | ms5611->osr_mask);
    if (rcode != 0)
        return rcode;
    delayMicroseconds(ms5611->osr_delay_micros);
    rcode = I2C_ReadData(MS5611_ADDRESS, MS5611_CMD_ADC_READ, buf, 3, true);
    if (rcode != 0)
        return rcode;
    uint32_t D1 = (uint32_t)((buf[0] << 16) | (buf[1] << 8) | buf[2]);

    rcode = I2C_Write(MS5611_ADDRESS, MS5611_CMD_CONV_D2 | ms5611->osr_mask);
    if (rcode != 0)
        return rcode;
    delayMicroseconds(ms5611->osr_delay_micros);
    rcode = I2C_ReadData(MS5611_ADDRESS, MS5611_CMD_ADC_READ, buf, 3, true);
    if (rcode != 0)
        return rcode;
    uint32_t D2 = (uint32_t)((buf[0] << 16) | (buf[1] << 8) | buf[2]);

    // Difference between actual and reference temperature
    int32_t dT = D2 - (uint32_t)ms5611->prom_c[4] * 256;

    // Actual temperature (-40 ... 85 C with 0.01 C resolution)
    int32_t TEMP = 2000L + ((int64_t)dT * (int64_t)ms5611->prom_c[5]) / 8388608UL;

    // Offset at actualy temperature
    int64_t OFF = (int64_t)ms5611->prom_c[1] * 65536 + ((int64_t)ms5611->prom_c[3] * dT) / 128;

    // Sensitivity at actualy temperature
    int64_t SENS = (int64_t)ms5611->prom_c[0] * 32768 + ((int64_t)ms5611->prom_c[2] * dT) / 256;

    // Check if the temperature is below 20 C
    int32_t T2 = 0;
    int64_t OFF2 = 0, SENS2 = 0;

    if (TEMP < 2000) {
        T2 = pow2((int64_t)dT) / 2147483648UL;
        OFF2 = 5 * pow2((int64_t)(TEMP - 2000)) / 2;
        SENS2 = 5 * pow2((int64_t)(TEMP - 2000)) / 4;

        if (TEMP < -1500) {
            OFF2 = OFF2 + 7 * pow2((int64_t)(TEMP + 1500));
            SENS2 = SENS2 + 11 * pow2((int64_t)(TEMP + 1500)) / 2;
        }
    }

    // Second order temperature compensation
    TEMP = TEMP - T2;
    OFF = OFF - OFF2;
    SENS = SENS - SENS2;

    // Temperature compensated pressure (10 ... 1200 mbar with 0.01 mbar resolution i.e. pascal)
    ms5611->pressure = (D1 * SENS / 2097152UL - OFF) / 32768;

    static const int32_t p0 = 101325; // Pressure at sea level
    ms5611->altitude = 44330.0f * (1.0f - powf((float)ms5611->pressure / (float)p0, 1.0f / 5.255f)); // Get altitude in m

    // Convert temperatue to celcius
    ms5611->temperature = (float)TEMP / 100.0f;

    return 0;
}
