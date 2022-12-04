#ifndef MAME_MEMORY_H
#define MAME_MEMORY_H

#if defined(__cplusplus) && !defined(USE_CPLUS)
extern "C" {
#endif

#define MAX_BANKS 8

/***************************************************************************

Note that the memory hooks are not passed the actual memory address where
the operation takes place, but the offset from the beginning of the block
they are assigned to. This makes handling of mirror addresses easier, and
makes the handlers a bit more "object oriented". If you handler needs to
read/write the main memory area, provide a "base" pointer: it will be
initialized by the main engine to point to the beginning of the memory block
assigned to the handler. You may also provided a pointer to "size": it
will be set to the length of the memory area processed by the handler.

***************************************************************************/
struct MemoryReadAddress
{
	int start,end;
	int (*handler)(int offset);   /* see special values below */
	unsigned char **base;         /* optional (see explanation above) */
	int *size;                    /* optional (see explanation above) */
};

#define MRA_NOP   0	              /* don't care, return 0 */
#define MRA_RAM   ((int(*)(int))-1)	  /* plain RAM location (return its contents) */
#define MRA_ROM   ((int(*)(int))-2)	  /* plain ROM location (return its contents) */
#define MRA_BANK1 ((int(*)(int))-10)  /* bank memory */
#define MRA_BANK2 ((int(*)(int))-11)  /* bank memory */
#define MRA_BANK3 ((int(*)(int))-12)  /* bank memory */
#define MRA_BANK4 ((int(*)(int))-13)  /* bank memory */
#define MRA_BANK5 ((int(*)(int))-14)  /* bank memory */
#define MRA_BANK6 ((int(*)(int))-15)  /* bank memory */
#define MRA_BANK7 ((int(*)(int))-16)  /* bank memory */
#define MRA_BANK8 ((int(*)(int))-17)  /* bank memory */

struct MemoryWriteAddress
{
	int start,end;
	void (*handler)(int offset,int data);	/* see special values below */
	unsigned char **base;	/* optional (see explanation above) */
	int *size;	/* optional (see explanation above) */
};

#define MWA_NOP 0	                  /* do nothing */
#define MWA_RAM ((void(*)(int,int))-1)	   /* plain RAM location (store the value) */
#define MWA_ROM ((void(*)(int,int))-2)	   /* plain ROM location (do nothing) */
/* RAM[] and ROM[] are usually the same, but they aren't if the CPU opcodes are */
/* encrypted. In such a case, opcodes are fetched from ROM[], and arguments from */
/* RAM[]. If the program dynamically creates code in RAM and executes it, it */
/* won't work unless writes to RAM affects both RAM[] and ROM[]. */
#define MWA_RAMROM ((void(*)(int,int))-3)	/* write to both the RAM[] and ROM[] array. */
#define MWA_BANK1 ((void(*)(int,int))-10)  /* bank memory */
#define MWA_BANK2 ((void(*)(int,int))-11)  /* bank memory */
#define MWA_BANK3 ((void(*)(int,int))-12)  /* bank memory */
#define MWA_BANK4 ((void(*)(int,int))-13)  /* bank memory */
#define MWA_BANK5 ((void(*)(int,int))-14)  /* bank memory */
#define MWA_BANK6 ((void(*)(int,int))-15)  /* bank memory */
#define MWA_BANK7 ((void(*)(int,int))-16)  /* bank memory */
#define MWA_BANK8 ((void(*)(int,int))-17)  /* bank memory */



/***************************************************************************

IN and OUT ports are handled like memory accesses, the hook template is the
same so you can interchange them. Of course there is no 'base' pointer for
IO ports.

***************************************************************************/
struct IOReadPort
{
	int start,end;
	int (*handler)(int offset);	/* see special values below */
};

#define IORP_NOP 0	/* don't care, return 0 */


struct IOWritePort
{
	int start,end;
	void (*handler)(int offset,int data);	/* see special values below */
};

#define IOWP_NOP 0	/* do nothing */


/***************************************************************************

If a memory region contains areas that are outside of the ROM region for
an address space, the memory system will allocate an array of structures
to track the external areas.

***************************************************************************/

#define MAX_EXT_MEMORY 64

struct ExtMemory
{
	int start,end,region;
	unsigned char *data;
};

extern struct ExtMemory ext_memory[MAX_EXT_MEMORY];



/* memory element block size */
#define MH_SBITS    8			/* sub element bank size */
#define MH_PBITS    8			/* port current element size */
#define MH_ELEMAX  64			/* sub elements limit */
#define MH_HARDMAX 64			/* hardware functions limit */


/* 29 bits address (dword access)     AJP 980803 */
#define ABITS1_29    19
#define ABITS2_29     8
#define ABITS3_29     0
#define ABITS_MIN_29  2      /* minimum memory block is 4 bytes */

/* 24 bits address (word access) */
#define ABITS1_24    15
#define ABITS2_24     8
#define ABITS3_24     0
#define ABITS_MIN_24  1      /* minimum memory block is 2 bytes */
/* 20 bits address */
#define ABITS1_20    12
#define ABITS2_20     8
#define ABITS3_20     0
#define ABITS_MIN_20  0      /* minimum memory block is 1 byte */
/* 21 bits address */
#define ABITS1_21    13
#define ABITS2_21     8
#define ABITS3_21     0
#define ABITS_MIN_21  0      /* minimum memory block is 1 byte */
/* 16 bits address */
#define ABITS1_16    12
#define ABITS2_16     4
#define ABITS3_16     0
#define ABITS_MIN_16  0      /* minimum memory block is 1 byte */
/* 16 bits address (little endian word access) */
#define ABITS1_16LEW   12
#define ABITS2_16LEW    3
#define ABITS3_16LEW    0
#define ABITS_MIN_16LEW 1      /* minimum memory block is 2 bytes */
/* mask bits */
#define MHMASK(abits)    (0xffffffff>>(32-abits))

typedef unsigned char MHELE;

extern int cpu_mshift1;
extern MHELE *cur_mrhard;
extern MHELE *cur_mwhard;
extern MHELE curhw;

extern unsigned char *ROM;
extern unsigned char *OP_RAM;	/* op_code used */
extern unsigned char *OP_ROM;	/* op_code used */

/* ----- memory setting subroutine ---- */
void cpu_setOPbase16(int pc);
void cpu_setOPbase16lew(int pc);
void cpu_setOPbase20(int pc);
void cpu_setOPbase21(int pc);
void cpu_setOPbase24(int pc);
void cpu_setOPbase29(int pc);  /* AJP 980803 */
void cpu_setOPbaseoverride (int (*f)(int));

/* ----- memory setup function ----- */
int initmemoryhandlers(void);
void shutdownmemoryhandler(void);

void memorycontextswap(int activecpu);
void updatememorybase(int activecpu);

void install_mem_read_handler(int cpu, int start, int end, int (*handler)(int));
void install_mem_write_handler(int cpu, int start, int end, void (*handler)(int, int));

/* ----- memory read /write function ----- */
int cpu_readmem16(int address);
int cpu_readmem16lew(int address);
int cpu_readmem16lew_word(int address);
int cpu_readmem20(int address);
int cpu_readmem21(int address);
int cpu_readmem24(int address);
int cpu_readmem24_word(int address);
int cpu_readmem24_dword(int address);
int cpu_readmem29(int address);        /* AJP 980803 */
int cpu_readmem29_word(int address);   /* AJP 980803 */
int cpu_readmem29_dword(int address);  /* AJP 980803 */
void cpu_writemem16(int address,int data);
void cpu_writemem16lew(int address,int data);
void cpu_writemem16lew_word(int address,int data);
void cpu_writemem20(int address,int data);
void cpu_writemem21(int address,int data);
void cpu_writemem24(int address,int data);
void cpu_writemem24_word(int address,int data);
void cpu_writemem24_dword(int address,int data);
void cpu_writemem29(int address,int data);        /* AJP 980803 */
void cpu_writemem29_word(int address,int data);   /* AJP 980803 */
void cpu_writemem29_word_masked(int address,int data);   /* AJP 980816 */
void cpu_writemem29_dword(int address,int data);  /* AJP 980803 */

/* ----- 16-bit memory access macros ----- */

#ifdef ACORN
/* Use these to avoid alignment problems on non-x86 hardware. */
#ifdef LSB_FIRST
	#define READ_WORD(addr) \
	(((int)addr & 1)? \
	( ((((unsigned char *)(addr))[0])) | ((((unsigned char *)(addr))[1]) <<  8) ) : \
	(*(unsigned short *)(addr)) )
	#define READ_WORD_ALIGNED(a) (*(unsigned short *)(a))
	#define READ_WORD_NONALIGNED(addr) ( ((((unsigned char *)(addr))[0])) | ((((unsigned char *)(addr))[1]) <<  8) )

	#define WRITE_WORD(addr,value) \
	(((int)addr & 1)? \
	( ((unsigned char *)(addr))[0]=(unsigned char)((value)),\
	  ((unsigned char *)(addr))[1]=(unsigned char)((value)>>8),\
	  value ) : \
	(*(unsigned short *)(addr) = (value)) )
	#define WRITE_WORD_ALIGNED(a,d) (*(unsigned short *)(a) = (d))
	#define WRITE_WORD_NONALIGNED(addr,value) (((unsigned char *)(addr))[0]=(unsigned char)((value)),((unsigned char *)(addr))[1]=(unsigned char)((value)>>8),value)
#else
	#define READ_WORD(addr) \
	(((int)addr & 1)? \
	( ((((unsigned char *)(addr))[1])) | ((((unsigned char *)(addr))[0]) <<  8) ) : \
	(*(unsigned short *)(addr)) )
	#define READ_WORD_ALIGNED(a) (*(unsigned short *)(a))
	#define READ_WORD_NONALIGNED(addr) ( ((((unsigned char *)(addr))[1])) | ((((unsigned char *)(addr))[0]) <<  8) )

	#define WRITE_WORD(addr,value) \
	(((int)addr & 1)? \
	( ((unsigned char *)(addr))[1]=(unsigned char)((value)),\
	  ((unsigned char *)(addr))[0]=(unsigned char)((value)>>8),\
	  value ) : \
	(*(unsigned short *)(addr) = (value)) )
	#define WRITE_WORD_ALIGNED(a,d) (*(unsigned short *)(a) = (d))
	#define WRITE_WORD_NONALIGNED(addr,value) (((unsigned char *)(addr))[1]=(unsigned char)((value)),((unsigned char *)(addr))[0]=(unsigned char)((value)>>8),value)
#endif

#else
#define READ_WORD(a)               (*(unsigned short *)(a))
#define READ_WORD_ALIGNED(a)       (*(unsigned short *)(a))
#define READ_WORD_NONALIGNED(a)    (*(unsigned short *)(a))
#define WRITE_WORD(a,d)            (*(unsigned short *)(a) = (d))
#define WRITE_WORD_ALIGNED(a,d)    (*(unsigned short *)(a) = (d))
#define WRITE_WORD_NONALIGNED(a,d) (*(unsigned short *)(a) = (d))
#endif

#define COMBINE_WORD(w,d)     (((w) & ((d) >> 16)) | ((d) & 0xffff))
#define COMBINE_WORD_MEM(a,d) (WRITE_WORD((a), (READ_WORD(a) & ((d) >> 16)) | (d)))

/* ----- port read / write function ----- */
int cpu_readport(int Port);
void cpu_writeport(int Port,int Value);

/* ----- for use OPbaseOverride driver, request override callback to next cpu_setOPbase ----- */
#define catch_nextBranch()      (ophw = 0xff)

/* -----  bank memory function ----- */
#define cpu_setbank(B,A)  {cpu_bankbase[B]=(unsigned char *)(A);if(ophw==B){ophw=0xff;cpu_setOPbase16(cpu_getpc());}}

/* ------ bank memory handler ------ */
extern void cpu_setbankhandler_r(int bank,int (*handler)(int) );
extern void cpu_setbankhandler_w(int bank,void (*handler)(int,int) );

/* ----- op-code region set function ----- */
#define change_pc16(pc) {if(cur_mrhard[(pc)>>(ABITS2_16+ABITS_MIN_16)]!=ophw)cpu_setOPbase16(pc);}
#define change_pc16lew(pc) {if(cur_mrhard[(pc)>>(ABITS2_16LEW+ABITS_MIN_16LEW)]!=ophw)cpu_setOPbase16lew(pc);}
#define change_pc20(pc) {if(cur_mrhard[(pc)>>(ABITS2_20+ABITS_MIN_20)]!=ophw)cpu_setOPbase20(pc);}
#define change_pc24(pc) {if(cur_mrhard[(pc)>>(ABITS2_24+ABITS_MIN_24)]!=ophw)cpu_setOPbase24(pc);}
#define change_pc29(pc) {if(cur_mrhard[((unsigned int)pc)>>(ABITS2_29+ABITS_MIN_29+3)]!=ophw)cpu_setOPbase29(pc);}

#define change_pc change_pc16


/* bank memory functions */
extern MHELE ophw;
extern unsigned char *cpu_bankbase[];

#define cpu_readop(A) 		(OP_ROM[A])
#define cpu_readop16(A)         READ_WORD(&OP_ROM[A])
#define cpu_readop_arg(A)	(OP_RAM[A])
#define cpu_readop_arg16(A)     READ_WORD(&OP_RAM[A])

#if defined(__cplusplus) && !defined(USE_CPLUS)
}
#endif
#endif
