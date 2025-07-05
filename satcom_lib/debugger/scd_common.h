/**
 *  SatCom Library
 *  by cafe-alpha.
 *  WWW: http://ppcenter.free.fr/satcom/
 *
 *  See LICENSE file for details.
**/

#ifndef SATCOM_DEBUGGER_COMMON_H
#define SATCOM_DEBUGGER_COMMON_H


//----------------------------------------------------------------------
// Saturn debugger common stuff.
// All the common stuff between Saturn and PC are gathered in a
//  structure shared between Saturn and PC.
// 
//----------------------------------------------------------------------


// GCC have alternative #pragma pack(N) syntax and old gcc version not
// support pack(push,N), also any gcc version not support it at some platform
#if defined( __GNUC__ )
#pragma pack(1)
#else
#pragma pack(push,1)
#endif

/**
 * SatCom debugger version.
 * Format is (Major.Minor), and minor is ignored during version checking on PC.
 * If Saturn/PC Major versions mismatch, the debugger will throw an error (in order to avoid software crash).
 *
 * Versions history:
 *  2015/10/28, Ver 5.5: Changed debugger structure size from 4KB to 3KB.
 *  2015/04/07, Ver 5.3: Added wh_on_log field. Doesn't break compatiblity, so version number is left as-is.
 *  2012/03/12, Ver 5.3: removed cb_autoflush (dbg_on and dbg_disable are used instead) and moved cb_memuse so that 4 consecutives bytes are free for future use.
 *  2011/10/04, Ver 5.2: added cb_autoflush and cb_memuse to debugger structure.
 *  2011/05/11, Ver 5.2: added dbg_disable to debugger structure.
 *  2010/10/15, Ver 5.1: added fileio_size to debugger structure.
 *  2010/09/10, Ver 5.0: gathered "rw_complete" and "prompt_complete" flag into "proc_complete" flag.
 *  2010/09/09, Ver 4.2: added version field to debugger structure.
**/
#define SCD_VERSION_MAJOR 5
#define SCD_VERSION_MINOR 5

/**
 * Note : the order of the elements in the structure is
 * important, so you shouldn't change it !
**/
#define SCD_CIRCBUFF_MAXSIZE 65536
#define SCD_PROMPT_SIZE      256
/* Debugger structure size : 3KB. (Change prohibited !) */
#define SCD_STRUCT_SIZE      3072

typedef struct _scd_data_t
{
    /* Circular buffer R/W pointers (address is relative from the begining of circular buffer). */
    unsigned short readptr;
    unsigned short writeptr;

    /* Read until here when polling (4 bytes).             */
    /*-----------------------------------------------------*/

    /* Set to 1 by the PC in order to indicate that debugger is ON. */
    unsigned char dbg_on;
    /* Indicate that the file read/write from PC is completed. */
    /* Indicates that the prompt answer has been sent from PC. */
    unsigned char proc_complete;
    /* Disable debugging stuff (used to evaluate speed that release version would reach). */
    unsigned char dbg_disable;
    /* Circular buffer memory usage (0:empty~128:full). */
    unsigned char cb_memuse;


    /* Set to the transfered size (in bytes) after a file request with PC. */
    unsigned long fileio_size;


    /* Structure/debugger version (used for version checking). */
    unsigned char version[2];
    /* This structure size (used for version checking). */
    unsigned short structsz;


    /* 0:normal log output, 1:flush on every log output. */
    unsigned char dbg_logflush;
    /* 0:normal log output, 1:"where here" on every log. */
    unsigned char wh_on_log;
    /* Reserved bytes for future stuff, and for 4 bytes alignment. */
    unsigned char reserved2[2];


    /* Circular buffer internal. */
    unsigned long buffer_size;
    unsigned long start_address;
    unsigned long end_address;

    /* Read until here when init (32 bytes).               */
    /*-----------------------------------------------------*/


    /* Circular buffer used when reading/writing debug data. */
    /* Size is defined in order to pack whole structure size to 4KB. */
    unsigned char rbuffer[SCD_STRUCT_SIZE - SCD_PROMPT_SIZE - 32];

    /* Place where to write prompt's answer. */
    unsigned char prompt_answer[SCD_PROMPT_SIZE];
} scd_data_t;

// GCC have alternative #pragma pack() syntax and old gcc version not
// support pack(pop), also any gcc version not support it at some platform
#if defined( __GNUC__ )
#pragma pack()
#else
#pragma pack(pop)
#endif


/**
 * Absolute data:
 *  - address to scd_data_t debugger data.
 *  - magic value in order to check if the debugger is running.
 * Their addresses are defined in sc_global_data.h
**/
#include "../sc_global_data.h"
#define SCD_ABS_SIZE  8
#define SCD_ABS_MAGIC 0xCAFE76EC




/**
 * Circular buffer:
 * Generic data format: 
 *  UCHAR        len
 *  UCHAR        type
 *  UCHAR[len-2] data
**/

/**
 * Type values.
 * It can be used to add new commands, such as string request,
 * file data request, etc.
**/

/*
 * Debug message output:
 * Data format:
 *  UCHAR         len
 *  UCHAR         type (='M')
 *  UCHAR[len-2]  debug_message_string
 */
#define SCD_TYPE_MSG 'M'
/*
 * Message prompt:
 * Data format:
 *  UCHAR         len
 *  UCHAR         type (='P')
 *  UCHAR[len-2]  prompt_message_string
 */
#define SCD_TYPE_PROMPT 'P'

/*
 * File input/output request:
 * Data format:
 *  UCHAR         len
 *  UCHAR         type (='R', 'W', 'A', 'S', 'l', 'm', 'x')
 *  ULONG         file_offset
 *  ULONG         rw_address
 *  ULONG         rw_len
 *  UCHAR[len-14] file_name
 */
#define SCD_TYPE_RFILE     'R'
#define SCD_TYPE_WFILE     'W'
#define SCD_TYPE_AFILE     'A'
#define SCD_TYPE_SFILE     'S'
#define SCD_TYPE_LISTDIR   'l'
#define SCD_TYPE_MKDIR     'm'
#define SCD_TYPE_REMOVE    'x'


/*
 * Indicate current location in source/binary files:
 * Data format:
 *  UCHAR         len
 *  UCHAR         type (='w')
 *  UCHAR[len-2]  where_here_string
 *
 * Note : this data frame can be processed in a similar way as SCD_TYPE_MSG.
 */
#define SCD_TYPE_WHEREHERE 'w'


// GCC have alternative #pragma pack(N) syntax and old gcc version not
// support pack(push,N), also any gcc version not support it at some platform
#if defined( __GNUC__ )
#pragma pack(1)
#else
#pragma pack(push,1)
#endif

/*
 * sc_file.h :
 * Retrieve maximum filename length
 * (SC_FILENAME_MAXLEN) definition.
 */
#include "../sc_file.h"

/*
 * File entry record used when listing files in folder.
 *
 * Among others, it is used in custom_yabause, so
 * changing this structure is not recommended.
 */
typedef struct _scdf_file_entry_t
{
    /* Entry flags. */
#define FILEENTRY_FLAG_ENABLED    0x01
#define FILEENTRY_FLAG_FOLDER     0x02
    /* Internal usage flags. */
#define FILEENTRY_FLAG_COMPRESSED 0x20
#define FILEENTRY_FLAG_LASTENTRY  0x40
#define FILEENTRY_FLAG_FILTERED   0x80
    unsigned char flags;

    /* File name+null character. */
    char filename[SC_FILENAME_MAXLEN];

    /* File index in this folder (start from zero). */
    unsigned short file_index;

    /* Index after file name sorting (start from zero). */
    unsigned short sort_index;
} scdf_file_entry_t;
// GCC have alternative #pragma pack() syntax and old gcc version not
// support pack(pop), also any gcc version not support it at some platform
#if defined( __GNUC__ )
#pragma pack()
#else
#pragma pack(pop)
#endif



/*--- Saturn-side functions. ---*/
/**
 * Init circular buffer.
**/
void scd_cbinit(scd_data_t* d);
/**
 * Write data in circular buffer.
 * It modifies circular buffer's write pointer.
**/
void scd_cbwrite(scd_data_t* d, unsigned char* data, unsigned long datalen);
/**
 * Return Circular buffer's memory usage.
 * Return value is between 0(buffer empty) and 100 (buffer full).
**/
unsigned char scd_cbmemuse(scd_data_t* d);




#endif // SATCOM_DEBUGGER_COMMON_H

