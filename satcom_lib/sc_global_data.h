/**
 *  SatCom Library
 *  by cafe-alpha.
 *  WWW: http://ppcenter.free.fr/satcom/
 *
 *  See LICENSE file for details.
**/

#ifndef SATCOM_GLOBAL_DATA_H
#define SATCOM_GLOBAL_DATA_H

//----------------------------------------------------------------------
// SatCom Global Data.
// It consists in 8 bytes (unsigned long * 2) of data put at the
// at the end of HRAM memory.
// 
// This is used in the case we can't use local data.
//  - Location of SatCom buffer.
//
// Satcom global data start address is SC_GLOBAL_DATA_ADR (0x002FFFF8)
// 
//----------------------------------------------------------------------


// GCC have alternative #pragma pack(N) syntax and old gcc version not
// support pack(push,N), also any gcc version not support it at some platform
#if defined( __GNUC__ )
#pragma pack(1)
#else
#pragma pack(push,1)
#endif


typedef struct _sc_global_t
{
    /******************************************/
    /* ---        SatCom Debugger.         ---*/
    /* Address to the SCD data. */
    unsigned long scd_ptr;                      // 0x002F FFF8
    /* Magic number in order to check if we can use SCD or not. */
    unsigned long scd_magic;                    // 0x002F FFFC
} sc_global_t;


// GCC have alternative #pragma pack() syntax and old gcc version not
// support pack(pop), also any gcc version not support it at some platform
#if defined( __GNUC__ )
#pragma pack()
#else
#pragma pack(pop)
#endif


/**
 * Set global data start address at the end of LRAM.
**/
#define SC_GLOBAL_DATA_ADR (0x00300000 - sizeof(sc_global_t))

#include "satcart.h"

#endif //SATCOM_GLOBAL_DATA_H

