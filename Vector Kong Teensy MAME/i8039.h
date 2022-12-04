/**************************************************************************
 *                      Intel 8039 Portable Emulator                      *
 *                                                                        *
 *                   Copyright (C) 1997 by Mirko Buffoni                  *
 *  Based on the original work (C) 1997 by Dan Boris, an 8048 emulator    *
 *     You are not allowed to distribute this software commercially       *
 *        Please, notify me, if you make any changes to this file         *
 **************************************************************************/

#ifndef _I8039_H
#define _I8039_H

/**************************************************************************
 * If your compiler doesn't know about inlined functions, uncomment this  *
 **************************************************************************/

/* #define INLINE static */

#ifndef EMU_TYPES_8039
#define EMU_TYPES_8039

/**************************************************************************
 * sizeof(byte)=1, sizeof(word)=2, sizeof(dword)>=4                       *
 **************************************************************************/
/* #define LSB_FIRST  */              /* Compile for low-endian CPU       */

#include "types.h"	/* -NS- */

typedef union
{
#ifdef LSB_FIRST
   struct { byte l,h,h2,h3; } B;
   struct { word w1,w2; } W;
#else
   struct { byte h3,h2,h,l; } B;
   struct { word w2,w1; } W;
#endif
} i8039_pair;

#endif  /* EMUTYPES_8039 */

/**************************************************************************
 * End of machine dependent definitions                                   *
 **************************************************************************/

#ifndef INLINE
#define INLINE static __inline
#endif

typedef struct
{
  i8039_pair PC;	        	/* -NS- */
  byte       A, SP, PSW;
  byte       RAM[128];
  byte       bus, f1;		/* Bus data, and flag1			  */

  int   pending_irq,irq_executing, masterClock, regPtr;
  byte  t_flag, timer, timerON, countON, xirq_en, tirq_en;
  word  A11, A11ff;
} I8039_Regs;

extern   int I8039_ICount;      /* T-state count                          */

#define  I8039_IGNORE_INT   0   /* Ignore interrupt                       */
#define  I8039_EXT_INT     -1   /* Execute a normal extern interrupt      */
#define  I8039_TIMER_INT   -2   /* Execute a Timer interrupt              */
#define  I8039_COUNT_INT   -3   /* Execute a Counter interrupt            */

unsigned I8039_GetPC  (void);             /* Get program counter          */
void     I8039_GetRegs(I8039_Regs *Regs); /* Get registers                */
void     I8039_SetRegs(I8039_Regs *Regs); /* Set registers                */
void     I8039_Reset  (void);             /* Reset processor & registers  */
int      I8039_Execute(int cycles);       /* Execute cycles T-States - returns number of cycles actually run */

void     I8039_Cause_Interrupt(int type);	/* NS 970904 */
void     I8039_Clear_Pending_Interrupts(void);	/* NS 970904 */

/*   This handling of special I/O ports should be better for actual MAME
 *   architecture.  (i.e., define access to ports { I8039_p1, I8039_p1, dkong_out_w })
 */

#if OLDPORTHANDLING
        byte     I8039_port_r(byte port);
        void     I8039_port_w(byte port, byte data);
        byte     I8039_test_r(byte port);
        void     I8039_test_w(byte port, byte data);
        byte     I8039_bus_r(void);
        void     I8039_bus_w(byte data);
#else
        #define  I8039_p0	0x100   /* Not used */
        #define  I8039_p1	0x101
        #define  I8039_p2	0x102
        #define  I8039_p4	0x104
        #define  I8039_p5	0x105
        #define  I8039_p6	0x106
        #define  I8039_p7	0x107
        #define  I8039_t0	0x110
        #define  I8039_t1	0x111
        #define  I8039_bus	0x120
#endif

#include "memory.h"

/*
 *   Input a byte from given I/O port
 */
#define I8039_In(Port) ((byte)cpu_readport(Port))


/*
 *   Output a byte to given I/O port
 */
#define I8039_Out(Port,Value) (cpu_writeport(Port,Value))


/*
 *   Read a byte from given memory location
 */
#define I8039_RDMEM(A) ((unsigned)cpu_readmem16(A))


/*
 *   Write a byte to given memory location
 */
#define I8039_WRMEM(A,V) (cpu_writemem16(A,V))


/*
 *   I8039_RDOP() is identical to I8039_RDMEM() except it is used for reading
 *   opcodes. In case of system with memory mapped I/O, this function can be
 *   used to greatly speed up emulation
 */
#define I8039_RDOP(A) ((unsigned)cpu_readop(A))


/*
 *   I8039_RDOP_ARG() is identical to I8039_RDOP() except it is used for reading
 *   opcode arguments. This difference can be used to support systems that
 *   use different encoding mechanisms for opcodes and opcode arguments
 */
#define I8039_RDOP_ARG(A) ((unsigned)cpu_readop_arg(A))

#endif  /* _I8039_H */
