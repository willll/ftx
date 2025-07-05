#ifndef __FAT_GLOBAL_H__
#define __FAT_GLOBAL_H__

#include "fat_filelib.h"

//-----------------------------------------------------------------------------
// Structure
//-----------------------------------------------------------------------------
typedef struct sFL_GLOBAL
{
    /* Used in fat_filelib.c */
    FL_FILE            files[FATFS_MAX_OPEN_FILES];
    int                filelib_init;
    int                filelib_valid;
    struct fatfs       fs;
    struct fat_list    open_file_list;
    struct fat_list    free_file_list;
} FL_GLOBAL;


//-----------------------------------------------------------------------------
// Prototypes
//-----------------------------------------------------------------------------

/* Media access functions. */
int fat_media_read(uint32 sector, uint8 *buffer, uint32 sector_count);
int fat_media_write(uint32 sector, uint8 *buffer, uint32 sector_count);

/* Global data access function. */
FL_GLOBAL* fat_get_global(void);

#endif
