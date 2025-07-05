/**
 *  SatCom Library
 *  by cafe-alpha.
 *  WWW: http://ppcenter.free.fr/satcom/
 *
 *  See LICENSE file for details.
**/

#ifndef ROM_CODE_HEADER_H
#define ROM_CODE_HEADER_H


//----------------------------------------------------------------------
// Header for use by ROM code.
// 
//----------------------------------------------------------------------


// GCC have alternative #pragma pack(N) syntax and old gcc version not
// support pack(push,N), also any gcc version not support it at some platform
#if defined( __GNUC__ )
#pragma pack(1)
#else
#pragma pack(push,1)
#endif

/* ROM code/data type. */
#define RC_TYPE_ROM_CODE  1
#define RC_TYPE_DATA_RAW  2
#define RC_TYPE_DATA_COMP 3

typedef struct _sc_rom_code_header_t
{
    /* Must be set to 0x42. */
    unsigned char magic_42;
    /* Must be set to 0x53. */
    unsigned char magic_53;

    /* Contents type. */
    unsigned char type;

    /* Reserved for future use. */
    unsigned char reserved01[1];


    /* ROM code start address. */
    unsigned long address;

    /* Reserved for future use. */
    unsigned char reserved02[8];


    /* Version strings. */
    char version_string1[16];
    char version_string2[16];


    /* ROM code maximum length.
     * This does includes both header and data lengths.
     */
    unsigned long maxlen;

    /* ROM code used length.
     * Note : this is the length of data itself, header is not included.
     */
    unsigned long usedlen;

    /* ROM code decompressed length.
     * Note : this is the length of data itself, header is not included.
     */
    unsigned long decomplen;

    /* ROM code CRC32 value.
     * Note : rom code header is excluded from CRC computation, 
     * and CRC is computed until `usedlen' bytes.
     *  -> crc32_calc((const void*)((unsigned char*)(hdr)+sizeof(sc_rom_code_header_t)), hdr->usedlen);
     */
    unsigned long crc32;
} sc_rom_code_header_t;

// GCC have alternative #pragma pack() syntax and old gcc version not
// support pack(pop), also any gcc version not support it at some platform
#if defined( __GNUC__ )
#pragma pack()
#else
#pragma pack(pop)
#endif


#endif // ROM_CODE_HEADER_H

