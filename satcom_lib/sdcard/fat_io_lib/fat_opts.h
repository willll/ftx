#ifndef __FAT_OPTS_H__
#define __FAT_OPTS_H__

#ifdef SC_SATURN
    /* Definition of maximum filename/path lengths. */
    #include "../../sc_file.h"
#endif

//-------------------------------------------------------------
// Configuration
//-------------------------------------------------------------

// Is the processor little endian (1) or big endian (0)
#ifdef SC_SATURN
    #define FATFS_IS_LITTLE_ENDIAN          0
#else
    #define FATFS_IS_LITTLE_ENDIAN          1
#endif

// Max filename Length
#ifdef SC_SATURN
    #define FATFS_MAX_LONG_FILENAME SC_PATHNAME_MAXLEN
#else
    #define FATFS_MAX_LONG_FILENAME 96
#endif

// Max open files (reduce to lower memory requirements)
#ifndef FATFS_MAX_OPEN_FILES
    #define FATFS_MAX_OPEN_FILES            1
#endif

// Number of sectors per FAT_BUFFER (min 1)
#ifndef FAT_BUFFER_SECTORS
    #define FAT_BUFFER_SECTORS              1
#endif

// Max FAT sectors to buffer (min 1)
// (mem used is FAT_BUFFERS * FAT_BUFFER_SECTORS * FAT_SECTOR_SIZE)
#ifndef FAT_BUFFERS
    #define FAT_BUFFERS                     1
#endif

// Size of cluster chain cache (can be undefined)
// Mem used = FAT_CLUSTER_CACHE_ENTRIES * 4 * 2
// Improves access speed considerably
#define FAT_CLUSTER_CACHE_ENTRIES         128

// Include support for writing files (1 / 0)?
#define FATFS_INC_WRITE_SUPPORT         1

// Support long filenames (1 / 0)?
// (if not (0) only 8.3 format is supported)
#ifndef FATFS_INC_LFN_SUPPORT
    #define FATFS_INC_LFN_SUPPORT           1
#endif

// (TODO) Maximum filename length
//#ifdef SC_SATURN
//    #define FATFS_FILENAME_MAXLEN SC_FILENAME_MAXLEN
//#else
//    #define FATFS_FILENAME_MAXLEN 27
//#endif

// Support directory listing (1 / 0)?
#define FATFS_DIR_LIST_SUPPORT          1

// Support time/date (1 / 0)?
#define FATFS_INC_TIME_DATE_SUPPORT     1

// Include support for formatting disks (1 / 0)?
#ifndef FATFS_INC_FORMAT_SUPPORT
    #define FATFS_INC_FORMAT_SUPPORT        0
#endif

// Sector size used
#define FAT_SECTOR_SIZE                     512

// Time/Date support requires time.h
#if FATFS_INC_TIME_DATE_SUPPORT
    #include <time.h>
#endif

#endif
