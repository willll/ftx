#include "thinfat32.h"
void tf_logout_end(void);


#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NO_ERROR 0
#define FILE_OPEN_ERROR -1
#define DATA_READ_ERROR -2
#define DATA_WRITE_ERROR -3
#define DATA_MISMATCH_ERROR -4


void test_dirlist(char* path)
{
    tf_file_t *fp;

    // Directory listing.
    char filename[TF_FILENAME_MAXLEN];
    int file_cnt = 0;
    tf_logout(1, "Directory listing Test Start", 0);
    tf_logout(1, "Path = \"%s\"", path);
    tf_logout(1, "-----------------------", 0);
    fp = tf_findfiles_start((uint8_t*)path);
    if(fp)
    {
        int find_res;
        do
        {
            find_res = tf_findfiles_get(fp, (uint8_t*)filename);
            if(find_res & TF_GETFILENAME_VALID)
            {
                file_cnt++;
                tf_logout(1, "+Name : res[0x%02X] CNT[%3d] PATH[%s] FILE[%s]", 
                    find_res, 
                    file_cnt, 
                    fp->path, 
                    filename);
            }
            else
            {
                tf_logout(1, "~find_res[0x%02X] FILE[%02X %02X %02X %02X ...]", 
                    find_res, 
                    filename[0], 
                    filename[1], 
                    filename[2], 
                    filename[3]);
            }
        } while(((find_res & TF_GETFILENAME_END) == 0) && (file_cnt < 200)/*limit ouput in the case of bugs ...*/);
        tf_findfiles_close(fp);
    }
    tf_logout(1, "Directory listing Test END (file_cnt=%d)", file_cnt);
}


/*
 * Open a file, read its contents, if the contents match, return 0
 *If the contents don't match, or any other error occurs, return 
 * an appropriate error code.
 */
int test_basic_read(char* input_file)
{
    char reference_file[MAX_PATH];
    tf_file_t *fp;
    unsigned char* data_ref = NULL;
    unsigned char* data_fat = NULL;
    unsigned long size;

    /* Set path to reference file. */
    sprintf(reference_file, "FAT32_files%s", input_file);
    tf_logout(1, "[test_basic_read] : Input file: \"%s\"", input_file);
    tf_logout(1, "[test_basic_read] : Ref.  file: \"%s\"", reference_file);

    /* Open reference file on PC. */
    FILE* fp_ref = fopen(reference_file, "rb");
    if(!fp_ref)
    {
        tf_logout(1, "[test_basic_read] : open reference file error (%s) !", reference_file);
        return FILE_OPEN_ERROR;
    }
    fseek(fp_ref, 0, SEEK_END);
    size = ftell(fp_ref);
    fseek(fp_ref, 0, SEEK_SET);

    /* Allocate test & reference buffers. */
    data_ref = (unsigned char*)malloc(size);
    if(data_ref == NULL)
    {
        tf_logout(1, "[test_basic_read] : malloc error (size=%d) !", size);
        return FILE_OPEN_ERROR;
    }
    data_fat = (unsigned char*)malloc(size);
    if(data_fat == NULL)
    {
        free(data_ref);

        tf_logout(1, "[test_basic_read] : malloc error (size=%d) !", size);
        return FILE_OPEN_ERROR;
    }

    /* Read whole reference file. */
    fread(data_ref, 1, size, fp_ref);
    fclose(fp_ref);
    tf_logout(1, "[test_basic_read] : read reference file (size=%d bytes)", size);


    /* Open file using thinfat32 API. */
    fp = tf_fopen(input_file, TF_MODE_READ);

    if(!fp)
    {
        tf_logout(1, "[test_basic_read] : fp is NULL ...", 0);
        free(data_ref);
        free(data_fat);
        return DATA_MISMATCH_ERROR;
    }

    /* Verify file size. */
    tf_logout(1, "[test_basic_read] : fp->size = %d", fp->size);
    if(fp->size != size)
    {
        tf_logout(1, "[test_basic_read] : fp->size[%d] != size[%d]=%d !", fp->size, size);

        free(data_ref);
        free(data_fat);
        tf_fclose(fp);

        return DATA_MISMATCH_ERROR;
    }

    memset(data_fat, '\0', size);
    tf_fread(data_fat, size, fp);

    if(memcmp(data_fat, data_ref, size) != 0)
    {
        /* Dump file to disk. */
        char dump_file[MAX_PATH];
        sprintf(dump_file, "./%s.DMP", input_file);
        FILE* fp_dmp = fopen(dump_file, "wb");
        if(fp_dmp)
        {
            tf_logout(1, "[test_basic_read] : Dump contents to \"%s\" ...", dump_file);
            fwrite(data_fat, 1, size, fp_dmp);
            fclose(fp_dmp);
        }


        free(data_ref);
        free(data_fat);
        tf_fclose(fp);

        return DATA_MISMATCH_ERROR;
    }

    free(data_ref);
    free(data_fat);
    tf_fclose(fp);

    return NO_ERROR;
}

/*
 * Open a file, write a string to it, return 0.
 * Return an appropriate error code if there's any problem.
 */
int test_basic_write_append(char* input_file, char* pc_file, int append)
{
    tf_file_t *fp;
    unsigned char* data  = NULL;
    unsigned char* verif = NULL;
    unsigned long size;
    int rc;

    if(pc_file == NULL)
    {
        if((input_file[0] == '\\')
        || (input_file[0] == '/'))
        {
            pc_file = input_file + 1;
        }
        else
        {
            pc_file = input_file;
        }
    }

    /* Set path to reference file. */
    tf_logout(1, "[test_basic_read] : Input file (FAT32): \"%s\"", input_file);
    tf_logout(1, "[test_basic_read] : Input file (PC)   : \"%s\"", pc_file);

    /* Open reference file on PC. */
    FILE* fp_ref = fopen(pc_file, "rb");
    if(!fp_ref)
    {
        tf_logout(1, "[test_basic_read] : open reference file error (%s) !", pc_file);
        return FILE_OPEN_ERROR;
    }
    fseek(fp_ref, 0, SEEK_END);
    size = ftell(fp_ref);
    fseek(fp_ref, 0, SEEK_SET);

    /* Allocate test & reference buffers. */
    data = (unsigned char*)malloc(size);
    if(data == NULL)
    {
        tf_logout(1, "[test_basic_read] : malloc error (size=%d) !", size);
        return FILE_OPEN_ERROR;
    }
    verif = (unsigned char*)malloc(size);
    if(verif == NULL)
    {
        free(data);
        tf_logout(1, "[test_basic_read] : malloc error (size=%d) !", size);
        return FILE_OPEN_ERROR;
    }

    /* Read whole reference file. */
    fread(data, 1, size, fp_ref);
    fclose(fp_ref);
    tf_logout(1, "[test_basic_read] : read reference file (size=%d bytes)", size);


    /* Open file using thinfat32 API. */
    fp = tf_fopen(input_file, append ? TF_MODE_APPEND : TF_MODE_WRITE);

    uint32_t init_size = 0;
    if(append)
    {
        init_size = fp->size - 1;
    }
    tf_logout(1, "[TEST] test_basic_write STT (%s), init_size = %d", input_file, init_size);

    if(fp)
    {
        tf_logout(1, "[TEST] writing data to file ...", 0);
        rc = tf_fwrite(data, size, fp);
        tf_fclose(fp);
        if(rc<1)
        {
            free(verif);
            free(data);
            return DATA_WRITE_ERROR;
        }
    }

// free(verif);
// free(data);
// return NO_ERROR;

    /* Re-open file using thinfat32 API, in order to verify file contents. */
    fp = tf_fopen(input_file, TF_MODE_READ);

    if(!fp)
    {
        tf_logout(1, "[test_basic_write::verif] : fp is NULL ...", 0);
        free(verif);
        free(data);
        return DATA_MISMATCH_ERROR;
    }

    /* Seek to append initial size. */
    tf_fseek(fp, init_size, 0/*offset*/);

    /* Verify file size. */
    tf_logout(1, "[test_basic_write::verif] : fp->size = %d", fp->size);
    if(fp->size != (init_size+size))
    {
        tf_logout(1, "[test_basic_write::verif] : fp->size[%d] != size[%d]+init_size[%d]=%d !", fp->size, size, init_size, size+init_size);

        free(verif);
        free(data);
        tf_fclose(fp);

        return DATA_MISMATCH_ERROR;
    }

    memset(verif, '\0', size);
    tf_logout(1, "[test_basic_write::verif] : tf_fread(0x%08X, %d, 0x%08X) ...", verif, size, fp);
    tf_fread(verif, size, fp);
    tf_logout(1, "[test_basic_write::verif] : tf_fread END", 0);

    if(memcmp(verif, data, size) != 0)
    {
        /* Dump file to disk. */
        char dump_file[MAX_PATH];
        sprintf(dump_file, "./%s.DMP", input_file);
        FILE* fp_dmp = fopen(dump_file, "wb");
        if(fp_dmp)
        {
            tf_logout(1, "[test_basic_read] : Dump contents to \"%s\" ...", dump_file);
            fwrite(verif, 1, size, fp_dmp);
            fclose(fp_dmp);
        }

        free(verif);
        free(data);
        tf_fclose(fp);

        return DATA_MISMATCH_ERROR;
    }

    free(verif);
    free(data);
    tf_fclose(fp);

    return NO_ERROR;
}


int main(int argc, char **argv)
{
    tf_file_t *fp;
    char data;
    int rc;

    tf_logout(1, "FAT32 Filesystem Test", 0);
    tf_logout(1, "-----------------------", 0);
    tf_init();

#if 1
    // Directory & sub-directory creation test
    if(rc = tf_mkdir("/test01", 0/*mkParents*/))
    {
        tf_logout(1, "[TEST] mkdir test#1 failed with error code 0x%x", rc);
        tf_logout_end();
        return 0;
    }
    else
    {
        tf_logout(1, "[TEST] mkdir test#1 PASSED.", 0);
    }
#endif

#if 0
    // BASIC READ, Root directory, LFN
    tf_logout(1, "[TEST] Basic LFN read test", 0);
    //if(rc = test_basic_read("/SCTest.iso"/*fat*/))
    if(rc = test_basic_read("/DHR99/DHRDATA/SYN.MME"/*fat*/))
    {
        tf_logout(1, "failed with error code 0x%x (%d)", rc, rc);
        tf_logout_end();
        return 0;
    }
    else
    {
        tf_logout(1, " PASSED.", 0);
    }
#endif

#if 1
    // BASIC WRITE, Root directory, 8.3 filename
    if(rc = test_basic_write_append("/TEST01/wrtest.dat", NULL/*pc_file*/, 0/*append*/))
    {
        tf_logout(1, "[TEST] Basic 8.3 write test#1 failed with error code 0x%x", rc);
        tf_logout_end();
        return 0;
    }
    else
    {
        tf_logout(1, "[TEST] Basic 8.3 write test#1 PASSED.", 0);
    }
    if(rc = test_basic_write_append("/TEST01/TEST1.JPG", NULL/*pc_file*/, 0/*append*/))
    {
        tf_logout(1, "[TEST] Basic 8.3 write test#2 failed with error code 0x%x", rc);
        tf_logout_end();
        return 0;
    }
    else
    {
        tf_logout(1, "[TEST] Basic 8.3 write test#2 PASSED.", 0);
    }
    if(rc = test_basic_write_append("/TEST01/TEST2.jpg", NULL/*pc_file*/, 0/*append*/))
    {
        tf_logout(1, "[TEST] Basic 8.3 write test#3 failed with error code 0x%x", rc);
        tf_logout_end();
        return 0;
    }
    else
    {
        tf_logout(1, "[TEST] Basic 8.3 write test#3 PASSED.", 0);
    }

    // BASIC APPEND
    if(rc = test_basic_write_append("/TEST01/WrTest.dat", "FAT32_files\\kbqwerty.gif"/*pc_file*/, 1/*append*/))
    {
        tf_logout(1, "[TEST] Basic 8.3 append#1 test failed with error code 0x%x", rc);
        tf_logout_end();
        return 0;
    }
    else
    {
        tf_logout(1, "[TEST] Basic 8.3 append#1 test PASSED.", 0);
    }

    tf_logout(1, "[TEST] Basic 8.3 append#2 test START.", 0);
    if(rc = test_basic_write_append("/TEST01/WrTest.dat", "FAT32_files\\SonicR.cue"/*pc_file*/, 1/*append*/))
    {
        tf_logout(1, "[TEST] Basic 8.3 append#2 test failed with error code 0x%x", rc);
        tf_logout_end();
        return 0;
    }
    else
    {
        tf_logout(1, "[TEST] Basic 8.3 append#2 test PASSED.", 0);
    }
#endif

#if 1
    // Create, then delete file.
    if(rc = test_basic_write_append("/TEST01/deltest.dat", NULL/*pc_file*/, 0/*append*/))
    {
        tf_logout(1, "[TEST] Basic 8.3 write test#1 failed with error code 0x%x", rc);
        tf_logout_end();
        return 0;
    }
    rc = tf_remove("/TEST01/deltest.dat");
    tf_logout(1, "[TEST] remove file rc=%d", rc);
    if(rc)
    {
        tf_logout(1, "[TEST] 8.3 file delete test failed with error code 0x%x", rc);
        tf_logout_end();
        return 0;
    }
#endif


#if 1
    test_dirlist("/");
    test_dirlist("/TEST01");
    test_dirlist("/TEST01/SUBDIR");
#endif

    tf_logout_end();
    return 0;
}

