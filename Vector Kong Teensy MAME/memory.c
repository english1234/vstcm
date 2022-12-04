/***************************************************************************

  memory.c

  Functions which handle the CPU memory and I/O port access.

***************************************************************************/

#include "driver.h"
#include "osd_cpu.h"

/* Convenience macros - not in cpuintrf.h because they shouldn't be used by everyone */
#define MEMORY_READ(index,offset)       ((*cpuintf[Machine->drv->cpu[index].cpu_type & ~CPU_FLAGS_MASK].memory_read)(offset))
#define MEMORY_WRITE(index,offset,data) ((*cpuintf[Machine->drv->cpu[index].cpu_type & ~CPU_FLAGS_MASK].memory_write)(offset,data))
#define SET_OP_BASE(index,pc)           ((*cpuintf[Machine->drv->cpu[index].cpu_type & ~CPU_FLAGS_MASK].set_op_base)(pc))
#define ADDRESS_BITS(index)             (cpuintf[Machine->drv->cpu[index].cpu_type & ~CPU_FLAGS_MASK].address_bits)
#define ABITS1(index)                   (cpuintf[Machine->drv->cpu[index].cpu_type & ~CPU_FLAGS_MASK].abits1)
#define ABITS2(index)                   (cpuintf[Machine->drv->cpu[index].cpu_type & ~CPU_FLAGS_MASK].abits2)
#define ABITS3(index)                   (0)
#define ABITSMIN(index)                 (cpuintf[Machine->drv->cpu[index].cpu_type & ~CPU_FLAGS_MASK].abitsmin)

#if LSB_FIRST
	#define BYTE_XOR_BE(a) ((a) ^ 1)
	#define BYTE_XOR_LE(a) (a)
	#define BIG_DWORD_BE(x) (((x) >> 16) + ((x) << 16))
	#define BIG_DWORD_LE(x) (x)
 	/* GSL 980224 Shift values for bytes within a word, used by the misaligned word load/store code */
	#define SHIFT0 16
	#define SHIFT1 24
	#define SHIFT2 0
	#define SHIFT3 8
#else
	#define BYTE_XOR_BE(a) (a)
	#define BYTE_XOR_LE(a) ((a) ^ 1)
	#define BIG_DWORD_BE(x) (x)
	#define BIG_DWORD_LE(x) (((UINT32)(x) >> 16) + ((x) << 16))
	/* GSL 980224 Shift values for bytes within a word, used by the  misaligned word load/store code */
	#define SHIFT0 24
	#define SHIFT1 16
	#define SHIFT2 8
	#define SHIFT3 0
#endif

unsigned char *RAM;
unsigned char *OP_RAM;
unsigned char *OP_ROM;
MHELE ophw;				/* op-code hardware number */

struct ExtMemory ext_memory[MAX_EXT_MEMORY];

static unsigned char *ramptr[MAX_CPU], *romptr[MAX_CPU];
/* quick kludge: we support encrypted opcodes on only one CPU. This is usually */
/* CPU #0, to use a different one, change this variable in opcode_decode() */
/* TODO: handle this better!!! */
int encrypted_cpu;

/* element shift bits, mask bits */
int mhshift[MAX_CPU][3], mhmask[MAX_CPU][3];

/* pointers to port structs */
/* ASG: port speedup */
static const struct IOReadPort *readport[MAX_CPU];
static const struct IOWritePort *writeport[MAX_CPU];
static int portmask[MAX_CPU];
static const struct IOReadPort *cur_readport;
static const struct IOWritePort *cur_writeport;
static int cur_portmask;

/* current hardware element map */
static MHELE *cur_mr_element[MAX_CPU];
static MHELE *cur_mw_element[MAX_CPU];

/* sub memory/port hardware element map */
MHELE readhardware[MH_ELEMAX << MH_SBITS];  /* mem/port read  */
MHELE writehardware[MH_ELEMAX << MH_SBITS]; /* mem/port write */

/* memory hardware element map */
/* value:                      */
#define HT_RAM    0		/* RAM direct        */
#define HT_BANK1  1		/* bank memory #1    */
#define HT_BANK2  2		/* bank memory #2    */
#define HT_BANK3  3		/* bank memory #3    */
#define HT_BANK4  4		/* bank memory #4    */
#define HT_BANK5  5		/* bank memory #5    */
#define HT_BANK6  6		/* bank memory #6    */
#define HT_BANK7  7		/* bank memory #7    */
#define HT_BANK8  8		/* bank memory #8    */
#define HT_NON    9		/* non mapped memory */
#define HT_NOP    10		/* NOP memory        */
#define HT_RAMROM 11		/* RAM ROM memory    */
#define HT_ROM    12		/* ROM memory        */

#define HT_USER   13		/* user functions    */
/* [MH_HARDMAX]-0xff	   link to sub memory element  */
/*                         (value-MH_HARDMAX)<<MH_SBITS -> element bank */

#define HT_BANKMAX (HT_BANK1 + MAX_BANKS - 1)

/* memory hardware handler */
int (*memoryreadhandler[MH_HARDMAX])(int address);
int memoryreadoffset[MH_HARDMAX];
void (*memorywritehandler[MH_HARDMAX])(int address,int data);
int memorywriteoffset[MH_HARDMAX];

/* bank ram base address */
unsigned char *cpu_bankbase[HT_BANKMAX+1];
static int bankreadoffset[HT_BANKMAX+1];
static int bankwriteoffset[HT_BANKMAX+1];

/* override OP base handler */
static int (*setOPbasefunc)(int);

/* current cpu current hardware element map point */
MHELE *cur_mrhard;
MHELE *cur_mwhard;


#ifdef macintosh
#endif


/***************************************************************************

  Memory handling

***************************************************************************/

int mrh_ram(int address){return RAM[address];}
int mrh_bank1(int address){return cpu_bankbase[1][address];}
int mrh_bank2(int address){return cpu_bankbase[2][address];}
int mrh_bank3(int address){return cpu_bankbase[3][address];}
int mrh_bank4(int address){return cpu_bankbase[4][address];}
int mrh_bank5(int address){return cpu_bankbase[5][address];}
int mrh_bank6(int address){return cpu_bankbase[6][address];}
int mrh_bank7(int address){return cpu_bankbase[7][address];}
int mrh_bank8(int address){return cpu_bankbase[8][address];}

int mrh_error(int address)
{
	return RAM[address];
}
/* 24-bit address spaces are sparse, so we can't just return RAM[address] */
int mrh_error_sparse(int address)
{
	return 0;
}
int mrh_error_sparse_bit(int address)
{
	return 0;
}
int mrh_nop(int address)
{
	return 0;
}

void mwh_ram(int address,int data){RAM[address] = data;}
void mwh_bank1(int address,int data){cpu_bankbase[1][address] = data;}
void mwh_bank2(int address,int data){cpu_bankbase[2][address] = data;}
void mwh_bank3(int address,int data){cpu_bankbase[3][address] = data;}
void mwh_bank4(int address,int data){cpu_bankbase[4][address] = data;}
void mwh_bank5(int address,int data){cpu_bankbase[5][address] = data;}
void mwh_bank6(int address,int data){cpu_bankbase[6][address] = data;}
void mwh_bank7(int address,int data){cpu_bankbase[7][address] = data;}
void mwh_bank8(int address,int data){cpu_bankbase[8][address] = data;}

void mwh_error(int address,int data)
{
	RAM[address] = data;
}
/* 24-bit address spaces are sparse, so we can't just write to RAM[address] */
void mwh_error_sparse(int address,int data)
{
}
void mwh_error_sparse_bit(int address,int data)
{
}
void mwh_rom(int address,int data)
{
}
void mwh_ramrom(int address,int data)
{
	RAM[address] = ROM[address] = data;
}
void mwh_nop(int address,int data)
{
}


/* return element offset */
static MHELE *get_element( MHELE *element , int ad , int elemask ,
                        MHELE *subelement , int *ele_max )
{
	MHELE hw = element[ad];
	int i,ele;
	int banks = ( elemask / (1<<MH_SBITS) ) + 1;

	if( hw >= MH_HARDMAX ) return &subelement[(hw-MH_HARDMAX)<<MH_SBITS];

	/* create new element block */
	if( (*ele_max)+banks > MH_ELEMAX )
	{
		return 0;
	}
	/* get new element nunber */
	ele = *ele_max;
	(*ele_max)+=banks;
	/* set link mark to current element */
	element[ad] = ele + MH_HARDMAX;
	/* get next subelement top */
	subelement  = &subelement[ele<<MH_SBITS];
	/* initialize new block */
	for( i = 0 ; i < (1<<MH_SBITS) ; i++ )
		subelement[i] = hw;

	return subelement;
}

static void set_element( int cpu , MHELE *celement , int sp , int ep , MHELE type , MHELE *subelement , int *ele_max )
{
	int i;
	int edepth = 0;
	int shift,mask;
	MHELE *eele = celement;
	MHELE *sele = celement;
	MHELE *ele;
	int ss,sb,eb,ee;

	if( (unsigned int) sp > (unsigned int) ep ) return;
	do{
		mask  = mhmask[cpu][edepth];
		shift = mhshift[cpu][edepth];

		/* center element */
		ss = (unsigned int) sp >> shift;
		sb = (unsigned int) sp ? ((unsigned int) (sp-1) >> shift) + 1 : 0;
		eb = ((unsigned int) (ep+1) >> shift) - 1;
		ee = (unsigned int) ep >> shift;

		if( sb <= eb )
		{
			if( (sb|mask)==(eb|mask) )
			{
				/* same reasion */
				ele = (sele ? sele : eele);
				for( i = sb ; i <= eb ; i++ ){
				 	ele[i & mask] = type;
				}
			}
			else
			{
				if( sele ) for( i = sb ; i <= (sb|mask) ; i++ )
				 	sele[i & mask] = type;
				if( eele ) for( i = eb&(~mask) ; i <= eb ; i++ )
				 	eele[i & mask] = type;
			}
		}

		edepth++;

		if( ss == sb ) sele = 0;
		else sele = get_element( sele , ss & mask , mhmask[cpu][edepth] ,
									subelement , ele_max );
		if( ee == eb ) eele = 0;
		else eele = get_element( eele , ee & mask , mhmask[cpu][edepth] ,
									subelement , ele_max );

	}while( sele || eele );
}


/* ASG 980121 -- allocate all the external memory */
static int memory_allocate_ext (void)
{
	struct ExtMemory *ext = ext_memory;
	int cpu;

	/* loop over all CPUs */
	for (cpu = 0; cpu < cpu_gettotalcpu (); cpu++)
	{
		const struct RomModule *romp = Machine->gamedrv->rom;
		const struct MemoryReadAddress *mra;
		const struct MemoryWriteAddress *mwa;

		int region = Machine->drv->cpu[cpu].memory_region;
		int curr = 0, size = 0;

		/* skip through the ROM regions to the matching one */
		while (romp->name || romp->offset || romp->length)
		{
			/* headers are all zeros except from the offset */
			if (!romp->name && !romp->length && !romp->crc)
			{
				/* got a header; break if this is the match */
				if (curr++ == region)
				{
					size = romp->offset & ~ROMFLAG_MASK;
					break;
				}
			}
			romp++;
		}

		/* now it's time to loop */
		while (1)
		{
			int lowest = 0x7fffffff, end, lastend;

			/* find the base of the lowest memory region that extends past the end */
			for (mra = Machine->drv->cpu[cpu].memory_read; mra->start != -1; mra++)
				if (mra->end >= size && mra->start < lowest) lowest = mra->start;
			for (mwa = Machine->drv->cpu[cpu].memory_write; mwa->start != -1; mwa++)
				if (mwa->end >= size && mwa->start < lowest) lowest = mwa->start;

			/* done if nothing found */
			if (lowest == 0x7fffffff)
				break;

			/* now loop until we find the end of this contiguous block of memory */
			lastend = -1;
			end = lowest;
			while (end != lastend)
			{
				lastend = end;

				/* find the base of the lowest memory region that extends past the end */
				for (mra = Machine->drv->cpu[cpu].memory_read; mra->start != -1; mra++)
					if (mra->start <= end && mra->end > end) end = mra->end + 1;
				for (mwa = Machine->drv->cpu[cpu].memory_write; mwa->start != -1; mwa++)
					if (mwa->start <= end && mwa->end > end) end = mwa->end + 1;
			}

			/* time to allocate */
			ext->start = lowest;
			ext->end = end - 1;
			ext->region = region;
			ext->data = (unsigned char*)malloc (end - lowest);

			/* if that fails, we're through */
			if (!ext->data)
				return 0;

			/* reset the memory */
			memset (ext->data, 0, end - lowest);
			size = ext->end + 1;
			ext++;
		}
	}

	return 1;
}


unsigned char *memory_find_base (int cpu, int offset)
{
	int region = Machine->drv->cpu[cpu].memory_region;
	struct ExtMemory *ext;

	/* look in external memory first */
	for (ext = ext_memory; ext->data; ext++)
		if (ext->region == region && ext->start <= offset && ext->end >= offset)
			return ext->data + (offset - ext->start);

	return ramptr[cpu] + offset;
}

/* make these static so they can be used in a callback by game drivers */

static int rdelement_max = 0;
static int wrelement_max = 0;
static int rdhard_max = HT_USER;
static int wrhard_max = HT_USER;

/* return = FALSE:can't allocate element memory */
int initmemoryhandlers(void)
{
	int i, cpu;
	const struct MemoryReadAddress *memoryread;
	const struct MemoryWriteAddress *memorywrite;
	const struct MemoryReadAddress *mra;
	const struct MemoryWriteAddress *mwa;
	MHELE hardware;
	int abits1,abits2,abits3,abitsmin;
	rdelement_max = 0;
	wrelement_max = 0;
	rdhard_max = HT_USER;
	wrhard_max = HT_USER;

	for( cpu = 0 ; cpu < MAX_CPU ; cpu++ )
		cur_mr_element[cpu] = cur_mw_element[cpu] = 0;

	/* ASG 980121 -- allocate external memory */
	if (!memory_allocate_ext ())
		return 0;

	setOPbasefunc = NULL;

	for( cpu = 0 ; cpu < cpu_gettotalcpu() ; cpu++ )
	{
		const struct MemoryReadAddress *_mra;
		const struct MemoryWriteAddress *_mwa;


		ramptr[cpu] = Machine->memory_region[Machine->drv->cpu[cpu].memory_region];

		/* opcode decryption is currently supported only for the first memory region */
		if (cpu == encrypted_cpu) romptr[cpu] = ROM;
		else romptr[cpu] = ramptr[cpu];


		/* initialize the memory base pointers for memory hooks */
		_mra = Machine->drv->cpu[cpu].memory_read;
		while (_mra->start != -1)
		{
			if (_mra->base) *_mra->base = memory_find_base (cpu, _mra->start);
			if (_mra->size) *_mra->size = _mra->end - _mra->start + 1;
			_mra++;
		}
		_mwa = Machine->drv->cpu[cpu].memory_write;
		while (_mwa->start != -1)
		{
			if (_mwa->base) *_mwa->base = memory_find_base (cpu, _mwa->start);
			if (_mwa->size) *_mwa->size = _mwa->end - _mwa->start + 1;
			_mwa++;
		}

		/* initialize port structures */
		readport[cpu] = Machine->drv->cpu[cpu].port_read;
		writeport[cpu] = Machine->drv->cpu[cpu].port_write;
		if ((Machine->drv->cpu[cpu].cpu_type & ~CPU_FLAGS_MASK) == CPU_Z80 &&
				(Machine->drv->cpu[cpu].cpu_type & CPU_16BIT_PORT) == 0)
			portmask[cpu] = 0xff;
		else
			portmask[cpu] = 0xffff;
	}

	/* initialize grobal handler */
	for( i = 0 ; i < MH_HARDMAX ; i++ ){
		memoryreadoffset[i] = 0;
		memorywriteoffset[i] = 0;
	}
	/* bank1 memory */
	memoryreadhandler[HT_BANK1] = mrh_bank1;
	memorywritehandler[HT_BANK1] = mwh_bank1;
	/* bank2 memory */
	memoryreadhandler[HT_BANK2] = mrh_bank2;
	memorywritehandler[HT_BANK2] = mwh_bank2;
	/* bank3 memory */
	memoryreadhandler[HT_BANK3] = mrh_bank3;
	memorywritehandler[HT_BANK3] = mwh_bank3;
	/* bank4 memory */
	memoryreadhandler[HT_BANK4] = mrh_bank4;
	memorywritehandler[HT_BANK4] = mwh_bank4;
	/* bank5 memory */
	memoryreadhandler[HT_BANK5] = mrh_bank5;
	memorywritehandler[HT_BANK5] = mwh_bank5;
	/* bank6 memory */
	memoryreadhandler[HT_BANK6] = mrh_bank6;
	memorywritehandler[HT_BANK6] = mwh_bank6;
	/* bank7 memory */
	memoryreadhandler[HT_BANK7] = mrh_bank7;
	memorywritehandler[HT_BANK7] = mwh_bank7;
	/* bank8 memory */
	memoryreadhandler[HT_BANK8] = mrh_bank8;
	memorywritehandler[HT_BANK8] = mwh_bank8;
	/* non map memory */
	memoryreadhandler[HT_NON] = mrh_error;
	memorywritehandler[HT_NON] = mwh_error;
	/* NOP memory */
	memoryreadhandler[HT_NOP] = mrh_nop;
	memorywritehandler[HT_NOP] = mwh_nop;
	/* RAMROM memory */
	memorywritehandler[HT_RAMROM] = mwh_ramrom;
	/* ROM memory */
	memorywritehandler[HT_ROM] = mwh_rom;

	/* if any CPU is 24-bit or more, we change the error handlers to be more benign */
	for (cpu = 0; cpu < cpu_gettotalcpu(); cpu++)
		if (ADDRESS_BITS (cpu) >= 24)
		{
			memoryreadhandler[HT_NON] = mrh_error_sparse;
			memorywritehandler[HT_NON] = mwh_error_sparse;
			if ((Machine->drv->cpu[cpu].cpu_type & ~CPU_FLAGS_MASK)==CPU_TMS34010)
			{
				memoryreadhandler[HT_NON] = mrh_error_sparse_bit;
				memorywritehandler[HT_NON] = mwh_error_sparse_bit;
			}
		}

	for( cpu = 0 ; cpu < cpu_gettotalcpu() ; cpu++ )
	{
		/* cpu selection */
		abits1 = ABITS1 (cpu);
		abits2 = ABITS2 (cpu);
		abits3 = ABITS3 (cpu);
		abitsmin = ABITSMIN (cpu);

		/* element shifter , mask set */
		mhshift[cpu][0] = (abits2+abits3);
		mhshift[cpu][1] = abits3;			/* 2nd */
		mhshift[cpu][2] = 0;				/* 3rd (used by set_element)*/
		mhmask[cpu][0]  = MHMASK(abits1);		/*1st(used by set_element)*/
		mhmask[cpu][1]  = MHMASK(abits2);		/*2nd*/
		mhmask[cpu][2]  = MHMASK(abits3);		/*3rd*/

		/* allocate current element */
		if( (cur_mr_element[cpu] = (MHELE *)malloc(sizeof(MHELE)<<abits1)) == 0 )
		{
			shutdownmemoryhandler();
			return 0;
		}

		if( (cur_mw_element[cpu] = (MHELE *)malloc(sizeof(MHELE)<<abits1)) == 0 )
		{
			shutdownmemoryhandler();
			return 0;
		}

		/* initialize curent element table */
		for( i = 0 ; i < (1<<abits1) ; i++ )
		{
			cur_mr_element[cpu][i] = HT_NON;	/* no map memory */
			cur_mw_element[cpu][i] = HT_NON;	/* no map memory */
		}

		memoryread = Machine->drv->cpu[cpu].memory_read;
		memorywrite = Machine->drv->cpu[cpu].memory_write;

		/* memory read handler build */
		mra = memoryread;
		while (mra->start != -1) mra++;
		mra--;

		while (mra >= memoryread)
		{
			int (*handler)(int) = mra->handler;

			if ( (((FPTR)handler)==(((FPTR)MRA_RAM))) ||
			     (((FPTR)handler)==((FPTR)MRA_ROM))) 
				hardware = HT_RAM;	/* sprcial case ram read */
			else if (((FPTR)handler)==((FPTR)MRA_BANK1))
			{
				hardware = HT_BANK1;
				memoryreadoffset[1] = bankreadoffset[1] = mra->start;
				cpu_bankbase[1] = memory_find_base (cpu, mra->start);
			}
			else if (((FPTR)handler)==((FPTR)MRA_BANK2))
			{
				hardware = HT_BANK2;
				memoryreadoffset[2] = bankreadoffset[2] = mra->start;
				cpu_bankbase[2] = memory_find_base (cpu, mra->start);
			}
			else if  (((FPTR)handler)==((FPTR)MRA_BANK3))
			{
				hardware = HT_BANK3;
				memoryreadoffset[3] = bankreadoffset[3] = mra->start;
				cpu_bankbase[3] = memory_find_base (cpu, mra->start);
			}
			else if  (((FPTR)handler)==((FPTR)MRA_BANK4))
			{
				hardware = HT_BANK4;
				memoryreadoffset[4] = bankreadoffset[4] = mra->start;
				cpu_bankbase[4] = memory_find_base (cpu, mra->start);
			}
			else if  (((FPTR)handler)==((FPTR)MRA_BANK5))
			{
				hardware = HT_BANK5;
				memoryreadoffset[5] = bankreadoffset[5] = mra->start;
				cpu_bankbase[5] = memory_find_base (cpu, mra->start);
			}
			else if  (((FPTR)handler)==((FPTR)MRA_BANK6))
			{
				hardware = HT_BANK6;
				memoryreadoffset[6] = bankreadoffset[6] = mra->start;
				cpu_bankbase[6] = memory_find_base (cpu, mra->start);
			}
			else if  (((FPTR)handler)==((FPTR)MRA_BANK7))
			{
				hardware = HT_BANK7;
				memoryreadoffset[7] = bankreadoffset[7] = mra->start;
				cpu_bankbase[7] = memory_find_base (cpu, mra->start);
			}
			else if  (((FPTR)handler)==((FPTR)MRA_BANK8))
			{
				hardware = HT_BANK8;
				memoryreadoffset[8] = bankreadoffset[8] = mra->start;
				cpu_bankbase[8] = memory_find_base (cpu, mra->start);
			}
			else if  (((FPTR)handler)==((FPTR)MRA_NOP))
				hardware = HT_NOP;
			else
			{
				/* create newer hardware handler */
				if( rdhard_max == MH_HARDMAX )
				{
					hardware = 0;
				}
				else
				{
					/* regist hardware function */
					hardware = rdhard_max++;
					memoryreadhandler[hardware] = handler;
					memoryreadoffset[hardware] = mra->start;
				}
			}
			/* hardware element table make */
			set_element( cpu , cur_mr_element[cpu] ,
				(((unsigned int) mra->start) >> abitsmin) ,
				(((unsigned int) mra->end) >> abitsmin) ,
				hardware , readhardware , &rdelement_max );
			mra--;
		}

		/* memory write handler build */
		mwa = memorywrite;
		while (mwa->start != -1) mwa++;
		mwa--;

		while (mwa >= memorywrite)
		{
			void (*handler)(int,int) = mwa->handler;
#ifdef SGI_FIX_MWA_NOP
			if ((FPTR)handler == (FPTR)MWA_NOP) {
				hardware = HT_NOP;
			} else {
#endif

			if  (((FPTR)handler)==((FPTR)MWA_RAM))
				hardware = HT_RAM;	/* sprcial case ram write */
			else if (((FPTR)handler)==((FPTR)MWA_BANK1))
			{
				hardware = HT_BANK1;
				memorywriteoffset[1] = bankwriteoffset[1] = mwa->start;
				cpu_bankbase[1] = memory_find_base (cpu, mwa->start);
			}
			else if (((FPTR)handler)==((FPTR)MWA_BANK2))
			{
				hardware = HT_BANK2;
				memorywriteoffset[2] = bankwriteoffset[2] = mwa->start;
				cpu_bankbase[2] = memory_find_base (cpu, mwa->start);
			}
			else if (((FPTR)handler)==((FPTR)MWA_BANK3))
			{
				hardware = HT_BANK3;
				memorywriteoffset[3] = bankwriteoffset[3] = mwa->start;
				cpu_bankbase[3] = memory_find_base (cpu, mwa->start);
			}
			else if (((FPTR)handler)==((FPTR)MWA_BANK4))
			{
				hardware = HT_BANK4;
				memorywriteoffset[4] = bankwriteoffset[4] = mwa->start;
				cpu_bankbase[4] = memory_find_base (cpu, mwa->start);
			}
			else if (((FPTR)handler)==((FPTR)MWA_BANK5))
			{
				hardware = HT_BANK5;
				memorywriteoffset[5] = bankwriteoffset[5] = mwa->start;
				cpu_bankbase[5] = memory_find_base (cpu, mwa->start);
			}
			else if (((FPTR)handler)==((FPTR)MWA_BANK6))
			{
				hardware = HT_BANK6;
				memorywriteoffset[6] = bankwriteoffset[6] = mwa->start;
				cpu_bankbase[6] = memory_find_base (cpu, mwa->start);
			}
			else if (((FPTR)handler)==((FPTR)MWA_BANK7))
			{
				hardware = HT_BANK7;
				memorywriteoffset[7] = bankwriteoffset[7] = mwa->start;
				cpu_bankbase[7] = memory_find_base (cpu, mwa->start);
			}
			else if (((FPTR)handler)==((FPTR)MWA_BANK8))
			{
				hardware = HT_BANK8;
				memorywriteoffset[8] = bankwriteoffset[8] = mwa->start;
				cpu_bankbase[8] = memory_find_base (cpu, mwa->start);
			}
			else if (((FPTR)handler)==((FPTR)MWA_NOP))
				hardware = HT_NOP;
			else if (((FPTR)handler)==((FPTR)MWA_RAMROM))
				hardware = HT_RAMROM;
			else if (((FPTR)handler)==((FPTR)MWA_ROM))
				hardware = HT_ROM;
			else
			{
				/* create newer hardware handler */
				if( wrhard_max == MH_HARDMAX ){
					hardware = 0;
				}else{
					/* regist hardware function */
					hardware = wrhard_max++;
					memorywritehandler[hardware] = handler;
					memorywriteoffset[hardware]  = mwa->start;
				}
			}
#ifdef SGI_FIX_MWA_NOP
			}
#endif
			/* hardware element table make */
			set_element( cpu , cur_mw_element[cpu] ,
				(int) (((unsigned int) mwa->start) >> abitsmin) ,
				(int) (((unsigned int) mwa->end) >> abitsmin) ,
				hardware , (MHELE *)writehardware , &wrelement_max );
			mwa--;
		}
	}

	return 1;	/* ok */
}

void memorycontextswap(int activecpu)
{
	RAM = cpu_bankbase[0] = ramptr[activecpu];
	ROM = romptr[activecpu];

	cur_mrhard = cur_mr_element[activecpu];
	cur_mwhard = cur_mw_element[activecpu];

	/* ASG: port speedup */
	cur_readport = readport[activecpu];
	cur_writeport = writeport[activecpu];
	cur_portmask = portmask[activecpu];

	/* op code memory pointer */
	ophw = HT_RAM;
	OP_RAM = RAM;
	OP_ROM = ROM;
}

void shutdownmemoryhandler(void)
{
	struct ExtMemory *ext;
	int cpu;

	for( cpu = 0 ; cpu < MAX_CPU ; cpu++ )
	{
		if( cur_mr_element[cpu] != 0 )
		{
			free( cur_mr_element[cpu] );
			cur_mr_element[cpu] = 0;
		}
		if( cur_mw_element[cpu] != 0 )
		{
			free( cur_mw_element[cpu] );
			cur_mw_element[cpu] = 0;
		}
	}

	/* ASG 980121 -- free all the external memory */
	for (ext = ext_memory; ext->data; ext++)
		free (ext->data);
	memset (ext_memory, 0, sizeof (ext_memory));
}

void updatememorybase(int activecpu)
{
	/* keep track of changes to RAM and ROM pointers (bank switching) */
	ramptr[activecpu] = RAM;
	romptr[activecpu] = ROM;
}



/***************************************************************************

  Perform a memory read. This function is called by the CPU emulation.

***************************************************************************/

int cpu_readmem16 (int address)
{
	MHELE hw;

	/* 1st element link */
	hw = cur_mrhard[address >> (ABITS2_16 + ABITS_MIN_16)];
	if (!hw) return RAM[address];
	if (hw >= MH_HARDMAX)
	{
		/* 2nd element link */
		hw = readhardware[((hw - MH_HARDMAX) << MH_SBITS) + ((address >> ABITS_MIN_16) & MHMASK(ABITS2_16))];
		if (!hw) return RAM[address];
	}

	/* fallback to handler */
	return memoryreadhandler[hw](address - memoryreadoffset[hw]);
}

int cpu_readmem16lew (int address)
{
	int shift, data;
	MHELE hw;

	/* 1st element link */
	hw = cur_mrhard[address >> (ABITS2_16LEW + ABITS_MIN_16LEW)];
	if (hw <= HT_BANKMAX) return cpu_bankbase[hw][BYTE_XOR_LE (address - memoryreadoffset[hw])];
	if (hw >= MH_HARDMAX)
	{
		/* 2nd element link */
		hw = readhardware[((hw - MH_HARDMAX) << MH_SBITS) + ((address >> ABITS_MIN_16LEW) & MHMASK(ABITS2_16LEW))];
		if (hw <= HT_BANKMAX) return cpu_bankbase[hw][BYTE_XOR_LE (address - memoryreadoffset[hw])];
	}

	/* fallback to handler */
	shift = (address & 1) << 3;
	address &= ~1;
	data = memoryreadhandler[hw](address - memoryreadoffset[hw]);
	return (data >> shift) & 0xff;
}


int cpu_readmem16lew_word (int address)
{
	MHELE hw;

	/* reads across word boundaries must be broken up */
	if (address & 1)
		return cpu_readmem16lew (address) | (cpu_readmem16lew (address + 1) << 8);

	/* 1st element link */
	hw = cur_mrhard[address >> (ABITS2_16LEW + ABITS_MIN_16LEW)];
	if (hw <= HT_BANKMAX) return READ_WORD (&cpu_bankbase[hw][address - memoryreadoffset[hw]]);
	if (hw >= MH_HARDMAX)
	{
		/* 2nd element link */
		hw = readhardware[((hw - MH_HARDMAX) << MH_SBITS) + ((address >> ABITS_MIN_16LEW) & MHMASK(ABITS2_16LEW))];
		if (hw <= HT_BANKMAX) return READ_WORD (&cpu_bankbase[hw][address - memoryreadoffset[hw]]);
	}

	/* fallback to handler */
	return memoryreadhandler[hw](address - memoryreadoffset[hw]);
}


int cpu_readmem20 (int address)
{
	MHELE hw;

	/* 1st element link */
	hw = cur_mrhard[address >> (ABITS2_20 + ABITS_MIN_20)];
	if (!hw) return RAM[address];
	if (hw >= MH_HARDMAX)
	{
		/* 2nd element link */
		hw = readhardware[((hw - MH_HARDMAX) << MH_SBITS) + ((address >> ABITS_MIN_20) & MHMASK(ABITS2_20))];
		if (!hw) return RAM[address];
	}

	/* fallback to handler */
	return memoryreadhandler[hw](address - memoryreadoffset[hw]);
}

int cpu_readmem21 (int address)
{
        MHELE hw;
        /* 1st element link */
        hw = cur_mrhard[address >> (ABITS2_21 + ABITS_MIN_21)];
        if (hw <= HT_BANKMAX) return cpu_bankbase[hw][(address - memoryreadoffset[hw])];
        if (hw >= MH_HARDMAX)
        {
                /* 2nd element link */
                hw = readhardware[((hw - MH_HARDMAX) << MH_SBITS) + ((address >> ABITS_MIN_21) & MHMASK(ABITS2_21))];
                if (hw <= HT_BANKMAX) return cpu_bankbase[hw][(address - memoryreadoffset[hw])];
        }

        return memoryreadhandler[hw](address - memoryreadoffset[hw]);
}

int cpu_readmem24 (int address)
{
	int shift, data;
	MHELE hw;

	/* 1st element link */
	hw = cur_mrhard[address >> (ABITS2_24 + ABITS_MIN_24)];
	if (hw <= HT_BANKMAX) return cpu_bankbase[hw][BYTE_XOR_BE (address - memoryreadoffset[hw])];
	if (hw >= MH_HARDMAX)
	{
		/* 2nd element link */
		hw = readhardware[((hw - MH_HARDMAX) << MH_SBITS) + ((address >> ABITS_MIN_24) & MHMASK(ABITS2_24))];
		if (hw <= HT_BANKMAX) return cpu_bankbase[hw][BYTE_XOR_BE (address - memoryreadoffset[hw])];
	}

	/* fallback to handler */
	shift = ((address ^ 1) & 1) << 3;
	address &= ~1;
	data = memoryreadhandler[hw](address - memoryreadoffset[hw]);
	return (data >> shift) & 0xff;
}


int cpu_readmem24_word (int address)
{
	/* should only be called on even byte addresses */
	MHELE hw;

	/* 1st element link */
	hw = cur_mrhard[address >> (ABITS2_24 + ABITS_MIN_24)];
	if (hw <= HT_BANKMAX) return READ_WORD (&cpu_bankbase[hw][address - memoryreadoffset[hw]]);
	if (hw >= MH_HARDMAX)
	{
		/* 2nd element link */
		hw = readhardware[((hw - MH_HARDMAX) << MH_SBITS) | ((address >> ABITS_MIN_24) & MHMASK(ABITS2_24))];
		if (hw <= HT_BANKMAX) return READ_WORD (&cpu_bankbase[hw][address - memoryreadoffset[hw]]);
	}

	/* fallback to handler */
	return memoryreadhandler[hw](address - memoryreadoffset[hw]);
}


int cpu_readmem24_dword (int address)
{
	/* should only be called on even byte addresses */
	unsigned int val;
	MHELE hw;

	if (!(address&0x02))  /* words might not have same handler - AJP 980816 */
	{
		/* 1st element link */
		hw = cur_mrhard[address >> (ABITS2_24 + ABITS_MIN_24)];
        if (hw != cur_mrhard[(address + 2) >> (ABITS2_24 + ABITS_MIN_24)])
			return (((cpu_readmem24_word(address)) << 16) | ((cpu_readmem24_word(address + 2))&0xffff));
        if (hw <= HT_BANKMAX)
		{
		  	#ifdef ACORN /* GSL 980224 misaligned dword load case */
			if (address & 3)
			{
				unsigned char *addressbase = (unsigned char *)&cpu_bankbase[hw][address - memoryreadoffset[hw]];
				return ((*addressbase)<<SHIFT0) | (*(addressbase+1)<<SHIFT1) | (*(addressbase+2)<<SHIFT2) | (*(addressbase+3)<<SHIFT3);
			}
			else
			{
				val = *(unsigned int *)&cpu_bankbase[hw][address - memoryreadoffset[hw]];
				return BIG_DWORD_BE(val);
			}

			#else

			val = *(unsigned int *)&cpu_bankbase[hw][address - memoryreadoffset[hw]];
			return BIG_DWORD_BE(val);
			#endif

		}
		if (hw >= MH_HARDMAX)
		{
			MHELE hw2;
			/* 2nd element link */
			hw -= MH_HARDMAX;
			hw2 = readhardware[(hw << MH_SBITS) + ((address >> ABITS_MIN_24) & MHMASK(ABITS2_24))];
			if (hw2 != readhardware[(hw << MH_SBITS) | (((address + 2) >> ABITS_MIN_24) & MHMASK(ABITS2_24))])
				return (((cpu_readmem24_word(address)) << 16) | ((cpu_readmem24_word(address + 2))&0xffff));
			hw = hw2;
			if (hw < HT_BANKMAX)
			{
			  	#ifdef ACORN
				if (address & 3)
				{
					unsigned char *addressbase = (unsigned char *)&cpu_bankbase[hw][address - memoryreadoffset[hw]];
					return ((*addressbase)<<SHIFT0) + (*(addressbase+1)<<SHIFT1) + (*(addressbase+2)<<SHIFT2) + (*(addressbase+3)<<SHIFT3);
				}
				else
				{
					val = *(unsigned int *)&cpu_bankbase[hw][address - memoryreadoffset[hw]];
					return BIG_DWORD_BE(val);
				}

				#else

				val = *(unsigned int *)&cpu_bankbase[hw][address - memoryreadoffset[hw]];
				return BIG_DWORD_BE(val);
				#endif

			}
		}

		/* fallback to handler */
        address -= memoryreadoffset[hw];
		return (memoryreadhandler[hw](address) << 16) | (memoryreadhandler[hw](address + 2) & 0xffff);
    }
	else
	{
		return (((cpu_readmem24_word(address)) << 16) | ((cpu_readmem24_word(address + 2))&0xffff));
	}
}

/*
 *
 * This is for the TMS34010
 * When writing a dword 0x12345678, the bytes get written as
 * 0x78, 0x56, 0x34, 0x12
 *
 * So, a read from an even byte gives the low 8 bits of a word
 */
int cpu_readmem29 (int address)  /* AJP 980803 */
{
	unsigned int shift, data;
	MHELE hw;

	/* 1st element link */
	hw = cur_mrhard[(unsigned int) address >> (ABITS2_29 + ABITS_MIN_29)];
	if (hw <= HT_BANKMAX) return cpu_bankbase[hw][BYTE_XOR_LE (address - memoryreadoffset[hw])];
	if (hw >= MH_HARDMAX)
	{
		/* 2nd element link */
		hw = readhardware[((hw - MH_HARDMAX) << MH_SBITS) + (((unsigned int) address >> ABITS_MIN_29) & MHMASK(ABITS2_29))];
		if (hw <= HT_BANKMAX) return cpu_bankbase[hw][BYTE_XOR_LE (address - memoryreadoffset[hw])];
	}

	/* fallback to handler */
	shift = (((unsigned int) address) & 1) << 3;
	address &= ~1;
	data = memoryreadhandler[hw](address - memoryreadoffset[hw]);
	return (data >> shift) & 0xff;
}


int cpu_readmem29_word (int address)  /* AJP 980803 */
{
	/* should only be called on even byte addresses */
	MHELE hw;

	/* 1st element link */
	hw = cur_mrhard[(unsigned int) address >> (ABITS2_29 + ABITS_MIN_29)];
	if (hw <= HT_BANKMAX) return READ_WORD (&cpu_bankbase[hw][address - memoryreadoffset[hw]]);
	if (hw >= MH_HARDMAX)
	{
		/* 2nd element link */
		hw = readhardware[((hw - MH_HARDMAX) << MH_SBITS) | (((unsigned int) address >> ABITS_MIN_29) & MHMASK(ABITS2_29))];
		if (hw <= HT_BANKMAX) return READ_WORD (&cpu_bankbase[hw][address - memoryreadoffset[hw]]);
	}

	/* fallback to handler */
	return memoryreadhandler[hw](address - memoryreadoffset[hw]);
}


int cpu_readmem29_dword (int address)  /* AJP 980803 */
{
	/* should only be called on even byte addresses */
	unsigned int val;
	MHELE hw;

	if (!(address&0x02))  /* words might not have same handler - AJP 980816 */
	{
		/* 1st element link */
		hw = cur_mrhard[(unsigned int) address >> (ABITS2_29 + ABITS_MIN_29)];
		if (hw <= HT_BANKMAX)
		{
		  	#ifdef ACORN /* GSL 980224 misaligned dword load case */
			if (address & 3)
			{
				unsigned char *addressbase = (unsigned char *)&cpu_bankbase[hw][address - memoryreadoffset[hw]];
				return ((*addressbase)<<SHIFT0) | (*(addressbase+1)<<SHIFT1) | (*(addressbase+2)<<SHIFT2) | (*(addressbase+3)<<SHIFT3);
			}
			else
			{
				val = *(unsigned int *)&cpu_bankbase[hw][address - memoryreadoffset[hw]];
				return BIG_DWORD_LE(val);
			}

			#else

			val = *(unsigned int *)&cpu_bankbase[hw][address - memoryreadoffset[hw]];
			return BIG_DWORD_LE(val);
			#endif

		}
		if (hw >= MH_HARDMAX)
		{
			/* 2nd element link */
			hw = readhardware[((hw - MH_HARDMAX) << MH_SBITS) + (((unsigned int) address >> ABITS_MIN_29) & MHMASK(ABITS2_29))];
			if (hw <= HT_BANKMAX)
			{
			  	#ifdef ACORN
				if (address & 3)
				{
					unsigned char *addressbase = (unsigned char *)&cpu_bankbase[hw][address - memoryreadoffset[hw]];
					return ((*addressbase)<<SHIFT0) + (*(addressbase+1)<<SHIFT1) + (*(addressbase+2)<<SHIFT2) + (*(addressbase+3)<<SHIFT3);
				}
				else
				{
					val = *(unsigned int *)&cpu_bankbase[hw][address - memoryreadoffset[hw]];
					return BIG_DWORD_LE(val);
				}

				#else

				val = *(unsigned int *)&cpu_bankbase[hw][address - memoryreadoffset[hw]];
				return BIG_DWORD_LE(val);
				#endif

			}
		}

		/* fallback to handler */
		address -= memoryreadoffset[hw];
		return (memoryreadhandler[hw]((unsigned int) address) & 0xffff) + (memoryreadhandler[hw]((unsigned int) address + 2) <<16);
	}
	else
	{
		return (((cpu_readmem29_word(address)) & 0xffff) | ((cpu_readmem29_word((address + 2)&0x1fffffff)) <<16));
	}
}

/***************************************************************************

  Perform a memory write. This function is called by the CPU emulation.

***************************************************************************/

void cpu_writemem16 (int address, int data)
{
	MHELE hw;

	/* 1st element link */
	hw = cur_mwhard[address >> (ABITS2_16 + ABITS_MIN_16)];
	if (!hw)
	{
		RAM[address] = data;
		return;
	}
	if (hw >= MH_HARDMAX)
	{
		/* 2nd element link */
		hw = writehardware[((hw - MH_HARDMAX) << MH_SBITS) + ((address >> ABITS_MIN_16) & MHMASK(ABITS2_16))];
		if (!hw)
		{
			RAM[address] = data;
			return;
		}
	}

	/* fallback to handler */
	memorywritehandler[hw](address - memorywriteoffset[hw], data);
}


void cpu_writemem16lew (int address, int data)
{
	int shift;
	MHELE hw;

	/* 1st element link */
	hw = cur_mwhard[address >> (ABITS2_16LEW + ABITS_MIN_16LEW)];
	if (hw <= HT_BANKMAX)
	{
		cpu_bankbase[hw][BYTE_XOR_LE (address - memorywriteoffset[hw])] = data;
		return;
	}
	if (hw >= MH_HARDMAX)
	{
		/* 2nd element link */
		hw = writehardware[((hw - MH_HARDMAX) << MH_SBITS) + ((address >> ABITS_MIN_16LEW) & MHMASK(ABITS2_16LEW))];
		if (hw <= HT_BANKMAX)
		{
			cpu_bankbase[hw][BYTE_XOR_LE (address - memorywriteoffset[hw])] = data;
			return;
		}
	}

	/* fallback to handler */
	shift = (address & 1) << 3;
	address &= ~1;
	memorywritehandler[hw](address - memorywriteoffset[hw], (0xff000000 >> shift) | ((data & 0xff) << shift));
}


void cpu_writemem16lew_word (int address, int data)
{
	MHELE hw;

	/* writes across word boundaries must be broken up */
	if (address & 1)
	{
		cpu_writemem16lew (address, data & 0xff);
		cpu_writemem16lew (address + 1, data >> 8);
		return;
	}

	/* 1st element link */
	hw = cur_mwhard[address >> (ABITS2_16LEW + ABITS_MIN_16LEW)];
	if (hw <= HT_BANKMAX)
	{
		WRITE_WORD (&cpu_bankbase[hw][address - memorywriteoffset[hw]], data);
		return;
	}
	if (hw >= MH_HARDMAX)
	{
		/* 2nd element link */
		hw = writehardware[((hw - MH_HARDMAX) << MH_SBITS) | ((address >> ABITS_MIN_16LEW) & MHMASK(ABITS2_16LEW))];
		if (hw <= HT_BANKMAX)
		{
			WRITE_WORD (&cpu_bankbase[hw][address - memorywriteoffset[hw]], data);
			return;
		}
	}

	/* fallback to handler */
	memorywritehandler[hw](address - memorywriteoffset[hw], data & 0xffff);
}


void cpu_writemem20 (int address, int data)
{
	MHELE hw;

	/* 1st element link */
	hw = cur_mwhard[address >> (ABITS2_20 + ABITS_MIN_20)];
	if (!hw)
	{
		RAM[address] = data;
		return;
	}
	if ( hw >= MH_HARDMAX)
	{
		/* 2nd element link */
		hw = writehardware[((hw - MH_HARDMAX) << MH_SBITS) + ((address >> ABITS_MIN_20) & MHMASK(ABITS2_20))];
		if (!hw)
		{
			RAM[address] = data;
			return;
		}
	}

	/* fallback to handler */
	memorywritehandler[hw](address - memorywriteoffset[hw],data);
}

void cpu_writemem21 (int address, int data)
{       
        MHELE hw; 
        /* 1st element link */
        hw = cur_mwhard[address >> (ABITS2_21 + ABITS_MIN_21)];
                
        if (hw <= HT_BANKMAX)
        {
                cpu_bankbase[hw][(address - memorywriteoffset[hw])] = data;
                return;
        }
        if (hw >= MH_HARDMAX)
        {
                /* 2nd element link */ 
                hw = writehardware[((hw - MH_HARDMAX) << MH_SBITS) + ((address >> ABITS_MIN_21) & MHMASK(ABITS2_21))];
                if (hw <= HT_BANKMAX)
                {
                        cpu_bankbase[hw][(address - memorywriteoffset[hw])] = data;
                        return;
                }
        }

        /* fallback to handler */
        memorywritehandler[hw](address - memorywriteoffset[hw],data);
}

void cpu_writemem24 (int address, int data)
{
	int shift;
	MHELE hw;

	/* 1st element link */
	hw = cur_mwhard[address >> (ABITS2_24 + ABITS_MIN_24)];
	if (hw <= HT_BANKMAX)
	{
		cpu_bankbase[hw][BYTE_XOR_BE (address - memorywriteoffset[hw])] = data;
		return;
	}
	if (hw >= MH_HARDMAX)
	{
		/* 2nd element link */
		hw = writehardware[((hw - MH_HARDMAX) << MH_SBITS) + ((address >> ABITS_MIN_24) & MHMASK(ABITS2_24))];
		if (hw <= HT_BANKMAX)
		{
			cpu_bankbase[hw][BYTE_XOR_BE (address - memorywriteoffset[hw])] = data;
			return;
		}
	}

	/* fallback to handler */
	shift = ((address ^ 1) & 1) << 3;
	address &= ~1;
	memorywritehandler[hw](address - memorywriteoffset[hw], (0xff000000 >> shift) | ((data & 0xff) << shift));
}


void cpu_writemem24_word (int address, int data)
{
	/* should only be called on even byte addresses */
	MHELE hw;

	/* 1st element link */
	hw = cur_mwhard[address >> (ABITS2_24 + ABITS_MIN_24)];
	if (hw <= HT_BANKMAX)
	{
		WRITE_WORD (&cpu_bankbase[hw][address - memorywriteoffset[hw]], data);
		return;
	}
	if (hw >= MH_HARDMAX)
	{
		/* 2nd element link */
		hw = writehardware[((hw - MH_HARDMAX) << MH_SBITS) | ((address >> ABITS_MIN_24) & MHMASK(ABITS2_24))];
		if (hw <= HT_BANKMAX)
		{
			WRITE_WORD (&cpu_bankbase[hw][address - memorywriteoffset[hw]], data);
			return;
		}
	}

	/* fallback to handler */
	memorywritehandler[hw](address - memorywriteoffset[hw], data & 0xffff);
}


void cpu_writemem24_dword (int address, int data)
{
	/* should only be called on even byte addresses */
	MHELE hw;

	if (!(address&0x02))  /* words might not have same handler - AJP 980816 */
	{
		/* 1st element link */
		hw = cur_mwhard[address >> (ABITS2_24 + ABITS_MIN_24)];
        if (hw != cur_mwhard[(address + 2) >> (ABITS2_24 + ABITS_MIN_24)])
		{
			cpu_writemem24_word(address, (data >> 16) & 0xffff);
			cpu_writemem24_word(address + 2, data & 0xffff);
			return;
		}
        if (hw <= HT_BANKMAX)
		{
		  	#ifdef ACORN /* GSL 980224 misaligned dword store case */
			if (address & 3)
			{
				unsigned char *addressbase = (unsigned char *)&cpu_bankbase[hw][address - memorywriteoffset[hw]];
				*addressbase = data >> SHIFT0;
				*(addressbase+1) = data >> SHIFT1;
				*(addressbase+2) = data >> SHIFT2;
				*(addressbase+3) = data >> SHIFT3;
				return;
			}
			else
			{
				data = BIG_DWORD_BE ((unsigned int)data);
				*(unsigned int *)&cpu_bankbase[hw][address - memorywriteoffset[hw]] = data;
				return;
			}

			#else

			data = BIG_DWORD_BE ((unsigned int)data);
			*(unsigned int *)&cpu_bankbase[hw][address - memorywriteoffset[hw]] = data;
			return;
			#endif

		}
		if (hw >= MH_HARDMAX)
		{
			MHELE hw2;
			/* 2nd element link */
			hw -= MH_HARDMAX;
			hw2 = writehardware[(hw << MH_SBITS) | ((address >> ABITS_MIN_24) & MHMASK(ABITS2_24))];
			if (hw2 != writehardware[(hw << MH_SBITS) | (((address + 2) >> ABITS_MIN_24) & MHMASK(ABITS2_24))])
			{
				cpu_writemem24_word(address, (data >> 16) &0xffff);
				cpu_writemem24_word(address + 2, data & 0xffff);
				return;
			}
            hw = hw2;
			if (hw <= HT_BANKMAX)
			{
			  	#ifdef ACORN
				if (address & 3)
				{
					unsigned char *addressbase = (unsigned char *)&cpu_bankbase[hw][address - memorywriteoffset[hw]];
					*addressbase = data >> SHIFT0;
					*(addressbase+1) = data >> SHIFT1;
					*(addressbase+2) = data >> SHIFT2;
					*(addressbase+3) = data >> SHIFT3;
					return;
				}
				else
				{
					data = BIG_DWORD_BE ((unsigned int)data);
					*(unsigned int *)&cpu_bankbase[hw][address - memorywriteoffset[hw]] = data;
					return;
				}

				#else

				data = BIG_DWORD_BE ((unsigned int)data);
				*(unsigned int *)&cpu_bankbase[hw][address - memorywriteoffset[hw]] = data;
				return;
				#endif

			}
		}

		/* fallback to handler */
		address -= memorywriteoffset[hw];
        memorywritehandler[hw](address, (data >> 16) & 0xffff);
		memorywritehandler[hw](address + 2, data & 0xffff);
    }
	else
	{
		cpu_writemem24_word(address, (data >> 16) &0xffff);
		cpu_writemem24_word(address + 2, data & 0xffff);
	}
}

void cpu_writemem29 (int address, int data)  /* AJP 980803 */
{
	unsigned int shift;
	MHELE hw;

	/* 1st element link */
	hw = cur_mwhard[(unsigned int) address >> (ABITS2_29 + ABITS_MIN_29)];
	if (hw <= HT_BANKMAX)
	{
		cpu_bankbase[hw][BYTE_XOR_LE (address - memorywriteoffset[hw])] = data;
		return;
	}
	if (hw >= MH_HARDMAX)
	{
		/* 2nd element link */
		hw = writehardware[((hw - MH_HARDMAX) << MH_SBITS) | (((unsigned int) address >> ABITS_MIN_29) & MHMASK(ABITS2_29))];
		if (hw <= HT_BANKMAX)
		{
			cpu_bankbase[hw][BYTE_XOR_LE (address - memorywriteoffset[hw])] = data;
			return;
		}
	}

	/* fallback to handler */
	shift = (((unsigned int) address) & 1) << 3;
	address &= ~1;
	memorywritehandler[hw](address - memorywriteoffset[hw], (0xff000000 >> shift) | ((data & 0xff) << shift));
}


void cpu_writemem29_word (int address, int data)  /* AJP 980803 */
{
	/* should only be called on even byte addresses */
	MHELE hw;

	/* 1st element link */
	hw = cur_mwhard[(unsigned int) address >> (ABITS2_29 + ABITS_MIN_29)];
	if (hw <= HT_BANKMAX)
	{
		WRITE_WORD (&cpu_bankbase[hw][address - memorywriteoffset[hw]], data);
		return;
	}
	if (hw >= MH_HARDMAX)
	{
		/* 2nd element link */
		hw = writehardware[((hw - MH_HARDMAX) << MH_SBITS) | (((unsigned int) address >> ABITS_MIN_29) & MHMASK(ABITS2_29))];
		if (hw <= HT_BANKMAX)
		{
			WRITE_WORD (&cpu_bankbase[hw][address - memorywriteoffset[hw]], data);
			return;
		}
	}

	/* fallback to handler */
	memorywritehandler[hw](address - memorywriteoffset[hw], data & 0xffff);
}

void cpu_writemem29_word_masked (int address, int data)  /* AJP 980803 */
{
	/* should only be called on even byte addresses */
	/* data is (mask<<16) + data_word */
	MHELE hw;

	/* 1st element link */
	hw = cur_mwhard[(unsigned int) address >> (ABITS2_29 + ABITS_MIN_29)];
	if (hw <= HT_BANKMAX)
	{
		COMBINE_WORD_MEM (&cpu_bankbase[hw][address - memorywriteoffset[hw]], data);
		return;
	}
	if (hw >= MH_HARDMAX)
	{
		/* 2nd element link */
		hw = writehardware[((hw - MH_HARDMAX) << MH_SBITS) | (((unsigned int) address >> ABITS_MIN_29) & MHMASK(ABITS2_29))];
		if (hw <= HT_BANKMAX)
		{
			COMBINE_WORD_MEM (&cpu_bankbase[hw][address - memorywriteoffset[hw]], data);
			return;
		}
	}

	/* fallback to handler */
	memorywritehandler[hw](address - memorywriteoffset[hw], data);
}

void cpu_writemem29_dword (int address, int data)  /* AJP 980803 */
{
	/* should only be called on even byte addresses */
	MHELE hw;

	if (!(address&0x02))  /* words might not have same handler - AJP 980816 */
	{
		/* 1st element link */
		hw = cur_mwhard[(unsigned int) address >> (ABITS2_29 + ABITS_MIN_29)];
		if (hw <= HT_BANKMAX)
		{
		  	#ifdef ACORN /* GSL 980224 misaligned dword store case */
			if (address & 3)
			{
				unsigned char *addressbase = (unsigned char *)&cpu_bankbase[hw][address - memorywriteoffset[hw]];
				*addressbase = data >> SHIFT0;
				*(addressbase+1) = data >> SHIFT1;
				*(addressbase+2) = data >> SHIFT2;
				*(addressbase+3) = data >> SHIFT3;
				return;
			}
			else
			{
				data = BIG_DWORD_LE ((unsigned int)data);
				*(unsigned int *)&cpu_bankbase[hw][address - memorywriteoffset[hw]] = data;
				return;
			}

			#else

			data = BIG_DWORD_LE ((unsigned int)data);
			*(unsigned int *)&cpu_bankbase[hw][address - memorywriteoffset[hw]] = data;
			return;
			#endif

		}
		if (hw >= MH_HARDMAX)
		{
			/* 2nd element link */
			hw = writehardware[((hw - MH_HARDMAX) << MH_SBITS) | (((unsigned int) address >> ABITS_MIN_29) & MHMASK(ABITS2_29))];
			if (hw <= HT_BANKMAX)
			{
			  	#ifdef ACORN  /* this may be wrong */
				if (address & 3)
				{
					unsigned char *addressbase = (unsigned char *)&cpu_bankbase[hw][address - memorywriteoffset[hw]];
					*addressbase = data >> SHIFT0;
					*(addressbase+1) = data >> SHIFT1;
					*(addressbase+2) = data >> SHIFT2;
					*(addressbase+3) = data >> SHIFT3;
					return;
				}
				else
				{
					data = BIG_DWORD_LE ((unsigned int)data);
					*(unsigned int *)&cpu_bankbase[hw][address - memorywriteoffset[hw]] = data;
					return;
				}

				#else

				data = BIG_DWORD_LE ((unsigned int)data);
				*(unsigned int *)&cpu_bankbase[hw][address - memorywriteoffset[hw]] = data;
				return;
				#endif

			}
		}

		/* fallback to handler */
		address -= memorywriteoffset[hw];
		memorywritehandler[hw](address, data & 0xffff);
		memorywritehandler[hw](address + 2, (data >> 16) & 0xffff);
	}
	else
	{
		cpu_writemem29_word(address, data & 0xffff);
		cpu_writemem29_word((address + 2)&0x1fffffff, (data >> 16) &0xffff);
	}
}

/***************************************************************************

  Perform an I/O port read. This function is called by the CPU emulation.

***************************************************************************/
int cpu_readport(int Port)
{
	const struct IOReadPort *iorp = cur_readport;


	if (iorp)
	{
		Port &= cur_portmask;
		while (iorp->start != -1)
		{
			if (Port >= iorp->start && Port <= iorp->end)
			{
				int (*handler)(int) = iorp->handler;


				if (handler == IORP_NOP) return 0;
				else return (*handler)(Port - iorp->start);
			}

			iorp++;
		}
	}

	return 0;
}


/***************************************************************************

  Perform an I/O port write. This function is called by the CPU emulation.

***************************************************************************/
void cpu_writeport(int Port,int Value)
{
	const struct IOWritePort *iowp = cur_writeport;


	if (iowp)
	{
		Port &= cur_portmask;
		while (iowp->start != -1)
		{
			if (Port >= iowp->start && Port <= iowp->end)
			{
				void (*handler)(int,int) = iowp->handler;


				if (handler == IOWP_NOP) return;
				else (*handler)(Port - iowp->start,Value);

				return;
			}

			iowp++;
		}
	}

}


/* set readmemory handler for bank memory  */
void cpu_setbankhandler_r(int bank,int (*handler)(int) )
{
	int offset = 0;

	if ( (((FPTR)handler)==((FPTR)MRA_RAM)) ||
	     (((FPTR)handler)==((FPTR)MRA_ROM)))
		handler = mrh_ram;
	else if (((FPTR)handler)==((FPTR)MRA_BANK1))
	{
		handler = mrh_bank1;
		offset = bankreadoffset[1];
	}
	else if (((FPTR)handler)==((FPTR)MRA_BANK2))
	{
		handler = mrh_bank2;
		offset = bankreadoffset[2];
	}
	else if (((FPTR)handler)==((FPTR)MRA_BANK3))
	{
		handler = mrh_bank3;
		offset = bankreadoffset[3];
	}
	else if (((FPTR)handler)==((FPTR)MRA_BANK4))
	{
		handler = mrh_bank4;
		offset = bankreadoffset[4];
	}
	else if (((FPTR)handler)==((FPTR)MRA_BANK5))
	{
		handler = mrh_bank5;
		offset = bankreadoffset[5];
	}
	else if (((FPTR)handler)==((FPTR)MRA_BANK6))
	{
		handler = mrh_bank6;
		offset = bankreadoffset[6];
	}
	else if (((FPTR)handler)==((FPTR)MRA_BANK7))
	{
		handler = mrh_bank7;
		offset = bankreadoffset[7];
	}
	else if (((FPTR)handler)==((FPTR)MRA_BANK8))
	{
		handler = mrh_bank8;
		offset = bankreadoffset[8];
	}
	else if (((FPTR)handler)==((FPTR)MRA_NOP))
		handler = mrh_nop;
	else
		offset = bankreadoffset[bank];
	memoryreadoffset[bank] = offset;
	memoryreadhandler[bank] = handler;
}

/* set writememory handler for bank memory  */
void cpu_setbankhandler_w(int bank,void (*handler)(int,int) )
{
	int offset = 0;

	if (((FPTR)handler)==((FPTR)MWA_RAM))
		handler = mwh_ram;
	else if (((FPTR)handler)==((FPTR)MWA_BANK1))
	{
		handler = mwh_bank1;
		offset = bankwriteoffset[1];
	}
	else if (((FPTR)handler)==((FPTR)MWA_BANK2))
	{
		handler = mwh_bank2;
		offset = bankwriteoffset[2];
	}
	else if (((FPTR)handler)==((FPTR)MWA_BANK3))
	{
		handler = mwh_bank3;
		offset = bankwriteoffset[3];
	}
	else if (((FPTR)handler)==((FPTR)MWA_BANK4))
	{
		handler = mwh_bank4;
		offset = bankwriteoffset[4];
	}
	else if (((FPTR)handler)==((FPTR)MWA_BANK5))
	{
		handler = mwh_bank5;
		offset = bankwriteoffset[5];
	}
	else if (((FPTR)handler)==((FPTR)MWA_BANK6))
	{
		handler = mwh_bank6;
		offset = bankwriteoffset[6];
	}
	else if (((FPTR)handler)==((FPTR)MWA_BANK7))
	{
		handler = mwh_bank7;
		offset = bankwriteoffset[7];
	}
	else if (((FPTR)handler)==((FPTR)MWA_BANK8))
	{
		handler = mwh_bank8;
		offset = bankwriteoffset[8];
	}
	else if (((FPTR)handler)==((FPTR)MWA_NOP))
		handler = mwh_nop;
	else if (((FPTR)handler)==((FPTR)MWA_RAMROM))
		handler = mwh_ramrom;
	else if (((FPTR)handler)==((FPTR)MWA_ROM))
		handler = mwh_rom;
	else
		offset = bankwriteoffset[bank];
	memorywriteoffset[bank] = offset;
	memorywritehandler[bank] = handler;
}

/* cpu change op-code memory base */
void cpu_setOPbaseoverride (int (*f)(int))
{
	setOPbasefunc = f;
}


/* Need to called after CPU or PC changed (JP,JR,BRA,CALL,RET) */
void cpu_setOPbase16 (int pc)
{
	MHELE hw;

	/* ASG 970206 -- allow overrides */
	if (setOPbasefunc)
	{
		pc = setOPbasefunc (pc);
		if (pc == -1)
			return;
	}

	/* 1st element link */
	hw = cur_mrhard[pc >> (ABITS2_16 + ABITS_MIN_16)];
	if (hw >= MH_HARDMAX)
	{
		/* 2nd element link */
		hw = readhardware[((hw - MH_HARDMAX) << MH_SBITS) | ((pc >> ABITS_MIN_16) & MHMASK(ABITS2_16))];
	}
	ophw = hw;

	if (!hw)
	{
	 /* memory direct */
		OP_RAM = RAM;
		OP_ROM = ROM;
		return;
	}

	if (hw <= HT_BANKMAX)
	{
		/* banked memory select */
		OP_RAM = cpu_bankbase[hw] - memoryreadoffset[hw];
		if (RAM == ROM) OP_ROM = OP_RAM;
		return;
	}

}


void cpu_setOPbase16lew (int pc)
{
	MHELE hw;

	/* ASG 970206 -- allow overrides */
	if (setOPbasefunc)
	{
		pc = setOPbasefunc (pc);
		if (pc == -1)
			return;
	}

	/* 1st element link */
	hw = cur_mrhard[pc >> (ABITS2_16LEW + ABITS_MIN_16LEW)];
	if (hw >= MH_HARDMAX)
	{
		/* 2nd element link */
		hw = readhardware[((hw - MH_HARDMAX) << MH_SBITS) | ((pc >> ABITS_MIN_16LEW) & MHMASK(ABITS2_16LEW))];
	}
	ophw = hw;

	if (!hw)
	{
	 /* memory direct */
		OP_RAM = RAM;
		OP_ROM = ROM;
		return;
	}

	if (hw <= HT_BANKMAX)
	{
		/* banked memory select */
		OP_RAM = cpu_bankbase[hw] - memoryreadoffset[hw];
		if (RAM == ROM) OP_ROM = OP_RAM;
		return;
	}

}


void cpu_setOPbase20 (int pc)
{
	MHELE hw;

	/* ASG 970206 -- allow overrides */
	if (setOPbasefunc)
	{
		pc = setOPbasefunc (pc);
		if (pc == -1)
			return;
	}

	/* 1st element link */
	hw = cur_mrhard[pc >> (ABITS2_20 + ABITS_MIN_20)];
	if (hw >= MH_HARDMAX)
	{
		/* 2nd element link */
		hw = readhardware[((hw - MH_HARDMAX) << MH_SBITS) | ((pc >> ABITS_MIN_20) & MHMASK (ABITS2_20))];
	}
	ophw = hw;

	if (!hw)
	{
		/* memory direct */
		OP_RAM = RAM;
		OP_ROM = ROM;
		return;
	}

	if (hw <= HT_BANKMAX)
	{
		/* banked memory select */
		OP_RAM = cpu_bankbase[hw] - memoryreadoffset[hw];
		if (RAM == ROM) OP_ROM = OP_RAM;
		return;
	}

}

/* Opcode execution is _always_ within the 16 bit range */
void cpu_setOPbase21 (int pc)
{
        OP_RAM = RAM;
        OP_ROM = ROM;
} 

void cpu_setOPbase24 (int pc)
{
	MHELE hw;

	/* ASG 970206 -- allow overrides */
	if (setOPbasefunc)
	{
		pc = setOPbasefunc (pc);
		if (pc == -1)
			return;
	}

	/* 1st element link */
	hw = cur_mrhard[pc >> (ABITS2_24 + ABITS_MIN_24)];
	if (hw >= MH_HARDMAX)
	{
		/* 2nd element link */
		hw = readhardware[((hw - MH_HARDMAX) << MH_SBITS) | ((pc >> ABITS_MIN_24) & MHMASK(ABITS2_24))];
	}
	ophw = hw;

	if (!hw)
	{
		/* memory direct */
		OP_RAM = RAM;
		OP_ROM = ROM;
		return;
	}

	if (hw <= HT_BANKMAX)
	{
		/* banked memory select */
		OP_RAM = cpu_bankbase[hw] - memoryreadoffset[hw];
		if (RAM == ROM) OP_ROM = OP_RAM;
		return;
	}

}


void cpu_setOPbase29 (int pc)    /* AJP 980803 */
{
	MHELE hw;

	pc = ((unsigned int) pc)>>3;

	/* ASG 970206 -- allow overrides */
	if (setOPbasefunc)
	{
		pc = setOPbasefunc (pc);
		if (pc == -1)
			return;
	}

	/* 1st element link */
	hw = cur_mrhard[((unsigned int) pc) >> (ABITS2_29 + ABITS_MIN_29)];
	if (hw >= MH_HARDMAX)
	{
		/* 2nd element link */
		hw = readhardware[((hw - MH_HARDMAX) << MH_SBITS) | ((((unsigned int) pc) >> ABITS_MIN_29) & MHMASK(ABITS2_29))];
	}
	ophw = hw;

	if (!hw)
	{
		/* memory direct */
		OP_RAM = RAM;
		OP_ROM = ROM;
		return;
	}

	if (hw <= HT_BANKMAX)
	{
		/* banked memory select */
		OP_RAM = cpu_bankbase[hw] - memoryreadoffset[hw];
		if (RAM == ROM) OP_ROM = OP_RAM;
		return;
	}

}

void install_mem_read_handler(int cpu, int start, int end, int (*handler)(int))
{
	MHELE hardware = 0;
	int abitsmin;
	int i, hw_set;
	abitsmin = ABITSMIN (cpu);

	/* see if this function is already registered */
	hw_set = 0;
	for ( i = 0 ; i < MH_HARDMAX ; i++)
	{
		/* record it if it matches */
		if (( memoryreadhandler[i] == handler ) &&
			(  memoryreadoffset[i] == start))
		{
			hardware = i;
			hw_set = 1;
		}
	}

	if ( (((FPTR)handler)==((FPTR)MRA_RAM)) ||
	     (((FPTR)handler)==((FPTR)MRA_ROM)) )
	{
			hardware = HT_RAM;	/* sprcial case ram read */
			hw_set = 1;
	}
	else if (((FPTR)handler)==((FPTR)MRA_BANK1))
	{
			hardware = HT_BANK1;
			hw_set = 1;
	}
	else if (((FPTR)handler)==((FPTR)MRA_BANK2))
	{
			hardware = HT_BANK2;
			hw_set = 1;
	}
	else if (((FPTR)handler)==((FPTR)MRA_BANK3))
	{
			hardware = HT_BANK3;
			hw_set = 1;
	}
	else if (((FPTR)handler)==((FPTR)MRA_BANK4))
	{
			hardware = HT_BANK4;
			hw_set = 1;
	}
	else if (((FPTR)handler)==((FPTR)MRA_BANK5))
	{
			hardware = HT_BANK5;
			hw_set = 1;
	}
	else if (((FPTR)handler)==((FPTR)MRA_BANK6))
	{
			hardware = HT_BANK6;
			hw_set = 1;
	}
	else if (((FPTR)handler)==((FPTR)MRA_BANK7))
	{
			hardware = HT_BANK7;
			hw_set = 1;
	}
	else if (((FPTR)handler)==((FPTR)MRA_BANK8))
	{
			hardware = HT_BANK8;
			hw_set = 1;
	}
	else if (((FPTR)handler)==((FPTR)MRA_NOP))
	{
			hardware = HT_NOP;
			hw_set = 1;
	}
	if (!hw_set)  /* no match */
	{
		/* create newer hardware handler */
		if( rdhard_max == MH_HARDMAX )
		{
			return;
		}
		else
		{
			/* register hardware function */
			hardware = rdhard_max++;
			memoryreadhandler[hardware] = handler;
			memoryreadoffset[hardware] = start;
		}
	}
	/* set hardware element table entry */
	set_element( cpu , cur_mr_element[cpu] ,
		(((unsigned int) start) >> abitsmin) ,
		(((unsigned int) end) >> abitsmin) ,
		hardware , readhardware , &rdelement_max );
}
void install_mem_write_handler(int cpu, int start, int end, void (*handler)(int, int))
{
	MHELE hardware = 0;
	int abitsmin;
	int i, hw_set;
	abitsmin = ABITSMIN (cpu);

	/* see if this function is already registered */
	hw_set = 0;
	for ( i = 0 ; i < MH_HARDMAX ; i++)
	{
		/* record it if it matches */
		if (( memorywritehandler[i] == handler ) &&
			(  memorywriteoffset[i] == start))
		{
			hardware = i;
			hw_set = 1;
		}
	}

	if (((FPTR)handler)==((FPTR)MWA_RAM))
	{
			hardware = HT_RAM;	/* sprcial case ram write */
			hw_set = 1;
	}
	else if (((FPTR)handler)==((FPTR)MWA_BANK1))
	{
			hardware = HT_BANK1;
			hw_set = 1;
	}
	else if (((FPTR)handler)==((FPTR)MWA_BANK2))
	{
			hardware = HT_BANK2;
			hw_set = 1;
	}
	else if (((FPTR)handler)==((FPTR)MWA_BANK3))
	{
			hardware = HT_BANK3;
			hw_set = 1;
	}
	else if (((FPTR)handler)==((FPTR)MWA_BANK4))
	{
			hardware = HT_BANK4;
			hw_set = 1;
	}
	else if (((FPTR)handler)==((FPTR)MWA_BANK5))
	{
			hardware = HT_BANK5;
			hw_set = 1;
	}
	else if (((FPTR)handler)==((FPTR)MWA_BANK6))
	{
			hardware = HT_BANK6;
			hw_set = 1;
	}
	else if (((FPTR)handler)==((FPTR)MWA_BANK7))
	{
			hardware = HT_BANK7;
			hw_set = 1;
	}
	else if (((FPTR)handler)==((FPTR)MWA_BANK8))
	{
			hardware = HT_BANK8;
			hw_set = 1;
	}
	else if (((FPTR)handler)==((FPTR)MWA_NOP))
	{
			hardware = HT_NOP;
			hw_set = 1;
	}
	else if (((FPTR)handler)==((FPTR)MWA_RAMROM))
	{
			hardware = HT_RAMROM;
			hw_set = 1;
	}
	else if (((FPTR)handler)==((FPTR)MWA_ROM))
	{
			hardware = HT_ROM;
			hw_set = 1;
	}
	if (!hw_set)  /* no match */
	{
		/* create newer hardware handler */
		if( wrhard_max == MH_HARDMAX )
		{
			return;
		}
		else
		{
			/* register hardware function */
			hardware = wrhard_max++;
			memorywritehandler[hardware] = handler;
			memorywriteoffset[hardware] = start;
		}
	}
	/* set hardware element table entry */
	set_element( cpu , cur_mw_element[cpu] ,
		(((unsigned int) start) >> abitsmin) ,
		(((unsigned int) end) >> abitsmin) ,
		hardware , writehardware , &wrelement_max );
}
