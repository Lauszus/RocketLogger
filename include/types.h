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

#ifndef __types_h__
#define __types_h__

#include <stdint.h>

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

#endif // __types_h__
