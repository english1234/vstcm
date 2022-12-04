#ifndef MAME_CPUINTRF_H
#define MAME_CPUINTRF_H

#define CPU_CONTEXT_SIZE 1500            /* Maximum size that a CPU structre may be */

#include "timer.h"

#if defined(__cplusplus) && !defined(USE_CPLUS)
extern "C" {
#endif

/* ASG 971222 -- added this generic structure */
struct cpu_interface
{
	void (*reset)(void);
	int (*execute)(int cycles);
	void (*set_regs)(void *reg);
	void (*get_regs)(void *reg);
	unsigned int (*get_pc)(void);
	void (*cause_interrupt)(int type);
	void (*clear_pending_interrupts)(void);
	int *icount;
	int no_int, irq_int, nmi_int;

	int (*memory_read)(int offset);
	void (*memory_write)(int offset, int data);
	void (*set_op_base)(int pc);
	int address_bits;
	int abits1, abits2, abitsmin;
};

extern struct cpu_interface cpuintf[];



void cpu_init(void);
void cpu_run(void);

/* optional watchdog */
void watchdog_reset_w(int offset,int data);
int watchdog_reset_r(int offset);
/* Use this function to reset the machine */
void machine_reset(void);
/* Use this function to reset a single CPU */
void cpu_reset(int cpu);

/* Use this function to stop and restart CPUs */
void cpu_halt(int cpunum,int running);
/* This function returns CPUNUM current status (running or halted) */
int  cpu_getstatus(int cpunum);
int cpu_gettotalcpu(void);
int cpu_getactivecpu(void);
void cpu_setactivecpu(int cpunum);

int cpu_getpc(void);
int cpu_getpreviouspc(void);  /* -RAY- */
int cpu_getreturnpc(void);
int cycles_currently_ran(void);
int cycles_left_to_run(void);
/* Returns the number of CPU cycles which take place in one video frame */
int cpu_gettotalcycles(void);
/* Returns the number of CPU cycles before the next interrupt handler call */
int cpu_geticount(void);
/* Returns the number of CPU cycles before the end of the current video frame */
int cpu_getfcount(void);
/* Returns the number of CPU cycles in one video frame */
int cpu_getfperiod(void);
/* Scales a given value by the ratio of fcount / fperiod */
int cpu_scalebyfcount(int value);

/* Returns the current scanline number */
int cpu_getscanline(void);
/* Returns the amount of time until a given scanline */
double cpu_getscanlinetime(int scanline);
/* Returns the duration of a single scanline */
double cpu_getscanlineperiod(void);
/* Returns the duration of a single scanline in cycles */
int cpu_getscanlinecycles(void);
/* Returns the number of cycles since the beginning of this frame */
int cpu_getcurrentcycles(void);
/* Returns the current horizontal beam position in pixels */
int cpu_gethorzbeampos(void);

void cpu_seticount(int cycles);
/*
  Returns the number of times the interrupt handler will be called before
  the end of the current video frame. This is can be useful to interrupt
  handlers to synchronize their operation. If you call this from outside
  an interrupt handler, add 1 to the result, i.e. if it returns 0, it means
  that the interrupt handler will be called once.
*/
int cpu_getiloops(void);
/* Returns the current VBLANK state */
int cpu_getvblank(void);
/* Returns the number of the video frame we are currently playing */
int cpu_getcurrentframe(void);

/* generate a trigger after a specific period of time */
void cpu_triggertime (double duration, int trigger);
/* generate a trigger now */
void cpu_trigger (int trigger);

/* burn CPU cycles until a timer trigger */
void cpu_spinuntil_trigger (int trigger);
/* burn CPU cycles until the next interrupt */
void cpu_spinuntil_int (void);
/* burn CPU cycles until our timeslice is up */
void cpu_spin (void);
/* burn CPU cycles for a specific period of time */
void cpu_spinuntil_time (double duration);

/* yield our timeslice for a specific period of time */
void cpu_yielduntil_trigger (int trigger);
/* yield our timeslice until the next interrupt */
void cpu_yielduntil_int (void);
/* yield our current timeslice */
void cpu_yield (void);
/* yield our timeslice for a specific period of time */
void cpu_yielduntil_time (double duration);

/* cause an interrupt on a CPU */
void cpu_cause_interrupt(int cpu,int type);
void cpu_clear_pending_interrupts(int cpu);
void interrupt_enable_w(int offset,int data);
void interrupt_vector_w(int offset,int data);
int interrupt(void);
int nmi_interrupt(void);
int m68_level1_irq(void);
int m68_level2_irq(void);
int m68_level3_irq(void);
int m68_level4_irq(void);
int m68_level5_irq(void);
int m68_level6_irq(void);
int m68_level7_irq(void);
int ignore_interrupt(void);

void* cpu_getcontext (int _activecpu);
int cpu_is_saving_context(int _activecpu);


/* daisy-chain link */
typedef struct {
    void (*reset)(int);             /* reset callback     */
    int  (*interrupt_entry)(int);   /* entry callback     */
    void (*interrupt_reti)(int);    /* reti callback      */
    int irq_param;                  /* callback paramater */
} Z80_DaisyChain;

#define Z80_MAXDAISY	4		/* maximum of daisy chan device */

#define Z80_INT_REQ     0x01    /* interrupt request mask       */
#define Z80_INT_IEO     0x02    /* interrupt disable mask(IEO)  */

#define Z80_VECTOR(device,state) (((device)<<8)|(state))

void cpu_setdaisychain (int cpunum, Z80_DaisyChain *daisy_chain );

#if defined(__cplusplus) && !defined(USE_CPLUS)
}
#endif
#endif
