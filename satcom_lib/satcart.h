/**
 *  SatCom Library
 *  by cafe-alpha.
 *  WWW: http://ppcenter.free.fr/satcom/
 *
 *  See LICENSE file for details.
**/

#ifndef SATCOM_SATCART_H
#define SATCOM_SATCART_H


//---------------------------------------------------------------------------------
// Saturn Development Cartridge definitions.
// 
//---------------------------------------------------------------------------------
// CS0 area : Flash memory and USB related registers
//  0x220x xxxx : RW 1MB flash memory (2 x 512KB flash memory chips used)
//  0x221x xxxx : RW USB module fifo data
//  0x222x xxxx : RW USB module fifo status register
//  0x223x xxxx :    unused
//  0x224x xxxx :    unused
//  0x225x xxxx :    unused
//  0x226x xxxx :    unused
//  0x227x xxxx :    unused
//
//
//---------------------------------------------------------------------------------
// CS1 area : CPLD registers
//  0x2400 00xx : cartridge/CPLD version, SD card related registers, etc
//
// Register map :
//   Offset | Name                | Access | Size  | Description                   
//    (HEX) |                     |        | (bit) |                               
//   =======+=====================+========+=======+===============================
//     01   | REG_CPLD_55         |   R    |   8   | Return 0x55 when read         
//   -------+---------------------+--------+-------+-------------------------------
//     03   | REG_CPLD_AA         |   R    |   8   | Return 0xAA when read         
//   -------+---------------------+--------+-------+-------------------------------
//     05   | REG_CART_CPLD_VER   |   R    |   8   | CPLD version                  
//          | Read details :                                                       
//          | |  bit0-3   : CPLD minor version (hexadecimal format)                
//          | |  bit4-7   : CPLD major version (hexadecimal format)                
//          | CPLD versions :                                                      
//          | |  0x10 : Memory Cart Rev3.1                                        
//          | |  0x11 : Debug carts Rev2.f, Rev3.2                                
//   -------+---------------------+--------+-------+-------------------------------
//     07   | REG_CART_BETA_ID    |   R    |   8   | Cart ID                       
//          | Read details :                                                       
//          | |  bit0-6   : Cart ID                                                
//          | |              00    : Release ID                                    
//          | |              01~7F : unique ID set for each cartridge              
//          | |                      sent for beta testing                         
//          | |               -> 01 : cafe-alpha     (Rev2.f)                      
//          | |               -> 02 : TabajaLabs     (Rev3.1 a) 052M               
//          | |               -> 03 : ElMetek        (Rev3.1)   054M               
//          | |               -> 04 : Stac           (Rev3.1 a) 053M               
//          | |  bit7     : yabause use flag                                       
//   -------+---------------------+--------+-------+-------------------------------
//     09   | REG_CPLD_IO         |  R/W   |   8   | LED/Switch access register    
//          | Read details :                                                       
//          | |  bit0    : green LED status                                        
//          | |  bit1    : red LED status                                          
//          | |  bit4    : SW1 status                                              
//          | |  bit7    : SD card eject status (0:inserted, 1:ejected)            
//          | |  Other   : Reserved - Returns zero                                 
//          | Write details :                                                      
//          | |  bit0    : green LED control                                       
//          | |  bit1    : red LED control                                         
//   -------+---------------------+--------+-------+-------------------------------
//     0B   | REG_SDIN_BITS       |  R/W   |   8   | SD card bitbang register      
//          | Read details :                                                       
//          | |  bit0    : CS  status                                              
//          | |  bit1    : DIN status                                              
//          | |  bit2    : CLK status                                              
//          | |  bit3~7  : Reserved - Returns zero                                 
//          | Write details :                                                      
//          | |  bit0    : CS  control                                             
//          | |  bit1    : DIN control                                             
//          | |  bit2    : CLK control                                             
//   -------+---------------------+--------+-------+-------------------------------
//     0D   | REG_LED_SETTING     |  R/W   |   8   | Green LED display setting     
//          | Note : Debug use, may not be implemented.                            
//          | Read details :                                                       
//          | |  bit0~7  : Display settings                                        
//          | Write details :                                                      
//          | |  bit0    : CS                                                      
//          | |  bit1    : DIN                                                     
//          | |  bit2    : CLK                                                     
//          | |  bit3    : DOUT                                                    
//          | |  bit4    : RD                                                      
//          | |  bit5    : WR0                                                     
//          | |  bit6    : CS0                                                     
//          | |  bit7    : CS1                                                     
//   -------+---------------------+--------+-------+-------------------------------
//     0F   | REG_SD_CLK_SET      |    W   |   8   | Set CLK to specified value    
//          | Read details :                                                       
//          | |  bit0    : CLK status.                                             
//          | |  bit1~7  : Reserved - Returns zero                                 
//          | Write details :                                                      
//          | |  bit0    : CLK control                                             
//   -------+---------------------+--------+-------+-------------------------------
//     11   | REG_SDOUT_BIT       |  R/W   |   8   | SD card bitbang register      
//          | REG_SD_IO_0         |        |       | DIN/DOUT access register #0   
//          | Read details :                                                       
//          | |  bit0    : DOUT value.                                             
//          | |  bit1~7  : Reserved - Returns zero                                 
//          | Write details :                                                      
//          | |  bit0    : Set DIN to bit0 value and CLK to 0.                     
//   -------+---------------------+--------+-------+-------------------------------
//     13   | REG_SD_IO_1         |  R/W   |   8   | DIN/DOUT access register #1   
//          | Read details :                                                       
//          | |  bit0    : Reserved - Returns zero                                 
//          | |  bit1    : DOUT value.                                             
//          | |  bit2~7  : Reserved - Returns zero                                 
//          | Write details :                                                      
//          | |  bit1    : Set DIN to bit1 value and CLK to 0.                     
//   -------+---------------------+--------+-------+-------------------------------
//     15   | REG_SD_IO_2         |  R/W   |   8   | DIN/DOUT access register #2   
//          | Read details :                                                       
//          | |  bit0~1  : Reserved - Returns zero                                 
//          | |  bit2    : DOUT value.                                             
//          | |  bit3~7  : Reserved - Returns zero                                 
//          | Write details :                                                      
//          | |  bit2    : Set DIN to bit2 value and CLK to 0.                     
//   -------+---------------------+--------+-------+-------------------------------
//     17   | REG_SD_IO_3         |  R/W   |   8   | DIN/DOUT access register #3   
//          | Read details :                                                       
//          | |  bit0~2  : Reserved - Returns zero                                 
//          | |  bit3    : DOUT value.                                             
//          | |  bit4~7  : Reserved - Returns zero                                 
//          | Write details :                                                      
//          | |  bit3    : Set DIN to bit3 value and CLK to 0.                     
//   -------+---------------------+--------+-------+-------------------------------
//     19   | REG_SD_REINSERT     |  R/W   |   8   | SD card reinsert register     
//          | Read details :                                                       
//          | |  bit0    : Reinsert status (0:normal, 1:reinsert)                  
//          | |  bit1~7  : Reserved - Returns zero                                 
//          | Write details :                                                      
//          | |  bit0    : 1:Resert reinsert status to normal.                     
//   -------+---------------------+--------+-------+-------------------------------
//
//---------------------------------------------------------------------------------


#define CART_BASE_ADR 0x22000000


/******************************************
 * Flash memory address.
******************************************/
#define CART_FLASH (CART_BASE_ADR + 0)

/**
 * Maximum length allowed for firmware.
 * (limited by LRAM size, because flashing cart
 *  from stream data is not supported)
 */
#define FIRM_MAXLEN (1024*1024)


/******************************************
 * USB dev cart related definitions.
 * From antime's website : www.iki.fi/Anders.Montonen/sega/usbcart/
******************************************/
#define USB_FLAGS (*(volatile unsigned char*)(CART_BASE_ADR + 0x200001))
#define USB_RXF     (1 << 0)
#define USB_TXE     (1 << 1)
#define USB_PWREN   (1 << 7)
#define USB_FIFO (*(volatile unsigned char*)(CART_BASE_ADR + 0x100001))



/******************************************
 * CPLD related definitions.
******************************************/
#define CPLD_BASE_ADDR (0x24000000)

/* 0x55 / 0xAA registers, in order to detect cartridge. */
#define REG_CPLD_FF55 (*(volatile unsigned short*)(CPLD_BASE_ADDR + 0x00))
#define REG_CPLD_FFAA (*(volatile unsigned short*)(CPLD_BASE_ADDR + 0x02))
#define REG_CPLD_55 (*(volatile unsigned char*)(CPLD_BASE_ADDR + 0x01))
#define REG_CPLD_AA (*(volatile unsigned char*)(CPLD_BASE_ADDR + 0x03))

/* Cartridge version IDs. */
#define REG_CART_CPLD_VER (*(volatile unsigned char*)(CPLD_BASE_ADDR + 0x05))
#define REG_CART_BETA_ID  (*(volatile unsigned char*)(CPLD_BASE_ADDR + 0x07))

/* Non-SPI I/O registers. */
#define REG_CPLD_IO (*(volatile unsigned char*)(CPLD_BASE_ADDR + 0x09))
#define SD_LEDG_LSHFT  0
#define SD_LEDR_LSHFT  1
#define SD_SW1_LSHFT   4
#define SD_EJECT_LSHFT 7

/* LED setting register. */
#define REG_LED_SETTING (*(volatile unsigned char*)(CPLD_BASE_ADDR + 0x0D))

/* Saturn->SD SPI registers. */
#define REG_SDIN_BITS  (*(volatile unsigned char*)(CPLD_BASE_ADDR + 0x0B))
#define SD_CSL_LSHFT 0
#define SD_DIN_LSHFT 1
#define SD_CLK_LSHFT 2

#define REG_SD_CLK_SET (*(volatile unsigned char*)(CPLD_BASE_ADDR + 0x0F))

/* SD->Saturn SPI registers. */
#define REG_SDOUT_BIT (*(volatile unsigned char*)(CPLD_BASE_ADDR + 0x11))
#define REG_SD_IO_0   (*(volatile unsigned char*)(CPLD_BASE_ADDR + 0x11))
#define REG_SD_IO_1   (*(volatile unsigned char*)(CPLD_BASE_ADDR + 0x13))
#define REG_SD_IO_2   (*(volatile unsigned char*)(CPLD_BASE_ADDR + 0x15))
#define REG_SD_IO_3   (*(volatile unsigned char*)(CPLD_BASE_ADDR + 0x17))

/* SD card reinsertion detection register. */
#define REG_SD_REINSERT (*(volatile unsigned char*)(CPLD_BASE_ADDR + 0x19))


#endif // SATCOM_SATCART_H

