/**
 *  SatCom Library
 *  by cafe-alpha.
 *  WWW: http://ppcenter.free.fr/satcom/
 *
 *  See LICENSE file for details.
**/

#include "sdcard.h"
#include "../sc_saturn.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "fat_io_lib/fat_filelib.h"
#include "fat_io_lib/fat_global.h"


/**
 *------------------------------------------------------------------
 * End-user functions. (FAT32-level related)
 *------------------------------------------------------------------
**/

sdc_ret_t sdc_fat_reset(void)
{
    sdcard_t* sd = sc_get_sdcard_buff();

    /* Clear SD card related buffer.
     * This is needed in order to indicate
     * that SD card library isn't initialized yet.
     */
    memset((void*)sd, 0, sizeof(sdcard_t));

    return SDC_OK;
}

sdc_ret_t sdc_fat_init(void)
{
    sdc_ret_t ret;
    sdcard_t* sd = sc_get_sdcard_buff();

    sd->init_ok = 0;

    ret = sdc_init();
    sd->init_status = ret;
    sdc_logout(" [sdc_fat_init]sdc_init ret=%d", ret);
    if(ret != SDC_OK)
    {
        return ret;
    }

    /* Init FAT32 library. */
    fl_init();
    ret = fl_attach_media(fat_media_read, fat_media_write);
    if(ret != FAT_INIT_OK)
    {
        sd->init_status = ret;
        sdc_logout("[sdc_fat_init]ERROR: Media attach failed (code=%d) !", ret);
        return ret;
    }

    sd->init_ok = 1;
    return SDC_OK;
}


sdc_ret_t sdc_fat_file_size(char* filename, unsigned long* filesize)
{
    sdcard_t* sd = sc_get_sdcard_buff();
    FL_FILE *file;

    if(filesize)
    {
        *filesize = 0;
    }

    /* Verify if need to reinitialize SPI & FAT library. */
    if((!sd->init_ok) || (sdc_is_reinsert()))
    {
        sdc_fat_init();
    }

    /* Can't use SD card ? */
    if(!sd->init_ok)
    {
        return SDC_FAILURE;
    }

    file = fl_fopen(filename, "rb");
    if(!file)
    {
        sdc_logout("sdc_fat_file_size fl_fopen failure (%s) !", filename);
        return SDC_NOFILE;
    }

    if(fl_fseek(file, 0, SEEK_END) != 0)
    {
        sdc_logout("sdc_fat_file_size fl_fseek failure !", 0);
        fl_fclose(file);
        return SDC_FATERROR;
    }

    if(filesize)
    {
        *filesize = fl_ftell(file);
        sdc_logout("sdc_fat_file_size OK, size = %d", *filesize);
    }


    fl_fclose(file);

    return SDC_OK;
}


sdc_ret_t sdc_fat_read_file(char* filename, unsigned long offset, unsigned long length, unsigned char* buffer, unsigned long* read_length)
{
    sdcard_t* sd = sc_get_sdcard_buff();
    FL_FILE *file;

    if(read_length)
    {
        *read_length = 0;
    }

    /* Verify if need to reinitialize SPI & FAT library. */
    if((!sd->init_ok) || (sdc_is_reinsert()))
    {
        sdc_fat_init();
    }

    /* Can't use SD card ? */
    if(!sd->init_ok)
    {
        return SDC_FAILURE;
    }

    file = fl_fopen(filename, "rb");
    if(!file)
    {
        sdc_logout("sdc_fat_read_file fl_fopen failure !", 0);
        return SDC_NOFILE;
    }

    sdc_logout("sdc_fat_read_file fl_fseek(offset=%d) ...", offset);
    if(fl_fseek(file, offset, SEEK_SET) != 0)
    {
        sdc_logout("sdc_fat_read_file fl_fseek failure !", 0);
        fl_fclose(file);
        return SDC_FATERROR;
    }

    sdc_logout("sdc_fat_read_file fl_fread(length=%d) ...", length);
    fl_fread(buffer, 1, length, file);

    if(read_length)
    {
        *read_length = length;
    }

    sdc_logout("sdc_fat_read_file OK, length = %d", length);

    fl_fclose(file);

    return SDC_OK;
}


sdc_ret_t sdc_fat_write_file(char* filename, unsigned long length, unsigned char* buffer, unsigned long* write_length)
{
    sdcard_t* sd = sc_get_sdcard_buff();
    FL_FILE *file;
    unsigned long tmp;

    if(write_length)
    {
        *write_length = 0;
    }

    /* Verify if need to reinitialize SPI & FAT library. */
    if((!sd->init_ok) || (sdc_is_reinsert()))
    {
        sdc_fat_init();
    }

    /* Can't use SD card ? */
    if(!sd->init_ok)
    {
        return SDC_FAILURE;
    }

    /* First, delete previous file, if exist. */
    fl_remove(filename);

    file = fl_fopen(filename, "wb");
    if(!file)
    {
        sdc_logout("sdc_fat_write_file fl_fopen failure (%s) !", filename);
        return SDC_NOFILE;
    }

    tmp = fl_fwrite(buffer, 1, length, file);
    if(tmp != length)
    {
        sdc_logout("sdc_fat_write_file fl_fwrite failure (tmp=%d) !", tmp);
        fl_fclose(file);
        return SDC_FATERROR;
    }

    if(write_length)
    {
        *write_length = tmp;
    }

    sdc_logout("sdc_fat_write_file OK, length = %d", tmp);

    fl_fclose(file);

    return SDC_OK;
}

sdc_ret_t sdc_fat_append_file(char* filename, unsigned long length, unsigned char* buffer, unsigned long* write_length)
{
    sdcard_t* sd = sc_get_sdcard_buff();
    FL_FILE *file;
    unsigned long tmp;

    if(write_length)
    {
        *write_length = 0;
    }

    /* Verify if need to reinitialize SPI & FAT library. */
    if((!sd->init_ok) || (sdc_is_reinsert()))
    {
        sdc_fat_init();
    }

    /* Can't use SD card ? */
    if(!sd->init_ok)
    {
        return SDC_FAILURE;
    }

    file = fl_fopen(filename, "ab");
    if(!file)
    {
        sdc_logout("sdc_fat_append_file fl_fopen failure (%s) !", filename);
        return SDC_NOFILE;
    }

    tmp = fl_fwrite(buffer, 1, length, file);
    if(tmp != length)
    {
        sdc_logout("sdc_fat_append_file fl_fwrite failure (tmp=%d) !", tmp);
        fl_fclose(file);
        return SDC_FATERROR;
    }

    if(write_length)
    {
        *write_length = tmp;
    }

    sdc_logout("sdc_fat_append_file OK, length = %d", tmp);

    fl_fclose(file);

    return SDC_OK;
}

sdc_ret_t sdc_fat_mkdir(char* directory)
{
    sdcard_t* sd = sc_get_sdcard_buff();

    /* Verify if need to reinitialize SPI & FAT library. */
    if((!sd->init_ok) || (sdc_is_reinsert()))
    {
        sdc_fat_init();
    }

    /* Can't use SD card ? */
    if(!sd->init_ok)
    {
        return SDC_FAILURE;
    }

    fl_createdirectory(directory);

    return SDC_OK;
}


sdc_ret_t sdc_fat_filelist(scdf_file_entry_t* list_ptr, unsigned long list_count, unsigned long list_offset, char* path, unsigned long* file_count_ptr)
{
    sdcard_t* sd = sc_get_sdcard_buff();

    sdc_logout("sdc_fat_filelist::path = \"%s\"", path);

    /* Verify if need to reinitialize SPI & FAT library. */
    if((!sd->init_ok) || (sdc_is_reinsert()))
    {
        sdc_fat_init();
    }

    /* Can't use SD card ? */
    if(!sd->init_ok)
    {
        return SDC_FAILURE;
    }

    /* Initialize output parameters. */
    if(file_count_ptr)
    {
        *file_count_ptr = 0;
    }
    memset((void*)list_ptr, 0, list_count*sizeof(scdf_file_entry_t));

    FL_DIR dirstat;

    // FL_LOCK(&_fs);

    long file_index = 0;
    long list_index = 0;
    if (fl_opendir(path, &dirstat))
    {
        struct fs_dir_ent dirent;

        while (fl_readdir(&dirstat, &dirent) == 0)
        {
            if((list_ptr)
            && (list_count != 0)
            && (file_index >= list_offset)
            && (file_index < (list_offset+list_count)))
            {
                list_ptr[list_index].flags = FILEENTRY_FLAG_ENABLED;
                if(dirent.is_dir)
                {
                    list_ptr[list_index].flags |= FILEENTRY_FLAG_FOLDER;
                }
                strncpy(list_ptr[list_index].filename, dirent.filename, SC_FILENAME_MAXLEN);
                list_ptr[list_index].filename[SC_FILENAME_MAXLEN-1] = '\0';
                list_ptr[list_index].file_index = file_index;
                list_index++;
            }
            file_index++;
        }

        fl_closedir(&dirstat);
    }

    // FL_UNLOCK(&_fs);

    /* Update output parameter. */
    if(file_count_ptr)
    {
        *file_count_ptr = file_index;
    }

    return SDC_OK;
}


sdc_ret_t sdc_fat_unlink(char* path)
{
    sdcard_t* sd = sc_get_sdcard_buff();

    /* Verify if need to reinitialize SPI & FAT library. */
    if((!sd->init_ok) || (sdc_is_reinsert()))
    {
        sdc_fat_init();
    }

    /* Can't use SD card ? */
    if(!sd->init_ok)
    {
        return SDC_FAILURE;
    }

    sdc_logout("sdc_fat_unlink(%s)", path);

    fl_remove(path);

    return SDC_OK;
}

