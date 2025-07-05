/**
 *  SatCom Library
 *  by cafe-alpha.
 *  WWW: http://ppcenter.free.fr/satcom/
 *
 *  See LICENSE file for details.
**/

#ifndef SATCOM_COMMON_H
#define SATCOM_COMMON_H

#ifndef MAX
#   define MAX( x, y ) ((x) > (y) ? (x) : (y))
#endif
#ifndef MIN
#   define MIN( x, y ) ((x) < (y) ? (x) : (y))
#endif


#ifdef _BCB
#ifdef  __cplusplus
extern "C" {
#endif
#endif // _BCB


//----------------------------------------------------------------------
// Char manipulation functions.
// Used for filename sorting (scf_filelist function) in sc_saturn.c.
// 
//----------------------------------------------------------------------
char sc_toupper(char c);
char sc_tolower(char c);


//----------------------------------------------------------------------
// Convert char to printable char.
// 
//----------------------------------------------------------------------
unsigned char char2pchar(unsigned char c);


//----------------------------------------------------------------------
// Saturn debugger common stuff.
// 
//----------------------------------------------------------------------
#include "debugger/scd_common.h"


//----------------------------------------------------------------------
// CRC32, used by RomFs tool.
// 
//----------------------------------------------------------------------
unsigned long crc32_update(unsigned long crc, const void *buf, unsigned long size);
#define CRC32_INITVAL 0
#define crc32_calc(_BUFF_, _SIZE_) crc32_update(CRC32_INITVAL, _BUFF_, _SIZE_)

//----------------------------------------------------------------------
// 8 bits CRC, used by USB dev cart
// http://www.iki.fi/Anders.Montonen/sega/usbcart/
// 
//----------------------------------------------------------------------
unsigned char crc_usbdc_init(void);
unsigned char crc_usbdc_update(unsigned char crc, const unsigned char *data, unsigned long data_len);
unsigned char crc_usbdc_finalize(unsigned char crc);

/* Functions codes for USB dev cart. */
enum
{
    USBDC_FUNC_DOWNLOAD      = 1,
    USBDC_FUNC_UPLOAD        = 2,
    USBDC_FUNC_EXEC          = 3,
    USBDC_FUNC_GET_BUFF_ADDR = 4,
    USBDC_FUNC_COPYEXEC      = 5,
    USBDC_FUNC_EXEC_EXT      = 6,
};


//----------------------------------------------------------------------
// USB dev cart firmware version access parameters.
// 
//----------------------------------------------------------------------
#define USBDC_VERSION_ADR 0x207FFFF0
#define USBDC_VERSION_LEN 16


//----------------------------------------------------------------------
// Soft reset flags.
// Used in soft reset callback, and in sc_execute function.
// 
//----------------------------------------------------------------------
#define SC_SOFTRES_HRAM     (1 <<  0)
#define SC_SOFTRES_LRAM     (1 <<  1)
#define SC_SOFTRES_VDP1REGS (1 << 16)
#define SC_SOFTRES_VDP1RAM  (1 << 17)
#define SC_SOFTRES_VDP2REGS (1 << 18)
#define SC_SOFTRES_VDP2RAM  (1 << 19)
#define SC_SOFTRES_NONE 0x00000000
#define SC_SOFTRES_ALL  0xFFFFFFFF


//----------------------------------------------------------------------
// Compression routines.
// 
//----------------------------------------------------------------------
#include "sc_compress.h"






//----------------------------------------------------------------------
// Flash memory main executable program data arrangement.
// Used in mkfirm and Pseudo Saturn Kai firmware.
// 
//----------------------------------------------------------------------

/* Flash memory start address. */
#define FC_FLASH_ADDRESS 0x22000000

/* About firmware data:
 *   CONTENTS    ~~ SIZE     ~~ START OFFSET
 *---------------------------------------------
 *  IP.BIN       ~~ 3840  B  ~~ 0x0000 (    0)
 *  loader       ~~ 112   B  ~~ 0x0F00 ( 3840)
 *  parameters   ~~ 16    B  ~~ 0x0F70 ( 3952)
 *  decompressor ~~ 5504  B  ~~ 0x0F80 ( 3968)
 *  comp. data   ~~ Varies   ~~ 0x2500 ( 9472)
 */


/* IP.BIN start offset. */
#define FC_IPBIN_OFFSET        0
/* Loader start offset. */
#define FC_LOADER_OFFSET       (FC_IPBIN_OFFSET + 0xF00)
/* Parameters start offset. (Max 4 parameters) */
#define FC_PARAMS_OFFSET       (FC_LOADER_OFFSET + 112)
/* Decompressor start offset and length. */
#define FC_DECOMPRESSOR_OFFSET (FC_PARAMS_OFFSET + 16)
#define FC_DECOMPRESSOR_LENGTH (5504)
/* Decompressor execute address */
#define FC_DECOMPRESSOR_EXEC   0x002F0000
/* Compressed data start offset. */
#define FC_DATA_OFFSET         (FC_DECOMPRESSOR_OFFSET + FC_DECOMPRESSOR_LENGTH)

/* Parameter 1: input data length. */
#define FC_PDATALEN_INDEX    0
/* Parameter 2: execution address. */
#define FC_EXEADDRS_INDEX    1

/* Parameter 3: compressed data length (output parameter). */
#define FC_COMPDATALEN_INDEX 2
/* Parameter 4: compressed data CRC (output parameter). */
#define FC_COMPDATACRC_INDEX 3


#ifdef _BCB
#ifdef  __cplusplus
}
#endif
#endif // _BCB


#endif //SATCOM_COMMON_H
