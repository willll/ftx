/**
 *  SatCom Library
 *  by cafe-alpha.
 *  WWW: http://ppcenter.free.fr/satcom/
 *
 *  See LICENSE file for details.
**/

#ifndef SC_FILE_H
#define SC_FILE_H


/**
 * Maximum length for file name, 
 * including terminating null character.
 *
 * This size is used in many structures
 * (example : scdf_file_entry_t), 
 * in several projects
 * (example : satcom_lib, vmem, custom_yabause), 
 * so changing it is not recommended.
**/
#define SC_FILENAME_MAXLEN 27

/**
 * Maximum length for file full path, 
 * including terminating null character.
**/
#define SC_PATHNAME_MAXLEN 96


/**
 * Devices IDs.
**/
#define SC_FILE_CDROM      0
#define SC_FILE_ROMFS      1
#define SC_FILE_PC         2
#define SC_FILE_SDCARD     3

#define SC_DEVICES_CNT 4


typedef struct _sc_device_t
{
    /* Device name. */
    char* name;

    /* Device ID. */
    unsigned char dev_id;

    /* 0:not available for use, 1:can use. */
    unsigned char available;

    /* 1:read/write, 1:write only. */
    unsigned char read_only;

    /* Varies according to device, zero if model is not defined. */
    unsigned char model;
} sc_device_t;



#endif // SC_FILE_H

