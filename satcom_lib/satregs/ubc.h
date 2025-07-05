/*
* Copyright (c) 2012 Israel Jacques
* See LICENSE for details.
*
* Israel Jacques <mrko@eecs.berkeley.edu>
*/

#ifndef SATREGS_UBC_H_
#define SATREGS_UBC_H_


/* 
 * UBC stuff 
 * Doc : http://koti.kapsi.fi/~antime/sega/files/sh7604.pdf, from pdf page 123
 *
 * BBRA : Break bus cycle register A
 * BARA : Break address register A
 * BASRA: Break ASID register A
 * BAMRA: Break address mask register A
 * BBRB : Break bus cycle register B
 * BARB : Break address register B
 * BASRB: Break ASID register B
 * BAMRB: Break address mask register B
 * BDRB : Break data register B
 * BDMRB: Break data mask register B
 * BRCR : Break control register
 *
 * Ch. Register                      Abbr. R/W  Initial   Access   Access
 *                                              Value*1   Size     Address
 * ---------------------------------------------------------------------------
 * A   Break address register A      BARA  R/W Undefined Longword  H'FFFFFFB0
 *     Break ASID register A         BASRA R/W Undefined Byte      H'FFFFFFE4
 *     Break address mask register A BAMRA R/W Undefined Byte      H'FFFFFFB4
 *     Break bus cycle register A    BBRA  R/W H'0000*2  Word      H'FFFFFFB8
 * B   Break address register B      BARB  R/W Undefined Longword  H'FFFFFFA0
 *     Break address mask register B BAMRB R/W Undefined Byte      H'FFFFFFA4
 *     Break ASID register B         BASRB R/W Undefined Byte      H'FFFFFFE8
 *     Break bus cycle register B    BBRB  R/W H'0000*2  Word      H'FFFFFFA8
 *     Break data register B         BDRB  R/W Undefined Longword  H'FFFFFF90
 *     Break data mask register B    BDMRB R/W Undefined Longword  H'FFFFFF94
 *     Both Break control register   BRCR  R/W H'0000*2  Word      H'FFFFFF98
 *
 * Break conditions and register settings are related as follows:
 *     1. Channel A or B and the respective break conditions are set in registers.
 *     2. Addresses are set in BARA and BARB. ASIDs are set in the BASRA and BASRB registers.
 *        The BAMA and BAMB bits of the BAMRA and BAMRB registers set whether to include
 *        addresses in the break conditions or whether to mask them. To include ASIDs in the
 *        conditions, set the BASMA and BASMB bits of the BAMRA and BAMRB registers.
 *     3. The bus cycle break conditions are set in the BBRA and BBRB registers. Instruction fetch or
 *        data access, read or write, and the data access size are set. For an instruction fetch, use the
 *        PCBA and PCBB bits in the BRCR register to set to break before or after the instruction
 *        execution.
 *     4. For channel B, data can be included in the break conditions. Set the data in the BDRB register.
 *        To mask data, set the BDMRB register. Use the DBEB bit of the BRCR register to set whether
 *        to include data in the break conditions.
 *     5. To use channels A and B sequentially, set the SEQ bit in the BRCR register. When set for
 *        sequential use, a user break occurs when the channel A conditions are met and then the channel
 *        B conditions are met.
 *     6. The CMFA and CMFB bits in the BRCR register are set to 1 when a user break occurs. To
 *        generate a break again, set CMFA and CMFB to 0.
 */
/*---------------------- BARA: 0xFFFFFF40 ----------------------*/
#define BARA (*(volatile unsigned long *)(0xFFFFFF40))
#define BARB (*(volatile unsigned long *)(0xFFFFFF60))


/*---------------------- BAMRA: 0xFFFFFF44 ----------------------*/
/* Set to 0h : No BARA bits are masked, entire 32-bit address is included in break conditions
 * Set to 1h : Lower-order 10 bits of BARA are masked
 * Set to 2h : Lower-order 12 bits of BARA are masked
 * Set to 3h : 1 All BARA bits are masked;
 */
#define BAMRA (*(volatile unsigned long *)(0xFFFFFF44))
#define BAMRB (*(volatile unsigned long *)(0xFFFFFF64))


/*---------------------- BBRA: 0xFFFFFF48 (word access) ----------------------*/
#define BBRA (*(volatile unsigned short *)(0xFFFFFF48))
#define BBRB (*(volatile unsigned short *)(0xFFFFFF68))
#define BBR_CPA_NONE			(0 << 6)
#define BBR_CPA_CPU				(1 << 6)
#define BBR_CPA_PER				(2 << 6)

#define BBR_IDA_NONE			(0 << 4)
#define BBR_IDA_INST	 		(1 << 4)
#define BBR_IDA_DATA			(2 << 4)

#define BBR_RWA_NONE			(0 << 2)
#define BBR_RWA_READ	 		(1 << 2)
#define BBR_RWA_WRITE			(2 << 2)

#define BBR_SZA_NONE			(0 << 0)
#define BBR_SZA_BYTE	 		(1 << 0)
#define BBR_SZA_WORD			(2 << 0)
#define BBR_SZA_LONGWORD	 	(3 << 0)

/*---------------------- BDRB/BDMRB: 0xFFFFFF70/74 ---------------------------*/
#define BDRB  (*(volatile unsigned long *)(0xFFFFFF70))
#define BDMRB (*(volatile unsigned long *)(0xFFFFFF74))

/*---------------------- BRCR: 0xFFFFFF78 (word access) ----------------------*/
#define BRCR (*(volatile unsigned short *)(0xFFFFFF78))
#define BRCR_CMFCA				(1 << 15)
#define BRCR_CMFPA				(1 << 14)
#define BRCR_EBBE				(1 << 13)
#define BRCR_UMD				(1 << 12)
#define BRCR_PCBA				(1 << 10)

#define BRCR_CMFCB				(1 << 7)
#define BRCR_CMFPB				(1 << 6)
#define BRCR_SEQ				(1 << 4)
#define BRCR_DBEB				(1 << 3)
#define BRCR_PCBB				(1 << 2)



#endif /* !SATREGS_UBC_H_ */

