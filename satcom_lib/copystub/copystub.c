/**
 *  SatCom Library
 *  by cafe-alpha.
 *  WWW: http://ppcenter.free.fr/satcom/
 *
 *  See LICENSE file for details.
**/

#include "copystub.h"


#ifdef COPYSTUB_TEST

#   define HRAM_STT 200
#   define HRAM_END 210

#   define LRAM_STT 100
#   define LRAM_END 110

/* When debugging on PC, display debug messages to stdout. */
#   include <stdio.h>
#   define DBG(CONTEN, STR) printf("C=0x%02X, DBG=%s\n", CONTEN, STR)

#else


// HRAM 0x06004000 ~ 0x060FFFFF
#   define HRAM_STT 0x06004000
#   define HRAM_END 0x06100000

// LRAM (don't erase the last bytes containing copystub executable).
#   define LRAM_STT 0x00200000
#   define LRAM_END COPYSTUB_START_ADRESS

/* Set Saturn compile options here. */
#   define DBG(CONTEN, STR) 

#endif


/* SC_SOFTRES_* definition. */
#ifdef COPYSTUB_TEST
#else
#include "../sc_common.h"
#endif


#define CLAMP(x, low, high)  (((x) > (high)) ? (high) : (((x) < (low)) ? (low) : (x)))



#ifdef COPYSTUB_TEST
int main(int argc, char** argv)
#else
int _main(unsigned long softres_flags, unsigned char* src_stt, unsigned long length, unsigned char* dst_stt)
#endif
{
    /* Retrieve the parameters (buffer address, length, execution start address). */
#ifdef COPYSTUB_TEST
    unsigned char* src_stt = (unsigned char*)(atoi(argv[1]) & 0x0FFFFFFF);
    unsigned long length   =                  atoi(argv[2]);
    unsigned char* dst_stt = (unsigned char*)(atoi(argv[3]) & 0x0FFFFFFF);
#else
#endif
    unsigned char* dst_end = dst_stt + length;

    /* Data used for copy/erase. */
    unsigned char* ptr1;
    unsigned char* ptr2;
    /* Data used for erase only. */
    unsigned long erase_stt, erase_end;
    unsigned char i, j;

    int ram_clear[2];

#ifdef COPYSTUB_TEST
    ram_clear[0] = 1;
    ram_clear[1] = 1;
#else
    ram_clear[0] = softres_flags & SC_SOFTRES_LRAM ? 1 : 0;
    ram_clear[1] = softres_flags & SC_SOFTRES_HRAM ? 1 : 0;
#endif

    DBG(0x00/*MSG*/, "CS::copyPG");
    DBG(0xDD/*Copystub Debug*/, "DBG");

    /* Copy the target program from the source buffer to the execution area. */
    if(src_stt != dst_stt)
    { /* In the case src == dst, don't need to copy anything. */
        if( (dst_stt >  src_stt         )
        &&  (dst_stt < (src_stt+length)) )
        { /* Special case when buffers overlap. */
            ptr1 = src_stt+length - 1;
            ptr2 = dst_end        - 1;
            while((ptr2+1) != dst_stt)
            {
#ifdef COPYSTUB_TEST
                printf("Copy byte from index %3d to %3d [backward]\n", ptr1, ptr2);
                ptr1--;
                ptr2--;
#else
                *(ptr2--) = *(ptr1--);
#endif
            }
        }
        else
        { /* Normal copy when buffers don't overlap. */
            ptr1 = src_stt;
            ptr2 = dst_stt;
            while(ptr2 != dst_end)
            {
#ifdef COPYSTUB_TEST
                printf("Copy byte from index %3d to %3d\n", ptr1, ptr2);
                ptr1++;
                ptr2++;
#else
                *(ptr2++) = *(ptr1++);
#endif
            }
        }
    }

    /* Clear High/Low RAM */
    DBG(0x00/*MSG*/, "CS::clear");
    for(i=0; i<2; i++)
    { /* HRAM-LRAM */
        for(j=0; j<2; j++)
        { /* cropped zone1-cropped zone2 */

            if(i==0)
            { /* Erase LRAM. */
                /* In order not to erase target program, crop the zone to erase. */
                if(j==0)
                { /* Start zone. */
                    erase_stt = LRAM_STT;
                    erase_end = CLAMP((unsigned long)dst_stt, LRAM_STT, LRAM_END);
                }
                else
                { /* End zone. */
                    erase_stt = CLAMP((unsigned long)dst_end, LRAM_STT, LRAM_END);
                    erase_end = LRAM_END;
                }
            }
            else
            { /* Erase HRAM. */
                /* In order not to erase target program, crop the zone to erase. */
                if(j==0)
                { /* Start zone. */
                    erase_stt = HRAM_STT;
                    erase_end = CLAMP((unsigned long)dst_stt, HRAM_STT, HRAM_END);
                }
                else
                { /* End zone. */
                    erase_stt = CLAMP((unsigned long)dst_end, HRAM_STT, HRAM_END);
                    erase_end = HRAM_END;
                }
            }

            if(ram_clear[i])
            {
                /* Erase cropped zone. */
                ptr1 = (unsigned char*)erase_stt;
                ptr2 = (unsigned char*)erase_end;
                for(length=erase_end-erase_stt; length; length--)
                {
#ifdef COPYSTUB_TEST
                    printf("Erase byte at index %3d\n", ptr1);
                    ptr1++;
#else
                    *(ptr1++) = 0;
#endif
                }
            }
        }
    }

    /* Execute the target program (-> quit this program).
     * (code scrap from antime's satclient.c, http://www.iki.fi/~antime/)
     */
    DBG(0x00/*MSG*/, "CS::EXEC");

#ifdef COPYSTUB_TEST
    printf("Execute code from index %3d\n", dst_stt);
#else
    void (*pFun)(void);
    pFun = (void(*)(void))dst_stt;
    (*pFun)();
#endif

    return 0;
}
