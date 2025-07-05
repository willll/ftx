//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
//                            FAT16/32 File IO Library
//                                    V2.6
//                              Ultra-Embedded.com
//                            Copyright 2003 - 2012
//
//                         Email: admin@ultra-embedded.com
//
//                                License: GPL
//   If you would like a version with a more permissive license for use in
//   closed source commercial applications please contact me for details.
//-----------------------------------------------------------------------------
//
// This file is part of FAT File IO Library.
//
// FAT File IO Library is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.
//
// FAT File IO Library is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with FAT File IO Library; if not, write to the Free Software
// Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
#include <string.h>

#include "fat_internal.h"
#include "fat_global.h"


#ifdef SC_SATURN
#else
char logout_buff[1024];
void _fat_printf(char* string, ...)
{
    FILE* logout_fp = NULL;
    va_list argptr;

    /* Print the log message to the memory. */
    tf_memset((void*)logout_buff, 0, sizeof(logout_buff));
    va_start(argptr, string);
    vsnprintf(logout_buff, sizeof(logout_buff)-4, string, argptr);
    va_end(argptr);

    int len = tf_strlen((const uint8_t*)logout_buff);
    logout_buff[len++] = '\r';
    logout_buff[len++] = '\n';

    if(logout_fp == NULL)
    {
        logout_fp = fopen ("tests.log", "a+");
    }
    if(logout_fp)
    {
        fwrite(logout_buff, len, 1, logout_fp);
        fclose(logout_fp); logout_fp = NULL;
    }
}
#endif
