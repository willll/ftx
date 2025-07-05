#ifndef __FAT_INTERNAL_H__
#define __FAT_INTERNAL_H__

#ifdef SC_SATURN
    /* Definition of :
     *  - logging function.
     */
    #include "../../sc_saturn.h"
#else

    #include <stdio.h>

#endif



// Printf output (directory listing / debug)
#include <stdarg.h>
#ifdef SC_SATURN
    #define FAT_PRINTF(a)               sdc_logout a
#else
    void _fat_printf(char* string, ...);
    #define FAT_PRINTF(a)               _fat_printf a
#endif

#endif
