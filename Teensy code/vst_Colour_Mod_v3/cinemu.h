#ifndef CINES_H
#define CINES_H

#define UINT32 unsigned int
#define UINT16 unsigned short int
#define UINT8  unsigned char

#define INT32  signed int
#define INT16  signed short int
#define INT8   signed char

#define uchar unsigned char
#define uint unsigned int

struct CPUSTATE {
  uint	ram[256];		// RAM data
	uint	accVal;			// Last Acc value
	uint	cmpVal;			// Compare value
	uint	pa0Cf;			// A0 bit & C-flg
	uint	eRegPC;			// PC-reg
	uint	eRegA;			// A-reg
	uint	eRegB;			// B-reg
	uint	eRegI;			// I-reg
	uint	eRegJ;			// J-reg
	uchar	eRegP;			// P-reg
	uchar	eCState;		// current state
};

#ifdef DEBUGGER

//#define	TRACE			0
//#define	WRITEADR		0

#define	DISBRK		0xFFFF		// indicates no break point set

#define	STATE_A		0
#define	STATE_A2	1
#define	STATE_B		2
#define	STATE_B2	3

#define	MAXINSLEN	2				// maximum instruction length

extern uchar	*ProgData;
extern uint	ramBank[256];			        // RAM registers

// Instruction cycle counters

extern uint	insCount;				// instruction counter

// CPU flags and temp storage

extern uint	accVal;					// last ACC
extern uint	cmpVal;					// new Acc

// Do not change order of next two labels

extern uchar	pa0;					// Bit zero of A-reg at last ACC access
extern uchar	cFlag;					// carry flag indicator

// Graphics data

extern uint	Xstart;
extern uint	Xend;
extern uint	Ystart;
extern uint	Yend;

// By saving the following, the state of the CPU can be saved

extern uint	ioOutputs;				// output values

extern uint	eRegPC;					// PC-reg
extern uint	eRegA;					// A-reg
extern uint	eRegB;					// B-reg
extern uint	eRegI;					// I-reg
extern uint	eRegJ;					// J-reg
extern uchar	eRegP;					// P-reg
extern uchar	eCState;					// current state

extern uchar	eMiFlg;					// minus flag
extern uchar	eLtFlg;					// less than flag
extern uchar	eEqFlg;					// equal flag
extern uchar	eCyFlg;					// carry flag
extern uchar	eA0Flg;					// PA0 flag
extern uchar	eBfFlg;					// B flip/flop flag

// Debugger data

extern uint	oRegPC;					// PC-reg
extern uint	oRegA;					// A-reg
extern uint	oRegB;					// B-reg
extern uint	oRegI;					// I-reg
extern uint	oRegJ;					// J-reg
extern uchar	oRegP;					// P-reg
extern uchar	oCState;					// current state
extern uchar	oMiFlg;					// minus flag
extern uchar	oLtFlg;					// less than flag
extern uchar	oEqFlg;					// equal flag
extern uchar	oCyFlg;					// carry flag
extern uchar	oA0Flg;					// PA0 flag
extern uchar	oBfFlg;					// B flip/flop flag

extern uint	eInsBrk0;				// break address 0
extern uint	eInsBrk1;				// break address 1
extern uint	eInsBrk2;				// break address 2
extern uint	eInsBrk3;				// break address 3
extern uint	eInsGo;					// "GO" break address

#ifdef TRACE
extern uint	tracePtr;				// trace pointer index
extern uint	traceBfr[16];			        // last 16 instructions
#endif

#ifdef WRITEADR
extern uint	writeAdr;				// write address breakpoint
#endif

extern int   cineExec(uint aInsCount);
extern uint *findBrkPt(uint adr);
extern uint *getAvlBrkPt(void);
extern uint  isAnyBrkPt(void);
extern uint  decodeCmd(char *aCmdStr);
extern void  hotkeys(int &cc);
#endif

#define GAME_TAILGUNNER 1
#define GAME_RIPOFF 2
#define GAME_SPACEWARS 3
#define GAME_BOXINGBUGS 4
#define GAME_ARMORATTACK 5
#define GAME_STARCASTLE 6
#define GAME_STARHAWK 7
#define GAME_SPEEDFREAK 8
#define GAME_DEMON 9
#define GAME_SOLARQUEST 10
#define GAME_COSMICCHASM 11
#define GAME_WAROFTHEWORLDS 12
#define GAME_WARRIOR 13
#define GAME_BARRIER 14
#define GAME_SUNDANCE 15
#define GAME_QB3 16

extern UINT16 initialSwitches;
extern UINT16 initialInputs;
extern int ccpudebug;

#endif /* CINES_H */