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

#ifndef __ms5611_h__
#define __ms5611_h__

typedef enum {
  MS5611_OSR_4096 = 0x08,
  MS5611_OSR_2048 = 0x06,
  MS5611_OSR_1024 = 0x04,
  MS5611_OSR_512  = 0x02,
  MS5611_OSR_256  = 0x00
} ms5611_osr_mask_e;

/** Struct for MS5611 data */
typedef struct {
// public
  int32_t pressure; // Pressure in pascal
  float altitude; // Altitude in meters
  float temperature; // Temperature in celcius

// private
  ms5611_osr_mask_e osr_mask;
  uint32_t osr_delay_micros;
  uint16_t prom_c[6];
} ms5611_t;

void MS5611_Init(ms5611_t *ms5611, ms5611_osr_mask_e ms5611_osr_mask);

uint8_t MS5611_GetData(ms5611_t *ms5611);

float MS5611_GetAbsoluteAltitude(int32_t pressure);

#endif // __ms5611_h__
