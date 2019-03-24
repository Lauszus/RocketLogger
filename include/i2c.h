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

#ifndef __i2c_h__
#define __i2c_h__

#include <stdint.h>

void I2C_Init(int sda, int scl);
uint8_t I2C_Write(uint8_t addr, uint8_t regAddr, bool sendStop = true);
uint8_t I2C_WriteData(uint8_t addr, uint8_t regAddr, uint8_t data, bool sendStop = true);
uint8_t I2C_WriteData(uint8_t addr, uint8_t regAddr, uint8_t *data, size_t size, bool sendStop = true);
uint8_t I2C_ReadData(uint8_t addr, uint8_t regAddr, uint8_t *data, size_t size, bool sendStop = false);

#endif // __i2c_h__
