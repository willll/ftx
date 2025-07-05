/*
 * Sega Saturn USB flash cart ROM
 * by Anders Montonen, 2012
 *
 * Creative Commons Attribution-ShareAlike 3.0 Unported (CC BY-SA 3.0)
 *
 */
#ifndef SATREGS_SCU_H_
#define SATREGS_SCU_H_

#define SCU_BASE 0x25FE0000

#define D0R     (*(volatile unsigned long*)(SCU_BASE+0x00))
#define D0W     (*(volatile unsigned long*)(SCU_BASE+0x04))
#define D0C     (*(volatile unsigned long*)(SCU_BASE+0x08))
#define D0AD    (*(volatile unsigned long*)(SCU_BASE+0x0c))
#define D0EN    (*(volatile unsigned long*)(SCU_BASE+0x10))
#define D0MD    (*(volatile unsigned long*)(SCU_BASE+0x14))

#define D1R     (*(volatile unsigned long*)(SCU_BASE+0x20))
#define D1W     (*(volatile unsigned long*)(SCU_BASE+0x24))
#define D1C     (*(volatile unsigned long*)(SCU_BASE+0x28))
#define D1AD    (*(volatile unsigned long*)(SCU_BASE+0x2c))
#define D1EN    (*(volatile unsigned long*)(SCU_BASE+0x30))
#define D1MD    (*(volatile unsigned long*)(SCU_BASE+0x34))

#define D2R     (*(volatile unsigned long*)(SCU_BASE+0x40))
#define D2W     (*(volatile unsigned long*)(SCU_BASE+0x44))
#define D2C     (*(volatile unsigned long*)(SCU_BASE+0x48))
#define D2AD    (*(volatile unsigned long*)(SCU_BASE+0x4c))
#define D2EN    (*(volatile unsigned long*)(SCU_BASE+0x50))
#define D2MD    (*(volatile unsigned long*)(SCU_BASE+0x54))

#define DSTP    (*(volatile unsigned long*)(SCU_BASE+0x60))
#define DSTA    (*(volatile unsigned long*)(SCU_BASE+0x7c))

#define PPAF    (*(volatile unsigned long*)(SCU_BASE+0x80))
#define PPD     (*(volatile unsigned long*)(SCU_BASE+0x84))
#define PDA     (*(volatile unsigned long*)(SCU_BASE+0x88))
#define PDD     (*(volatile unsigned long*)(SCU_BASE+0x8c))

#define T0C     (*(volatile unsigned long*)(SCU_BASE+0x90))
#define T1S     (*(volatile unsigned long*)(SCU_BASE+0x94))
#define T1MD    (*(volatile unsigned long*)(SCU_BASE+0x98))

#define IMS     (*(volatile unsigned long*)(SCU_BASE+0xa0))
#define IST     (*(volatile unsigned long*)(SCU_BASE+0xa4))

#define AIACK   (*(volatile unsigned long*)(SCU_BASE+0xa8))
#define ASR0    (*(volatile unsigned long*)(SCU_BASE+0xb0))
#define ASR1    (*(volatile unsigned long*)(SCU_BASE+0xb4))
#define AREF    (*(volatile unsigned long*)(SCU_BASE+0xb8))

#define RSEL    (*(volatile unsigned long*)(SCU_BASE+0xc4))

#define SCU_VER (*(volatile unsigned long*)(SCU_BASE+0xc8))

#endif /* SATREGS_SCU_H_ */
