/*
 * vecsim.c: Atari Vector game simulator
 *
 * Copyright 1991, 1992, 1993, 1996, 2003 Hedley Rainnie and Eric Smith
 *
 *    This program is free software; you can redistribute it and/or modify
 *    it under the terms of the GNU General Public License as published by
 *    the Free Software Foundation; either version 2 of the License, or
 *    (at your option) any later version.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU General Public License for more details.
 *
 *    You should have received a copy of the GNU General Public License
 *    along with this program; if not, write to the Free Software
 *    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

#include "main.h"  // Needs to be first as contains define VSTCM

#ifdef VSTCM

#include <SD.h>
#include <Bounce2.h>
//#include <IRremote.hpp>
#include <Wire.h>
#include <SerialFlash.h>
#include "buttons.h"

extern char gMsg[50];  // Optional additional information to show on menu

#else

#pragma warning(disable : 4996)  // Get rid of annoying compiler warnings in VC++
#include <windows.h>             // for using CaptureStackBackTrace
#include <stdlib.h>
#include <stdio.h>
#include <cstring>
#include <ctype.h>
#include SDL_PATH

#endif

#include "vecsim.h"
#include "drawing.h"

#ifdef VSTCM

// Bounce objects to read five pushbuttons (pins 0-4)
extern Bounce button0;
extern Bounce button1;
extern Bounce button2;
extern Bounce button3;
extern Bounce button4;

#else
extern SDL_Renderer *rend_2D_orig;  // Renderer for original 2D game
#endif

#define MAX_ARGS 16

typedef struct _elem {
  uint8_t cell;
  uint8_t tagr;
  uint8_t tagw;
  //#ifdef MAGIC_PC
  //  uint8_t magic;  // flag indicating interrupt OK here
  //#else
  uint8_t pad;
  //#endif
} elem;

elem *mem;

int32_t breakflag = 0;
int32_t bank = 0; /* RAM bank select */
int32_t self_test = 0;

/* input switch counters */
int32_t cslot_left = 0;
int32_t cslot_right = 0;
int32_t cslot_util = 0;

int32_t slam = 0;
int32_t start1 = 0;
int32_t start2 = 0;

switch_rec switches[2] = { { 0, 0, 0, 0, 0, 0, 0 }, { 0, 0, 0, 0, 0, 0, 0 } };

joystick_rec joystick = { 0x80, 0x80 };

int32_t dvg = 0;
int32_t portrait = 0;

#ifdef VG_DEBUG
int32_t trace_vgo = 0;
int32_t vg_step = 0; /* single step the vector generator */
int32_t vg_print = 0;
uint32_t last_vgo_cyc = 0;
uint32_t vgo_count = 0;
#endif /* VG_DEBUG */

int32_t vg_busy = 0;
uint32_t vg_done_cyc; /* cycle after which VG will be done */
uint32_t vector_mem_offset;

#define MAXSTACK 8 /* Tempest needs more than 4     BW 210797 */

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

#define twos_comp_val(num, bits) ((num & (1 << (bits - 1))) ? (num | ~((1 << bits) - 1)) : (num & ((1 << bits) - 1)))

const char *avg_mnem[] = { "vctr", "halt", "svec", "stat", "cntr", "jsrl", "rtsl",
                           "jmpl", "scal" };

const char *dvg_mnem[] = { "????", "vct1", "vct2", "vct3",
                           "vct4", "vct5", "vct6", "vct7",
                           "vct8", "vct9", "labs", "halt",
                           "jsrl", "rtsl", "jmpl", "svec" };

#define map_addr(n) (((n) << 1) + vector_mem_offset)

struct RGBColor {
  unsigned short red;    // Red component (0-65535)
  unsigned short green;  // Green component (0-65535)
  unsigned short blue;   // Blue component (0-65535)
};
typedef struct RGBColor RGBColor;

RGBColor gColorValue[8][16];

RGBColor black = { 0, 0, 0 };
RGBColor white = { 0xFFFF, 0xFFFF, 0xFFFF };
RGBColor green = { 0, 0xFFFF, 0 };

extern bool should_quit;
extern bool has_focus;

static int iBUTT1 = 0;
static int iBUTT2 = 0;
static int iBUTT3 = 0;
static int iBUTT4 = 0;
static int iBUTT5 = 0;
static int iBUTT6 = 0;

static int iSW1 = 0;
static int iSW2 = 0;
static int iSW3 = 0;
static int iSW4 = 0;
static int iSW5 = 0;

uint8_t optionreg[MAX_OPT_REG] = { 0xff, 0xff, 0xff };

/* math box scratch registers */
uint16_t mb_reg[16];  // at one point a value of 0xFFFF is assigned to REGF, so it should be unsigned 16 bits to avoid overflow

/* math box result */
int16_t mb_result = 0;

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

int32_t use_nmi;
int32_t game = 0;

// Open log file for PC
#ifndef VSTCM
FILE *trace_file;
#endif

const uint32_t HSIZE = (1024 * 8192);
const uint32_t VSIZE = (1024 * 8192);

#define SAMPLE_RATE 44100    // Standard sample rate
#define BUFFER_SIZE 256      // Number of samples in the buffer
#define FIRE_FREQ 1000       // 1 kHz ship fire sound
#define FIRE_DURATION_MS 50  // 50 ms duration
#define FIRE_SAMPLES ((SAMPLE_RATE * FIRE_DURATION_MS) / 1000)

volatile int16_t soundBuffer[BUFFER_SIZE];  // Audio sample buffer
volatile uint16_t bufferIndex = 0;          // Playback index
volatile uint8_t isPlaying = 0;             // Flag to track playback

// Thump variables
int thumpSpeed = 1000;   // Initial speed in ms
int asteroidCount = 10;  // Dummy asteroid count


// EAROM functions
/*void init_earom();
void earom_set_control(uint8_t cs1, uint8_t cs2, uint8_t c1, uint8_t c2);
void earom_set_clk(uint8_t state);
void earom_write(uint8_t offset, uint8_t data);
uint8_t earom_read();
void earom_update_state();
*/
// Define global offset variables
int game_screen_offset_x = 0;
int game_screen_offset_y = 0;

// Helper function to set offsets based on game
void set_game_screen_offset(int game) {
    switch (game) {
        case SPACE_DUEL:
        case GRAVITAR:
        case BLACK_WIDOW:
            game_screen_offset_x = -2048;
            game_screen_offset_y = -3072;
            break;
        case ASTEROIDS:
        case ASTEROIDS_DX:
        default:
            game_screen_offset_x = 0;
            game_screen_offset_y = 0;
            break;
    }
}

/*
 * This used to decrement the switch variable if it was non-zero, so that
 * they would automatically release.  This has been changed to increment
 * it if less than zero, so switches set by the debugger will release, but
 * to leave it alone if it is greater than zero, for keyboard handling.
 */
int check_switch_decr(int32_t *sw) {
  if ((*sw) < 0) {
    (*sw)++;
    if ((*sw) == 0)
      printf("switch released\n");
  }
  return ((*sw) != 0);
}

#define OP0 (m_op & 1)
#define OP1 (m_op & 2)
#define OP2 (m_op & 4)
#define OP3 (m_op & 8)

#define ST3 (m_state_latch & 8)


#define u8 unsigned char
#define u16 unsigned short int
#define s32 signed int

u16 m_pc;
u8 m_sp;
u16 m_dvx;
u16 m_dvy;
u8 m_dvy12;
u16 m_timer;
u16 m_stack[4];
u16 m_data;

u8 m_state_latch;
u8 m_int_latch;
u8 m_scale;
u8 m_bin_scale;
u8 m_intensity;
u8 m_color;
u8 m_enspkl;
u8 m_spkl_shift;
u8 m_map;

u16 m_hst;
u16 m_lst;
u16 m_izblank;

u8 m_op;
u8 m_halt;
u8 m_sync_halt;

u16 m_xdac_xor;
u16 m_ydac_xor;

s32 m_xpos;
s32 m_ypos;

s32 m_clipx_min;
s32 m_clipy_min;
s32 m_clipx_max;
s32 m_clipy_max;

int m_xmin, m_xmax, m_ymin, m_ymax;
int m_xcenter, m_ycenter;

u16 m_vectorram_offset;
u16 m_colorram_offset;
u8 *m_prom;
u8 avg_prom[256];

int avg_done(unsigned long cyc) {

  return m_halt ? 1 : 0;
  //    return m_sync_halt ? 1 : 0;
}

int vg_done(unsigned long cyc) {
  if (game == TEMPEST)
    return avg_done(cyc);

  if (vg_busy && (cyc > vg_done_cyc))
    vg_busy = 0;

  return (!vg_busy);
}


// EAROM state variables
static uint8_t m_earom_data[64];  // 64 bytes of EAROM data
static uint8_t m_earom_control = 0;
static uint8_t m_earom_address = 0;
static uint8_t m_earom_output = 0;

// Initialize EAROM
void init_earom() {

    // previous version
    //    for (int ii = 0; ii < 64; ii++)
      //      m_rom_data[ii] = 0xff;


    memset(m_earom_data, 0xFF, sizeof(m_earom_data));  // Start with all FF
    m_earom_control = 0;
    m_earom_address = 0;
    m_earom_output = 0;
}

// Set EAROM control lines
void earom_set_control(uint8_t cs1, uint8_t cs2, uint8_t c1, uint8_t c2) {
    m_earom_control = (cs1 ? 0x08 : 0) |
        (cs2 ? 0x10 : 0) |
        (c1 ? 0x02 : 0) |
        (c2 ? 0x04 : 0);

    // create a new composite control state
	// this is a previous version of the code that was commented out
  /*  unsigned char oldstate = m_control_state;
    m_control_state = oldstate & CK;
    m_control_state |= (c1 != 0) ? C1 : 0;
    m_control_state |= (c2 != 0) ? C2 : 0;
    m_control_state |= (cs1 != 0) ? CS1 : 0;
    m_control_state |= (cs2 != 0) ? CS2 : 0;

    // if not selected, or if change from previous, we're done
    if ((m_control_state & (CS1 | CS2)) != (CS1 | CS2) || m_control_state == oldstate)
        return;

    update_state();*/
}

// Set EAROM clock
void earom_set_clk(uint8_t state) {
    m_earom_control = (m_earom_control & ~0x01) | (state ? 0x01 : 0);


    /*    unsigned char oldstate = m_control_state;
        if (state)
            m_control_state |= CK;
        else
            m_control_state &= ~CK;

        // updates occur on falling edge when chip is selected
        if ((m_control_state & (CS1 | CS2)) == (CS1 | CS2) && (m_control_state != oldstate) && !state) {
            // read mode (C2 is "Don't Care")
            if ((m_control_state & C1) == C1) {
                m_data2 = m_rom_data[m_address];
                //            LOG("Read %02X = %02X\n", m_address, m_data);
            }

            update_state();
        }
*/
    // Note: The above commented code is not needed in the new implementation
	// as we handle the clock and control state directly in earom_update_state()
}

// Write to EAROM
void earom_write(uint8_t offset, uint8_t data) {


    // This was a previous version of the code that was commented out
     //   m_address = offset & 0x3f;
      //  m_data2 = data;



    m_earom_address = offset & 0x3F;  // Only 6-bit address
    m_earom_output = data;
}

// Read from EAROM
uint8_t earom_read() {


	// This was a previous version of the code that was commented out
      //  return m_data2;
   

    // Only read if chip is selected and in read mode
    if ((m_earom_control & 0x18) == 0x18 &&  // CS1 and CS2 both high
        (m_earom_control & 0x02)) {          // C1 high (read mode)
        return m_earom_data[m_earom_address];
    }
    return 0xFF;  // Default value when not readable
}

// Update EAROM state
void earom_update_state() {
    // Only process if chip is selected
    if ((m_earom_control & 0x18) != 0x18) {
        return;
    }

    // Check for falling clock edge
    static uint8_t last_clk = 0;
    uint8_t current_clk = m_earom_control & 0x01;

    if (last_clk && !current_clk) {  // Falling edge
        // Read mode
        if (m_earom_control & 0x02) {
            m_earom_output = m_earom_data[m_earom_address];
        }
        // Write mode
        else if (!(m_earom_control & 0x04)) {  // C2 low
            m_earom_data[m_earom_address] &= m_earom_output;
        }
        // Erase mode
        else if (m_earom_control & 0x04) {  // C2 high
            m_earom_data[m_earom_address] = 0xFF;
        }
    }
    last_clk = current_clk;
}

uint8_t MEMRD(uint16_t addr, uint16_t PC, uint32_t cyc) {
  // byte MEMRD( unsigned addr, int PC, unsigned long cyc ) {
  // register uint8_t tag, result = 0;

  uint8_t tag, result = 0;
  /*
   if (addr == 0x2401) {
#ifdef VSTCM
      Serial.printf( "MEM READ: PC=%04X Addr=2401 Value=%02X tagr=%02X CYC=%d\n",
                    PC, mem[addr].cell, mem[addr].tagr, cyc );
      Serial.flush();
#else
      fprintf( trace_file, "MEM READ: PC=%04X Addr=2401 Value=%02X tagr=%02X CYC=%d\n",
              PC, mem[addr].cell, mem[addr].tagr, cyc );
      fflush( trace_file );
#endif
   }
*/

  if (!(tag = mem[addr].tagr))
    return (mem[addr].cell);

  if (tag & BREAKTAG) {
    breakflag = 1;
  }

  switch (tag & 0x3f) {
    case MEMORY:
      result = mem[addr].cell;
      break;
    case TEMPEST_PROTECTTION_0:
      // always read 0, than nothing bad can happen :-)
      result = 0;
      break;
    case COLORRAM:
      result = mem[addr].cell;
      break;
    case COININ:
        if (game == TEMPEST) {
            result = (iSW2 << 7) |  // Coin 1
                (iSW3 << 6) |  // Coin 2
                (iSW4 << 5) |  // Service
                (vg_done(cyc) << 3) |
                ((cyc >> 2) & 0x80);  // 3kHz clock
        } else
      result =
        ((!check_switch_decr(&cslot_right))) | ((!check_switch_decr(&cslot_left)) << 1) | ((!check_switch_decr(&cslot_util)) << 2) | ((!check_switch_decr(&slam)) << 3) | ((!self_test) << 4) | (1 << 5) | /* signature analysis */
        (vg_done(cyc) << 6) |
        /* clock toggles at 3 KHz */
        ((cyc >> 1) & 0x80);
      //result = iSW2;  // Using player 2 start button as coin up TEMPORARILY COMMENTED OUT WHILE TESTING TEMPEST
      break;
    case EAROMRD:
        result = earom_read();
      //result = 0;
      break;
    case OPTSW1:
      result = optionreg[0];
      break;
    case OPTSW2:
      result = optionreg[1];
      break;
    case OPT1_2BIT:
      result = 0xfc | ((optionreg[0] >> (2 * (3 - (addr & 0x3)))) & 0x3);
      break;
    case POKEY1:
      result = pokey_read(0, addr & 0x0f, PC, cyc);
      break;
    case POKEY2:
      result = pokey_read(1, addr & 0x0f, PC, cyc);
      break;
    case POKEY3:
      result = pokey_read(2, addr & 0x0f, PC, cyc);
      break;
    case POKEY4:
      result = pokey_read(3, addr & 0x0f, PC, cyc);
      break;
    case MBLO:
      result = mb_result & 0xff;
      break;
    case MBHI:
      result = (mb_result >> 8) & 0xff;
      break;
    case MBSTAT:
      result = 0x00; /* always done! */
      break;
    case GRAVITAR_IN1:
    //  result =
     //   ((optionreg[2] & 0x07) << 5) | ((!switches[0].thrust) << 4) | ((!switches[0].left) << 3) | ((!switches[0].right) << 2) | ((!switches[0].fire) << 1) | ((!switches[0].shield));
      
        result = ((optionreg[2] & 0x07) << 5) |
            (iBUTT3 << 4) |  // Thrust
            (iBUTT5 << 3) |  // Left
            (iBUTT4 << 2) |  // Right
            (iBUTT1 << 1) |  // Fire
            (iBUTT2);        // Shield
        break;
    case GRAVITAR_IN2:
      result =
        0x80 | /* upright cabinet */
        ((!check_switch_decr(&start2)) << 6) | ((!check_switch_decr(&start1)) << 5) | ((!switches[1].thrust) << 4) | ((!switches[1].left) << 3) | ((!switches[1].right) << 2) | ((!switches[1].fire) << 1) | ((!switches[1].shield));
      break;
    case SD_INPUTS:
      switch (addr & 0x07) {
        case 0:
          return ((switches[0].shield << 7) | (switches[0].fire << 6));
        case 1:
          return ((switches[1].shield << 7) | (switches[1].fire << 6));
        case 2:
          return ((switches[0].left << 7) | (switches[0].right << 6));
        case 3:
          return ((switches[1].left << 7) | (switches[1].right << 6));
        case 4:
          return ((switches[0].thrust << 7) | (check_switch_decr(&start1) << 6));
        case 5:
          return ((switches[1].thrust << 7) | ((optionreg[2] << 6) & 0x40));
        case 6:
          return (((check_switch_decr(&start2)) << 7) | ((optionreg[2] << 5) & 0x40));
        case 7:
          return (0x00 /* upright */ | ((optionreg[2] << 4) & 0x40));
      }
      result = 0x00;
      break;
    case BZ_INPUTS:
      result =
        (check_switch_decr(&start1) << 5) | (switches[0].fire << 4) | (switches[0].leftfwd << 3) | (switches[0].leftrev << 2) | (switches[0].rightfwd << 1) | (switches[0].rightrev << 0);
      break;
    case LUNAR_MEM:
      result = mem[addr & 0xff].cell;
      break;
    case LUNAR_SW1:
#ifdef ORIGINAL_VERSION
       result = 0x80 | /* DIAG STEP */
          ((cyc >> 2) & 0x40) | /* 3 KHz */
          ((!check_switch_decr( &slam )) << 2) |
          ((!self_test) << 1) |
          vg_done( cyc ) + 0x20 + 0x10 + 0x08;   /* gcc warns here about precedence of '|' and '+' and suggests adding brackets ...*/
#else
       result = 0x80 | /* DIAG STEP */
          ((cyc >> 2) & 0x40) | /* 3 KHz */
          ((!check_switch_decr( &slam )) << 2) |
          ((!self_test) << 1) |
          vg_done( cyc )
          | 0x20 | 0x10 | 0x08;
#endif
    case LUNAR_SW2:
      switch (addr & 0x07) {
        case 0:
          result = check_switch_decr(&start1) << 7;
          break;
        case 1:
          result = (!check_switch_decr(&cslot_left)) << 7;
          break;
        case 2:
          result = (check_switch_decr(&cslot_util)) << 7;
          break;
        case 3:
          result = (!check_switch_decr(&cslot_right)) << 7;
          break;
        case 4: /* game select */
          result = (!check_switch_decr(&start2)) << 7;
          break;
        case 5:
          result = switches[0].abort << 7;
          break;
        case 6:
          result = switches[0].right << 7;
          break;
        case 7:
          result = switches[0].left << 7;
          break;
      }
      break;
    case LUNAR_POT:
      result = 255 - joystick.y;
      break;
    case ASTEROIDS_SW1:
      switch (addr & 0x07) {
        case 0: /* nothing */
          result = 0;
          break;
        case 1: /* 3 KHz */
                /* clock toggles at 3 KHz */
          result = ((cyc >> 2) & 0x80);
          break;
        case 2: /* vector generator halt */
          result = (!vg_done(cyc)) << 7;
          break;
        case 3: /* hyperspace */
          if (game == ASTEROIDS)
            result = iBUTT6;
          // result = switches[0].hyper << 7;
          else
            result = switches[0].shield << 7;
          break;
        case 4: /* fire */
                //result = switches[0].fire << 7;
          result = iBUTT1;
          break;
        case 5: /* diag step */
          result = 0;
          break;
        case 6: /* slam */
          result = check_switch_decr(&slam) << 7;
          break;
        case 7: /* self test */
          result = self_test << 7;
          break;
      }
      break;
    case ASTEROIDS_SW2:
      switch (addr & 0x07) {
        case 0:
          result = iSW3; /* left coin */
                         //   result = !(check_switch_decr( &cslot_left ) << 7);  // this is ambiguous for the compiler, so check it works
          break;
        case 1:           /* center coin */
          result = iSW2;  // Temporarily using player 2 start button as coin up
                          //   result = !(check_switch_decr(&cslot_util) << 7);
          break;
        case 2: /* right coin */
                //   result = !(check_switch_decr( &cslot_right ) << 7);  // this is ambiguous for the compiler, so check it works
          break;
        case 3: /* 1P start */
          result = iSW5;
          //  result = check_switch_decr(&start1) << 7;
          break;
        case 4: /* 2P start */
                //     result = check_switch_decr( &start2 ) << 7;
          break;
        case 5: /* thrust */
          result = iSW1;
          //   result = switches[0].thrust << 7;
          break;
        case 6: /* rot right */
          result = iBUTT4;
          //result = switches[0].right << 7;
          break;
        case 7: /* rot left */
          result = iBUTT5;
          //result = switches[0].left << 7;
          break;
      }
      break;
    case RB_SW:
      result =
        ((switches[0].fire) << 7) | ((!check_switch_decr(&start1)) << 6) | 0x3f;
      break;
    case RB_JOY:
      if (mem[0x1808].cell & 0x01) /* POTSEL signal in RB_SND output register */
        result = joystick.x;
      else
        result = joystick.y;
      break;
    case UNKNOWN:
      /* Battlezone has a bogus cmp (00,x) instruction at 6b79 that causes
        lots of stray reads */
      if ((game != BATTLEZONE) || (PC != 0x6b7b)) {
        breakflag = 1;
        printf("@%04x Unknown rd addr %04x data %02x tag %02x\n",
               PC, addr, mem[addr].cell, mem[addr].tagr);
      }
      result = 0xff;
      break;
    default:
      breakflag = 1;
      printf("@%04x Why are we here rd addr %04x data %02x tag %02x\n",
             PC, addr, mem[addr].cell, mem[addr].tagr);
      result = 0xff;
      break;
  }

  if (tag & BREAKTAG) {
    printf("@%04x Breakpoint read %04x, data %02x\n", PC, addr, result);
  }

  return (result);
}

uint8_t memrd_debug(uint16_t addr, uint16_t PC, uint32_t cycles) {
#ifdef VSTCM
  Serial.printf("memrd_debug before if: PC=%04X Addr=2401 Value=%02X tagr=%02X CYC=%d\n", PC, mem[addr].cell, mem[addr].tagr, cycles);
#else
  fprintf(trace_file, "memrd_debug before if: PC=%04X Addr=2401 Value=%02X tagr=%02X CYC=%d\n", PC, mem[addr].cell, mem[addr].tagr, cycles);
#endif

  uint8_t value = mem[addr].tagr ? MEMRD(addr, PC, cycles) : mem[addr].cell;

#ifdef VSTCM
  Serial.printf("memrd_debug after if: PC=%04X Addr=2401 Value=%02X tagr=%02X CYC=%d\n", PC, mem[addr].cell, mem[addr].tagr, cycles);
#else
  fprintf(trace_file, "memrd_debug after if: PC=%04X Addr=2401 Value=%02X tagr=%02X CYC=%d\n", PC, mem[addr].cell, mem[addr].tagr, cycles);
#endif

  return value;
}

#define memrd(addr, PC, cyc) (mem[addr].tagr ? MEMRD(addr, PC, cyc) : mem[addr].cell)
//#define memrd(addr, PC, cyc) memrd_debug(addr, PC, cyc)
#define memrdwd(addr, PC, cyc) ((mem[addr].tagr || mem[(addr) + 1].tagr) ? (MEMRD(addr, PC, cyc) | (MEMRD((addr) + 1, PC, cyc) << 8)) : (mem[addr].cell | (mem[(addr) + 1].cell << 8)))

// See TeensyMAME1 for when flipword is needed (Star Wars and Quantum)
// bytes are flipped
#define memrdwd_flip(addr, PC, cyc) ((mem[addr].tagr || mem[(addr) + 1].tagr) ? ((MEMRD((addr) + 1, PC, cyc) | (MEMRD(addr, PC, cyc) << 8))) : ((mem[(addr) + 1].cell | (mem[addr].cell << 8))))

#define memwr(addr, val, PC, cyc) \
  if (mem[addr].tagw) MEMWR(addr, val, PC, cyc); \
  else mem[addr].cell = val

void MEMWR(uint16_t addr, uint8_t val, uint16_t PC, uint32_t cyc);

// DISPLAY.C AVG FUNCTIONS

#define rdColor(c) memrd((c) + m_colorram_offset, 0, 0)
#define rdVram(r) memrd((r) + m_vectorram_offset, 0, 0)
#define rdProm(p) avg_prom[(p)]

static int colorram[16]; /* colorram entries */

int old_x = 0;
int old_y = 0;

static inline void draw_line(int32_t x1, int32_t y1, int32_t x2, int32_t y2, int color, int z) {
    /*
  #ifdef VSTCM  // Teensy (GCC)
        Serial.printf( "DRAW LINE from: %p -> (%d, %d) -> (%d, %d) Color=%d Z=%d\n",
                      __builtin_return_address( 0 ), x1, y1, x2, y2, color, z );
        Serial.flush();
  #else  // PC (Windows, MSVC)
        void* stack[1];
        uint32_t frames = CaptureStackBackTrace( 0, 1, stack, NULL );
        fprintf( trace_file, "DRAW LINE from: %p -> (%d, %d) -> (%d, %d) Color=%d Z=%d\n",
                frames ? stack[0] : NULL, x1, y1, x2, y2, color, z );
        fflush( trace_file );
  #endif
    */

    // if (game == ASTEROIDS || game == ASTEROIDS_DX) {
    // Asteroids x = -32 to 1055, y = -31 to 800
    x1 = 1 + (((x1 + 32) * 243000) >> 16);
    x2 = 1 + (((x2 + 32) * 243000) >> 16);
    y1 = 1 + (((y1 + 31) * 310000) >> 16);
    y2 = 1 + (((y2 + 31) * 310000) >> 16);
    // }

      /*	if (game == GRAVITAR || game == SPACE_DUEL) {
              x1 += 512;
              x2 += 512;
              y1 -= 512;
              y2 -= 512;
          }*/

          // Apply game-specific screen offsets
    x1 += game_screen_offset_x;
    y1 += game_screen_offset_y;
    x2 += game_screen_offset_x;
    y2 += game_screen_offset_y;

    if (x1 < 0 || x1 > 4095 || y1 < 0 || y1 > 4095 || x2 < 0 || x2 > 4095 || y2 < 0 || y2 > 4095)
        return;  // don't bother drawing it if it's out of bounds
    /*
if (x1 < x1min) x1min = x1;
if (x2 < x2min) x2min = x2;
if (x1 > x1max) x1max = x1;
if (x2 > x2max) x2max = x2;

if (y1 < y1min) y1min = y1;
if (y2 < y2min) y2min = y2;
if (y1 > y1max) y1max = y1;
if (y2 > y2max) y2max = y2;

char msg[100];

sprintf (msg, "x1min %d x1max %d x2min %d x2max %d y1min %d y1max %d y2min %d y2max %d", x1min, x1max, x2min, x2max, y1min, y1max, y2min, y2max );
Serial.println(msg);
*/

/*
observed min & max coordinates sent to draw_line

bzone/astdelux
x = 44 to 1164
Y = -640 to 1344

bwidow
x = -128 to 152
y = -591 to 985

asteroids
x = 188 to 860
y = -1017 to 1826
*/

//  if ((x1 == x2) && (y1 == y2))
//   x2++;

// other drawing routines in this code seem to define a colour for each pixel, so need to incorporate that in the draw_to_xyrgb function


// could store previous coordinates in order to prevent needless move commands
// also use a buffer to time each frame

//if (z != 0) {
    draw_moveto(x1, 4096 - y1);

	if (game == TEMPEST) {  // Tempest sends out of bounds values for some reason so this is a quick kludge
        if (color < 0 || color >= 8 || z < 0 || z >= 16) {
            // Handle error or return
            color = 3;
            z = 8;
          //  return;
        }
    }
  // draw_to_xyrgb(x2, 4096 - y2, gColorValue[color][z].red >> 8, gColorValue[color][z].green >> 8, gColorValue[color][z].blue >> 8);
  draw_to_xyrgb(x2, 4096 - y2, gColorValue[color][z].red, gColorValue[color][z].green, gColorValue[color][z].blue);
  // } else
  //  draw_moveto(x2, 4096 - y2);
}

void vg_add_point_buf(int x, int y, int color, int intensity) {
#define SHIFT_T 10
  // printf("draw_line: %i,%i,%i,%i,%i,%i\n", (old_x>>SHIFT_T), -(old_y>>SHIFT_T), (x>>SHIFT_T), -(y>>SHIFT_T), color, intensity>>4);
  //  draw_line2 ((old_x>>SHIFT_T), -(old_y>>SHIFT_T), (x>>SHIFT_T), -(y>>SHIFT_T), color, intensity>>4);
  //draw_line2((old_y >> SHIFT_T), -(old_x >> SHIFT_T), (y >> SHIFT_T), -(x >> SHIFT_T), color, intensity >> 4);
 // draw_line((old_y >> SHIFT_T), -(old_x >> SHIFT_T), (y >> SHIFT_T), -(x >> SHIFT_T), color, intensity >> 4);
  draw_line( old_y, old_x, x, y, color, intensity );
  old_x = x;
  old_y = y;
}



/********************************************************************
 *
 *  AVG handler functions
 *
 *  AVG is in many ways different from DVG. The only thing they have
 *  in common is the state machine approach. There are small
 *  differences among the AVGs, mostly related to color and vector
 *  clipping.
 *
 *******************************************************************/
void avg_init(u16 vram, u16 cram) {
  /* AVG PROM */
  //	ROM_REGION( 0x100, "avg:prom", 0 )
  //	ROM_LOAD( "136002-125.d7",   0x0000, 0x0100, CRC(5903af03) SHA1(24bc0366f394ad0ec486919212e38be0f08d0239) )
  m_prom = avg_prom;

  //    2000-2FFF  R/W   D  D  D  D  D  D  D  D   Vector Ram (4K)
  //    3000-3FFF   R    D  D  D  D  D  D  D  D   Vector Rom (4K)
  m_vectorram_offset = vram;


  //    0800-080F   W                D  D  D  D   Colour ram
  m_colorram_offset = cram;

  m_pc = 0;
  m_sp = 0;
  m_dvx = 0;
  m_dvy = 0;
  m_dvy12 = 0;
  m_timer = 0;
  m_stack[0] = 0;
  m_stack[1] = 0;
  m_stack[2] = 0;
  m_stack[3] = 0;
  m_data = 0;

  m_state_latch = 0;
  m_int_latch = 0;
  m_scale = 0;
  m_bin_scale = 0;
  m_intensity = 0;
  m_color = 0;
  m_enspkl = 0;
  m_spkl_shift = 0;
  m_map = 0;

  m_hst = 0;
  m_lst = 0;
  m_izblank = 0;

  m_op = 0;
  m_halt = 0;
  m_sync_halt = 0;

  m_xdac_xor = 0;
  m_ydac_xor = 0;

  m_xpos = 0;
  m_ypos = 0;


  m_xcenter = 0;
  m_ycenter = 0;
  old_x = 0;
  old_y = 0;


  //    m_xcenter = ((m_xmax - m_xmin) / 2) << 15;
  //    m_ycenter = ((m_ymax - m_ymin) / 2) << 15;


  /*
     * The x and y DACs use 10 bit of the counter values which are in
     * two's complement representation. The DAC input is xored with
     * 0x200 to convert the value to unsigned.
     */
  m_xdac_xor = 0x200;
  m_ydac_xor = 0x200;
}

void vg_init(void) {
    if (dvg) {
        // DVG initialization
        vector_mem_offset = 0x4000; // For Asteroids/Battlezone
        portrait = 0;
    }
    else {
        // AVG initialization
        vector_mem_offset = 0x2000; // For Gravitar/Black Widow
        if (game == TEMPEST) {
            avg_init(vector_mem_offset, 0x800); // Tempest needs special init
        }
    }
}

u8 state_addr()  // avg_state_addr
{
  return (((m_state_latch >> 4) ^ 1) << 7)
         | (m_op << 4)
         | (m_state_latch & 0xf);
}

void update_databus()  // avg_data
{
  m_data = rdVram((m_pc & 0x1fff) ^ 1);  // 0x2000 being the vectorram+rom length - this should not go out of bounds!
}

void vggo()  // avg_vggo
{
  m_pc = 0;
  m_sp = 0;
}

void vgrst()  // avg_vgrst
{
  m_state_latch = 0;
  m_bin_scale = 0;
  m_scale = 0;
  m_color = 0;
}

int handler_0()  // avg_latch0
{
  m_dvy = (m_dvy & 0x1f00) | m_data;
  m_pc++;

  return 0;
}

int handler_1()  // avg_latch1
{
  m_dvy12 = (m_data >> 4) & 1;
  m_op = m_data >> 5;

  m_int_latch = 0;
  m_dvy = (m_dvy12 << 12) | ((m_data & 0xf) << 8);
  m_dvx = 0;
  m_pc++;

  return 0;
}

int handler_2()  // avg_latch2
{
  m_dvx = (m_dvx & 0x1f00) | m_data;
  m_pc++;

  return 0;
}

int handler_3()  // avg_latch3
{
  m_int_latch = m_data >> 4;
  m_dvx = ((m_int_latch & 1) << 12) | ((m_data & 0xf) << 8) | (m_dvx & 0xff);
  m_pc++;

  return 0;
}

int handler_4()  // avg_strobe0
{
  if (OP0) {
    m_stack[m_sp & 3] = m_pc;
  } else {
    /*
		 * Normalization is done to get roughly constant deflection
		 * speeds. See Jed's essay why this is important. In addition
		 * to the intensity and overall time saving issues it is also
		 * needed to avoid accumulation of DAC errors. The X/Y DACs
		 * only use bits 3-12. The normalization ensures that the
		 * first three bits hold no important information.
		 *
		 * The circuit doesn't check for dvx=dvy=0. In this case
		 * shifting goes on as long as VCTR, SCALE and CNTR are
		 * low. We cut off after 16 shifts.
		 */
    int i = 0;
    while ((((m_dvy ^ (m_dvy << 1)) & 0x1000) == 0)
           && (((m_dvx ^ (m_dvx << 1)) & 0x1000) == 0)
           && (i++ < 16)) {
      m_dvy = (m_dvy & 0x1000) | ((m_dvy << 1) & 0x1fff);
      m_dvx = (m_dvx & 0x1000) | ((m_dvx << 1) & 0x1fff);
      m_timer >>= 1;
      m_timer |= 0x4000 | (OP1 << 6);
    }

    if (OP1)
      m_timer &= 0xff;
  }

  return 0;
}

int avg_common_strobe1() {
  if (OP2) {
    if (OP1)
      m_sp = (m_sp - 1) & 0xf;
    else
      m_sp = (m_sp + 1) & 0xf;
  }
  return 0;
}

int handler_5()  // avg_strobe1
{
  if (OP2 == 0) {
    for (int i = m_bin_scale; i > 0; i--) {
      m_timer >>= 1;
      m_timer |= 0x4000 | (OP1 << 6);
    }
    if (OP1)
      m_timer &= 0xff;
  }

  return avg_common_strobe1();
}

int avg_common_strobe2() {
  if (OP2) {
    if (OP0) {
      m_pc = m_dvy << 1;

      if (m_dvy == 0) {
        /*
				 * Tempest and Quantum keep the AVG in an endless
				 * loop. I.e. at one point the AVG jumps to address 0
				 * and starts over again. The main CPU updates vector
				 * RAM while AVG is running. The hardware takes care
				 * that the AVG doesn't read vector RAM while the CPU
				 * writes to it. Usually we wait until the AVG stops
				 * (halt flag) and then draw all vectors at once. This
				 * doesn't work for Tempest and Quantum so we wait for
				 * the jump to zero and draw vectors then.
				 *
				 * Note that this has nothing to do with the real hardware
				 * because for a vector monitor it is perfectly okay to
				 * have the AVG drawing all the time. In the emulation we
				 * somehow have to divide the stream of vectors into
				 * 'frames'.
				 */
      }
    } else {
      m_pc = m_stack[m_sp & 3];
    }
  } else {
    if (m_dvy12) {
      m_scale = m_dvy & 0xff;
      m_bin_scale = (m_dvy >> 8) & 7;
    }
  }

  return 0;
}

int handler_6()  // avg_strobe2
{
  if (!OP2 && !m_dvy12 ) {
    m_color = m_dvy & 0x7;
    m_intensity = (m_dvy >> 4) & 0xf;
  }

  return avg_common_strobe2();
}

int avg_common_strobe3() {
  int cycles = 0;

  m_halt = OP0;

 // if ((m_op & 5) == 0) {
  if (!OP0 && !OP2) { // RC 12/04/2025: updated to same as latest version of MAME
    if (OP1) {
      cycles = 0x100 - (m_timer & 0xff);
    } else {
      cycles = 0x8000 - m_timer;
    }
    m_timer = 0;

    m_xpos += ((((m_dvx >> 3) ^ m_xdac_xor) - 0x200) * cycles * (m_scale ^ 0xff)) >> 4;
    m_ypos -= ((((m_dvy >> 3) ^ m_ydac_xor) - 0x200) * cycles * (m_scale ^ 0xff)) >> 4;
  }

  if (OP2) {
    cycles = 0x8000 - m_timer;
    m_timer = 0;
    m_xpos = m_xcenter;
    m_ypos = m_ycenter;
    vg_add_point_buf(m_xpos, m_ypos, 0, 0);  
  }

  return cycles;
}

int handler_7()  // avg_strobe3
{
  const int cycles = avg_common_strobe3();

  //if ((m_op & 5) == 0) {
  if (!OP0 && !OP2) { // RC 12/04/2025: updated to same as latest version of MAME
    vg_add_point_buf(m_xpos, m_ypos, m_color, (((m_int_latch >> 1) == 1) ? m_intensity : m_int_latch & 0xe) << 4);
  }

  return cycles;
}

/*************************************
 *
 *  Tempest handler functions
 *
 *************************************/

int tempest_handler_6()  // tempest_strobe2
{
  if (!OP2 && !m_dvy12) {
    // Contrary to previous documentation in MAME,
		// Tempest does not have the m_enspkl bit. 
    if (m_dvy & 0x800)
      m_color = m_dvy & 0xf;
    else
      m_intensity = (m_dvy >> 4) & 0xf;
  }

  return avg_common_strobe2();
}

int rgb_t(u8 r, u8 g, u8 b) {
  return ((r + g + b) / 3) / 32;
}

// In tempest_handler_7()
int tempest_handler_7() {
    const int cycles = avg_common_strobe3();
    if (!OP0 && !OP2) {
        const uint8_t data = rdColor(m_color);
        const uint8_t r = ((data >> 1) & 1) ? 0xff : 0x00;
        const uint8_t g = ((data >> 2) & 1) ? 0xff : 0x00;
        const uint8_t b = ((data >> 0) & 1) ? 0xff : 0x00;
        const int rgb = (r << 16) | (g << 8) | b;

        vg_add_point_buf(m_xpos, m_ypos, rgb,
            ((m_int_latch >> 1) == 1) ? m_intensity : (m_int_latch & 0xe) << 4);
    }
    return cycles;
}

/*************************************
 *
 *  halt functions
 *
 *************************************/

void avg_halt(int dummy) {
  m_halt = dummy;
  m_sync_halt = dummy;
}

// NB Lastest version of MAME in avgdvg.cpp has different functions to handle Major Havoc, Quantum, Tempest,
// Battle Zone and Star Wars which need to be replicated here

/********************************************************************
 *
 * State Machine
 *
 * The state machine is a 256x4 bit PROM connected to a latch. The
 * address of the next state is generated from the latched previous
 * state, an op code and the halt flag. Op codes come from vector
 * RAM/ROM. The state machine is clocked with 1.5 MHz. Three bits of
 * the state are decoded and used to trigger various parts of the
 * hardware.
 *
 *******************************************************************/

// This code can be found in TIMER_CALLBACK_MEMBER(avgdvg_device_base::run_state_machine)
// in the latest version of MAME  

void avg_draw_vector_list_t() {
    int cycles = 0;

    if (game != TEMPEST) 
        return;  // Only for Tempest

    // Special state machine implementation
    while (!m_halt) {
        m_state_latch = (m_state_latch & 0x10) | (rdProm(state_addr()) & 0xf);

        if (ST3) {
            update_databus();
            switch (m_state_latch & 7) {
            case 0: cycles += handler_0(); break;
            case 1: cycles += handler_1(); break;
            case 2: cycles += handler_2(); break;
            case 3: cycles += handler_3(); break;
            case 4: cycles += handler_4(); break;
            case 5: cycles += handler_5(); break;
            case 6: cycles += tempest_handler_6(); break;
            case 7: cycles += tempest_handler_7(); break;
            }
        }
        m_state_latch = (m_halt << 4) | (m_state_latch & 0xf);
        cycles += 8;
    }
}


/*************************************
 *
 *  VG halt/vggo
 *
 ************************************/


// go_w in MAME
void avg_go(unsigned long cyc) {
  vggo();
  /*
	if (m_sync_halt && (m_nvect > 10))
	{
		/ *
		 * This is a good time to start a new frame. Major Havoc
		 * sometimes sets VGGO after a very short vector list. That's
		 * why we ignore frames with less than 10 vectors.
		 * /
		 
		 // non tempest
	}
*/

  avg_halt(0);
  avg_draw_vector_list_t();
}

/*************************************
 *
 *  Reset
 *
 ************************************/

void avg_reset(unsigned long cyc) {
  vgrst();
  // vg_set_halt in MAME
  avg_halt(1);
}
// END DISPLAY.C AVG FUNCTIONS



/*
// Process queued sound commands
void processSounds() {
  if (soundQueueStart != soundQueueEnd) {
    uint8_t cmd = soundQueue[soundQueueStart];
    soundQueueStart = (soundQueueStart + 1) % MAX_SOUND_QUEUE;

    switch (cmd) {
      case 0x01:  // Ship Fire
        waveform1.begin(WAVEFORM_SQUARE);
        waveform1.frequency(1000);
        waveform1.amplitude(0.8);
        delay(50);
        waveform1.amplitude(0);
        break;

      case 0x02:  // Explosion
        waveform1.begin(WAVEFORM_SQUARE);
        // waveform1.begin( WAVEFORM_NOISE );
        waveform1.frequency(500);
        waveform1.amplitude(1.0);
        delay(300);
        waveform1.amplitude(0);
        break;

      case 0x03:  // UFO Sound
        waveform1.begin(WAVEFORM_SAWTOOTH);
        waveform1.frequency(200);
        waveform1.amplitude(0.5);
        delay(500);
        waveform1.amplitude(0);
        break;

      case 0x04:  // UFO Fire
        waveform1.begin(WAVEFORM_SQUARE);
        waveform1.frequency(1500);
        waveform1.amplitude(0.7);
        delay(50);
        waveform1.amplitude(0);
        break;

      case 0x05:  // Thump Sound
        waveform1.begin(WAVEFORM_SQUARE);
        waveform1.frequency(50);  // Low thump frequency
        waveform1.amplitude(0.6);
        delay(thumpSpeed);  // Control tempo
        waveform1.amplitude(0);

        // Speed up as asteroids decrease
        if (asteroidCount > 1) {
          thumpSpeed = max(200, 1000 - (asteroidCount * 80));  // Faster thumps
        }
        break;
      default:
        break;
    }
  }
}
*/
/*
 * display.c: Atari DVG and AVG simulators
 */



static int vector_timer(long deltax, long deltay) {
  deltax = labs(deltax);
  deltay = labs(deltay);
  return max(deltax, deltay) >> 17;
}

static void dvg_vector_timer(int scale) {
  vg_done_cyc += 4 << scale;
}


static void dvg_draw_vector_list(void) {
  static int32_t pc;
  static int32_t sp;
  static int32_t stack[MAXSTACK];

  static int32_t scale;
  //static int32_t statz;

  static int32_t currentx;
  static int32_t currenty;

  int done = 0;

  //int firstwd = 0, secondwd = 0;
  uint16_t firstwd = 0, secondwd = 0;
  int opcode;

  int32_t x, y;
  int z, temp;
  int a;

  int32_t oldx, oldy;
  int32_t deltax, deltay;


  pc = 0;
  sp = 0;
  scale = 0;
  // statz = 0;
  if (portrait) {
    currentx = VSIZE;
    currenty = HSIZE;
  } else {
    currentx = HSIZE;
    currenty = VSIZE;
  }

  while (!done) {
    vg_done_cyc += 8;
#ifdef VG_DEBUG
    if (vg_step) {
      printf("Current beam position: (%d, %d)\n",
             currentx, currenty);
      getchar();
    }
#endif
    firstwd = memrdwd(map_addr(pc), 0, 0);
    opcode = firstwd >> 12;
#ifdef VG_DEBUG
    if (vg_print)
      printf("%4x: %4x ", map_addr(pc), firstwd);
#endif
    pc++;
    if ((opcode >= 0 /* DVCTR */) && (opcode <= DLABS)) {
      secondwd = memrdwd(map_addr(pc), 0, 0);
      pc++;
#ifdef VG_DEBUG
      if (vg_print)
        printf("%4x  ", secondwd);
#endif
    }
#ifdef VG_DEBUG
    else if (vg_print)
      printf("      ");
#endif

#ifdef VG_DEBUG
    if (vg_print)
      printf("%s ", dvg_mnem[opcode]);
#endif

    switch (opcode) {
      case 0:
#ifdef DVG_OP_0_ERR
        printf("Error: DVG opcode 0!  Addr %4x Instr %4x %4x\n", map_addr(pc - 2), firstwd, secondwd);
        done = 1;
        break;
#endif
      case 1:
      case 2:
      case 3:
      case 4:
      case 5:
      case 6:
      case 7:
      case 8:
      case 9:
        y = firstwd & 0x03ff;
        if (firstwd & 0x0400)
          y = -y;
        x = secondwd & 0x03ff;
        if (secondwd & 0x400)
          x = -x;
        z = secondwd >> 12;
#ifdef VG_DEBUG
        if (vg_print) {
          printf("(%d,%d) z: %d scal: %d", x, y, z, opcode);
        }
#endif
#if 0
	  if (vg_step)
	    {
	      printf ("\nx: %x  x<<21: %x  (x<<21)>>%d: %x\n", x, x<<21, 30-(scale+opcode), (x<<21)>>(30-(scale+opcode)));
	      printf ("y: %x  y<<21: %x  (y<<21)>>%d: %x\n", y, y<<21, 30-(scale+opcode), (y<<21)>>(30-(scale+opcode)));
	    }
#endif
        oldx = currentx;
        oldy = currenty;
        temp = (scale + opcode) & 0x0f;
        if (temp > 9)
          temp = -1;
        deltax = (x << 21) >> (30 - temp);
        deltay = (y << 21) >> (30 - temp);
#if 0
	  if (vg_step)
	    {
	      printf ("deltax: %x  deltay: %x\n", deltax, deltay);
	    }
#endif
        currentx += deltax;
        currenty -= deltay;
        dvg_vector_timer(temp);

		  // See VIDEO_AVGDVG.C for more sophisticated code for adding vectors to a buffer, colour handling, etc.
        
        draw_line(oldx, oldy, currentx, currenty, 7, z);    // 7 = always same colour... needs to be improved as Bzone needs Green & Red
        break;

      case DLABS:
        x = twos_comp_val(secondwd, 12);
        y = twos_comp_val(firstwd, 12);
        /*
	  x = secondwd & 0x07ff;
	  if (secondwd & 0x0800)
	    x = x - 0x1000;
	  y = firstwd & 0x07ff;
	  if (firstwd & 0x0800)
	    y = y - 0x1000;
*/
		  // This scaling is handled better in video_avgdvg.c, improve this
        scale = secondwd >> 12;
        currentx = x;
        currenty = (896 - y);  // This prevents wraparound of the Y axis with the scores at the bottom of the screen

#ifdef VG_DEBUG
        if (vg_print) {
          printf("(%d,%d) scal: %d", x, y, secondwd >> 12);
        }
#endif
        break;

      case DHALT:
#ifdef VG_DEBUG
        if (vg_print)
          if ((firstwd & 0x0fff) != 0)
            printf("(%d?)", firstwd & 0x0fff);
#endif
        done = 1;
        break;

      case DJSRL:
        a = firstwd & 0x0fff;
#ifdef VG_DEBUG
        if (vg_print)
          printf("%4x", map_addr(a));
#endif
        stack[sp] = pc;
        if (sp == (MAXSTACK - 1)) {
          printf("\n*** Vector generator stack overflow! ***\n");
          done = 1;
          sp = 0;
        } else
          sp++;
        pc = a;
        break;

      case DRTSL:
#ifdef VG_DEBUG
        if (vg_print)
          if ((firstwd & 0x0fff) != 0)
            printf("(%d?)", firstwd & 0x0fff);
#endif
        if (sp == 0) {
          printf("\n*** Vector generator stack underflow! ***\n");
          done = 1;
          sp = MAXSTACK - 1;
        } else
          sp--;
        pc = stack[sp];
        break;

      case DJMPL:
        a = firstwd & 0x0fff;
#ifdef VG_DEBUG
        if (vg_print)
          printf("%4x", map_addr(a));
#endif
        pc = a;
        break;

      case DSVEC:
        y = firstwd & 0x0300;
        if (firstwd & 0x0400)
          y = -y;
        x = (firstwd & 0x03) << 8;
        if (firstwd & 0x04)
          x = -x;
        z = (firstwd >> 4) & 0x0f;
        temp = 2 + ((firstwd >> 2) & 0x02) + ((firstwd >> 11) & 0x01);
#ifdef VG_DEBUG
        if (vg_print) {
          printf("(%d,%d) z: %d scal: %d", x, y, z, temp);
        }
#endif
#if 0
	  if (vg_step)
	    {
	      printf ("\nx: %x  x<<21: %x  (x<<21)>>%d: %x\n", x, x<<21, 30-(scale+temp), (x<<21)>>(30-(scale+temp)));
	      printf ("y: %x  y<<21: %x  (y<<21)>>%d: %x\n", y, y<<21, 30-(scale+temp), (y<<21)>>(30-(scale+temp)));
	    }
#endif
        oldx = currentx;
        oldy = currenty;
        temp = (scale + temp) & 0x0f;
        if (temp > 9)
          temp = -1;
        deltax = (x << 21) >> (30 - temp);
        deltay = (y << 21) >> (30 - temp);
#if 0
	  if (vg_step)
	    {
	      printf ("deltax: %x  deltay: %x\n", deltax, deltay);
	    }
#endif
        currentx += deltax;
        currenty -= deltay;
        //   dvg_vector_timer(temp);   // commented out in pitrex code but present in TeensyMAME
        vg_done_cyc += ((4 << temp) * 4) / 8;
        draw_line(oldx, oldy, currentx, currenty, 7, z);
        break;

      default:
        fprintf(stderr, "internal error\n");
        done = 1;
    }
#ifdef VG_DEBUG
    if (vg_print)
      printf("\n");
#endif
  }
}

static void avg_draw_vector_list(void) {
  static int32_t pc;
  static int32_t sp;
  static int32_t stack[MAXSTACK];

  static long xscale, yscale;  // June 2022 RC separate X & Y scales to fill screen
                               //  static int32_t scale;
  static int32_t statz;
  static int32_t color;

  static int32_t currentx;
  static int32_t currenty;

  int done = 0;

  int32_t firstwd, secondwd;
  int32_t opcode;

  int32_t x, y, z, b, l, a;

  int32_t oldx, oldy;
  int32_t deltax, deltay;

  pc = 0;
  sp = 0;

#define XREF 16384  // The smaller these numbers are, the faster the game executes!
#define YREF 16384
  xscale = 8192;
  yscale = 8192;
  //   scale = 16384;

  statz = 0;
  color = 0;

  if (portrait) {
    currentx = VSIZE;
    currenty = HSIZE;
  } else {
    currentx = HSIZE;
    currenty = VSIZE;
  }

  firstwd = memrdwd(map_addr(pc), 0, 0);
  secondwd = memrdwd(map_addr(pc + 1), 0, 0);
  if ((firstwd == 0) && (secondwd == 0)) {
    printf("VGO with zeroed vector memory\n");
    return;
  }

  while (!done) {
    vg_done_cyc += 8;
#ifdef VG_DEBUG
    if (vg_step)
      getchar();
#endif
    firstwd = memrdwd(map_addr(pc), 0, 0);
    opcode = firstwd >> 13;
#ifdef VG_DEBUG
    if (vg_print)
      printf("%4x: %4x ", map_addr(pc), firstwd);
#endif
    pc++;
    if (opcode == VCTR) {
      secondwd = memrdwd(map_addr(pc), 0, 0);
      pc++;
#ifdef VG_DEBUG
      if (vg_print)
        printf("%4x  ", secondwd);
#endif
    }
#ifdef VG_DEBUG
    else if (vg_print)
      printf("      ");
#endif

    if ((opcode == STAT) && ((firstwd & 0x1000) != 0))
      opcode = SCAL;

#ifdef VG_DEBUG
    if (vg_print)
      printf("%s ", avg_mnem[opcode]);
#endif

    switch (opcode) {
      case VCTR:
			/* From TeensyMAME code:
         if (vectorEngine == USE_AVG_QUANTUM)
         {
            x = twos_comp_val( secondwd, 12 );
            y = twos_comp_val( firstwd, 12 );
         }
         else
         {
           // These work for all other games. 
            x = twos_comp_val( secondwd, 13 );
            y = twos_comp_val( firstwd, 13 );
         }
         z = (secondwd >> 12) & ~0x01;
         */

        x = twos_comp_val(secondwd, 13);
        y = twos_comp_val(firstwd, 13);
        z = 2 * (secondwd >> 13);
#ifdef VG_DEBUG
        if (vg_print)
          printf("%d,%d,", x, y);
#endif
        if (z == 0) {
#ifdef VG_DEBUG
          if (vg_print)
            printf("blank");
#endif
        } else if (z == 2) {
          z = statz;
#ifdef VG_DEBUG
          if (vg_print)
            printf("stat (%d)", z);
#endif
        }
#ifdef VG_DEBUG
        else if (vg_print)
          printf("%d", z);
#endif
        oldx = currentx;
        oldy = currenty;
        //     deltax = x * scale;
        //     deltay = y * scale;
        deltax = x * xscale;
        deltay = y * yscale;

        currentx += deltax;
        currenty -= deltay;
        vg_done_cyc += vector_timer(deltax, deltay);

       // draw_line(oldx >> 13, oldy >> 13, currentx >> 13, currenty >> 13, color, z);
        // Gravitar uses this line of code, so colour needs to be correct, not 7! however using color in place of 7 doesn't draw line currently
        draw_line( oldx >> 13, oldy >> 13, currentx >> 13, currenty >> 13, color, z );
        break;

      case SVEC:     // Draw a short vector
        x = twos_comp_val(firstwd, 5) << 1;
        y = twos_comp_val(firstwd >> 8, 5) << 1;
        z = 2 * ((firstwd >> 5) & 7);
#ifdef VG_DEBUG
        if (vg_print)
          printf("%d,%d,", x, y);
#endif
        if (z == 0) {
#ifdef VG_DEBUG
          if (vg_print)
            printf("blank");
#endif
        } else if (z == 2) {
          z = statz;
#ifdef VG_DEBUG
          if (vg_print)
            printf("stat");
#endif
        }
#ifdef VG_DEBUG
        else if (vg_print)
          printf("%d", z);
#endif
        oldx = currentx;
        oldy = currenty;

        deltax = x * xscale;
        deltay = y * yscale;
        //  deltax = x * scale;
        //  deltay = y * scale;
        currentx += deltax;
        currenty -= deltay;
        vg_done_cyc += vector_timer(labs(deltax), labs(deltay));
        draw_line(oldx >> 13, oldy >> 13, currentx >> 13, currenty >> 13, color, z);
        break;

      case STAT:
         // These lines are a bit different for Star Wars, see lib retro source for details
         // also Tempest needs a sparkle bit
        color = firstwd & 0x0f;
        statz = (firstwd >> 4) & 0x0f;
#ifdef VG_DEBUG
        if (vg_print)
          printf("z: %d color: %d", statz, color);
#endif
        /* should do e, h, i flags here! */
        break;

      case SCAL:  // scaling of graphics
                  /*
        The firstwd variable contains the 16-bit instruction word fetched from the DVG’s instruction stream. 
        The SCAL instruction in the DVG format encodes scaling information in it.

        b = (firstwd >> 8) & 0x07;

        This extracts bits 8 to 10 (00000bbbxxxxxxxx).
        b is the shift amount (scale factor) used to divide the coordinate values.
        Since it's a 3-bit value (0-7), it effectively controls scaling by powers of 2.
        l = firstwd & 0xff;

        This extracts bits 0 to 7 (xxxxxxxx).
        l is a base scale factor that determines the magnitude of the scaling operation.
        */
        b = (firstwd >> 8) & 0x07;
        l = firstwd & 0xff;

        xscale = (XREF - (l << 6)) >> b;
        yscale = (YREF - (l << 6)) >> b;

        // scale = (16384 - (l << 6)) >> b;
        /* scale = (1.0-(l/256.0)) * (2.0 / (1 << b)); */


#ifdef VG_DEBUG
        if (vg_print) {
          printf("bin: %d, lin: ", b);
          if (l > 0x80)
            printf("(%d?)", l);
          else
            printf("%d", l);
          printf(" scale: %f", (scale / 8192.0));
        }
#endif
        break;

      case CNTR:  // centre?

#ifdef VG_DEBUG

        int32_t d = firstwd & 0xff;

        if (vg_print) {
          if (d != 0x40)
            printf("%d", d);
        }
#endif

        if (portrait) {
          currentx = VSIZE;
          currenty = HSIZE;
        } else {
          currentx = HSIZE;
          currenty = VSIZE;
        }

        // Maybe should move to the centre like in other versions of MAME?
       /* currentx = xcenter;  // ASG 080497 .ac JAN2498 
        currenty = ycenter;  
        vector_add_point( currentx, currenty, 0, 0 ); */

        break;

      case RTSL:
#ifdef VG_DEBUG
        if (vg_print)
          if ((firstwd & 0x1fff) != 0)
            printf("(%d?)", firstwd & 0x1fff);
#endif
        if (sp == 0) {
          printf("\n*** Vector generator stack underflow! ***\n");
          done = 1;
          sp = MAXSTACK - 1;
        } else
          sp--;
        pc = stack[sp];
        break;

      case HALT:
#ifdef VG_DEBUG
        if (vg_print)
          if ((firstwd & 0x1fff) != 0)
            printf("(%d?)", firstwd & 0x1fff);
#endif
        done = 1;
        break;

      case JMPL:
        a = firstwd & 0x1fff;
#ifdef VG_DEBUG
        if (vg_print)
          printf("%4x", map_addr(a));
#endif
        pc = a;
        break;

      case JSRL:
        a = firstwd & 0x1fff;
#ifdef VG_DEBUG
        if (vg_print)
          printf("%4x", map_addr(a));
#endif
        stack[sp] = pc;
        if (sp == (MAXSTACK - 1)) {
          printf("\n*** Vector generator stack overflow! ***\n");
          done = 1;
          sp = 0;
        } else
          sp++;
        pc = a;
        break;

      default:
        fprintf(stderr, "internal error\n");
    }
#ifdef VG_DEBUG
    if (vg_print)
      printf("\n");
#endif
  }
}

int32_t drop_frames = 0;
static int32_t df = 1;


// Add a timestamp to throttle vg_go to 60Hz
static uint32_t last_vg_millis = 0;

void vg_go( uint32_t cyc ) {
   vg_busy = 1;
   if (game == TEMPEST) 
       // Tempest requires faster vector updates
       vg_done_cyc = cyc + 500;  // Instead of +8
   else
   vg_done_cyc = cyc + 8;

#ifdef VG_DEBUG
   vgo_count++;
   if (trace_vgo)
      printf( "VGO #%d at cycle %d, delta %d\n", vgo_count, cyc, cyc - last_vgo_cyc );
   last_vgo_cyc = cyc;
#endif

   if (--df == 0) {
      df = (drop_frames > 0) ? drop_frames : 1;

      // Throttle to ~60Hz (16ms/frame)
#ifdef VSTCM
      while (millis() - last_vg_millis < 16) {
        // wait actively (or use delayMicroseconds(100))
      }
      last_vg_millis = millis();
#else
      while (SDL_GetTicks() - last_vg_millis < 16) {
         SDL_Delay( 1 ); // yield to system
      }
      last_vg_millis = SDL_GetTicks();
#endif

      if (dvg) {
         dvg_draw_vector_list();
      }
      else {
         if (game == TEMPEST) {
            avg_go( cyc );
         }
         else {
            avg_draw_vector_list();
         }
      }
   }
}


void old_vg_go(uint32_t cyc) {
  /*
#ifdef VSTCM
   Serial.println( "vg_go() STARTED!" );
   Serial.flush();
#else
   fprintf( trace_file, "vg_go() STARTED!\n" );
   fflush( trace_file );
#endif
*/
  vg_busy = 1;
  vg_done_cyc = cyc + 8;
#ifdef VG_DEBUG
  vgo_count++;
  if (trace_vgo)
    printf("VGO #%d at cycle %d, delta %d\n", vgo_count, cyc, cyc - last_vgo_cyc);
  last_vgo_cyc = cyc;
#endif

  if (--df == 0) {
    df = (drop_frames > 0) ? drop_frames : 1;
    if (dvg) {
      dvg_draw_vector_list();
    } else {
      if (game == TEMPEST) {
        avg_go(cyc);
      } else
        avg_draw_vector_list();
    }
  }
}

void vg_reset(uint32_t cyc) {
  vg_busy = 0;
#ifdef VG_DEBUG
  if (trace_vgo)
    printf("vector generator reset @%04x\n", PC);
#endif
}

/*
 * game.c: Atari Vector game definitions & setup functions
 */

typedef struct {
  int32_t show;
  const char *kw1;
  const char *kw2;
  const char *name;
} game_info;

game_info game_names[] = {
  { 0, "unknown", "??", "Unknown" },
  { 1, "lunar", "ll", "Lunar Lander" },
  { 1, "asteroids", "a", "Asteroids" },
  { 1, "deluxe", "ad", "Asteroids Deluxe" },
  { 0, "redbaron", "rb", "Red Baron" },
  { 0, "battlezone", "bz", "Battlezone" },
  { 0, "tempest", "t", "Tempest" },
  { 1, "spaceduel", "sd", "Space Duel" },
  { 1, "gravitar", "g", "Gravitar" },
  { 1, "blackwidow", "bw", "Black Widow" },
  { 0, "majorhavoc", "mh", "The Adventures of Major Havoc" },
  { 0, "starwars", "sw", "Star Wars" },
  { 0, "empire", "tesb", "The Empire Strikes Back" },
  { 0, "quantum", "q", "Quantum" }
};

int pick_game(char *name) {
  int32_t i;

  for (i = FIRST_GAME; i <= LAST_GAME; i++)
    if ((strcmp(name, game_names[i].kw1) == 0) || (strcmp(name, game_names[i].kw2) == 0)) {
      return (i);
    }

  fprintf(stderr, "ERROR: Unknown game \"%s\"\n", name);
  exit(1);
}

void show_games(void) {
  int32_t i;

  for (i = FIRST_GAME; i <= LAST_GAME; i++)
    if (game_names[i].show)
      fprintf(stderr, "    %-10s    %s\n", game_names[i].kw1, game_names[i].name);
}

char *game_name(int game) {
  return (char *)(game_names[game].name);
}

rom_info black_widow_roms[] = {
  { "roms/BlackWidow/136017.101", 0x9000, 0x1000, 0 },
  { "roms/BlackWidow/136017.102", 0xa000, 0x1000, 0 },
  { "roms/BlackWidow/136017.103", 0xb000, 0x1000, 0 },
  { "roms/BlackWidow/136017.104", 0xc000, 0x1000, 0 },
  { "roms/BlackWidow/136017.105", 0xd000, 0x1000, 0 },
  { "roms/BlackWidow/136017.106", 0xe000, 0x1000, 0 },

  { "roms/BlackWidow/136017.107", 0x2800, 0x0800, 0 },
  { "roms/BlackWidow/136017.108", 0x3000, 0x1000, 0 },
  { "roms/BlackWidow/136017.109", 0x4000, 0x1000, 0 },
  { "roms/BlackWidow/136017.110", 0x5000, 0x1000, 0 },

  { NULL, 0, 0, 0 }
};

tag_info black_widow_tags[] = {
  { 0x0000, 0x0800, RD | WR, MEMORY }, /* RAM */

  { 0x2000, 0x0800, RD | WR, VECRAM }, /* vector RAM */

  /* they try to scribble on the vector ROM */
  { 0x2fac, 0x002c, WR, IGNWRT },

  { 0x6000, 0x0800, RD | WR, POKEY1 },
  { 0x6800, 0x0800, RD | WR, POKEY2 },

  { 0x6008, 1, RD, OPTSW1 },
  { 0x6808, 1, RD, OPTSW2 },

  { 0x7000, 4, RD, EAROMRD }, /* why 4 locs? */

  { 0x7800, 1, RD, COININ },
  { 0x8000, 1, RD, GRAVITAR_IN1 },
  { 0x8800, 1, WR, COINOUT },
  { 0x8800, 1, RD, GRAVITAR_IN2 },
  { 0x8840, 1, WR, VGO },
  { 0x8880, 1, WR, VGRST },
  { 0x88c0, 1, WR, INTACK },
  { 0x8900, 1, WR, EAROMCON },
  { 0x8940, 0x40, WR, EAROMWR },

  /* they write to 8981..89ed */
  { 0x8980, 0x6e, WR, WDCLR },

  { 0, 0, 0, 0 }
};
/*
rom_info gravitar_roms[] = {
  { "roms/Gravitar/136010.201", 0x9000, 0x1000, 0 },
  { "roms/Gravitar/136010.202", 0xa000, 0x1000, 0 },
  { "roms/Gravitar/136010.203", 0xb000, 0x1000, 0 },
  { "roms/Gravitar/136010.204", 0xc000, 0x1000, 0 },
  { "roms/Gravitar/136010.205", 0xd000, 0x1000, 0 },
  { "roms/Gravitar/136010.206", 0xe000, 0x1000, 0 },

  { "roms/Gravitar/136010.210", 0x2800, 0x0800, 0 },
  { "roms/Gravitar/136010.207", 0x3000, 0x1000, 0 },
  { "roms/Gravitar/136010.208", 0x4000, 0x1000, 0 },
  { "roms/Gravitar/136010.209", 0x5000, 0x1000, 0 },

  { NULL, 0, 0, 0 }
};
*/
rom_info gravitar_roms[] =
{
  { "roms/gravitar/136010-301.d1", 0x9000, 0x1000, 0 },
  { "roms/gravitar/136010-302.ef1", 0xa000, 0x1000, 0 },
  { "roms/gravitar/136010-303.h1", 0xb000, 0x1000, 0 },
  { "roms/gravitar/136010-304.j1", 0xc000, 0x1000, 0 },
  { "roms/gravitar/136010-305.kl1", 0xd000, 0x1000, 0 },
  { "roms/gravitar/136010-306.m1", 0xe000, 0x1000, 0 },
  { "roms/gravitar/136010-210.l7", 0x2800, 0x0800, 0 },
  { "roms/gravitar/136010-207.mn7", 0x3000, 0x1000, 0 },
  { "roms/gravitar/136010-208.np7", 0x4000, 0x1000, 0 },
  { "roms/gravitar/136010-309.r7", 0x5000, 0x1000, 0 },

  { NULL,   0,      0,      0 }
};

tag_info gravitar_tags[] = {
  { 0x0000, 0x0800, RD | WR, MEMORY }, /* RAM */

  { 0x2000, 0x0800, RD | WR, VECRAM }, /* vector RAM */

  { 0x6000, 0x0800, RD | WR, POKEY1 },
  { 0x6800, 0x0800, RD | WR, POKEY2 },

  { 0x6008, 1, RD, OPTSW1 },
  { 0x6808, 1, RD, OPTSW2 },

  { 0x7000, 4, RD, EAROMRD }, /* why 4 locs? */

  { 0x7800, 1, RD, COININ },
  { 0x8000, 1, RD, GRAVITAR_IN1 },
  { 0x8800, 1, WR, COINOUT },
  { 0x8800, 1, RD, GRAVITAR_IN2 },
  { 0x8840, 1, WR, VGO },
  { 0x8880, 1, WR, VGRST },
  { 0x88c0, 1, WR, INTACK },
  { 0x8900, 1, WR, EAROMCON },
  { 0x8940, 0x40, WR, EAROMWR },
  { 0x8980, 1, WR, WDCLR },

  { 0, 0, 0, 0 }
};

rom_info space_duel_roms[] = {
  { "roms/SpaceDuel/136006.201", 0x4000, 0x1000, 0 },
  { "roms/SpaceDuel/136006.102", 0x5000, 0x1000, 0 },
  { "roms/SpaceDuel/136006.103", 0x6000, 0x1000, 0 },
  { "roms/SpaceDuel/136006.104", 0x7000, 0x1000, 0 },
  { "roms/SpaceDuel/136006.105", 0x8000, 0x1000, 0 },

  { "roms/SpaceDuel/136006.106", 0x2800, 0x0800, 0 },
  { "roms/SpaceDuel/136006.107", 0x3000, 0x1000, 0 },

  { NULL, 0, 0, 0 }
};

tag_info space_duel_tags[] = {
  { 0x0000, 0x0400, RD | WR, MEMORY }, /* RAM */
  { 0x1000, 0x0400, RD | WR, POKEY1 },
  { 0x1400, 0x0400, RD | WR, POKEY2 },
  { 0x2000, 0x0800, RD | WR, VECRAM },

  { 0x0800, 1, RD, COININ },

  /* Space duel uses an ASL instruction to get bit 7 of some
     of its inputs into the carry flag.  The write may be safely
     ignored.  ELS 920718 */
  { 0x0900, 8, RD, SD_INPUTS },
  { 0x0905, 2, WR, IGNWRT },

  { 0x0a00, 1, RD, EAROMRD },
  { 0x0c00, 1, WR, COINOUT },
  { 0x0c80, 1, WR, VGO },
  { 0x0d00, 1, WR, WDCLR },
  { 0x0d80, 1, WR, VGRST },
  { 0x0e00, 1, WR, INTACK },
  { 0x0e80, 1, WR, EAROMCON },
  { 0x0f00, 0x40, WR, EAROMWR },
  { 0x1008, 1, RD, OPTSW1 },
  { 0x1408, 1, RD, OPTSW2 },

  { 0, 0, 0, 0 }
};

rom_info tempest_roms[] = {
  { "roms/Tempest/136002-133.d1", 0x9000, 0x1000, 0 },
  { "roms/Tempest/136002-134.f1", 0xa000, 0x1000, 0 },
  { "roms/Tempest/136002-235.j1", 0xb000, 0x1000, 0 },  // or .235
  { "roms/Tempest/136002-136.lm1", 0xc000, 0x1000, 0 },
  { "roms/Tempest/136002-237.p1", 0xd000, 0x1000, 0 },  // or .237
  { "roms/Tempest/136002-138.np3", 0x3000, 0x1000, 0 },
  { NULL, 0, 0, 0 }
};
/*
rom_info tempest_roms[] = {
#if 0
  { "roms/Tempest/136002.133", 0x9000, 0x1000, 0 },
  { "roms/Tempest/136002.134", 0xa000, 0x1000, 0 },
  { "roms/Tempest/136002.135", 0xb000, 0x1000, 0 },  // or .235
  { "roms/Tempest/136002.136", 0xc000, 0x1000, 0 },
  { "roms/Tempest/136002.137", 0xd000, 0x1000, 0 },  // or .237

  { "roms/Tempest/136002.138", 0x3000, 0x1000, 0 },
#else
  { "roms/Tempest/136002.113", 0x9000, 0x0800, 0 },
  { "roms/Tempest/136002.114", 0x9800, 0x0800, 0 },
  { "roms/Tempest/136002.115", 0xa000, 0x0800, 0 },
  { "roms/Tempest/136002.116", 0xa800, 0x0800, 0 },
  { "roms/Tempest/136002.117", 0xb000, 0x0800, 0 }, // or .217
  { "roms/Tempest/136002.118", 0xb800, 0x0800, 0 },
  { "roms/Tempest/136002.119", 0xc000, 0x0800, 0 },
  { "roms/Tempest/136002.120", 0xc800, 0x0800, 0 },
  { "roms/Tempest/136002.121", 0xd000, 0x0800, 0 },
  { "roms/Tempest/136002.122", 0xd800, 0x0800, 0 }, // or .222 

  { "roms/Tempest/136002.123", 0x3000, 0x0800, 0 },
  { "roms/Tempest/136002.124", 0x3800, 0x0800, 0 },
#endif
  { NULL, 0, 0, 0 }
};
*/

tag_info tempest_tags[] = {
  { 0x0000, 0x0800, RD | WR, MEMORY }, /* RAM */
  { 0x0800, 0x0010, WR, COLORRAM },

  { 0x0C00, 1, RD, COININ },
  { 0x0d00, 1, RD, OPTSW1 },
  { 0x0e00, 1, RD, OPTSW2 },

  { 0x2000, 0x1000, RD | WR, VECRAM },

  { 0x4000, 1, WR, COINOUT },
  { 0x4800, 1, WR, VGO },
  { 0x5000, 1, WR, WDCLR },
  { 0x5800, 1, WR, VGRST },

  { 0x6000, 0x40, WR, EAROMWR },
  { 0x6040, 1, WR, EAROMCON },
  { 0x6050, 1, RD, EAROMRD },

  { 0x6040, 1, RD, MBSTAT },
  { 0x6060, 1, RD, MBLO },
  { 0x6070, 1, RD, MBHI },
  { 0x6080, 0x20, WR, MBSTART },

  { 0x60C0, 0x10, RD | WR, POKEY1 },
  { 0x60D0, 0x10, RD | WR, POKEY2 },

  { 0x60e0, 1, WR, TEMP_OUTPUTS },

  { 0, 0, 0, 0 }
};


rom_info battlezone_roms[] = {
  { "roms/bzone/036414-02.e1", 0x5000, 0x0800, 0 },
  { "roms/bzone/036413-01.h1", 0x5800, 0x0800, 0 },
  { "roms/bzone/036412-01.j1", 0x6000, 0x0800, 0 },
  { "roms/bzone/036411-01.k1", 0x6800, 0x0800, 0 },
  { "roms/bzone/036410-01.lm1", 0x7000, 0x0800, 0 },
  { "roms/bzone/036409-01.n1", 0x7800, 0x0800, 0 },
  { "roms/bzone/036422-01.bc3", 0x3000, 0x0800, 0 },
  { "roms/bzone/036421-01.a3", 0x3800, 0x0800, 0 },
  { NULL, 0, 0, 0 }
};


/*
rom_info battlezone_roms[] = {
  { "roms/Battlezone/036414a.01", 0x5000, 0x0800, 0 },
  { "roms/Battlezone/036413.01", 0x5800, 0x0800, 0 },
  { "roms/Battlezone/036412.01", 0x6000, 0x0800, 0 },
  { "roms/Battlezone/036411.01", 0x6800, 0x0800, 0 },
  { "roms/Battlezone/036410.01", 0x7000, 0x0800, 0 },
  { "roms/Battlezone/036409.01", 0x7800, 0x0800, 0 },

  { "roms/Battlezone/036422.01", 0x3000, 0x0800, 0 },
  { "roms/Battlezone/036421.01", 0x3800, 0x0800, 0 },

  { NULL, 0, 0, 0 }
};
*/
tag_info battlezone_tags[] = {
  { 0x0000, 0x0400, RD | WR, MEMORY }, /* RAM */

  { 0x0800, 1, RD, COININ },
  { 0x0a00, 1, RD, OPTSW1 },
  { 0x0c00, 1, RD, OPTSW2 },
  { 0x1000, 1, WR, COINOUT },
  { 0x1200, 1, WR, VGO },
  { 0x1400, 1, WR, WDCLR },
  { 0x1600, 1, WR, VGRST },

  { 0x1800, 1, RD, MBSTAT },
  { 0x1810, 1, RD, MBLO },
  { 0x1818, 1, RD, MBHI },

  { 0x1820, 0x10, RD | WR, POKEY1 },
  { 0x1828, 1, RD, BZ_INPUTS },
  { 0x1840, 1, WR, BZ_SOUND },

  { 0x1860, 0x20, WR, MBSTART },

  { 0x2000, 0x1000, RD | WR, VECRAM },

  { 0, 0, 0, 0 }
};

/*
rom_info red_baron_roms[] = {
#if 1 // Battlezone conversion - uses a 2732 with a disgusting hack 
  { "roms/RedBaron/037587.01", 0x4800, 0x0800, 0 },
  { "roms/RedBaron/037000.01E", 0x5000, 0x0800, 0 },
  { "roms/RedBaron/037587.01", 0x5800, 0x0800, 0x0800 },
#else
  { "roms/RedBaron/037001.01E", 0x4800, 0x0800, 0 },
  { "roms/RedBaron/037000.01E", 0x5000, 0x0800, 0 },
  { "roms/RedBaron/036999.01E", 0x5800, 0x0800, 0 },
#endif
  { "roms/RedBaron/036998.01E", 0x6000, 0x0800, 0 },
  { "roms/RedBaron/036997.01E", 0x6800, 0x0800, 0 },
  { "roms/RedBaron/036996.01E", 0x7000, 0x0800, 0 },
  { "roms/RedBaron/036995.01E", 0x7800, 0x0800, 0 },

  { "roms/RedBaron/037006.01E", 0x3000, 0x0800, 0 },
  { "roms/RedBaron/037007.01E", 0x3800, 0x0800, 0 },

  { NULL, 0, 0, 0 }
};*/

rom_info red_baron_roms[] = {
  { "roms/redbaron/037587-01.fh1", 0x4800, 0x0800, 0 },
  { "roms/redbaron/037000-01.e1", 0x5000, 0x0800, 0 },
  { "roms/redbaron/037587-01.fh1", 0x5800, 0x0800, 0x0800 },
  { "roms/redbaron/036998-01.j1", 0x6000, 0x0800, 0 },
  { "roms/redbaron/036997-01.k1", 0x6800, 0x0800, 0 },
  { "roms/redbaron/036996-01.lm1", 0x7000, 0x0800, 0 },
  { "roms/redbaron/036995-01.n1", 0x7800, 0x0800, 0 },
  { "roms/redbaron/037006-01.bc3", 0x3000, 0x0800, 0 },
  { "roms/redbaron/037007-01.a3", 0x3800, 0x0800, 0 },
  { NULL, 0, 0, 0 }
};

tag_info red_baron_tags[] = {
  { 0x0000, 0x0400, RD | WR, MEMORY }, /* RAM */

  { 0x0800, 1, RD, COININ },
  { 0x0a00, 1, RD, OPTSW1 },
  { 0x0c00, 1, RD, OPTSW2 },
  { 0x1000, 1, WR, COINOUT },
  { 0x1200, 1, WR, VGO },
  { 0x1400, 1, WR, WDCLR },
  { 0x1600, 1, WR, VGRST },

  { 0x1800, 1, RD, MBSTAT },
  { 0x1802, 1, RD, RB_SW },
  { 0x1804, 1, RD, MBLO },
  { 0x1806, 1, RD, MBHI },

  { 0x1808, 1, WR, RB_SND },
  { 0x180a, 1, WR, RB_SND_RST },
  { 0x180c, 1, WR, EAROMCON },

  { 0x1810, 0x10, RD | WR, POKEY1 },
  { 0x1818, 1, RD, RB_JOY },

  { 0x1820, 0x40, WR, EAROMWR },
  { 0x1820, 0x40, RD, EAROMRD },

  { 0x1860, 0x20, WR, MBSTART },

  { 0x2000, 0x1000, RD | WR, VECRAM },

  { 0, 0, 0, 0 }
};

rom_info lunar_lander_roms[] = {
  { "roms/llanderx/034572-02.f1", 0x6000, 0x0800, 0 },
  { "roms/llanderx/034571-02.de1", 0x6800, 0x0800, 0 },
  { "roms/llanderx/034570-01.c1", 0x7000, 0x0800, 0 },
  { "roms/llanderx/034569-02.b1", 0x7800, 0x0800, 0 },
  { "roms/llanderx/034599-01.r3", 0x4800, 0x0800, 0 },
  { "roms/llanderx/034598-01.np3", 0x5000, 0x0800, 0 },
  { "roms/llanderx/034597-01.m3", 0x5800, 0x0800, 0 },
  { NULL, 0, 0, 0 }
};

/*
rom_info lunar_lander_roms[] = {
  { "roms/Lunar/034572.02", 0x6000, 0x0800, 0 },
  { "roms/Lunar/034571.02", 0x6800, 0x0800, 0 },
  { "roms/Lunar/034570.02", 0x7000, 0x0800, 0 },
  { "roms/Lunar/034569.02", 0x7800, 0x0800, 0 },
  { "roms/Lunar/034599.01", 0x4800, 0x0800, 0 },
  { "roms/Lunar/034598.01", 0x5000, 0x0800, 0 },
  { "roms/Lunar/034597.01", 0x5800, 0x0800, 0 },
  { NULL, 0, 0, 0 }
};
*/
tag_info lunar_lander_tags[] = {
  { 0x0000, 0x0100, RD | WR, MEMORY },    /* RAM */
  { 0x0100, 0x0100, RD | WR, LUNAR_MEM }, /* copy of ZP for stack */

  { 0x2000, 1, RD, LUNAR_SW1 },
  { 0x2400, 8, RD, LUNAR_SW2 },
  { 0x2800, 4, RD, OPT1_2BIT },
  { 0x2C00, 1, RD, LUNAR_POT },

  { 0x3000, 1, WR, VGO },
  { 0x3200, 1, WR, LUNAR_OUT },
  { 0x3400, 1, WR, WDCLR },
  { 0x3800, 1, WR, DMACNT },
  { 0x3C00, 1, WR, LUNAR_SND },
  { 0x3E00, 1, WR, LUNAR_SND_RST },

  { 0x4000, 0x0800, RD | WR, VECRAM },

  /* they try to increment 0x5800 to test for presence of the 
     French/German/Spanish message ROM */
  { 0x5800, 1, WR, IGNWRT },

  { 0, 0, 0, 0 }
};

rom_info asteroids_roms[] = {
  { "roms/Asteroid2/035145-02.ef2", 0x6800, 0x0800, 0 },
  { "roms/Asteroid2/035144-02.h2", 0x7000, 0x0800, 0 },
  { "roms/Asteroid2/035143-02.j2", 0x7800, 0x0800, 0 },
  { "roms/Asteroid2/035127-02.np3", 0x5000, 0x0800, 0 },
  { NULL, 0, 0, 0 }
};

/*
rom_info asteroids_roms[] = {
  { "roms/Asteroids/035145.02", 0x6800, 0x0800, 0 },
  { "roms/Asteroids/035144.02", 0x7000, 0x0800, 0 },
  { "roms/Asteroids/035143.02", 0x7800, 0x0800, 0 },

  { "roms/Asteroids/035127.02", 0x5000, 0x0800, 0 },

  { NULL, 0, 0, 0 }
};
*/
tag_info asteroids_tags[] = {
  { 0x0000, 0x0400, RD | WR, MEMORY }, /* RAM */

  { 0x2000, 8, RD, ASTEROIDS_SW1 },
  { 0x2400, 8, RD, ASTEROIDS_SW2 },
  { 0x2800, 4, RD, OPT1_2BIT },

  /* Asteroids uses an LSR instruction to get bit 0 of some
     of its inputs into the carry flag.  The write may be safely
     ignored, so we mark it as memory.  ELS 920721 */
  { 0x2000, 8, WR, MEMORY },
  { 0x2400, 8, WR, MEMORY },
  { 0x2802, 4, WR, MEMORY },

  { 0x3000, 1, WR, VGO },
  { 0x3200, 1, WR, ASTEROIDS_OUT },
  { 0x3400, 1, WR, WDCLR },
  { 0x3600, 1, WR, ASTEROIDS_EXP },
  { 0x3800, 1, WR, DMACNT },
  { 0x3A00, 1, WR, ASTEROIDS_THUMP },
  { 0x3C00, 6, WR, ASTEROIDS_SND },
  { 0x3E00, 1, WR, ASTEROIDS_SND_RST },

  { 0x4000, 0x0800, RD | WR, VECRAM },

  { 0, 0, 0, 0 }
};

rom_info asteroidsdx_roms[] = {
#ifdef OLD_AD
  { "roms/astdelu2/036430.01", 0x6000, 0x0800, 0 },
  { "roms/astdelu2/036431.01", 0x6800, 0x0800, 0 },
  { "roms/astdelu2/036432.01", 0x7000, 0x0800, 0 },
  { "roms/astdelu2/036433.02", 0x7800, 0x0800, 0 },
  { "roms/astdelu2/036800.01", 0x4800, 0x0800, 0 },
  { "roms/astdelu2/036799.01", 0x5000, 0x0800, 0 },
#else
  { "roms/astdelux/036430-02.d1", 0x6000, 0x0800, 0 },
  { "roms/astdelux/036431-02.ef1", 0x6800, 0x0800, 0 },
  { "roms/astdelux/036432-02.fh1", 0x7000, 0x0800, 0 },
  { "roms/astdelux/036433-03.j1", 0x7800, 0x0800, 0 },
  { "roms/astdelux/036800-02.r2", 0x4800, 0x0800, 0 },
  { "roms/astdelux/036799-01.np2", 0x5000, 0x0800, 0 },
#endif
  { NULL, 0, 0, 0 }
};

tag_info asteroidsdx_tags[] = {
  { 0x0000, 0x0400, RD | WR, MEMORY }, /* RAM */
  { 0x2000, 8, RD, ASTEROIDS_SW1 },
  { 0x2000, 8, WR, IGNWRT }, /* they use an LSR to read the switches */
  { 0x2400, 8, RD, ASTEROIDS_SW2 },
  { 0x2400, 8, WR, IGNWRT }, /* they use an LSR to read the switches */
  { 0x2800, 4, RD, OPT1_2BIT },
  { 0x2C00, 16, RD | WR, POKEY1 },
  { 0x2C08, 1, RD, OPTSW2 },
  { 0x2C40, 64, RD, EAROMRD },
  { 0x3000, 1, WR, VGO },
  { 0x3200, 64, WR, EAROMWR },
  { 0x3400, 1, WR, WDCLR },
  { 0x3600, 1, WR, ASTEROIDS_EXP },
  { 0x3800, 1, WR, VGRST },
  { 0x3A00, 1, WR, EAROMCON },
  { 0x3c00, 8, WR, AST_DEL_OUT },
  { 0x3e00, 1, WR, ASTEROIDS_SND_RST },
  { 0x4000, 0x0800, RD | WR, VECRAM },

  { 0, 0, 0, 0 }
};

rom_info major_havoc_roms[] =
{
  /* this is copied from Gravitar and hasn't yet been updated! */
  { "roms/MajorHavoc/136025.104", 0x9000, 0x4000, 0 },
  { "roms/MajorHavoc/136025.103", 0xa000, 0x4000, 0 },
  { "roms/MajorHavoc/136025.109", 0xb000, 0x4000, 0 },
  { "roms/MajorHavoc/136025.101", 0xc000, 0x4000, 0 },
  { "roms/MajorHavoc/136025.106", 0xd000, 0x4000, 0 },
  { "roms/MajorHavoc/136025.107", 0xe000, 0x4000, 0 },
  { "roms/MajorHavoc/136025.108", 0x3000, 0x4000, 0 },

  /* vector generator */
  { "roms/MajorHavoc/136025.110", 0x2000, 0x2000, 0 },

  { NULL,   0,      0,      0 }
};

/*
rom_info major_havoc_roms[] = {
 
  // updated from mame 0.36 (RC)


  // Alpha Processor ROMs 
  // ROM_REGION( 0x21000, REGION_CPU1 )

  // vector generator
  { "roms/MajorHavoc/136010.110", 0x5000, 0x2000, 0 },
  // programme ROM
  { "roms/MajorHavoc/136010.103", 0x8000, 0x4000, 0 },
  { "roms/MajorHavoc/136025.104", 0xc000, 0x4000, 0 },

  // Paged Program ROM - switched to 2000-3fff
  { "roms/MajorHavoc/136010.101", 0x10000, 0x4000, 0 },  //page 0+1
  { "roms/MajorHavoc/136010.109", 0x14000, 0x4000, 0 },  //page 2+3
  // Paged Vector Generator ROM
  { "roms/MajorHavoc/136010.106", 0x18000, 0x4000, 0 },  //page 0+1
  { "roms/MajorHavoc/136010.107", 0x1c000, 0x4000, 0 },  //page 2+3
  // the last 0x1000 is used for the 2 RAM pages

  // Gamma Processor ROM
  { "roms/MajorHavoc/136010.108", 0x08000, 0x4000, 0 },

  //  ROM_REGION( 0x10000, REGION_CPU2 )	// 16k for code 

  // ROM_RELOAD( 0x0c000, 0x4000 ) // reset+interrupt vectors 

  { NULL, 0, 0, 0 }
};
*/
tag_info major_havoc_tags[] = {
  /* this is copied from Gravitar and hasn't yet been updated! */
  { 0x0000, 0x0800, RD | WR, MEMORY }, /* RAM */

  { 0x2000, 0x0800, RD | WR, VECRAM }, /* vector RAM */

  { 0x6000, 0x0800, RD | WR, POKEY1 },
  { 0x6800, 0x0800, RD | WR, POKEY2 },

  { 0x6008, 1, RD, OPTSW1 },
  { 0x6808, 1, RD, OPTSW2 },

  { 0x7000, 4, RD, EAROMRD }, /* why 4 locs? */

  { 0x7800, 1, RD, COININ },
  { 0x8000, 1, RD, GRAVITAR_IN1 },
  { 0x8800, 1, WR, COINOUT },
  { 0x8800, 1, RD, GRAVITAR_IN2 },
  { 0x8840, 1, WR, VGO },
  { 0x8880, 1, WR, VGRST },
  { 0x88c0, 1, WR, INTACK },
  { 0x8900, 1, WR, EAROMCON },
  { 0x8940, 0x40, WR, EAROMWR },
  { 0x8980, 1, WR, WDCLR },

  { 0, 0, 0, 0 }
};

void tag_area(uint16_t addr, uint32_t len, int32_t dir, int32_t tag) {
  uint32_t i;  // This can take a value of 0x10000, so it needs to be 32 bits

  // printf("Mapping addr %04X-%04X with tag %02X\n", addr, addr + len - 1, tag);

  // casts avoid compiler warnings
  printf("Mapping addr %04X-%04X with tag %02x\n", (unsigned int)addr, (unsigned int)(addr + len - 1), (unsigned int)tag);

  for (i = 0; i < len; i++) {
    if (dir & RD)
      mem[addr].tagr = tag;
    if (dir & WR)
      mem[addr].tagw = tag;
    addr++;
  }
}

int read_rom_image_avg( const char* fn, unsigned char *faddr, unsigned len, unsigned off_set ) {
   unsigned j;

#ifdef VSTCM
  // open the file on the sd card
   File dataFile = SD.open( fn, FILE_READ );

   if (dataFile) {
      Serial.println( fn );

      for (j = 0; j < len; j++) {
         faddr[j]= dataFile.read();
      }

      // close the file:
      dataFile.close();
   }
   else {
  // if the file didn't open, print an error:
      char msg[100];
      sprintf( msg, "error: can't open file '%s'.\n", fn );
      Serial.println( msg );
      return 1;
   }
#else
   char rompath[200];
   char rom[4096];  // Black Widow ROMS are 4K

   strcpy( rompath, MY_ROMPATH );
   strcat( rompath, fn );

   FILE* f = fopen( rompath, "rb" );
   if (f == NULL) {
     // strcpy(gMsg, "error: can't open file");

      printf( "error: can't open file '%s'.\n", rompath );
      return 1;
   }
   else
      printf( "loading '%s'.\n", rompath );

   size_t result = fread( rom, sizeof( uint8_t ), len, f );
   if (result != len) {
     // strcpy(gMsg, "error: while reading file");
      fprintf( stderr, "error: while reading file '%s'\n", rompath );
      return 1;
   }
   else

      printf( "loaded '%s'.\n", rompath );

   for (j = 0; j < len; j++) {
      faddr[j] = rom[j];
     
   }

   fclose( f );
#endif
   return 0;
}

int read_rom_image(const char *fn, unsigned faddr, unsigned len, unsigned off_set) {
  unsigned j;

#ifdef VSTCM
  // open the file on the sd card
  File dataFile = SD.open(fn, FILE_READ);

  if (dataFile) {
    Serial.println(fn);

    for (j = 0; j < len; j++) {
      mem[faddr].cell = dataFile.read();
      mem[faddr].tagr = 0;
      mem[faddr].tagw = ROMWRT;
      faddr++;
    }

    // close the file:
    dataFile.close();
  } else {
    // if the file didn't open, print an error:
    char msg[100];
    sprintf(msg, "error: can't open file '%s'.\n", fn);
    Serial.println(msg);
    return 1;
  }
#else
  char rompath[200];
  char rom[4096];  // Black Widow ROMS are 4K

  strcpy(rompath, MY_ROMPATH);
  strcat(rompath, fn);

  FILE *f = fopen(rompath, "rb");
  if (f == NULL) {
    // strcpy(gMsg, "error: can't open file");

    printf("error: can't open file '%s'.\n", rompath);
    return 1;
  } else
    printf("loading '%s'.\n", rompath);

  size_t result = fread(rom, sizeof(uint8_t), len, f);
  if (result != len) {
    // strcpy(gMsg, "error: while reading file");
    fprintf(stderr, "error: while reading file '%s'\n", rompath);
    return 1;
  } else

    printf("loaded '%s'.\n", rompath);

  for (j = 0; j < len; j++) {
    mem[faddr].cell = rom[j];
    mem[faddr].tagr = 0;
    mem[faddr].tagw = ROMWRT;
    faddr++;
  }

  fclose(f);
#endif
  return 0;
}

void setup_roms_and_tags(rom_info *rom_list, tag_info *tag_list) {
  // Log ROM setup
  while (rom_list->name != NULL) {
    read_rom_image(rom_list->name, rom_list->addr, rom_list->len, rom_list->offset);

    // 🔹 Log ROM loading results
    for (uint16_t i = 0; i < rom_list->len; i++) {
#ifdef VSTCM  // Teensy Logging
      Serial.printf("ROM[%s] Addr=%04X Value=%02X\n",
                    rom_list->name, rom_list->addr + i, mem[rom_list->addr + i].cell);
      Serial.flush();
#else  // PC Logging
      fprintf(trace_file, "ROM[%s] Addr=%04X Value=%02X\n",
              rom_list->name, rom_list->addr + i, mem[rom_list->addr + i].cell);
      fflush(trace_file);
#endif
    }
    rom_list++;
  }

  // Log tag setup
  while (tag_list->len != 0) {
    tag_area(tag_list->addr, tag_list->len, tag_list->dir, tag_list->tag);

    // 🔹 Log tag assignments
    for (uint16_t i = 0; i < tag_list->len; i++) {
#ifdef VSTCM  // Teensy Logging
      Serial.printf("TAG Addr=%04X Dir=%02X Tag=%02X\n",
                    tag_list->addr + i, tag_list->dir, tag_list->tag);
      Serial.flush();
#else  // PC Logging
      fprintf(trace_file, "TAG Addr=%04X Dir=%02X Tag=%02X\n",
              tag_list->addr + i, tag_list->dir, tag_list->tag);
      fflush(trace_file);
#endif
    }
    tag_list++;
  }
}

void copy_rom(uint16_t source, uint16_t dest, uint16_t len) {
  uint16_t i;

  for (i = 0; i < len; i++) {
    mem[dest].cell = mem[source].cell;
    mem[dest].tagr = mem[source].tagr;
    mem[dest].tagw = mem[source].tagw;
    dest++;
    source++;
  }
}

unsigned char m_rom_data[64];  // not persisting!
//static int SIZE_DATA = 0x40;
static unsigned char CK = 0x01;
static unsigned char C1 = 0x02;
static unsigned char C2 = 0x04;
static unsigned char CS1 = 0x08;
static unsigned char CS2 = 0x10;

// internal state
//unsigned char m_control_state;
//unsigned char m_address;
//static unsigned char m_data2;


/*
void update_state() {
  switch (m_control_state & (C1 | C2)) {
    // write mode; erasing is required, so we perform an AND against previous
    // data to simulate incorrect behavior if erasing was not done
    case 0:
      m_rom_data[m_address] &= m_data2;
      //            LOG("Write %02X = %02X\n", m_address, m_data);
      break;

    // erase mode
    case 0x10:  //C2:
      m_rom_data[m_address] = 0xff;
      //            LOG("Erase %02X\n", m_address);
      break;
  }
}
*/





void setup_game(void) {
  tag_area(0x0000, 0x10000, RD | WR, UNKNOWN);

  switch (game) {
    case BLACK_WIDOW:
      setup_roms_and_tags(black_widow_roms, black_widow_tags);

      /* copy_rom (0xe000, 0xf000, 0x1000); */
      copy_rom(0xeffa, 0xfffa, 6);

      // Bypass protection checks
      mem[0x963c].cell = 0xEA;  // NOP
      mem[0x963d].cell = 0xEA;  // NOP
      mem[0x963e].cell = 0xEA;  // NOP

      vector_mem_offset = 0x2000;

#ifdef MAGIC_PC
      mem[0x963c].magic = 1;
      mem[0x98a6].magic = 1;
      mem[0x9a4c].magic = 1;
#endif

      optionreg[0] = 0xff; /* switch D4, 1..8, off = 0, on = 1 */
      optionreg[1] = 0xff; /* switch B4, 1..8, off = 0, on = 1 */

      break;

    case GRAVITAR:
      setup_roms_and_tags(gravitar_roms, gravitar_tags);

      /* copy_rom (0xe000, 0xf000, 0x1000); */
      copy_rom(0xeffa, 0xfffa, 6);

      vector_mem_offset = 0x2000;

#ifdef MAGIC_PC
      mem[0xe8a6].magic = 1;
      mem[0xcd66].magic = 1;
      /* magicPC3 = 0xeccb; tried this for self-test, doesn't seem to help */
#endif

      optionreg[0] = 0x10; /* switch D4, 1..8, off = 0, on = 1 */
      optionreg[1] = 0x02; /* switch B4, 1..8, off = 0, on = 1 */

      break;

    case SPACE_DUEL:
      setup_roms_and_tags(space_duel_roms, space_duel_tags);

      /*
      copy_rom (0x8000, 0x9000, 0x1000);
      copy_rom (0x8000, 0xa000, 0x1000);
      copy_rom (0x8000, 0xb000, 0x1000);
      copy_rom (0x8000, 0xc000, 0x1000);
      copy_rom (0x8000, 0xd000, 0x1000);
      copy_rom (0x8000, 0xe000, 0x1000);
      copy_rom (0x8000, 0xf000, 0x1000);
      */
      copy_rom(0x8ffa, 0xfffa, 6);

      vector_mem_offset = 0x2000;

#ifdef MAGIC_PC
      mem[0x4027].magic = 1;
      mem[0x80ae].magic = 1;
#endif

      optionreg[0] = 0x00; /* switch D4 8..1, off = 0, on = 1 */
      optionreg[1] = 0x00; /* switch B4 8..1, off = 0, on = 1 */
                           /* set to 0x02 for free play */

      optionreg[2] = 0x00; /* switch P10/11 4..2, off = 0, on = 1 */

      break;

    case TEMPEST:
      init_earom();
      setup_roms_and_tags(tempest_roms, tempest_tags);

      // Bypass copy protection checks
      mem[0x11b].cell = 0x00;  // Normally checked value
      mem[0x455].cell = 0x00;  // Normally checked value
      mem[0x11f].cell = 0x00;  // Pokey piracy check
      mem[0x720].cell = 0x00;  // Pokey piracy check
      /*      
00011B  1  xx           copyr_vid_cksum1
000455  1  xx           copyr_vid_cksum2

00011F  1  xx           pokey_piracy_detected
000720  1  xx           pokey_piracy_detected2
*/
// read_rom_image(const char *fn, unsigned faddr, unsigned len, unsigned off_set)
//  read_rom_image(rom_list->name, rom_list->addr, rom_list->len, rom_list->offset);
    //  read_rom_image_avg("roms/tempest/136002-125.d7", &avg_prom, 256, 0 );  // NOT SURE THIS IS RIGHT, CHECK IN PITREX
        // Modify the function call to pass the correct type for the second argument.  
        // Use the array name without the address-of operator (&) to pass a pointer to the first element.  
        read_rom_image_avg("roms/tempest/136002-125.d7", avg_prom, 256, 0);
      vector_mem_offset = 0x2000;
      m_colorram_offset = 0x0800;
      avg_init(vector_mem_offset, 0x800);


      /* copy_rom (0xc000, 0xe000, 0x2000); */
      copy_rom(0xdffa, 0xfffa, 6);


#ifdef MAGIC_PC
      mem[0xc7a7].magic = 1;
#endif

      optionreg[0] = 0x02;  //N13 INVERTED and backwards
      optionreg[1] = 0x00;  //L12 INVERTED and backwards

      /*
   
    GAME OPTIONS:
    (8-position switch at N13 on Analog Vector-Generator PCB)

    1   2   3   4   5   6   7   8   Meaning
    -------------------------------------------------------------------------
    Off Off                         2 lives per game
    On  On                          3 lives per game
    On  Off                         4 lives per game
    Off On                          5 lives per game
            On  On  Off             Bonus life every 10000 pts
            On  On  On              Bonus life every 20000 pts
            On  Off On              Bonus life every 30000 pts
            On  Off Off             Bonus life every 40000 pts
            Off On  On              Bonus life every 50000 pts
            Off On  Off             Bonus life every 60000 pts
            Off Off On              Bonus life every 70000 pts
            Off Off Off             No bonus lives
                        On  On      English
                        On  Off     French
                        Off On      German
                        Off Off     Spanish
                                On  1-credit minimum
                                Off 2-credit minimum

 PRICING OPTIONS:
    (8-position switch at L12 on Analog Vector-Generator PCB)
    1   2   3   4   5   6   7   8   Meaning
    -------------------------------------------------------------------------
    On  On  On                      No bonus coins
    On  On  Off                     For every 2 coins, game adds 1 more coin
    On  Off On                      For every 4 coins, game adds 1 more coin
    On  Off Off                     For every 4 coins, game adds 2 more coins
    Off On  On                      For every 5 coins, game adds 1 more coin
    Off On  Off                     For every 3 coins, game adds 1 more coin
    On  Off                 Off On  Demonstration Mode (see notes)
    Off Off                 Off On  Demonstration-Freeze Mode (see notes)
                On                  Left coin mech * 1
                Off                 Left coin mech * 2
                    On  On          Right coin mech * 1
                    On  Off         Right coin mech * 4
                    Off On          Right coin mech * 5
                    Off Off         Right coin mech * 6
                            Off On  Free Play
                            Off Off 1 coin 2 plays
                            On  On  1 coin 1 play
                            On  Off 2 coins 1 play * 
 * 
    GAME OPTIONS:
    (4-position switch at D/E2 on Math Box PCB)

    1   2   3   4                   Meaning
    -------------------------------------------------------------------------
        Off                         Minimum rating range: 1, 3, 5, 7, 9
        On                          Minimum rating range tied to high score
            Off Off                 Medium difficulty (see notes)
            Off On                  Easy difficulty (see notes)
            On  Off                 Hard difficulty (see notes)
            On  On                  Medium difficulty (see notes)



*/

      portrait = 1;

      break;

    case BATTLEZONE:
      setup_roms_and_tags(battlezone_roms, battlezone_tags);

      /* copy_rom (0x5000, 0x4000, 0x1000); */
      /* copy_rom (0x5000, 0xd000, 0x3000); */
      copy_rom(0x7ffa, 0xfffa, 6);

      vector_mem_offset = 0x2000;

      // Bypass protection checks
      // For Battlezone
      mem[0x5000].cell = 0x60;  // RTS

#ifdef MAGIC_PC
      mem[0x5000].magic = 1;
#endif

      optionreg[0] = 0x15; /* M10 8..1 */
      optionreg[1] = 0x60; /* P10 8..1 */

      use_nmi = 1;

      break;

    case RED_BARON:
      setup_roms_and_tags(red_baron_roms, red_baron_tags);

      copy_rom(0x7ffa, 0xfffa, 6);

      vector_mem_offset = 0x2000;

      optionreg[0] = 0xff; /* M10 8..1 coins/credit */
      optionreg[1] = 0xeb; /* P10 8..1 language/# planes/bonus points*/

      use_nmi = 1;

      break;

    case LUNAR_LANDER:
      setup_roms_and_tags(lunar_lander_roms, lunar_lander_tags);

      copy_rom(0x7ffa, 0xfffa, 6);

      vector_mem_offset = 0x4000;

      /* they try to increment 0x5800 to test for presence of the 
	 French/German/Spanish message ROM */
      /*
      mem [0x5800].cell = 0xff;
      */

#ifdef MAGIC_PC
      mem[0x652d].magic = 1;
#endif

      optionreg[0] = 0xff;

      dvg = 1;
      use_nmi = 1;

      break;

    case ASTEROIDS:
      setup_roms_and_tags(asteroids_roms, asteroids_tags);

#ifdef VSTCM
      Serial.printf("ROM LOAD: Addr=%04X Value=%02X\n", 0xfffc, mem[0xfffc].cell);
      Serial.printf("ROM LOAD: Addr=%04X Value=%02X\n", 0xfffd, mem[0xfffd].cell);
#else
      fprintf(trace_file, "ROM LOAD: Addr=%04X Value=%02X\n", 0xfffc, mem[0xfffc].cell);
      fprintf(trace_file, "ROM LOAD: Addr=%04X Value=%02X\n", 0xfffd, mem[0xfffd].cell);
#endif
      copy_rom(0x7ffa, 0xfffa, 6);

      vector_mem_offset = 0x4000;

#ifdef MAGIC_PC
      mem[0x680c].magic = 1;
#endif

      optionreg[0] = 0x00;

      dvg = 1;
      use_nmi = 1;

      break;

    case ASTEROIDS_DX:
      setup_roms_and_tags(asteroidsdx_roms, asteroidsdx_tags);

      copy_rom(0x7ffa, 0xfffa, 6);

      vector_mem_offset = 0x4000;

#ifdef MAGIC_PC
      mem[0x601c].magic = 1;
      mem[0x601e].magic = 1;
#endif

      optionreg[0] = (~0xD3) & 0xff;

      dvg = 1;
      use_nmi = 1;

#ifdef OLD_AD
      if (mem[0x7cf8].cell != 0x9d) {
        fprintf(stderr, "Bad opcode at 7cf8!\n");
        mem[0x7cf8].cell = 0x9d;
      }

      if (mem[0x7d15].cell != 0xc9) {
        fprintf(stderr, "Bad branch offset at 7d15!\n");
        mem[0x7d15].cell = 0xc9;
      }
#endif

      break;

    case MAJOR_HAVOC:

      // NO IDEA IF THIS WORKS JUST MADE IT UP TO SEE IF IT WOULD WORK (RC)



      setup_roms_and_tags(major_havoc_roms, major_havoc_tags);
      copy_rom(0x7ffa, 0xfffa, 6);
      vector_mem_offset = 0x2000;
      break;

    default:
      fprintf(stderr, "ERROR: Unknown game\n");
      exit(1);
  }

  // Set the vector memory offset
  vg_init();
  init_earom();
  // Set screen position based on game
  set_game_screen_offset(game);
}

void handle_input() {
  // If attached to a arcade Harness, poll control panel buttons and save their state

#if VSTCM
  /* Button mapping on the MultiVekta Asteroids expansion PCB

AUDIO1      Output 1 from PT8211 DAC
AUDIO2      Output 2 from PT8211 DAC
BUTT1   33  Fire
BUTT2   34  Player 2 signal
BUTT3   35  Player 1 signal
BUTT4   36  Rotate right
BUTT5   37  Rotate left
BUTT6   40  Hyperspace
SW1      3  Thrust
SW2      0  Start 2 player
SW3      2  Left coin in
SW4      1  Centre coin in
SW5      4  Start 1 player
P1_LED  10  Player 1 LED
P2_LED  25  Player 2 LED

*/

  iBUTT1 = digitalRead(BUTT1) ? 128 : 0;
  iBUTT2 = digitalRead(BUTT2) ? 128 : 0;
  iBUTT3 = digitalRead(BUTT3) ? 128 : 0;
  iBUTT4 = digitalRead(BUTT4) ? 128 : 0;
  iBUTT5 = digitalRead(BUTT5) ? 128 : 0;
  iBUTT6 = digitalRead(BUTT6) ? 128 : 0;

  iSW1 = digitalRead(SW1) ? 128 : 0;
  iSW2 = digitalRead(SW2) ? 128 : 0;
  iSW3 = digitalRead(SW3) ? 128 : 0;
  iSW4 = digitalRead(SW4) ? 128 : 0;
  iSW5 = digitalRead(SW5) ? 128 : 0;

#else

  // Use keyboard to simulate pressing buttons on arcade control panel

  // COMMENTED OUT FOR TESTING

  /* iBUTT1 = 0;
  iBUTT2 = 0;
  iBUTT3 = 0;
  iBUTT4 = 0;
  iBUTT5 = 0;
  iBUTT6 = 0;
  iSW1 = 0;
  iSW2 = 0;
  iSW3 = 0;
  iSW4 = 0;
  iSW5 = 0;*/

  SDL_Event e;
  bool keydown = false;
  while (SDL_PollEvent(&e) != 0) {
    if (e.type == SDL_QUIT) {
      should_quit = true;
    } else if (e.type == SDL_WINDOWEVENT) {
      if (e.window.event == SDL_WINDOWEVENT_FOCUS_GAINED) {
        has_focus = true;
      } else if (e.window.event == SDL_WINDOWEVENT_FOCUS_LOST) {
        has_focus = false;
      }
    } else if (e.type == SDL_KEYDOWN && e.key.repeat == 0 && keydown == false) {  // check on repeat is to debounce (RC)
      keydown = true;                                                             // another attempt to debounce
      switch (e.key.keysym.scancode) {
        case SDL_SCANCODE_1:
          iBUTT1 = 128;
          break;
        case SDL_SCANCODE_2:
          iBUTT2 = 128;
          break;
        case SDL_SCANCODE_3:
          iBUTT3 = 128;
          break;
        case SDL_SCANCODE_4:
          iBUTT4 = 128;
          break;
        case SDL_SCANCODE_5:
          iBUTT5 = 128;
          break;
        case SDL_SCANCODE_6:
          iBUTT6 = 128;
          break;
        case SDL_SCANCODE_7:
          iSW1 = 128;
          break;
        case SDL_SCANCODE_8:
          iSW2 = 128;
          break;
        case SDL_SCANCODE_9:
          iSW3 = 128;
          break;
        case SDL_SCANCODE_0:
          iSW4 = 128;
          break;
        case SDL_SCANCODE_Q:
          iSW5 = 128;
          break;
      }
    }
  }
#endif
}

short MacIsKeyPressed(unsigned short k)
/* k = any keyboard scan code, 0-127 */
{
  /*  unsigned char km[16];

  //GetKeys ((unsigned long *) km);
  return ((km[k >> 3] >> (k & 7)) & 1);
  */
  return 0;  // get rid of compiler warning until this function is reimplemented for the Teensy
}

void MacInitColors(void) {
  short i;
  short z;
  short color;

  i = 15 * 7 - 1;
  for (z = 15; z >= 0; z--)
    for (color = 0; color < 8; color++)
      if ((color == 0) || (z == 0))
        gColorValue[color][z] = black;
      else {
        if (i >= 0) {
          gColorValue[color][z].red = (color & 4) ? (0x8000 + (0x0888 * z)) : 0;
          gColorValue[color][z].green = (color & 2) ? (0x8000 + (0x0888 * z)) : 0;
          gColorValue[color][z].blue = (color & 1) ? (0x8000 + (0x0888 * z)) : 0;
        } else
          gColorValue[color][z] = gColorValue[color][z + 1];
      }

   /* initialize the colorram */
  for (int i = 0; i < 16; i++)
     colorram[i] = i & 0x07;
}


/*
 * main.c: Atari Vector game simulator
 */

void show_usage_message(char *av0) {
#ifdef SINGLE_GAME
  fprintf(stderr, "Usage: %s [<options>]\n", av0);
#else
  fprintf(stderr, "Usage: %s <game> [<options>]\n", av0);
#endif
  fprintf(stderr,
          "   -st     self test mode\n"
#ifdef VG_DEBUG
          "   -vgo    trace vgo's\n"
#endif
          "   -load <file>  specify a memory dump file to load\n"
          "   -df <n> display only every nth frame\n"
          "   -large  use a large window\n"
          "   -nopm   don't use a pixmap (faster?)\n"
          "   -wide   use wide lines\n"
          "   -brk    break into debugger before starting\n");
#ifndef SINGLE_GAME
  fprintf(stderr,
          "\nThe following games are supported:\n"
          "    keyword       title\n"
          "    ----------    ------------------------------\n");
  show_games();
#endif
}

uint8_t save_A;
uint8_t save_X;
uint8_t save_Y;
uint8_t save_flags;
uint16_t save_PC;
uint8_t SP;

uint32_t irq_cycle;
uint32_t save_totcycles;

#ifdef WRAP_CYC_COUNT
uint32_t cyc_wraps = 0;
#endif

void reset_simulation() {
  vg_busy = 0;
  vg_done_cyc = 0;

  //  pc = 0;
  //sp = 0;
  //   memset(stack, 0, sizeof(stack));

  //currentx = 0;
  // currenty = 0;
#ifdef VSTCM
  //soundQueueStart = 0;
  //soundQueueEnd = 0;
  //memset(soundQueue, 0, sizeof(soundQueue));
#endif
  // game = 0;
  // use_nmi = 0;
}

#ifdef PT8211_SOUND

// PT8211 Pin Configuration (Assumes default Teensy 4.1 layout)
#define BCLK_PIN 21   // I2S BCLK
#define LRCLK_PIN 20  // I2S FS
#define DATA_PIN 7    // I2S TX

extern void setup_pt8211();

void generate_ship_fire() {
  uint32_t period_samples = SAMPLE_RATE / FIRE_FREQ;
  int16_t amplitude = 16000;  // Adjust as needed

  for (uint16_t i = 0; i < FIRE_SAMPLES; i++) {
    soundBuffer[i % BUFFER_SIZE] = (i % period_samples < (period_samples / 2)) ? amplitude : -amplitude;
  }

  bufferIndex = 0;
  isPlaying = 1;
}

void emu_sndInit() {
  setup_pt8211();
  generate_ship_fire();
}

void send_audio_sample(int16_t sample) {
  while (!(IMXRT_SAI1.TCSR & I2S_TCSR_FR)) {}  // Wait until FIFO is empty
  IMXRT_SAI1.TCSR |= I2S_TCSR_FR;              // Clear the FIFO Request Flag
}

void PIT_IRQHandler() {
  if (PIT_TFLG0 & 1) {
    PIT_TFLG0 = 1;  // Clear interrupt flag

    if (isPlaying) {
      send_audio_sample(soundBuffer[bufferIndex]);

      bufferIndex++;
      if (bufferIndex >= FIRE_SAMPLES) {
        isPlaying = 0;  // Stop playback
      }
    } else {
      send_audio_sample(0);  // Output silence when not playing
    }
  }
}

void queueSound(uint8_t soundCmd) {
  if (soundCmd == 0x01) {  // Ship Fire
    generate_ship_fire();
  }
}

void setupPIT() {
  CCM_CCGR1 |= CCM_CCGR1_PIT(CCM_CCGR_ON);
  PIT_MCR = 0x00;  // Enable PIT module

  PIT_LDVAL0 = (F_CPU / SAMPLE_RATE) - 1;      // Set interval for 44.1 kHz
  PIT_TCTRL0 = PIT_TCTRL_TEN | PIT_TCTRL_TIE;  // Enable timer and interrupt

  NVIC_ENABLE_IRQ(IRQ_PIT);
}
#endif

uint32_t g_millis_count;
extern uint32_t TickCount();

int vecsim(char *which_game) {
  // int32_t ac;
  //  char **av;

  int32_t show_usage = 1;
  // char *reload_file = NULL;

  // int32_t smallwindow = 1;
  // int32_t use_pixmap = 1;
  //  int32_t line_width = 0;

  // Open trace log file on PC
#ifndef VSTCM
  trace_file = fopen("trace_pc.log", "w");
  if (!trace_file) {
    printf("Error opening trace file!\n");
    return 0;
  } else
    printf("Trace file opened\n");
#endif

#ifdef THINK_C
  c = ccommand(&argv);
#endif

#ifdef USE_HISTORY
  using_history();
#endif

  mem = (elem *)calloc(65536L, sizeof(elem));
  if (!mem) {
    printf("Cannot get memory for emulator\n");
    exit(1);
  } else
    printf("Memory allocated SUCCESS\n");

  game = pick_game(which_game);
  show_usage = 0;

  setup_game();
  save_PC = (memrd(0xfffd, 0, 0) << 8) | memrd(0xfffc, 0, 0);
  save_A = 0;
  save_X = 0;
  save_Y = 0;
  save_flags = 0;
  save_totcycles = 0;
  irq_cycle = 8192;

  MacInitColors();
  // emu_sndInit();
  reset_simulation();
  g_millis_count = TickCount();  // Counts time to get approx 60hz refresh rate
  sim_6502();

  free(mem);  // Free up the memory we allocated
  mem = NULL;

  //quit:
  if (show_usage) {
    // show_usage_message(av[0]);
    printf("game quitting\n");
    return 1;
  }
  return 0;
}
/*
 * mathbox.c: math box simulation (Battlezone/Red Baron/Tempest)
 */

/*define MB_TEST*/

void mb_go(uint16_t addr, uint8_t data) {
  int32_t mb_temp; /* temp 32-bit multiply results */
  int16_t mb_q;    /* temp used in division */
  int32_t msb;

#ifdef MB_TEST
  fprintf(stderr, "math box command %02x data %02x  ", addr, data);
#endif

  switch (addr) {
    case 0x00: mb_result = REG0 = (REG0 & 0xff00) | data; break;
    case 0x01: mb_result = REG0 = (REG0 & 0x00ff) | (data << 8); break;
    case 0x02: mb_result = REG1 = (REG1 & 0xff00) | data; break;
    case 0x03: mb_result = REG1 = (REG1 & 0x00ff) | (data << 8); break;
    case 0x04: mb_result = REG2 = (REG2 & 0xff00) | data; break;
    case 0x05: mb_result = REG2 = (REG2 & 0x00ff) | (data << 8); break;
    case 0x06: mb_result = REG3 = (REG3 & 0xff00) | data; break;
    case 0x07: mb_result = REG3 = (REG3 & 0x00ff) | (data << 8); break;
    case 0x08: mb_result = REG4 = (REG4 & 0xff00) | data; break;
    case 0x09: mb_result = REG4 = (REG4 & 0x00ff) | (data << 8); break;

    case 0x0a:
      mb_result = REG5 = (REG5 & 0xff00) | data;
      break;
      /* note: no function loads low part of REG5 without performing a computation */

    case 0x0c:
      mb_result = REG6 = data;
      break;
      /* note: no function loads high part of REG6 */

    case 0x15: mb_result = REG7 = (REG7 & 0xff00) | data; break;
    case 0x16: mb_result = REG7 = (REG7 & 0x00ff) | (data << 8); break;

    case 0x1a: mb_result = REG8 = (REG8 & 0xff00) | data; break;
    case 0x1b: mb_result = REG8 = (REG8 & 0x00ff) | (data << 8); break;

    case 0x0d: mb_result = REGa = (REGa & 0xff00) | data; break;
    case 0x0e: mb_result = REGa = (REGa & 0x00ff) | (data << 8); break;
    case 0x0f: mb_result = REGb = (REGb & 0xff00) | data; break;
    case 0x10: mb_result = REGb = (REGb & 0x00ff) | (data << 8); break;

    case 0x17: mb_result = REG7; break;
    case 0x19: mb_result = REG8; break;
    case 0x18: mb_result = REG9; break;

    case 0x0b:

      REG5 = (REG5 & 0x00ff) | (data << 8);

      REGf = 0xffff;
      REG4 -= REG2;
      REG5 -= REG3;

step_048:

      mb_temp = ((int32_t)REG0) * ((int32_t)REG4);
      REGc = mb_temp >> 16;
      REGe = mb_temp & 0xffff;

      mb_temp = ((int32_t)-REG1) * ((int32_t)REG5);
      REG7 = mb_temp >> 16;
      mb_q = mb_temp & 0xffff;

      REG7 += REGc;

      /* rounding */
      REGe = (REGe >> 1) & 0x7fff;
      REGc = (mb_q >> 1) & 0x7fff;
      mb_q = REGc + REGe;
      if (mb_q < 0)
        REG7++;

      mb_result = REG7;

      if (REGf < 0)
        break;

      REG7 += REG2;

      /* fall into command 12 */

    case 0x12:

      mb_temp = ((int32_t)REG1) * ((int32_t)REG4);
      REGc = mb_temp >> 16;
      REG9 = mb_temp & 0xffff;

      mb_temp = ((int32_t)REG0) * ((int32_t)REG5);
      REG8 = mb_temp >> 16;
      mb_q = mb_temp & 0xffff;

      REG8 += REGc;

      /* rounding */
      REG9 = (REG9 >> 1) & 0x7fff;
      REGc = (mb_q >> 1) & 0x7fff;
      REG9 += REGc;
      if (REG9 < 0)
        REG8++;
      REG9 <<= 1; /* why? only to get the desired load address? */

      mb_result = REG8;

      if (REGf < 0)
        break;

      REG8 += REG3;

      REG9 &= 0xff00;

      /* fall into command 13 */

    case 0x13:
#ifdef MB_TEST
      fprintf(stderr, "\nR7: %04x  R8: %04x  R9: %04x\n", REG7, REG8, REG9);
#endif

      REGc = REG9;
      mb_q = REG8;
      goto step_0bf;

    case 0x14:
      REGc = REGa;
      mb_q = REGb;

step_0bf:
      REGe = REG7 ^ mb_q; /* save sign of result */
      REGd = mb_q;
      if (mb_q >= 0)
        mb_q = REGc;
      else {
        REGd = -mb_q - 1;
        mb_q = -REGc - 1;
        if ((mb_q < 0) && ((mb_q + 1) < 0))
          REGd++;
        mb_q++;
      }

      /* step 0c9: */
      /* REGc = abs (REG7) */
      if (REG7 >= 0)
        REGc = REG7;
      else
        REGc = -REG7;

      REGf = REG6; /* step counter */

      do {
        REGd -= REGc;
        msb = ((mb_q & 0x8000) != 0);
        mb_q <<= 1;
        if (REGd >= 0)
          mb_q++;
        else
          REGd += REGc;
        REGd <<= 1;
        REGd += msb;
      } while (--REGf >= 0);

      if (REGe >= 0)
        mb_result = mb_q;
      else
        mb_result = -mb_q;
      break;

    case 0x11:
      REG5 = (REG5 & 0x00ff) | (data << 8);
      REGf = 0x0000; /* do everything in one step */
      goto step_048;
      break;

    case 0x1c:
      /* window test? */
      REG5 = (REG5 & 0x00ff) | (data << 8);
      do {
        REGe = (REG4 + REG7) >> 1;
        REGf = (REG5 + REG8) >> 1;
        if ((REGb < REGe) && (REGf < REGe) && ((REGe + REGf) >= 0)) {
          REG7 = REGe;
          REG8 = REGf;
        } else {
          REG4 = REGe;
          REG5 = REGf;
        }
      } while (--REG6 >= 0);

      mb_result = REG8;
      break;

    case 0x1d:
      REG3 = (REG3 & 0x00ff) | (data << 8);

      REG2 -= REG0;
      if (REG2 < 0)
        REG2 = -REG2;

      REG3 -= REG1;
      if (REG3 < 0)
        REG3 = -REG3;

      /* fall into command 1e */

    case 0x1e:
      /* result = max (REG2, REG3) + 3/8 * min (REG2, REG3) */
      if (REG3 >= REG2) {
        REGc = REG2;
        REGd = REG3;
      } else {
        REGd = REG2;
        REGc = REG3;
      }
      REGc >>= 2;
      REGd += REGc;
      REGc >>= 1;
      mb_result = REGd = (REGc + REGd);
      break;

    case 0x1f:
        // Math self-test
        if (game == BATTLEZONE)
            // Battlezone expects specific signature
            mb_result = 0x1234;
        else if (game == TEMPEST) {
            static int test_count = 0;
            // First call returns 0, subsequent calls return 0x1234
            mb_result = (test_count++ > 0) ? 0x1234 : 0;
        }

      // fprintf(stderr, "math box function 0x1f\n");
      /* $$$ do some computation here (selftest? signature analysis? */
      break;
  }

#ifdef MB_TEST
  fprintf(stderr, "  result %04x\n", mb_result & 0xffff);
#endif
}
/*
 * memory.c: memory and I/O functions for Atari Vector game simulator
 */

void bell() {
  // nothing yet
}

extern uint32_t TickCount();

void MEMWR(uint16_t addr, uint8_t val, uint16_t PC, uint32_t cyc) {
  //void MEMWR(unsigned addr, int val, int PC, unsigned long cyc) {
  // register uint8_t tag;
  uint8_t tag;
  int32_t newbank;
  int32_t i;
  uint8_t temp;
  uint32_t currentMillis = 0;

   if (addr == VGO) {
    printf("MEMWR: VGO write detected! PC=%04X, Value=%02X, Cycle=%lu\n", PC, val, cyc);
  }
  /*
#ifdef VSTCM
  Serial.printf( "MEMWR: addr=%04X val=%02X PC=%04X cyc=%lu tagw=%02X\n",
                addr, val, PC, cyc, mem[addr].tagw );
  Serial.flush();  // Ensure log is sent
#else
  fprintf( trace_file, "MEMWR: addr=%04X val=%02X PC=%04X cyc=%lu tagw=%02X\n",
          addr, val, PC, cyc, mem[addr].tagw );
  fflush( trace_file );  // Ensure log is written to file immediately
#endif
*/
  /*
  if (addr == 0x2401) {
#ifdef VSTCM
     Serial.printf( "MEM WRITE: PC=%04X Addr=%04X Old=%02X New=%02X CYC=%d\n",
                   PC, addr, mem[addr].cell, val, cyc );
     Serial.flush();
#else
     fprintf( trace_file, "MEM WRITE: PC=%04X Addr=%04X Old=%02X New=%02X CYC=%d\n",
             PC, addr, mem[addr].cell, val, cyc );
      fflush( trace_file );  // Ensure log is written to file immediately
   */

  if (!(tag = mem[addr].tagw))
    mem[addr].cell = val;
  else {
    if (tag & BREAKTAG) {
      breakflag = 1;
      printf("@%04x Breakpoint write %04x, data %02x\n", PC, addr, val);
    }

    switch (tag & 0x3f) {
      case MEMORY:
        mem[addr].cell = val;
        break;
#if (MEMORY != VECRAM)
      case VECRAM:
        mem[addr].cell = val;
        break;
#endif
      case COINOUT:
        newbank = (val >> 2) & 1;
        if (newbank != bank)
          printf("Bank select %d\n", (int)newbank);
        bank = newbank;
        break;
      case INTACK:
        irq_cycle = cyc + 6144;
        break;
      case WDCLR:
      case EAROMCON:
          earom_set_control((val >> 3) & 1, 1, !(val & 4), val & 2);
          earom_set_clk(val & 1);
//#define BIT(x, n) (((x) >> (n)) & 1)
  //      earom_set_control(BIT(val, 3), 1, !BIT(val, 2), BIT(val, 1));
   //     earom_set_clk(BIT(val, 0));
        break;
      case EAROMWR:
        /* none of these are implemented yet, but they're OK. */
          earom_write(addr & 0x3f, val);
        break;
      case VGRST:
        vg_reset(cyc);
        break;
      case VGO:

        //  g_vctr_vg_count++;
        /*   while (1) {  // Wait 17ms, approx 60hz
          currentMillis = TickCount();
          if (currentMillis - g_millis_count > 17)
            break;
        }*/

        g_millis_count = currentMillis;  // reset millisecond counter
        vg_go(cyc);

        break;
      case DMACNT:
        printf("@%04x write to DMACNT!!!\n", PC);
        breakflag = 1;
        break;
      case POKEY1:
        pokey_write(0, addr & 0x0f, val, PC, cyc);
        break;
      case POKEY2:
        pokey_write(1, addr & 0x0f, val, PC, cyc);
        break;
      case POKEY3:
        pokey_write(2, addr & 0x0f, val, PC, cyc);
        break;
      case POKEY4:
        pokey_write(3, addr & 0x0f, val, PC, cyc);
        break;
      case MBSTART:
        /* printf("@%04x MBSTART wr addr %04x val %02x\n", PC, addr & 0x1f, val); */
        mb_go(addr & 0x1f, val);
        break;
      case COLORRAM:
        mem[addr].cell = val;
        break;
      case TEMP_OUTPUTS:
        break;
      case BZ_SOUND:

        /*
          BZ_SOUNDS[7]  motoren
          BZ_SOUNDS[6]  start led
          BZ_SOUNDS[5]  sound en
          BZ_SOUNDS[4]  engine H/L
          BZ_SOUNDS[3]  shell L/S
          BZ_SOUNDS[2]  shell enabl
          BZ_SOUNDS[1]  explo L/S
          BZ_SOUNDS[0]  explo en
        */
        /*  if (val & bit(5))
          g_aud_enable = 1;
        else
          g_aud_enable = 0;

        if (val & bit(0)) // expl
        */

       /*  if (val & 0x01) // Explosion sound
              queueSound(0x02);
          if (val & 0x04) // Shell sound
              queueSound(0x03);*/
     /*   if ((val & 0x04) && !((mem[addr].cell & 0x0c) == 0x04))
          bell();
        mem[addr].cell = val; */
        break;
      case RB_SND:
        mem[addr].cell = val;
        break;
      case RB_SND_RST:
        break;
      case LUNAR_MEM:
        mem[addr & 0xff].cell = val;
        break;
      case LUNAR_OUT:
        break;
      case LUNAR_SND:
      case LUNAR_SND_RST:
        break;
      case ASTEROIDS_OUT:
        newbank = (val >> 2) & 1;
        if (newbank != bank)
          printf("Bank select %d\n", (int)newbank);
        for (i = 0; i < 0x100; i++) {
          temp = mem[0x200 + i].cell;
          mem[0x200 + i].cell = mem[0x300 + i].cell;
          mem[0x300 + i].cell = temp;
        }
        bank = newbank;
        break;
      case ASTEROIDS_EXP:
#ifdef PT_8211
        queueSound(0x01);  // Send sound command to the queue
#endif
        break;
      case ASTEROIDS_THUMP:
#ifdef PT_8211
        queueSound(0x01);  // Send sound command to the queue
#endif
        break;
      case ASTEROIDS_SND:
#ifdef PT_8211
        queueSound(0x01);  // Send sound command to the queue
#endif
        break;
      case ASTEROIDS_SND_RST:
        break;
      case AST_DEL_OUT:
        switch (addr & 7) {
          case 0: /* player 1 start LED */
          case 1: /* player 2 start LED */
          case 2:
          case 3: /* thrust sound */
#ifdef PT_8211
            queueSound(0x01);  // Send sound command to the queue
#endif
            break;
          case 4: /* bank switching */
            newbank = (val >> 7) & 1;
            if (newbank != bank)
              printf("Bank select %d\n", (int)newbank);
            for (i = 0; i < 0x100; i++) {
              temp = mem[0x200 + i].cell;
              mem[0x200 + i].cell = mem[0x300 + i].cell;
              mem[0x300 + i].cell = temp;
            }
            bank = newbank;
            break;
          case 5: /* left coin counter */
          case 6: /* center coin counter */
          case 7: /* right coin counter */
            break;
        }
        break;
      case IGNWRT:
        break;
      case ROMWRT:
        printf("@%04x ROM write addr %04x val %02x data %02x tag %02x\n",
               PC, addr, val, mem[addr].cell, mem[addr].tagw);
        break;
      case UNKNOWN:
        printf("@%04x Unknown wr addr %04x val %02x data %02x tag %02x\n",
               PC, addr, val, mem[addr].cell, mem[addr].tagw);
        breakflag = 1;
        break;
      default:
        printf("@%04x Why are we here wr addr %04x val %02x data %02x tag %02x\n",
               PC, addr, val, mem[addr].cell, mem[addr].tagw);
        breakflag = 1;
        break;
    }
  }
}

/*
 * pokey.c: POKEY chip simulation functions
 */

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

const char *pokey_rreg_name[] = {
  "POT0", "POT1", "POT2", "POT3",
  "POT4", "POT5", "POT6", "POT7",
  "ALLPOT", "KBCODE", "RANDOM", "unused0xB",
  "unused0xC", "unused0xD", "IRQSTAT", "SKSTAT"
};

const char *pokey_wreg_name[] = {
  "AUDF1", "AUDC1", "AUDF2", "AUDC2",
  "AUDF3", "AUDC3", "AUDF4", "AUDC4",
  "AUDCTL", "STIMER", "SKRES", "POTGO",
  "unused0xC", "SEROUT", "IRQEN", "SKCTL"
};

uint8_t pokey_rreg[MAX_POKEY][MAX_REG];
uint8_t pokey_wreg[MAX_POKEY][MAX_REG];

#ifdef POKEY_DEBUG
uint8_t pokey_wreg_inited[MAX_POKEY][MAX_REG] = { { 0 } };
#endif

uint8_t pokey_read(int pokeynum, int reg, int PC, unsigned long cyc) {
  switch (reg) {
    case RANDOM:
      if ((pokey_wreg[pokeynum][SKCTL] & 0x03) != 0x00)
        pokey_rreg[pokeynum][RANDOM] = (rand() >> 12) & 0xff;
      return (pokey_rreg[pokeynum][RANDOM]);
    default:
#ifdef POKEY_DEBUG
      printf("pokey %d read reg %1x (%s)\n", pokeynum, reg, pokey_rreg_name[reg]);
#endif
      return (pokey_rreg[pokeynum][RANDOM]);
  }
}

void pokey_write(int pokeynum, int reg, uint8_t val, int PC, unsigned long cyc) {
#ifdef POKEY_DEBUG
  if (!pokey_wreg_inited[pokeynum][reg]) {
    pokey_wreg_inited[pokeynum][reg] = 1;
    pokey_wreg[pokeynum][reg] = val + 1; /* make sure we log it */
  }
  if (pokey_wreg[pokeynum][reg] != val) {
    printf("pokey %d reg %1x (%s) write data %02x\n", pokeynum, reg, pokey_wreg_name[reg], val);
  }
#endif
  pokey_wreg[pokeynum][reg] = val;
}

/*
 * sim6502.c: 6502 simulator for Atari Vector game simulator
 */

#ifdef COUNT_INTERRUPTS
uint32_t int_count = 0;
uint32_t int_quit = 0;
#endif

#if 0
#define dopush(val) memwr(0x100 + (SP--), (val))
#define dopop() memrd(0x100 + (SP = (++SP)))
/* #define dopop() memrd(0x100+(SP=(SP+1)&0xff)) */
#else
void dopush(uint8_t val, unsigned short PC) {
  uint16_t addr;
  addr = SP + 0x100;
  SP--;
  SP &= 0xff;
  memwr(addr, val, PC, 0);
}

uint8_t dopop(unsigned short PC) {
  uint16_t addr;
  SP++;
  SP &= 0xff;
  addr = SP + 0x100;
  return (memrd(addr, PC, 0));
}
#endif

#ifdef FIFO
struct _fifo fifo[0x10000];
unsigned short pcpos = 0;
#endif

#ifdef INST_COUNT
uint32_t icount = 0;
#endif

extern void SPI_flush();

// Logging function to track Z flag changes
void log_z_change(uint16_t PC, uint8_t new_flags) {
  /*
#ifdef VSTCM
  Serial.printf("Z Changed at PC=%04X: New FLAGS=%02X\n", PC, new_flags);
#else
  fprintf(trace_file, "Z Changed at PC=%04X: New FLAGS=%02X\n", PC, new_flags);
#endif
*/
}

uint8_t last_A = 0;  // Store last known value of A
int nextDraw = 30000;

void sim_6502(void) {
  uint16_t PC;
  uint8_t opcode;
  uint16_t addr;
  uint8_t A;
  uint8_t X;
  uint8_t Y;
  // uint32_t CC;
  DECLARE_CC;          // Not sure exactly why this is needed, but it is
  uint32_t totcycles;  // Changed from unsigned long to uint32_t

 // SP = 0xFE;  // This was missing, and caused SP to be 0 on PC, and FE on Teensy (trying to get them to execute identically)

#ifdef COUNT_INTERRUPTS
  int int_count = 0;
  int int_quit = 0;
  int irq_cycle = 0;
#endif

#ifdef CYCLE_COUNT_EXACT
  int32_t oldaddr;
#endif

  A = save_A;
  X = save_X;
  Y = save_Y;
  byte_to_flags(save_flags);
  PC = save_PC;
  totcycles = save_totcycles;

#ifndef VSTCM
  SDL_SetRenderDrawColor(rend_2D_orig, 0, 0, 0, 255);  // Black background
  SDL_RenderClear(rend_2D_orig);

  uint32_t loopcount = 0;
#endif

#ifndef VSTCM
  Uint32 last_time = SDL_GetTicks();  // Get the starting time
#endif

  while (1)
  {


     if (game == TEMPEST)
     {
        if (nextDraw < totcycles)
        {
           avg_draw_vector_list_t();
           nextDraw = totcycles + 30000;
        }
     }

  handle_input();  // Check for input from the user: maybe not necessary to call on each loop iteration
                   // this needs to be executed at the beginning in order to put a value in iSWx

  earom_update_state();

  if (totcycles > irq_cycle) {
    if (use_nmi) {
#ifdef MAGIC_PC
      if ((!self_test) && (mem[PC].magic))
#else
      if (!self_test)
#endif
      {
        /* do NMI */
#ifdef COUNT_INTERRUPTS
        int_count += 1;
        if ((int_quit) && (int_count >= int_quit))
          exit(0);
#endif
        dopush(PC >> 8, PC);
        dopush(PC & 0xff, PC);
        dopush(flags_to_byte, PC);
        SET_I;
        PC = memrdwd(0xfffa, PC, totcycles);
        totcycles += 7;
     //   irq_cycle += 6144;  // <<<<<< hkjr 03/30/14. NMI in an MMI occasionally with a # like this...
        // Reduce NMI frequency for Battlezone
        irq_cycle += (game == BATTLEZONE) ? 4096 : 6144;
        //   handle_input();
      }
    } else {
#ifdef MAGIC_PC
      if ((!TST_I) && (mem[PC].magic))
#else
      if (!TST_I)
#endif
      {
        /* do IRQ */
#ifdef COUNT_INTERRUPTS
        int_count++;
        if ((int_quit) && (int_count >= int_quit))
          exit(0);
#endif
        dopush( PC >> 8, PC );
        dopush( PC & 0xff, PC );
        dopush( flags_to_byte, PC );
        SET_I;
        PC = memrdwd( 0xfffe, PC, totcycles );
        totcycles += 7;
//		  irq_cycle = 0x7fffffff;
//		  handle_input ();
      }
    }
    irq_cycle += 6144;
  }

#ifdef WATCHDOG_HACK
  if (PC == 0xcbf9) /* Hack kludge  to emulate watchdog reset */
  {
    printf("WATCHDOG RESET lastpc %x tot instr %ld\n", PC, icount);
    ourexit(1);
  }
#endif
  /*
#ifdef VSTCM
  Serial.printf( "MEM INIT: Addr=2401 Value=%02X\n", mem[0x2401].cell );
  Serial.flush();
#else
  fprintf( trace_file, "MEM INIT: Addr=2401 Value=%02X\n", mem[0x2401].cell );
  fflush( trace_file );
#endif
*/
  


    /*    if (PC < 0x8000) { // Ignore ROM fetches
     static int count = 0;
     #ifdef VSTCM
      static char msg[50];
      
      sprintf( msg, "CPU Executing: PC=%04X, Opcode=%02X\n", PC, mem[PC].cell );
      Serial.print(msg);
     #else
        printf( "CPU Executing: PC=%04X, Opcode=%02X\n", PC, mem[PC].cell );
        #endif

      //  count++;
     //   if (count>20)
      //    return;
     }*/


#if 0
      if((icount & 0x1ffff) == 0) printf("%ld @%x 1%x\n", icount, PC, SP);
#endif

#ifdef FIFO
    fifo[pcpos].PC = PC;
    fifo[pcpos].A = A;
    fifo[pcpos].X = X;
    fifo[pcpos].Y = Y;
    fifo[pcpos].flags = flags_to_byte;
    fifo[pcpos++ & 0xffff].SP = SP;
#endif

#ifdef INST_COUNT
    icount++;
#endif

    /*  if (PC == 0x7985)   // For Battlezone
      start_sample(A);  // pokey audio
    else if (PC == 0x6a22)
      enable_sound(SMART);  // smart missile (pokey ch3&4)
*/

    //#if 1
  //  opcode = mem[PC++].cell;
    //#else
    //   opcode = memrd(PC, PC, totcycles);
    //   PC++;
    //#endif

    opcode = memrd( PC, PC, totcycles ); PC++;

    /*
#ifdef VSTCM
    Serial.printf("PC=%04X OP=%02X A=%02X X=%02X Y=%02X SP=%02X FLAGS=%02X CYC=%d\n",
                  PC - 1, opcode, A, X, Y, SP, flags_to_byte, totcycles);
    Serial.flush();  // Ensure data is sent before next instruction
#else
    fprintf(trace_file, "PC=%04X OP=%02X A=%02X X=%02X Y=%02X SP=%02X FLAGS=%02X CYC=%d\n",
            PC - 1, opcode, A, X, Y, SP, flags_to_byte, totcycles);
#endif
*/

    //sound_update(p);

   // last_A = A;  // Store previous value of A


    switch (opcode) 			/* execute opcode */
    {
    case 0x69:  /* ADC */  EA_IMM;    DO_ADC;   C( 2 );  break;
    case 0x65:  /* ADC */  EA_ZP;     DO_ADC;   C( 3 );  break;
    case 0x75:  /* ADC */  EA_ZP_X;   DO_ADC;   C( 4 );  break;
    case 0x6d:  /* ADC */  EA_ABS;    DO_ADC;   C( 4 );  break;
    case 0x7d:  /* ADC */  EA_ABS_X_C;  DO_ADC;   C( 4 );  break;
    case 0x79:  /* ADC */  EA_ABS_Y_C;  DO_ADC;   C( 4 );  break;
    case 0x61:  /* ADC */  EA_IND_X;  DO_ADC;   C( 6 );  break;
    case 0x71:  /* ADC */  EA_IND_Y_C;  DO_ADC;   C( 5 );  break;

    case 0x29:  /* AND */  EA_IMM;    DO_AND;   C( 2 );  break;
    case 0x25:  /* AND */  EA_ZP;     DO_AND;   C( 3 );  break;
    case 0x35:  /* AND */  EA_ZP_X;   DO_AND;   C( 4 );  break;
    case 0x2d:  /* AND */  EA_ABS;    DO_AND;   C( 4 );  break;
    case 0x3d:  /* AND */  EA_ABS_X_C;  DO_AND;   C( 4 );  break;
    case 0x39:  /* AND */  EA_ABS_Y_C;  DO_AND;   C( 4 );  break;
    case 0x21:  /* AND */  EA_IND_X;  DO_AND;   C( 6 );  break;
    case 0x31:  /* AND */  EA_IND_Y_C;  DO_AND;   C( 5 );  break;

    case 0x0a:  /* ASL */            DO_ASLA;  C( 2 );  break;
    case 0x06:  /* ASL */  EA_ZP;     DO_ASL;   C( 5 );  break;
    case 0x16:  /* ASL */  EA_ZP_X;   DO_ASL;   C( 6 );  break;
    case 0x0e:  /* ASL */  EA_ABS;    DO_ASL;   C( 6 );  break;
    case 0x1e:  /* ASL */  EA_ABS_X;  DO_ASL;   C( 7 );  break;

    case 0x90:  /* BCC */		 DO_BCC;   C( 2 );  continue;
    case 0xb0:  /* BCS */		 DO_BCS;   C( 2 );  continue;
    case 0xf0:  /* BEQ */		 DO_BEQ;   C( 2 );  continue;
    case 0x30:  /* BMI */		 DO_BMI;   C( 2 );  continue;
    case 0xd0:  /* BNE */		 DO_BNE;   C( 2 );  continue;
    case 0x10:  /* BPL */		 DO_BPL;   C( 2 );  continue;
    case 0x50:  /* BVC */		 DO_BVC;   C( 2 );  continue;
    case 0x70:  /* BVS */		 DO_BVS;   C( 2 );  continue;

    case 0x24:  /* BIT */  EA_ZP;     DO_BIT;   C( 3 );  break;
    case 0x2c:  /* BIT */  EA_ABS;    DO_BIT;   C( 4 );  break;

#if 0
    case 0x00:  /* BRK */            DO_BRK;   C( 7 );  break;
#endif

    case 0x18:  /* CLC */            DO_CLC;   C( 2 );  break;
    case 0xd8:  /* CLD */            DO_CLD;   C( 2 );  break;
    case 0x58:  /* CLI */            DO_CLI;   C( 2 );  break;
    case 0xb8:  /* CLV */            DO_CLV;   C( 2 );  break;

    case 0xc9:  /* CMP */  EA_IMM;    DO_CMP;   C( 2 );  break;
    case 0xc5:  /* CMP */  EA_ZP;     DO_CMP;   C( 3 );  break;
    case 0xd5:  /* CMP */  EA_ZP_X;   DO_CMP;   C( 4 );  break;
    case 0xcd:  /* CMP */  EA_ABS;    DO_CMP;   C( 4 );  break;
    case 0xdd:  /* CMP */  EA_ABS_X_C;  DO_CMP;   C( 4 );  break;
    case 0xd9:  /* CMP */  EA_ABS_Y_C;  DO_CMP;   C( 4 );  break;
    case 0xc1:  /* CMP */  EA_IND_X;  DO_CMP;   C( 6 );  break;
    case 0xd1:  /* CMP */  EA_IND_Y_C;  DO_CMP;   C( 5 );  break;

    case 0xe0:  /* CPX */  EA_IMM;    DO_CPX;   C( 2 );  break;
    case 0xe4:  /* CPX */  EA_ZP;     DO_CPX;   C( 3 );  break;
    case 0xec:  /* CPX */  EA_ABS;    DO_CPX;   C( 4 );  break;

    case 0xc0:  /* CPY */  EA_IMM;    DO_CPY;   C( 2 );  break;
    case 0xc4:  /* CPY */  EA_ZP;     DO_CPY;   C( 3 );  break;
    case 0xcc:  /* CPY */  EA_ABS;    DO_CPY;   C( 4 );  break;

    case 0xc6:  /* DEC */  EA_ZP;     DO_DEC;   C( 5 );  break;
    case 0xd6:  /* DEC */  EA_ZP_X;   DO_DEC;   C( 6 );  break;
    case 0xce:  /* DEC */  EA_ABS;    DO_DEC;   C( 6 );  break;
    case 0xde:  /* DEC */  EA_ABS_X;  DO_DEC;   C( 7 );  break;

    case 0xca:  /* DEX */            DO_DEX;   C( 2 );  break;
    case 0x88:  /* DEY */            DO_DEY;   C( 2 );  break;

    case 0x49:  /* EOR */  EA_IMM;    DO_EOR;   C( 2 );  break;
    case 0x45:  /* EOR */  EA_ZP;     DO_EOR;   C( 3 );  break;
    case 0x55:  /* EOR */  EA_ZP_X;   DO_EOR;   C( 4 );  break;
    case 0x4d:  /* EOR */  EA_ABS;    DO_EOR;   C( 4 );  break;
    case 0x5d:  /* EOR */  EA_ABS_X_C;  DO_EOR;   C( 4 );  break;
    case 0x59:  /* EOR */  EA_ABS_Y_C;  DO_EOR;   C( 4 );  break;
    case 0x41:  /* EOR */  EA_IND_X;  DO_EOR;   C( 6 );  break;
    case 0x51:  /* EOR */  EA_IND_Y_C;  DO_EOR;   C( 5 );  break;

    case 0xe6:  /* INC */  EA_ZP;     DO_INC;   C( 5 );  break;
    case 0xf6:  /* INC */  EA_ZP_X;   DO_INC;   C( 6 );  break;
    case 0xee:  /* INC */  EA_ABS;    DO_INC;   C( 6 );  break;
    case 0xfe:  /* INC */  EA_ABS_X;  DO_INC;   C( 7 );  break;

    case 0xe8:  /* INX */            DO_INX;   C( 2 );  break;
    case 0xc8:  /* INY */            DO_INY;   C( 2 );  break;

    case 0x4c:  /* JMP */  EA_ABS;    DO_JMP;   C( 3 );  continue;
    case 0x6c:  /* JMP */  EA_IND;    DO_JMP;   C( 5 );  continue;

    case 0x20:  /* JSR */  EA_ABS;    DO_JSR;   C( 6 );  continue;

    case 0xa9:  /* LDA */  EA_IMM;    DO_LDA;   C( 2 );  break;
    case 0xa5:  /* LDA */  EA_ZP;     DO_LDA;   C( 3 );  break;
    case 0xb5:  /* LDA */  EA_ZP_X;   DO_LDA;   C( 4 );  break;
    case 0xad:  /* LDA */  EA_ABS;    DO_LDA;   C( 4 );  break;
    case 0xbd:  /* LDA */  EA_ABS_X_C;  DO_LDA;   C( 4 );  break;
    case 0xb9:  /* LDA */  EA_ABS_Y_C;  DO_LDA;   C( 4 );  break;
    case 0xa1:  /* LDA */  EA_IND_X;  DO_LDA;   C( 6 );  break;
    case 0xb1:  /* LDA */  EA_IND_Y_C;  DO_LDA;   C( 5 );  break;

    case 0xa2:  /* LDX */  EA_IMM;    DO_LDX;   C( 2 );  break;
    case 0xa6:  /* LDX */  EA_ZP;     DO_LDX;   C( 3 );  break;
    case 0xb6:  /* LDX */  EA_ZP_Y;   DO_LDX;   C( 4 );  break;
    case 0xae:  /* LDX */  EA_ABS;    DO_LDX;   C( 4 );  break;
    case 0xbe:  /* LDX */  EA_ABS_Y_C;  DO_LDX;   C( 4 );  break;

    case 0xa0:  /* LDY */  EA_IMM;    DO_LDY;   C( 2 );  break;
    case 0xa4:  /* LDY */  EA_ZP;     DO_LDY;   C( 3 );  break;
    case 0xb4:  /* LDY */  EA_ZP_X;   DO_LDY;   C( 4 );  break;
    case 0xac:  /* LDY */  EA_ABS;    DO_LDY;   C( 4 );  break;
    case 0xbc:  /* LDY */  EA_ABS_X_C;  DO_LDY;   C( 4 );  break;

    case 0x4a:  /* LSR */            DO_LSRA;  C( 2 );  break;
    case 0x46:  /* LSR */  EA_ZP;     DO_LSR;   C( 5 );  break;
    case 0x56:  /* LSR */  EA_ZP_X;   DO_LSR;   C( 6 );  break;
    case 0x4e:  /* LSR */  EA_ABS;    DO_LSR;   C( 6 );  break;
    case 0x5e:  /* LSR */  EA_ABS_X;  DO_LSR;   C( 7 );  break;

    case 0xea:  /* NOP */                     C( 2 );  break;

    case 0x09:  /* ORA */  EA_IMM;    DO_ORA;   C( 2 );  break;
    case 0x05:  /* ORA */  EA_ZP;     DO_ORA;   C( 3 );  break;
    case 0x15:  /* ORA */  EA_ZP_X;   DO_ORA;   C( 4 );  break;
    case 0x0d:  /* ORA */  EA_ABS;    DO_ORA;   C( 4 );  break;
    case 0x1d:  /* ORA */  EA_ABS_X_C;  DO_ORA;   C( 4 );  break;
    case 0x19:  /* ORA */  EA_ABS_Y_C;  DO_ORA;   C( 4 );  break;
    case 0x01:  /* ORA */  EA_IND_X;  DO_ORA;   C( 6 );  break;
    case 0x11:  /* ORA */  EA_IND_Y_C;  DO_ORA;   C( 5 );  break;

    case 0x48:  /* PHA */            DO_PHA;   C( 3 );  break;
    case 0x08:  /* PHP */            DO_PHP;   C( 3 );  break;
    case 0x68:  /* PLA */            DO_PLA;   C( 4 );  break;
    case 0x28:  /* PLP */            DO_PLP;   C( 4 );  break;

    case 0x2a:  /* ROL */            DO_ROLA;  C( 2 );  break;
    case 0x26:  /* ROL */  EA_ZP;     DO_ROL;   C( 5 );  break;
    case 0x36:  /* ROL */  EA_ZP_X;   DO_ROL;   C( 6 );  break;
    case 0x2e:  /* ROL */  EA_ABS;    DO_ROL;   C( 6 );  break;
    case 0x3e:  /* ROL */  EA_ABS_X;  DO_ROL;   C( 7 );  break;

    case 0x6a:  /* ROR */            DO_RORA;  C( 2 );  break;
    case 0x66:  /* ROR */  EA_ZP;     DO_ROR;   C( 5 );  break;
    case 0x76:  /* ROR */  EA_ZP_X;   DO_ROR;   C( 6 );  break;
    case 0x6e:  /* ROR */  EA_ABS;    DO_ROR;   C( 6 );  break;
    case 0x7e:  /* ROR */  EA_ABS_X;  DO_ROR;   C( 7 );  break;

    case 0x40:  /* RTI */            DO_RTI;   C( 6 );  continue;
    case 0x60:  /* RTS */            DO_RTS;   C( 6 );  continue;

    case 0xe9:  /* SBC */  EA_IMM;    DO_SBC;   C( 2 );  break;
    case 0xe5:  /* SBC */  EA_ZP;     DO_SBC;   C( 3 );  break;
    case 0xf5:  /* SBC */  EA_ZP_X;   DO_SBC;   C( 4 );  break;
    case 0xed:  /* SBC */  EA_ABS;    DO_SBC;   C( 4 );  break;
    case 0xfd:  /* SBC */  EA_ABS_X_C;  DO_SBC;   C( 4 );  break;
    case 0xf9:  /* SBC */  EA_ABS_Y_C;  DO_SBC;   C( 4 );  break;
    case 0xe1:  /* SBC */  EA_IND_X;  DO_SBC;   C( 6 );  break;
    case 0xf1:  /* SBC */  EA_IND_Y_C;  DO_SBC;   C( 5 );  break;

    case 0x38:  /* SEC */            DO_SEC;   C( 2 );  break;
    case 0xf8:  /* SED */            DO_SED;   C( 2 );  break;
    case 0x78:  /* SEI */            DO_SEI;   C( 2 );  break;

    case 0x85:  /* STA */  EA_ZP;     DO_STA;   C( 3 );  break;
    case 0x95:  /* STA */  EA_ZP_X;   DO_STA;   C( 4 );  break;
    case 0x8d:  /* STA */  EA_ABS;    DO_STA;   C( 4 );  break;
    case 0x9d:  /* STA */  EA_ABS_X;  DO_STA;   C( 5 );  break;
    case 0x99:  /* STA */  EA_ABS_Y;  DO_STA;   C( 5 );  break;
    case 0x81:  /* STA */  EA_IND_X;  DO_STA;   C( 6 );  break;
    case 0x91:  /* STA */  EA_IND_Y;  DO_STA;   C( 6 );  break;

    case 0x86:  /* STX */  EA_ZP;     DO_STX;   C( 3 );  break;
    case 0x96:  /* STX */  EA_ZP_Y;   DO_STX;   C( 4 );  break;
    case 0x8e:  /* STX */  EA_ABS;    DO_STX;   C( 4 );  break;

    case 0x84:  /* STY */  EA_ZP;     DO_STY;   C( 3 );  break;
    case 0x94:  /* STY */  EA_ZP_X;   DO_STY;   C( 4 );  break;
    case 0x8c:  /* STY */  EA_ABS;    DO_STY;   C( 4 );  break;

    case 0xaa:  /* TAX */            DO_TAX;   C( 2 );  break;
    case 0xa8:  /* TAY */            DO_TAY;   C( 2 );  break;
    case 0x98:  /* TYA */            DO_TYA;   C( 2 );  break;
    case 0xba:  /* TSX */            DO_TSX;   C( 2 );  break;
    case 0x8a:  /* TXA */            DO_TXA;   C( 2 );  break;
    case 0x9a:  /* TXS */            DO_TXS;   C( 2 );  break;

    default:
       printf( "@%x Illegal opcode %2x\r\n", PC, opcode );
       breakflag = 1;
       break;
    }  // end switch opcode

#ifdef VSTCM
    // Only reset the position if a vector operation was executed.
    if (opcode == 0x8D || opcode == 0x99 || opcode == 0x9D) {  // Example: Vector-related opcodes
      goto_xy(REST_X, REST_Y);
      SPI_flush();
    }
#else
    loopcount++;

    if (loopcount > 10000) {
      // Update the screen with the contents of the buffer
      SDL_RenderPresent(rend_2D_orig);
      SDL_SetRenderDrawColor(rend_2D_orig, 0, 0, 0, 255);  // Black background
      SDL_RenderClear(rend_2D_orig);                       // Clear the buffer only after we've displayed it
      loopcount = 0;

      // Cap the frame rate to ~60Hz (roughly 16ms/frame)
      Uint32 now = SDL_GetTicks();
      Uint32 elapsed = now - last_time;

      if (elapsed < 16) {
         SDL_Delay( 16 - elapsed );
      }

      last_time = SDL_GetTicks();  // reset for next frame

    }
#endif
	 // Throttle the CPU to avoid running too fast
#ifndef VSTCM

    
#else
// Teensy version (VSTCM)
	 // Throttle the CPU to avoid running too fast
    static const uint32_t CYCLES_PER_SLICE = 10000;
    static const uint32_t NANOS_PER_CYCLE = 667;  // Roughly 1.5 MHz: 1 / 1.5e6 = ~667 ns

    static uint32_t last_totcycles = 0;
    if (totcycles - last_totcycles >= CYCLES_PER_SLICE) {
       uint32_t delay_nanos = (totcycles - last_totcycles) * NANOS_PER_CYCLE;
       delayNanoseconds( delay_nanos );  // Teensy-specific delay
       last_totcycles = totcycles;
    }
#endif




    // Detect left and right keys pressed simultaneously to end game and return to menu
#ifdef VSTCM
    // Update the button objects
    button0.update();

    if (button0.fell()) {
      // Quit the game if down button on PCB is pressed
      break;
    }
#else
    // Quit the game if escape key is pressed in Windows
    SDL_Event e;

    if (SDL_PollEvent(&e) != 0) {
      if (e.key.keysym.scancode == SDL_SCANCODE_ESCAPE)
        break;  // Return to the main menu
    }
#endif

    // Break loop if needed
#ifdef VSTCM
    if (Serial.available() && Serial.read() == 'q') break;  // Quit if 'q' received
#else
    if (feof(trace_file)) break;
#endif
  }  // end while(1)
  //dobreak(INTBREAK);
  // Close log file on PC
#ifndef VSTCM
  fclose(trace_file);
#endif
}

