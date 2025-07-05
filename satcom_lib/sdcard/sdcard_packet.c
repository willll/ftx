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
#include <string.h> /* For memset,memcpy */


void sdc_a_bus_init(void)
{
    AREF = 1;
    ASR0 = 1;
}
void sdc_a_bus_deinit(void)
{
}

void sdc_output(void)
{
    sdcard_t* sd = sc_get_sdcard_buff();
    /* Output signals to SD card */
    REG_SDIN_BITS = (sd->s.csl << SD_CSL_LSHFT )
                  | (sd->s.din << SD_DIN_LSHFT )
                  | (sd->s.clk << SD_CLK_LSHFT );
}



sdc_ret_t sdc_ledset(unsigned long led_color, unsigned long led_state)
{
    unsigned short tmp = 0;

    switch(led_color)
    {   
    default:
    case(SDC_LED_RED):
        tmp = 1 << SD_LEDR_LSHFT ;
        break;
    case(SDC_LED_GREEN):
        tmp = 1 << SD_LEDG_LSHFT;
        break;
    }

    if(led_state == SDC_LED_OFF)
    {
        REG_CPLD_IO = REG_CPLD_IO & (~tmp);
    }
    else
    {
        REG_CPLD_IO = REG_CPLD_IO | tmp;
    }

    return SDC_OK;
}

void sdc_cs_assert(void)
{
    sdcard_t* sd = sc_get_sdcard_buff();
    sd->s.csl = 0;

    //PDR2 = PDR2 & (~(1 << SD_CSL_LSHFT ));
    //REG_SDIN_BITS = (0 << SD_CSL_LSHFT ); // Setting CLK to zero should be OK
    REG_SDIN_BITS = REG_SDIN_BITS & (~(1 << SD_CSL_LSHFT ));
}
void sdc_cs_deassert(void)
{
    sdcard_t* sd = sc_get_sdcard_buff();
    sd->s.csl = 1;

    //PDR2 = PDR2 | (1 << SD_CSL_LSHFT );
    //REG_SDIN_BITS = (1 << SD_CSL_LSHFT );
    REG_SDIN_BITS = REG_SDIN_BITS | (1 << SD_CSL_LSHFT );
}

void sdc_sendbyte_array(unsigned char* ptr, unsigned short len)
{
    sdcard_t* sd = sc_get_sdcard_buff();
    register unsigned char dat = 0;
    register unsigned char tmp = 0;
    // unsigned short bit_din0_clk0, bit_din0_clk1, bit_din1_clk0, bit_din1_clk1;

    // if(sd->s.csl)
    // {
        // bit_din0_clk0 = (0 << SD_DIN_LSHFT) | (0 << SD_CLK_LSHFT) | (1 << SD_CSL_LSHFT);
        // bit_din0_clk1 = (0 << SD_DIN_LSHFT) | (1 << SD_CLK_LSHFT) | (1 << SD_CSL_LSHFT);
        // bit_din1_clk0 = (1 << SD_DIN_LSHFT) | (0 << SD_CLK_LSHFT) | (1 << SD_CSL_LSHFT);
        // bit_din1_clk1 = (1 << SD_DIN_LSHFT) | (1 << SD_CLK_LSHFT) | (1 << SD_CSL_LSHFT);
    // }
    // else
    // {
        // bit_din0_clk0 = (0 << SD_DIN_LSHFT) | (0 << SD_CLK_LSHFT) | (0 << SD_CSL_LSHFT);
        // bit_din0_clk1 = (0 << SD_DIN_LSHFT) | (1 << SD_CLK_LSHFT) | (0 << SD_CSL_LSHFT);
        // bit_din1_clk0 = (1 << SD_DIN_LSHFT) | (0 << SD_CLK_LSHFT) | (0 << SD_CSL_LSHFT);
        // bit_din1_clk1 = (1 << SD_DIN_LSHFT) | (1 << SD_CLK_LSHFT) | (0 << SD_CSL_LSHFT);
    // }


    for(/*nothing*/; len; len--)
    {
        dat = *ptr++;

#if 0 /* Set to 1 if you want to use working, but a little slow routines. */
        /* Send MSB first. */

        /* bit7 */
        if(dat & 0x80) { REG_SDIN_BITS = bit_din1_clk0; REG_SDIN_BITS = bit_din1_clk1;}
        else           { REG_SDIN_BITS = bit_din0_clk0; REG_SDIN_BITS = bit_din0_clk1;}
        /* bit6 */
        if(dat & 0x40) { REG_SDIN_BITS = bit_din1_clk0; REG_SDIN_BITS = bit_din1_clk1;}
        else           { REG_SDIN_BITS = bit_din0_clk0; REG_SDIN_BITS = bit_din0_clk1;}
        /* bit5 */
        if(dat & 0x20) { REG_SDIN_BITS = bit_din1_clk0; REG_SDIN_BITS = bit_din1_clk1;}
        else           { REG_SDIN_BITS = bit_din0_clk0; REG_SDIN_BITS = bit_din0_clk1;}
        /* bit4 */
        if(dat & 0x10) { REG_SDIN_BITS = bit_din1_clk0; REG_SDIN_BITS = bit_din1_clk1;}
        else           { REG_SDIN_BITS = bit_din0_clk0; REG_SDIN_BITS = bit_din0_clk1;}
        /* bit3 */
        if(dat & 0x08) { REG_SDIN_BITS = bit_din1_clk0; REG_SDIN_BITS = bit_din1_clk1;}
        else           { REG_SDIN_BITS = bit_din0_clk0; REG_SDIN_BITS = bit_din0_clk1;}
        /* bit2 */
        if(dat & 0x04) { REG_SDIN_BITS = bit_din1_clk0; REG_SDIN_BITS = bit_din1_clk1;}
        else           { REG_SDIN_BITS = bit_din0_clk0; REG_SDIN_BITS = bit_din0_clk1;}
        /* bit1 */
        if(dat & 0x02) { REG_SDIN_BITS = bit_din1_clk0; REG_SDIN_BITS = bit_din1_clk1;}
        else           { REG_SDIN_BITS = bit_din0_clk0; REG_SDIN_BITS = bit_din0_clk1;}
        /* bit0 */
        if(dat & 0x01) { REG_SDIN_BITS = bit_din1_clk0; REG_SDIN_BITS = bit_din1_clk1;}
        else           { REG_SDIN_BITS = bit_din0_clk0; REG_SDIN_BITS = bit_din0_clk1;}
#else /* Fast routine STT. */
        tmp = dat >> 4;

        /* Send upper nibble. */
        REG_SD_IO_3 = tmp; // DIN=data bit7, CLK='0'
        REG_SD_CLK_SET = 0x01; // CLK='1'

        REG_SD_IO_2 = tmp;
        REG_SD_CLK_SET = 0x01;

        REG_SD_IO_1 = tmp;
        REG_SD_CLK_SET = 0x01;

        REG_SD_IO_0 = tmp;
        REG_SD_CLK_SET = 0x01;

        /* Send lower nibble. */
        REG_SD_IO_3 = dat;
        REG_SD_CLK_SET = 0x01;

        REG_SD_IO_2 = dat;
        REG_SD_CLK_SET = 0x01;

        REG_SD_IO_1 = dat;
        REG_SD_CLK_SET = 0x01;

        REG_SD_IO_0 = dat;
        REG_SD_CLK_SET = 0x01;
#endif /* Fast routine END. */
    }

    /* Update SPI lines states. */
    sd->s.din = (dat & 0x01 ? 1 : 0);
    sd->s.clk = 1;

    //sdc_logout("~ sdc_sendbyte(0x%02X, %3d, %c)", dat, dat, char2pchar(dat)); //TMP
}
void sdc_sendbyte(unsigned char dat)
{
    sdc_sendbyte_array(&dat, 1);
}
void sdc_sendlong(unsigned long dat)
{
    unsigned char buffer[4];

    buffer[0] = (dat >> 24) & 0xFF;
    buffer[1] = (dat >> 16) & 0xFF;
    buffer[2] = (dat >>  8) & 0xFF;
    buffer[3] = (dat >>  0) & 0xFF;

    sdc_sendbyte_array(buffer, 4);

    //sdc_logout("~ sdc_sendlong(0x%08X, %d)", dat, dat);
}

void sdc_receivebyte_array(unsigned char* ptr, unsigned short len)
{
    sdcard_t* sd = sc_get_sdcard_buff();
    register unsigned char tmp;
    register unsigned char tmp1;

    register unsigned short bitrcv1, bitrcv2;
    //unsigned char bitrcv1 = (csl << SD_CSL_LSHFT ) | (1 << SD_DIN_LSHFT ) | (1 << SD_CLK_LSHFT );
    //unsigned char bitrcv2 = (csl << SD_CSL_LSHFT ) | (1 << SD_DIN_LSHFT ) | (0 << SD_CLK_LSHFT );

    if(sd->s.csl)
    {
        bitrcv1 = (1 << SD_DIN_LSHFT) | (1 << SD_CLK_LSHFT) | (1 << SD_CSL_LSHFT);
        bitrcv2 = (1 << SD_DIN_LSHFT) | (0 << SD_CLK_LSHFT) | (1 << SD_CSL_LSHFT);
    }
    else
    {
        bitrcv1 = (1 << SD_DIN_LSHFT) | (1 << SD_CLK_LSHFT) | (0 << SD_CSL_LSHFT);
        bitrcv2 = (1 << SD_DIN_LSHFT) | (0 << SD_CLK_LSHFT) | (0 << SD_CSL_LSHFT);
    }

    /* Setup SPI lines states. */
    sd->s.din = 1;
    sd->s.clk = 1;

    /* Last bit receiption can't be optimized. */
    for(/*nothing*/; len; len--)
    {
        /* Note : since bits1~3 are zero and bits4~7 are nonzero (HW version), 
         * receive data 4bit per 4bit instead of OR-ing with 0x01 on every bit.
         */

        /* Receive first nibble. */
        REG_SDIN_BITS = bitrcv1;
        REG_SDIN_BITS = bitrcv2;
        tmp1 = REG_SD_IO_3;

        REG_SDIN_BITS = bitrcv1;
        REG_SDIN_BITS = bitrcv2;
        tmp1 |= REG_SD_IO_2;

        REG_SDIN_BITS = bitrcv1;
        REG_SDIN_BITS = bitrcv2;
        tmp1 |= REG_SD_IO_1;

        REG_SDIN_BITS = bitrcv1;
        REG_SDIN_BITS = bitrcv2;
        tmp1 |= REG_SD_IO_0;


        /* Receive second nibble. */
        REG_SDIN_BITS = bitrcv1;
        REG_SDIN_BITS = bitrcv2;
        tmp = REG_SD_IO_3;

        REG_SDIN_BITS = bitrcv1;
        REG_SDIN_BITS = bitrcv2;
        tmp |= REG_SD_IO_2;

        REG_SDIN_BITS = bitrcv1;
        REG_SDIN_BITS = bitrcv2;
        tmp |= REG_SD_IO_1;

        REG_SDIN_BITS = bitrcv1;
        REG_SDIN_BITS = bitrcv2;
        tmp |= REG_SD_IO_0;

        /* Store the result in a byte. */
        //tmp &= 0X0F;
        tmp |= tmp1 << 4;

        /* Udpdate data buffer. */
        *ptr++ = tmp;
    }

    /* Update SPI lines states (calling sdc_output is not needed). */
    sd->s.din = 0;
    sd->s.clk = 0;
}

unsigned char sdc_receivebyte(void)
{
    unsigned char dat;
    sdc_receivebyte_array(&dat, 1);
    return dat;
}



/**
 * Fed up with reading the SD card registers contents bit per bit.
 * So I made a function that does it for me. (Millions times faster, and error-free !)
 * Since this stuff is used only in sdc_init function, it won't affect transfer speeds.
**/
unsigned long sdc_slice(unsigned char sz, unsigned char stt, unsigned char end)
{
    sdcard_t* sd = sc_get_sdcard_buff();
    unsigned long ret = 0;
    unsigned char len, i, tmp1, tmp2;

    len  = stt - end + 1;
    stt = (sd->status_len<<3) + sz-1 - stt;
    end = (sd->status_len<<3) + sz-1 - end;

    tmp1 = len-1;
    tmp2 = stt+len;
    for(i=stt; i<tmp2; i++)
    {
        ret |= ( ( sd->stat_reg_buffer[i>>3] >> (7 - (i&0x07)) ) & 0x01 ) << tmp1;
        tmp1--;
    }
    return ret;
}


int sdc_is_reinsert(void)
{
    if(sc_extdev_getid() != SC_EXT_SATCART)
    {
        return 0;
    }

    if(REG_SD_REINSERT & 0x01)
    {
        return 1;
    }

    return 0;
}


sdc_ret_t sdc_init(void)
{
    sdcard_t* sd = sc_get_sdcard_buff();
    int i;
    unsigned char offs;
    unsigned char tmp;
    unsigned long tmpl;
    double tmpd;

    sdc_logout(" [sdc_init]RESET STT", 0);

    memset((void*)sd, 0, sizeof(sdcard_t));

    /* Reset "log to SD card" flag. */
    sd->log_sdcard = 1;

    /* Init version numbers, etc. */
    sd->stats.version_major = SDC_VERSION_MAJOR;
    sd->stats.version_minor = SDC_VERSION_MINOR;

    /* Init IO related stuff. */
    sd->s.csl = 1;
    sd->s.din = 1;
    sd->s.clk = 1;

    sd->inserted = 0;

    sdc_logout(" [sdc_init]RESET END", 0);

    /* Can detect CPLD ? */
    if(sc_extdev_getid() != SC_EXT_SATCART)
    {
        return SDC_NOHARDWARE;
    }

    /* SD card inserted ? */
    if((REG_CPLD_IO & (1<<SD_EJECT_LSHFT)))
    {
        sdc_logout(" [sdc_init]SDC_NOCARD", 0);
        return SDC_NOCARD;
    }

    /* SD card inserted and detected ! */
    sd->inserted = 1;
    /* Reset SD card reinsert flag. */
    REG_SD_REINSERT = 0x01;

    /* Set A bus settings. */
    sdc_a_bus_init();

    /* First, turn on both Red and Green LEDs. */
    sdc_ledset(SDC_LED_GREEN, SDC_LED_ON);
    sdc_ledset(SDC_LED_RED  , SDC_LED_ON);

    /* 74 Clocks while DI & CS high to get it started. */
    sd->s.csl = 1;
    sd->s.din = 1;
    for(i=0; i<74; i++)
    {
        sd->s.clk = 1;
        sdc_output();
        sd->s.clk = 0;
        sdc_output();
    }
    sdc_cs_assert();

    /* Let the card power on and initialize. */
    sc_usleep(10*1000);

    /* Reset - CMD0. */
    sdc_sendpacket(SDC_GO_IDLE_STATE/*CMD0*/, 0, NULL, 1/*blocks_count*/);
    if((sd->packet_timeout)
    || (sd->stat_reg_buffer[0] != 0x01))
    {
        sdc_logout("~sdc::CMD0 failure ! (timeout:%d, R1=0x%02X)", 
            sd->packet_timeout, 
            sd->stat_reg_buffer[0]);

        sdc_cs_deassert();

        /* Turn on Red LED only in the case error happened. */
        sdc_ledset(SDC_LED_GREEN, SDC_LED_OFF);
        sdc_ledset(SDC_LED_RED  , SDC_LED_ON );

        sdc_a_bus_deinit();

        return SDC_FAILURE;
    }


    /* Check - CMD8. */
    sdc_sendpacket(SDC_SEND_IF_COND/*CMD8*/, 0x000001AA, NULL, 1/*blocks_count*/);
    if((sd->packet_timeout)
    || (sd->stat_reg_buffer[0] != 0x01))
    {
        /* Ver 1.x SD card, or MMC Card. */
        sd->sd_ver2 = 0;
    }
    else
    {
        if((sd->stat_reg_buffer[0] == 0x01)
        && (sd->stat_reg_buffer[1] == 0x00)
        && (sd->stat_reg_buffer[2] == 0x00)
        && (sd->stat_reg_buffer[3] == 0x01)
        && (sd->stat_reg_buffer[4] == 0xAA))
        {
            /* Ver 2.x+ SD card. */
            sd->sd_ver2 = 1;
        }
        else
        {
            if(i != 0)
            {
                sdc_logout("~sdc::CMD8 failure ! (timeout:%d, R7=0x%02X %02X%02X%02X%02X)", 
                    sd->packet_timeout, 
                    sd->stat_reg_buffer[0], 
                    sd->stat_reg_buffer[1], sd->stat_reg_buffer[2], sd->stat_reg_buffer[3], sd->stat_reg_buffer[4]);

                sdc_cs_deassert();

                /* Turn on Red LED only in the case error happened. */
                sdc_ledset(SDC_LED_GREEN, SDC_LED_OFF);
                sdc_ledset(SDC_LED_RED  , SDC_LED_ON );

                sdc_a_bus_deinit();

                return SDC_FAILURE;
            }
        }
    }

    /* Loop until Idle - CMD55/41. */
    sd->mmc = 0;
    for(i=0; i<5000; i++)
    {
        if(sd->mmc)
        {
            sdc_sendpacket(SDC_SEND_OP_COND/*CMD1*/, 0x00000000, NULL, 1/*blocks_count*/);
        }
        else
        {
            sdc_sendpacket(SDC_APP_CMD/*CMD55*/, 0, NULL, 1/*blocks_count*/);
            sdc_sendpacket(SDC_SD_SEND_OP_COND/*CMD41*/, sd->sd_ver2 ? 0x40000000 : 0x00000000, NULL, 1/*blocks_count*/);
        }

        /* ACMD41/CMD1 success. */
        if((!sd->packet_timeout)
        && (sd->stat_reg_buffer[0] == 0x00))
        {
            break;
        }

        /* ACMD41 failure on first try ? Retry with CMD1 before giving up ... */
        if((i==0) && (sd->sd_ver2 == 0))
        {
            sd->mmc = 1;
        }
        else
        {
            /* ACMD41/CMD1 failure. */
            if((sd->packet_timeout)
            || (sd->stat_reg_buffer[0] != 0x01))
            {
                sdc_logout("~sdc::ACMD41/CMD1 failure ! (timeout:%d, R1=0x%02X)", 
                    sd->packet_timeout, 
                    sd->stat_reg_buffer[0]);

                sdc_cs_deassert();

                /* Turn on Red LED only in the case error happened. */
                sdc_ledset(SDC_LED_GREEN, SDC_LED_OFF);
                sdc_ledset(SDC_LED_RED  , SDC_LED_ON );

                sdc_a_bus_deinit();

                return SDC_FAILURE;
            }
        }
    }

    /* Read OCR register - CMD58. */
    sdc_sendpacket(SDC_READ_OCR/*CMD58*/, 0, NULL, 1/*blocks_count*/);
    /* Retrieve CCS bit (OCR bit 30).
     *  CCS=0 : card is SDSD.
     *  CCS=1 : SDHC or SDXC.
     * Note : CCS is stored in OCR bit 30, and R1(1byte)+OCR(bit31~8) are stored in sd->stat_reg_buffer.
     */
    sd->ccs = ((sd->stat_reg_buffer[1]) >> 6) & 0x1;


    /* Read CID register - CMD10. */
    // Physical Layer Simplified Specification Version 3.01
    // Part_1_Physical_Layer_Simplified_Specification_Ver_3.01_Final_100518.pdf page 93)
    //5.2 CID register
    //    The Card IDentification (CID) register is 128 bits wide. It contains the card identification information
    //    used during the card identification phase. Every individual Read/Write (RW) card shall have a unique
    //    identification number. The structure of the CID register is defined in the following paragraphs:
    //        Name                  Field Width CID-slice    MEMO
    //        --------------------------------------------------------------------------
    //        Manufacturer ID       MID      8  [127:120]    dat[1]
    //        OEM/Application ID    OID     16  [119:104]    (dat[2] << 8) | dat[3]
    //        Product name          PNM     40  [103:64]     dat[4,5,6,7,8]
    //        Product revision      PRV      8  [63:56]      dat[9]
    //        Product serial number PSN     32  [55:24]      dat[10,11,12,13]
    //        reserved --                    4  [23:20]
    //        Manufacturing date    MDT     12  [19:8]       ((dat[14]&0x0F) << 8) | dat[15]
    //        CRC7 checksum         CRC      7  [7:1]        dat[16]>>1
    //        not used, always 1      -      1  [0:0]
    // MID OID Name
    // 3   SD  SanDisk
    // 2   TM  Kingston / Toshiba
    // 13  KG  KingMax
    // 29  AD  Corsair
    // 54  AO  Crucial
    sdc_sendpacket(SDC_SEND_CID/*CMD10*/, 0, NULL, 1/*blocks_count*/);
    offs = sd->status_len;
    memcpy((void*)(sd->stats.cid), (void*)(sd->stat_reg_buffer+offs), sizeof(sd->stats.cid));

    /* Reset stat infos. */
    sd->stats.sdhc        = 2;
    sd->stats.user_size   = 0;
    sd->stats.file_format = 3;

    /* Read CSD register - CMD9. */
    sdc_sendpacket(SDC_APP_CMD/*CMD55*/, 0, NULL, 1/*blocks_count*/);
    sdc_sendpacket(SDC_SEND_CSD/*CMD9*/, 0, NULL, 1/*blocks_count*/);
    offs = sd->status_len;
    memcpy((void*)(sd->stats.csd), (void*)(sd->stat_reg_buffer+offs), sizeof(sd->stats.csd));
    tmp = (sd->stat_reg_buffer[offs+ 0] >> 6) & 0x03;

    /* Fill stat infos. */
    sd->stats.sdhc = tmp;

    if(tmp == 0)
    { /* SD card (<= 2GB). */
        // SanDisk Secure Digital (SD) Card Product Manual, Rev. 1.9 © 2003 SANDISK CORPORATION
        // ProdManualSDCardv1.9.pdf, page 3-13 (38)
        // Table 3-10. CSD Register
        // Name             Field               Width   Cell Type   CSD-Slice   CSD Value   CSD Code
        // --------------------------------------------------------------------------------------
        // CSD structure    CSD_STRUCTURE        2      R           [127:126]   1.0         00b
        // Reserved         -                    6      R           [125:120]   -           000000b
        // data read        TAAC                 8                  [119:112]
        //  access-time-1       Binary                  R                       1.5msec     00100110b
        //                      MLC                     R                       10msec      00001111b
        // DRAT-2 (CLK)     NSAC                 8      R           [111:104]   0           00000000b
        // max. tr. rate    TRAN_SPEED           8      R           [103: 96]   25MHz       00110010b
        // card command cl. CCC                 12      R           [ 95: 84]   All         1F5h
        // max. read bl len READ_BL_LEN          4      R           [ 83: 80]   512byte     1001b
        // partial bl read  READ_BL_PARTIAL      1      R           [ 79: 79]   Yes         1b
        // write block misa WRITE_BLK_MISALIGN   1      R           [ 78: 78]   No          0b
        // read block misa  READ_BLK_MISALIGN    1      R           [ 77: 77]   No          0b
        // DSR implemented  DSR_IMP              1      R           [ 76: 76]   No          0b
        // Reserved         -                    2      R           [ 75: 74]   -           00b
        // device size      C_SIZE              12      R           [ 73: 62]
        //                                                                      SD128=3843  F03h
        //                                                                      SD064=3807  EDFh
        //                                                                      SD032=1867  74Bh
        //                                                                      SD016=899   383h
        //                                                                      SD008=831   33Fh
        // max. read curr   VDD_R_CURR_MIN       3      R           [ 61: 59]   100mA       111b
        // max. read curr   VDD_R_CURR_MAX       3      R           [ 58: 56]   80mA        110b
        // max. write curr  VDD_W_CURR_MIN       3      R           [ 55: 53]   100mA       111b
        // max. write curr  VDD_W_CURR_MAX       3      R           [ 52: 50]   80mA        110b
        // device size mult C_SIZE_MULT          3      R           [ 49: 47]
        //                                                                      SD128=64    100b
        //                                                                      SD064=32    011b
        //                                                                      SD032=32    011b
        //                                                                      SD016=32    011b
        //                                                                      SD008=16    010b
        //erase sngl bl en  ERASE_BLK_EN         1      R           [ 46: 46]   Yes         1b 
        // erase sector sz  SECTOR_SIZE          7      R           [ 45: 39]   32blocks    0011111b
        // wr prtct grp sz  WP_GRP_SIZE          7      R           [ 38: 32]   128sectors  1111111b
        // wr prtct grp en  WP_GRP_ENABLE        1      R           [ 31: 31]   Yes         1b
        // Reserved for MultiMediaCard compat    2      R           [ 30: 29]   -           00b
        // wr spd factor    R2W_FACTOR           3      R           [ 28: 26]
        //                      Binary                                          X16         100b
        //                      MLC                                             X4          010b
        // max. wr data bl  WRITE_BL_LEN         4      R           [ 25: 22]   512Byte     1001b
        // partial bl wr a  WRITE_BL_PARTIAL     1      R           [ 21: 21]   No          0
        // Reserved         -                    5      R           [ 20: 16]   -           00000b
        // File format grp  FILE_FORMAT_GRP      1      R/W(1)      [ 15: 15]   0           0b
        // copy flag (OTP)  COPY                 1      R/W(1)      [ 14: 14]   Not Origin. 1b
        // perm wr prot     PERM_WRITE_PROTECT   1      R/W(1)      [ 13: 13]   Not Prot.   0b
        // temp wr prot     TMP_WRITE_PROTECT    1      R/W         [ 12: 12]   Not Prot.   0b
        // File format      FILE_FORMAT          2      R/W(1)      [ 11: 10]   HD w/part.  00b
        // Reserved                              2      R/W         [  9:  8]   -           00b
        // CRC              CRC                  7      R/W         [  7:  1]   -           CRC7
        // always E E       -                    1      -           [  0:  0]   -           1b

        unsigned long block_length;
        unsigned long c_size;
        unsigned long c_size_mult;

        tmp   = sdc_slice(128, 83, 80);
        tmpl  = 1 << tmp;
        block_length = tmpl;

        tmpl  = sdc_slice(128, 73, 62);
        tmpl += 1;
        c_size = tmpl;

        tmp  = sdc_slice(128, 49, 47);
        tmpl = 1 << (tmp+2);
        c_size_mult = tmpl;

        /* Fill stat infos. */
        tmpd = (c_size * c_size_mult * block_length) / 1048576;
        tmpl = (unsigned long)(1000*tmpd);
        sd->stats.user_size = tmpl>>10;

        /* Fill stat infos. */
        tmp  = sdc_slice(128, 11, 10);
        sd->stats.file_format = tmp;
    }
    else if(tmp == 1)
    { /* SDHC card. */
        sd->stats.sdhc = 1;

        // Physical Layer Simplified Specification Version 3.01
        // Part_1_Physical_Layer_Simplified_Specification_Ver_3.01_Final_100518.pdf page 115)
        // 5.3.3 CSD Register (CSD Version 2.0)
        //       Table 5-16 shows Definition of the CSD Version 2.0 for the High Capacity SD Memory Card and
        //       Extended Capacity SD Memory Card.
        //       The following sections describe the CSD fields and the relevant data types for SDHC and SDXC Cards.
        //       CSD Version 2.0 is applied to SDHC and SDXC Cards. The field name in parenthesis is set to fixed
        //       value and indicates that the host is not necessary to refer these fields. The fixed values enables host,
        //       which refers to these fields, to keep compatibility to CSD Version 1.0. The Cell Type field is coded as
        //       follows: R = readable, W(1) = writable once, W = multiple writable.
        // 
        //          Name                Field                Width   Value           Cell Type   CSD-slice
        //          --------------------------------------------------------------------------------------
        //          CSD structure       CSD_STRUCTURE        2       01b             R           [127:126]
        //          reserved            -                    6       00 0000b        R           [125:120]
        //          data read acc-tm    TAAC                 8       0Eh             R           [119:112]
        //          DRAT-2 (CLK)        NSAC                 8       00h             R           [111:104]
        //          max. tr. rate       TRAN_SPEED           8       32h/5Ah/0Bh/2Bh R           [103: 96]
        //          card command class. CCC                 12       01x110110101b   R           [ 95: 84]
        //          max. rd data bl len READ_BL_LEN          4       9               R           [ 83: 80]
        //          part. bl rd allowed READ_BL_PARTIAL      1       0               R           [ 79: 79]
        //          write block misalig WRITE_BLK_MISALIGN   1       0               R           [ 78: 78]
        //          read block misalig  READ_BLK_MISALIGN    1       0               R           [ 77: 77]
        //          DSR implemented     DSR_IMP              1       x               R           [ 76: 76]
        //          reserved            -                    6       00 0000b        R           [ 75: 70]
        //          device size         C_SIZE              22       xxxxxxh         R           [ 69: 48]
        //          reserved            -                    1       0               R           [ 47: 47]
        //          erase sngl bl enabl ERASE_BLK_EN         1       1               R           [ 46: 46]
        //          erase sctr size     SECTOR_SIZE          7       7Fh             R           [ 45: 39]
        //          write pr grp size   WP_GRP_SIZE          7       0000000b        R           [ 38: 32]
        //          write pr grp enable WP_GRP_ENABLE        1       0               R           [ 31: 31]
        //          reserved                                 2       00b             R           [ 30: 29]
        //          write speed factor  R2W_FACTOR           3       010b            R           [ 28: 26]
        //          max. wr dat bl len  WRITE_BL_LEN         4       9               R           [ 25: 22]
        //          part bl wrt allowed WRITE_BL_PARTIAL     1       0               R           [ 21: 21]
        //          reserved            -                    5       00000b          R           [ 20: 16]
        //          File format group   FILE_FORMAT_GRP      1       0               R           [ 15: 15]
        //          copy flag           COPY                 1       x               R/W(1)      [ 14: 14]
        //          permanent wrt prot. PERM_WRITE_PROTECT   1       x               R/W(1)      [ 13: 13]
        //          temporary wrt prot. TMP_WRITE_PROTECT    1       x               R/W         [ 12: 12]
        //          File format         FILE_FORMAT          2       00b             R           [ 11: 10]
        //          reserved            -                    2       00b             R           [  9:  8]
        //          CRC                 CRC                  7       xxxxxxxb        R/W         [  7:  1]
        //          not used, always'1' -                    1       1               -           [  0:  0]
        //       Table 5-16: The CSD Register Fields (CSD Version 2.0)

        unsigned long c_size;
        //unsigned long c_size_mult;

        // tmp   = sdc_slice(128, 83, 80);
        // tmpl  = 1 << tmp;
        // block_length = tmpl;

        tmpl  = sdc_slice(128, 69, 48);
        tmpl += 1;
        c_size = tmpl;

        //c_size_mult = 0; // Not defined in SDHC mode ?

        /* Fill stat infos. */
        tmpd = ((c_size + 1) * 524288) / 1048576; tmpl = (unsigned long)(1000*tmpd);
        sd->stats.user_size = tmpl>>10;
    }
    else
    {
        sdc_logout("~sdc::Unknown SD card capacity. Stopped", 0);
        sdc_cs_deassert();

        /* Turn on Red LED only in the case error happened. */
        sdc_ledset(SDC_LED_GREEN, SDC_LED_OFF);
        sdc_ledset(SDC_LED_RED  , SDC_LED_ON );

        sdc_a_bus_deinit();

        return SDC_FAILURE;
    }


    if(sd->sd_ver2)
    {
        /*
         * The initial read/write block length may be set to value
         * different than 512 bytes, 
         * so that the block size should be re-initialized to 512 bytes
         * with CMD16 to work with FAT file system.
         */
        sdc_sendpacket(SDC_SET_BLOCK_LEN/*CMD16*/, SDC_BLOCK_SIZE, NULL, 1/*blocks_count*/);
    }

    sdc_cs_deassert();
    sdc_a_bus_deinit();

    /* Turn off both red and green LEDS to indicate initialization was successful. */
    sdc_ledset(SDC_LED_GREEN, SDC_LED_OFF);
    sdc_ledset(SDC_LED_RED  , SDC_LED_OFF);

    return SDC_OK;
}





unsigned char sdc_sendpacket(unsigned char cmd, unsigned long arg_tmp, unsigned char* buffer, unsigned long blocks_count)
{
    sdcard_t* sd = sc_get_sdcard_buff();
    unsigned long arg = arg_tmp;
    unsigned long pollcnt;
    unsigned long startbit_wait[2]; /* For measuring start bit timings. */
    unsigned short response_len;
    unsigned short i;
    unsigned char tmp;
    unsigned char timeout;
    unsigned long blocks_count_tmp = blocks_count;
    unsigned char* buffer_tmp = buffer;
    unsigned char* rcvptr;
    short rcvcnt;
    unsigned short bitrcv1 = (0 << SD_CSL_LSHFT ) | (1 << SD_DIN_LSHFT ) | (1 << SD_CLK_LSHFT );
    unsigned short bitrcv2 = (0 << SD_CSL_LSHFT ) | (1 << SD_DIN_LSHFT ) | (0 << SD_CLK_LSHFT );

    /* First, set up timer to measure transfer time. */
    sc_tmr_start();

    /* Turn on Green LED when exchanging packet data. */
    sdc_ledset(SDC_LED_GREEN, SDC_LED_ON);

    sd->packet_timeout = 0;
    sd->status_len = 0;
    startbit_wait[0] = 0; startbit_wait[1] = 0;
    sd->duration_usec = 0;
    /* Initialize return value. */
    sd->stat_reg_buffer[0] = 0;


    sdc_cs_assert();


    /* SD card accepts byte address while SDHC accepts block address in multiples of 512
     * so, if it's SD card we need to convert block address into corresponding byte address by 
     * multipying it with 512. which is equivalent to shifting it left 9 times
     * following 'if' loop does that.
     */
    if(sd->ccs == 0)
    {
        if((cmd == SDC_READ_SINGLE_BLOCK     )
        || (cmd == SDC_READ_MULTIPLE_BLOCKS  )
        || (cmd == SDC_WRITE_SINGLE_BLOCK    )
        || (cmd == SDC_WRITE_MULTIPLE_BLOCKS )
        || (cmd == SDC_ERASE_BLOCK_START_ADDR)
        || (cmd == SDC_ERASE_BLOCK_END_ADDR  ))
        {
            arg = arg_tmp << 9;
        }
    }

    /**
     * Send 8 synchronisation clocks before each command.
     * This is needed in order to get some SD cards working.
     * http://www.avrfreaks.net/forum/cannot-access-sdtransflash-card-illegal-command-error
    **/
    sdc_sendbyte(0xFF);

    /* Send 2 start bits and command: `0' `1' `6 bit_cmd'. */
    sdc_sendbyte(0x40 | (cmd & 0x3F));

    /* Send argument. */
    sdc_sendlong(arg);

    /*
     * (Pre)compute CRC7.
     *
     * Note : last bit needs to be equal to 1, 
     *        so (crc << 1) | 1 is actually sent.
     *
     * It is compulsory to send correct CRC for 
     * CMD8 (CRC=0x87) and CMD0 (CRC=0x95).
     * for remaining commands, CRC is ignored in SPI mode.
     */
    switch(cmd)
    {
    case SDC_GO_IDLE_STATE: tmp = 0x95; break; /* CMD0 */
    case SDC_SEND_OP_COND : tmp = 0xF9; break; /* CMD1 */
    case SDC_SEND_IF_COND : tmp = 0x87; break; /* CMD8 */
    /* CMD41 */
    case SDC_SD_SEND_OP_COND:
        tmp = (arg == 0 ? 0xE5 : 0x77/*arg=0x40000000*/);
        break;
    /* Dummy CRC for other commands. */
    default: tmp = 0xFF; break;
    }
    sdc_sendbyte(tmp);

    /* Set Responce Length. */
    switch(cmd)
    {
    /* R1. */
    case(SDC_GO_IDLE_STATE):
    case(SDC_SEND_OP_COND):
    case(SDC_SET_BLOCK_LEN):
    case(SDC_READ_SINGLE_BLOCK):
    case(SDC_READ_MULTIPLE_BLOCKS):
    case(SDC_WRITE_SINGLE_BLOCK):
    case(SDC_WRITE_MULTIPLE_BLOCKS):
    case(SDC_ERASE_BLOCK_START_ADDR):
    case(SDC_ERASE_BLOCK_END_ADDR):
    case(SDC_SD_SEND_OP_COND):
    case(SDC_APP_CMD):
        response_len = 1;
        sd->status_len = response_len;
        break;

    /* R1b. */
    case(SDC_STOP_TRANSMISSION):
    case(SDC_ERASE_SELECTED_BLOCKS):
        response_len = 1;
        sd->status_len = response_len;
        break;

    /* R2. */
    case(SDC_SEND_CSD):
    case(SDC_SEND_CID):
        response_len = 17 + 3;
        sd->status_len = 3;
        break;

    /* R7,R3. */
    case(SDC_SEND_IF_COND):
    case(SDC_READ_OCR):
        response_len = 5;
        sd->status_len = response_len;
        break;

    //case(SDC_SEND_STATUS):
    //    response_len = 512 + 9;
    //    sd->status_len = ???;
    //    break;

    default:
        response_len = 1;
        sd->status_len = response_len;
        sdc_logout("~sdc::cmd%-2d[ERROR] unknown command ! arg=0x%08X(%d), cnt=0x%08X(%d)", 
            cmd, 
            arg, arg, 
            blocks_count, blocks_count);
        break;
    }


    timeout = 1;
    /* Wait for start bit. */
    pollcnt = 0;
    do
    {
        REG_SDIN_BITS = bitrcv1;
        REG_SDIN_BITS = bitrcv2;
        tmp = REG_SDOUT_BIT;

        if(tmp == 0)
        {
            timeout = 0;
            break;
        }
        else
        {
            startbit_wait[0]++; /* Count time to wait for start bit. */
        }
    } while(pollcnt++ < SDC_POLL_COUNTMAX);
    if(timeout)
    {
        /* Update transfer time. */
        sd->duration_usec = sc_tmr_lap_usec();

        sdc_logout("~sdc::cmd%-2d[ERROR] Header start bit reception timeout ! arg=0x%08X(%d), cnt=0x%08X(%d)", 
            cmd, 
            arg, arg, 
            blocks_count, blocks_count);
        sd->packet_timeout = 1;

        /* Turn on Red LED in the case error happened. */
        sdc_ledset(SDC_LED_RED, SDC_LED_ON);

        sdc_cs_deassert();

        return sd->stat_reg_buffer[0];
    }


    /* Receive the Header data. */
    rcvcnt = response_len;
    rcvptr = sd->stat_reg_buffer;
    memset((void*)rcvptr, 0, rcvcnt);

    /* Receive the header first 7 bits, then the remaining bytes. */
    pollcnt = 7;
    for(/*nothing*/; rcvcnt; rcvcnt--)
    {
        tmp = 0;
        for(i = 0; i < pollcnt; i++)
        {
            REG_SDIN_BITS = bitrcv1;
            REG_SDIN_BITS = bitrcv2;
            tmp = (tmp<<1) | REG_SDOUT_BIT;
        }

        *rcvptr++ = tmp;
        pollcnt = 8; /* First bit count is 7, then 8 for the remaining bytes. */
    } // Byte loop

    /* Read single block of data. */
    if((cmd == SDC_READ_SINGLE_BLOCK) || (cmd == SDC_READ_MULTIPLE_BLOCKS))
    {
        response_len = SDC_BLOCK_SIZE * blocks_count;

        while(blocks_count_tmp)
        {
            timeout = 1;
            /* Wait for start bit. */
            pollcnt = 0;
            do
            {
                REG_SDIN_BITS = bitrcv1;
                REG_SDIN_BITS = bitrcv2;
                tmp = REG_SDOUT_BIT;

                if(tmp == 0)
                {
                    timeout = 0;
                    break;
                }
                else
                {
                    startbit_wait[1]++; /* Count time to wait for start bit. */
                }
            } while(pollcnt++ < SDC_POLL_COUNTMAX);
            if(timeout)
            {
                /* Update transfer time. */
                sd->duration_usec = sc_tmr_lap_usec();

                sdc_logout("~sdc::cmd%-2d[ERROR] Data start bit reception timeout ! arg=0x%08X(%d), cnt=0x%08X(%d)", 
                    cmd, 
                    arg, arg, 
                    blocks_count, blocks_count);
                sd->packet_timeout = 1;

                /* Turn on Red LED in the case error happened. */
                sdc_ledset(SDC_LED_RED, SDC_LED_ON);

                sdc_cs_deassert();

                return sd->stat_reg_buffer[0];
            }

            /* Receive the block data. */
            sdc_receivebyte_array(buffer_tmp, SDC_BLOCK_SIZE);

            /* Receive CRC16 (and ignore it). */
            sdc_receivebyte();
            sdc_receivebyte();

            /* Extra 8 clock pulses. */
            sdc_receivebyte();

            blocks_count_tmp--;
            buffer_tmp += SDC_BLOCK_SIZE;
        }

        if(cmd == SDC_READ_SINGLE_BLOCK)
        {
            // Nothing
        }
        else // if(cmd == SDC_READ_MULTIPLE_BLOCKS))
        {
            /* Command to stop transmission. */
            sdc_sendpacket(SDC_STOP_TRANSMISSION, 0, NULL, 1/*blocks_count*/);
        }

        sdc_cs_deassert();
        /* Extra 8 clock pulses. */
        sdc_receivebyte();
    }


    /* Write single block command. */
    if((cmd == SDC_WRITE_SINGLE_BLOCK) || (cmd == SDC_WRITE_MULTIPLE_BLOCKS))
    {
        response_len = SDC_BLOCK_SIZE * blocks_count;

        while(blocks_count_tmp)
        {
            if(cmd == SDC_WRITE_SINGLE_BLOCK)
            {
                /* Send start block token 0xfe (0b11111110). */
                sdc_sendbyte(0xFE);
            }
            else //if(cmd == SDC_WRITE_MULTIPLE_BLOCKS)
            {
                /* Send start block token 0xfe (0b11111100). */
                sdc_sendbyte(0xFC);
            }

            /* Send 512 bytes data. */
            sdc_sendbyte_array(buffer_tmp, SDC_BLOCK_SIZE);

            /* Transmit dummy CRC (16-bit), CRC is ignored here. */
            sdc_sendbyte(0xFF);
            sdc_sendbyte(0xFF);

            tmp = sdc_receivebyte();
            if( (tmp & 0x1f) != 0x05) //response= 0xXXX0AAA1 ; AAA='010' - data accepted
            {                         //AAA='101'-data rejected due to CRC error
                                      //AAA='110'-data rejected due to write error
                /* Update transfer time. */
                sd->duration_usec = sc_tmr_lap_usec();

                sdc_logout("~sdc::cmd%-2d[ERROR] Write data rejected ! tmp=0x%02X arg=0x%08X(%d), cnt=0x%08X(%d)", 
                    cmd, 
                    tmp, 
                    arg, arg, 
                    blocks_count, blocks_count);
                sd->packet_timeout = 1;

                /* Turn on Red LED in the case error happened. */
                sdc_ledset(SDC_LED_RED, SDC_LED_ON);

                sdc_cs_deassert();

                return sd->stat_reg_buffer[0];
            }

            /* wait for SD card to complete writing and get idle. */
            pollcnt = 0;
            while(!sdc_receivebyte())
            {
                startbit_wait[1]++;

                if(pollcnt++ > SDC_POLL_COUNTMAX)
                {
                    /* Update transfer time. */
                    sd->duration_usec = sc_tmr_lap_usec();

                    sdc_logout("~sdc::cmd%-2d[ERROR] Write data timeout #1 ! arg=0x%08X(%d), cnt=0x%08X(%d)", 
                        cmd, 
                        arg, arg, 
                        blocks_count, blocks_count);
                    sd->packet_timeout = 1;

                    /* Turn on Red LED in the case error happened. */
                    sdc_ledset(SDC_LED_RED, SDC_LED_ON);

                    sdc_cs_deassert();

                    return sd->stat_reg_buffer[0];
                }
            }

            if(cmd == SDC_WRITE_SINGLE_BLOCK)
            {
                // Nothing
            }
            else //if(cmd == SDC_WRITE_MULTIPLE_BLOCKS)
            {
                /* Extra 8 bits. */
                sdc_receivebyte();
            }

            blocks_count_tmp--;
            buffer_tmp += SDC_BLOCK_SIZE;
        }

        if(cmd == SDC_WRITE_SINGLE_BLOCK)
        {
            /* Just spend 8 clock cycle delay before reasserting the CS line. */
            sdc_cs_deassert();
            sdc_sendbyte(0xFF);
            /* Re-asserting the CS line to verify if card is still busy. */
            sdc_cs_assert();

            /* wait for SD card to complete writing and get idle. */
            pollcnt = 0;
            while(!sdc_receivebyte())
            {
                startbit_wait[1]++;

                if(pollcnt++ > SDC_POLL_COUNTMAX)
                {
                    /* Update transfer time. */
                    sd->duration_usec = sc_tmr_lap_usec();

                    sdc_logout("~sdc::cmd%-2d[ERROR] Write data timeout #2 ! arg=0x%08X(%d), cnt=0x%08X(%d)", 
                        cmd, 
                        arg, arg, 
                        blocks_count, blocks_count);
                    sd->packet_timeout = 1;

                    /* Turn on Red LED in the case error happened. */
                    sdc_ledset(SDC_LED_RED, SDC_LED_ON);

                    sdc_cs_deassert();

                    return sd->stat_reg_buffer[0];
                }
            }
            sdc_cs_deassert();
        }
        else //if(cmd == SDC_WRITE_MULTIPLE_BLOCKS)
        {
            /* Send 'stop transmission token'. */
            sdc_sendbyte(0xFD);

            /* wait for SD card to complete writing and get idle. */
            pollcnt = 0;
            while(!sdc_receivebyte())
            {
                startbit_wait[1]++;

                if(pollcnt++ > SDC_POLL_COUNTMAX)
                {
                    /* Update transfer time. */
                    sd->duration_usec = sc_tmr_lap_usec();

                    sdc_logout("~sdc::cmd%-2d[ERROR] Write data timeout #2 ! arg=0x%08X(%d), cnt=0x%08X(%d)", 
                        cmd, 
                        arg, arg, 
                        blocks_count, blocks_count);
                    sd->packet_timeout = 1;

                    /* Turn on Red LED in the case error happened. */
                    sdc_ledset(SDC_LED_RED, SDC_LED_ON);

                    sdc_cs_deassert();

                    return sd->stat_reg_buffer[0];
                }
            }

            /* Just spend 8 clock cycle delay before reasserting the CS line. */
            sdc_cs_deassert();
            sdc_sendbyte(0xFF);
            /* Re-asserting the CS line to verify if card is still busy. */
            sdc_cs_assert();

            /* wait for SD card to complete writing and get idle. */
            pollcnt = 0;
            while(!sdc_receivebyte())
            {
                startbit_wait[1]++;

                if(pollcnt++ > SDC_POLL_COUNTMAX)
                {
                    /* Update transfer time. */
                    sd->duration_usec = sc_tmr_lap_usec();

                    sdc_logout("~sdc::cmd%-2d[ERROR] Write data timeout #2 ! arg=0x%08X(%d), cnt=0x%08X(%d)", 
                        cmd, 
                        arg, arg, 
                        blocks_count, blocks_count);
                    sd->packet_timeout = 1;

                    /* Turn on Red LED in the case error happened. */
                    sdc_ledset(SDC_LED_RED, SDC_LED_ON);

                    sdc_cs_deassert();

                    return sd->stat_reg_buffer[0];
                }
            }
            sdc_cs_deassert();
        }
    }

    /* Update transfer time. */
    sd->duration_usec = sc_tmr_lap_usec();

    sdc_logout(" sdc::cmd%-2d, arg=0x%08X, ret=%02X %02X%02X%02X%02X, len=%3d (bl=%2d), stt_wait[%4d][%4d] d=%d", 
        cmd, 
        arg_tmp, 
        sd->stat_reg_buffer[0], 
        sd->stat_reg_buffer[1], sd->stat_reg_buffer[2], sd->stat_reg_buffer[3], sd->stat_reg_buffer[4], 
        response_len, blocks_count, 
        startbit_wait[0], startbit_wait[1], 
        sd->duration_usec);


    /* Packet data exchange finished : turn off Green LED. */
    sdc_ledset(SDC_LED_GREEN, SDC_LED_OFF);

    return sd->stat_reg_buffer[0];
}



unsigned char sdc_read_multiple_blocks(unsigned long start_block, unsigned char* buffer, unsigned long blocks_count)
{
    sdcard_t* sd = sc_get_sdcard_buff();
    unsigned char response = 0;
    unsigned long i;
 
    /* Set A bus settings. */
    sdc_a_bus_init();

    sdc_logout(" sdc_read_multiple_blocks::start_block=0x%08X, blocks_count=0x%08X(%d)", start_block, blocks_count, blocks_count);
    for(i=0; i<blocks_count; i++)
    {
        response = sdc_sendpacket(SDC_READ_SINGLE_BLOCK, start_block+i, buffer+512*i, 1) >> 24;
        if(sd->packet_timeout) return SDC_TIMEOUT;
    }
    //if(response != 0x00) return response; //check for SD status: 0x00 - OK (No flags set)

    sdc_a_bus_deinit();

    return response;
}

unsigned char sdc_write_multiple_blocks(unsigned long start_block, unsigned char* buffer, unsigned long blocks_count)
{
    sdcard_t* sd = sc_get_sdcard_buff();
    unsigned char response = 0;
    unsigned long i;

    /* Set A bus settings. */
    sdc_a_bus_init();

    sdc_logout(" sdc_write_multiple_blocks::start_block=0x%08X, blocks_count=0x%08X(%d)", start_block, blocks_count, blocks_count);
    for(i=0; i<blocks_count; i++)
    {
        response = sdc_sendpacket(SDC_WRITE_SINGLE_BLOCK, start_block+i, buffer+512*i, 1/*cnt*/) >> 24;
        if(sd->packet_timeout) return SDC_TIMEOUT;
    }
    //if(response != 0x00) return response; //check for SD status: 0x00 - OK (No flags set)

    sdc_a_bus_deinit();

    return response;
}



