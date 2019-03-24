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

#include <Wire.h>

#include "i2c.h"

void I2C_Init(int sda, int scl) {
  Wire.begin(sda, scl);
  Wire.setClock(400000UL); // Set I2C frequency to 400kHz
}

uint8_t I2C_Write(uint8_t addr, uint8_t regAddr, bool sendStop /*= true*/) {
  Wire.beginTransmission(addr);
  Wire.write(regAddr);
  return Wire.endTransmission(sendStop); // See: http://arduino.cc/en/Reference/WireEndTransmission
}

uint8_t I2C_WriteData(uint8_t addr, uint8_t regAddr, uint8_t data, bool sendStop /*= true*/) {
  return I2C_WriteData(addr, regAddr, &data, 1, sendStop); // Returns 0 on success
}

uint8_t I2C_WriteData(uint8_t addr, uint8_t regAddr, uint8_t *data, size_t size, bool sendStop /*= true*/) {
  Wire.beginTransmission(addr);
  Wire.write(regAddr);
  Wire.write(data, size);
  return Wire.endTransmission(sendStop); // See: http://arduino.cc/en/Reference/WireEndTransmission
}

uint8_t I2C_ReadData(uint8_t addr, uint8_t regAddr, uint8_t *data, size_t size, bool sendStop /*= false*/) {
  Wire.beginTransmission(addr);
  Wire.write(regAddr);
  uint8_t rcode = Wire.endTransmission(sendStop); // Don't release the bus
  if (rcode)
    return rcode; // See: http://arduino.cc/en/Reference/WireEndTransmission
  size_t read = Wire.requestFrom(addr, size, true); // Send a repeated start and then release the bus after reading
  if (read != size)
    return 5; // This error value is not already taken by endTransmission
  for (uint8_t i = 0; i < size; i++)
    data[i] = Wire.read();
  return 0; // Success
}
