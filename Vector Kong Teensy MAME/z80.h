

#if defined(__cplusplus) && !defined(USE_CPLUS)
extern "C" {
#endif

#ifdef USE_DRZ80
#include "../drz80/z80.h"
#else

#ifndef Z80_H
#define Z80_H

#include "osd_cpu.h"
#include "cpuintrf.h"

#define NO_Z80_BIG_FLAGS_ARRAY 1

/****************************************************************************/
/* Define a Z80 word. Upper bytes are always zero                           */
/****************************************************************************/
typedef union {
#ifdef __128BIT__
 #ifdef LSB_FIRST
   struct { UINT8 l,h,h2,h3,h4,h5,h6,h7,h8,h9,h10,h11,h12,h13,h14,h15; } B;
   struct { UINT16 l,h,h2,h3,h4,h5,h6,h7; } W;
   UINT32 D;
 #else
   struct { UINT8 h15,h14,h13,h12,h11,h10,h9,h8,h7,h6,h5,h4,h3,h2,h,l; } B;
   struct { UINT16 h7,h6,h5,h4,h3,h2,h,l; } W;
   UINT32 D;
 #endif
#elif __64BIT__
 #ifdef LSB_FIRST
   struct { UINT8 l,h,h2,h3,h4,h5,h6,h7; } B;
   struct { UINT16 l,h,h2,h3; } W;
   UINT32 D;
 #else
   struct { UINT8 h7,h6,h5,h4,h3,h2,h,l; } B;
   struct { UINT16 h3,h2,h,l; } W;
   UINT32 D;
 #endif
#else
 #ifdef LSB_FIRST
   struct { UINT8 l,h,h2,h3; } B;
   struct { UINT16 l,h; } W;
   UINT32 D;
 #else
   struct { UINT8 h3,h2,h,l; } B;
   struct { UINT16 h,l; } W;
   UINT32 D;
 #endif
#endif
} Z80_pair; /* -NS- */

/****************************************************************************/
/* The Z80 registers. HALT is set to 1 when the CPU is halted, the refresh  */
/* register is calculated as follows: refresh=(Regs.R&127)|(Regs.R2&128)    */
/****************************************************************************/
typedef struct {
	Z80_pair	AF,BC,DE,HL,IX,IY,PREPC,PC,SP;
	Z80_pair	AF2,BC2,DE2,HL2;
	unsigned	R;
	UINT8		IFF1,IFF2,HALT,IM,I,R2;

	int vector; 					/* vector for SINGLE INT mode			*/
	int pending_irq;
	int irq_max;					/* number of daisy chain devices		*/
	int request_irq;				/* daisy chain next request device		*/
	int service_irq;				/* daisy chain next reti handling devicve */
	UINT8	irq_state;			/* irq line state */
	int int_state[Z80_MAXDAISY];
	Z80_DaisyChain irq[Z80_MAXDAISY];
#if NEW_INTERRUPT_SYSTEM
	int nmi_state;					/* nmi line state */
	int irq_state;					/* irq line state */
	int (*irq_callback)(int irqline);	/* irq callback */
#endif
	int 	(*irq_callback)(int irqline);
	int 	extra_cycles;		/* extra cycles for interrupts */
}   Z80_Regs;

extern int Z80_ICount;				/* T-state count */

#define Z80_IGNORE_INT	-1			/* Ignore interrupt 					*/
#define Z80_NMI_INT 	-2			/* Execute NMI							*/
#define Z80_IRQ_INT 	-1000		/* Execute IRQ							*/

extern unsigned Z80_GetPC (void);		   /* Get program counter				   */
extern void Z80_GetRegs (Z80_Regs *Regs);  /* Get registers 					   */
extern void Z80_SetRegs (Z80_Regs *Regs);  /* Set registers 					   */
extern void Z80_Reset (Z80_DaisyChain *daisy_chain);
extern int	Z80_Execute(int cycles);	   /* Execute cycles T-States - returns number of cycles actually run */
extern int	Z80_Interrupt(void);
#if NEW_INTERRUPT_SYSTEM
extern void Z80_set_nmi_line(int state);
extern void Z80_set_irq_line(int irqline, int state);
extern void Z80_set_irq_callback(int (*irq_callback)(int));
#else
extern void Z80_Cause_Interrupt(int type);
extern void Z80_Clear_Pending_Interrupts(void);
#endif

enum {
	Z80_TABLE_op,
	Z80_TABLE_cb,
	Z80_TABLE_ed,
	Z80_TABLE_xy,
	Z80_TABLE_xycb,
	Z80_TABLE_ex	/* cycles counts for taken jr/jp/call and interrupt latency (rst opcodes) */
};
#define PAIR Z80_pair
#define b B
#define d D
#define w W
#define logerror
#define LOG
#define CALL_MAME_DEBUG
#define CLEAR_LINE		0		/* clear (a fired, held or pulsed) line */
#define ASSERT_LINE     1       /* assert an interrupt immediately */
#define HOLD_LINE       2       /* hold interrupt line until enable is true */
#define PULSE_LINE		3		/* pulse interrupt line for one instruction */

#endif
#endif

#if defined(__cplusplus) && !defined(USE_CPLUS)
} /* End of extern "C" */
#endif
