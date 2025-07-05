/**
 *  SatCom Library
 *  by cafe-alpha.
 *  WWW: http://ppcenter.free.fr/satcom/
 *
 *  See LICENSE file for details.
**/

#ifndef SATCOM_SATURN_H
#define SATCOM_SATURN_H

#include "sc_common.h"

#ifdef SC_USE_IAPETUS
#include "iapetus/iapetus.h"
#endif // SC_USE_IAPETUS

#ifndef SC_NO_SATREGS
#include "satregs/satregs.h"
#endif // SC_NO_SATREGS

//----------------------------------------------------------------------
// Header for use in optional ROM code : indicates that code is present
// in ROM or not.
// 
//----------------------------------------------------------------------
#include "sc_rom_code.h"


/*------------------------------------------------------------------------*/
/*- Libyaul stuff.                                                      --*/
//#ifdef SC_USE_LIBYAUL //TMP
#if 0
#include "libyaul/inttypes.h"

//#include "libyaul/common/common.h"
//#include "libyaul/common/endian.h"
//#include "libyaul/common/exception.h"
//#include "libyaul/common/irq-mux.h"
//#include "libyaul/common/stack.h"
//#include "libyaul/common/gdb/gdb-signals.h"
//#include "libyaul/common/gdb/gdb.h"
//#include "libyaul/common/gdb/ihr.h"
//#include "libyaul/common/gdb/sh2-704x.h"

//#include "libyaul/scu/scu-internal.h"
#include "libyaul/scu/scu.h"

//#include "libyaul/scu/bus/a/cs0/dram-cartridge/dram-cartridge-internal.h"
#include "libyaul/scu/bus/a/cs0/dram-cartridge/dram-cartridge.h"

//#include "libyaul/scu/bus/a/cs0/usb-cartridge/usb-cartridge-internal.h"
#include "libyaul/scu/bus/a/cs0/usb-cartridge/usb-cartridge.h"

//#include "libyaul/scu/bus/a/cs2/cd-block/cd-block-internal.h"

#include "libyaul/scu/bus/a/cs2/cd-block/cd-block.h"
#include "libyaul/scu/bus/a/cs2/cd-block/cd-block/cmd.h"

//#include "libyaul/scu/bus/b/scsp/scsp-internal.h"
#include "libyaul/scu/bus/b/scsp/scsp.h"

//#include "libyaul/scu/bus/b/vdp1/vdp1-internal.h"
#include "libyaul/scu/bus/b/vdp1/vdp1.h"
#include "libyaul/scu/bus/b/vdp1/vdp1/cmdt.h"
#include "libyaul/scu/bus/b/vdp1/vdp1/fbcr.h"
#include "libyaul/scu/bus/b/vdp1/vdp1/ptmr.h"

//#include "libyaul/scu/bus/b/vdp2/vdp2-internal.h"
#include "libyaul/scu/bus/b/vdp2/vdp2.h"
#include "libyaul/scu/bus/b/vdp2/vdp2/cram.h"
#include "libyaul/scu/bus/b/vdp2/vdp2/pn.h"
#include "libyaul/scu/bus/b/vdp2/vdp2/priority.h"
#include "libyaul/scu/bus/b/vdp2/vdp2/scrn.h"
#include "libyaul/scu/bus/b/vdp2/vdp2/tvmd.h"
#include "libyaul/scu/bus/b/vdp2/vdp2/vram.h"

//#include "libyaul/scu/bus/cpu/cpu-internal.h"
#include "libyaul/scu/bus/cpu/cpu.h"
#include "libyaul/scu/bus/cpu/cpu/dmac.h"
#include "libyaul/scu/bus/cpu/cpu/intc.h"
#include "libyaul/scu/bus/cpu/cpu/registers.h"
#include "libyaul/scu/bus/cpu/cpu/ubc.h"

//#include "libyaul/scu/bus/cpu/smpc/smpc-internal.h"
#include "libyaul/scu/bus/cpu/smpc/smpc.h"
#include "libyaul/scu/bus/cpu/smpc/smpc/peripheral.h"
#include "libyaul/scu/bus/cpu/smpc/smpc/rtc.h"
#include "libyaul/scu/bus/cpu/smpc/smpc/smc.h"

#include "libyaul/scu/scu/dma.h"

#include "libyaul/scu/scu/dsp.h"

#include "libyaul/scu/scu/ic.h"

#include "libyaul/scu/scu/timer.h"

#endif // SC_USE_LIBYAUL


/*------------------------------------------------------------------------*/
/*- USB dev cart, SD card, etc related definitions                      --*/
#include "satcart.h"




/*------------------------------------------------------------------------*/
/*- Some definitions from SBL.                                          --*/
#include "SBL/INCLUDE/SEGA_TIM.H"
#include "SBL/INCLUDE/SEGA_SYS.H"
#include "SBL/INCLUDE/SEGA_INT.H"



/**
 *           --- Note about compile flags: ---
 *
 * When building a "Release" version of your program, you should 
 * define "SC_RELEASE" and rebuild your program.
 * And in this case, transfer and debug-related functions are removed from your program.
 *
 * When want your program to run in Low RAM, you shoud define "SC_EXEC_IN_LRAM"
 * and rebuild your program.
 * When SC_EXEC_IN_LRAM is defined, temporary upload area for code to execute
 * is set from 0x06004000, and when SC_EXEC_IN_LRAM is not defined, this area
 * is set from the begining of Low RAM.
 *
 * If you execute satcom from ROM, please define SC_EXEC_IN_ROM.
 * In this case, if SRAM is not available, SatCom work RAM will be set on BUP library work RAM.
 *
 * It is recommended to define stuff by using gcc's -D command line switch.
 * Example: gcc -DSC_RELEASE <etc>
**/




/*------------------------------------------------------------------------*/
/*- Cartridge RAM related function. --------------------------------------*/

#if defined( __GNUC__ )
#pragma pack(1)
#else
#pragma pack(push,1)
#endif

typedef struct _sc_cartram_t
{
    /* Cartridge RAM size, in MB. */
    unsigned long size;

    /* Pointer to cartridge RAM area. */
    void* ptr;

    /* Cartridge RAM ID. */
    unsigned char id;

    /* Reserved for future use. */
    unsigned char reserved;

    /* Detection status. Set to 1 even if verify failed. */
    unsigned char detected;

    /* Verify status. Set to 1 if no RAM detected. */
    unsigned char verify_ok;

    /* RAM detection status. */
    const char* status_str;
} sc_cartram_t;

#if defined( __GNUC__ )
#pragma pack()
#else
#pragma pack(pop)
#endif

/**
 * scm_cartram_detect
 *
 * Detect extra RAM on cartridge.
 *  - 1MB or 4MB on RAM extension cart, or Action Replay.
 *  - Other from custom cartridge ? (TBD)
 *
 * Note : this function is not called in sc_init function.
 *        Please call it when your application need extra RAM.
 *
 * Return cartridge RAM informations in "sc_cartram_t* ram" output parameter.
**/
#define SCM_CARTRAM_FAST_VERIFY (0)
#define SCM_CARTRAM_FULL_VERIFY (1 << 0)
void scm_cartram_detect(sc_cartram_t* ram, unsigned long detect_flags);




/*------------------------------------------------------------------------*/
/*- ROM code related function. -------------------------------------------*/

/**
 * sc_rom_code_load
 *
 * Copy ROM code data to RAM.
 * Return 1 if operation success, 0 else.
**/
unsigned long sc_rom_code_load(void* rom_adr, void* ram_adr);


/*------------------------------------------------------------------------*/
/*- Exit/Reset functions. ------------------------------------------------*/

/* Exit to multiplayer (BIOS). */
void sc_exit(void);

/* System Reset. */
void sc_sysres(void);



/*------------------------------------------------------------------------*/
/* Log stuff. ------------------------------------------------------------*/

/**
 * scd_logscreen_callback
 *
 * Callback for use when you want to print your SatCom log messages to screen.
 * Note: Set callback to NULL if you want to remove it.
 *
 * The purpose of this function is to provide log display environment to
 *  user program using SatCom in the case sc_log's messages can't be displayed on PC.
 * Example: when SD card is connected on pad2 and backup memory card is connected
 *  instead of ARP, PC can't be connected to Saturn, but displaying log messages
 *  to screen enables debugging.
**/
typedef void (*Fct_scd_screen)(char*/*string*/);
void scd_logscreen_callback(Fct_scd_screen cons);


/**
 * Callback for use when satcom_lib want to perform soft reset.
 * This is called before executing a program uploaded via USB/whatever link.
 * 
 * First parameter is soft reset flags, defined as SC_SOFTRES_* in sc_common.h .
**/
typedef void (*Fct_sc_soft_reset)(unsigned long);



/*------------------------------------------------------------------------*/
/*- Base functions. ------------------------------------------------------*/
#include "sdcard/sdcard.h"

/**
 * Memory requirements for SatCom workram : 7KB.
 * Note : In order to keep compatibility with other applications, 
 *        memory size shouldn't be larger than 7KB.
 * START | END  | Description          
 * ------+------+-----------------------
 *     0 | 3071 | Debugger related data
 *  3072 | 7167 | SatCom internal data
**/
#define SC_DEBUGGER_SIZE   (3*1024)
#define SC_INTERNAL_SIZE   (4*1024)
#define SC_WORKRAM_SIZE    (SC_DEBUGGER_SIZE + SC_INTERNAL_SIZE)
#define SC_WORKRAM_U32SIZE (SC_WORKRAM_SIZE / sizeof(unsigned long))

/**
 * Execute this function at the program startup.
 * Parameter1: unsigned long* workram : 4bytes aligned buffer for use by SatCom.
 *
 * Example of use :
 *  unsigned long _sc_workram[SC_WORKRAM_U32SIZE];
 *  ...
 *  void main(void) {
 *      ...
 *      sc_init(_sc_workram);
**/
void sc_init(unsigned long* workram);


/**
 * Alternate sc_init function that allows to perform
 * or skip CDROM initialisation, etc.
 * Please don't use unless you know what you are doing.
 * Using this function is required if you want to access SD card.
**/
typedef struct _sc_initcfg_t
{
    /**
     * Initialisation flags.
     *
     * SC_INITFLAG_NOCDROM :
     *  If set, don't access CDROM.
     *  Else, access CDROM via CyberWarriorX's iapetus library.
     *  Not using CDROM may be needed to prevent SatCom freezing
     *  on startup, typically when booting from cartridge.
     *
     * SC_INITFLAG_NOSMPCTIME :
     *  If set, don't use SMPC when acquiring current time :
     *   - In sc_tim_smpcpoll function.
     *   - When setting file timestamp in SD card library.
     *
     * SC_INITFLAG_KEEPLOG :
     *  If set, keep previous log file on SD card.
     *  Mostly for internal debug purpose, since log file may grow indefinitely.
     *
     * SC_INITFLAG_DEFAULT :
     *  Default configuration for typical usage.
     *  SC_INITFLAG_* are designed in order to have default
     *  configuration with flag value set to zero.
    **/
#define SC_INITFLAG_NOCDROM    (0x00000001)
#define SC_INITFLAG_NOSMPCTIME (0x00000002)

#define SC_INITFLAG_KEEPLOG    (0x80000000)

#define SC_INITFLAG_DEFAULT    (0x00000000)
    //unsigned char cdrom_use;
    unsigned long flags;

    /**
     * Pointer to workram used by SatCom.
     * This RAM can be located on HRAM or LRAM.
    **/
    unsigned long* workram;

    /**
     * Log file name when saving log to SD card.
     * When NULL, default file name is used.
    **/
    char* logfile_name;

    /**
     * Callback function used to display log messages to screen.
     * Same usage as scd_logscreen_callback function.
    **/
    Fct_scd_screen logscreen_callback;

    /**
     * Callback function when soft reset is required.
    **/
    Fct_sc_soft_reset soft_reset;
} sc_initcfg_t;

/**
 * sc_initcfg_reset
 *
 * Reset configuration structure with default settings.
**/
void sc_initcfg_reset(sc_initcfg_t* cfg, unsigned long* workram);

/**
 * sc_init_cfg
 *
 * Init SatCom library with specified settings.
 * Example : same behavior as sc_init function :
 *  unsigned long _sc_workram[SC_WORKRAM_U32SIZE];
 *  void main(void) {
 *    sc_initcfg_t cfg;
 *    sc_initcfg_reset(&cfg, _sc_workram);
 *    sc_init_cfg(&cfg);
 *
**/
void sc_init_cfg(sc_initcfg_t* cfg);



/**
 * sc_extdev_getid
 * sc_extdev_getname
 *
 * Extension (cartridge or other) device.
 * Extension device is detected during sc_init function call.
 *
 * Function below returns ID of extension devices detected.
**/
#define SC_EXT_NONE     0
#define SC_EXT_BOOTCART 1
#define SC_EXT_USBDC    2
#define SC_EXT_SATCART  3
#define SC_EXT_WASCA    4
#define SC_EXT_PAD2USB  5

#define SC_EXTDEV_CNT   6
unsigned char sc_extdev_getid(void);
const char* sc_extdev_getname(void);


/**
 * sc_usb_is_port_available
 *
 * Check if USB port is available or not.
 * Returns 0 if USB dev cart not detected.
 * Returns 1 if USB port detected.
**/
int sc_usb_is_port_available(void);

/**
 * sc_usb_is_connected
 *
 * Check if USB cable is connected or not.
 * Returns 0 if USB dev cart not detected.
 * Returns 0 if no USB cable connected.
 * Returns 1 if USB cable connected.
**/
int sc_usb_is_connected(void);



/**
 * sc_check
 *
 * Perform pending I/O with PC.
 *
 * Please call this function regularly, for example
 * before during each V-Blank, or when timer/whatever
 * interrupt is triggered, or any timing else you want.
 *
 * This function won't block your program execution flow :
 * if there's nothing coming from your PC, sc_check will
 * check a couple of I/O related registers and return
 * to your program.
**/
#ifndef SC_RELEASE
    void _sc_internal_check(void);
#   define sc_check() _sc_internal_check()
#else
#   define sc_check() {;}
#endif // !SC_RELEASE

/**
 * Debug stuff for link testing purpose.
 * Use only when you know what you are doing.
**/
#ifdef SC_EXEC_IN_LRAM
//#define UDC_DEBUG
#endif

#ifdef UDC_DEBUG

/*
 * This uses Pseudo Saturn Kai's console printf function, 
 * hence won't even compile on other projects.
 * Should remove UDC_DEBUG stuff when no longer needed ...
 */
void cons_printf(unsigned short color, char *string, ...);
#define UDC_LOGOUT(_STR_, ...) cons_printf(15, _STR_, __VA_ARGS__)

#else
#define UDC_LOGOUT(_STR_, ...)
#endif




/*------------------------------------------------------------------------*/
/*- Link-related functions. ----------------------------------------------*/
/**
 * Supported link types :
 *  - SC_LINK_NONE    : Do nothing.
 *  - SC_LINK_LEGACY  : Action Replay "legacy" comms link.
 *  - SC_LINK_USBDC   : "USB dev cart" by Antime.
 *  - SC_LINK_USBPAD2 : USB connectivity via second pad.
**/
#define SC_LINK_NONE    0
#define SC_LINK_LEGACY  1
#define SC_LINK_USBDC   2
#define SC_LINK_USBPAD2 3

#define SC_LINKS_COUNT 4

/**
 * EMS/Datel type for commslink.
**/
#define SC_ARP_EMS   1
#define SC_ARP_DATEL 2



/**
 * SatCom settings kept in SD card.
 *  - SC_SET_VER1    : Version ID (1 byte) kept at the beginning of the settings data.
 *
 * When you loose compatibility with previous structure (example: modify
 * structure size, and/or modify elements other than `reserved*', you MUST
 * change the version ID.
**/
#define SC_SET_VER1     '1'

/**
 * Few bytes at the end of SatCom settings data
 * are reserved for main application using SatCom
 * library (-> Pseudo Saturn Kai).
 * data is stored as-is in extra_dat region, and
 * up to SC_BUP_EXTRA_LEN bytes are available for use.
 * Size in this area is small, and due to SatCom
 * internal memory limitations, it can't be extended, so 
 * please only save boot-related important data here.
**/
#define SC_BUP_EXTRA_LEN (192-8-sizeof(struct tm))

//----------------------------------------------------------------------
// Settings data saved in SD card.
// It consists in 128 bytes starting with :
//   1 byte    : this structure whole size, in bytes (=128)
//   1 byte    : this structure version (=SC_SET_VER1)
//   1 byte    : Saturn <-> PC transfer type.
//   1 byte    : ARP address type (0:Auto Detect, 1:EMS, 2:Datel).
//   1 byte    : Set to 1 if saving logs to SD card is requested.
//   3 bytes   : reserved for future usage.
//  36 bytes   : Last SMPC time recorded.
//  remaining  : For use by host application
//----------------------------------------------------------------------
#define SC_SETTINGS_NAME "/SETTINGS.DAT"

#if defined( __GNUC__ )
#pragma pack(1)
#else
#pragma pack(push,1)
#endif

typedef struct _sc_settings_t
{
    /*-----------------------------------------*/
    /* This structure size.                    */
    unsigned char struct_size;
    /* This structure version.                 */
    unsigned char struct_version;
    /* Link type used.                         */
    unsigned char link_type;
    /* ARP type, in the case an ARP is used.   */
    unsigned char arp_type;

    /**
     * 0   : Don't save log messages to SD card.
     * else: Save log messages to SD card.
    **/
    unsigned char log_sdcard;

    /*-----------------------------------------*/
    /* Reserved for future use.                */
    unsigned char reserved1[3];

    /*-----------------------------------------*/
    /* Last time from SMPC (36 bytes).         */
    struct tm time;

    /*-----------------------------------------*/
    /* Reserved for host application.          */
    unsigned char extra_dat[SC_BUP_EXTRA_LEN];
} sc_settings_t;

#if defined( __GNUC__ )
#pragma pack()
#else
#pragma pack(pop)
#endif


/**
 * sc_settings_read
 * sc_settings_write
 * sc_settings_clear
 *
 * Get/Set/Erase settings data.
**/
void sc_settings_read(sc_settings_t* dat_out);
void sc_settings_write(sc_settings_t* dat_in);
void sc_settings_clear(void);

/**
 * sc_settings_getptr
 *
 * Get settings data pointer from internal buffer.
**/
sc_settings_t* sc_settings_getptr(void);

/**
 * sc_get_linkname
 *
 * Get link type name.
**/
char* sc_get_linkname(unsigned char p);

/**
 * sc_get_arptype
 *
 * Get ARP type string.
**/
char* sc_get_arptype(unsigned char a);



/*------------------------------------------------------------------------*/
/*- Program execute function. --------------------------------------------*/

/**
 * copystub_execute
 *
 * Parameters:
 *  tmp_address : address where program to execute is stored.
 *  tmp_address : address where program to execute is stored.
 *  exec_address: address where to execute program from.
 *
 * Copy program to execute to its execution area, and clean up HRAM/LRAM.
 * Copy/execution are performed by standalone "copystub" program, so that
 * maximum RAM cleanup (not 100% clean, but can't do better) is made
 * before executing program.
**/
void copystub_execute(unsigned long softres_flags, unsigned long tmp_address, unsigned long length, unsigned long exec_address);

/**
 * sc_execute
 *
 * Perform SatCom related cleanup, and then call copystub_execute function.
 *
 * Note : can't be called when SatCom isn't initialized !
**/
void sc_execute(unsigned long softres_flags, unsigned long tmp_address, unsigned long length, unsigned long exec_address);


/*------------------------------------------------------------------------*/
/*- Wait for *Blank functions. -------------------------------------------*/
/* Sync macros */
#define sc_wait_vblank_in()  while(!(TVSTAT&VBLANK));
#define sc_wait_vblank_out() while(TVSTAT&VBLANK);

void sc_wait_vblank(void);


/*------------------------------------------------------------------------*/
/*- "Stopwatch-like" timer functions. ------------------------------------*/


/**
 * sc_tmr_start
 *
 * Start/Reset the free run timer.
**/
void sc_tmr_start(void);

/**
 * sc_tmr_lap
 *
 * Get free run timer value in CPU tick unit.
 * Note1: 1 tick = 0.3 usec (approx.), so you can safely directly use the usec value.
 *        (using tick value is recommended in the case you need to add dozens of lap values)
 * Note2: tick value overflows at around 16msec (16000usec), so don't measure long time interval !!
**/
unsigned short sc_tmr_lap(void);

/**
 * sc_tmr_lap
 *
 * Get timer value in micro second unit.
**/
unsigned long sc_tmr_lap_usec(void);

/**
 * sc_tmr_tick2usec
 *
 * Convert tick value to usec value.
 * Note1: 1 tick = 0.3 usec (approx.), so you can safely directly use the usec value.
 *        (using tick value is recommended in the case you need to add dozens of lap values)
 * Note2: tick value overflows at around 16msec (16000usec), so don't measure long time interval !!
**/
unsigned long sc_tmr_tick2usec(unsigned long tick);

/**
 * sc_usleep
 *
 * Wait for a given amount of time (in usec).
 * Note: You can safely sleep up to 42 seconds (= 42000000 usec).
 *       After this value, sleep will be incorrect due to overflow errors.
**/
void sc_usleep(unsigned long usec);


/*------------------------------------------------------------------------*/
/*- SMPC's RTC date and time acquisition. --------------------------------*/
#include <time.h>

/**
 * sc_tim_smpcpoll
 *
 * Acquire current time from SMPC's internal clock
 * to output parameter.
 *
 * Note : if wanting to use time in unix format, 
 *        just use mktime function as usual :
 *        time_t unix_time = mktime(sc_tim_smpcpoll());
**/
struct tm* sc_tim_smpcpoll(void);



/*------------------------------------------------------------------------*/
/*- Built-in backup memory related includes. -----------------------------*/
#include "SBL/INCLUDE/SEGA_BUP.H"

/*
 * Backup devices.
 */
#define BUP_DEV_INTERNAL  0
#define BUP_DEV_CARTRIDGE 1
#define BUP_DEV_FLOPPY    2

/**
 * Convert date from struct tm to BupDate format.
 * Typical usage example;
 *  BupDate date;
 *  sc_bup_conv_date(sc_tim_smpcpoll(), &date);
**/
void sc_bup_conv_date(struct tm* t, BupDate* sat_date);


/*------------------------------------------------------------------------*/
/*- Debugger functions. --------------------------------------------------*/

/**
 * sc_get_dbg_buff
 *
 * Get address of debugger library workram.
**/
scd_data_t* sc_get_dbg_buff(void);


#include <stdio.h>
#include <stdarg.h>


#ifndef SC_RELEASE

/**
 * scd_usbsd_logout
 * scd_usbsd_msgout
 *
 * Log functions for satcom_lib internal use.
 * Please don't use unless you know what you are doing.
**/
void _scd_internal_logout(const char* file, int line, const char* func, int usbonly, char *string, ...);
#define scd_usbsd_logout(usbonly, ...) _scd_internal_logout(__BASE_FILE__, __LINE__, __FUNCTION__, usbonly, __VA_ARGS__)
void _scd_internal_msgout(const char* file, int line, const char* func, int usbonly, unsigned char id, char *string, ...);
#define scd_usbsd_msgout(usbonly, id, ...) _scd_internal_msgout(__BASE_FILE__, __LINE__, __FUNCTION__, usbonly, id, __VA_ARGS__)

/**
 * scd_logout
 *  Send message to scrollable text area in SatLink's log window.
 *
 * scd_msgout
 *  Send message to non-scrollable text area in SatLink's log window.
 *
 *
 * scd_msgout function displays message to non-scrollable text area, 
 * while scd_logout displays message in scrollable text area.
 *
 * scd_msgout is recommended when wanting to periodically display
 * information (example : FPS, polygon count, memory used, etc).
 *
 * scd_logout is recommended when wanting to display even based
 * information (example : button pushed, error hapened, etc).
 * Please use it in the same way as you would use printf when
 * testing something on your PC.
 *
 * Example :
 *  scd_logout("Test 1 (%d %d %d)", 1, 2, 3);
 *  scd_msgout(1, "Test 2");
 *  scd_msgout(2, "Test 3");
 *  scd_msgout(1, "Test 4");
 *  scd_logout("Test 5");
 *
 *  int counter = 0;
 *  while(1)
 *  {
 *      scd_msgout(3, "Test 6 (%d)", counter);
 *      sc_check();
 *      counter++;
 *  }
 *
 * Will display something like this in SatLink's log window :
 * +------------------------------------------------------------------------+
 * |> Test 1 (1 2 3)              |[ID]| [Message]                          |
 * |> Test 5                      |  1 | Test 4                             |
 * |                              |  2 | Test 3                             |
 * |                              |  3 | Test 6 (123)                       |
**/
#define scd_logout(...) _scd_internal_logout(__BASE_FILE__, __LINE__, __FUNCTION__, 0, __VA_ARGS__)
#define scd_msgout(id, ...) _scd_internal_msgout(__BASE_FILE__, __LINE__, __FUNCTION__, 0, id, __VA_ARGS__)

/**
 * scd_prompt
 *
 * Send prompt_str message to PC, then wait until receiving answer from PC.
 * Note1: returned string is a global variable, so you may need to copy
 *        it somewhere in the case you want to heap up calls to scd_prompt.
**/
char* scd_prompt(char* prompt_str);

/**
 * scd_logflush
 *
 * Wait PC to receive all the debug messages.
 * Unlike sc_check, this function will block your program until PC receives
 * all debug messages from Saturn.
 *
 * Typical usage : call this function when your program may crash soon.
 * Reason : sc_check function is fast, but doesn't guarantees that all
 * debug messages will be actualy sent to PC.
 * In normal usage, this doesn't matters, since debug messages will be processed
 * a while after that, but in the case you're not sure if code after your
 * log message will run or crash, you may need something to safely send all
 * your log messages to PC
 *
 * Example :
 *  scd_logout("Test 1");
 *  scd_logflush();
 *  some_function_that_may_crash_your_saturn();
 *  scd_logout("Test 2");
 *  scd_logflush();
 **/
void scd_logflush(void);


/**
 * scd_where_here
 *
 * Indicates where (which source file, which line) you are.
 * Typically used when you want to know which condition is executed, and which one isn't.
**/
void _scd_internal_wh(const char* file, int line, const char* func, int usbonly);
#define scd_where_here() _scd_internal_wh(__BASE_FILE__, __LINE__, __FUNCTION__, 1)

#else

#   define scd_logout(...)     {;}
#   define scd_msgout(id, ...) {;}
#   define scd_prompt(pstr)    ""
#   define scd_logflush()      {;}
#   define scd_where_here()    {;}

#endif // !SC_RELEASE


/*------------------------------------------------------------------------*/
/*- File name/path related functions. ------------------------------------*/
/**
 * scf_compare_extension
 *
 * Compare filename's extension to extension given in parameter.
 * Comparision is case insensitive.
 *
 * Returns 1 when extension are the same, zero else.
 *
 * Example : scf_compare_extension("/FOLDER/FILE.BIN", ".bin") returns 1;
**/
int scf_compare_extension(char* str, char* ext);


/**
 * scf_get_filename
 *
 * Extract file name from specified path.
 *
 * Examples :
 *  "/FOLDER/SUBFLDR/FILE.EXT" -> "FILE.EXT"
**/
char* scf_get_filename(char* path);


/**
 * scf_parent_path
 *
 * Extract parent path from specified path.
 *
 * Examples :
 *  "/FOLDER/SUBFLDR/FILE.EXT" -> "/FOLDER/SUBFLDR"
 *  "/FOLDER/SUBFLDR"          -> "/FOLDER"
 *  "/FOLDER"                  -> ""
**/
void scf_parent_path(char* path);


/*------------------------------------------------------------------------*/
/*- File I/O functions. --------------------------------------------------*/
/**
 * Note: Please use the following folder/file naming rules :
 *   1. Please name from the root directory.
 *   2. Please use slash ('/') folder delimiter.
 *   3. Please use UPPER CASE maximum 8 characters directory names.
 *   4. Please use UPPER CASE 8.3 file names.
 *  -> Typical file name: "/FOLDER1/FOLDER2/FILE.EXT"
 *   5. Maximum SC_PATHNAME_MAXLEN (96) characters per full file path, 
 *      including terminating null character.
 *  -> Due to 8.3 limitation, maximum path length is more than 10 sub
 *     folders, so shouldn't be a problem in practice.
 *  * Theses rules applies to all file device : PC, CD-ROM, SD card, etc.
**/
#include "sc_file.h"


/**
 * sc_get_sdcard_buff
 *
 * Get address of SD card library workram.
**/
sdcard_t* sc_get_sdcard_buff(void);




/**
 * scf_dev_getname
 *
 * Return name of device with specified ID.
**/
char* scf_dev_getname(unsigned char dev_id);

/**
 * scf_dev_status
 *
 * Set status for specified device.
**/
void scf_dev_status(sc_device_t* dev, unsigned char dev_id);


/**
 * scf_dev_list
 *
 * Set a list of available devices.
 * Return the count of devices available.
 * sc_device_t* devlist must point to an array of SC_DEVICES_CNT elements.
**/
int scf_dev_list(sc_device_t* devlist);



/**
 * scf_size
 *
 * Return specified file's size.
 * In the case the file doesn't exists, return 0
**/
unsigned long scf_size(unsigned char dev_id, char* filename);


/**
 * scf_read
 *
 * Read `size' bytes from specified file's `offset'th byte to `ptr'.
 *
 * Return the number of bytes actually read.
 * In the case the file doesn't exists, or read operation failed, return 0
 *
 * Note1: To increase performances when reading from CD-ROM, it is recommended
 *        to use offset values that are multiple of 2048 bytes.
 *        If it is not the case, 2 open->read->close requests are performed (instead of 1).
 *        Moreover, up to 2047 "useless" bytes may be read from CD when offset is not a multiple of 2048 bytes.
 *
 * Note2: To increase performances when reading from CD-ROM, it is recommended
 *        to use files in root folder only.
 *
 * Note3: If `size' is higher than file's size (=fsz), fsz bytes are actually read, and fsz is returned.
 *        So you can use the following code :
 *        char buffer[1234];
 *        unsigned long file_len = sc_fread(SC_FILE_SDCARD, filename, 0, sizeof(buffer), (void*)buffer);
**/
unsigned long scf_read(unsigned char dev_id, char* filename, unsigned long offset, unsigned long size, void* ptr);


/**
 * scf_write
 *
 * Write `size' bytes from `ptr' to specified file.
 * In the case specified file already exists, its contents is erased.
 *
 * Return the number of bytes actually written.
 * In the case write operation failed, return 0
 *
 * Note: When trying to write to a file in the CD-ROM, 0 is returned.
**/
unsigned long scf_write(void* ptr, unsigned long size, unsigned char dev_id, char* filename);


/**
 * scf_append
 *
 * Append `size' bytes from `ptr' to specified file.
 * In the case specified file doesn't exists, file is created, and data written to it.
 *
 * Return the number of bytes actually appended.
 * In the case append operation failed, return 0
 *
 * Note: When trying to append to a file in the CD-ROM, 0 is returned.
**/
unsigned long scf_append(void* ptr, unsigned long size, unsigned char dev_id, char* filename);

/**
 * scf_mkdir
 *
 * Make directory.
 *
 * Return 1 on success, 0 else.
 * Note : returns 1 even in the case folder already exists.
**/
unsigned long scf_mkdir(unsigned char dev_id, char* folder);

/**
 * scf_delete
 *
 * Remove file specified in path.
 *
 * Return 1 on success, 0 else.
**/
unsigned long scf_delete(unsigned char dev_id, char* path);

/**
 * scf_filecount
 *
 * Return the count of files/folders in specified path.
**/
unsigned long scf_filecount(unsigned char dev_id, char* path);

/**
 * scf_filelist
 *
 * List files/folders in specified path.
 *
 * Up to `list_count' files/folders entries are stored in `list_ptr' array.
 *
 * `list_offset' specifies the entry ID from where to start listing.
 * Example : In the case 42 files/folders are present in specified path, 
 * and `list_count' = 10, and `list_offset' = 20, 
 * files/folders from ID #20 to ID #29 are stored in `list_ptr' array.
 *
 * This allows to list entries little by little, even in the case there are 
 * many files/folders to list and/or few memory is available on Saturn
 * for storing listing results.
 *
 * When `list_sort' is set to 1, sort entries according to their file names.
 * (Entries themselves in `list_ptr' array are unsorted, 
 * you need to use `sort_index' in each entry in order to get the sorted list)
 *
 * `filter' allows to filter files according to their extensions.
 *  - When NULL, all files are listed.
 *  - All extensions are separated by commas.
 *  - Extension can't be larger than 3 characters.
 * Example filter = ".dat,.bin,.txt"
 *
 * Note : when `list_count' is lower than files/folder in specified path, 
 * or when `list_offset' is nonzero, entries are not sorted, 
 * even when `list_sort' is set to 1.
 *
 * Return the total count of files/folders in the specified folder.
**/
unsigned long scf_filelist(scdf_file_entry_t* list_ptr, unsigned long list_count, unsigned long list_offset, unsigned char list_sort, char* filter, unsigned char dev_id, char* path);


/*------------------------------------------------------------------------*/
/*- File I/O functions --> RomFs-only stuff. -----------------------------*/

/**
 * sc_romfs_setdata
 *
 * Specify where RomFs data is located.
 * In the case invalid RomFs data pointer is provided, previous pointer is kept.
 *  -> It is OK to call this function several times with RomFs candidate
 *  pointers : the last valid pointer will be used to access RomFs data.
 * Note : sc_init function must be called before this function.
 *
 * Returns 1 if RomFs detected, zero else.
**/
int sc_romfs_setdata(unsigned char* romfs);

/**
 * sc_romfs_get_dataptr
 *
 * Return pointer/length to raw data of specified file in RomFs data.
 *
 * Note : if RomFs data doesn't exist, or specified file doesn't exist, 
 *  NULL pointer is returned.
 * Note : If specified file is compressed, NULL pointer is returned.
**/
unsigned char* sc_romfs_get_dataptr(char* filename, unsigned long* datalen);


#endif //SATCOM_SATURN_H
