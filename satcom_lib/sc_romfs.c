/**
 *  SatCom Library
 *  by cafe-alpha.
 *  WWW: http://ppcenter.free.fr/satcom/
 *
 *  See LICENSE file for details.
**/

#include <stdio.h>
#include <string.h>

#include "sc_romfs.h"


/* Compression routines. */
#include "sc_compress.h"

/* sc_toupper/sc_tolower functions. */
#include "sc_common.h"


/* Debug macro, for both Win32 and Saturn ports. */
#ifdef SC_SATURN
#include "sc_saturn.h"
#   define logout(_STR_, ...) {;}
//#   define logout(_STR_, ...) {scd_logout(_STR_, __VA_ARGS__);}
#else
#   define logout(_STR_, ...) {printf(_STR_, __VA_ARGS__);}
#endif

/**
 * romfs_decomp_getsize
 * Return decompressed size from input buffer.
**/
unsigned long romfs_decomp_getsize(unsigned char* ind, unsigned long inl)
{
    unsigned long ret = 0;

    ret = ((ind[0]) << 24)
        | ((ind[1]) << 16)
        | ((ind[2]) <<  8)
        | ((ind[3]) <<  0);

    return ret;
}

/**
 * Decompress data.
 * It is supposed there is enough space to decompress the data.
 * Note : output data length is automatically computed from input data.
 *  ind : input data.
 *  inl : input data length.
 *  outd: output data.
 *  offset,size : decompression range within output data.
 * Return total decompressed data size.
**/
unsigned char _romfs_decomp_buffer[ROMFS_COMP_CHUNK];
unsigned long romfs_decomp_exec(unsigned char* ind, unsigned long inl, unsigned char* outd, unsigned long offset, unsigned long size)
{
    unsigned long len, decomp_len;
    unsigned long in_offset, i;

    decomp_len = romfs_decomp_getsize(ind, inl);
    ind += 4; /* Skip header. */

    in_offset = 0;
    len = 0;
    do
    {
        /* Get chunk size. */
        unsigned long chunklen, chunk_decomp_len;
        chunklen = ((ind[0]) << 8)
                 | ((ind[1]) << 0);
        ind += 2;

        /* Decompress current chunk. */
        chunk_decomp_len = decomp_exec(ind, chunklen, _romfs_decomp_buffer);
logout("~~~ in_ofs[0x%08X] decomp(chklen=%5d) -> %5d [%02X %02X %02X %02X ...]\n", 
        in_offset, 
        chunklen, chunk_decomp_len, 
        _romfs_decomp_buffer[0], 
        _romfs_decomp_buffer[1], 
        _romfs_decomp_buffer[2], 
        _romfs_decomp_buffer[3]);
        ind += chunklen;

        /* If data is in decompression range, copy to destination buffer. */
        for(i=0; i<chunk_decomp_len; i++)
        {
            if(((in_offset) >= offset      )
            && ((in_offset) < (offset+size)))
            {
                *outd++ = _romfs_decomp_buffer[i];
            }
            in_offset++;
        }

        /* No need to decompress next chunk(s) ? */
        if(in_offset >= (offset+size))
        {
            return decomp_len;
        }

        len  += chunk_decomp_len;
    } while(len < decomp_len);

    return decomp_len;
}


int romfs_check_header(unsigned char* romfs, int crc_check)
{
    romfs_header_t hdr;

    if(romfs == NULL) return 0;

    memcpy((void*)&hdr, (void*)romfs, sizeof(romfs_header_t));

    if(hdr.magic[0] != 'R') return 0;
    if(hdr.magic[1] != 'o') return 0;
    if(hdr.magic[2] != 'm') return 0;
    if(hdr.magic[3] != 'F') return 0;
    if(hdr.magic[4] != 's') return 0;
    if(hdr.magic[5] != '1') return 0;
    if(hdr.magic[6] != 0xCA) return 0;
    if(hdr.magic[7] != 0xFE) return 0;

    /* Verify if at least header is present in romfs data. */
    if(hdr.size < sizeof(romfs_header_t))
    {
        return 0;
    }

    /* CRC check (optional). */
    if(crc_check)
    {
        if(hdr.crc32 != crc32_calc((void*)(romfs + sizeof(romfs_header_t)), hdr.size - sizeof(romfs_header_t)))
        {
            return 0;
        }
    }

    return 1;
}


/* Internal function to compare filenames. */
int _romfs_compare_filename(char* filename, int len, char* txt)
{
    int i;
    for(i=0; i<(len-1); i++)
    {
        if(filename[i] != sc_toupper(txt[i]))
        {
            return 0;
        }
    }

    return 1;
}

romfs_entry_t* romfs_get_first_entry(unsigned char* romfs, char* path)
{
    /* File name currently processing. */
    char filename[SC_FILENAME_MAXLEN];
    int i, j, len;
    /* Pointer to current entry. */
    romfs_entry_t* e;

    /* Entry buffer. Used to access 2/4 byte sized elements inside. */
    romfs_entry_t entry_buff;

logout("romfs_get_first_entry path=%s\n", path);
    /* Check data integrity. */
    if(!romfs_check_header(romfs, 0/*crc_check*/))
    {
logout("header check error !\n", 0);
        return NULL;
    }

    /* Set pointer to first entry in root folder. */
    e = (romfs_entry_t*)(romfs + sizeof(romfs_header_t));

    /* Search for each file names in path. */
    i = (path[0] == '/' ? 1 : 0);
    j = i;

    do
    {
        if((path[i] == '/')
        || (path[i] == '\0'))
        {
            /* Filename length, including terminating null character. */
            len = i-j+1;

            if(len <= 1)
            {
                return e;
            }

            /* Path sanity check. */
            if(len > SC_FILENAME_MAXLEN)
            {
                return NULL;
            }

            /* Copy current filename to buffer. */
            memcpy((void*)filename, (void*)(path+j), len);
            filename[len-1] = '\0';
logout("Filename (len=%d, i=%d, j=%d) : %s\n", len, i, j, filename);
            /* Prepare filename for comparison. */
            for(j=0; j<(len-1); j++)
            {
                filename[j] = sc_toupper(filename[j]);
            }

            /* Search for corresponding entry in entry list. */
            while(1)
            {
logout(" +\"%s\"\n", ((char*)e) + sizeof(romfs_entry_t));
                /* Compare filenames and entry type. */
                if((e->flags & FILEENTRY_FLAG_ENABLED)
                && (e->filename_len == len)
                && (_romfs_compare_filename(filename, len, ((char*)e) + sizeof(romfs_entry_t))))
                {
                    /* Entry found. If needed, change folder. */
                    if(e->flags & FILEENTRY_FLAG_FOLDER)
                    {
                        memcpy((void*)&entry_buff, e, sizeof(romfs_entry_t));
logout(" +++ dir=%d\n", entry_buff.offset);
                        e = (romfs_entry_t*)(romfs + entry_buff.offset);
                    }

                    break;
                }

                /* Nothing found and no more entry ? */
                if(e->flags & FILEENTRY_FLAG_LASTENTRY)
                {
                    return NULL;
                }

                /* Jump to next entry. */
                e = (romfs_entry_t*)(((unsigned char*)e) + sizeof(romfs_entry_t) + e->filename_len);
            }

            /* Update offset to begining of file name. */
            j = i+1;
        }
    } while(path[i++] != '\0');

    return e;
}


romfs_entry_t* romfs_get_next_entry(unsigned char* romfs, romfs_entry_t* entry)
{
    romfs_entry_t* e = entry;

    /* Not really needed, but in case of ... */
    if(e == NULL)
    {
        return NULL;
    }

    /**
     * Note filename and length are 1 byte data, 
     * so it is safe to read them directly from input pointer.
    **/

    /* Last entry ? */
    if(e->flags & FILEENTRY_FLAG_LASTENTRY)
    {
        return NULL;
    }

    do
    {
        /* Jump to next entry. */
        e = (romfs_entry_t*)(((unsigned char*)e) + sizeof(romfs_entry_t) + e->filename_len);

        /* Check that next entry is not last and disabled. */
        if((e->flags & (FILEENTRY_FLAG_LASTENTRY|FILEENTRY_FLAG_ENABLED)) == FILEENTRY_FLAG_LASTENTRY)
        {
            return NULL;
        }

        /* Skip disabled entries. */
    } while((e->flags & FILEENTRY_FLAG_ENABLED) == 0);

    return e;
}

unsigned long romfs_get_entry_size(unsigned char* romfs, romfs_entry_t* e)
{
    unsigned long ret;

    /* Can't get size from folder. */
    if(e->flags & FILEENTRY_FLAG_FOLDER)
    {
        return 0;
    }

    if(e->flags & FILEENTRY_FLAG_COMPRESSED)
    {
        ret = romfs_decomp_getsize(romfs+e->offset, e->size);
    }
    else
    {
        ret = e->size;
    }

    return ret;
}

void romfs_get_entry_data(unsigned char* romfs, romfs_entry_t* e, unsigned char* buffer, unsigned long offset, unsigned long size)
{
    /* Can't get data from folder. */
    if(e->flags & FILEENTRY_FLAG_FOLDER)
    {
        return;
    }

    /* Copy/decompress data. */
    if(e->flags & FILEENTRY_FLAG_COMPRESSED)
    {
        romfs_decomp_exec(romfs+e->offset, e->size, buffer, offset, size);
    }
    else
    {
        memcpy((void*)buffer, (void*)(romfs+e->offset+offset), size);
    }
}


unsigned char* romfs_get_entry_ptr(unsigned char* romfs, romfs_entry_t* e, unsigned long* datalen)
{
    unsigned char* ret;

    if(datalen)
    {
        *datalen = 0;
    }

    if(e->flags & FILEENTRY_FLAG_FOLDER)
    {
        ret = NULL;
    }

    if(e->flags & FILEENTRY_FLAG_COMPRESSED)
    {
        /* Returning pointer to compressed data
         * doesn't make sense, so return NULL instead.
         */
        ret = NULL;
    }
    else
    {
        if(datalen)
        {
            *datalen = e->size;
        }
        ret = romfs+e->offset;
    }

    return ret;
}