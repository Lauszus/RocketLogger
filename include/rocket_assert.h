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

#ifndef __assert_h__
#define __assert_h__

#include <Arduino.h>

#define ROCKET_ASSERT(x) do { if((x) == 0) { Serial.printf("Assert failed in \"%s\" at line \"%d\" in function \"%s\". Expression: \"%s\"\n", __FILE__, __LINE__, __func__, #x); Serial.flush(); delay(1000); ESP.restart(); for (;;); } } while (0)

#endif // __assert_h__
