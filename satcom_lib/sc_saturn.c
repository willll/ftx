/**
 *  SatCom Library
 *  by cafe-alpha.
 *  WWW: http://ppcenter.free.fr/satcom/
 *
 *  See LICENSE file for details.
**/

#include <string.h> /* For memset,memcpy */
#include <stdarg.h>
#include <stdio.h>
size_t strnlen(const char *s, size_t maxlen);
int rand (void);
void srand (unsigned int seed);



/*
 * SatCom-related include.
 */
#include "sc_saturn.h"



/*
 * Satregs definitions, for internal purpose.
 */
#include "satregs/satregs.h"



/*----------------------------------------------------------------------------*/
/*- Libyaul stuff.                                                          --*/
#ifdef SC_USE_LIBYAUL

/* Should do libyaul includes someday ... */
// #include "libyaul\kernel\lib\memb.h"
// #include "libyaul\kernel\mm\slob.h"
// #include "libyaul\kernel\sys\init.h"

// //#include "libyaul\kernel\vfs\component_name.h"
// //#include "libyaul\kernel\vfs\hash.h"
// //#include "libyaul\kernel\vfs\mount.h"
// //#include "libyaul\kernel\vfs\pathname_cache.h"
// //#include "libyaul\kernel\vfs\vfs.h"
// //#include "libyaul\kernel\vfs\vnode.h"
// //#include "libyaul\kernel\vfs\fs\iso9660\iso9660-internal.h"
// //#include "libyaul\kernel\vfs\fs\iso9660\iso9660.h"
// //#include "libyaul\kernel\vfs\fs\romdisk\romdisk.h"

#endif // SC_USE_LIBYAUL


/**
 * Debugger global data.
**/
#include "debugger/scd_circbuffer.inc.c"

/*
 * SatCom Global data definition.
 * Used for/by debugger
 */
#include "sc_global_data.h"


#if defined( __GNUC__ )
#pragma pack(1)
#else
#pragma pack(push,1)
#endif


/**
 * Global data that need to be stored in RAM.
**/
typedef struct _sc_internal_t
{
    /*-----------------------------------------*/
    /* Pointer to global data. */
    sc_global_t* global;

    /* Pointer to debugger data. */
    scd_data_t* dbg;

    /*-----------------------------------------*/
    /* Link type/etc related settings (saved in backup memory). */
    sc_settings_t set;

    /*-----------------------------------------*/
    /* ARP's IO registers: flag, R, W */
    unsigned long address[3];

    /*-----------------------------------------*/
    /* Set to 1 when comms link was first used. */
    unsigned char use_comms;

    /*-----------------------------------------*/
    /* Set to 1 when SatCom has been intialized. */
    unsigned char init_ok;

    /* Indicates if first log to output to SD card or not. */
    unsigned char already_log_sd;

    /* 4 Bytes alignment, reserved for future use. */
    unsigned char reserved1[1];

    /* Set to 1 when use of SMPC time is allowed. */
    unsigned char smpctime_use;

    /* CDROM use and init status. */
#define SC_CDSTATUS_USE  0x01
#define SC_CDSTATUS_INIT 0x02
    unsigned char cdrom_status;

    /* Set to 1 when custom yabause is used. */
    unsigned char yabause_use;

    /*-----------------------------------------*/
    /* Extension devices (SC_EXT_* value). */
    unsigned char ext_dev;

    /*-----------------------------------------*/
    /* Access 1,2,4 bytes length data as unsigned char/short/long. */
    /* Note : this don't need anymore to be global ? */
    unsigned char* rw4_buffer;
    unsigned long rw4_buffer_buff;

    /* Pointer used when sending data to execute. */
    unsigned char* exec_buffer;

    /*-----------------------------------------*/
    /* Callback function for soft reset is required. */
    Fct_sc_soft_reset soft_reset;

    /*-----------------------------------------*/
    /* Used for SatCom debugging (internal use only !) */
    Fct_scd_screen  dbg_logscreen;

    /*-----------------------------------------*/
    /* Used for SatCom debugging (internal use only !) */
#define SCD_PRINT_LEN 256
    char dbg_print[SCD_PRINT_LEN];

    /* Log file name when logging to SD card. */
    char* logfile_name;

    /*-----------------------------------------*/
    /* File I/O stuff. */
    char filename_buff[SC_PATHNAME_MAXLEN];

#define SCDF_BUFFSIZE 256
    char scd_file_request_buff[SCDF_BUFFSIZE];

    /* Pointer to RomFs data. */
    unsigned char* romfs;

    /* SD card-related buffer. */
    sdcard_t sd;

    /*-----------------------------------------*/
    /* Reserved for future usage and 4 bytes alignment.           */
    /* Keep structure total size to SC_INTERNAL_SIZE bytes (4KB). */
    unsigned char reserved9[SC_INTERNAL_SIZE
            - sizeof(sc_settings_t)
            - SCD_PRINT_LEN
            - SC_PATHNAME_MAXLEN
            - SCDF_BUFFSIZE
            - sizeof(sdcard_t)
            - 56/* Total size of misc. declarations */];
} sc_internal_t;

#if defined( __GNUC__ )
#pragma pack()
#else
#pragma pack(pop)
#endif



/**
 * Pointer to SatCom internal data.
**/
#ifdef FBA_BUILD
extern sc_internal_t* _sc;
#else
sc_internal_t* _sc = NULL;
#endif

sc_internal_t* sc_get_internal(void)
{
#ifdef SC_EXEC_IN_ROM
    /**
     * Use memory for backup library.
     * Size allocated to backup library workram is 8KB, 
     * and satcom related stuff is put at the end of this area.
    **/
    unsigned long* ptr = *(unsigned long **)(0x6000354);
    ptr += (8*1024) / sizeof(unsigned long);
    ptr -= SC_WORKRAM_U32SIZE;

    return (sc_internal_t*)ptr;
#else
    /* Return pointer to user area memory. */
    return _sc;
#endif
}










#define CARTRAM_ID_8M       0x5a
#define CARTRAM_ID_32M      0x5c

#define CartRAM0    ((void *)0x02400000)
#define CartRAM1    ((void *)0x02600000)

//  RB_initRAMCart(unsigned char cs)
//  - checks for present RAM cart, inits and verifies it
//
//  returns:
//      cartridge id    -   on success
//                          (8Mb: 0x5a or 32Mb: 0x5c)
//      0xFF            -   no cart
//      0xFE            -   data verify failed 

/*
 * When verifying all the data in the memory cart, about 3~4 seconds
 * are required, so in order to speed-up memory cart verify process, 
 * only some data in the memory cart are checked.
 */
const char* _ram_status_str[] =
{
    "Not detected    ", 
    "1MB test failure", 
    "4MB test failure", 
    "1MB init OK     ", 
    "4MB init OK     ", 
};

unsigned char RB_CartRAM_init(sc_cartram_t* ram, unsigned long detect_flags)
{
    //unsigned long setReg, refReg;
    unsigned char id;

    ram->verify_ok = 1;

    // 3. write 1 to initialize
    // is at the end of A-BUS Dummy area
    // !!! word format !!! (but what is meant?)
    *((unsigned short *)0x257efffe) = 1;

    // cartridge initialization: as in ST-TECH-47.pdf
    // 1. verify cartridge id
    // 5A - 8 Mb
    // 5C - 32 Mb
    //! is last byte of A-BUS CS1 area
    id = *((unsigned char *)0x24ffffff);
    if(id == 0x5a)
    {
        /*
         * 1MB RAM detected !
         * Physical location of RAM area is
         *  0x22400000 (512KB) and 0x22600000 (512KB)
         * But as RAM areas are mirrored every 512KB, 
         * last mirror of first RAM upper half (512KB) 
         * and RAM lower half (512KB) can be accessed
         * as a 1MB continous RAM block.
         */
        ram->detected = 1;
        ram->size = 1*1024*1024;
        ram->ptr  = (void*)0x22580000;
    }
    else if(id == 0x5c)
    {
        ram->detected = 1;
        ram->size = 4*1024*1024;
        ram->ptr  = (void*)0x22400000;
    }
    else
    {
        ram->detected = 0;
        ram->size = 0;
        ram->ptr  = NULL;
        // 2. prompt proper connection
        //if(quiet == 0) {
        //    slPrint("The extended RAM", slLocate(11,5));
        //    slPrint("cartridge is not", slLocate(11,6));
        //    slPrint("inserted properly.", slLocate(11,7));
        //
        //    slPrint("Please turn off", slLocate(11,9));
        //    slPrint("power and reinsert", slLocate(11,10));
        //    slPrint("the extended RAM", slLocate(11,11));
        //    slPrint("cartridge.", slLocate(11,12));
        //}    
        ram->status_str = _ram_status_str[0];
        return 0xFF; // Error, no cart
    }
    
    // // 4. set A-bus set and refresh registers
    // setReg = refReg = 0;
    // if(cs == 0)
    // {
    //     // set cs0 for 32MBit
    //     setReg |= 1 << 29;  // After-READ precharge insert bit
    //     setReg |= 3 << 24;  // Burst cycle wait number
    //     setReg |= 3 << 20;  // Normal cycle wait number
    // }
    // else
    // {
    //     // set cs1 for 16MBit ??
    //     setReg |= 1 << 28;  // external wait effective bit
    //     setReg |= 15 << 24; // Burst cycle wait number
    //     setReg |= 15 << 20; // Normal cycle wait number
    // }
    // *((unsigned long *)0x25fe00B0) = setReg;
    // // A-Bus refresh register
    // refReg |= 1 << 4;       // A-Bus refresh enable bit
    // // I've just take this value
    // // see SCU user manual for value range
    // refReg |= 1;            // A-Bus refresh cycle wait
    // *((unsigned long *)0x25fe00B8) = refReg;
    (*(unsigned long *)0x25FE00B0) = 0x23301FF0;
    (*(unsigned long *)0x25FE00B8) = 0x00000013;


    /* When performing quick verify, verify only
     * "some" bytes in order to shorten verify time.
     *
     * 17 bytes increment is choosen in order to avoid
     * weird side effects leading to false positive
     * detection.
     */
    unsigned long verify_incr = (detect_flags & SCM_CARTRAM_FULL_VERIFY ? 1 : 17);
    unsigned long *DRAM0, *DRAM1, DRAMsize, i, ok;

    // 5. verify data before proceeding
    // cache-through DRAM0 and DRAM1
    DRAMsize = ram->size >> 3; // byte length per cart -> longword length per chip
    if(ram->size == (1*1024*1024))
    {
        /* Use 1st DRAM chip's last mirror. */
        DRAM0 = (unsigned long *)0x22580000;
    }
    else
    {
        DRAM0 = (unsigned long *)0x22400000;
    }
    DRAM1 = (unsigned long *)0x22600000;
    // write
    for(i = 0; i < DRAMsize; i+=verify_incr)
    {
        *(DRAM0 + i) = i;
        *(DRAM1 + i) = DRAMsize - 1 - i;
    }    
    // read
    ok = 1;
    for(i = 0; i < DRAMsize; i+=verify_incr)
    {
        if(*(DRAM0 + i) != i)
        {
            ok = 0;
        }    
        if(*(DRAM1 + i) != (DRAMsize - 1 - i))
        {
            ok = 0;
        }    
    }

    if(ok == 0)
    {
        //if(quiet == 0)
        //    slPrint("verifying RAM cart FAILED!", slLocate(8,7));
        ram->size = 0;
        ram->ptr  = NULL;
        ram->verify_ok = 0;
        ram->status_str = _ram_status_str[id == 0x5a ? 1 : 2];
        return 0xFE; // Error, verify failed
    }

    //if(quiet == 0)
    //    slPrint("verifying RAM cart OK!", slLocate(8,7));
    ram->status_str = _ram_status_str[id == 0x5a ? 3 : 4];
    return id;
}


void scm_cartram_detect(sc_cartram_t* ram, unsigned long detect_flags)
{
    /* Initialize output parameter. */
    memset((void*)ram, 0, sizeof(sc_cartram_t));

    /* Look for official 1MB/4MB RAM. */
    ram->id = RB_CartRAM_init(ram, detect_flags);

    /* Look for RAM on custom cartridge (TBD). */


    /* Some RAM detected ? Don't forget to clear it. */
    if(ram->ptr)
    {
        memset(ram->ptr, 0, ram->size);
    }
}




/* ------------------------------------------------ */
/* ROM code/data/whatever loader. */


/**
 * Copy ROM code data to RAM.
 * Pointer to ROM data must point to ROM code header structure.
 * Length of data to copy, compression options whatever
 * are verified from ROM code header data.
 *
 * Return loaded data length if operation success, 0 else.
**/
#include "sc_compress.h"
unsigned long sc_rom_code_load(void* rom_adr, void* ram_adr)
{
    sc_rom_code_header_t* hdr = (sc_rom_code_header_t*)rom_adr;
    unsigned long load_len = 0;

    /* Valid header ? */
    if((hdr->magic_42 != 0x42)
    || (hdr->magic_53 != 0x53))
    {
        return load_len;
    }

    /* According to type, copy or decompress data. */
    switch(hdr->type)
    {
    case(RC_TYPE_ROM_CODE):
    case(RC_TYPE_DATA_RAW):
    default:
        memcpy(ram_adr, rom_adr+sizeof(sc_rom_code_header_t), hdr->usedlen);
        load_len = hdr->usedlen;
        break;

    case(RC_TYPE_DATA_COMP):
        load_len = decomp_exec((unsigned char*)(rom_adr+sizeof(sc_rom_code_header_t)), hdr->usedlen, (unsigned char*)ram_adr);
        /* If problem during decompression, return error. */
        if(load_len != hdr->decomplen)
        {
            load_len = 0;
        }
        break;
    }

    return load_len;
}




/**
 * Exit to multiplayer.
**/
void sc_exit(void)
{
    (**(void(**)(void))0x600026C)();
    while(1){;} /* <- Needed in the case yabause & internal bios is used. */
}


/**
 * System Reset.
**/
/* Acquisition timeout value, unit is poll count. */
#define SC_SYSRES_TIMEOUT (5000*1000)
void sc_sysres(void)
{
    unsigned long cntr = 0;

    /* Waits for SF to be Reset. */
    do { cntr++; if(cntr > SC_SYSRES_TIMEOUT) return; } while(SF == 1);

    /* Writes Command Code in COMREG. */
    COMREG = 0x0D;

    /* Set SF. */
    SF = 1;

    /* Expected value in OREG31 is 0x0E. */
    if(OREG31 != 0x0E)
    {
        return;
    }

    /* Final wait. */
    while(1) {;}
}




/**
 * SatCom "log to screen" callback.
**/
void scd_logscreen_callback(Fct_scd_screen cons)
{
    sc_internal_t* sc = sc_get_internal();
    sc->dbg_logscreen = cons;
}





/* Get link name. */
char* _links_name[SC_LINKS_COUNT] =
{
    "None", 
    "ARP ", 
    "USB ", 
    "PAD2"
};
char* sc_get_linkname(unsigned char p)
{
    if(p < SC_LINKS_COUNT)
    {
        return _links_name[p];
    }

    return _links_name[0]; /*NONE*/
}

/* Get ARP type string. */
char* _arp_types[3] =
{
    "Auto ", 
    "EMS  ", 
    "Datel"
};
char* sc_get_arptype(unsigned char a)
{
    if(a < 3)
    {
        return _arp_types[a];
    }

    return _arp_types[0]; /* Auto Detect */
}




/* ------------------------------------------------ */
/* Copy/execute stub. */

/* Start address, maximum length and parameters address */
#include "copystub/copystub.h"
/* The stub code itself */
#include "copystub/copystub_inc.c"


void copystub_execute(unsigned long softres_flags, unsigned long tmp_address, unsigned long length, unsigned long exec_address)
{
    /* Copy stub to memory and call it (-> quit SatCom library).
     * Code scrap from antime's satclient.c, http://www.iki.fi/~antime/
     */
    int (*stub)(unsigned long, unsigned char*, unsigned long, unsigned char*);
    stub = (int(*)(unsigned long, unsigned char*, unsigned long, unsigned char*))COPYSTUB_START_ADRESS;

    if(COPYSTUB_BIN_SIZ > COPYSTUB_LENGTH) return;

    memcpy((void*)stub, (void*)copystub_bin_dat, COPYSTUB_BIN_SIZ);

    (*stub)(softres_flags, (unsigned char*)(tmp_address & 0x0FFFFFFF), length, (unsigned char*)(exec_address & 0x0FFFFFFF));
}

void sc_execute(unsigned long softres_flags, unsigned long tmp_address, unsigned long length, unsigned long exec_address)
{
    sc_internal_t* sc = sc_get_internal();

    /* If callback available, reset VDP settings. */
    if(sc->soft_reset)
    {
        sc->soft_reset(softres_flags);
    }

    /* Set magic numbers to zero so that functions associated to SatCom debugger are disabled. */
    sc->global->scd_ptr   = 0;
    sc->global->scd_magic = 0;

    copystub_execute(softres_flags, tmp_address, length, exec_address);
}




/* ------------------------- */
/* Wait for VBlank function. */
void sc_wait_vblank(void)
{
    /* First, verify connection with PC. */
    sc_check();

    sc_wait_vblank_in();
    sc_wait_vblank_out();
}






/* ------------------------------------------------------------ */
/* SBL timer library is used in order to measure time interval. */
#include "SBL/INCLUDE/SEGA_TIM.H"

void sc_tmr_start(void)
{
// From sega_tim.h :
 // * NAME:    TIM_FRT_INIT            - FRT初期化 ("init free run timer")
 // *
 // * PARAMETERS :
 // *      (1) Uint32 mode             - <i>   分周数 (parameter1: clock divider)
// #define TIM_B_CKS0      (0)                             /* CKS0              */
//  #define TIM_CKS_8       ( 0 << TIM_B_CKS0) -> 0             /* 8 カウント        */
//  #define TIM_CKS_32      ( 1 << TIM_B_CKS0) -> 1             /* 32カウント        */
//  #define TIM_CKS_128     ( 2 << TIM_B_CKS0) -> 2             /* 128カウント       */
//  #define TIM_CKS_OUT     ( 3 << TIM_B_CKS0) -> 3             /* 外部カウント      */


    TIM_FRT_INIT(TIM_CKS_32); // TIM_CKS_8, TIM_CKS_32 or TIM_CKS_128
    TIM_FRT_SET_16(0);
}

unsigned short sc_tmr_lap(void)
{
    return (TIM_FRT_GET_16());
}
unsigned long sc_tmr_lap_usec(void)
{
    return sc_tmr_tick2usec(TIM_FRT_GET_16());

    // Float32 f = TIM_FRT_CNT_TO_MCR(TIM_FRT_GET_16());
    // f = f*10; // ???
    // return ((unsigned short)(f));
}


// Q&D : use fixed values to convert tick to usec. (-> timing may change with HW region)
//     sc_tmr_tick2usec(60000)=71444
//     sc_tmr_tick2usec(10000)=11907
//     sc_tmr_tick2usec(2^14)=19508
// Note : In order to avoid overflows, tick can't be greater than 155000.
unsigned long sc_tmr_tick2usec(unsigned long tick)
{
#if 1
    Float32 f = TIM_FRT_CNT_TO_MCR(tick);
    return ((unsigned long)(f));
#else
    unsigned long ret = tick;
    ret = ret*19508;
    ret = ret >> 14;
    return ret;
#endif
}

void sc_tick_sleep(unsigned short tick)
{
    sc_tmr_start();
    while(TIM_FRT_GET_16() < tick);
}
void sc_usleep(unsigned long usec)
{
    /* Convert delay to tick value. */
    unsigned long delay_tick;

    if(usec <= 50*1000)
    { /* Compute tick value at good precision for delay lower than 50 msec. */

        delay_tick = (usec*60000) / (sc_tmr_tick2usec(60000));

        // Changed to delay_tick = (usec*55040) / (sc_tmr_tick2usec(55040)); where sc_tmr_tick2usec(55040) = 64K
        // delay_tick = (usec*55040);
        // delay_tick = delay_tick >> 16;
    }
    else
    { /* Poor precision, but no overflow (up to 42 seconds) for higher values. */
        delay_tick = (usec*100) / (sc_tmr_tick2usec(100));

        // Changed to delay_tick = (usec*108) / (sc_tmr_tick2usec(108)); where sc_tmr_tick2usec(108) = 128
        // delay_tick = (usec*108);
        // delay_tick = delay_tick >> 7;
    }

    /* Sleep at most 60000 ticks. */
    unsigned long i;
    for(i=0; i<delay_tick; i+=60000)
    {
        unsigned long s = ((i+60000) < delay_tick ? 60000 : delay_tick - i);
        sc_tick_sleep(s);
    }
}




/* -------------------------------------------------------- */
/* Retrieve current date and time from SMPC internal clock. */


/**
 * SMPC clock acquisition test.
 * Retrieve Saturn internal clock value.
 * Note: Doesn't uses SMPC interrupt. Instead, SMPCregister is polled during acquisition.
 * 
 * For details about clock value acquisition, see information below (taken from Sega docs).
 * --- ST-169-R1-072694.pdf pages 13-16: ---
 *
 *  Table 2.4 Command Issue Types
 *    Type    Command
 *      A   MSHON, SYSRES, NMIREQ, CKCHG352, CKCHG320
 *      B   SSHON, SSHOFF, SNDON, SNDOFF, CDON, CDOFF, RESENAB, RESDISA
 *      C   SETTIME, SETSMEM
 *      D   INTBACK
 *
 *  Figure 2.4 Type D Command Flow
 *      1. START
 *      2. Waits for SF to be Reset
 *      3. Sets the Command Parameter in IREG
 *      4. Writes Command Code in COMREG
 *      5. Set SF
 *      6. END
 *
 *      1. SMPC Interrupt Processing START
 *      2. Clear Interrupt Flag
 *      3. Look at SR and Receive OREG Result Parameter
 *      4. Depending on Peripheral Status,
 *         Issue Continue or Break to SMPC
 *         (Refer to Chapter 3 for Details)
 *      5. Interrupt Processing END
 *  Note:
 *   A routine similar to the SMPC interrupt routine can be executed by masking the
 *   SMPC routine (SMPC interrupt is not used) and conducting polling after the SMPC
 *   interrupt flag or SF clear is conducted.
**/
/* Acquisition timeout value, unit is poll count. */
#define SC_RTC_TIMEOUT (5000*1000)
/* macro BCD_INT : convert bcd to int */
#define BCD_INT(bcd) (((bcd & 0xf0) >> 4) * 10 + (bcd &0x0f))



struct tm* sc_tim_smpcpoll(void)
{
    sc_internal_t* sc = sc_get_internal();
    unsigned long cntr = 0;
    unsigned char oreg_tmp[7];
    int tmp;
    struct tm* t;

    t = &(sc->set.time);

    /* First, verify if should reset last date in buffer. */
    if((t->tm_sec  > 61)
    || (t->tm_min  > 59)
    || (t->tm_hour > 23)
    || (t->tm_mday == 0)
    || (t->tm_mday > 31)
    || (t->tm_mon  > 11)
    || (t->tm_wday >  6))
    {
        memset((void*)t, 0, sizeof(struct tm));

        /* Set some dummy time in the case SMPC use is forbidden, 
         * or in the case acquisition failed.
         * "Dummy time" is set as follow :
         *  "The value in question, 0x008246A0, corresponds to 
         *  1996/3/26 0:00, which pretty much coincides with
         *  the passing of Comet Hyakutake. As far as easter
         *  eggs go, that'd be my guess."
         */
        /* The number of years since 1900. */
        t->tm_year  = 1996-1900;
        t->tm_mon   = 3-1;
        t->tm_mday  = 26;
        t->tm_hour  = 0;
        t->tm_min   = 0;
        t->tm_sec   = 0;
        t->tm_isdst = -1;
    }

    /* Can't use SMPC time. Okay, don't go further. */
    if(!sc->smpctime_use)
    {
        return t;
    }

    /* Waits for SF to be Reset. */
    do { cntr++; if(cntr > SC_RTC_TIMEOUT) return t; } while(SF == 1);

    /* Sets the Command Parameter in IREG. */
    IREG0 = 0x01;
    IREG1 = 0x08;
    IREG2 = 0xF0;

    /* Writes Command Code in COMREG. */
    COMREG = 0x10;

    /* Set SF. */
    SF = 1;

    /* Wait SF Clear. */
    do { cntr++; if(cntr > SC_RTC_TIMEOUT) return t; } while(SF == 1);

    /* Get register values from SMPC. */
    oreg_tmp[0] = OREG1;
    oreg_tmp[1] = OREG2;
    oreg_tmp[2] = OREG3;
    oreg_tmp[3] = OREG4;
    oreg_tmp[4] = OREG5;
    oreg_tmp[5] = OREG6;
    oreg_tmp[6] = OREG7;

    /* The number of years since 1900. */
    tmp = (BCD_INT(oreg_tmp[0]) * 100 + BCD_INT(oreg_tmp[1])) - 1900;
    t->tm_year = tmp;

    /* The number of months since January, in the range 0 to 11. */
    tmp = (oreg_tmp[2] & 0x0F) - 1;
    t->tm_mon  = tmp;

    /* The day of the month, in the range 1 to 31. */
    tmp = BCD_INT(oreg_tmp[3]);
    t->tm_mday = tmp;

    /* The number of hours past midnight, in the range 0 to 23. */
    tmp = BCD_INT(oreg_tmp[4]);
    t->tm_hour = tmp;

    /* The number of minutes after the hour, in the range 0 to 59. */
    tmp = BCD_INT(oreg_tmp[5]);
    t->tm_min  = tmp;

    /* The number of seconds after the minute, normally in the range 0 to 59, but can be up to 60 to allow for leap seconds. */
    tmp = BCD_INT(oreg_tmp[6]);
    t->tm_sec  = tmp;

    /* No information about daylight saving. */
    t->tm_isdst = -1;

    /* tm_wday and tm_tm_yday fields will be set by mktime function. */

    return t;
}


void sc_bup_conv_date(struct tm* t, BupDate* sat_date)
{
//typedef struct BupDate {
//	Uint8	year;		/* Year - 1980 */
//	Uint8	month;		/* Month (1~12) */
//	Uint8	day;		/* Day (1~31) */
//	Uint8	time;		/* Hour (0~23) */
//	Uint8	min;		/* Minute (0~59) */
//	Uint8	week;		/* Weekday: (Sunday:0 - Saturday:6) */
//}
    sat_date->year  = t->tm_year - 80;
    sat_date->month = t->tm_mon + 1;
    sat_date->day   = t->tm_mday;
    sat_date->time  = t->tm_hour;
    sat_date->min   = (t->tm_hour >= 60 ? 59 : t->tm_min);
    sat_date->week  = t->tm_wday;
}




void sc_initcfg_reset(sc_initcfg_t* cfg, unsigned long* workram)
{
    /* Clear SatCom work RAM. */
    memset((void*)workram, 0, SC_WORKRAM_U32SIZE*sizeof(unsigned long));

    /* Reset initialization settings. */
    memset((void*)cfg, 0, sizeof(sc_initcfg_t));
    cfg->workram   = workram;
    cfg->flags     = SC_INITFLAG_DEFAULT;
}

/* Default file name when logging to SD card. */
char* _logfile_default = "satcom.log";

void sc_init_cfg(sc_initcfg_t* cfg)
{
    sc_internal_t* sc;
    sdcard_t* sd;

    /* First, test structures size. */
    if((sizeof(sc_internal_t) != SC_INTERNAL_SIZE)
    || (sizeof(scd_data_t)    != SC_DEBUGGER_SIZE))
    {
        while(1) {;}
    }

    /* Set pointer to internal data. */
    _sc = (sc_internal_t*)(cfg->workram);
    sc = sc_get_internal();
    memset((void*)sc, 0, /*sizeof(sc_internal_t)*/SC_WORKRAM_U32SIZE*sizeof(unsigned long));

    /* Set pointer to LRAM global data. */
    sc->global = (sc_global_t*)SC_GLOBAL_DATA_ADR;

    /* Set pointer to debugger data. */
    sc->dbg = (scd_data_t*)(((unsigned char*)sc) + sizeof(sc_internal_t));
    /* Small tweak in order to get debug data start address 4bytes alignment. */
    if((((unsigned long)sc->dbg) & 0x3) != 0)
    {
        sc->dbg = (scd_data_t*)((((unsigned long)sc->dbg) + 4) & ~0x3);
    }

    /* Init debugger stuff. */
    memset((void*)sc->dbg, 0, sizeof(scd_data_t));
    sc->dbg_logscreen = NULL;
    sc->logfile_name  = (cfg->logfile_name ? cfg->logfile_name : _logfile_default);

    /* Set debugger structure size and version */
    sc->dbg->version[0] = SCD_VERSION_MAJOR;
    sc->dbg->version[1] = SCD_VERSION_MINOR;
    sc->dbg->structsz   = sizeof(scd_data_t);
    /* Init circular buffer-related stuff. */
    scd_cbinit(sc->dbg);

    /* Output location informations. */
    sc->dbg->wh_on_log = 0;
    sc->dbg->dbg_logflush = 0;

    /* Set soft reset callback. */
    sc->soft_reset = cfg->soft_reset;

    /* If needed, set debug callback. */
    if(cfg->logscreen_callback)
    {
        scd_logscreen_callback(cfg->logscreen_callback);
    }

    /* Set debugger absolute data. */
    sc->global->scd_magic = SCD_ABS_MAGIC;
    sc->global->scd_ptr   = (unsigned long)(sc->dbg);

    /* Init pointer to 2/4 read/write buffer. */
    sc->rw4_buffer = (unsigned char*)(&sc->rw4_buffer_buff);


    /* Set buffer where to upload saturn executables. */
#ifdef SC_EXEC_IN_LRAM
    /* Execute satcom in LRAM -> use HRAM as a temporary buffer. */
    sc->exec_buffer = (unsigned char*)0x06004000;
#else
    /* Execute satcom in HRAM -> use LRAM as a temporary buffer. */
    sc->exec_buffer = (unsigned char*)0x00200000;
#endif

    /* Indicate if we can use CDROM or not. */
    sc->cdrom_status = (cfg->flags & SC_INITFLAG_NOCDROM ? 0 : SC_CDSTATUS_USE);

    /* Indicate if we can retrieve time from SMPC or not. */
    sc->smpctime_use = (cfg->flags & SC_INITFLAG_NOSMPCTIME ? 0 : 1);

    /* Indicate if we need to delete log file (SD card)
     * on first log output or not.
     */
    sc->already_log_sd = (cfg->flags & SC_INITFLAG_KEEPLOG ? 1 : 0);

    /* Check if custom yabause is used or not. */
    unsigned char ver_major = REG_CART_CPLD_VER >> 4;
    if((ver_major != 0x0) && (ver_major != 0xF))
    {
        sc->yabause_use = (REG_CART_BETA_ID & 0x80 ? 1 : 0);
    }
    else
    {
        sc->yabause_use = 0;
    }

    /* Set SatCom init flag to true */
    sc->init_ok = 1;

    /* Look for extension device. */
    sc->ext_dev = SC_EXT_NONE;

    if(memcmp((void*)(0x02000000), (void*)"SEGA SEGASATURN ", 16) == 0)
    { /* Boot cartridge (Action Replay or equivalent) detected. */
        sc->ext_dev = SC_EXT_BOOTCART;
    }

    if(sc_usb_is_port_available())
    { /* USB dev cart detected. */
        sc->ext_dev = SC_EXT_USBDC;
    }

    if((REG_CPLD_55 == 0x55)
    && (REG_CPLD_AA == 0xAA)
    && ((REG_CART_CPLD_VER & 0xF0) == 0x10))
    {
        /* SatCart v1 detected. */
        sc->ext_dev = SC_EXT_SATCART;
    }
    else
    {
        /* Todo : Add Wasca detection stuff here. */
    }

    /* Init SD card library. */
    sdc_fat_reset();
    sdc_fat_init();

    /* Set "log to SD card" flag. */
    sd = sc_get_sdcard_buff();
    sd->log_sdcard = sc->set.log_sdcard;

    scd_logout(" sd->init_status=%d", sd->init_status);
    scd_logout(" hw_ver=0x%02X, yabause_use=%d", REG_CART_CPLD_VER, sc->yabause_use);

    /* Get link type from backup memory to SatCom internal buffer. */
    sc_settings_read(NULL);

    /* Init rand seed, in a Q&D way, but better than nothing.
     * Note : calling sc_tim_smpcpoll function is MANDATORY here.
     * The reason is that calling this function will initialize
     * time field in settings, and this field will be used by vmem
     * (or any other module where SMPC access is forbidden) when
     * creating files on SD card.
     */
    struct tm* t = sc_tim_smpcpoll();
    unsigned long rand_seed = mktime(t);
    srand(rand_seed);
    scd_logout("* date: %4d/%02d/%02d %02d:%02d:%02d (0x%08X)", 
        1900+t->tm_year, 1+t->tm_mon, t->tm_mday, 
        t->tm_hour, t->tm_min, t->tm_sec, 
        rand_seed);
    scd_logout("* size:sdcard=%d", sizeof(sdcard_t));
    scd_logout("* size:dbgr=%d, intrnl=%d(free=%d), total=%d[%d]", 
        sizeof(scd_data_t), 
        sizeof(sc_internal_t), 
        sizeof(sc->reserved9), 
        sizeof(scd_data_t)+sizeof(sc_internal_t), 
        SC_WORKRAM_SIZE);
    scd_logout("* sc->global=0x%08X, dbg=0x%08X", sc->global, sc->dbg);

#ifdef SC_EXEC_IN_LRAM
    scd_logout("* Welcome to SatCom debugger (LRAM)", 0);
#else
    scd_logout("* Welcome to SatCom debugger", 0);
#endif
    scd_logout("* Build: " __DATE__ ", " __TIME__, 0);
    scd_logout(" --- ~ ~ ~ ~ --- ", 0);
}


void sc_init(unsigned long* workram)
{
    sc_initcfg_t cfg;

    /* Init SatCom with default settings. */
    sc_initcfg_reset(&cfg, workram);
    sc_init_cfg(&cfg);
}

unsigned char sc_extdev_getid(void)
{
    sc_internal_t* sc = sc_get_internal();

    return sc->ext_dev;
}

const char* _ext_dev_names[SC_EXTDEV_CNT] = 
{
    "Not Found   ", 
    "Boot Cart   ", 
    "USB dev cart", 
    "MemCart     ", 
    "Wasca       ", 
    "Pad2 USB    ", 
};
const char* sc_extdev_getname(void)
{
    sc_internal_t* sc = sc_get_internal();

    return _ext_dev_names[sc->ext_dev];
}

/**
 * PAR Registers addresses definition.
**/
// http://segaxtreme.net/community/topic/14669-usb-downloading-device-for-sega-saturn/page__st__40
//The flash ID has some tag of correlation to cart type (Datel usually uses SST chips, EMS usually
// uses Atmel chips; I have no idea about other vendors such as Xinga), but I don't think it's a guarantee.
//my EMS cart got chips of vendorid 0xBFBF.
//
// Comms status flag (read-only)
// ===
// Datel: 0x22500001
// EMS: 0x22100001
#define PAR_DATEL_FLAG         0x22500001
#define PAR_EMS_FLAG           0x22100001

// Comms byte read (read-only)
// ===
// Datel: 0x22600001
// EMS: 0x22180001
#define PAR_DATEL_READ         0x22600001
#define PAR_EMS_READ           0x22180001

// Comms byte write (write-only)
// ===
// Datel: 0x22400001
// EMS: 0x22080001
#define PAR_DATEL_WRITE        0x22400001
#define PAR_EMS_WRITE          0x22080001



/* Internal function that sets the Read/Write registers addresses. */
void sc_initlink(void)
{
    sc_internal_t* sc = sc_get_internal();

    unsigned char l   = sc->set.link_type;
    unsigned char adr = sc->set.arp_type;

    /* Dirty hack when yabause is used : don't use any link. */
    if(sc->yabause_use)
    {
        l = SC_LINK_NONE;
    }

    /*** Assign ARP address pattern. ***/
    if(l == SC_LINK_LEGACY)
    {
        if(adr == SC_ARP_DATEL)
        { /* Force address to Datel. */
            sc->address[0] = PAR_DATEL_FLAG;
            sc->address[1] = PAR_DATEL_READ;
            sc->address[2] = PAR_DATEL_WRITE;
        }
        else //if(adr == SC_ARP_EMS)
        { /* Force address to EMS. */
            sc->address[0] = PAR_EMS_FLAG;
            sc->address[1] = PAR_EMS_READ;
            sc->address[2] = PAR_EMS_WRITE;
        }
    }
}

/**
 * Get/set link type data from/to backup memory.
**/
void sc_settings_read(sc_settings_t* dat_out)
{
    sc_internal_t* sc = sc_get_internal();
    unsigned long ret;

    /* Read settings from SD card. */
    memset((void*)(&(sc->set)), 0, sizeof(sc_settings_t));
    ret = scf_read(SC_FILE_SDCARD, SC_SETTINGS_NAME/*filename*/, 0/*offset*/, sizeof(sc_settings_t)/*size*/, (void*)(&(sc->set)));

    if((ret != sizeof(sc_settings_t))
    || (sc->set.struct_version != SC_SET_VER1))
    {
        /* If couldn't read settings, delete invalid file and use parameters default values. */
        scf_delete(SC_FILE_SDCARD, SC_SETTINGS_NAME/*filename*/);

        memset((void*)(&(sc->set)), 0, sizeof(sc_settings_t));
        sc->set.struct_size    = sizeof(sc_settings_t);
        sc->set.struct_version = SC_SET_VER1;
        sc->set.link_type = sc_usb_is_port_available() ? SC_LINK_USBDC : SC_LINK_LEGACY;
        sc->set.arp_type = SC_ARP_EMS;
    }

    /* Dirty hack when yabause is used : don't use any link. */
    if(sc->yabause_use)
    {
        sc->set.link_type = SC_LINK_NONE;
    }

    /* As the link type may have changed, we need to re-init it. */
    sc_initlink();

    /* Copy the resulting data to the output buffer. */
    if(dat_out != NULL)
    {
        memcpy((void*)dat_out, (void*)(&(sc->set)), sizeof(sc_settings_t));
    }
}

void sc_settings_write(sc_settings_t* dat_in)
{
    sc_internal_t* sc = sc_get_internal();
    sdcard_t* sd = sc_get_sdcard_buff();

    if(dat_in == NULL) return;

    /* Update satcom library internal settings. */
    memcpy((void*)(&(sc->set)), (void*)dat_in, sizeof(sc_settings_t));

    /* Indicate SD card library if we save logs
     * to SD card or not.
     * This is needed in order to avoid SD card library
     * outputing logs that will be saved to SD card, 
     * that will output more logs, etc.
     */
    sd->log_sdcard = sc->set.log_sdcard;

    /* Init link related internal stuff. */
    sc_initlink();

    /* Save settings to SD card. */
    scf_write((void*)(&(sc->set)), sizeof(sc_settings_t)/*size*/, SC_FILE_SDCARD, SC_SETTINGS_NAME/*filename*/);
}

void sc_settings_clear(void)
{
    scf_delete(SC_FILE_SDCARD, SC_SETTINGS_NAME/*filename*/);
    sc_settings_read(NULL);
}

sc_settings_t* sc_settings_getptr(void)
{
    sc_internal_t* sc = sc_get_internal();
    return &(sc->set);
}



#ifndef SC_RELEASE

void sc_check_usbdc(void);
void sc_check_legacy(void);
unsigned char comms_exc_byte(unsigned char value);

void _sc_internal_check(void)
{
    sc_internal_t* sc = sc_get_internal();

    unsigned char l = sc->set.link_type;

    if(l == SC_LINK_LEGACY)
    {
        sc_check_legacy();
    }
    else if(l == SC_LINK_USBDC)
    {
        sc_check_usbdc();
    }
}


/*
 * "USB dev pad" related informations.
 *
 * Purposes :
 *  - From PC, same API as USB dev cart.
 *  - USB-less cartridge(s) debug.
 *  - ROM/Flash/Backup Memory cartridges dump.
 *
 *
 * First, some informations from sattech.txt :
 *
 * The Saturn has two 7-bit I/O ports. Here's a diagram of the port itself and
 * a list of pin assignments. The names on the left are the Sega Genesis style
 * naming conventions.
 * 
 * Left            Right
 *  .-----------------.
 * | 1 2 3 4 5 6 7 8 9 |
 * +-=-=-=-=-=-=-=-=-=-+
 * 
 * Pin 1 - +5v
 * Pin 2 - D2         (Left)
 * Pin 3 - D3         (Right)
 * Pin 4 - D4         (TL)
 * Pin 5 - D5         (TR)
 * Pin 6 - D6         (TH)
 * Pin 7 - D0         (Up)
 * Pin 8 - D1         (Down)
 * Pin 9 - Ground
 * 
 * The SMPC uses the following registers for I/O port control:
 * 
 * 0x20100075 - PDR1  I/O port #1 pin output level (1=high, 0=low)
 * 0x20100077 - PDR2  I/O port #2 pin output level (1=high, 0=low)
 * 0x20100079 - DDR1  I/O port #1 pin direction (1=output, 0=input)
 * 0x2010007B - DDR2  I/O port #2 pin direction (1=output, 0=input)
 * 
 * For each register, bits 6-0 correspond to TH,TR,TL,D3,D2,D1,D0. Bit 7 is
 * unused and will latch whatever value was written to it.
 * 
 * Pins defined as an output through DDRx will return the value you set in
 * PDRx. Pins defined as an input return whatever value was sent to that pin
 * by the peripheral in use.
 * 
 * For an empty I/O port, each bit returns 1. (so $7F would be read from PDRx
 * assuming the software hadn't set bit 7 beforehand)
 * 
 * 0x2010007D - IOSEL
 * 
 * D1 - I/O port #2
 * D0 - I/O port #1
 * 
 * The lower two bits direct the SMPC to poll or ignore the corresponding
 * I/O port. (0= poll, 1= ignore)
 * 
 * This doesn't stop you from doing direct I/O anyway, but is used to prevent
 * a conflict since the SMPC will attempt to poll it at the same time.
 * 
 * Unused bits latch whatever value was last written.
 * 
 * DDR1   = 0x60      Port 1 - pins TH,TR as outputs, TL,D3-D0 are inputs
 * DDR2   = 0x60      Port 2 - pins TH,TR as outputs, TL,D3-D0 are inputs
 * IOSEL  = 0x03      Use direct I/O mode instead of SMPC polling
 *
 * You can then get pad data by writing a 2-bit value to the TH,TR pins and
 * reading the lower four bits of either input port. The values read back
 * are the following:
 *
 * When PDRx = 0x60
 *
 * D4 : Always '1'
 * D3 : Left shoulder button (0=pressed, 1=released)
 * D2 : Always '1'
 * D1 : Always '0'
 * D0 : Always '0'
 *
 * When PDRx = 0x40
 *
 * D4 : Always '1'
 * D3 : Start button (0=pressed, 1=released)
 * D2 : Button A (0=pressed, 1=released)
 * D1 : Button C (0=pressed, 1=released)
 * D0 : Button B (0=pressed, 1=released)
 *
 * When PDRx = 0x20
 *
 * D4 : Always '1'
 * D3 : Right direction (0=pressed, 1=released)
 * D2 : Left direction (0=pressed, 1=released)
 * D1 : Down direction (0=pressed, 1=released)
 * D0 : Up direction (0=pressed, 1=released)
 *
 * When PDRx = 0x00
 *
 * D4 : Always '1'
 * D3 : Right shoulder button (0=pressed, 1=released)
 * D2 : Button X (0=pressed, 1=released)
 * D1 : Button Y (0=pressed, 1=released)
 * D0 : Button Z (0=pressed, 1=released)
 *
 * A small delay is needed between changing the state of PDR1 and reading
 * the new button data. The pad I have seems to need no delay, but perhaps
 * 3rd party controllers using slower logic may need more time before they
 * return new data after switching the input source.
 *
 *
 * "USB dev pad" pinout :
 *
 * ------------------------------------------
 * PAD2 PINOUT:          NORMAL    DEV PAD   |  NAME
 *  Pin 1 - +5v                              |
 *  Pin 2 - D2   (Left)  Sat<-Pad            |  -
 *  Pin 3 - D3   (Right) Sat<-Pad            |  RDAT  (H: no data available, L: data from PC)
 *  Pin 4 - D4   (TL)    Sat<-Pad            |  PWREN (H: USB not connected, L: USB connected)
 *  Pin 5 - D5   (TR)    Sat->Pad            |  CLK   (Inverted each time a bit is sent/received)
 *  Pin 6 - D6   (TH)    Sat->Pad            |  DATS
 *  Pin 7 - D0   (Up)    Sat<-Pad            |  DATR0
 *  Pin 8 - D1   (Down)  Sat<-Pad            |  DATR1
 *  Pin 9 - Ground
 *
 * ------------------------------------------
 * SMPC (glue logic between pad2 <-> FTDI) specs:
 *
 * SMPC is powered by USB bus, hence can't be disabled by plugging/unplugging USB connector.
 * Should add a "disable" switch in order to make pad2 connector behaving as "empty I/O port" ?
 * Or should try to power SMPC from Saturn pad connector ?
 * // - When USB is not connected (FTDI's PWREN='H'),  :
 * //    - D0-D3 always return 'H'.
 * //    - Input from TR/TH lines is ignored.
 * // - When USB is connected (FTDI's PWREN='L'), pad2 connector behaves as "USB dev pad" :
 * //    - Data is sent/received via SNDx/RCVx lines
**/



/* USB_FLAGS, USB_FIFO etc definitions, 
 * for USB dev cart.
 */
#include "satcart.h"


int sc_usb_is_port_available(void)
{
    unsigned char tmp = USB_FLAGS;

    /* USB flag's bit 6-2 are always low, and other 
     * cartridges (verified on Action Replay)
     * return FFh when reading this address, so the following
     * stuff below should detect if USB port is present or not.
     */
    if((tmp & 0x7C) == 0)
    {
        return 1;
    }

    return 0;
}
int sc_usb_is_connected(void)
{
    unsigned char tmp = USB_FLAGS;

    /* Verify bit 7 (PWREN#) which is low when
     * FTDI chip is USB powered.
     */
    if((tmp & 0xFC) == 0)
    {
        return 1;
    }

    return 0;
}

static unsigned char RecvByte(void)
{
    while ((USB_FLAGS & USB_RXF) != 0) ;
    return USB_FIFO;
}

static void SendByte(unsigned char byte)
{
    while ((USB_FLAGS & USB_TXE) != 0) ;
    USB_FIFO = byte;
}


static unsigned long RecvDword(void)
{
    unsigned long tmp = RecvByte();
    tmp = (tmp << 8) | RecvByte();
    tmp = (tmp << 8) | RecvByte();
    tmp = (tmp << 8) | RecvByte();

    return tmp;
}

static void SendDword(unsigned long data)
{
    SendByte((data >> 24) & 0xFF);
    SendByte((data >> 16) & 0xFF);
    SendByte((data >>  8) & 0xFF);
    SendByte((data >>  0) & 0xFF);
}


const unsigned char _usbdc_version_dat[16] =
{
    0x31, /* version */
    0x00, 0x00, 0x00,       /* reserved */
    0x00, 0x00, 0x00, 0x00, /* reserved */
    0x00, 0x00, 0x00, 0x00, /* reserved */
    0x00, 0x00, 0x00, 0x00  /* reserved */
};


static void DoDownload(void)
{
    sc_internal_t* sc = sc_get_internal();
    unsigned char *pData;
    unsigned long len, ii;
    unsigned char checksum = crc_usbdc_init();

    pData = (unsigned char*)RecvDword();
    len = RecvDword();
    UDC_LOGOUT(" ptr=0x%08X len=0x%08X", pData, len);

    if((len == 2) || (len == 4))
    { /* 1,2,4 bytes length data access. */
        if(len == 2)
        {
            unsigned short* ptr_in2  = (unsigned short*)(((unsigned long)pData) & 0xFFFFFFFE);
            unsigned short* ptr_out2 = (unsigned short*)sc->rw4_buffer;
            *ptr_out2 = *ptr_in2;
        }
        else if(len == 4)
        {
            unsigned long* ptr_in4  = (unsigned long*)(((unsigned long)pData) & 0xFFFFFFFC);
            unsigned long* ptr_out4 = (unsigned long*)sc->rw4_buffer;
            *ptr_out4 = *ptr_in4;
        }
        pData = (unsigned char*)sc->rw4_buffer;
    }

    /* USB dev cart firmware version check ? */
    if(((((unsigned long)pData) & 0x0FFFFFFF) == 0x007FFFF0)
    && (len == 16))
    {
        pData = (unsigned char*)_usbdc_version_dat;
    }


    for (ii = 0; ii < len; ++ii)
    {
        while ((USB_FLAGS & USB_TXE) != 0) ;
        USB_FIFO = pData[ii];
    }

    checksum = crc_usbdc_update(checksum, pData, len);
    checksum = crc_usbdc_finalize(checksum);

    SendByte(checksum);
}



static void DoUpload(void)
{
    sc_internal_t* sc = sc_get_internal();
    unsigned char *pData;
    unsigned long len, ii;
    unsigned char readchecksum;
    unsigned char checksum = crc_usbdc_init();

    pData = (unsigned char*)RecvDword();
    len = RecvDword();
    UDC_LOGOUT(" ptr=0x%08X len=0x%08X", pData, len);

    if((len == 2) || (len == 4))
    { /* 1,2,4 bytes length data access. */
        unsigned char *pBuffer = (unsigned char*)sc->rw4_buffer;

        /* Receive data to write. */
        for (ii = 0; ii < len; ++ii)
        {
            // inlining is 20K/s faster
            while ((USB_FLAGS & USB_RXF) != 0) ;
            pBuffer[ii] = USB_FIFO;
        }
        checksum = crc_usbdc_update(checksum, pBuffer, len);
        checksum = crc_usbdc_finalize(checksum);

        if(len == 2)
        {
            unsigned short* ptr_in2  = (unsigned short*)pBuffer;
            unsigned short* ptr_out2 = (unsigned short*)(((unsigned long)pData) & 0xFFFFFFFE);
            *ptr_out2 = *ptr_in2;
        }
        else if(len == 4)
        {
            unsigned long* ptr_in4  = (unsigned long*)pBuffer;
            unsigned long* ptr_out4 = (unsigned long*)(((unsigned long)pData) & 0xFFFFFFFC);
            *ptr_out4 = *ptr_in4;
        }
    }
    else
    {
        for (ii = 0; ii < len; ++ii)
        {
            // inlining is 20K/s faster
            while ((USB_FLAGS & USB_RXF) != 0) ;
            pData[ii] = USB_FIFO;
        }
        checksum = crc_usbdc_update(checksum, pData, len);
        checksum = crc_usbdc_finalize(checksum);
    }

    readchecksum = RecvByte();
    if (checksum != readchecksum)
    {
        SendByte(0x1);
        UDC_LOGOUT(" Checksum error !", 0);
    }
    else
    {
        SendByte(0);
    }
}

static void DoExecute(void)
{
    /* Read address, execute call. */
#ifdef UDC_DEBUG
#else
    sc_internal_t* sc = sc_get_internal();
#endif
    void (*pFun)(void);

    pFun = (void(*)(void))RecvDword();
    UDC_LOGOUT(" ptr=0x%08X", pFun);

#ifdef UDC_DEBUG
    while(1){;}
#else
    if(sc->soft_reset) {sc->soft_reset(SC_SOFTRES_ALL);}

    (*pFun)();
#endif
}

static void DoExecExt(void)
{
    sc_internal_t* sc = sc_get_internal();
    unsigned char *pData;
    unsigned char *pExec;
    unsigned long len, ii;
    unsigned long softres_flags;
    unsigned char readchecksum;
    unsigned char checksum = crc_usbdc_init();

    /*
     * PC sends execute address (4 bytes)
     * PC sends data length     (4 bytes)
     * PC sends reset flags     (4 bytes)
     * PC sends data            (variable length)
     * PC sends CRC             (1 byte)
     * Saturn sends CRC check   (1 byte, 0: CRC match, else: CRC mismatch)
     */

    pExec = (unsigned char*)RecvDword();

    /* Verify if can receive data directly to execution address or not. */
    if((((unsigned long)(pExec)) & 0x0F000000) != ((unsigned long)(sc->exec_buffer) & 0x0F000000))
    {
        pData = sc->exec_buffer;
    }
    else
    {
        pData = pExec;
    }
    UDC_LOGOUT(" ptr=0x%08X 0x%08X", pExec, pData);

    len = RecvDword();

    softres_flags = RecvDword();
    UDC_LOGOUT(" len=0x%08X, flags=0x%08X", len, softres_flags);

    for (ii = 0; ii < len; ++ii)
    {
        // inlining is 20K/s faster
        while ((USB_FLAGS & USB_RXF) != 0) ;
        pData[ii] = USB_FIFO;
    }
    checksum = crc_usbdc_update(checksum, pData, len);
    checksum = crc_usbdc_finalize(checksum);

    readchecksum = RecvByte();
    if (checksum != readchecksum)
    {
        SendByte(0x1);
        UDC_LOGOUT(" Checksum error !", 0);
    }
    else
    { /* CRC matched, can execute program. */
        SendByte(0);

#ifdef UDC_DEBUG
    while(1){;}
#else
        /* If callback available, perform soft reset. */
        if(sc->soft_reset) {sc->soft_reset(softres_flags);}

        sc_execute(softres_flags, (unsigned long)pData, len, (unsigned long)pExec);
#endif
    }
#ifdef UDC_DEBUG
    while(1){;}
#endif
}



static void DoSendBufferAddress(void)
{
    sc_internal_t* sc = sc_get_internal();
    unsigned long tmp = (unsigned long)sc->exec_buffer;

    SendDword(tmp);
}

static void DoCopyExec(void)
{
    unsigned long tmp_address  = RecvDword();
    unsigned long length       = RecvDword();
    unsigned long exec_address = RecvDword();

    sc_execute(SC_SOFTRES_ALL, tmp_address, length, exec_address);
}


void sc_check_usbdc(void)
{
    int can_exit = 1;
    unsigned char command;

    do
    {
        unsigned char usb_flags = USB_FLAGS;

        if((usb_flags & USB_RXF) == 0)
        {
            command = USB_FIFO;
            UDC_LOGOUT("CMD=0x%02X", command);

            // // Configure SCU A-bus settings
            // // Disabling A-bus refresh adds ~20KB/s
            // AREF = 0;
            // // Removing wait states doubles speed
            // ASR0 = 0;

            switch(command)
            {
            case USBDC_FUNC_DOWNLOAD:
                DoDownload();
                break;

            case USBDC_FUNC_UPLOAD:
                DoUpload();
                // can_exit = 0; //TMP
                break;

            case USBDC_FUNC_EXEC:
                DoExecute();
                can_exit = 1;
                break;

            case USBDC_FUNC_GET_BUFF_ADDR:
                DoSendBufferAddress();

                /* PC will soon send data for upload & exec, so it's better
                 * to stay here until the end.
                 */
                can_exit = 0;
                break;

            case USBDC_FUNC_COPYEXEC:
                DoCopyExec();
                can_exit = 1;
                break;

            case USBDC_FUNC_EXEC_EXT:
                DoExecExt();
                can_exit = 0;
                break;

            default:
                break;
            }
        }

    } while(can_exit == 0);
}







/* Uses code from cgfm2 programs. */
unsigned char comms_exc_byte(unsigned char value)
{
    sc_internal_t* sc = sc_get_internal();
    unsigned char temp;
    volatile unsigned char *flag = (unsigned char *)sc->address[0];

    /* Wait for comms exchange flag. */
    if(sc->use_comms == 1)
    {
        while(((*flag) & 1) == 0);
    }
    sc->use_comms = 1;

    /* Receive data. */
    temp = *((unsigned char *)sc->address[1]);

    /* Send data. */
    *((unsigned char *)sc->address[2]) = value;

    return temp;
}
unsigned short comms_exc_short(unsigned short data)
{
    unsigned short rdata;
    rdata  = comms_exc_byte((data >>  8) & 0xFF) <<  8;
    rdata |= comms_exc_byte((data >>  0) & 0xFF);
    return rdata;
}
unsigned long comms_exc_long(unsigned long data)
{
    unsigned long rdata;
    rdata  = comms_exc_byte((data >> 24) & 0xFF) << 24;
    rdata |= comms_exc_byte((data >> 16) & 0xFF) << 16;
    rdata |= comms_exc_byte((data >>  8) & 0xFF) <<  8;
    rdata |= comms_exc_byte((data >>  0) & 0xFF);
    return rdata;
}


void sc_check_legacy(void)
{
    sc_internal_t* sc = sc_get_internal();
    /* ARP Communication data. */
    volatile unsigned char *flag = (unsigned char *)sc->address[0];
    unsigned long tmp;
    unsigned char command_byte;
    unsigned long address;
    unsigned long length;
    unsigned char exec_flag;
    unsigned char checksum;

    unsigned char* ptr;
    unsigned long i;

    int flag_on = 0;
    /*
     * Update state and exchange data when state becomes 1
     */
    if(*flag & 1)
    {
        flag_on = 1;
    }

    /* No data received -> exit function. */
    if(flag_on != 1) return;
    
    /* Dirty hack: don't check flag for the first
     * byte, because we polled it just before.
     */
    sc->use_comms = 1;


    /* Synchronize with PC. */
    tmp = comms_exc_byte('D');
    if (tmp != 'I')
    {
        return;
    }
    tmp = comms_exc_byte('O');
    if (tmp != 'N')
    {
        return;
    }


    /* Read the command byte from the PC. */
    command_byte = comms_exc_byte(0);

    switch(command_byte)
    {

    /** From http://cgfm2.emuviews.com/sat/satar.txt :
     * Function $01: Download memory
     * 
     * It does the following:
     * 
     * 1. Write longword in SH-2 register R9 to the PC.
     * 2. Read two longwords, address and length, from the PC.
     * 3. If length is zero, exchange bytes 'O' and 'K', then return.
     *    It does not care what the PC sends back.
     * 4. Exchange data bytes from specified memory addresses until all memory
     *    has been read. The SH-2 can hang if it reads invalid address.
     * 5. Exchange a single byte, which is a checksum.
     * 6. Go to step 2 and repeat.
     * 
     * For a single download, you'd send a length of zero and any address the
     * second time around. The checksum is generated by adding each byte to
     * an 8-bit accumulator. (so it wraps from $FF to $00)
    **/
    case(0x01):
        comms_exc_long(0);

        address = comms_exc_long(0);

        length  = comms_exc_long(0);

        while(length != 0)
        {
            if((length == 2) || (length == 4))
            { /* 1,2,4 bytes length data access. */
                if(length == 2)
                {
                    unsigned short* ptr_in2  = (unsigned short*)(address & 0xFFFFFFFE);
                    unsigned short* ptr_out2 = (unsigned short*)sc->rw4_buffer;
                    *ptr_out2 = *ptr_in2;
                }
                else if(length == 4)
                {
                    unsigned long* ptr_in4  = (unsigned long*)(address & 0xFFFFFFFC);
                    unsigned long* ptr_out4 = (unsigned long*)sc->rw4_buffer;
                    *ptr_out4 = *ptr_in4;
                }
                ptr = (unsigned char*)sc->rw4_buffer;
            }
            else
            { /* Byte-per-byte register access. */
                ptr = (unsigned char*)address;
            }

            checksum = 0;
            for(i=0; i<length; i++)
            {
                comms_exc_byte(ptr[i]);
                checksum += ptr[i];
            }
            comms_exc_byte(checksum);

            address = comms_exc_long(0);
            length  = comms_exc_long(0);
        }
        /* Finished download. */
        comms_exc_byte('O');
        comms_exc_byte('K');
        break;


    /** From http://cgfm2.emuviews.com/sat/satar.txt :
     * Function $08: Write byte to memory address
     * 
     * Reads a longword from the PC (address to write to) and exchanges zero,
     * using the value it receives to write as a byte value to the specified
     * address.
    **/
    case(0x08):
        address = comms_exc_long(0);
        tmp     = comms_exc_byte(0);
        ((unsigned char*)address)[0] = tmp;
        break;


    /** From http://cgfm2.emuviews.com/sat/satar.txt :
     * Function $09: Upload (and execute) data
     * 
     * 1. Read longword from the PC (address to load data)
     * 2. Read longword from the PC(data length)
     * 3. Exchange byte (if $01, JSR to load address after loading, else do nothing)
     * 4. Read data bytes from PC until all data was transferred
     * 
     * When transferring data, the first byte exchanged is the value of SH-2
     * register R9, and all subsequent bytes are the previous byte sent to the PC.
     * 
     * Assuming the stack isn't modified, the program called by this function
     * could end with an RTS and return to the PAR software.
    **/
    case(0x09):
        address   = comms_exc_long(0);
        length    = comms_exc_long(0);
        exec_flag = comms_exc_byte(0);
        if((length == 2) || (length == 4))
        { /* 1,2,4 bytes length data access. */

            /* Receive data to write. */
            ptr = (unsigned char*)sc->rw4_buffer;
            for(i=0; i<length; i++)
            {
                ptr[i] = comms_exc_byte((i==0 ? 0 : ptr[i-1]));
            }

            if(length == 2)
            {
                unsigned short* ptr_in2  = (unsigned short*)ptr;
                unsigned short* ptr_out2 = (unsigned short*)(address & 0xFFFFFFFE);
                *ptr_out2 = *ptr_in2;
            }
            else if(length == 4)
            {
                unsigned long* ptr_in4  = (unsigned long*)ptr;
                unsigned long* ptr_out4 = (unsigned long*)(address & 0xFFFFFFFC);
                *ptr_out4 = *ptr_in4;
            }
        }
        else
        {
            /* Receive data to write. */
            ptr = (unsigned char*)address;
            for(i=0; i<length; i++)
            {
                ptr[i] = comms_exc_byte((i==0 ? 0 : ptr[i-1]));
            }
        }

        /* Execute ? */
        if(exec_flag == 1)
        {
            sc_execute(SC_SOFTRES_ALL, address/*tmp_address*/, length/*length*/, address/*exec_address*/);
        }
        break;


    /* Unknown/unimplemented function. */
    default:
        break;
    }


} /* sc_check_legacy function. */





#endif // !SC_RELEASE








/*----------------------------------------------------------------------------*/
/*- Debugger functions. ------------------------------------------------------*/

scd_data_t* sc_get_dbg_buff(void)
{
    sc_internal_t* sc = sc_get_internal();

    return sc->dbg;
}


#ifndef SC_RELEASE
void _scd_log_append(unsigned char type, unsigned char id, char *buff, unsigned long bufflen, int usbonly)
{
    sc_internal_t* sc = sc_get_internal();

    /* Set length and type to data */
    unsigned long len = strnlen(buff+3, bufflen-3);
    if(len == 0) return;
    buff[0] = 3 + len;
    buff[1] = type;
    buff[2] = id; /* 0: console, 1~: string grid */

    /* Append the log message to the debug data circular buffer. */
    scd_cbwrite(sc->dbg, (unsigned char*)buff, 3+len);

    /* If requested by user, display log message to screen. */
    if(sc->dbg_logscreen)
    {
        sc->dbg_logscreen(buff+3);
    }

    /* If requested by user, save log message to SD card. */
    if((!usbonly) && (sc->set.log_sdcard))
    {
        /*
         * Message stored in global buff array will be overwritten
         * next time logout function will be called.
         * This happens when SD card module call logout function during
         * scf_append function call.
         * Hence, data is copied into sc->dbg->prompt_answer. This works
         * well at the condition that scd_prompt function is not called
         * from SD card library.
         */
        char* buff_sd = (char*)(sc->dbg->prompt_answer);
        memcpy(buff_sd, buff+3, MIN(SCD_PRINT_LEN-3, SCD_PROMPT_SIZE-1));

        /* Don't forget to append line break before writing to SD card. */
        buff_sd[SCD_PROMPT_SIZE-2] = '\0';
        len = strlen(buff_sd);
        buff_sd[len++] = '\r';
        buff_sd[len++] = '\n';

        /* If needed, delete previous log file, then append log message to file. */
        if(!sc->already_log_sd)
        {
            scf_delete(SC_FILE_SDCARD, sc->logfile_name); /* Not needed ? */
            sc->already_log_sd = 1;
        }
        scf_append((void*)(buff_sd), len, SC_FILE_SDCARD, sc->logfile_name/*filename*/);
    }

    /* Circular buffer auto-flush. */
    if(sc->dbg->dbg_on)
    {
        /* Update circular buffer memory usage. */
        sc->dbg->cb_memuse = scd_cbmemuse(sc->dbg);

        /* If requested by user, or if circular buffer nearly full (75%), automatically flush it to the PC. */
        if((sc->dbg->dbg_logflush)
        || (sc->dbg->cb_memuse >= 96/*75% x 128*/))
        {
            scd_logflush();
        }
    }
}
void _scd_internal_logout(const char* file, int line, const char* func, int usbonly, char *string, ...)
{
    sc_internal_t* sc = sc_get_internal();
    va_list argptr;
    char* buff = sc->dbg_print;

    /* If debug is disabled, don't do anything. */
    if(sc->dbg->dbg_disable) return;

    /* If needed, indicate from where we sent log message. */
    if(sc->dbg->wh_on_log)
    {
        _scd_internal_wh(file, line, func, usbonly);
    }

    /* Print the log message to the memory. */
    va_start(argptr, string);
    vsnprintf(buff+3, SCD_PRINT_LEN-3, string, argptr); buff[SCD_PRINT_LEN-1] = '\0';
    va_end(argptr);

    _scd_log_append(SCD_TYPE_MSG, 0, buff, SCD_PRINT_LEN, usbonly);
}

void _scd_internal_msgout(const char* file, int line, const char* func, int usbonly, unsigned char id, char *string, ...)
{
    sc_internal_t* sc = sc_get_internal();
    va_list argptr;
    char* buff = sc->dbg_print;

    /* If debug is disabled, don't do anything. */
    if(sc->dbg->dbg_disable) return;

    /* If needed, indicate from where we sent log message. */
    if(sc->dbg->wh_on_log)
    {
        _scd_internal_wh(file, line, func, usbonly);
    }

    /* Print the log message to the memory. */
    va_start(argptr, string);
    vsnprintf(buff+3, SCD_PRINT_LEN-3, string, argptr); buff[SCD_PRINT_LEN-1] = '\0';
    va_end(argptr);

    _scd_log_append(SCD_TYPE_MSG, id, buff, SCD_PRINT_LEN, usbonly);
}


char* _dummy_answer = "";
char* scd_prompt(char* prompt_str)
{
    sc_internal_t* sc = sc_get_internal();
    char* buff = sc->dbg_print;

    /* Exit when debugger is disabled, as release version would do. */
    if(sc->dbg->dbg_disable) return _dummy_answer;

    strncpy(buff+3, prompt_str, SCD_PRINT_LEN-3); buff[SCD_PRINT_LEN-1] = '\0';

    /* Send prompt message.
     * Note about usbonly parameter set to '1' :
     * Log to SD card use "sc->dbg->prompt_answer" global buffer that
     * is also used by this function.
     * Hence log to SD card is disabled in "scd_prompt" case.
     */
    _scd_log_append(SCD_TYPE_PROMPT, 0/*to console*/, buff, SCD_PRINT_LEN, 1/*usbonly*/);

    /* Wait until the PC finishes to send the prompt answer. */
    sc->dbg->proc_complete = 0;
    while(sc->dbg->proc_complete == 0)
    {
        /* Wait for PC to consume all debugger data. */
        sc_check();
    }

    /* Return the prompt answer. */
    sc->dbg->prompt_answer[SCD_PROMPT_SIZE-1] = '\0';
    return (char*)sc->dbg->prompt_answer;
}



void scd_logflush(void)
{
    sc_internal_t* sc = sc_get_internal();

    /* Don't flush logs when absolute data are invalid, 
     * in order to avoid infinite loop of Saturn application
     * waiting for signal that SatLink can't send.
     * Example : Backup Manager and vmem : scd_logflush calls
     *           from vmem are "hidden" by Backup Manager.
     */
    if((sc->global->scd_magic != SCD_ABS_MAGIC)
    || (sc->global->scd_ptr   != (unsigned long)(sc->dbg)))
    {
        return;
    }

    /* Exit when debugger is disabled, as release version would do. */
    if(sc->dbg->dbg_disable) return;

    while(sc->dbg->writeptr != sc->dbg->readptr)
    {
        /* Wait for PC to consume all debugger data. */
        sc_check();
    }
}


void _scd_internal_wh(const char* file, int line, const char* func, int usbonly)
{
    sc_internal_t* sc = sc_get_internal();
    char* buff = sc->dbg_print;

    /* If debug is disabled, don't do anything. */
    if(sc->dbg->dbg_disable) return;

    /* Print the log message to the memory. */
    snprintf(buff+3, SCD_PRINT_LEN-3, "%s::%d [%s]", file, line, func);
    buff[SCD_PRINT_LEN-1] = '\0';

    _scd_log_append(/*SCD_TYPE_MSG*/SCD_TYPE_WHEREHERE, 0/*id (nonzero for grid)*/, buff, SCD_PRINT_LEN, usbonly);
}


#endif // !SC_RELEASE



/*----------------------------------------------------------------------------*/
/*- File I/O functions --> PC stuff. -----------------------------------------*/


unsigned long _scd_file_request(
    unsigned char type, 
    unsigned long foffset, 
    unsigned long frw_address, 
    unsigned long frw_len, 
    char*         fname)
{
#ifndef SC_RELEASE
    sc_internal_t* sc = sc_get_internal();
    char* buff = sc->scd_file_request_buff;

    /* If debug is disabled, don't do anything. */
    if(sc->dbg->dbg_disable) return 0;

    /* Write the parameters. */
    unsigned char offs = 2;
    buff[offs++] = ((foffset) >> 24) & 0xFF;
    buff[offs++] = ((foffset) >> 16) & 0xFF;
    buff[offs++] = ((foffset) >>  8) & 0xFF;
    buff[offs++] = ((foffset) >>  0) & 0xFF;

    buff[offs++] = ((frw_address) >> 24) & 0xFF;
    buff[offs++] = ((frw_address) >> 16) & 0xFF;
    buff[offs++] = ((frw_address) >>  8) & 0xFF;
    buff[offs++] = ((frw_address) >>  0) & 0xFF;

    buff[offs++] = ((frw_len) >> 24) & 0xFF;
    buff[offs++] = ((frw_len) >> 16) & 0xFF;
    buff[offs++] = ((frw_len) >>  8) & 0xFF;
    buff[offs++] = ((frw_len) >>  0) & 0xFF;

    /* Print the filename to the memory. */
    snprintf(buff+offs, SCDF_BUFFSIZE-offs, fname);

    /* Set length and type to data */
    unsigned long len = offs + strnlen(buff+offs, SCDF_BUFFSIZE-offs);
    buff[0] = 2 + len;
    buff[1] = type;

    /*
     * In the case circular buffer is full, file request data
     * will not appended to it, hence PC will never set the
     * proc_complete flag, which results in infinite loop.
     *
     * So that flushing circular buffer is needed before
     * appending file request data.
     */
    scd_logflush();

    /* Append the file request to the debug data circular buffer. */
    scd_cbwrite(sc->dbg, (unsigned char*)buff, 2+len);

    /* Wait until the PC processes the file data request. */
    sc->dbg->proc_complete = 0;
    sc->dbg->fileio_size   = 0;
    do
    {
        sc_check();
    } while(sc->dbg->proc_complete == 0);

    /* Return size actually read/written/appended. */
    return(sc->dbg->fileio_size);


#else
    /* Don't do anything in release mode. */
    return 0;
#endif
}



/*----------------------------------------------------------------------------*/
/*- File I/O functions --> SD card stuff. ------------------------------------*/
sdcard_t* sc_get_sdcard_buff(void)
{
    sc_internal_t* sc = sc_get_internal();
    return &(sc->sd);
}



/*----------------------------------------------------------------------------*/
/*- File I/O functions --> CDROM stuff (scrapped from iapetus). --------------*/
//#include "cwx_cdrom/iapetus_cdrom.h"
#include "iapetus/iapetus.h"
#include "iapetus/cd/cd.h"
#include "iapetus/cd/cdfs.h"
#include "iapetus/sh2/sh2int.h"


/*----------------------------------------------------------------------------*/
/*- File I/O functions --> RomFs stuff. --------------------------------------*/
#include "sc_romfs.h"

int sc_romfs_setdata(unsigned char* romfs)
{
    sc_internal_t* sc = sc_get_internal();

    /* Update pointer to RomFs data only if header and CRC verified succesfully. */
    if(romfs_check_header(romfs, 1/*crc_check*/))
    {
        sc->romfs = romfs;

        return 1;
    }

    return 0;
}

unsigned char* sc_romfs_get_dataptr(char* filename, unsigned long* datalen)
{
    sc_internal_t* sc = sc_get_internal();
    romfs_entry_t entry;
    romfs_entry_t* e;
    unsigned char* ret;

    if(datalen)
    {
        *datalen = 0;
    }

    e = romfs_get_first_entry(sc->romfs, filename);

    if(e == NULL)
    {
        ret = NULL;
    }
    else
    {
        if(e->flags & FILEENTRY_FLAG_FOLDER)
        {
            ret = NULL;
        }
        else
        {
            memcpy((void*)&entry, (void*)e, sizeof(romfs_entry_t));
            ret = romfs_get_entry_ptr(sc->romfs, &entry, datalen);
        }
    }

    return ret;
}





/* Workram required by CDROM library. */
unsigned long _cd_workbuff[4096/sizeof(unsigned long)];
file_struct _cdrom_file;

void cdrom_init_check(void)
{
    sc_internal_t* sc = sc_get_internal();

    /* Need to init CD-ROM ? */
    if( (sc->cdrom_status & SC_CDSTATUS_USE )
    &&(!(sc->cdrom_status & SC_CDSTATUS_INIT)))
    {
        cdfs_init((void *)_cd_workbuff, sizeof(_cd_workbuff));
        /* Mark CDROM library as initialized. */
        sc->cdrom_status |= SC_CDSTATUS_INIT;
    }

}


/* CDROM file open/close functions, for SatCom library internal use. */
void cdrom_open_file(void)
{
    sc_internal_t* sc = sc_get_internal();

    cdfs_open(sc->filename_buff, &_cdrom_file);
}

void cdrom_close_file(void)
{
    cdfs_close(&_cdrom_file);
}



const char* _dev_names[SC_DEVICES_CNT] = 
{
    "CD-ROM", 
    "RomFs ", 
    "PClink", 
    "SDcard", 
};

char* scf_dev_getname(unsigned char dev_id)
{
    char* ret;

    switch(dev_id)
    {
    case(SC_FILE_CDROM):
    case(SC_FILE_ROMFS):
    case(SC_FILE_PC):
    case(SC_FILE_SDCARD):
        ret = (char*)(_dev_names[dev_id]);
        break;

    default:
        ret = (char*)(_dev_names[0]);
        break;
    }

    return ret;
}

void scf_dev_status(sc_device_t* dev, unsigned char dev_id)
{
    sc_internal_t* sc = sc_get_internal();
    sdcard_t* sd;

    /* Reset device list. */
    memset((void*)dev, 0, sizeof(sc_device_t));

    /* Set status for device with specified ID. */
    dev->dev_id = dev_id;
    dev->name   = scf_dev_getname(dev_id);

    switch(dev_id)
    {
    case(SC_FILE_CDROM):
        dev->available = (sc->cdrom_status & SC_CDSTATUS_USE ? 1 : 0);
        dev->read_only = 1;
        dev->model     = 0; /* Unused/ */
        break;

    case(SC_FILE_ROMFS):
        dev->available = romfs_check_header(sc->romfs, 0/*crc_check*/);
        dev->read_only = 1;
        dev->model     = 0; /* Unused/ */
        break;

    case(SC_FILE_PC):
        dev->available = (sc->dbg->dbg_on ? 1 : 0);
        dev->read_only = 0;
        dev->model     = 0; /* Unused/ */
        break;

    case(SC_FILE_SDCARD):
        /* Note : possible SD card models are :
         *  SC_EXT_NONE    (no SD card detected)
         *  SC_EXT_SATCART
         *  SC_EXT_WASCA  
         * (Defined in sc_saturn.h file)
         */
        dev->model = sc_extdev_getid();
        sd = sc_get_sdcard_buff();
        dev->available = (dev->model != SC_EXT_NONE ? (sd->init_ok ? 1 : 0) : 0);
        dev->read_only = 0;
        break;

    default:
        /* Unknown device. */
        dev->available = 0;
        dev->read_only = 0;
        dev->model     = 0; /* Unused/ */
        break;
    }
}


int scf_dev_list(sc_device_t* devlist)
{
    unsigned char i, j;

    /* If needed, enable PC link. */
    sc_check();

    j=0;
    for(i=0; i<SC_DEVICES_CNT; i++)
    {
        /* Set devices properties. */
        scf_dev_status(devlist+j, i/*dev_id*/);

        /* If device is available, update count. */
        if(devlist[j].available)
        {
            j++;
        }
    }

    return j;
}


void scf_filename_bufferize(char* filename)
{
    sc_internal_t* sc = sc_get_internal();
    int i;

    memset((void*)(sc->filename_buff), '\0', SC_PATHNAME_MAXLEN);

    for(i=0; i<SC_PATHNAME_MAXLEN; i++)
    {
        if(filename[i] == '\\')
        {
            /* Convert backslashes to slashes. */
            sc->filename_buff[i] = '/';
        }
        else
        {
            sc->filename_buff[i] = filename[i];
        }
        if(filename[i] == '\0')
        {
            break;
        }
    }

    sc->filename_buff[SC_PATHNAME_MAXLEN-1] = '\0';
}


unsigned long scf_size(unsigned char dev_id, char* filename)
{
    sc_internal_t* sc = sc_get_internal();

    unsigned long size = 0;

    romfs_entry_t romfs_entry;
    romfs_entry_t* e;

    scf_filename_bufferize(filename);
#ifdef SC_RELEASE
    /* In release mode, always use CD-ROM instead of PC. */
    if(dev_id == SC_FILE_PC)
    {
        dev_id = SC_FILE_CDROM;
    }
#endif

    if(dev_id == SC_FILE_PC)
    { /* Use file from PC. */
        _scd_file_request(
            /*type       */SCD_TYPE_SFILE, 
            /*foffset    */0, 
            /*frw_address*/(unsigned long)(&size), 
            /*frw_len    */sizeof(unsigned long), 
            /*fname      */sc->filename_buff);
    }
    else if(dev_id == SC_FILE_ROMFS)
    { /* Use file from RomFs. */
        e = romfs_get_first_entry(sc->romfs, sc->filename_buff);
        if(e == NULL)
        {
            size = 0;
        }
        else
        {
            if(e->flags & FILEENTRY_FLAG_FOLDER)
            {
                size = 0;
            }
            else
            {
                memcpy((void*)&romfs_entry, (void*)e, sizeof(romfs_entry_t));
                size = romfs_get_entry_size(sc->romfs, &romfs_entry);
            }
        }
    }
    else if(dev_id == SC_FILE_CDROM)
    { /* Use file from CDROM. */
        if(sc->cdrom_status & SC_CDSTATUS_USE)
        {
            cdrom_init_check();

            cdrom_open_file();
            size = _cdrom_file.size;
            cdrom_close_file();
        }
    }
    else if(dev_id == SC_FILE_SDCARD)
    { /* Use file from SD card. */
        sdc_fat_file_size(sc->filename_buff, &size);
    }

    return size;
}



unsigned long scf_read(unsigned char dev_id, char* filename, unsigned long offset, unsigned long size, void* ptr)
{
    sc_internal_t* sc = sc_get_internal();

    unsigned long retsize = 0;

    romfs_entry_t romfs_entry;
    romfs_entry_t* e;

    scf_filename_bufferize(filename);
#ifdef SC_RELEASE
    /* In release mode, always use CD-ROM instead of PC. */
    if(dev_id == SC_FILE_PC)
    {
        dev_id = SC_FILE_CDROM;
    }
#endif

    // scd_usbsd_logout(1/*usbonly*/, "@@@scf_fread(ptr=%08X, 0ffset=%d, size=%d, dev_id=%d, name=\"%s\")", ptr, offset, size, dev_id, sc->filename_buff);

    if(dev_id == SC_FILE_PC)
    { /* Use file from PC. */
        unsigned long file_size = scf_size(dev_id, filename);

        /* Check parameters. */
        retsize = size;
        if(offset > file_size)
        {
            offset = 0;
        }
        if((offset + retsize) > file_size)
        {
            retsize = file_size - offset;
        }

        if(retsize)
        {
            retsize = _scd_file_request(
                        /*type       */SCD_TYPE_RFILE, 
                        /*foffset    */offset, 
                        /*frw_address*/(unsigned long)(ptr), 
                        /*frw_len    */retsize, 
                        /*fname      */sc->filename_buff);
        }
    }
    else if(dev_id == SC_FILE_ROMFS)
    { /* Use file from RomFs. */
        unsigned long file_size = scf_size(dev_id, filename);

        /* Check parameters. */
        retsize = size;
        if(offset > file_size)
        {
            offset = 0;
        }
        if((offset + retsize) > file_size)
        {
            retsize = file_size - offset;
        }

        if(retsize)
        {
            e = romfs_get_first_entry(sc->romfs, sc->filename_buff);

            if(e == NULL)
            {
                retsize = 0;
            }
            else
            {
                if(e->flags & FILEENTRY_FLAG_FOLDER)
                {
                    retsize = 0;
                }
                else
                {
                    memcpy((void*)&romfs_entry, (void*)e, sizeof(romfs_entry_t));
                    romfs_get_entry_data(sc->romfs, &romfs_entry, (unsigned char*)ptr, offset, retsize);
                }
            }
        }
    }
    else if(dev_id == SC_FILE_CDROM)
    { /* Use file from CD-ROM. */
        if(sc->cdrom_status & SC_CDSTATUS_USE)
        {
            cdrom_init_check();

            cdrom_open_file();

            /* Check parameters. */
            if(offset > _cdrom_file.size)
            {
                offset = 0;
            }
            if((offset + size) > _cdrom_file.size)
            {
                size = _cdrom_file.size - offset;
            }

            int err;
            err = cdfs_seek(&(_cdrom_file), offset, CDFS_SEEK_SET);
            err = cdfs_read(ptr, 1, size, &(_cdrom_file));
            if(err == IAPETUS_ERR_OK)
            {
                retsize = size;
            }

            cdrom_close_file();
        }
    }
    else if(dev_id == SC_FILE_SDCARD)
    { /* Use file from SD card. */
        sdc_ret_t ret;
        unsigned long file_size = scf_size(dev_id, filename);

        /* Check parameters. */
        retsize = size;
        if(offset > file_size)
        {
            offset = 0;
        }
        if((offset + retsize) > file_size)
        {
            retsize = file_size - offset;
        }

        if(retsize)
        {
            scd_usbsd_logout(1/*usbonly*/, "@@@[SD]sdc_fat_read_file(offset=%d, size=%d) ...", offset, retsize);
            ret = sdc_fat_read_file(sc->filename_buff, offset, retsize, (unsigned char*)ptr, &retsize);
            if(ret != SDC_OK) retsize = 0;
        }
    }

    // scd_usbsd_logout(1/*usbonly*/, "@@@scf_read end retsize=%d", retsize);

    return retsize;
}


unsigned long scf_write(void* ptr, unsigned long size, unsigned char dev_id, char* filename)
{
    unsigned long retsize = 0;
    sc_internal_t* sc = sc_get_internal();

    scf_filename_bufferize(filename);
#ifdef SC_RELEASE
    /* In release mode, always use CD-ROM instead of PC. */
    if(dev_id == SC_FILE_PC)
    {
        dev_id = SC_FILE_CDROM;
    }
#endif

    if(dev_id == SC_FILE_PC)
    { /* Modify file on PC. */
        retsize = _scd_file_request(
                    /*type       */SCD_TYPE_WFILE, 
                    /*foffset    */0, 
                    /*frw_address*/(unsigned long)(ptr), 
                    /*frw_len    */size, 
                    /*fname      */sc->filename_buff);
    }
    else if(dev_id == SC_FILE_ROMFS)
    { /* Can't write to RomFs -> do nothing. */
    }
    else if(dev_id == SC_FILE_CDROM)
    { /* Can't write to CD-ROM -> do nothing. */
    }
    else if(dev_id == SC_FILE_SDCARD)
    { /* Use file from SD card. */
        sdc_ret_t ret;
        ret = sdc_fat_write_file(filename, size, (unsigned char*)ptr, &retsize);
        if(ret != SDC_OK) retsize = 0;
    }

    return retsize;
}


unsigned long scf_append(void* ptr, unsigned long size, unsigned char dev_id, char* filename)
{
    unsigned long retsize = 0;
    sc_internal_t* sc = sc_get_internal();

    scf_filename_bufferize(filename);
#ifdef SC_RELEASE
    /* In release mode, always use CD-ROM instead of PC. */
    if(dev_id == SC_FILE_PC)
    {
        dev_id = SC_FILE_CDROM;
    }
#endif

    if(dev_id == SC_FILE_PC)
    { /* Use file from PC. */
        retsize = _scd_file_request(
                    /*type       */SCD_TYPE_AFILE, 
                    /*foffset    */0, 
                    /*frw_address*/(unsigned long)(ptr), 
                    /*frw_len    */size, 
                    /*fname      */sc->filename_buff);
    }
    else if(dev_id == SC_FILE_ROMFS)
    { /* Can't write to RomFs -> do nothing. */
    }
    else if(dev_id == SC_FILE_CDROM)
    { /* Can't write to CD-ROM -> do nothing. */
    }
    else if(dev_id == SC_FILE_SDCARD)
    { /* Use file from SD card. */
        sdc_ret_t ret;
        ret = sdc_fat_append_file(filename, size, (unsigned char*)ptr, &retsize);
        if(ret != SDC_OK) retsize = 0;
    }

    return retsize;
}


unsigned long scf_mkdir(unsigned char dev_id, char* folder)
{
    unsigned long retval = 0;

#ifdef SC_RELEASE
    /* In release mode, always use CD-ROM instead of PC. */
    if(dev_id == SC_FILE_PC)
    {
        dev_id = SC_FILE_CDROM;
    }
#endif

    if(dev_id == SC_FILE_PC)
    { /* Create folder on PC. */
        retval  = _scd_file_request(
                    /*type       */SCD_TYPE_MKDIR, 
                    /*foffset    */0, 
                    /*frw_address*/0, 
                    /*frw_len    */0, 
                    /*fname      */folder);
    }
    else if(dev_id == SC_FILE_ROMFS)
    { /* Can't write to RomFs -> do nothing. */
    }
    else if(dev_id == SC_FILE_CDROM)
    { /* Can't write to CD-ROM -> do nothing. */
    }
    else if(dev_id == SC_FILE_SDCARD)
    { /* Create folder on SD card. */
        sdc_ret_t ret;
        ret = sdc_fat_mkdir(folder);
        retval = (ret == SDC_OK ? 1 : 0);
    }

    return retval;
}


/**
 * Remove file specified in path.
 *
 * Return 1 on success, 0 else.
**/
unsigned long scf_delete(unsigned char dev_id, char* path)
{
    unsigned long retval = 0;

#ifdef SC_RELEASE
    /* In release mode, always use CD-ROM instead of PC. */
    if(dev_id == SC_FILE_PC)
    {
        dev_id = SC_FILE_CDROM;
    }
#endif

    if(dev_id == SC_FILE_PC)
    { /* Use file from PC. */
        retval  = _scd_file_request(
                    /*type       */SCD_TYPE_REMOVE, 
                    /*foffset    */0, 
                    /*frw_address*/0, 
                    /*frw_len    */0, 
                    /*fname      */path);
    }
    else if(dev_id == SC_FILE_ROMFS)
    { /* Can't write to RomFs -> do nothing. */
    }
    else if(dev_id == SC_FILE_CDROM)
    { /* Can't write to CD-ROM -> do nothing. */
    }
    else if(dev_id == SC_FILE_SDCARD)
    { /* Use file from SD card. */
        sdc_ret_t ret;
        ret = sdc_fat_unlink(path);
        if(ret == SDC_OK) retval = 1;
    }

    return retval;
}



int scf_compare_extension(char* str, char* ext)
{
    int ret = 0;
    int i;
    char e1[4];
    char e2[sizeof(e1)];

    for(i=0; i<sizeof(e1); i++)
    {
        e1[i] = '\0';
        e2[i] = '\0';
    }

    i = strlen(str);
    if(i > 4)
    {
        str += i-4;
    }
    for(i=0; i<4; i++)
    {
        if((*str) == '.')
        {
            str++;
            break;
        }
        str++;
    }
    for(i=0; i<3; i++)
    {
        if(str[i] == '\0') break;
        e1[i] = sc_toupper(str[i]);
    }

    if(ext[0] == '.')
    {
        for(i=0; i<3; i++)
        {
            if(ext[i+1] == '\0') break;
            e2[i] = sc_toupper(ext[i+1]);
        }
    }
    else
    {
        for(i=0; i<3; i++)
        {
            if(ext[i] == '\0') break;
            e2[i] = sc_toupper(ext[i]);
        }
    }

    if((e1[0] == e2[0]) && (e1[1] == e2[1]) && (e1[2] == e2[2]))
    {
        ret = 1;
    }

    return ret;
}


char* scf_get_filename(char* path)
{
    char* ret = path;
    int n, i;

    n = strlen(path);

    /* Fix backslash in path. */
    for(i=0; i<(n-1); i++)
    {
        if((path[i] == '/' )
        || (path[i] == '\\'))
        {
            ret = path+i+1;
        }
    }

    return ret;
}

void scf_parent_path(char* path)
{
    int n, i;

    n = strlen(path);

    /* Fix backslash in path. */
    for(i=0; i<n; i++)
    {
        if((path[i] == '/' )
        || (path[i] == '\\'))
        {
            path[i] = '/';
        }
    }

    /* Remove eventual final slash. */
    if(n != 0)
    {
        if(path[n-1] == '/' )
        {
            n--;
            path[n] = '\0';
        }
    }

    if(n != 0)
    {
        for(i=n-1; i>=0; i--)
        {
            if(path[i] == '/')
            {
                path[i] = '\0';
                break;
            }
            /* Back to first folder -> root folder. */
            if(i == 0) path[0] = '\0';
        }
    }
}


/**
 * Return 1 if f1's name is greater than f2's name, zero else.
 * Note : case insensitive.
 * (for use in scf_filelist function only.)
**/
int scf_file_is_greater(scdf_file_entry_t* f1, scdf_file_entry_t* f2)
{
    int ret = 0;
    int i;
    char c1, c2;

    if((f1->flags & FILEENTRY_FLAG_FOLDER) != (f2->flags & FILEENTRY_FLAG_FOLDER))
    { /* Put folder on top of list. */
        ret = (f1->flags & FILEENTRY_FLAG_FOLDER ? 0 : 1);
    }
    else
    {
        for(i=0; i<SC_FILENAME_MAXLEN; i++)
        {
            c1 = sc_tolower(f1->filename[i]);
            c2 = sc_tolower(f2->filename[i]);

            if(c1 != c2)
            {
                ret = (c1 > c2 ? 1 : 0);
                break;
            }
        }
    }

    return ret;
}

/**
 * Return the count of files/folders in specified path.
**/
unsigned long scf_filecount(unsigned char dev_id, char* path)
{
    return scf_filelist(NULL, 0, 0,0, NULL, dev_id, path);
}


/**
 * List files in specified folder.
 *
 * Return the count of files in the specified folder.
**/
unsigned long scf_filelist(scdf_file_entry_t* list_ptr, unsigned long list_count, unsigned long list_offset, unsigned char list_sort, char* filter, unsigned char dev_id, char* path)
{
    sc_internal_t* sc = sc_get_internal();

    unsigned long retval = 0;
    long file_index;
    unsigned long items_count;

    romfs_entry_t* e;

#ifdef SC_RELEASE
    /* In release mode, always use CD-ROM instead of PC. */
    if(dev_id == SC_FILE_PC)
    {
        dev_id = SC_FILE_CDROM;
    }
#endif

    if((list_ptr)
    && (list_count != 0))
    {
        /* Reset file list data. */
        memset((void*)list_ptr, 0x00, list_count*sizeof(scdf_file_entry_t));
    }

    if(dev_id == SC_FILE_PC)
    { /* Use file from PC. */
        retval  = _scd_file_request(
                    /*type       */SCD_TYPE_LISTDIR, 
                    /*foffset    */list_offset, 
                    /*frw_address*/(unsigned long)list_ptr, 
                    /*frw_len    */list_count, 
                    /*fname      */path);
    }
    else if(dev_id == SC_FILE_CDROM)
    { /* List files in CD-ROM. */
        if(sc->cdrom_status & SC_CDSTATUS_USE)
        {
            cdrom_init_check();

            cdfs_filelist(list_ptr, list_count, list_offset, path, &retval);
        }
    }
    else if(dev_id == SC_FILE_ROMFS)
    { /* Use file from RomFs. */
        e = romfs_get_first_entry(sc->romfs, path);

        if(e == NULL)
        {
            retval = 0;
        }
        else
        {
            if(e->flags & FILEENTRY_FLAG_FOLDER)
            {
                /* Folder found. List files inside it. */
                file_index = 0;
                retval = 0;
                while(1)
                {
                    /* List files in selected range. */
                    if((file_index >= list_offset)
                    && (retval < list_count))
                    {
                        /* File within range : copy filename and flags to ouput parameter. */
                        char* name = (char*)(e) + sizeof(romfs_entry_t);

                        list_ptr[retval].flags = e->flags;
                        memcpy((void*)(list_ptr[retval].filename), (void*)name, e->filename_len);

                        retval++;
                    }

                    if(e->flags & FILEENTRY_FLAG_LASTENTRY)
                    {
                        break;
                    }

                    e =  romfs_get_next_entry(sc->romfs, e);
                    file_index++;
                }
            }
            else
            {
                retval = 0;
            }
        }
    }
    else if(dev_id == SC_FILE_SDCARD)
    { /* Use file from SD card. */
        sdc_fat_filelist(list_ptr, list_count, list_offset, path, &retval);
    }

    /* Get number if items in list. */
    items_count = retval;
    if(items_count > list_count)
    {
        items_count = list_count;
    }

    if(list_ptr)
    {
        /* Sanitize folder names : append slash on begining. */
        for(file_index=0; file_index<list_count; file_index++)
        {
            if((list_ptr[file_index].flags & FILEENTRY_FLAG_ENABLED)
            && (list_ptr[file_index].flags & FILEENTRY_FLAG_FOLDER ))
            {
                /* Append slash on begining of folder names. */
                if((list_ptr[file_index].filename[0] != '/' )
                && (list_ptr[file_index].filename[0] != '\\'))
                {
                    int i;
                    for(i=SC_FILENAME_MAXLEN-2; i>=0; i--)
                    {
                        list_ptr[file_index].filename[i+1] = list_ptr[file_index].filename[i];
                    }

                    /* Add slash. */
                    list_ptr[file_index].filename[0] = '/';

                    /* Don't forget terminating null character. */
                    list_ptr[file_index].filename[SC_FILENAME_MAXLEN-1] = '\0';
                }

                /* Remove "." and ".." folders. */
                if((list_ptr[file_index].filename[1] ==  '.')
                && (list_ptr[file_index].filename[2] == '\0'))
                {
                    list_ptr[file_index].flags &= ~FILEENTRY_FLAG_ENABLED;
                }
                if((list_ptr[file_index].filename[1] ==  '.')
                && (list_ptr[file_index].filename[2] ==  '.')
                && (list_ptr[file_index].filename[3] == '\0'))
                {
                    list_ptr[file_index].flags &= ~FILEENTRY_FLAG_ENABLED;
                }
            }
        }

        /* Reset sort index. */
        for(file_index=0; file_index<list_count; file_index++)
        {
            list_ptr[file_index].sort_index = file_index;
        }

        /* If required, apply file filtering. */
        if(filter != NULL)
        {
            char ext[5];
            int n = strlen(filter);
            int i, j;

            /* Mark all folders as selected by filtering. */
            for(file_index=0; file_index<items_count; file_index++)
            {
                if(list_ptr[file_index].flags & FILEENTRY_FLAG_FOLDER)
                {
                    list_ptr[file_index].flags |= FILEENTRY_FLAG_FILTERED;
                }
            }

            for(i = 0; i<n; i++)
            {
                if((i == 0)
                || (filter[i] == ',')
                || (filter[i] == '\0'))
                {
                    if(filter[i] == ',')
                    {
                        i++;
                    }

                    for(j=0; j<sizeof(ext); j++)
                    {
                        ext[j] = '\0';
                    }

                    for(j=0; j<(sizeof(ext)-1); j++)
                    {
                        if(filter[i] == '\0') break;
                        ext[j] = filter[i];
                        if(filter[i+1] == ',') break;
                        i++;
                    }

                    for(file_index=0; file_index<items_count; file_index++)
                    {
                        /* Don't compare already selected files. */
                        if((list_ptr[file_index].flags & FILEENTRY_FLAG_FILTERED) == 0)
                        {
                            if(scf_compare_extension(list_ptr[file_index].filename, ext))
                            {
                                list_ptr[file_index].flags |= FILEENTRY_FLAG_FILTERED;
                            }
                        }
                    }
                }
            }

            /* Disable non filtered files. */
            for(file_index=0; file_index<items_count; file_index++)
            {
                if((list_ptr[file_index].flags & FILEENTRY_FLAG_FILTERED) == 0)
                {
                    list_ptr[file_index].flags &= ~FILEENTRY_FLAG_ENABLED;
                }
                list_ptr[file_index].flags &= ~FILEENTRY_FLAG_FILTERED;
            }
        }

        /* Sort entries */
        if(list_sort)
        {
            // Sort faces. (from mic's flatcube)
            // Bubble-sort the whole lot.. yeehaw!
            if(items_count > 1)
            {
                unsigned short i, j, idi, idj;
                unsigned short temp;
                for(i=0; i<items_count-1; i++)
                {

                    for(j=i+1; j<items_count; j++)
                    {
                        idi = list_ptr[i].sort_index;
                        idj = list_ptr[j].sort_index;

                        if(scf_file_is_greater(list_ptr+idi, list_ptr+idj))
                        {
                            temp                   = list_ptr[i].sort_index;
                            list_ptr[i].sort_index = list_ptr[j].sort_index;
                            list_ptr[j].sort_index = temp;
                        }
                    }
                }
            }
        } // if(list_sort)
    }

    return retval;
}


