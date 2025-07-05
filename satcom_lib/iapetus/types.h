/*  Copyright 2006 Theo Berkau

    This file is part of Iapetus.

    Iapetus is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    Iapetus is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with Iapetus; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

#ifndef _TYPES_H
#define _TYPES_H

#include <inttypes.h> // uint32_t, etc

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned long int u32;

typedef signed char s8;
typedef signed short int s16;
typedef signed long int s32;


typedef unsigned char uint8;
typedef unsigned short int uint16;
typedef unsigned long int uint32;

typedef signed char int8;
typedef signed short int int16;
typedef signed long int int32;

typedef volatile unsigned char vuint8;
typedef volatile unsigned short int vuint16;
typedef volatile unsigned long int vuint32;

typedef volatile signed char vint8;
typedef volatile signed short int vint16;
typedef volatile signed long int vint32;


typedef int BOOL;

#define TRUE    1
#define FALSE   0

#ifndef NULL
#define NULL    0
#endif
#endif

