//
// Tail Gunner
//
// Made for the GP32 console by Graham Toal
//
// Adapted for VSTCM on Teensy
// by Robin Champion - July 2022
//

// SPI register bits
/*#define LPSPI_SR_WCF    ((uint32_t)(1<<8))  // Received Word complete flag
#define LPSPI_SR_FCF    ((uint32_t)(1<<9))  // Frame complete flag
#define LPSPI_SR_TCF    ((uint32_t)(1<<10)) // Transfer complete flag
#define LPSPI_SR_MBF    ((uint32_t)(1<<24)) // Module busy flag
#define LPSPI_TCR_RXMSK ((uint32_t)(1<<19)) // Receive Data Mask (when 1 no data received to FIFO)
*/
// Teensy SS pins connected to DACs
const int SS0_IC5_RED     =  8;       // RED output
const int SS1_IC4_X_Y     =  6;       // X and Y outputs
const int SS2_IC3_GRE_BLU = 22;       // GREEN and BLUE outputs

#define SDI                 11        // MOSI on SPI0
#define SCK                 13        // SCK on SPI0
//#define BUFFERED                      // If defined, uses buffer on DACs

// Defines channel A and B of each MCP4922 dual DAC
#define DAC_CHAN_A 0
#define DAC_CHAN_B 1

#define DAC_X_CHAN 1                  // Used to flip X & Y axis if needed
#define DAC_Y_CHAN 0

typedef struct ColourIntensity {      // Stores current levels of brightness for each colour
  uint8_t red;
  uint8_t green;
  uint8_t blue;
} ColourIntensity_t;

static ColourIntensity_t LastColInt;  // Stores last colour intensity levels

static uint16_t x_pos;                 // Current position of beam
static uint16_t y_pos;

#define CARRYBIT (1<<12)
#define A0BIT 1

/* for Zonn translation :) */
#define SAR16(var,arg)    ( ( (signed short int) var ) >> arg )

/* for setting/checking the A0 flag */
#define SETA0(var)    ( RCacc_a0 = var )
#define GETA0()       ( RCacc_a0 )

/* for setting/checking the Carry flag */
#define SETFC(val)    ( RCflag_C = val )
#define GETFC()       ( ( RCflag_C >> 8 ) & 0xFF )

/* Contorted sign-extend macro only evaluates its parameter once, and
   executes in two shifts.  This is far more efficient that any other
   sign extend procedure I can think of, and relatively safe */

#define SEX(twelvebit) ((((int)twelvebit) << (int)((sizeof(int)*8)-12)) \
                        >> (int)((sizeof(int)*8)-12))

#define SW_ABORT           0x100          /* for ioSwitches */

/* Now the same information for Graham's machine */

/* Define new types for the c-cpu emulator */
typedef long unsigned int CINEWORD;      /* 12bits on the C-CPU */
typedef unsigned char      CINEBYTE;      /* 8 (or less) bits on the C-CPU */
typedef long signed int   CINESWORD;     /* 12bits on the C-CPU */
typedef signed char        CINESBYTE;     /* 8 (or less) bits on the C-CPU */
typedef unsigned long int  CINELONG;

typedef enum
{
  state_A = 0,
  state_AA,
  state_B,
  state_BB
} CINESTATE;

/* INP $8 ? */
#define IO_START   0x80

/* INP $7 ? */
#define IO_SHIELDS 0x40

#define IO_FIRE    0x20
#define IO_DOWN    0x10
#define IO_UP      0x08
#define IO_LEFT    0x04
#define IO_RIGHT   0x02
#define SW_QUARTERS_PER_GAME 0x01

#define IO_COIN   0x80

#define IO_KNOWNBITS (IO_START | IO_SHIELDS | IO_FIRE | IO_DOWN | IO_LEFT | IO_RIGHT)

/* initial value of shields (on a DIP) */
#define SW_SHIELDS80 ((1<<1) | (1<<4) | (1<<5))  /* 000 */
#define SW_SHIELDS70 ((1<<1) | (0<<4) | (1<<5))  /* 010 */
#define SW_SHIELDS60 ((1<<1) | (1<<4) | (0<<5))  /* 001 */
#define SW_SHIELDS50 ((1<<1) | (0<<4) | (0<<5))  /* 011 */
#define SW_SHIELDS40 ((0<<1) | (1<<4) | (1<<5))  /* 100 */
#define SW_SHIELDS30 ((0<<1) | (0<<4) | (1<<5))  /* 110 */
#define SW_SHIELDS20 ((0<<1) | (1<<4) | (0<<5))  /* 101 */
#define SW_SHIELDS15 ((0<<1) | (0<<4) | (0<<5))  /* 111 */
#define SW_SHIELDS15 ((0<<1) | (0<<4) | (0<<5))  /* 111 */
#define SW_SHIELDS SW_SHIELDS80

#define SW_SHIELDMASK ((1<<1) | (1<<4) | (1<<5))  /* 000 */

static uint8_t bBailOut = false;
static uint8_t ccpu_jmi_dip = 0;  /* as set by cineSetJMI */
static uint8_t ccpu_msize = 0;  /* as set by cineSetMSize */
static uint8_t ccpu_monitor = 0;  /* as set by cineSetMonitor */

uint8_t bFlipX;
uint8_t bFlipY;
uint8_t bSwapXY;
volatile static uint32_t dwElapsedTicks = 0;

uint16_t ioSwitches;
uint16_t ioInputs;
uint8_t ioOutputs = 0;    /* Where are these used??? */

int16_t JoyX;
int16_t JoyY;
uint8_t bNewFrame;

/* C-CPU context information begins --  */
CINEWORD register_PC = 0; /* C-CPU registers; program counter */
/*register */ CINEWORD register_A = 0;
/* A-Register (accumulator) */
/*register */ CINEWORD register_B = 0;
/* B-Register (accumulator) */
CINEWORD /*CINEBYTE*/ register_I = 0; /* I-Register (last access RAM location) */
CINEWORD register_J = 0;  /* J-Register (target address for JMP opcodes) */
CINEWORD /*CINEBYTE*/ register_P = 0; /* Page-Register (4 bits, shifts to high short nibble for code, hight byte nibble for ram) */
CINEWORD FromX = 0;   /* X-Register (start of a vector) */
CINEWORD FromY = 0;   /* Y-Register (start of a vector) */
CINEWORD register_T = 0;  /* T-Register (vector draw length timer) */
CINEWORD flag_C = 0;    /* C-CPU flags; carry. Is word sized, instead
           of CINEBYTE, so we can do direct assignment
           and then change to BYTE during inspection.
*/

CINEWORD cmp_old = 0;   /* last accumulator value */
CINEWORD cmp_new = 0;   /* new accumulator value */
CINEWORD /*CINEBYTE*/ acc_a0 = 0; /* bit0 of A-reg at last accumulator access */

CINESTATE state = state_A;  /* C-CPU state machine current state */
CINEWORD ram[256];    /* C-CPU ram (for all pages) */

CINEWORD vgColour = 0;
CINEBYTE vgShiftLength = 0; /* number of shifts loaded into length reg */

/* -- Context information ends. */

int ccpudebug = 0;    /* default is off */

static int startflag = IO_START;
static int coinflag = 0;
static int shieldsflag = IO_SHIELDS;  /* Whether shields are up (mouse or key) */
static int fireflag = IO_FIRE;
static int quarterflag = SW_QUARTERS_PER_GAME;  /* 1 quarter per game */

const int xmousethresh = 2;
const int ymousethresh = 2;
static int mousecode = 0;
static int sound_addr = 0;
static int sound_addr_A = 0;
static int sound_addr_B = 0;
static int sound_addr_C = 0;
static int sound_data = 0;

#define SCREEN_X 4096
#define SCREEN_Y 4096
//#define FRAMEBUFFER_SIZE (SCREEN_X * SCREEN_Y)
//#define SURFACE_SIZE (FRAMEBUFFER_SIZE / 2)
#define FRAMEBUFFER_SIZE (640 * 480)
#define SURFACE_SIZE (FRAMEBUFFER_SIZE / 2)

// This is just a guess at the correct structure of GPDRAWSURFACE
typedef struct {
  int bpp;
  int buf_w;
  int buf_h;
  int ox;
  int oy;
  int *o_buffer;
  unsigned char *ptbuffer;
} GPDRAWSURFACE;

GPDRAWSURFACE surface1, surface2, *surface;
static long gpb = 0, gpe = 0;

/*
   This file includes GP32 specific additions by Jouni 'Mr.spiv' Korhonen
*/

/* Keys */
#define rKEY_A    0x4000
#define rKEY_B    0x2000
#define rKEY_L    0x1000
#define rKEY_R    0x8000
#define rKEY_UP   0x0800
#define rKEY_DOWN 0x0200
#define rKEY_LEFT 0x0100
#define rKEY_RIGHT  0x0400
#define rKEY_START  0x0040
#define rKEY_SELECT 0x0080

/* LCD CONTROLLER */
// these all looked like 32 bit variables
// so just declared them as such, not sure if the memory map
// needs to be recreated in a structure

uint32_t rLCDCON1;
uint32_t rLCDCON2;
uint32_t rLCDCON3;
uint32_t rLCDCON4;
uint32_t rLCDCON5;
uint32_t rLCDSADDR1;
uint32_t rLCDSADDR2;
uint32_t rLCDSADDR3;
uint32_t rTPAL;

// This appears to be 16*2 bytes long
char PALETTE[32];

/* I/O PORT */
uint32_t rPBDAT;
uint32_t rPEDAT;

static unsigned char *framebuf;
static unsigned char *framebuffer_strt;
static unsigned char *framebuffer_stop;

DMAMEM static unsigned char localbuf[FRAMEBUFFER_SIZE + 4096]; // SCREEN BUFFER!

static int debounce_oneshots = 0, debounce_shields = 0, debounce_coin = -1;
