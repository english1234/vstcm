/*
   Atari Vector game simulator

   Copyright 1991, 1993, 1996 Hedley Rainnie, Doug Neubauer, and Eric Smith
   Copyright 2015 Hedley Rainnie

   6502 simulator by Hedley Rainnie, Doug Neubauer, and Eric Smith

   Adapted for vstcm by Robin Champion June 2022
   https://github.com/english1234/vstcm

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

*/

#if 0
uint16_t pc_ring[64];
uint16_t ring_idx = 0;
#else

#ifdef TRACEFIFO
struct _fifo {
  uint16_t PC;
  uint16_t SP;
  uint8_t A;
  uint8_t X;
  uint8_t Y;
  uint8_t flags;
};
struct _fifo fifo[8192];
unsigned short pcpos = 0;
#endif

#endif

const int chipSelect = BUILTIN_SDCARD;
#define IR_RECEIVE_PIN      32
// Teensy SS pins connected to DACs
const int SS0_IC5_RED     =  8;       // RED output
const int SS1_IC4_X_Y     =  6;       // X and Y outputs
const int SS2_IC3_GRE_BLU = 22;       // GREEN and BLUE outputs
#define SDI                 11        // MOSI on SPI0
#define SCK                 13        // SCK on SPI0
#define DAC_CHAN_A 0                  // Defines channel A and B of each MCP4922 dual DAC
#define DAC_CHAN_B 1
#define BUFFERED                      // If defined, uses buffer on DACs
EventResponder callbackHandler;       // DMA SPI callbackHandler
volatile int activepin;               // Active CS pin of DAC receiving data

DMAMEM char dmabuf[2] __attribute__((aligned(32)));

/* types of access */
#define RD 1
#define WR 2

/* tags */
#define MEMORY   0 /* memory, no special processing */
#define ROMWRT   1 /* ROM write, just print a message */
#define IGNWRT   2 /* spurious write that we don't care about */
#define UNKNOWN  3 /* don't know what it is */

#define COININ   4 /* coin, slam, self test, diag, VG halt, 3KHz inputs */
#define COINOUT  5 /* coin counter and invert X & Y outputs */
#define WDCLR  6
#define INTACK   7

#define VGRST    8
#define VGO      9 /* VGO */
#define DMACNT   10 /* DVG only */

#define VECRAM   11 /* MEMORY Vector RAM */
/*
   VECRAM used to be 11 so we could do some special debugging stuff on
   writes, but that is no longer necessary.
*/

#define COLORRAM 12

#define POKEY1   13 /* Pokey */
#define POKEY2   14 /* Pokey */
#define POKEY3   15 /* Pokey */
#define POKEY4   16 /* Pokey */

#define OPTSW1        17
#define OPTSW2        18
#define OPT1_2BIT     19

/* 20 and 21 no longer used */

#define EAROMCON      22
#define EAROMWR       23
#define EAROMRD       24

#define MBLO          25
#define MBHI          26
#define MBSTART       27
#define MBSTAT        28

#define GRAVITAR_IN1  29
#define GRAVITAR_IN2  30

#define SD_INPUTS     31

#define TEMP_OUTPUTS  32

#define BZ_SOUND      33
#define BZ_INPUTS     34

#define LUNAR_MEM     35
#define LUNAR_SW1     36
#define LUNAR_SW2     37
#define LUNAR_POT     38
#define LUNAR_OUT     39
#define LUNAR_SND     40
#define LUNAR_SND_RST 41

#define ASTEROIDS_SW1     42
#define ASTEROIDS_SW2     43
#define ASTEROIDS_OUT     44
#define ASTEROIDS_EXP     45
#define ASTEROIDS_THUMP   46
#define ASTEROIDS_SND     47
#define ASTEROIDS_SND_RST 48

#define AST_DEL_OUT 49

#define RB_SW           50
#define RB_SND          51
#define RB_SND_RST      52
#define RB_JOY          53

#define MEMORY1         54 // Special for ST build
#define MEMORY_BB       55 // Special for ST build (bit bucket var)

#define BREAKTAG  0x80

#define memrd(addr,PC,cyc) (MEMRD(addr,PC,cyc))
uint8_t MEMRD(uint32_t addr, int32_t PC, uint32_t cyc);
#define memrdwd(addr,PC,cyc) ((MEMRD(addr,PC,cyc) | (MEMRD((addr)+1,PC,cyc) << 8)))
#define memwr(addr,val,PC,cyc) MEMWR(addr,val,PC,cyc);

void MEMWR(uint32_t addr, int32_t val, int32_t PC, uint32_t cyc);

typedef struct {
  char *name;
  uint32_t addr;
  uint32_t len;
  uint32_t offset;
} rom_info;

typedef struct {
  uint32_t addr;
  uint32_t len;
  int32_t dir;
  int32_t tag;
} tag_info;

#define SILENCE    (0)
#define EXPLODE_LO (1 << 1)
#define EXPLODE_HI (1 << 2)
#define SHELL_LO   (1 << 3)
#define SHELL_HI   (1 << 4)
#define MOTOR_LO   (1 << 5)
#define MOTOR_HI   (1 << 6)
#define SMART      (1 << 7)

#define AUDACITY_WAV_HDR_OFF (0x2c / 2) // divide by 2 so its in samples (16bit)
#define CHECK(x) \
    if(g_smask & bit(x)) { \
        worklist[idx++] = sounds[x].ptr[sounds[x].idx]; \
	sounds[x].idx++; \
	if(sounds[x].idx == sounds[x].len) { \
	    sounds[x].idx = AUDACITY_WAV_HDR_OFF; \
            if(sounds[x].oneshot) { \
                g_smask &= ~bit(x); \
	    } \
	} \
    }

// Bzone board support

#define START1_PIN  bit(5)
#define FIRE_PIN  bit(4)
#define LT_FWD_PIN  bit(3)
#define LT_REV_PIN  bit(2)
#define RT_FWD_PIN  bit(1)
#define RT_REV_PIN  bit(0)

#define START1_LED_PIN  bit(4)

// Global data defs

#define DECLARE(x,y,z) x y=(z)
#define DECLAREBSS(x,y) x y
#define MAX_OPT_REG 3

typedef struct
{
  int32_t left;
  int32_t right;
  int32_t fire;
  int32_t thrust;
  int32_t hyper;
  int32_t shield;
  int32_t abort;
} switch_rec;

#define leftfwd left
#define leftrev thrust
#define rightfwd right
#define rightrev hyper

static long currentx;
static long currenty;

// Soc
//DECLAREBSS(RCC_ClocksTypeDef, RCC_Clocks);
DECLAREBSS(volatile uint32_t, g_soc_sixty_hz);
DECLAREBSS(volatile uint32_t, g_soc_curr_switch);
// Codec/Audio
//DECLAREBSS(DMA_Stream_TypeDef, *AUDIO_MAL_DMA_STREAM);
DECLARE(uint32_t, g_codec_volume, 0xd0);
// Audio
DECLARE(int32_t, g_aud_explosion, -1);
DECLAREBSS(uint32_t, g_aud_idx);
DECLAREBSS(uint32_t, g_aud_ping_pong);
DECLAREBSS(uint32_t, g_aud_smask);
DECLAREBSS(int16_t, g_aud_abuf[2][16]);
DECLAREBSS(uint32_t, g_aud_curr_saucer);
DECLAREBSS(uint32_t, g_aud_curr_exp);
DECLAREBSS(uint32_t, g_aud_curr_thump);
DECLAREBSS(uint32_t, g_aud_last_saucer);
DECLAREBSS(uint32_t, g_aud_saucer_fire);
DECLAREBSS(uint32_t, g_aud_enable);
// emulated cpu
//DECLARE(uint32_t, g_cpu_irq_cycle_off, 10000); // Was 6144.. caused weird NMI in an NMI problem
DECLARE(uint32_t, g_cpu_irq_cycle_off, 6144);
DECLAREBSS(uint8_t, g_cpu_save_A);
DECLAREBSS(uint8_t, g_cpu_save_X);
DECLAREBSS(uint8_t, g_cpu_save_Y);
DECLAREBSS(uint8_t, g_cpu_save_flags);
DECLAREBSS(uint16_t, g_cpu_save_PC);
DECLAREBSS(uint8_t, g_cpu_SP);
DECLAREBSS(uint8_t, g_cpu_save_totcycles);
DECLAREBSS(uint32_t, g_cpu_cyc_wraps);
DECLAREBSS(uint32_t, g_cpu_irq_cycle);
// Vector graphics
DECLARE(uint32_t, g_vctr_delay_factor, 1);
DECLARE(uint32_t, g_vctr_post_delay, 1);    // varying this might speed things up a bit? was 100
DECLAREBSS(uint32_t, g_vctr_portrait);
DECLAREBSS(uint32_t, g_vctr_vg_busy);
DECLAREBSS(uint32_t, g_vctr_vg_done_cyc); /* cycle after which VG will be done */
DECLAREBSS(uint32_t, g_vctr_vector_mem_offset);
DECLAREBSS(uint32_t, g_vctr_vg_count);

// System vars
DECLAREBSS(int32_t, g_sys_breakflag);
DECLAREBSS(int32_t, g_sys_points);
DECLAREBSS(uint8_t, g_sys_sram[0x400]);   // ZP
DECLAREBSS(uint8_t, g_sys_vram[0x1000]);  // vector ram
DECLAREBSS(int32_t, g_sys_bank);          // RAM bank select 
DECLAREBSS(int32_t, g_sys_self_test);
/* input switch counters */
DECLAREBSS(int32_t, g_sys_cslot_left);
DECLAREBSS(int32_t, g_sys_cslot_right);
DECLAREBSS(int32_t, g_sys_cslot_util);
DECLAREBSS(int32_t, g_sys_slam);
DECLAREBSS(int32_t, g_sys_start1);
DECLAREBSS(int32_t, g_sys_start2);
DECLAREBSS(switch_rec, g_sys_switches[2]);
DECLAREBSS(uint8_t, g_sys_optionreg[MAX_OPT_REG]);

typedef struct _elem {
    uint8_t cell;
    uint8_t tagr;
    uint8_t tagw;
#ifdef MAGIC_PC
    uint8_t magic;  /* flag indicating interrupt OK here */
#else
    uint8_t pad;
#endif
} elem;

elem g_sys_mem[65536];

#define  bit(x)   ((uint32_t)(1UL << x))
#define REG32(x) ((*(volatile uint32_t  *)(x)))

// game definitions & setup functions

extern int game;

/* The following are B&W with the DVG: */
#define LUNAR_LANDER 1
#define ASTEROIDS 2
#define ASTEROIDS_DX 3
/* B&W with AVG: */
#define RED_BARON 4
#define BATTLEZONE 5  /* 2901 math box */
/* All the rest are color with AVG: */
#define TEMPEST 6     /* 2901 math box */
#define SPACE_DUEL 7
#define GRAVITAR 8
#define BLACK_WIDOW 9
#define MAJOR_HAVOC 10  /* extra 6502 for sound, quad POKEY */
/* The following use two 6809s, a math box, and four POKEYs: */
#define STAR_WARS 11
#define EMPIRE 12
/* The following uses a 68000: */
#define QUANTUM 13

#define FIRST_GAME LUNAR_LANDER
#define LAST_GAME MAJOR_HAVOC /* we don't do 6809 or 68000 (yet) */

extern int use_nmi;  /* set true to generate NMI instead of IRQ */

#ifdef SPEEDUP
#define RTEXT __attribute__((section(".rtext")))
#else
#define RTEXT
#endif

// macros for 6502 simulator and binary translator

/***********************************************************************
* cycle and byte count macros
***********************************************************************/

/* instruction byte count macro, for translated code only */
#ifdef KEEP_ACCURATE_PC
#define B(val) { PC += val; }
#else
#define B(val)
#endif

/* instruction cycle count macro */
#ifdef NO_CYCLE_COUNT
#define C(val)
#else
#define C(val) { totcycles += val; }
#endif

/***********************************************************************
* flag utilities
***********************************************************************/

#ifdef CC_INDIVIDUAL_VARS

  #define DECLARE_CC \
    register int32_t C_flag; \
    register int32_t Z_flag; \
    register int32_t N_flag; \
    register int32_t V_flag; \
    register int32_t D_flag; \
    register int32_t I_flag

  /* don't use expressions as the arguments to these macros (or at least not
     expressions with side effects */

  #define setflags(val) \
    { \
      Z_flag = ((val) == 0); \
      N_flag = (((val) & 0x80) != 0); \
    }

  #define flags_to_byte  (N_flag << 7 | V_flag << 6 | D_flag << 3 | \
        I_flag << 2 | Z_flag << 1 | C_flag)

  #define byte_to_flags(b) \
    { \
      N_flag = (((b) & N_BIT) != 0); \
      V_flag = (((b) & V_BIT) != 0); \
   /* B_flag = (((b) & B_BIT) != 0); */ \
      D_flag = (((b) & D_BIT) != 0); \
      I_flag = (((b) & I_BIT) != 0); \
      Z_flag = (((b) & Z_BIT) != 0); \
      C_flag = (((b) & C_BIT) != 0); \
    }

  #define STO_N(val) { N_flag = ((val) != 0); }
  #define SET_N { N_flag = 1; }
  #define CLR_N { N_flag = 0; }
  #define TST_N (N_flag)

  #define STO_V(val) { V_flag = ((val) != 0); }
  #define SET_V { V_flag = 1; }
  #define CLR_V { V_flag = 0; }
  #define TST_V (V_flag)

  #define STO_D(val) { D_flag = ((val) != 0); }
  #define SET_D { D_flag = 1; }
  #define CLR_D { D_flag = 0; }
  #define TST_D (D_flag)

  #define STO_I(val) { I_flag = ((val) != 0); }
  #define SET_I { I_flag = 1; }
  #define CLR_I { I_flag = 0; }
  #define TST_I (I_flag)

  #define STO_Z(val) { Z_flag = ((val) != 0); }
  #define SET_Z { Z_flag = 1; }
  #define CLR_Z { Z_flag = 0; }
  #define TST_Z (Z_flag)

  #define STO_C(val) { C_flag = ((val) != 0); }
  #define SET_C { C_flag = 1; }
  #define CLR_C { C_flag = 0; }
  #define TST_C (C_flag)

#else /* ! CC_INDIVIDUAL_VARS */

  #define DECLARE_CC \
    register int32_t CC

  #define setflags(val) \
    { \
      CC &= ~ (Z_BIT | N_BIT); \
      if ((val) == 0) \
        CC |= Z_BIT; \
      if ((val) & 0x80) \
        CC |= N_BIT; \
    }

  #define flags_to_byte (CC)  /* NOTE:  CC is not an argument to the macro! */

  #define byte_to_flags(b) { CC = (b); }

  #define STO_N(val) { if (val) CC |= N_BIT; else CC &= ~ N_BIT; }
  #define SET_N { CC |= N_BIT; }
  #define CLR_N { CC &= ~ N_BIT; }
  #define TST_N ((CC & N_BIT) != 0)

  #define STO_V(val) { if (val) CC |= V_BIT; else CC &= ~ V_BIT; }
  #define SET_V { CC |= V_BIT; }
  #define CLR_V { CC &= ~ V_BIT; }
  #define TST_V ((CC & V_BIT) != 0)

  #define STO_D(val) { if (val) CC |= D_BIT; else CC &= ~ D_BIT; }
  #define SET_D { CC |= D_BIT; }
  #define CLR_D { CC &= ~ D_BIT; }
  #define TST_D ((CC & D_BIT) != 0)

  #define STO_I(val) { if (val) CC |= I_BIT; else CC &= ~ I_BIT; }
  #define SET_I { CC |= I_BIT; }
  #define CLR_I { CC &= ~ I_BIT; }
  #define TST_I ((CC & I_BIT) != 0)

  #define STO_Z(val) { if (val) CC |= Z_BIT; else CC &= ~ Z_BIT; }
  #define SET_Z { CC |= Z_BIT; }
  #define CLR_Z { CC &= ~ Z_BIT; }
  #define TST_Z ((CC & Z_BIT) != 0)

  #define STO_C(val) { if (val) CC |= C_BIT; else CC &= ~ C_BIT; }
  #define SET_C { CC |= C_BIT; }
  #define CLR_C { CC &= ~ C_BIT; }
  #define TST_C ((CC & C_BIT) != 0)

#endif

/***********************************************************************
* effective address calculation for simulated code
***********************************************************************/

#define EA_IMM   { addr = PC++; }
#define EA_ABS   { addr = memrdwd (PC,PC,totcycles);     PC += 2; }
#define EA_ABS_X { addr = memrdwd (PC,PC,totcycles) + X; PC += 2; }
#define EA_ABS_Y { addr = memrdwd (PC,PC,totcycles) + Y; PC += 2; }
#define EA_ZP    { addr = memrd (PC,PC,totcycles);       PC++; }
#define EA_ZP_X  { addr = (memrd (PC,PC,totcycles) + X) & 0xff; PC++; }
#define EA_ZP_Y  { addr = (memrd (PC,PC,totcycles) + Y) & 0xff; PC++; }

#define EA_IND_X { addr = (memrd (PC,PC,totcycles) + X) & 0xff; addr = memrdwd (addr,PC,totcycles); PC++; }
/* Note that indirect indexed will do the wrong thing if the zero page address
   plus X is $FF, because the 6502 doesn't generate a carry */

#define EA_IND_Y { addr = memrd (PC,PC,totcycles); addr = memrdwd (addr,PC,totcycles) + Y; PC++; }
/* Note that indexed indirect will do the wrong thing if the zero page address
   is $FF, because the 6502 doesn't generate a carry */

#define EA_IND   { addr = memrdwd (PC,PC,totcycles); addr = memrdwd (addr,PC,totcycles); PC += 2; }
/* Note that this doesn't handle the NMOS 6502 indirect bug, where the low
   byte of the indirect address is $FF */

/***********************************************************************
* effective address calculation for translated code
***********************************************************************/

/*
 * The translator doesn't use an EA macro for immediate mode, as there are
 * specific instruction macros for immediate mode (DO_LDAI, DO_ADCI, etc.).
 *
 * A useful optimization would be to avoid using memrd if the translator
 * knows that the operand is in RAM or ROM.
 */

#define TR_ABS(arg)   { addr = arg; }
#define TR_ABS_X(arg) { addr = arg + X; }
#define TR_ABS_Y(arg) { addr = arg + Y; }
#define TR_ZP(arg)    { addr = arg; }
#define TR_ZP_X(arg)  { addr = (arg + X) & 0xff; }
#define TR_ZP_Y(arg)  { addr = (arg + Y) & 0xff; }

#define TR_IND_X(arg) { addr = memrdwd ((arg + X) & 0xff, PC, totcycles); }
/* Note that indirect indexed will do the wrong thing if the zero page address
   plus X is $FF, because the 6502 doesn't generate a carry */
/* The translator can't trivially check for this, because it doesn't know
   what is in the X register */

#define TR_IND_Y(arg) { addr = memrdwd (arg, PC, totcycles) + Y; }
/* Note that indexed indirect will do the wrong thing if the zero page address
   is $FF, because the 6502 doesn't generate a carry */
/* The translator should check for this */

#define TR_IND(arg)   { addr = memrdwd (addr, PC, totcycles); }
/* Note that this doesn't handle the NMOS 6502 indirect bug, where the low
   byte of the indirect address is $FF */
/* The translator should check for this */

/***********************************************************************
* loads and stores
***********************************************************************/

#define DO_LDA  { A = memrd (addr, PC, totcycles); setflags (A); }
#define DO_LDX  { X = memrd (addr, PC, totcycles); setflags (X); }
#define DO_LDY  { Y = memrd (addr, PC, totcycles); setflags (Y); }

#define DO_LDAI(data) { A = data; }
#define DO_LDXI(data) { X = data; }
#define DO_LDYI(data) { Y = data; }

#define DO_STA  { memwr (addr, A, PC, totcycles); }
#define DO_STX  { memwr (addr, X, PC, totcycles); }
#define DO_STY  { memwr (addr, Y, PC, totcycles); }

/***********************************************************************
* register transfers
***********************************************************************/

#define DO_TAX { X = A; setflags (X); }
#define DO_TAY { Y = A; setflags (Y); }
#define DO_TSX { X = g_cpu_SP; setflags (X); }
#define DO_TXA { A = X; setflags (A); }
#define DO_TXS { g_cpu_SP = X; }
#define DO_TYA { A = Y; setflags (A); }

/***********************************************************************
* index arithmetic
***********************************************************************/

#define DO_DEX { X = (X - 1) & 0xff; setflags (X); }
#define DO_DEY { Y = (Y - 1) & 0xff; setflags (Y); }
#define DO_INX { X = (X + 1) & 0xff; setflags (X); }
#define DO_INY { Y = (Y + 1) & 0xff; setflags (Y); }

/***********************************************************************
* stack operations
***********************************************************************/

#define DO_PHA { dopush (A, PC); }
#define DO_PHP { dopush (flags_to_byte, PC); }
#define DO_PLA { A = dopop(PC); setflags (A); }
#define DO_PLP { uint8_t f; f = dopop(PC); byte_to_flags (f); }

/***********************************************************************
* logical instructions
***********************************************************************/

#define DO_AND  { A &= memrd (addr, PC, totcycles); setflags (A); }
#define DO_ORA  { A |= memrd (addr, PC, totcycles); setflags (A); }
#define DO_EOR  { A ^= memrd (addr, PC, totcycles); setflags (A); }

#define DO_ANDI(data) { A &= data; setflags (A); }
#define DO_ORAI(data) { A |= data; setflags (A); }
#define DO_EORI(data) { A ^= data; setflags (A); }

#define DO_BIT \
  { \
    uint8_t bval; \
    bval = memrd(addr, PC, totcycles); \
    STO_N (bval & 0x80); \
    STO_V (bval & 0x40); \
    STO_Z ((A & bval) == 0); \
  }

/***********************************************************************
* arithmetic instructions
***********************************************************************/

#define DO_ADCI(data) \
  { \
    uint16_t wtemp; \
    if(TST_D) \
      { \
  uint16_t nib1, nib2; \
  uint16_t result1, result2; \
  uint16_t result3, result4; \
  wtemp = A; \
  nib1 = data & 0xf; \
  nib2 = wtemp & 0xf; \
  result1 = nib1+nib2+TST_C; /* Add carry */ \
  if(result1 >= 10) \
    { \
      result1 = result1 - 10; \
      result2 = 1; \
    } \
  else \
    result2 = 0; \
  nib1 = (data & 0xf0) >> 4; \
  nib2 = (wtemp & 0xf0) >> 4; \
  result3 = nib1+nib2+result2; \
  if(result3 >= 10) \
    { \
      result3 = result3 - 10; \
      result4 = 1; \
    } \
  else \
    result4 = 0; \
  STO_C (result4); \
  CLR_V; \
  wtemp = (result3 << 4) | (result1); \
  A = wtemp & 0xff; \
  setflags (A); \
      } \
    else \
      { \
  wtemp = A; \
  wtemp += TST_C;    /* add carry */ \
  wtemp += data; \
  STO_C (wtemp & 0x100); \
  STO_V ((((A ^ data) & 0x80) == 0) && (((A ^ wtemp) & 0x80) != 0)); \
  A = wtemp & 0xff; \
  setflags (A); \
      } \
  }

#define DO_SBCI(data) \
  { \
    uint16_t wtemp; \
    if (TST_D) \
      { \
  int32_t nib1, nib2; \
  int32_t result1, result2; \
  int32_t result3, result4; \
  wtemp = A; \
  nib1 = data & 0xf; \
  nib2 = wtemp & 0xf; \
  result1 = nib2-nib1-!TST_C; /* Sub borrow */ \
  if(result1 < 0) \
    { \
      result1 += 10; \
      result2 = 1; \
    } \
  else \
    result2 = 0; \
  nib1 = (data & 0xf0) >> 4; \
  nib2 = (wtemp & 0xf0) >> 4; \
  result3 = nib2-nib1-result2; \
  if(result3 < 0) \
    { \
      result3 += 10; \
      result4 = 1; \
    } \
  else \
    result4 = 0; \
  STO_C (!result4); \
  CLR_V; \
  wtemp = (result3 << 4) | (result1); \
  A = wtemp & 0xff; \
  setflags (A); \
      } \
    else \
      { \
  wtemp = A; \
  wtemp += TST_C; \
  wtemp += (data ^ 0xff); \
  STO_C (wtemp & 0x100); \
  STO_V ((((A ^ data) & 0x80) == 0) && (((A ^ wtemp) & 0x80) != 0)); \
  A = wtemp & 0xff; \
  setflags (A); \
      } \
  }

#define DO_ADC { uint8_t bval; bval = memrd (addr, PC, totcycles); DO_ADCI (bval); }
#define DO_SBC { uint8_t bval; bval = memrd (addr, PC, totcycles); DO_SBCI (bval); }

#define docompare(bval,reg) \
  { \
    STO_C (reg >= bval); \
    STO_Z (reg == bval); \
    STO_N ((reg - bval) & 0x80); \
  }

#define DO_CMP { uint8_t bval; bval = memrd (addr, PC, totcycles); docompare (bval, A); }
#define DO_CPX { uint8_t bval; bval = memrd (addr, PC, totcycles); docompare (bval, X); }
#define DO_CPY { uint8_t bval; bval = memrd (addr, PC, totcycles); docompare (bval, Y); }

#define DO_CMPI(data) { docompare (data, A); }
#define DO_CPXI(data) { docompare (data, X); }
#define DO_CPYI(data) { docompare (data, Y); }

/***********************************************************************
* read/modify/write instructions (INC, DEC, shifts and rotates)
***********************************************************************/

#define DO_INC \
  { \
    uint8_t bval; \
    bval = memrd(addr, PC, totcycles) + 1; \
    setflags (bval); \
    memwr(addr,bval, PC, totcycles); \
  }

#define DO_DEC \
  { \
    uint8_t bval; \
    bval = memrd(addr, PC, totcycles) - 1; \
    setflags (bval); \
    memwr(addr,bval, PC, totcycles); \
  }

#define DO_ROR_int(val) \
  { \
    uint8_t oldC = TST_C; \
    STO_C (val & 0x01); \
    val >>= 1; \
    if (oldC) \
      val |= 0x80; \
    setflags (val); \
  }

#define DO_RORA DO_ROR_int (A)

#define DO_ROR \
  { \
    uint8_t bval; \
    bval = memrd (addr, PC, totcycles); \
    DO_ROR_int (bval); \
    memwr (addr, bval, PC, totcycles); \
  }

#define DO_ROL_int(val) \
  { \
    uint8_t oldC = TST_C; \
    STO_C (val & 0x80); \
    val = (val << 1) & 0xff; \
    val |= oldC; \
    setflags (val); \
  }

#define DO_ROLA DO_ROL_int (A)

#define DO_ROL \
  { \
    uint8_t bval; \
    bval = memrd (addr, PC, totcycles); \
    DO_ROL_int (bval); \
    memwr (addr, bval, PC, totcycles); \
  }

#define DO_ASL_int(val) \
  { \
    STO_C (val & 0x80); \
    val = (val << 1) & 0xff; \
    setflags (val); \
  }

#define DO_ASLA DO_ASL_int (A)

#define DO_ASL \
  { \
    uint8_t bval; \
    bval = memrd (addr, PC, totcycles); \
    DO_ASL_int (bval); \
    memwr (addr, bval, PC, totcycles); \
  }

#define DO_LSR_int(val) \
  { \
    STO_C (val & 0x01); \
    val >>= 1; \
    setflags (val); \
  }

#define DO_LSRA DO_LSR_int (A)

#define DO_LSR \
  { \
    uint8_t bval; \
    bval = memrd (addr, PC, totcycles); \
    DO_LSR_int (bval); \
    memwr (addr, bval, PC, totcycles); \
  }

/***********************************************************************
* flag manipulation
***********************************************************************/

#define DO_CLC { CLR_C; }
#define DO_CLD { CLR_D; }
#define DO_CLI { CLR_I; }
#define DO_CLV { CLR_V; }

#define DO_SEC { SET_C; }
#define DO_SED { SET_D; }
#define DO_SEI { SET_I; }

/***********************************************************************
* instruction flow:  branches, jumps, calls, returns, SWI
***********************************************************************/

#define DO_JMP { PC = addr; }
#define TR_JMP { PC = addr; continue; }

#define DO_JSR \
  { \
    PC--; \
    dopush(PC >> 8, PC); \
    dopush(PC & 0xff, PC); \
    PC = addr; \
  }

/*
 * Note that the argument to TR_JSR is the address of the JSR instruction,
 * _not_ the address of the target.  The address of the target has to be
 * set up beforehand by the TR_EA_ABS macro or the like.
 */
#define TR_JSR(arg) \
  { \
    dopush ((arg + 2) >> 8, PC); \
    dopush ((arg + 2) & 0xff, PC); \
    PC = addr; \
    continue; \
  }

#define DO_RTI \
  { \
    uint8_t f; \
    f = dopop(PC); \
    byte_to_flags (f); \
    PC = dopop(PC); /* & 0xff */ \
    PC |= dopop(PC) << 8; \
  }

#define TR_RTI \
  { \
    uint8_t f; \
    f = dopop(PC); \
    byte_to_flags (f); \
    PC = dopop(PC); /* & 0xff */ \
    PC |= dopop(PC) << 8; \
    continue; \
  }

#define DO_RTS \
  { \
    PC = dopop(PC); /* & 0xff */ \
    PC |= dopop(PC) << 8; \
    PC++; \
  }

#define TR_RTS \
  { \
    PC = dopop(PC); /* & 0xff */ \
    PC |= dopop(PC) << 8; \
    PC++; \
    continue; \
  }

#define DO_BRK \
  { \
      for(;;);  \
  }
#if 0
#define TR_BRK(arg) \
  { \
    ; \
  }
#endif

#define dobranch(bit,sign) \
  { \
    int32_t offset; \
    if (bit == sign) \
      { \
  offset = memrd (PC, PC, totcycles); \
  if (offset & 0x80) \
          offset |= 0xff00; \
  PC = (PC + 1 + offset) & 0xffff; \
      } \
    else \
      PC++; \
  }

#define trbranch(bit,sign,dest) \
  { \
    if (bit == sign) \
      { \
        PC = dest; \
        continue; \
      } \
  }

#define DO_BCC { dobranch (TST_C, 0); }
#define DO_BCS { dobranch (TST_C, 1); }
#define DO_BEQ { dobranch (TST_Z, 1); }
#define DO_BMI { dobranch (TST_N, 1); }
#define DO_BNE { dobranch (TST_Z, 0); }
#define DO_BPL { dobranch (TST_N, 0); }
#define DO_BVC { dobranch (TST_V, 0); }
#define DO_BVS { dobranch (TST_V, 1); }

//#define CHECK_PC { if((PC == 0x7B71) || (PC == 0x7B81)) xyzzy(); }
#define TR_BCC(addr) { trbranch (TST_C, 0, addr); }
#define TR_BCS(addr) { trbranch (TST_C, 1, addr); }
#define TR_BEQ(addr) { trbranch (TST_Z, 1, addr); }
#define TR_BMI(addr) { trbranch (TST_N, 1, addr); }
#define TR_BNE(addr) { trbranch (TST_Z, 0, addr); }
#define TR_BPL(addr) { trbranch (TST_N, 0, addr); }
#define TR_BVC(addr) { trbranch (TST_V, 0, addr); }
#define TR_BVS(addr) { trbranch (TST_V, 1, addr); }

/***********************************************************************
* misc.
***********************************************************************/

#define DO_NOP

// math box simulation (Battlezone/Red Baron/Tempest)

#define REG0 mb_reg[0x00]
#define REG1 mb_reg[0x01]
#define REG2 mb_reg[0x02]
#define REG3 mb_reg[0x03]
#define REG4 mb_reg[0x04]
#define REG5 mb_reg[0x05]
#define REG6 mb_reg[0x06]
#define REG7 mb_reg[0x07]
#define REG8 mb_reg[0x08]
#define REG9 mb_reg[0x09]
#define REGa mb_reg[0x0a]
#define REGb mb_reg[0x0b]
#define REGc mb_reg[0x0c]
#define REGd mb_reg[0x0d]
#define REGe mb_reg[0x0e]
#define REGf mb_reg[0x0f]

//void mb_go (int addr, uint8_t data);

extern int16_t mb_result;

// POKEY chip simulation functions

#define MAX_POKEY 4  // get rid of compiler warning for subscript above array bounds
//#define MAX_POKEY 1  /* POKEYs are numbered 0 .. MAX_POKEY - 1 */

uint8_t pokey_read (int pokeynum, int reg, int PC, unsigned long cyc);
void pokey_write (int pokeynum, int reg, uint8_t val, int PC, unsigned long cyc);

#define MAX_REG 16

/* read registers */
#define POT0 0x0
#define POT1 0x1
#define POT2 0x2
#define POT3 0x3
#define POT4 0x4
#define POT5 0x5
#define POT6 0x6
#define POT7 0x7
#define ALLPOT 0x8
#define KBCODE 0x9
#define RANDOM 0xa
#define IRQSTAT 0xe
#define SKSTAT 0xf

/* write registers */
#define AUDF1 0x0
#define AUDC1 0x1
#define AUDF2 0x2
#define AUDC2 0x3
#define AUDF3 0x4
#define AUDC3 0x5
#define AUDF4 0x6
#define AUDC4 0x7
#define AUDCTL 0x8
#define STIMER 0x9
#define SKRES 0xa
#define POTGO 0xb
#define SEROUT 0xd
#define IRQEN 0xe
#define SKCTL 0xf

char *pokey_rreg_name[] =
{
  "POT0", "POT1", "POT2", "POT3",
  "POT4", "POT5", "POT6", "POT7",
  "ALLPOT", "KBCODE", "RANDOM", "unused0xB",
  "unused0xC", "unused0xD", "IRQSTAT", "SKSTAT"
};

char *pokey_wreg_name[] =
{
  "AUDF1", "AUDC1", "AUDF2", "AUDC2",
  "AUDF3", "AUDC3", "AUDF4", "AUDC4",
  "AUDCTL", "STIMER", "SKRES", "POTGO",
  "unused0xC", "SEROUT", "IRQEN", "SKCTL"
};

uint8_t pokey_rreg [MAX_POKEY][MAX_REG];
uint8_t pokey_wreg [MAX_POKEY][MAX_REG];

// DVG & AVG

#define MAXSTACK 4

#define VCTR 0
#define HALT 1
#define SVEC 2
#define STAT 3
#define CNTR 4
#define JSRL 5
#define RTSL 6
#define JMPL 7
#define SCAL 8

#define DVCTR 0x01
#define DLABS 0x0a
#define DHALT 0x0b
#define DJSRL 0x0c
#define DRTSL 0x0d
#define DJMPL 0x0e
#define DSVEC 0x0f

#define twos_comp_val(num, bits) ((num&(1<<(bits-1)))?(num|~((1<<bits)-1)):(num&((1<<bits)-1)))

#define map_addr(n) (((n)<<1)+g_vctr_vector_mem_offset)

#define max(x,y) (((x)>(y))?(x):(y))

// 6502 simulator for Atari Vector game simulator

//#define WRAP_CYC_COUNT 1000000000

#define N_BIT 0x80
#define V_BIT 0x40
#define B_BIT 0x10
#define D_BIT 0x08
#define I_BIT 0x04
#define Z_BIT 0x02
#define C_BIT 0x01

rom_info battlezone_roms [] =
{
  { "roms/Battlezone/036414a.01", 0x5000, 0x0800, 0 },
  { "roms/Battlezone/036413.01", 0x5800, 0x0800, 0 },
  { "roms/Battlezone/036412.01", 0x6000, 0x0800, 0 },
  { "roms/Battlezone/036411.01", 0x6800, 0x0800, 0 },
  { "roms/Battlezone/036410.01", 0x7000, 0x0800, 0 },
  { "roms/Battlezone/036409.01", 0x7800, 0x0800, 0 },

  { "roms/Battlezone/036422.01", 0x3000, 0x0800, 0 },
  { "roms/Battlezone/036421.01", 0x3800, 0x0800, 0 },

  { NULL,   0,      0,           0 }
};

tag_info battlezone_tags [] =
{
  { 0x0000, 0x0400, RD | WR, MEMORY },  /* RAM */

  { 0x0800,      1, RD,      COININ },
  { 0x0a00,      1, RD,      OPTSW1 },
  { 0x0c00,      1, RD,      OPTSW2 },
  { 0x1000,      1,      WR, COINOUT },
  { 0x1200,      1,      WR, VGO },
  { 0x1400,      1,      WR, WDCLR },
  { 0x1600,      1,      WR, VGRST },

  { 0x1800,      1, RD,      MBSTAT },
  { 0x1810,      1, RD,      MBLO },
  { 0x1818,      1, RD,      MBHI },

  { 0x1820,   0x10, RD | WR, POKEY1 },
  { 0x1828,      1, RD,      BZ_INPUTS },
  { 0x1840,      1,      WR, BZ_SOUND },

  { 0x1860,   0x20,      WR, MBSTART },

  { 0x2000, 0x1000, RD | WR, VECRAM },

  { 0,           0, 0,       0 }
};
