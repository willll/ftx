/**
 *  SatCom Library
 *  by cafe-alpha.
 *  WWW: http://ppcenter.free.fr/satcom/
 *
 *  See LICENSE file for details.
**/

#ifndef SC_ROMFS_H
#define SC_ROMFS_H

/* Include some #defines about filename length and flags. */
#include "debugger/scd_common.h"

/* Include some #defines about path maximum length. */
#include "sc_file.h"


/**
 * romfs structures
 * Header, then entries+filenames are stored at the top of romfs data.
 * Then, all files data are stored consecutively.
 *
 * File data may be compressed, or stored in raw format.
 * Purpose : shrink romfs data size for data that need to be
 * moved to (V)RAM for processing, or allow direct read for
 * read-only data.
 * Example :
 *  - game graphics are compressed to romfs, and decompressed
 *    to vram when needed.
 *  - game level/text/whatever data are stored as raw data, 
 *    and directly read from romfs, thus saves RAM usage.
 *
 * In the case file data is stored in raw format, it is possible
 * to retrieve a pointer to its data. Hence, raw data must be stored with
 * 4 bytes alignment with start of data.
 * Note : In the case non-char data read is made on romfs data, user MUST
 * store romfs data as 4 bytes aligned data.
 *
 * Example :
 * +- file1
 * +- file2
 * +- folder1
 * |+- file3
 * |+- file4
 * Romfs contents :
 * - header structure
 * - file1 entry, then filename
 * - file2 entry, then filename
 * - folder1 entry, then folder name
 * - file3 entry, then filename
 * - file4 entry, then filename
 * - file1 data
 * - file2 data
 * - file3 data
 * - file4 data
**/

#if defined( __GNUC__ )
#pragma pack(1)
#else
#pragma pack(push,1)
#endif

/**
 * romfs header definition
**/
typedef struct _romfs_header_t
{
    /* Magic data, to confirm presence of romfs data. */
    unsigned char magic[8];

    /* Whole data size, including this header. */
    unsigned long size;

    /* CRC of RomFs Data, excluding this header. */
    unsigned long crc32;

    /* Folder/file count.
     * Total entry count is the sum of theses 2 values.
     */
    unsigned long folder_cnt;
    unsigned long file_cnt;
} romfs_header_t;

/**
 * romfs entry definition
**/
typedef struct _romfs_entry_t
{
    /* Offset to data/next folder.
     * Offset is absolute from romfs start data.
     * When file entry, offset points to file start data.
     * When folder entry, offset points to first entry 
     *  in corresponding folder.
     * In the case of folder in root folder, offset is
     *  set to first entry in root folder.
     */
    unsigned long offset;

    /* Data size within RomFs data.
     * When entry is a folder, size is zero.
     * Note ; it doesn't correspond with file size
     *  in the case data is compressed.
     *  (please call romfs_get_entry_size in order
     *  to retrieve actual data size).
     */
    unsigned long size;

    /* File/folder informations.
     * Note : this data type MUST be 1 byte sized, 
     * because of data access on non-aligned buffer.
     */
    unsigned char flags;

    /* Filename length, including terminating null character.
     * Note : this data type MUST be 1 byte sized, 
     * because of data access on non-aligned buffer.
     */
    unsigned char filename_len;
} romfs_entry_t;

#if defined( __GNUC__ )
#pragma pack()
#else
#pragma pack(pop)
#endif


/**
 * (De)compression routines.
**/
/* Compress/decompress data by 8KB chunk. */
#define ROMFS_COMP_CHUNK (8*1024)
unsigned long romfs_decomp_getsize(unsigned char* ind, unsigned long inl);
unsigned long romfs_decomp_exec(unsigned char* ind, unsigned long inl, unsigned char* outd, unsigned long offset, unsigned long size);


/**
 * romfs_crc
 * Compute romfs data CRC.
**/
unsigned long romfs_crc(const void *buf, unsigned long size);


/**
 * romfs_check_header
 * Return 1 if header detected, zero else.
 *
 * Note : Execution may require time when crc_check is non null, 
 *        so CRC check shouldn't be done frequently.
**/
int romfs_check_header(unsigned char* romfs, int crc_check);


/**
 * romfs_get_first_entry
 * Return pointer to first entry specified in path.
 * Path can be a folder path or file path. In the case of folder path, a 
 * pointer to first entry in this folder is returned.
 * Return NULL if error or nothing found.
 *
 * Note : path must be something like this : 
 *  "/FOLDER/SUBFOLDER" in the case of folder.
 *  "/FOLDER/SUBFOLDER/TEST.BIN" in the case of file.
 *
 * Note : return value may not be 4 bytes aligned, so memcpy
 * of entry data to internal variable is needed when accessing entry values.
**/
romfs_entry_t* romfs_get_first_entry(unsigned char* romfs, char* path);


/**
 * romfs_get_next_entry
 * Return pointer to next entry specified after specified one.
 * Return null if nothing found, or no more entry available.
 *
 * Note : return value may not be 4 bytes aligned, so memcpy
 * of entry data to internal variable is needed when accessing entry values.
**/
romfs_entry_t* romfs_get_next_entry(unsigned char* romfs, romfs_entry_t* entry);


/**
 * romfs_get_entry_size
 * Get size of file data pointed by entry.
 *
 * Note : data pointed by romfs_entry_t* e need to be 4 bytes aligned.
**/
unsigned long romfs_get_entry_size(unsigned char* romfs, romfs_entry_t* e);

/**
 * romfs_get_entry_data
 * Copy/decompress data from RomFs to specified buffer.
 *
 * Note : data pointed by romfs_entry_t* e need to be 4 bytes aligned.
**/
void romfs_get_entry_data(unsigned char* romfs, romfs_entry_t* e, unsigned char* buffer, unsigned long offset, unsigned long size);

/**
 * romfs_get_entry_ptr
 * Get pointer to raw data from specified file in RomFs data.
 *
 * Note : data pointed by romfs_entry_t* e need to be 4 bytes aligned.
 * Note : in the case of compressed file NULL pointer is returned.
**/
unsigned char* romfs_get_entry_ptr(unsigned char* romfs, romfs_entry_t* e, unsigned long* datalen);


#endif //SATCOM_COMPRESS_H
