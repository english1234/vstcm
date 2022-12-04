/***************************************************************************

  cpuintrf.c

  Don't you love MS-DOS 8+3 names? That stands for CPU interface.
  Functions needed to interface the CPU emulator with the other parts of
  the emulation.

***************************************************************************/

#include "driver.h"

#define USE_Z80_GP 1
//#define USE_I8039_GP 1
//#define USE_I8085_GP 1
//#define USE_I86_GP 1
//#define USE_M6502_GP 1
//#define USE_M6809_GP 1
//#define USE_M6808_GP 1
//#define USE_M6805_GP 1


#ifdef USE_Z80_GP
#include "z80.h"
#endif

#ifdef USE_I8039_GP
#include "i8039.h"
#endif

#ifdef USE_I8085_GP
#include "i8085.h"
#endif

#ifdef USE_M6502_GP
#include "m6502.h"
#endif

#ifdef USE_M6809_GP
//#include "m6809.h"
#endif

#ifdef USE_M6808_GP
#include "m6808.h"
#endif

#ifdef USE_M6805_GP
#include "m6805.h"
#endif

#ifdef USE_M68000_GP
#include "m68000/m68000.h"
#endif

#ifdef USE_S2650_GP
#include "s2650/s2650.h"
#endif

#ifdef USE_I86_GP
#include "i86intrf.h"
#endif

#ifdef USE_T11_GP
#include "t11/t11.h"
#endif

#ifdef USE_TMS34010_GP
#include "tms34010/tms34010.h"
#endif

#ifdef USE_TMS9900_GP
#include "tms9900/tms9900.h"
#endif

#ifdef USE_H6280_GP
#include "h6280/h6280.h"
#endif

#ifdef USE_TMS32010_GP
#include "tms32010/tms32010.h"
#endif

#define DUMMY_CPU_GP Dummy_Reset,Dummy_Execute,(void (*)(void *))Dummy_SetRegs,(void (*)(void *))Dummy_GetRegs,Dummy_GetPC,Dummy_Cause_Interrupt,Dummy_Clear_Pending_Interrupts,&dummy_icount,0,-1,-1,cpu_readmem16,cpu_writemem16,cpu_setOPbase16,16,ABITS1_16,ABITS2_16,ABITS_MIN_16

#include "timer.h"

/* these are triggers sent to the timer system for various interrupt events */
#define TRIGGER_TIMESLICE       -1000
#define TRIGGER_INT             -2000
#define TRIGGER_YIELDTIME       -3000
#define TRIGGER_SUSPENDTIME     -4000

struct cpuinfo
{
	struct cpu_interface *intf;                /* pointer to the interface functions */
	int iloops;                                /* number of interrupts remaining this frame */
	int totalcycles;                           /* total CPU cycles executed */
	int vblankint_countdown;                   /* number of vblank callbacks left until we interrupt */
	int vblankint_multiplier;                  /* number of vblank callbacks per interrupt */
	void *vblankint_timer;                     /* reference to elapsed time counter */
	double vblankint_period;                   /* timing period of the VBLANK interrupt */
	void *timedint_timer;                      /* reference to this CPU's timer */
	double timedint_period;                    /* timing period of the timed interrupt */
	int save_context;                          /* need to context switch this CPU? yes or no */
#ifdef linux_alpha
	unsigned char context[CPU_CONTEXT_SIZE] __attribute__ ((__aligned__ (8)));
#else
	unsigned char context[CPU_CONTEXT_SIZE];   /* this CPU's context */
#endif
};

static struct cpuinfo cpu[MAX_CPU];


static int activecpu,totalcpu;
static int running;	/* number of cycles that the CPU emulation was requested to run */
					/* (needed by cpu_getfcount) */
static int have_to_reset;
static int hiscoreloaded;

int previouspc;

static int interrupt_enable[MAX_CPU];
static int interrupt_vector[MAX_CPU];


static int watchdog_counter;

static void *vblank_timer;
static int vblank_countdown;
static int vblank_multiplier;
static double vblank_period;

static void *refresh_timer;
static double refresh_period;
static double refresh_period_inv;

static void *timeslice_timer;
static double timeslice_period;

static double scanline_period;
static double scanline_period_inv;

static int usres; /* removed from cpu_run and made global */
static int vblank;
static int current_frame;

/* static FRANXIS 28-02-2006 */ void cpu_generate_interrupt (int _cpu, int (*func)(void), int num);
static void cpu_vblankintcallback (int param);
static void cpu_timedintcallback (int param);
static void cpu_manualintcallback (int param);
static void cpu_clearintcallback (int param);
static void cpu_resetcallback (int param);
static void cpu_timeslicecallback (int param);
static void cpu_vblankreset (void);
static void cpu_vblankcallback (int param);
static void cpu_updatecallback (int param);
static double cpu_computerate (int value);
static void cpu_inittimers (void);


/* dummy interfaces for non-CPUs */
static int dummy_icount;
static void Dummy_SetRegs(void *Regs);
static void Dummy_GetRegs(void *Regs);
static unsigned Dummy_GetPC(void);
static void Dummy_Reset(void);
static int Dummy_Execute(int cycles);
static void Dummy_Cause_Interrupt(int type);
static void Dummy_Clear_Pending_Interrupts(void);

#ifdef USE_Z80_GP
/* reset function wrapper */
static void Z80_Reset_with_daisychain(void);
/* pointers of daisy chain link */
static Z80_DaisyChain *Z80_daisychain[MAX_CPU];
#endif

/* warning these must match the defines in driver.h! */
struct cpu_interface cpuintf[] =
{
	// Dummy CPU -- placeholder for type 0 
	{
		DUMMY_CPU_GP
	},
	// #define CPU_Z80    1
	{
#ifdef USE_Z80_GP
		Z80_Reset_with_daisychain,         // Reset CPU 
		Z80_Execute,                       // Execute a number of cycles
		(void (*)(void *))Z80_SetRegs,     // Set the contents of the registers
		(void (*)(void *))Z80_GetRegs,     // Get the contents of the registers
		Z80_GetPC,                         // Return the current PC
		Z80_Cause_Interrupt,               // Generate an interrupt
		Z80_Clear_Pending_Interrupts,      // Clear pending interrupts
		(int *)&Z80_ICount,                // Pointer to the instruction count
		Z80_IGNORE_INT,Z80_IRQ_INT,Z80_NMI_INT,  // Interrupt types: none, IRQ, NMI
		cpu_readmem16,                     // Memory read 
		cpu_writemem16,                    // Memory write
		cpu_setOPbase16,                   // Update CPU opcode base
		16,                                // CPU address bits
		ABITS1_16,ABITS2_16,ABITS_MIN_16   // Address bits, for the memory system
#else
		DUMMY_CPU_GP
#endif
	},
	// #define CPU_8085A 2
	{
#ifdef USE_I8085_GP
		I8085_Reset,                        // Reset CPU
		I8085_Execute,                      // Execute a number of cycles
		(void (*)(void *))I8085_SetRegs,    // Set the contents of the registers
		(void (*)(void *))I8085_GetRegs,    // Get the contents of the registers
		(unsigned int (*)(void))I8085_GetPC,// Return the current PC 
		I8085_Cause_Interrupt,              // Generate an interrupt
		I8085_Clear_Pending_Interrupts,     // Clear pending interrupts 
		&I8085_ICount,                      // Pointer to the instruction count
		I8085_NONE,I8085_INTR,I8085_TRAP,   // Interrupt types: none, IRQ, NMI
		cpu_readmem16,                      // Memory read
		cpu_writemem16,                     // Memory write 
		cpu_setOPbase16,                    // Update CPU opcode base
		16,                                 // CPU address bits
		ABITS1_16,ABITS2_16,ABITS_MIN_16    // Address bits, for the memory system 
#else
		DUMMY_CPU_GP
#endif
	},
    // #define CPU_M6502  3 
	{
#ifdef USE_M6502_GP
 		M6502_Reset,			   // Reset CPU
 		M6502_Execute,			   // Execute a number of cycles
 		(void (*)(void *))M6502_SetRegs,   // Set the contents of the registers
 		(void (*)(void *))M6502_GetRegs,   // Get the contents of the registers
 		M6502_GetPC,			   // Return the current PC
 		M6502_Cause_Interrupt,	  	   // Generate an interrupt
 		M6502_Clear_Pending_Interrupts,    // Clear pending interrupts
 		&M6502_ICount,			   // Pointer to the instruction count
 		M6502_INT_NONE,M6502_INT_IRQ,M6502_INT_NMI, // Interrupt types: none, IRQ, NMI
		cpu_readmem16,                     // Memory read 
		cpu_writemem16,                    // Memory write
		cpu_setOPbase16,                   // Update CPU opcode base
		16,                                // CPU address bits
		ABITS1_16,ABITS2_16,ABITS_MIN_16   // Address bits, for the memory system 
#else
		DUMMY_CPU_GP
#endif
	},
    /* #define CPU_I86    4 */
	{
#ifdef USE_I86_GP
		i86_Reset,                         /* Reset CPU */
		i86_Execute,                       /* Execute a number of cycles */
		(void (*)(void *))i86_SetRegs,               /* Set the contents of the registers */
		(void (*)(void *))i86_GetRegs,               /* Get the contents of the registers */
		i86_GetPC,                         /* Return the current PC */
		i86_Cause_Interrupt,               /* Generate an interrupt */
		i86_Clear_Pending_Interrupts,      /* Clear pending interrupts */
		&i86_ICount,                       /* Pointer to the instruction count */
		I86_INT_NONE,I86_NMI_INT,I86_NMI_INT,/* Interrupt types: none, IRQ, NMI */
		cpu_readmem20,                     /* Memory read */
		cpu_writemem20,                    /* Memory write */
		cpu_setOPbase20,                   /* Update CPU opcode base */
		20,                                /* CPU address bits */
		ABITS1_20,ABITS2_20,ABITS_MIN_20   /* Address bits, for the memory system */
#else
		DUMMY_CPU_GP
#endif
	},
	/* #define CPU_I8039  5 */
	{
#ifdef USE_I8039_GP
		I8039_Reset,                       /* Reset CPU */
		I8039_Execute,                     /* Execute a number of cycles */
		(void (*)(void *))I8039_SetRegs,             /* Set the contents of the registers */
		(void (*)(void *))I8039_GetRegs,             /* Get the contents of the registers */
		I8039_GetPC,                       /* Return the current PC */
		I8039_Cause_Interrupt,             /* Generate an interrupt */
		I8039_Clear_Pending_Interrupts,    /* Clear pending interrupts */
		&I8039_ICount,                     /* Pointer to the instruction count */
		I8039_IGNORE_INT,I8039_EXT_INT,-1, /* Interrupt types: none, IRQ, NMI */
		cpu_readmem16,                     /* Memory read */
		cpu_writemem16,                    /* Memory write */
		cpu_setOPbase16,                   /* Update CPU opcode base */
		16,                                /* CPU address bits */
		ABITS1_16,ABITS2_16,ABITS_MIN_16   /* Address bits, for the memory system */
#else
		DUMMY_CPU_GP
#endif
	},
	/* #define CPU_M6808  6 */
	{
#ifdef USE_M6808_GP
		m6808_reset,                       /* Reset CPU */
		m6808_execute,                     /* Execute a number of cycles */
		(void (*)(void *))m6808_SetRegs,             /* Set the contents of the registers */
		(void (*)(void *))m6808_GetRegs,             /* Get the contents of the registers */
		m6808_GetPC,                       /* Return the current PC */
		m6808_Cause_Interrupt,             /* Generate an interrupt */
		m6808_Clear_Pending_Interrupts,    /* Clear pending interrupts */
		&m6808_ICount,                     /* Pointer to the instruction count */
		M6808_INT_NONE,M6808_INT_IRQ,M6808_INT_NMI, /* Interrupt types: none, IRQ, NMI */
		cpu_readmem16,                     /* Memory read */
		cpu_writemem16,                    /* Memory write */
		cpu_setOPbase16,                   /* Update CPU opcode base */
		16,                                /* CPU address bits */
		ABITS1_16,ABITS2_16,ABITS_MIN_16   /* Address bits, for the memory system */
#else
		DUMMY_CPU_GP
#endif
	},
	/* #define CPU_M6805  7 */
	{
#ifdef USE_M6805_GP
		m6805_reset,                       /* Reset CPU */
		m6805_execute,                     /* Execute a number of cycles */
		(void (*)(void *))m6805_SetRegs,             /* Set the contents of the registers */
		(void (*)(void *))m6805_GetRegs,             /* Get the contents of the registers */
		m6805_GetPC,                       /* Return the current PC */
		m6805_Cause_Interrupt,             /* Generate an interrupt */
		m6805_Clear_Pending_Interrupts,    /* Clear pending interrupts */
		&m6805_ICount,                     /* Pointer to the instruction count */
		M6805_INT_NONE,M6805_INT_IRQ,-1,   /* Interrupt types: none, IRQ, NMI */
		cpu_readmem16,                     /* Memory read */
		cpu_writemem16,                    /* Memory write */
		cpu_setOPbase16,                   /* Update CPU opcode base */
		16,                                /* CPU address bits */
		ABITS1_16,ABITS2_16,ABITS_MIN_16   /* Address bits, for the memory system */
#else
		DUMMY_CPU_GP
#endif
	},
	/* #define CPU_M6809  8 */
	{
#ifdef USE_M6809_GP
		m6809_reset,                       /* Reset CPU */
		m6809_execute,                     /* Execute a number of cycles */
		(void (*)(void *))m6809_SetRegs,             /* Set the contents of the registers */
		(void (*)(void *))m6809_GetRegs,             /* Get the contents of the registers */
		m6809_GetPC,                       /* Return the current PC */
		m6809_Cause_Interrupt,             /* Generate an interrupt */
		m6809_Clear_Pending_Interrupts,    /* Clear pending interrupts */
		&m6809_ICount,                     /* Pointer to the instruction count */
		M6809_INT_NONE,M6809_INT_IRQ,M6809_INT_NMI, /* Interrupt types: none, IRQ, NMI */
		cpu_readmem16,                     /* Memory read */
		cpu_writemem16,                    /* Memory write */
		cpu_setOPbase16,                   /* Update CPU opcode base */
		16,                                /* CPU address bits */
		ABITS1_16,ABITS2_16,ABITS_MIN_16   /* Address bits, for the memory system */
#else
		DUMMY_CPU_GP
#endif
	},
	/* #define CPU_M68000 9 */
	{
#ifdef USE_M68000_GP
		MC68000_Reset,                     // Reset CPU
		MC68000_Execute,                   // Execute a number of cycles
		(void (*)(void *))MC68000_SetRegs, // Set the contents of the registers
		(void (*)(void *))MC68000_GetRegs, // Get the contents of the registers
		(unsigned int (*)(void))MC68000_GetPC, // Return the current PC
		MC68000_Cause_Interrupt,           // Generate an interrupt
		MC68000_Clear_Pending_Interrupts,  // Clear pending interrupts
		&MC68000_ICount,                   // Pointer to the instruction count
		MC68000_INT_NONE,-1,-1,            // Interrupt types: none, IRQ, NMI
		cpu_readmem24,                     // Memory read
		cpu_writemem24,                    // Memory write
		cpu_setOPbase24,                   // Update CPU opcode base
		24,                                // CPU address bits
		ABITS1_24,ABITS2_24,ABITS_MIN_24   // Address bits, for the memory system
#else
		DUMMY_CPU_GP
#endif
	},
	/* #define CPU_T11  10 */
	{
#ifdef USE_T11_GP
		t11_reset,                       /* Reset CPU */
		t11_execute,                     /* Execute a number of cycles */
		(void (*)(void *))t11_SetRegs,             /* Set the contents of the registers */
		(void (*)(void *))t11_GetRegs,             /* Get the contents of the registers */
		t11_GetPC,                       /* Return the current PC */
		t11_Cause_Interrupt,             /* Generate an interrupt */
		t11_Clear_Pending_Interrupts,    /* Clear pending interrupts */
		&t11_ICount,                     /* Pointer to the instruction count */
		T11_INT_NONE,-1,-1,                /* Interrupt types: none, IRQ, NMI */
		cpu_readmem16lew,                  /* Memory read */
		cpu_writemem16lew,                 /* Memory write */
		cpu_setOPbase16lew,                /* Update CPU opcode base */
		16,                                /* CPU address bits */
		ABITS1_16LEW,ABITS2_16LEW,ABITS_MIN_16LEW /* Address bits, for the memory system */
#else
		DUMMY_CPU_GP
#endif
	},
	/* #define CPU_S2650 11 */
	{
#ifdef USE_S2650_GP
		S2650_Reset,						/* Reset CPU */
		S2650_Execute,						/* Execute a number of cycles */
		(void (*)(void *))S2650_SetRegs,	/* Set the contents of the registers */
		(void (*)(void *))S2650_GetRegs,	/* Get the contents of the registers */
		(unsigned int (*)(void))S2650_GetPC,/* Return the current PC */
		S2650_Cause_Interrupt,				/* Generate an interrupt */
		S2650_Clear_Pending_Interrupts, 	/* Clear pending interrupts */
		&S2650_ICount,						/* Pointer to the instruction count */
		S2650_INT_NONE,-1,-1,				/* Interrupt types: none, IRQ, NMI */
		cpu_readmem16,                      /* Memory read */
		cpu_writemem16,                     /* Memory write */
		cpu_setOPbase16,                    /* Update CPU opcode base */
		16, 								/* CPU address bits */
		ABITS1_16,ABITS2_16,ABITS_MIN_16	/* Address bits, for the memory system */
#else
		DUMMY_CPU_GP
#endif
	},
	/* #define CPU_TMS34010 12 */
	{
#ifdef USE_TMS34010_GP
		TMS34010_Reset,                     /* Reset CPU */
		TMS34010_Execute,                   /* Execute a number of cycles */
		(void (*)(void *))TMS34010_SetRegs,           /* Set the contents of the registers */
		(void (*)(void *))TMS34010_GetRegs,           /* Get the contents of the registers */
		(unsigned int (*)(void))TMS34010_GetPC,             /* Return the current PC */
		TMS34010_Cause_Interrupt,           /* Generate an interrupt */
		TMS34010_Clear_Pending_Interrupts,  /* Clear pending interrupts */
		&TMS34010_ICount,                   /* Pointer to the instruction count */
		TMS34010_INT_NONE,-1,-1,            /* Interrupt types: none, IRQ, NMI */
		cpu_readmem29,                     /* Memory read */
		cpu_writemem29,                    /* Memory write */
		cpu_setOPbase29,                   /* Update CPU opcode base */
		29,                                /* CPU address bits */
		ABITS1_29,ABITS2_29,ABITS_MIN_29   /* Address bits, for the memory system */
#else
		DUMMY_CPU_GP
#endif
	},
	/* #define CPU_TMS9900 13 */
	{
#ifdef USE_TMS9900_GP
		TMS9900_Reset,                          /* Reset CPU */
		TMS9900_Execute,                        /* Execute a number of cycles */
		(void (*)(void *))TMS9900_SetRegs,      /* Set the contents of the registers */
		(void (*)(void *))TMS9900_GetRegs,      /* Get the contents of the registers */
		(unsigned int (*)(void))TMS9900_GetPC,  /* Return the current PC */
		TMS9900_Cause_Interrupt,                /* Generate an interrupt */
		TMS9900_Clear_Pending_Interrupts,       /* Clear pending interrupts */
		&TMS9900_ICount,                        /* Pointer to the instruction count */
		TMS9900_NONE,-1,-1,			/* Interrupt types: none, IRQ, NMI */
		cpu_readmem16,                          /* Memory read */
		cpu_writemem16,                         /* Memory write */
		cpu_setOPbase16,                        /* Update CPU opcode base */
		16,                                     /* CPU address bits */
		ABITS1_16,ABITS2_16,ABITS_MIN_16        /* Address bits, for the memory system */
#else
		DUMMY_CPU_GP
#endif
	},
        /* #define CPU_H6280 14 */
        {
#ifdef USE_H6280_GP
                H6280_Reset,			/* Reset CPU */
                H6280_Execute,			/* Execute a number of cycles */
                (void (*)(void *))H6280_SetRegs,/* Set the contents of the registers */
                (void (*)(void *))H6280_GetRegs,/* Get the contents of the registers */
                H6280_GetPC,			/* Return the current PC */
                H6280_Cause_Interrupt,		/* Generate an interrupt */
                H6280_Clear_Pending_Interrupts,	/* Clear pending interrupts */
                &H6280_ICount,			/* Pointer to the instruction count */
                H6280_INT_NONE,-1,H6280_INT_NMI,/* Interrupt types: none, IRQ, NMI */
                cpu_readmem21,			/* Memory read */
                cpu_writemem21,			/* Memory write */
                cpu_setOPbase21,		/* Update CPU opcode base */
                21,				/* CPU address bits */
                ABITS1_21,ABITS2_21,ABITS_MIN_21/* Address bits, for the memory system */
#else
		DUMMY_CPU_GP
#endif
	},
        /* #define CPU_TMS320C10 15 */
        {
#ifdef USE_TMS32010_GP
                TMS320C10_Reset,                                        /* Reset CPU */
                TMS320C10_Execute,                                      /* Execute a number of cycles */
                (void (*)(void *))TMS320C10_SetRegs,/* Set the contents of the registers */
                (void (*)(void *))TMS320C10_GetRegs,/* Get the contents of the registers */
                TMS320C10_GetPC,                                        /* Return the current PC */
                TMS320C10_Cause_Interrupts,             /* Generate an interrupt */
                TMS320C10_Clear_Pending_Interrupts, /* Clear pending interrupts */
                &TMS320C10_ICount,                                      /* Pointer to the instruction count */
                TMS320C10_INT_NONE,-1,-1,                       /* Interrupt types: none, IRQ, NMI */
                cpu_readmem16,                                          /* Memory read */
                cpu_writemem16,                                         /* Memory write */
                cpu_setOPbase16,                                        /* Update CPU opcode base */
                16,                                                             /* CPU address bits */
                ABITS1_16,ABITS2_16,ABITS_MIN_16        /* Address bits, for the memory system */
#else
                DUMMY_CPU_GP
#endif
	}
};

/* Convenience macros - not in cpuintrf.h because they shouldn't be used by everyone */
#define RESET(index)                    ((*cpu[index].intf->reset)())
#define EXECUTE(index,cycles)           ((*cpu[index].intf->execute)(cycles))
#define SETREGS(index,regs)             ((*cpu[index].intf->set_regs)(regs))
#define GETREGS(index,regs)             ((*cpu[index].intf->get_regs)(regs))
#define GETPC(index)                    ((*cpu[index].intf->get_pc)())
#define CAUSE_INTERRUPT(index,type)     ((*cpu[index].intf->cause_interrupt)(type))
#define CLEAR_PENDING_INTERRUPTS(index) ((*cpu[index].intf->clear_pending_interrupts)())
#define ICOUNT(index)                   (*cpu[index].intf->icount)
#define INT_TYPE_NONE(index)            (cpu[index].intf->no_int)
#define INT_TYPE_IRQ(index)             (cpu[index].intf->irq_int)
#define INT_TYPE_NMI(index)             (cpu[index].intf->nmi_int)

#define SET_OP_BASE(index,pc)           ((*cpu[index].intf->set_op_base)(pc))


void cpu_init(void)
{
	int i;

	/* count how many CPUs we have to emulate */
	totalcpu = 0;

	while (totalcpu < MAX_CPU)
	{
		if (Machine->drv->cpu[totalcpu].cpu_type == 0) break;
		totalcpu++;
	}

	/* zap the CPU data structure */
	memset (cpu, 0, sizeof (cpu));

	/* set up the interface functions */
	for (i = 0; i < MAX_CPU; i++)
		cpu[i].intf = &cpuintf[Machine->drv->cpu[i].cpu_type & ~CPU_FLAGS_MASK];
#ifdef USE_Z80_GP
	for (i = 0; i < MAX_CPU; i++)
		Z80_daisychain[i] = 0;
#endif

	/* reset the timer system */
	timer_init ();
	timeslice_timer = refresh_timer = vblank_timer = NULL;
}



void cpu_run(void)
{
	int i, cpunum=0;

	/* determine which CPUs need a context switch */
	for (i = 0; i < totalcpu; i++)
	{
		int j;
#ifdef PROFILER_MAME4ALL
		{
			switch (Machine->drv->cpu[i].cpu_type & ~CPU_FLAGS_MASK)
			{
				case CPU_Z80: mame4all_prof_setmsg(i+2,"Z80"); break;
				case CPU_8080: mame4all_prof_setmsg(i+2,"8080"); break;
				case CPU_M6502:  mame4all_prof_setmsg(i+2,"M6502"); break;
				case CPU_I86:  mame4all_prof_setmsg(i+2,"I86 "); break;
				case CPU_I8035:  mame4all_prof_setmsg(i+2,"I8035"); break;
				case CPU_M6803:  mame4all_prof_setmsg(i+2,"M6803"); break;
				case CPU_M6805:  mame4all_prof_setmsg(i+2,"M6805"); break;
				case CPU_M6809:  mame4all_prof_setmsg(i+2,"M6809"); break;
				case CPU_M68000:  mame4all_prof_setmsg(i+2,"M68000"); break;
				case CPU_T11:  mame4all_prof_setmsg(i+2,"T11"); break;
				case CPU_S2650:  mame4all_prof_setmsg(i+2,"S2650"); break;
				case CPU_TMS34010:  mame4all_prof_setmsg(i+2,"TMS34010"); break;
				case CPU_TMS9900:  mame4all_prof_setmsg(i+2,"TMS9900"); break;
				case CPU_H6280:  mame4all_prof_setmsg(i+2,"H6280"); break;
			}
		}
#endif
		/* Save if there is another CPU of the same type */
		cpu[i].save_context = 0;

		for (j = 0; j < totalcpu; j++)
			if (i != j && (Machine->drv->cpu[i].cpu_type & ~CPU_FLAGS_MASK) == (Machine->drv->cpu[j].cpu_type & ~CPU_FLAGS_MASK))
				cpu[i].save_context = 1;

	}

reset:
	/* initialize the various timers (suspends all CPUs at startup) */
	cpu_inittimers ();
	watchdog_counter = -1;

	/* enable all CPUs (except for audio CPUs if the sound is off) */
	for (i = 0; i < totalcpu; i++)
		if (!(Machine->drv->cpu[i].cpu_type & CPU_AUDIO_CPU) || Machine->sample_rate != 0)
			timer_suspendcpu (i, 0);

	have_to_reset = 0;
	hiscoreloaded = 0;
	vblank = 0;

	/* start with interrupts enabled, so the generic routine will work even if */
	/* the machine doesn't have an interrupt enable port */
	for (i = 0;i < MAX_CPU;i++)
	{
		interrupt_enable[i] = 1;
		interrupt_vector[i] = 0xff;
	}

	/* do this AFTER the above so init_machine() can use cpu_halt() to hold the */
	/* execution of some CPUs, or disable interrupts */
	if (Machine->drv->init_machine) (*Machine->drv->init_machine)();

	/* reset each CPU */
	for (i = 0; i < totalcpu; i++)
	{
		/* swap memory contexts and reset */
		memorycontextswap (i);
		if (cpu[i].save_context) SETREGS (i, cpu[i].context);
		activecpu = i;
		RESET (i);
		/* save the CPU context if necessary */
		if (cpu[i].save_context) GETREGS (i, cpu[i].context);

		/* reset the total number of cycles */
		cpu[i].totalcycles = 0;
	}

	/* reset the globals */
	cpu_vblankreset ();
	current_frame = 0;

#ifdef PROFILER_MAME4ALL
	mame4all_prof_start(0);
#endif
	/* loop until the user quits */
	usres = 0;
	while (usres == 0)
	{
	
		/* was machine_reset() called? */
		if (have_to_reset) goto reset;

		/* ask the timer system to schedule */
		if (timer_schedule_cpu (&cpunum, &running))
		{
			int ran;

			/* switch memory and CPU contexts */
			activecpu = cpunum;
			memorycontextswap (activecpu);
			if (cpu[activecpu].save_context) SETREGS (activecpu, cpu[activecpu].context);

			/* make sure any bank switching is reset */
			SET_OP_BASE (activecpu, GETPC (activecpu));
#ifdef PROFILER_MAME4ALL
			mame4all_prof_start(activecpu+2);
#endif            
			/* run for the requested number of cycles */
			ran = EXECUTE (activecpu, running);
#ifdef PROFILER_MAME4ALL
			mame4all_prof_end(activecpu+2);
#endif
			/* update based on how many cycles we really ran */
			cpu[activecpu].totalcycles += ran;

			/* update the contexts */
			if (cpu[activecpu].save_context) GETREGS (activecpu, cpu[activecpu].context);
			updatememorybase (activecpu);
			activecpu = -1;

			/* update the timer with how long we actually ran */
			timer_update_cpu (cpunum, ran);
		}

	}

#ifdef PROFILER_MAME4ALL
	mame4all_prof_end(0);
#endif
	/* write hi scores to disk - No scores saving if cheat */
	if (hiscoreloaded != 0 && Machine->gamedrv->hiscore_save)
		(*Machine->gamedrv->hiscore_save)();
}




/***************************************************************************

  Use this function to initialize, and later maintain, the watchdog. For
  convenience, when the machine is reset, the watchdog is disabled. If you
  call this function, the watchdog is initialized, and from that point
  onwards, if you don't call it at least once every 10 video frames, the
  machine will be reset.

***************************************************************************/
void watchdog_reset_w(int offset,int data)
{
	watchdog_counter = Machine->drv->frames_per_second;
}

int watchdog_reset_r(int offset)
{
	watchdog_counter = Machine->drv->frames_per_second;
	return 0;
}



/***************************************************************************

  This function resets the machine (the reset will not take place
  immediately, it will be performed at the end of the active CPU's time
  slice)

***************************************************************************/
void machine_reset(void)
{
	/* write hi scores to disk - No scores saving if cheat */
	if (hiscoreloaded != 0 && Machine->gamedrv->hiscore_save)
		(*Machine->gamedrv->hiscore_save)();

	hiscoreloaded = 0;

	have_to_reset = 1;
}



/***************************************************************************

  Use this function to reset a specified CPU immediately

***************************************************************************/
void cpu_reset(int cpunum)
{
	timer_set (TIME_NOW, cpunum, cpu_resetcallback);
}


/***************************************************************************

  Use this function to stop and restart CPUs

***************************************************************************/
void cpu_halt(int cpunum,int _running)
{
	if (cpunum >= MAX_CPU) return;

	/* don't resume audio CPUs if sound is disabled */
	if (!(Machine->drv->cpu[cpunum].cpu_type & CPU_AUDIO_CPU) || Machine->sample_rate != 0)
		timer_suspendcpu (cpunum, !_running);
}



/***************************************************************************

  This function returns CPUNUM current status  (running or halted)

***************************************************************************/
int cpu_getstatus(int cpunum)
{
	if (cpunum >= MAX_CPU) return 0;

	return !timer_iscpususpended (cpunum);
}



int cpu_getactivecpu(void)
{
	int cpunum = (activecpu < 0) ? 0 : activecpu;
	return cpunum;
}

void cpu_setactivecpu(int cpunum)
{
	activecpu = cpunum;
}

int cpu_gettotalcpu(void)
{
	return totalcpu;
}



int cpu_getpc(void)
{
	int cpunum = (activecpu < 0) ? 0 : activecpu;
	return GETPC (cpunum);
}


/***************************************************************************

  This is similar to cpu_getpc(), but instead of returning the current PC,
  it returns the address of the opcode that is doing the read/write. The PC
  has already been incremented by some unknown amount by the time the actual
  read or write is being executed. This helps to figure out what opcode is
  actually doing the reading or writing, and therefore the amount of cycles
  it's taking. The Missile Command driver needs to know this.

  WARNING: this function might return -1, meaning that there isn't a valid
  previouspc (e.g. a memory push caused by an interrupt).

***************************************************************************/
int cpu_getpreviouspc(void)  /* -RAY- */
{
	int cpunum = (activecpu < 0) ? 0 : activecpu;

	switch(Machine->drv->cpu[cpunum].cpu_type & ~CPU_FLAGS_MASK)
	{
#ifdef USE_M68000_GP
#ifdef USE_CYCLONE
		case CPU_M68000:	/* notaz */
			return (cyclone.prev_pc - cyclone.membase - 2) & 0xffffff;
#else
		case CPU_M68000:	/* ASG 980413 */
			return previouspc;
#endif
#endif

#ifdef USE_Z80_GP
#ifdef USE_DRZ80
		case CPU_Z80:
			if (previouspc==-1)
				return -1;
			else
				return Z80_GetPC();
#else
		case CPU_Z80:
#endif
#endif
#ifdef USE_I8039_GP
		case CPU_I8039:
#endif
#ifdef USE_M6502_GP
		case CPU_M6502:
#endif
#ifdef USE_M6809_GP
		case CPU_M6809:
#endif
#ifdef USE_H6280_GP
		case CPU_H6280:
#endif
#ifdef USE_TMS32010_GP
		case CPU_TMS320C10:
#endif
			return previouspc;
			break;

		default:
			return -1;
			break;
	}
}








/***************************************************************************

  This is similar to cpu_getpc(), but instead of returning the current PC,
  it returns the address stored on the top of the stack, which usually is
  the address where execution will resume after the current subroutine.
  Note that the returned value will be wrong if the program has PUSHed
  registers on the stack.

***************************************************************************/
int cpu_getreturnpc(void)
{
	int cpunum = (activecpu < 0) ? 0 : activecpu;

	switch(Machine->drv->cpu[cpunum].cpu_type & ~CPU_FLAGS_MASK)
	{
#ifdef USE_Z80_GP
		case CPU_Z80:
			{
				Z80_Regs _regs;
				extern unsigned char *RAM;


				Z80_GetRegs(&_regs);
				#ifndef USE_DRZ80 /* FRANXIS 01-09-2005 */
					return RAM[_regs.SP.D] + (RAM[_regs.SP.D+1] << 8);
				#else
					return RAM[_regs.Z80SP-_regs.Z80SP_BASE] +
						(RAM[_regs.Z80SP-_regs.Z80SP_BASE+1] << 8);
				#endif
			}
			break;
#endif

		default:
			return -1;
			break;
	}
}


/* these are available externally, for the timer system */
int cycles_currently_ran(void)
{
	int cpunum = (activecpu < 0) ? 0 : activecpu;
	return running - ICOUNT (cpunum);
}

int cycles_left_to_run(void)
{
	int cpunum = (activecpu < 0) ? 0 : activecpu;
	return ICOUNT (cpunum);
}



/***************************************************************************

  Returns the number of CPU cycles since the last reset of the CPU

  IMPORTANT: this value wraps around in a relatively short time.
  For example, for a 6Mhz CPU, it will wrap around in
  2^32/6000000 = 716 seconds = 12 minutes.
  Make sure you don't do comparisons between values returned by this
  function, but only use the difference (which will be correct regardless
  of wraparound).

***************************************************************************/
int cpu_gettotalcycles(void)
{
	int cpunum = (activecpu < 0) ? 0 : activecpu;
	return cpu[cpunum].totalcycles + cycles_currently_ran();
}



/***************************************************************************

  Returns the number of CPU cycles before the next interrupt handler call

***************************************************************************/
int cpu_geticount(void)
{
	int cpunum = (activecpu < 0) ? 0 : activecpu;
	int result = TIME_TO_CYCLES (cpunum, cpu[cpunum].vblankint_period - timer_timeelapsed (cpu[cpunum].vblankint_timer));
	return (result < 0) ? 0 : result;
}



/***************************************************************************

  Returns the number of CPU cycles before the end of the current video frame

***************************************************************************/
int cpu_getfcount(void)
{
	int cpunum = (activecpu < 0) ? 0 : activecpu;
	int result = TIME_TO_CYCLES (cpunum, refresh_period - timer_timeelapsed (refresh_timer));
	return (result < 0) ? 0 : result;
}



/***************************************************************************

  Returns the number of CPU cycles in one video frame

***************************************************************************/
int cpu_getfperiod(void)
{
	int cpunum = (activecpu < 0) ? 0 : activecpu;
	return TIME_TO_CYCLES (cpunum, refresh_period);
}



/***************************************************************************

  Scales a given value by the ratio of fcount / fperiod

***************************************************************************/
int cpu_scalebyfcount(int value)
{
	int result = (int)((double)value * timer_timeelapsed (refresh_timer) * refresh_period_inv);
	if (value >= 0) return (result < value) ? result : value;
	else return (result > value) ? result : value;
}



/***************************************************************************

  Returns the current scanline, or the time until a specific scanline

  Note: cpu_getscanline() counts from 0, 0 being the first visible line. You
  might have to adjust this value to match the hardware, since in many cases
  the first visible line is >0.

***************************************************************************/
int cpu_getscanline(void)
{
	return (int)(timer_timeelapsed (refresh_timer) * scanline_period_inv);
}


double cpu_getscanlinetime(int scanline)
{
	double scantime = timer_starttime (refresh_timer) + (double)scanline * scanline_period;
	double time = timer_get_time ();
	if (time >= scantime) scantime += TIME_IN_HZ (Machine->drv->frames_per_second);
	return scantime - time;
}


double cpu_getscanlineperiod(void)
{
	return scanline_period;
}


/***************************************************************************

  Returns the number of cycles in a scanline

 ***************************************************************************/
int cpu_getscanlinecycles(void)
{
	int cpunum = (activecpu < 0) ? 0 : activecpu;
	return TIME_TO_CYCLES (cpunum, scanline_period);
}


/***************************************************************************

  Returns the number of cycles since the beginning of this frame

 ***************************************************************************/
int cpu_getcurrentcycles(void)
{
	int cpunum = (activecpu < 0) ? 0 : activecpu;
	return TIME_TO_CYCLES (cpunum, timer_timeelapsed (refresh_timer));
}


/***************************************************************************

  Returns the current horizontal beam position in pixels

 ***************************************************************************/
int cpu_gethorzbeampos(void)
{
	int scanlinecycles = cpu_getscanlinecycles();
	int horzposcycles = cpu_getcurrentcycles() % scanlinecycles;

	return (Machine->drv->screen_width * horzposcycles) / scanlinecycles;
}


void cpu_seticount(int cycles)
{
	int cpunum = (activecpu < 0) ? 0 : activecpu;
	ICOUNT (cpunum) = cycles;
}



/***************************************************************************

  Returns the number of times the interrupt handler will be called before
  the end of the current video frame. This can be useful to interrupt
  handlers to synchronize their operation. If you call this from outside
  an interrupt handler, add 1 to the result, i.e. if it returns 0, it means
  that the interrupt handler will be called once.

***************************************************************************/
int cpu_getiloops(void)
{
	int cpunum = (activecpu < 0) ? 0 : activecpu;
	return cpu[cpunum].iloops;
}



/***************************************************************************

  Interrupt handling

***************************************************************************/

/***************************************************************************

  Use this function to cause an interrupt immediately (don't have to wait
  until the next call to the interrupt handler)

***************************************************************************/
void cpu_cause_interrupt(int cpunum,int type)
{
	timer_set (TIME_NOW, (cpunum & 7) | (type << 3), cpu_manualintcallback);
}



void cpu_clear_pending_interrupts(int cpunum)
{
	timer_set (TIME_NOW, cpunum, cpu_clearintcallback);
}



void interrupt_enable_w(int offset,int data)
{
	int cpunum = (activecpu < 0) ? 0 : activecpu;
	interrupt_enable[cpunum] = data;

	/* make sure there are no queued interrupts */
	if (data == 0) cpu_clear_pending_interrupts(cpunum);
}



void interrupt_vector_w(int offset,int data)
{
	int cpunum = (activecpu < 0) ? 0 : activecpu;
	if (interrupt_vector[cpunum] != data)
	{
		interrupt_vector[cpunum] = data;

		/* make sure there are no queued interrupts */
		cpu_clear_pending_interrupts(cpunum);
	}
}



int interrupt(void)
{
	int cpunum = (activecpu < 0) ? 0 : activecpu;
	int val;

	if (interrupt_enable[cpunum] == 0)
		return INT_TYPE_NONE (cpunum);

	val = INT_TYPE_IRQ (cpunum);
	if (val == -1000)
		val = interrupt_vector[cpunum];
	return val;
}



int nmi_interrupt(void)
{
	int cpunum = (activecpu < 0) ? 0 : activecpu;
	if (interrupt_enable[cpunum] == 0)
		return INT_TYPE_NONE (cpunum);
	return INT_TYPE_NMI (cpunum);
}


#ifdef USE_M68000_GP
int m68_level1_irq(void)
{
	int cpunum = (activecpu < 0) ? 0 : activecpu;
	if (interrupt_enable[cpunum] == 0) return MC68000_INT_NONE;
	else return MC68000_IRQ_1;
}
int m68_level2_irq(void)
{
	int cpunum = (activecpu < 0) ? 0 : activecpu;
	if (interrupt_enable[cpunum] == 0) return MC68000_INT_NONE;
	else return MC68000_IRQ_2;
}
int m68_level3_irq(void)
{
	int cpunum = (activecpu < 0) ? 0 : activecpu;
	if (interrupt_enable[cpunum] == 0) return MC68000_INT_NONE;
	else return MC68000_IRQ_3;
}
int m68_level4_irq(void)
{
	int cpunum = (activecpu < 0) ? 0 : activecpu;
	if (interrupt_enable[cpunum] == 0) return MC68000_INT_NONE;
	else return MC68000_IRQ_4;
}
int m68_level5_irq(void)
{
	int cpunum = (activecpu < 0) ? 0 : activecpu;
	if (interrupt_enable[cpunum] == 0) return MC68000_INT_NONE;
	else return MC68000_IRQ_5;
}
int m68_level6_irq(void)
{
	int cpunum = (activecpu < 0) ? 0 : activecpu;
	if (interrupt_enable[cpunum] == 0) return MC68000_INT_NONE;
	else return MC68000_IRQ_6;
}
int m68_level7_irq(void)
{
	int cpunum = (activecpu < 0) ? 0 : activecpu;
	if (interrupt_enable[cpunum] == 0) return MC68000_INT_NONE;
	else return MC68000_IRQ_7;
}
#endif


int ignore_interrupt(void)
{
	int cpunum = (activecpu < 0) ? 0 : activecpu;
	return INT_TYPE_NONE (cpunum);
}



/***************************************************************************

  CPU timing and synchronization functions.

***************************************************************************/

/* generate a trigger */
void cpu_trigger (int trigger)
{
	timer_trigger (trigger);
}

/* generate a trigger after a specific period of time */
void cpu_triggertime (double duration, int trigger)
{
	timer_set (duration, trigger, cpu_trigger);
}



/* burn CPU cycles until a timer trigger */
void cpu_spinuntil_trigger (int trigger)
{
	int cpunum = (activecpu < 0) ? 0 : activecpu;
	timer_suspendcpu_trigger (cpunum, trigger);
}

/* burn CPU cycles until the next interrupt */
void cpu_spinuntil_int (void)
{
	int cpunum = (activecpu < 0) ? 0 : activecpu;
	cpu_spinuntil_trigger (TRIGGER_INT + cpunum);
}

/* burn CPU cycles until our timeslice is up */
void cpu_spin (void)
{
	cpu_spinuntil_trigger (TRIGGER_TIMESLICE);
}

/* burn CPU cycles for a specific period of time */
void cpu_spinuntil_time (double duration)
{
	static int timetrig = 0;

	cpu_spinuntil_trigger (TRIGGER_SUSPENDTIME + timetrig);
	cpu_triggertime (duration, TRIGGER_SUSPENDTIME + timetrig);
	timetrig = (timetrig + 1) & 255;
}



/* yield our timeslice for a specific period of time */
void cpu_yielduntil_trigger (int trigger)
{
	int cpunum = (activecpu < 0) ? 0 : activecpu;
	timer_holdcpu_trigger (cpunum, trigger);
}

/* yield our timeslice until the next interrupt */
void cpu_yielduntil_int (void)
{
	int cpunum = (activecpu < 0) ? 0 : activecpu;
	cpu_yielduntil_trigger (TRIGGER_INT + cpunum);
}

/* yield our current timeslice */
void cpu_yield (void)
{
	cpu_yielduntil_trigger (TRIGGER_TIMESLICE);
}

/* yield our timeslice for a specific period of time */
void cpu_yielduntil_time (double duration)
{
	static int timetrig = 0;

	cpu_yielduntil_trigger (TRIGGER_YIELDTIME + timetrig);
	cpu_triggertime (duration, TRIGGER_YIELDTIME + timetrig);
	timetrig = (timetrig + 1) & 255;
}



int cpu_getvblank(void)
{
	return vblank;
}

int cpu_getcurrentframe(void)
{
	return current_frame;
}


/***************************************************************************

  Internal CPU event processors.

***************************************************************************/
/* static FRANXIS 28-02-2006 */ void cpu_generate_interrupt (int cpunum, int (*func)(void), int num)
{
	int oldactive = activecpu;

	/* swap to the CPU's context */
	activecpu = cpunum;
	memorycontextswap (activecpu);
	if (cpu[activecpu].save_context) SETREGS (activecpu, cpu[activecpu].context);

	/* cause the interrupt, calling the function if it exists */
	if (func) num = (*func)();

	CAUSE_INTERRUPT (cpunum, num);

	/* update the CPU's context */
	if (cpu[activecpu].save_context) GETREGS (activecpu, cpu[activecpu].context);
	activecpu = oldactive;
	if (activecpu >= 0) memorycontextswap (activecpu);

	/* generate a trigger to unsuspend any CPUs waiting on the interrupt */
	if (num != INT_TYPE_NONE (cpunum))
		timer_trigger (TRIGGER_INT + cpunum);
}


static void cpu_clear_interrupts (int cpunum)
{
	int oldactive = activecpu;

	/* swap to the CPU's context */
	activecpu = cpunum;
	memorycontextswap (activecpu);
	if (cpu[activecpu].save_context) SETREGS (activecpu, cpu[activecpu].context);

	/* cause the interrupt, calling the function if it exists */
	CLEAR_PENDING_INTERRUPTS (cpunum);

	/* update the CPU's context */
	if (cpu[activecpu].save_context) GETREGS (activecpu, cpu[activecpu].context);
	activecpu = oldactive;
	if (activecpu >= 0) memorycontextswap (activecpu);
}


static void cpu_reset_cpu (int cpunum)
{
	int oldactive = activecpu;

	/* swap to the CPU's context */
	activecpu = cpunum;
	memorycontextswap (activecpu);
	if (cpu[activecpu].save_context) SETREGS (activecpu, cpu[activecpu].context);

	/* reset the CPU */
	RESET (cpunum);

	/* update the CPU's context */
	if (cpu[activecpu].save_context) GETREGS (activecpu, cpu[activecpu].context);
	activecpu = oldactive;
	if (activecpu >= 0) memorycontextswap (activecpu);
}


/***************************************************************************

  Interrupt callback. This is called once per CPU interrupt by either the
  VBLANK handler or by the CPU's own timer directly, depending on whether
  or not the CPU's interrupts are synced to VBLANK.

***************************************************************************/
static void cpu_vblankintcallback (int param)
{
	if (Machine->drv->cpu[param].vblank_interrupt)
		cpu_generate_interrupt (param, Machine->drv->cpu[param].vblank_interrupt, 0);

	/* update the counters */
	cpu[param].iloops--;
}


static void cpu_timedintcallback (int param)
{
	/* bail if there is no routine */
	if (!Machine->drv->cpu[param].timed_interrupt)
		return;

	/* generate the interrupt */
	cpu_generate_interrupt (param, Machine->drv->cpu[param].timed_interrupt, 0);
}


static void cpu_manualintcallback (int param)
{
	int intnum = param >> 3;
	int cpunum = param & 7;

	/* generate the interrupt */
	cpu_generate_interrupt (cpunum, 0, intnum);
}


static void cpu_clearintcallback (int param)
{
	/* clear the interrupts */
	cpu_clear_interrupts (param);
}


static void cpu_resetcallback (int param)
{
	/* reset the CPU */
	cpu_reset_cpu (param);
}


/***************************************************************************

  VBLANK reset. Called at the start of emulation and once per VBLANK in
  order to update the input ports and reset the interrupt counter.

***************************************************************************/
static void cpu_vblankreset (void)
{
	int i;

	/* read hi scores from disk */
	if (hiscoreloaded == 0 && Machine->gamedrv->hiscore_load)
		hiscoreloaded = (*Machine->gamedrv->hiscore_load)();

	/* read keyboard & update the status of the input ports */
	update_input_ports ();

	/* reset the cycle counters */
	for (i = 0; i < totalcpu; i++)
	{
		if (!timer_iscpususpended (i))
			cpu[i].iloops = Machine->drv->cpu[i].vblank_interrupts_per_frame - 1;
		else
			cpu[i].iloops = -1;
	}
}


/***************************************************************************

  VBLANK callback. This is called 'vblank_multipler' times per frame to
  service VBLANK-synced interrupts and to begin the screen update process.

***************************************************************************/
static void cpu_vblankcallback (int param)
{
	int i;

	/* loop over CPUs */
	for (i = 0; i < totalcpu; i++)
	{
		/* if the interrupt multiplier is valid */
		if (cpu[i].vblankint_multiplier != -1)
		{
			/* decrement; if we hit zero, generate the interrupt and reset the countdown */
			if (!--cpu[i].vblankint_countdown)
			{
				cpu_vblankintcallback (i);
				cpu[i].vblankint_countdown = cpu[i].vblankint_multiplier;
				timer_reset (cpu[i].vblankint_timer, TIME_NEVER);
			}
		}

		/* else reset the VBLANK timer if this is going to be a real VBLANK */
		else if (vblank_countdown == 1)
			timer_reset (cpu[i].vblankint_timer, TIME_NEVER);
	}

	/* is it a real VBLANK? */
	if (!--vblank_countdown)
	{
		/* do we update the screen now? */
		if (Machine->drv->video_attributes & VIDEO_UPDATE_BEFORE_VBLANK)
			usres = updatescreen();

		/* set the timer to update the screen */
		timer_set (TIME_IN_USEC (Machine->drv->vblank_duration), 0, cpu_updatecallback);
		vblank = 1;

		/* reset the globals */
		cpu_vblankreset ();

		/* reset the counter */
		vblank_countdown = vblank_multiplier;
	}
}


/***************************************************************************

  Video update callback. This is called a game-dependent amount of time
  after the VBLANK in order to trigger a video update.

***************************************************************************/
static void cpu_updatecallback (int param)
{
	/* update the sound system */
#ifdef PROFILER_MAME4ALL
	mame4all_prof_start(6);
#endif    
	sound_update();
#ifdef PROFILER_MAME4ALL
	mame4all_prof_end(6);
#endif

	/* update the screen if we didn't before */
	if (!(Machine->drv->video_attributes & VIDEO_UPDATE_BEFORE_VBLANK))
		usres = updatescreen();
	vblank = 0;

	/* update IPT_VBLANK input ports */
	inputport_vblank_end();

	/* check the watchdog */
	if (watchdog_counter > 0)
		if (--watchdog_counter == 0)
		{
			machine_reset ();
		}

	current_frame++;

	/* reset the refresh timer */
	timer_reset (refresh_timer, TIME_NEVER);
}


/***************************************************************************

  Converts an integral timing rate into a period. Rates can be specified
  as follows:

        rate > 0       -> 'rate' cycles per frame
        rate == 0      -> 0
        rate >= -10000 -> 'rate' cycles per second
        rate < -10000  -> 'rate' nanoseconds

***************************************************************************/
static double cpu_computerate (int value)
{
	/* values equal to zero are zero */
	if (value <= 0)
		return 0.0;

	/* values above between 0 and 50000 are in Hz */
	if (value < 50000)
		return TIME_IN_HZ (value);

	/* values greater than 50000 are in nanoseconds */
	else
		return TIME_IN_NSEC (value);
}


static void cpu_timeslicecallback (int param)
{
	timer_trigger (TRIGGER_TIMESLICE);
}


/***************************************************************************

  Initializes all the timers used by the CPU system.

***************************************************************************/
static void cpu_inittimers (void)
{
	int i, max, ipf;

	/* remove old timers */
	if (timeslice_timer)
		timer_remove (timeslice_timer);
	if (refresh_timer)
		timer_remove (refresh_timer);
	if (vblank_timer)
		timer_remove (vblank_timer);

	/* allocate a dummy timer at the minimum frequency to break things up */
	ipf = Machine->drv->cpu_slices_per_frame;
	if (ipf <= 0)
		ipf = 1;
	timeslice_period = TIME_IN_HZ (Machine->drv->frames_per_second * ipf);
	timeslice_timer = timer_pulse (timeslice_period, 0, cpu_timeslicecallback);

	/* allocate an infinite timer to track elapsed time since the last refresh */
	refresh_period = TIME_IN_HZ (Machine->drv->frames_per_second);
	refresh_period_inv = 1.0 / refresh_period;
	refresh_timer = timer_set (TIME_NEVER, 0, NULL);

	/* while we're at it, compute the scanline times */
	if (Machine->drv->vblank_duration)
		scanline_period = (refresh_period - TIME_IN_USEC (Machine->drv->vblank_duration)) /
				(double)(Machine->drv->visible_area.max_y - Machine->drv->visible_area.min_y + 1);
	else
		scanline_period = refresh_period / (double)Machine->drv->screen_height;
	scanline_period_inv = 1.0 / scanline_period;

	/*
	 *		The following code finds all the CPUs that are interrupting in sync with the VBLANK
	 *		and sets up the VBLANK timer to run at the minimum number of cycles per frame in
	 *		order to service all the synced interrupts
	 */

	/* find the CPU with the maximum interrupts per frame */
	max = 1;
	for (i = 0; i < totalcpu; i++)
	{
		ipf = Machine->drv->cpu[i].vblank_interrupts_per_frame;
		if (ipf > max)
			max = ipf;
	}

	/* now find the LCD with the rest of the CPUs (brute force - these numbers aren't huge) */
	vblank_multiplier = max;
	while (1)
	{
		for (i = 0; i < totalcpu; i++)
		{
			ipf = Machine->drv->cpu[i].vblank_interrupts_per_frame;
			if (ipf > 0 && (vblank_multiplier % ipf) != 0)
				break;
		}
		if (i == totalcpu)
			break;
		vblank_multiplier += max;
	}

	/* initialize the countdown timers and intervals */
	for (i = 0; i < totalcpu; i++)
	{
		ipf = Machine->drv->cpu[i].vblank_interrupts_per_frame;
		if (ipf > 0)
			cpu[i].vblankint_countdown = cpu[i].vblankint_multiplier = vblank_multiplier / ipf;
		else
			cpu[i].vblankint_countdown = cpu[i].vblankint_multiplier = -1;
	}

	/* allocate a vblank timer at the frame rate * the LCD number of interrupts per frame */
	vblank_period = TIME_IN_HZ (Machine->drv->frames_per_second * vblank_multiplier);
	vblank_timer = timer_pulse (vblank_period, 0, cpu_vblankcallback);
	vblank_countdown = vblank_multiplier;

	/*
	 *		The following code creates individual timers for each CPU whose interrupts are not
	 *		synced to the VBLANK, and computes the typical number of cycles per interrupt
	 */

	/* start the CPU interrupt timers */
	for (i = 0; i < totalcpu; i++)
	{
		ipf = Machine->drv->cpu[i].vblank_interrupts_per_frame;

		/* remove old timers */
		if (cpu[i].vblankint_timer)
			timer_remove (cpu[i].vblankint_timer);
		if (cpu[i].timedint_timer)
			timer_remove (cpu[i].timedint_timer);

		/* compute the average number of cycles per interrupt */
		if (ipf <= 0)
			ipf = 1;
		cpu[i].vblankint_period = TIME_IN_HZ (Machine->drv->frames_per_second * ipf);
		cpu[i].vblankint_timer = timer_set (TIME_NEVER, 0, NULL);

		/* see if we need to allocate a CPU timer */
		ipf = Machine->drv->cpu[i].timed_interrupts_per_second;
		if (ipf)
		{
			cpu[i].timedint_period = cpu_computerate (ipf);
			cpu[i].timedint_timer = timer_pulse (cpu[i].timedint_period, i, cpu_timedintcallback);
		}
	}
}


/* AJP 981016 */
int cpu_is_saving_context(int _activecpu)
{
	return (cpu[_activecpu].save_context);
}


/* JB 971019 */
void* cpu_getcontext (int _activecpu)
{
	return cpu[_activecpu].context;
}


#ifdef USE_Z80_GP
/* Reset Z80 with set daisychain link */
static void Z80_Reset_with_daisychain(void)
{
	Z80_Reset( Z80_daisychain[activecpu] );
}
/* set z80 daisy chain link (upload when after reset ) */
void cpu_setdaisychain (int cpunum, Z80_DaisyChain *daisy_chain )
{
	Z80_daisychain[cpunum] = daisy_chain;
}
#endif

/* dummy interfaces for non-CPUs */
static void Dummy_SetRegs(void *Regs) { }
static void Dummy_GetRegs(void *Regs) { }
static unsigned Dummy_GetPC(void) { return 0; }
static void Dummy_Reset(void) { }
static int Dummy_Execute(int cycles) { return cycles; }
static void Dummy_Cause_Interrupt(int type) { }
static void Dummy_Clear_Pending_Interrupts(void) { }



/***********************************/
/* Add by JMH                      */
/***********************************/
void cpu_start(void)
{
	int i;

	/* determine which CPUs need a context switch */
	for (i = 0; i < totalcpu; i++)
	{
		#ifdef MAME_DEBUG

			/* with the debugger, we need to save the contexts */
			cpu[i].save_context = 1;

		#else
			int j;


			/* otherwise, we only need to save if there is another CPU of the same type */
			cpu[i].save_context = 0;

			for (j = 0; j < totalcpu; j++)
				if (i != j && (Machine->drv->cpu[i].cpu_type & ~CPU_FLAGS_MASK) == (Machine->drv->cpu[j].cpu_type & ~CPU_FLAGS_MASK))
					cpu[i].save_context = 1;

		#endif
	}
reset:
	/* initialize the various timers (suspends all CPUs at startup) */
	cpu_inittimers ();
	watchdog_counter = -1;

	/* enable all CPUs (except for audio CPUs if the sound is off) */
	for (i = 0; i < totalcpu; i++)
		if (!(Machine->drv->cpu[i].cpu_type & CPU_AUDIO_CPU) || Machine->sample_rate != 0)
			timer_suspendcpu (i, 0);

	have_to_reset = 0;
	hiscoreloaded = 0;
	vblank = 0;

if (errorlog) fprintf(errorlog,"Machine reset\n");



	/* start with interrupts enabled, so the generic routine will work even if */
	/* the machine doesn't have an interrupt enable port */
	for (i = 0;i < MAX_CPU;i++)
	{
		interrupt_enable[i] = 1;
		interrupt_vector[i] = 0xff;
	}

	/* do this AFTER the above so init_machine() can use cpu_halt() to hold the */
	/* execution of some CPUs, or disable interrupts */
	if (Machine->drv->init_machine) (*Machine->drv->init_machine)();

	/* reset each CPU */
	for (i = 0; i < totalcpu; i++)
	{
		/* swap memory contexts and reset */
		memorycontextswap (i);
#ifdef Z80_DAISYCHAIN
		if (cpu[i].save_context) SETREGS (i, cpu[i].context);
		activecpu = i;
#endif
		RESET (i);
return;
		/* save the CPU context if necessary */
		if (cpu[i].save_context) GETREGS (i, cpu[i].context);

		/* reset the total number of cycles */
		cpu[i].totalcycles = 0;
	}


	/* reset the globals */
	cpu_vblankreset ();
}

extern int display_updated;

void cpu_step(void)
{
  int cpunum;

  /* loop until UPDATED	 */
  display_updated = 0;
  while(!display_updated)
  {
		/* was machine_reset() called? */
//		if (have_to_reset) goto reset;

		/* ask the timer system to schedule */
		if (timer_schedule_cpu (&cpunum, &running))
		{
			int ran;

			/* switch memory and CPU contexts */
			activecpu = cpunum;
			memorycontextswap (activecpu);
			if (cpu[activecpu].save_context) SETREGS (activecpu, cpu[activecpu].context);

			/* make sure any bank switching is reset */
			SET_OP_BASE (activecpu, GETPC (activecpu));

			/* run for the requested number of cycles */
			ran = EXECUTE (activecpu, running);

			/* update based on how many cycles we really ran */
			cpu[activecpu].totalcycles += ran;

			/* update the contexts */
			if (cpu[activecpu].save_context) GETREGS (activecpu, cpu[activecpu].context);
			updatememorybase (activecpu);
			activecpu = -1;

			/* update the timer with how long we actually ran */
			timer_update_cpu (cpunum, ran);
		}
  }
}

void cpu_stop(void)
{
	/* write hi scores to disk - No scores saving if cheat */
	if (hiscoreloaded != 0 && Machine->gamedrv->hiscore_save)
		(*Machine->gamedrv->hiscore_save)();
}

