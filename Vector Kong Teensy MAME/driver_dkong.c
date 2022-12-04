/***************************************************************************

Donkey Kong and Donkey Kong Jr. memory map (preliminary) (DKong 3 follows)

0000-3fff ROM (Donkey Kong Jr.and Donkey Kong 3: 0000-5fff)
6000-6fff RAM
6900-6a7f sprites
7000-73ff ?
7400-77ff Video RAM
8000-9fff ROM (DK3 only)

memory mapped ports:

read:
7c00      IN0
7c80      IN1
7d00      IN2 (DK3: DSW2)
7d80      DSW1

*
 * IN0 (bits NOT inverted)
 * bit 7 : ?
 * bit 6 : reset (when player 1 active)
 * bit 5 : ?
 * bit 4 : JUMP player 1
 * bit 3 : DOWN player 1
 * bit 2 : UP player 1
 * bit 1 : LEFT player 1
 * bit 0 : RIGHT player 1
 *
*
 * IN1 (bits NOT inverted)
 * bit 7 : ?
 * bit 6 : reset (when player 2 active)
 * bit 5 : ?
 * bit 4 : JUMP player 2
 * bit 3 : DOWN player 2
 * bit 2 : UP player 2
 * bit 1 : LEFT player 2
 * bit 0 : RIGHT player 2
 *
*
 * IN2 (bits NOT inverted)
 * bit 7 : COIN (IS inverted in Radarscope)
 * bit 6 : ? Radarscope does some wizardry with this bit
 * bit 5 : ?
 * bit 4 : ?
 * bit 3 : START 2
 * bit 2 : START 1
 * bit 1 : ?
 * bit 0 : ? if this is 1, the code jumps to $4000, outside the rom space
 *
*
 * DSW1 (bits NOT inverted)
 * bit 7 : COCKTAIL or UPRIGHT cabinet (1 = UPRIGHT)
 * bit 6 : \ 000 = 1 coin 1 play   001 = 2 coins 1 play  010 = 1 coin 2 plays
 * bit 5 : | 011 = 3 coins 1 play  100 = 1 coin 3 plays  101 = 4 coins 1 play
 * bit 4 : / 110 = 1 coin 4 plays  111 = 5 coins 1 play
 * bit 3 : \bonus at
 * bit 2 : / 00 = 7000  01 = 10000  10 = 15000  11 = 20000
 * bit 1 : \ 00 = 3 lives  01 = 4 lives
 * bit 0 : / 10 = 5 lives  11 = 6 lives
 *

write:
7800-7803 ?
7808      ?
7c00      Background sound/music select:
          00 - nothing
		  01 - Intro tune
		  02 - How High? (intermisson) tune
		  03 - Out of time
		  04 - Hammer
		  05 - Rivet level 2 completed (end tune)
		  06 - Hammer hit
		  07 - Standard level end
		  08 - Background 1	(first screen)
		  09 - ???
		  0A - Background 3	(springs)
		  0B - Background 2 (rivet)
		  0C - Rivet level 1 completed (end tune)
		  0D - Rivet removed
		  0E - Rivet level completed
		  0F - Gorilla roar
7c80      gfx bank select (Donkey Kong Jr. only)
7d00      digital sound trigger - walk
7d01      digital sound trigger - jump
7d02      digital sound trigger - boom (gorilla stomps foot)
7d03      digital sound trigger - coin input/spring
7d04      digital sound trigger	- gorilla fall
7d05      digital sound trigger - barrel jump/prize
7d06      ?
7d07      ?
7d80      digital sound trigger - dead
7d82      ?
7d83      ?
7d84      interrupt enable
7d85      0/1 toggle
7d86-7d87 palette bank selector (only bit 0 is significant: 7d86 = bit 0 7d87 = bit 1)


8035 Memory Map:

0000-07ff ROM
0800-0fff Compressed sound sample (Gorilla roar in DKong)

Read ports:
0x20   Read current tune
P2.5   Active low when jumping
T0     Select sound for jump (Normal or Barrell?)
T1     Active low when gorilla is falling

Write ports:
P1     Digital out
P2.7   External decay
P2.6   Select second ROM reading (MOVX instruction will access area 800-fff)
P2.2-0 Select the bank of 256 bytes for second ROM



Donkey Kong 3 memory map (preliminary):

RAM and read ports same as above;

write:
7d00      ?
7d80      ?
7e00      ?
7e80
7e81      char bank selector
7e82-7e83 ?
7e84      interrupt enable
7e85      ?
7e86-7e87 palette bank selector (only bit 0 is significant: 7e86 = bit 0 7e87 = bit 1)


I/O ports

write:
00        ?

Changes:
	Apr 7 98 Howie Cohen
	* Added samples for the climb, jump, land and walking sounds

***************************************************************************/

#include "driver.h"
#include "vidhrdw/generic.h"
#include "I8039.h"

static int page = 0;
static int p[8] = { 255, 255, 255, 255, 255, 255, 255, 255 };
static int t[2] = { 1, 1 };


void dkongjr_gfxbank_w(int offset, int data);
void dkong3_gfxbank_w(int offset, int data);
void dkong_palettebank_w(int offset, int data);
void dkong_vh_convert_color_prom(unsigned char *palette, unsigned short *colortable, const unsigned char *color_prom);
void dkong3_vh_convert_color_prom(unsigned char *palette, unsigned short *colortable, const unsigned char *color_prom);
int dkong_vh_start(void);
void dkong_vh_screenrefresh(struct osd_bitmap *bitmap, int full_refresh);
void dkong3_vh_screenrefresh(struct osd_bitmap *bitmap, int full_refresh);

void dkong_sh_w(int offset, int data);
void dkongjr_sh_death_w(int offset, int data);
void dkongjr_sh_drop_w(int offset, int data);
void dkongjr_sh_roar_w(int offset, int data);
void dkongjr_sh_jump_w(int offset, int data);
void dkongjr_sh_walk_w(int offset, int data);
void dkongjr_sh_climb_w(int offset, int data);
void dkongjr_sh_land_w(int offset, int data);

void dkong_sh1_w(int offset, int data);

#define ACTIVELOW_PORT_BIT(P, A, D) ((P & (~(1 << A))) | ((D ^ 1) << A))


void dkong_sh_spring(int offset, int data) {
  p[2] = ACTIVELOW_PORT_BIT(p[2], 5, data);
}
void dkong_sh_gorilla(int offset, int data) {
  t[1] = data ^ 1;
}
void dkong_sh_barrell(int offset, int data) {
  t[0] = data ^ 1;
}
void dkong_sh_tuneselect(int offset, int data) {
  soundlatch_w(offset, data ^ 0x0f);
}

void dkongjr_sh_test6(int offset, int data) {
  p[2] = ACTIVELOW_PORT_BIT(p[2], 6, data);
}
void dkongjr_sh_test5(int offset, int data) {
  p[2] = ACTIVELOW_PORT_BIT(p[2], 5, data);
}
void dkongjr_sh_test4(int offset, int data) {
  p[2] = ACTIVELOW_PORT_BIT(p[2], 4, data);
}
void dkongjr_sh_tuneselect(int offset, int data) {
  soundlatch_w(offset, data);
}

static int dkong_sh_getp1(int offset) {
  return p[1];
}

static int dkong_sh_getp2(int offset) {
  return p[2];
}

static int dkong_sh_gett0(int offset) {
  return t[0];
}

static int dkong_sh_gett1(int offset) {
  return t[1];
}

static int dkong_sh_gettune(int offset) {
  unsigned char *SND = Machine->memory_region[Machine->drv->cpu[1].memory_region];
  if (page & 0x40) {
    switch (offset) {
      case 0x20: return soundlatch_r(0);
    }
  }
  return (SND[2048 + (page & 7) * 256 + offset]);
}

#include <math.h>

static double envelope, tt;
static int decay;

#define TSTEP 0.001

static void dkong_sh_putp1(int offset, int data) {
  envelope = exp(-tt);
  DAC_data_w(0, (int)(data * envelope));
  if (decay) tt += TSTEP;
  else tt = 0;
}

static void dkong_sh_putp2(int offset, int data) {
  /*   If P2.Bit7 -> is apparently an external signal decay or other output control
	 *   If P2.Bit6 -> activates the external compressed sample ROM
	 *   P2.Bit2-0  -> select the 256 byte bank for external ROM
	 */

  decay = !(data & 0x80);
  page = (data & 0x47);
}



static struct MemoryReadAddress readmem[] = {
  { 0x0000, 0x5fff, MRA_ROM },        /* DK: 0000-3fff */
  { 0x6000, 0x6fff, MRA_RAM },        /* including sprites RAM */
  { 0x7400, 0x77ff, MRA_RAM },        /* video RAM */
  { 0x7c00, 0x7c00, input_port_0_r }, /* IN0 */
  { 0x7c80, 0x7c80, input_port_1_r }, /* IN1 */
  { 0x7d00, 0x7d00, input_port_2_r }, /* IN2/DSW2 */
  { 0x7d80, 0x7d80, input_port_3_r }, /* DSW1 */
  { 0x8000, 0x9fff, MRA_ROM },        /* DK3 and bootleg DKjr only */
  { -1 }                              /* end of table */
};

static struct MemoryWriteAddress dkong_writemem[] = {
  { 0x0000, 0x5fff, MWA_ROM },
  { 0x6000, 0x68ff, MWA_RAM },
  { 0x6900, 0x6a7f, MWA_RAM, &spriteram, &spriteram_size },
  { 0x6a80, 0x6fff, MWA_RAM },
  { 0x7000, 0x73ff, MWA_RAM }, /* ???? */
  { 0x7400, 0x77ff, videoram_w, &videoram, &videoram_size },
  { 0x7800, 0x7803, MWA_RAM }, /* ???? */
  { 0x7808, 0x7808, MWA_RAM }, /* ???? */
  { 0x7c80, 0x7c80, dkongjr_gfxbank_w },
  { 0x7c00, 0x7c00, dkong_sh_tuneselect },
  { 0x7d00, 0x7d02, dkong_sh1_w }, /* walk/jump/boom sample trigger */
  { 0x7d03, 0x7d03, dkong_sh_spring },
  { 0x7d04, 0x7d04, dkong_sh_gorilla },
  { 0x7d05, 0x7d05, dkong_sh_barrell },
  { 0x7d80, 0x7d80, dkong_sh_w },
  { 0x7d81, 0x7d83, MWA_RAM }, /* ???? */
  { 0x7d84, 0x7d84, interrupt_enable_w },
  { 0x7d85, 0x7d85, MWA_RAM },
  { 0x7d86, 0x7d87, dkong_palettebank_w },
  { -1 } /* end of table */
};
static struct MemoryReadAddress readmem_sound[] = {
  { 0x0000, 0x0fff, MRA_ROM },
  { -1 } /* end of table */
};
static struct MemoryWriteAddress writemem_sound[] = {
  { 0x0000, 0x0fff, MWA_ROM },
  { -1 } /* end of table */
};
static struct IOReadPort readport_sound[] = {
  { 0x00, 0xff, dkong_sh_gettune },
  { I8039_p1, I8039_p1, dkong_sh_getp1 },
  { I8039_p2, I8039_p2, dkong_sh_getp2 },
  { I8039_t0, I8039_t0, dkong_sh_gett0 },
  { I8039_t1, I8039_t1, dkong_sh_gett1 },
  { -1 } /* end of table */
};
static struct IOWritePort writeport_sound[] = {
  { I8039_p1, I8039_p1, dkong_sh_putp1 },
  { I8039_p2, I8039_p2, dkong_sh_putp2 },
  { -1 } /* end of table */
};


static struct MemoryWriteAddress dkongjr_writemem[] = {
  { 0x0000, 0x5fff, MWA_ROM },
  { 0x6000, 0x68ff, MWA_RAM },
  { 0x6900, 0x6a7f, MWA_RAM, &spriteram, &spriteram_size },
  { 0x6a80, 0x6fff, MWA_RAM },
  { 0x7400, 0x77ff, videoram_w, &videoram, &videoram_size },
  { 0x7800, 0x7803, MWA_RAM }, /* ???? */
  { 0x7808, 0x7808, MWA_RAM }, /* ???? */
  { 0x7c00, 0x7c00, dkongjr_sh_tuneselect },
  { 0x7c80, 0x7c80, dkongjr_gfxbank_w },
  { 0x7c81, 0x7c81, dkongjr_sh_test6 },
  { 0x7d00, 0x7d00, dkongjr_sh_climb_w }, /* HC - climb sound */
  { 0x7d01, 0x7d01, dkongjr_sh_jump_w },  /* HC - jump */
  { 0x7d02, 0x7d02, dkongjr_sh_land_w },  /* HC - climb sound */
  { 0x7d03, 0x7d03, dkongjr_sh_roar_w },
  //	{ 0x7d03, 0x7d03, dkongjr_sh_test5 },
  { 0x7d04, 0x7d04, dkong_sh_gorilla },
  { 0x7d05, 0x7d05, dkong_sh_barrell },
  { 0x7d06, 0x7d06, dkongjr_sh_walk_w }, /* HC - walk sound */
  { 0x7d07, 0x7d07, dkongjr_sh_walk_w },
  //	{ 0x7d06, 0x7d06, dkongjr_sh_test4 },
  { 0x7d80, 0x7d80, dkongjr_sh_death_w },
  { 0x7d81, 0x7d81, dkongjr_sh_drop_w }, /* active when Junior is falling */
  { 0x7d84, 0x7d84, interrupt_enable_w },
  { 0x7d86, 0x7d87, dkong_palettebank_w },
  { 0x8000, 0x9fff, MWA_ROM }, /* bootleg DKjr only */
  { -1 }                       /* end of table */
};

void dkong3_dac_w(int offset, int data) {
  DAC_data_w(0, data);
}

void dkong3_2a03_reset_w(int offset, int data) {
  if ((data & 1) == 0) {
    /* suspend execution */
    cpu_halt(1, 0);
    cpu_halt(2, 0);
  } else {
    /* reset and resume execution */
    cpu_reset(1);
    cpu_halt(1, 1);
    cpu_reset(2);
    cpu_halt(2, 1);
  }
}

static struct MemoryWriteAddress dkong3_writemem[] = {
  { 0x0000, 0x5fff, MWA_ROM },
  { 0x6000, 0x68ff, MWA_RAM },
  { 0x6900, 0x6a7f, MWA_RAM, &spriteram, &spriteram_size },
  { 0x6a80, 0x6fff, MWA_RAM },
  { 0x7400, 0x77ff, videoram_w, &videoram, &videoram_size },
  { 0x7c00, 0x7c00, soundlatch_w },
  { 0x7c80, 0x7c80, soundlatch2_w },
  { 0x7d00, 0x7d00, soundlatch3_w },
  { 0x7d80, 0x7d80, dkong3_2a03_reset_w },
  { 0x7e81, 0x7e81, dkong3_gfxbank_w },
  { 0x7e84, 0x7e84, interrupt_enable_w },
  { 0x7e85, 0x7e85, MWA_NOP }, /* ??? */
  { 0x7e86, 0x7e87, dkong_palettebank_w },
  { 0x8000, 0x9fff, MWA_ROM },
  { -1 } /* end of table */
};

static struct IOWritePort dkong3_writeport[] = {
  { 0x00, 0x00, IOWP_NOP }, /* ??? */
  { -1 }                    /* end of table */
};

static struct MemoryReadAddress dkong3_sound1_readmem[] = {
  { 0x0000, 0x01ff, MRA_RAM },
  { 0x4016, 0x4016, soundlatch_r },
  { 0x4017, 0x4017, soundlatch2_r },
  { 0x4000, 0x4017, NESPSG_0_r },
  { 0xe000, 0xffff, MRA_ROM },
  { -1 } /* end of table */
};

static struct MemoryWriteAddress dkong3_sound1_writemem[] = {
  { 0x0000, 0x01ff, MWA_RAM },
  { 0x4000, 0x4017, NESPSG_0_w },
  { 0xe000, 0xffff, MWA_ROM },
  { -1 } /* end of table */
};

static struct MemoryReadAddress dkong3_sound2_readmem[] = {
  { 0x0000, 0x01ff, MRA_RAM },
  { 0x4016, 0x4016, soundlatch3_r },
  { 0x4000, 0x4017, NESPSG_1_r },
  { 0xe000, 0xffff, MRA_ROM },
  { -1 } /* end of table */
};

static struct MemoryWriteAddress dkong3_sound2_writemem[] = {
  { 0x0000, 0x01ff, MWA_RAM },
  { 0x4011, 0x4011, dkong3_dac_w },
  { 0x4000, 0x4017, NESPSG_1_w },
  { 0xe000, 0xffff, MWA_ROM },
  { -1 } /* end of table */
};

INPUT_PORTS_START(dkong_input_ports)
PORT_START                                                       /* IN0 */
  PORT_BIT(0x01, IP_ACTIVE_HIGH, IPT_JOYSTICK_RIGHT | IPF_8WAY)  // bit 0 : RIGHT player 1
  PORT_BIT(0x02, IP_ACTIVE_HIGH, IPT_JOYSTICK_LEFT | IPF_8WAY)   // bit 1 : LEFT player 1
  PORT_BIT(0x04, IP_ACTIVE_HIGH, IPT_JOYSTICK_UP | IPF_8WAY)     // bit 2 : UP player 1
  PORT_BIT(0x08, IP_ACTIVE_HIGH, IPT_JOYSTICK_DOWN | IPF_8WAY)   // bit 3 : DOWN player 1
  PORT_BIT(0x10, IP_ACTIVE_HIGH, IPT_BUTTON1)                    // bit 4 : JUMP player 1
  PORT_BIT(0x40, IP_ACTIVE_HIGH, IPT_UNKNOWN)                    // bit 6 : reset (when player 1 active)
  PORT_BIT(0x80, IP_ACTIVE_HIGH, IPT_UNKNOWN)

    PORT_START /* IN1 */
  PORT_BIT(0x01, IP_ACTIVE_HIGH, IPT_JOYSTICK_RIGHT | IPF_8WAY | IPF_COCKTAIL)
    PORT_BIT(0x02, IP_ACTIVE_HIGH, IPT_JOYSTICK_LEFT | IPF_8WAY | IPF_COCKTAIL)
      PORT_BIT(0x04, IP_ACTIVE_HIGH, IPT_JOYSTICK_UP | IPF_8WAY | IPF_COCKTAIL)
        PORT_BIT(0x08, IP_ACTIVE_HIGH, IPT_JOYSTICK_DOWN | IPF_8WAY | IPF_COCKTAIL)
          PORT_BIT(0x10, IP_ACTIVE_HIGH, IPT_BUTTON1 | IPF_COCKTAIL)
            PORT_BIT(0x20, IP_ACTIVE_HIGH, IPT_UNKNOWN)
              PORT_BIT(0x40, IP_ACTIVE_HIGH, IPT_UNKNOWN)
                PORT_BIT(0x80, IP_ACTIVE_HIGH, IPT_UNKNOWN)

                  PORT_START /* IN2 */
  PORT_DIPNAME(0x01, 0x00, "Self Test", IP_KEY_NONE)
    PORT_DIPSETTING(0x00, "Off")
      PORT_DIPSETTING(0x01, "On")
        PORT_BIT(0x02, IP_ACTIVE_HIGH, IPT_UNKNOWN)
          PORT_BIT(0x04, IP_ACTIVE_HIGH, IPT_START1)
            PORT_BIT(0x08, IP_ACTIVE_HIGH, IPT_START2)
              PORT_BIT(0x10, IP_ACTIVE_HIGH, IPT_UNKNOWN)
                PORT_BIT(0x20, IP_ACTIVE_HIGH, IPT_UNKNOWN)
                  PORT_BIT(0x40, IP_ACTIVE_HIGH, IPT_UNKNOWN)
                    PORT_BIT(0x80, IP_ACTIVE_HIGH, IPT_COIN1)

                      PORT_START /* DSW0 */
  PORT_DIPNAME(0x03, 0x00, "Lives", IP_KEY_NONE)
    PORT_DIPSETTING(0x00, "3")
      PORT_DIPSETTING(0x01, "4")
        PORT_DIPSETTING(0x02, "5")
          PORT_DIPSETTING(0x03, "6")
            PORT_DIPNAME(0x0c, 0x00, "Bonus Life", IP_KEY_NONE)
              PORT_DIPSETTING(0x00, "7000")
                PORT_DIPSETTING(0x04, "10000")
                  PORT_DIPSETTING(0x08, "15000")
                    PORT_DIPSETTING(0x0c, "20000")
                      PORT_DIPNAME(0x70, 0x00, "Coinage", IP_KEY_NONE)
                        PORT_DIPSETTING(0x70, "5 Coins/1 Credit")
                          PORT_DIPSETTING(0x50, "4 Coins/1 Credit")
                            PORT_DIPSETTING(0x30, "3 Coins/1 Credit")
                              PORT_DIPSETTING(0x10, "2 Coins/1 Credit")
                                PORT_DIPSETTING(0x00, "1 Coin/1 Credit")
                                  PORT_DIPSETTING(0x20, "1 Coin/2 Credits")
                                    PORT_DIPSETTING(0x40, "1 Coin/3 Credits")
                                      PORT_DIPSETTING(0x60, "1 Coin/4 Credits")
                                        PORT_DIPNAME(0x80, 0x80, "Cabinet", IP_KEY_NONE)
                                          PORT_DIPSETTING(0x80, "Upright")
                                            PORT_DIPSETTING(0x00, "Cocktail")
                                              INPUT_PORTS_END

  INPUT_PORTS_START(dkong3_input_ports)
    PORT_START                                                   /* IN0 */
  PORT_BIT(0x01, IP_ACTIVE_HIGH, IPT_JOYSTICK_RIGHT | IPF_8WAY)  // bit 0 : RIGHT player 1
  PORT_BIT(0x02, IP_ACTIVE_HIGH, IPT_JOYSTICK_LEFT | IPF_8WAY)   // bit 1 : LEFT player 1
  PORT_BIT(0x04, IP_ACTIVE_HIGH, IPT_JOYSTICK_UP | IPF_8WAY)     // bit 2 : UP player 1
  PORT_BIT(0x08, IP_ACTIVE_HIGH, IPT_JOYSTICK_DOWN | IPF_8WAY)   // bit 3 : DOWN player 1
  PORT_BIT(0x10, IP_ACTIVE_HIGH, IPT_BUTTON1)                    // bit 4 : JUMP player 1
  PORT_BIT(0x20, IP_ACTIVE_HIGH, IPT_START1)
    PORT_BIT(0x40, IP_ACTIVE_HIGH, IPT_START2)  // bit 6 : reset (when player 1 active)
  PORT_BITX(0x80, IP_ACTIVE_HIGH, IPT_SERVICE, "Service Mode", OSD_KEY_F2, IP_JOY_NONE, 0)

    PORT_START /* IN1 */
  PORT_BIT(0x01, IP_ACTIVE_HIGH, IPT_JOYSTICK_RIGHT | IPF_8WAY | IPF_COCKTAIL)
    PORT_BIT(0x02, IP_ACTIVE_HIGH, IPT_JOYSTICK_LEFT | IPF_8WAY | IPF_COCKTAIL)
      PORT_BIT(0x04, IP_ACTIVE_HIGH, IPT_JOYSTICK_UP | IPF_8WAY | IPF_COCKTAIL)
        PORT_BIT(0x08, IP_ACTIVE_HIGH, IPT_JOYSTICK_DOWN | IPF_8WAY | IPF_COCKTAIL)
          PORT_BIT(0x10, IP_ACTIVE_HIGH, IPT_BUTTON1 | IPF_COCKTAIL)
            PORT_BITX(0x20, IP_ACTIVE_HIGH, IPT_COIN1 | IPF_IMPULSE, "Coin A", IP_KEY_DEFAULT, IP_JOY_DEFAULT, 1)
              PORT_BITX(0x40, IP_ACTIVE_HIGH, IPT_COIN2 | IPF_IMPULSE, "Coin B", IP_KEY_DEFAULT, IP_JOY_DEFAULT, 1)
                PORT_BIT(0x80, IP_ACTIVE_HIGH, IPT_UNKNOWN)

                  PORT_START /* DSW0 */
  PORT_DIPNAME(0x07, 0x00, "Coin A", IP_KEY_NONE)
    PORT_DIPSETTING(0x02, "3 Coins/1 Credit")
      PORT_DIPSETTING(0x04, "2 Coins/1 Credit")
        PORT_DIPSETTING(0x00, "1 Coin/1 Credit")
          PORT_DIPSETTING(0x06, "1 Coin/2 Credits")
            PORT_DIPSETTING(0x01, "1 Coin/3 Credits")
              PORT_DIPSETTING(0x03, "1 Coin/4 Credits")
                PORT_DIPSETTING(0x05, "1 Coin/5 Credits")
                  PORT_DIPSETTING(0x07, "1 Coin/6 Credits")
                    PORT_DIPNAME(0x08, 0x00, "Unknown 1", IP_KEY_NONE)
                      PORT_DIPSETTING(0x00, "Off")
                        PORT_DIPSETTING(0x08, "On")
                          PORT_DIPNAME(0x10, 0x00, "Unknown 2", IP_KEY_NONE)
                            PORT_DIPSETTING(0x00, "Off")
                              PORT_DIPSETTING(0x10, "On")
                                PORT_DIPNAME(0x20, 0x00, "Unknown 3", IP_KEY_NONE)
                                  PORT_DIPSETTING(0x00, "Off")
                                    PORT_DIPSETTING(0x20, "On")
                                      PORT_DIPNAME(0x40, 0x00, "Self Test", IP_KEY_NONE)
                                        PORT_DIPSETTING(0x00, "Off")
                                          PORT_DIPSETTING(0x40, "On")
                                            PORT_DIPNAME(0x80, 0x00, "Cabinet", IP_KEY_NONE)
                                              PORT_DIPSETTING(0x00, "Upright")
                                                PORT_DIPSETTING(0x80, "Cocktail")

                                                  PORT_START /* DSW1 */
  PORT_DIPNAME(0x03, 0x00, "Lives", IP_KEY_NONE)
    PORT_DIPSETTING(0x00, "3")
      PORT_DIPSETTING(0x01, "4")
        PORT_DIPSETTING(0x02, "5")
          PORT_DIPSETTING(0x03, "6")
            PORT_DIPNAME(0x0c, 0x00, "Bonus Life", IP_KEY_NONE)
              PORT_DIPSETTING(0x00, "30000")
                PORT_DIPSETTING(0x04, "40000")
                  PORT_DIPSETTING(0x08, "50000")
                    PORT_DIPSETTING(0x0c, "None")
                      PORT_DIPNAME(0x30, 0x00, "Additional Bonus", IP_KEY_NONE)
                        PORT_DIPSETTING(0x00, "30000")
                          PORT_DIPSETTING(0x10, "40000")
                            PORT_DIPSETTING(0x20, "50000")
                              PORT_DIPSETTING(0x30, "None")
                                PORT_DIPNAME(0xc0, 0x00, "Difficulty", IP_KEY_NONE)
                                  PORT_DIPSETTING(0x00, "Easy")
                                    PORT_DIPSETTING(0x40, "Medium")
                                      PORT_DIPSETTING(0x80, "Hard")
                                        PORT_DIPSETTING(0xc0, "Hardest")
                                          INPUT_PORTS_END



  static struct GfxLayout dkong_charlayout = {
    8, 8,                       /* 8*8 characters */
    256,                        /* 256 characters */
    2,                          /* 2 bits per pixel */
    { 256 * 8 * 8, 0 },         /* the two bitplanes are separated */
    { 0, 1, 2, 3, 4, 5, 6, 7 }, /* pretty straightforward layout */
    { 0 * 8, 1 * 8, 2 * 8, 3 * 8, 4 * 8, 5 * 8, 6 * 8, 7 * 8 },
    8 * 8 /* every char takes 8 consecutive bytes */
  };
static struct GfxLayout dkongjr_charlayout = {
  8, 8,                       /* 8*8 characters */
  512,                        /* 512 characters */
  2,                          /* 2 bits per pixel */
  { 512 * 8 * 8, 0 },         /* the two bitplanes are separated */
  { 0, 1, 2, 3, 4, 5, 6, 7 }, /* pretty straightforward layout */
  { 0 * 8, 1 * 8, 2 * 8, 3 * 8, 4 * 8, 5 * 8, 6 * 8, 7 * 8 },
  8 * 8 /* every char takes 8 consecutive bytes */
};
static struct GfxLayout dkong_spritelayout = {
  16, 16,                   /* 16*16 sprites */
  128,                      /* 128 sprites */
  2,                        /* 2 bits per pixel */
  { 128 * 16 * 16, 0 },     /* the two bitplanes are separated */
  { 0, 1, 2, 3, 4, 5, 6, 7, /* the two halves of the sprite are separated */
    64 * 16 * 16 + 0, 64 * 16 * 16 + 1, 64 * 16 * 16 + 2, 64 * 16 * 16 + 3, 64 * 16 * 16 + 4, 64 * 16 * 16 + 5, 64 * 16 * 16 + 6, 64 * 16 * 16 + 7 },
  { 0 * 8, 1 * 8, 2 * 8, 3 * 8, 4 * 8, 5 * 8, 6 * 8, 7 * 8, 8 * 8, 9 * 8, 10 * 8, 11 * 8, 12 * 8, 13 * 8, 14 * 8, 15 * 8 },
  16 * 8 /* every sprite takes 16 consecutive bytes */
};
static struct GfxLayout dkong3_spritelayout = {
  16, 16,                   /* 16*16 sprites */
  256,                      /* 256 sprites */
  2,                        /* 2 bits per pixel */
  { 256 * 16 * 16, 0 },     /* the two bitplanes are separated */
  { 0, 1, 2, 3, 4, 5, 6, 7, /* the two halves of the sprite are separated */
    128 * 16 * 16 + 0, 128 * 16 * 16 + 1, 128 * 16 * 16 + 2, 128 * 16 * 16 + 3, 128 * 16 * 16 + 4, 128 * 16 * 16 + 5, 128 * 16 * 16 + 6, 128 * 16 * 16 + 7 },
  { 0 * 8, 1 * 8, 2 * 8, 3 * 8, 4 * 8, 5 * 8, 6 * 8, 7 * 8, 8 * 8, 9 * 8, 10 * 8, 11 * 8, 12 * 8, 13 * 8, 14 * 8, 15 * 8 },
  16 * 8 /* every sprite takes 16 consecutive bytes */
};



static struct GfxDecodeInfo dkong_gfxdecodeinfo[] = {
  { 1, 0x0000, &dkong_charlayout, 0, 64 },
  { 1, 0x1000, &dkong_spritelayout, 0, 64 },
  { -1 } /* end of array */
};
static struct GfxDecodeInfo dkongjr_gfxdecodeinfo[] = {
  { 1, 0x0000, &dkongjr_charlayout, 0, 64 },
  { 1, 0x2000, &dkong_spritelayout, 0, 64 },
  { -1 } /* end of array */
};
static struct GfxDecodeInfo dkong3_gfxdecodeinfo[] = {
  { 1, 0x0000, &dkongjr_charlayout, 0, 64 },
  { 1, 0x2000, &dkong3_spritelayout, 0, 64 },
  { -1 } /* end of array */
};


static unsigned char hunchy_color_prom[] = {
  /* 2e - palette low 4 bits (inverted) */
  0x0F, 0x0F, 0x0A, 0x03, 0x0F, 0x03, 0x0C, 0x00, 0x0F, 0x06, 0x0A, 0x0C, 0x0F, 0x0F, 0x0E, 0x03,
  0x0F, 0x0C, 0x03, 0x0F, 0x0F, 0x00, 0x00, 0x0C, 0x0F, 0x0F, 0x0A, 0x03, 0x0F, 0x00, 0x0F, 0x00,
  0x0F, 0x0C, 0x0F, 0x03, 0x0F, 0x0F, 0x0C, 0x06, 0x0F, 0x00, 0x0F, 0x0E, 0x0F, 0x0C, 0x0F, 0x03,
  0x0F, 0x00, 0x00, 0x00, 0x0F, 0x00, 0x00, 0x00, 0x0F, 0x00, 0x00, 0x00, 0x0F, 0x00, 0x00, 0x00,
  0x0F, 0x0F, 0x0A, 0x0C, 0x0F, 0x03, 0x0C, 0x00, 0x0F, 0x06, 0x0A, 0x0C, 0x0F, 0x0F, 0x0E, 0x0C,
  0x0F, 0x0C, 0x03, 0x0C, 0x0F, 0x00, 0x00, 0x0C, 0x0F, 0x0F, 0x0A, 0x0C, 0x0F, 0x00, 0x0F, 0x0C,
  0x0F, 0x0C, 0x0F, 0x03, 0x0F, 0x0F, 0x0C, 0x06, 0x0F, 0x00, 0x0F, 0x0E, 0x0F, 0x0C, 0x0F, 0x03,
  0x0F, 0x00, 0x00, 0x00, 0x0F, 0x00, 0x00, 0x00, 0x0F, 0x00, 0x00, 0x00, 0x0F, 0x00, 0x00, 0x00,
  0x0F, 0x0F, 0x0A, 0x03, 0x0F, 0x03, 0x0C, 0x00, 0x0F, 0x06, 0x0A, 0x03, 0x0F, 0x0F, 0x0E, 0x03,
  0x0F, 0x0C, 0x03, 0x03, 0x0F, 0x00, 0x00, 0x03, 0x0F, 0x0F, 0x0A, 0x03, 0x0F, 0x00, 0x0F, 0x03,
  0x0F, 0x0C, 0x0F, 0x03, 0x0F, 0x0F, 0x0C, 0x06, 0x0F, 0x00, 0x0F, 0x0E, 0x0F, 0x0C, 0x0F, 0x03,
  0x0F, 0x00, 0x00, 0x00, 0x0F, 0x00, 0x00, 0x00, 0x0F, 0x00, 0x00, 0x00, 0x0F, 0x00, 0x00, 0x00,
  0x0F, 0x00, 0x00, 0x00, 0x0F, 0x00, 0x00, 0x00, 0x0F, 0x00, 0x00, 0x00, 0x0F, 0x00, 0x00, 0x00,
  0x0F, 0x00, 0x00, 0x00, 0x0F, 0x00, 0x00, 0x00, 0x0F, 0x00, 0x00, 0x00, 0x0F, 0x00, 0x00, 0x00,
  0x0F, 0x00, 0x00, 0x00, 0x0F, 0x00, 0x00, 0x00, 0x0F, 0x00, 0x00, 0x00, 0x0F, 0x00, 0x00, 0x00,
  0x0F, 0x00, 0x00, 0x00, 0x0F, 0x00, 0x00, 0x00, 0x0F, 0x00, 0x00, 0x00, 0x0F, 0x00, 0x00, 0x00,
  /* 2f - palette high 4 bits (inverted) */
  0x0F, 0x01, 0x04, 0x00, 0x0F, 0x0E, 0x01, 0x00, 0x0F, 0x00, 0x03, 0x01, 0x0F, 0x01, 0x0F, 0x0E,
  0x0F, 0x0F, 0x0E, 0x01, 0x0F, 0x00, 0x00, 0x0F, 0x0F, 0x01, 0x04, 0x00, 0x0F, 0x00, 0x01, 0x0E,
  0x0F, 0x01, 0x01, 0x0E, 0x0F, 0x01, 0x01, 0x00, 0x0F, 0x00, 0x01, 0x04, 0x0F, 0x01, 0x01, 0x0E,
  0x0F, 0x00, 0x00, 0x00, 0x0F, 0x00, 0x00, 0x00, 0x0F, 0x00, 0x00, 0x00, 0x0F, 0x00, 0x00, 0x00,
  0x0F, 0x01, 0x04, 0x01, 0x0F, 0x0E, 0x01, 0x00, 0x0F, 0x00, 0x03, 0x01, 0x0F, 0x01, 0x0F, 0x01,
  0x0F, 0x0F, 0x0E, 0x01, 0x0F, 0x00, 0x00, 0x01, 0x0F, 0x01, 0x04, 0x01, 0x0F, 0x00, 0x01, 0x01,
  0x0F, 0x01, 0x01, 0x0E, 0x0F, 0x01, 0x01, 0x00, 0x0F, 0x00, 0x01, 0x04, 0x0F, 0x01, 0x01, 0x0E,
  0x0F, 0x00, 0x00, 0x00, 0x0F, 0x00, 0x00, 0x00, 0x0F, 0x00, 0x00, 0x00, 0x0F, 0x00, 0x00, 0x00,
  0x0F, 0x01, 0x04, 0x0E, 0x0F, 0x0E, 0x01, 0x00, 0x0F, 0x00, 0x03, 0x0E, 0x0F, 0x01, 0x0F, 0x0E,
  0x0F, 0x0F, 0x0E, 0x0E, 0x0F, 0x00, 0x00, 0x0E, 0x0F, 0x01, 0x04, 0x0E, 0x0F, 0x00, 0x01, 0x0E,
  0x0F, 0x01, 0x01, 0x0E, 0x0F, 0x01, 0x01, 0x00, 0x0F, 0x00, 0x01, 0x04, 0x0F, 0x01, 0x01, 0x0E,
  0x0F, 0x00, 0x00, 0x00, 0x0F, 0x00, 0x00, 0x00, 0x0F, 0x00, 0x00, 0x00, 0x0F, 0x00, 0x00, 0x00,
  0x0F, 0x00, 0x00, 0x00, 0x0F, 0x00, 0x00, 0x00, 0x0F, 0x00, 0x00, 0x00, 0x0F, 0x00, 0x00, 0x00,
  0x0F, 0x00, 0x00, 0x00, 0x0F, 0x00, 0x00, 0x00, 0x0F, 0x00, 0x00, 0x00, 0x0F, 0x00, 0x00, 0x00,
  0x0F, 0x00, 0x00, 0x00, 0x0F, 0x00, 0x00, 0x00, 0x0F, 0x00, 0x00, 0x00, 0x0F, 0x00, 0x00, 0x00,
  0x0F, 0x00, 0x00, 0x00, 0x0F, 0x00, 0x00, 0x00, 0x0F, 0x00, 0x00, 0x00, 0x0F, 0x00, 0x00, 0x00,
  /* 2n - character color codes on a per-column basis */
  0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x02, 0x03, 0x03, 0x04, 0x04, 0x05, 0x05, 0x06, 0x06, 0x06,
  0x07, 0x07, 0x07, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x02, 0x03, 0x03, 0x04, 0x04, 0x05, 0x05, 0x06, 0x06, 0x06,
  0x07, 0x07, 0x07, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x02, 0x03, 0x03, 0x04, 0x04, 0x05, 0x05, 0x06, 0x06, 0x06,
  0x07, 0x07, 0x07, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x02, 0x03, 0x03, 0x04, 0x04, 0x05, 0x05, 0x06, 0x06, 0x06,
  0x07, 0x07, 0x07, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x02, 0x03, 0x03, 0x04, 0x04, 0x05, 0x05, 0x06, 0x06, 0x06,
  0x07, 0x07, 0x07, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x02, 0x03, 0x03, 0x04, 0x04, 0x05, 0x05, 0x06, 0x06, 0x06,
  0x07, 0x07, 0x07, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x02, 0x03, 0x03, 0x04, 0x04, 0x05, 0x05, 0x06, 0x06, 0x06,
  0x07, 0x07, 0x07, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x02, 0x03, 0x03, 0x04, 0x04, 0x05, 0x05, 0x06, 0x06, 0x06,
  0x07, 0x07, 0x07, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01
};



static struct DACinterface dkong_dac_interface = {
  1,
  { 100 }
};

static struct Samplesinterface dkong_samples_interface = {
  8 /* 8 channels */
};

static struct Samplesinterface dkongjr_samples_interface = {
  8 /* 8 channels */
};

static struct MachineDriver dkong_machine_driver = {
  /* basic machine hardware */
  {
    { CPU_Z80,
      3072000, /* 3.072 Mhz (?) */
      0,
      readmem, dkong_writemem, 0, 0,
      nmi_interrupt, 1 },
    { CPU_I8035 | CPU_AUDIO_CPU,
      6000000 / 15, /* 6Mhz crystal */
      3,
      readmem_sound, writemem_sound, readport_sound, writeport_sound,
      ignore_interrupt, 1 } },
  60,
  DEFAULT_60HZ_VBLANK_DURATION, /* frames per second, vblank duration */
  1,                            /* 1 CPU slice per frame - interleaving is forced when a sound command is written */
  0,

  /* video hardware */
  32 * 8,
  32 * 8,
  { 0 * 8, 32 * 8 - 1, 2 * 8, 30 * 8 - 1 },
  dkong_gfxdecodeinfo,
  256,
  64 * 4,
  dkong_vh_convert_color_prom,

  VIDEO_TYPE_RASTER | VIDEO_SUPPORTS_DIRTY,
  0,
  dkong_vh_start,
  generic_vh_stop,
  dkong_vh_screenrefresh,

  /* sound hardware */
  0,
  0,
  0,
  0,
  { { //	SOUND_DAC,
      SOUND_ADPCM,
      &dkong_dac_interface },
    { SOUND_SAMPLES,
      &dkong_samples_interface } }
};

static struct MachineDriver dkongjr_machine_driver = {
  /* basic machine hardware */
  {
    { CPU_Z80,
      3072000, /* 3.072 Mhz (?) */
      0,
      readmem, dkongjr_writemem, 0, 0,
      nmi_interrupt, 1 },
    { CPU_I8035 | CPU_AUDIO_CPU,
      6000000 / 15, /* 6Mhz crystal */
      3,
      readmem_sound, writemem_sound, readport_sound, writeport_sound,
      ignore_interrupt, 1 } },
  60,
  DEFAULT_60HZ_VBLANK_DURATION, /* frames per second, vblank duration */
  1,                            /* 1 CPU slice per frame - interleaving is forced when a sound command is written */
  0,

  /* video hardware */
  32 * 8,
  32 * 8,
  { 0 * 8, 32 * 8 - 1, 2 * 8, 30 * 8 - 1 },
  dkongjr_gfxdecodeinfo,
  256,
  64 * 4,
  dkong_vh_convert_color_prom,

  VIDEO_TYPE_RASTER | VIDEO_SUPPORTS_DIRTY,
  0,
  dkong_vh_start,
  generic_vh_stop,
  dkong_vh_screenrefresh,

  /* sound hardware */
  0,
  0,
  0,
  0,
  { { //	SOUND_DAC,
      SOUND_ADPCM,
      &dkong_dac_interface },
    { SOUND_SAMPLES,
      &dkongjr_samples_interface } }
};



static struct NESinterface nes_interface = {
  2,
  21477270, /* 21.47727 MHz */
  { 255, 255 },
};

static struct DACinterface dkong3_dac_interface = {
  1,
  { 255, 255 }
};



static struct MachineDriver dkong3_machine_driver = {
  /* basic machine hardware */
  {
    { CPU_Z80,
      8000000 / 2, /* 4 Mhz */
      0,
      readmem, dkong3_writemem, 0, dkong3_writeport,
      nmi_interrupt, 1 },
    { CPU_M6502 | CPU_AUDIO_CPU,
      21477270 / 16, /* ??? the external clock is right, I assume it is */
                     /* demultiplied internally by the CPU */
      3,
      dkong3_sound1_readmem, dkong3_sound1_writemem, 0, 0,
      nmi_interrupt, 1 },
    { CPU_M6502 | CPU_AUDIO_CPU,
      21477270 / 16, /* ??? the external clock is right, I assume it is */
                     /* demultiplied internally by the CPU */
      4,
      dkong3_sound2_readmem, dkong3_sound2_writemem, 0, 0,
      nmi_interrupt, 1 } },
  60,
  DEFAULT_60HZ_VBLANK_DURATION, /* frames per second, vblank duration */
  1,                            /* 1 CPU slice per frame - interleaving is forced when a sound command is written */
  0,

  /* video hardware */
  32 * 8,
  32 * 8,
  { 0 * 8, 32 * 8 - 1, 2 * 8, 30 * 8 - 1 },
  dkong3_gfxdecodeinfo,
  256,
  64 * 4,
  dkong3_vh_convert_color_prom,

  VIDEO_TYPE_RASTER | VIDEO_SUPPORTS_DIRTY,
  0,
  dkong_vh_start,
  generic_vh_stop,
  dkong_vh_screenrefresh,

  /* sound hardware */
  0,
  0,
  0,
  0,
  { { SOUND_NES,
      &nes_interface },
    { //	SOUND_DAC,
      SOUND_ADPCM,
      &dkong3_dac_interface } }
};



/***************************************************************************

  Game driver(s)

***************************************************************************/

ROM_START(radarscp_rom)
ROM_REGION(0x10000) /* 64k for code */
ROM_LOAD("trs2c5fc", 0x0000, 0x1000, 0x40949e0d)
ROM_LOAD("trs2c5gc", 0x1000, 0x1000, 0xafa8c49f)
ROM_LOAD("trs2c5hc", 0x2000, 0x1000, 0x51b8263d)
ROM_LOAD("trs2c5kc", 0x3000, 0x1000, 0x1f0101f7)
/* space for diagnostic ROM */

ROM_REGION_DISPOSE(0x3000) /* temporary space for graphics (disposed after conversion) */
ROM_LOAD("trs2v3gc", 0x0000, 0x0800, 0xf095330e)
ROM_LOAD("trs2v3hc", 0x0800, 0x0800, 0x15a316f0)
ROM_LOAD("trs2v3dc", 0x1000, 0x0800, 0xe0bb0db9)
ROM_LOAD("trs2v3cc", 0x1800, 0x0800, 0x6c4e7dad)
ROM_LOAD("trs2v3bc", 0x2000, 0x0800, 0x6fdd63f1)
ROM_LOAD("trs2v3ac", 0x2800, 0x0800, 0xb7ad0ba7)

ROM_REGION(0x0300)                                /* color/lookup proms */
ROM_LOAD("rs2-x.xxx", 0x0000, 0x0100, 0x54609d61) /* palette low 4 bits (inverted) */
ROM_LOAD("rs2-c.xxx", 0x0100, 0x0100, 0x79a7d831) /* palette high 4 bits (inverted) */
ROM_LOAD("rs2-v.1hc", 0x0200, 0x0100, 0x1b828315) /* character color codes on a per-column basis */

ROM_REGION(0x1000)                               /* sound */
ROM_LOAD("trs2s3i", 0x0000, 0x0800, 0x78034f14)  /* ??? */
ROM_LOAD("trs2v3ec", 0x0800, 0x0800, 0x0eca8d6b) /* ??? */
ROM_END

static void radarscp_unprotect(void) {
  unsigned char *RAM = Machine->memory_region[Machine->drv->cpu[0].memory_region];


  /* Radarscope does some checks with bit 6 of 7d00 which prevent it from working. */
  /* It's probably a copy protection. We comment it out. */
  RAM[0x1e9c] = 0xc3;
  RAM[0x1e9d] = 0xbd;
}

ROM_START(dkong_rom)
ROM_REGION(0x10000) /* 64k for code */
ROM_LOAD("dk.5e", 0x0000, 0x1000, 0xba70b88b)
ROM_LOAD("dk.5c", 0x1000, 0x1000, 0x5ec461ec)
ROM_LOAD("dk.5b", 0x2000, 0x1000, 0x1c97d324)
ROM_LOAD("dk.5a", 0x3000, 0x1000, 0xb9005ac0)
/* space for diagnostic ROM */

ROM_REGION_DISPOSE(0x3000) /* temporary space for graphics (disposed after conversion) */
ROM_LOAD("dk.3n", 0x0000, 0x0800, 0x12c8c95d)
ROM_LOAD("dk.3p", 0x0800, 0x0800, 0x15e9c5e9)
ROM_LOAD("dk.7c", 0x1000, 0x0800, 0x59f8054d)
ROM_LOAD("dk.7d", 0x1800, 0x0800, 0x672e4714)
ROM_LOAD("dk.7e", 0x2000, 0x0800, 0xfeaa59ee)
ROM_LOAD("dk.7f", 0x2800, 0x0800, 0x20f2ef7e)

ROM_REGION(0x300)                                /* color/lookup proms */
ROM_LOAD("dkong.2k", 0x0000, 0x0100, 0x1e82d375) /* palette low 4 bits (inverted) */
ROM_LOAD("dkong.2j", 0x0100, 0x0100, 0x2ab01dc8) /* palette high 4 bits (inverted) */
ROM_LOAD("dkong.5f", 0x0200, 0x0100, 0x44988665) /* character color codes on a per-column basis */

ROM_REGION(0x1000) /* sound */
ROM_LOAD("dk.3h", 0x0000, 0x0800, 0x45a4ed06)
ROM_LOAD("dk.3f", 0x0800, 0x0800, 0x4743fe92)
ROM_END

ROM_START(dkongjp_rom)
ROM_REGION(0x10000) /* 64k for code */
ROM_LOAD("5f.cpu", 0x0000, 0x1000, 0x424f2b11)
ROM_LOAD("5g.cpu", 0x1000, 0x1000, 0xd326599b)
ROM_LOAD("5h.cpu", 0x2000, 0x1000, 0xff31ac89)
ROM_LOAD("5k.cpu", 0x3000, 0x1000, 0x394d6007)

ROM_REGION_DISPOSE(0x3000) /* temporary space for graphics (disposed after conversion) */
ROM_LOAD("dk.3n", 0x0000, 0x0800, 0x12c8c95d)
ROM_LOAD("5k.vid", 0x0800, 0x0800, 0x3684f914)
ROM_LOAD("dk.7c", 0x1000, 0x0800, 0x59f8054d)
ROM_LOAD("dk.7d", 0x1800, 0x0800, 0x672e4714)
ROM_LOAD("dk.7e", 0x2000, 0x0800, 0xfeaa59ee)
ROM_LOAD("dk.7f", 0x2800, 0x0800, 0x20f2ef7e)

ROM_REGION(0x0300)                               /* color/lookup proms */
ROM_LOAD("dkong.2k", 0x0000, 0x0100, 0x1e82d375) /* palette low 4 bits (inverted) */
ROM_LOAD("dkong.2j", 0x0100, 0x0100, 0x2ab01dc8) /* palette high 4 bits (inverted) */
ROM_LOAD("dkong.5f", 0x0200, 0x0100, 0x44988665) /* character color codes on a per-column basis */

ROM_REGION(0x1000) /* sound */
ROM_LOAD("dk.3h", 0x0000, 0x0800, 0x45a4ed06)
ROM_LOAD("dk.3f", 0x0800, 0x0800, 0x4743fe92)
ROM_END

ROM_START(dkongjr_rom)
ROM_REGION(0x10000) /* 64k for code */
ROM_LOAD("dkj.5b", 0x0000, 0x1000, 0xdea28158)
ROM_CONTINUE(0x3000, 0x1000)
ROM_LOAD("dkj.5c", 0x2000, 0x0800, 0x6fb5faf6)
ROM_CONTINUE(0x4800, 0x0800)
ROM_CONTINUE(0x1000, 0x0800)
ROM_CONTINUE(0x5800, 0x0800)
ROM_LOAD("dkj.5e", 0x4000, 0x0800, 0xd042b6a8)
ROM_CONTINUE(0x2800, 0x0800)
ROM_CONTINUE(0x5000, 0x0800)
ROM_CONTINUE(0x1800, 0x0800)

ROM_REGION_DISPOSE(0x4000) /* temporary space for graphics (disposed after conversion) */
ROM_LOAD("dkj.3n", 0x0000, 0x1000, 0x8d51aca9)
ROM_LOAD("dkj.3p", 0x1000, 0x1000, 0x4ef64ba5)
ROM_LOAD("dkj.7c", 0x2000, 0x0800, 0xdc7f4164)
ROM_LOAD("dkj.7d", 0x2800, 0x0800, 0x0ce7dcf6)
ROM_LOAD("dkj.7e", 0x3000, 0x0800, 0x24d1ff17)
ROM_LOAD("dkj.7f", 0x3800, 0x0800, 0x0f8c083f)

ROM_REGION(0x0300)                                  /* color PROMs */
ROM_LOAD("dkjrprom.2e", 0x0000, 0x0100, 0x463dc7ad) /* palette low 4 bits (inverted) */
ROM_LOAD("dkjrprom.2f", 0x0100, 0x0100, 0x47ba0042) /* palette high 4 bits (inverted) */
ROM_LOAD("dkjrprom.2n", 0x0200, 0x0100, 0xdbf185bf) /* character color codes on a per-column basis */

ROM_REGION(0x1000) /* sound? */
ROM_LOAD("dkj.3h", 0x0000, 0x1000, 0x715da5f8)
ROM_END

ROM_START(dkngjrjp_rom)
ROM_REGION(0x10000) /* 64k for code */
ROM_LOAD("dkjr1", 0x0000, 0x1000, 0xec7e097f)
ROM_CONTINUE(0x3000, 0x1000)
ROM_LOAD("dkjr2", 0x2000, 0x0800, 0xc0a18f0d)
ROM_CONTINUE(0x4800, 0x0800)
ROM_CONTINUE(0x1000, 0x0800)
ROM_CONTINUE(0x5800, 0x0800)
ROM_LOAD("dkjr3", 0x4000, 0x0800, 0xa81dd00c)
ROM_CONTINUE(0x2800, 0x0800)
ROM_CONTINUE(0x5000, 0x0800)
ROM_CONTINUE(0x1800, 0x0800)

ROM_REGION_DISPOSE(0x6000) /* temporary space for graphics (disposed after conversion) */
ROM_LOAD("dkjr9", 0x0000, 0x1000, 0xa95c4c63)
ROM_LOAD("dkjr10", 0x1000, 0x1000, 0xadc11322)
ROM_LOAD("dkj.7c", 0x2000, 0x0800, 0xdc7f4164)
ROM_LOAD("dkj.7d", 0x2800, 0x0800, 0x0ce7dcf6)
ROM_LOAD("dkj.7e", 0x3000, 0x0800, 0x24d1ff17)
ROM_LOAD("dkj.7f", 0x3800, 0x0800, 0x0f8c083f)

ROM_REGION(0x0300)                                  /* color PROMs */
ROM_LOAD("dkjrprom.2e", 0x0000, 0x0100, 0x463dc7ad) /* palette low 4 bits (inverted) */
ROM_LOAD("dkjrprom.2f", 0x0100, 0x0100, 0x47ba0042) /* palette high 4 bits (inverted) */
ROM_LOAD("dkjrprom.2n", 0x0200, 0x0100, 0xdbf185bf) /* character color codes on a per-column basis */

ROM_REGION(0x1000) /* sound? */
ROM_LOAD("dkj.3h", 0x0000, 0x1000, 0x715da5f8)
ROM_END

ROM_START(dkjrjp_rom)
ROM_REGION(0x10000) /* 64k for code */
ROM_LOAD("dkjp.5b", 0x0000, 0x1000, 0x7b48870b)
ROM_CONTINUE(0x3000, 0x1000)
ROM_LOAD("dkjp.5c", 0x2000, 0x0800, 0x12391665)
ROM_CONTINUE(0x4800, 0x0800)
ROM_CONTINUE(0x1000, 0x0800)
ROM_CONTINUE(0x5800, 0x0800)
ROM_LOAD("dkjp.5e", 0x4000, 0x0800, 0x6c9f9103)
ROM_CONTINUE(0x2800, 0x0800)
ROM_CONTINUE(0x5000, 0x0800)
ROM_CONTINUE(0x1800, 0x0800)

ROM_REGION_DISPOSE(0x4000) /* temporary space for graphics (disposed after conversion) */
ROM_LOAD("dkj.3n", 0x0000, 0x1000, 0x8d51aca9)
ROM_LOAD("dkj.3p", 0x1000, 0x1000, 0x4ef64ba5)
ROM_LOAD("dkj.7c", 0x2000, 0x0800, 0xdc7f4164)
ROM_LOAD("dkj.7d", 0x2800, 0x0800, 0x0ce7dcf6)
ROM_LOAD("dkj.7e", 0x3000, 0x0800, 0x24d1ff17)
ROM_LOAD("dkj.7f", 0x3800, 0x0800, 0x0f8c083f)

ROM_REGION(0x0300)                                  /* color PROMs */
ROM_LOAD("dkjrprom.2e", 0x0000, 0x0100, 0x463dc7ad) /* palette low 4 bits (inverted) */
ROM_LOAD("dkjrprom.2f", 0x0100, 0x0100, 0x47ba0042) /* palette high 4 bits (inverted) */
ROM_LOAD("dkjrprom.2n", 0x0200, 0x0100, 0xdbf185bf) /* character color codes on a per-column basis */

ROM_REGION(0x1000) /* sound? */
ROM_LOAD("dkj.3h", 0x0000, 0x1000, 0x715da5f8)
ROM_END

ROM_START(dkjrbl_rom)
ROM_REGION(0x10000) /* 64k for code */
ROM_LOAD("djr1-c.5b", 0x0000, 0x1000, 0xffe9e1a5)
ROM_CONTINUE(0x3000, 0x1000)
ROM_LOAD("djr1-c.5c", 0x2000, 0x0800, 0x982e30e8)
ROM_CONTINUE(0x4800, 0x0800)
ROM_CONTINUE(0x1000, 0x0800)
ROM_CONTINUE(0x5800, 0x0800)
ROM_LOAD("djr1-c.5e", 0x4000, 0x0800, 0x24c3d325)
ROM_CONTINUE(0x2800, 0x0800)
ROM_CONTINUE(0x5000, 0x0800)
ROM_CONTINUE(0x1800, 0x0800)
ROM_LOAD("djr1-c.5a", 0x8000, 0x1000, 0xbb5f5180)

ROM_REGION_DISPOSE(0x4000) /* temporary space for graphics (disposed after conversion) */
ROM_LOAD("dkj.3n", 0x0000, 0x1000, 0x8d51aca9)
ROM_LOAD("dkj.3p", 0x1000, 0x1000, 0x4ef64ba5)
ROM_LOAD("dkj.7c", 0x2000, 0x0800, 0xdc7f4164)
ROM_LOAD("dkj.7d", 0x2800, 0x0800, 0x0ce7dcf6)
ROM_LOAD("dkj.7e", 0x3000, 0x0800, 0x24d1ff17)
ROM_LOAD("dkj.7f", 0x3800, 0x0800, 0x0f8c083f)

ROM_REGION(0x0300)                                  /* color PROMs */
ROM_LOAD("dkjrprom.2e", 0x0000, 0x0100, 0x463dc7ad) /* palette low 4 bits (inverted) */
ROM_LOAD("dkjrprom.2f", 0x0100, 0x0100, 0x47ba0042) /* palette high 4 bits (inverted) */
ROM_LOAD("dkjrprom.2n", 0x0200, 0x0100, 0xdbf185bf) /* character color codes on a per-column basis */

ROM_REGION(0x1000) /* sound? */
ROM_LOAD("dkj.3h", 0x0000, 0x1000, 0x715da5f8)
ROM_END

ROM_START(dkong3_rom)
ROM_REGION(0x10000) /* 64k for code */
ROM_LOAD("dk3c.7b", 0x0000, 0x2000, 0x38d5f38e)
ROM_LOAD("dk3c.7c", 0x2000, 0x2000, 0xc9134379)
ROM_LOAD("dk3c.7d", 0x4000, 0x2000, 0xd22e2921)
ROM_LOAD("dk3c.7e", 0x8000, 0x2000, 0x615f14b7)

ROM_REGION_DISPOSE(0x6000) /* temporary space for graphics (disposed after conversion) */
ROM_LOAD("dk3v.3n", 0x0000, 0x1000, 0x415a99c7)
ROM_LOAD("dk3v.3p", 0x1000, 0x1000, 0x25744ea0)
ROM_LOAD("dk3v.7c", 0x2000, 0x1000, 0x8ffa1737)
ROM_LOAD("dk3v.7d", 0x3000, 0x1000, 0x9ac84686)
ROM_LOAD("dk3v.7e", 0x4000, 0x1000, 0x0c0af3fb)
ROM_LOAD("dk3v.7f", 0x5000, 0x1000, 0x55c58662)

ROM_REGION(0x0300)                                /* color proms */
ROM_LOAD("dkc1-c.1d", 0x0000, 0x0200, 0xdf54befc) /* palette red & green component */
ROM_LOAD("dkc1-c.1c", 0x0100, 0x0200, 0x66a77f40) /* palette blue component */
ROM_LOAD("dkc1-v.2n", 0x0200, 0x0100, 0x50e33434) /* character color codes on a per-column basis */

ROM_REGION(0x10000) /* sound #1 */
ROM_LOAD("dk3c.5l", 0xe000, 0x2000, 0x7ff88885)

ROM_REGION(0x10000) /* sound #2 */
ROM_LOAD("dk3c.6h", 0xe000, 0x2000, 0x36d7200c)
ROM_END

ROM_START(hunchy_rom)
ROM_REGION(0x10000) /* 64k for code */
/* the following is all wrong. Hunchback runs on a modified Donkey */
/* Kong Jr. board, but with a twist: it uses a Signetics 2650 CPU */
/* The chip is chip labeled MAB2650A CRG3243MOY.  */
/* The db also has various 74LS chips and a couple proms (soldered), */
/* and a VERY strange looking flat chip. */
ROM_LOAD("1b.bin", 0x0000, 0x1000, 0x0)
ROM_LOAD("2a.bin", 0x1000, 0x1000, 0x0)
ROM_LOAD("3a.bin", 0x2000, 0x1000, 0x0)
ROM_LOAD("4c.bin", 0x3000, 0x1000, 0x0)
ROM_LOAD("5a.bin", 0x4000, 0x1000, 0x0)
ROM_LOAD("6c.bin", 0x5000, 0x1000, 0x0)
ROM_LOAD("8a.bin", 0x8000, 0x0800, 0x0)

ROM_REGION_DISPOSE(0x4000) /* temporary space for graphics (disposed after conversion) */
ROM_LOAD("11a.bin", 0x0000, 0x0800, 0x0)
ROM_LOAD("9b.bin", 0x0800, 0x0800, 0x0)
/* 1000-17ff empty */
ROM_LOAD("10b.bin", 0x1800, 0x0800, 0x0)
/* 2000-3fff empty */

ROM_REGION(0x10000) /* 64k for the audio CPU */
ROM_LOAD("5b_snd.bin", 0x0000, 0x0800, 0x0)
ROM_END

ROM_START(herocast_rom)
ROM_REGION(0x10000) /* 64k for code */
/* the loading addresses are most likely wrong */
/* the ROMs are probably not conriguous. */
/* For example there's a table which suddenly stops at */
/* 1dff and resumes at 3e00 */
ROM_LOAD("red-dot.rgt", 0x0000, 0x2000, 0x9c4af229) /* encrypted */
ROM_LOAD("wht-dot.lft", 0x2000, 0x2000, 0xc10f9235) /* encrypted */
/* space for diagnostic ROM */
ROM_LOAD("2532.3f", 0x4000, 0x1000, 0x553b89bb) /* ??? contains unencrypted */
                                                /* code mapped at 3000 */

ROM_REGION_DISPOSE(0x3000) /* temporary space for graphics (disposed after conversion) */
ROM_LOAD("pnk.3n", 0x0000, 0x0800, 0x574dfd7a)
ROM_LOAD("blk.3p", 0x0800, 0x0800, 0x16f7c040)
ROM_LOAD("gold.7c", 0x1000, 0x0800, 0x5f5282ed)
ROM_LOAD("orange.7d", 0x1800, 0x0800, 0x075d99f5)
ROM_LOAD("yellow.7e", 0x2000, 0x0800, 0xf6272e96)
ROM_LOAD("violet.7f", 0x2800, 0x0800, 0xca020685)

ROM_REGION(0x300)                                 /* color/lookup proms */
ROM_LOAD("82s126.2e", 0x0000, 0x0100, 0x463dc7ad) /* palette low 4 bits (inverted) */
ROM_LOAD("82s126.2f", 0x0100, 0x0100, 0x47ba0042) /* palette high 4 bits (inverted) */
ROM_LOAD("82s126.2n", 0x0200, 0x0100, 0x37aece4b) /* character color codes on a per-column basis */

ROM_REGION(0x1000) /* sound */
ROM_LOAD("silver.3h", 0x0000, 0x0800, 0x67863ce9)
ROM_END

static void herocast_decode(void) {
  int A;
  unsigned char *RAM = Machine->memory_region[Machine->drv->cpu[0].memory_region];


  /* swap data lines D3 and D4, this fixes the text but nothing more. */
  for (A = 0; A < 0x4000; A++) {
    int v;

    v = RAM[A];
    RAM[A] = (v & 0xe7) | ((v & 0x10) >> 1) | ((v & 0x08) << 1);
  }
}

static const char *sample_names[] = {
  "*dkong",
  "effect00.sam",
  "effect01.sam",
  "effect02.sam",
  0 /* end of array */
};

static const char *dkongjr_sample_names[] = {
  "*dkongjr",
  "death.sam",
  "drop.sam",
  "roar.sam",
  "jump.sam",  /* HC */
  "walk.sam",  /* HC */
  "land.sam",  /* HC */
  "climb.sam", /* HC */
  0            /* end of array */
};


static int radarscp_hiload(void) {
  unsigned char *RAM = Machine->memory_region[Machine->drv->cpu[0].memory_region];

  /* check if the hi score table has already been initialized */
  if (memcmp(&RAM[0x6307], "\x00\x00\x07", 3) == 0 && memcmp(&RAM[0x631c], "\x00\x50\x76", 3) == 0 && memcmp(&RAM[0x63a7], "\x00\xfc\x76", 3) == 0 && memcmp(&RAM[0x60a8], "\x50\x76\x00", 3) == 0) /* high score */
  {
    void *f;


    if ((f = osd_fopen(Machine->gamedrv->name, 0, OSD_FILETYPE_HIGHSCORE, 0)) != 0) {
      osd_fread(f, &RAM[0x6307], 162);
      osd_fclose(f);
      RAM[0x60a7] = RAM[0x631d];
      RAM[0x60a8] = RAM[0x631e];
      RAM[0x60a9] = RAM[0x631f];
      /* also have to copy the score to video ram so it displays on startup */
      RAM[0x7641] = RAM[0x6307];
      RAM[0x7621] = RAM[0x6308];
      RAM[0x7601] = RAM[0x6309];
      RAM[0x75e1] = RAM[0x630a];
      RAM[0x75c1] = RAM[0x630b];
      RAM[0x75a1] = RAM[0x630c];
    }
    return 1;
  } else return 0; /* we can't load the hi scores yet */
}

static void radarscp_hisave(void) {
  void *f;
  unsigned char *RAM = Machine->memory_region[Machine->drv->cpu[0].memory_region];


  if ((f = osd_fopen(Machine->gamedrv->name, 0, OSD_FILETYPE_HIGHSCORE, 1)) != 0) {
    osd_fwrite(f, &RAM[0x6307], 162);
    osd_fclose(f);
  }
}



static int hiload(void) {
  unsigned char *RAM = Machine->memory_region[Machine->drv->cpu[0].memory_region];


  /* check if the hi score table has already been initialized */
  if (memcmp(&RAM[0x611d], "\x50\x76\x00", 3) == 0 && memcmp(&RAM[0x61a5], "\x00\x43\x00", 3) == 0 && memcmp(&RAM[0x60b8], "\x50\x76\x00", 3) == 0) /* high score */
  {
    void *f;


    if ((f = osd_fopen(Machine->gamedrv->name, 0, OSD_FILETYPE_HIGHSCORE, 0)) != 0) {
      osd_fread(f, &RAM[0x6100], 34 * 5);
      RAM[0x60b8] = RAM[0x611d];
      RAM[0x60b9] = RAM[0x611e];
      RAM[0x60ba] = RAM[0x611f];
      /* also copy the high score to the screen, otherwise it won't be */
      /* updated until a new game is started */
      videoram_w(0x0241, RAM[0x6107]);
      videoram_w(0x0221, RAM[0x6108]);
      videoram_w(0x0201, RAM[0x6109]);
      videoram_w(0x01e1, RAM[0x610a]);
      videoram_w(0x01c1, RAM[0x610b]);
      videoram_w(0x01a1, RAM[0x610c]);
      osd_fclose(f);
    }

    return 1;
  } else return 0; /* we can't load the hi scores yet */
}

static void hisave(void) {
  void *f;
  unsigned char *RAM = Machine->memory_region[Machine->drv->cpu[0].memory_region];


  if ((f = osd_fopen(Machine->gamedrv->name, 0, OSD_FILETYPE_HIGHSCORE, 1)) != 0) {
    osd_fwrite(f, &RAM[0x6100], 34 * 5);
    osd_fclose(f);
  }
}

static int dkong3_hiload(void) {
  unsigned char *RAM = Machine->memory_region[Machine->drv->cpu[0].memory_region];
  static int firsttime;
  /* check if the hi score table has already been initialized */
  /* the high score table is intialized to all 0, so first of all */
  /* we dirty it, then we wait for it to be cleared again */
  if (firsttime == 0) {
    memset(&RAM[0x6b00], 0xff, 34 * 5);
    firsttime = 1;
  }




  /* check if the hi score table has already been initialized */
  if (memcmp(&RAM[0x6b00], "\xf3\x76\xe8", 3) == 0 && memcmp(&RAM[0x6ba7], "\x00\x5b\x76", 3) == 0) {
    void *f;


    if ((f = osd_fopen(Machine->gamedrv->name, 0, OSD_FILETYPE_HIGHSCORE, 0)) != 0) {
      osd_fread(f, &RAM[0x6b00], 34 * 5); /* hi scores */
      RAM[0x68f3] = RAM[0x6b1f];
      RAM[0x68f4] = RAM[0x6b1e];
      RAM[0x68f5] = RAM[0x6b1d];
      osd_fread(f, &RAM[0x6c20], 0x40); /* distributions */
      osd_fread(f, &RAM[0x6c16], 4);
      osd_fclose(f);
    }
    firsttime = 0;
    return 1;
  } else return 0; /* we can't load the hi scores yet */
}


static void dkong3_hisave(void) {
  void *f;
  unsigned char *RAM = Machine->memory_region[Machine->drv->cpu[0].memory_region];


  if ((f = osd_fopen(Machine->gamedrv->name, 0, OSD_FILETYPE_HIGHSCORE, 1)) != 0) {
    osd_fwrite(f, &RAM[0x6b00], 34 * 5); /* hi scores */
    osd_fwrite(f, &RAM[0x6c20], 0x40);   /* distribution */
    osd_fwrite(f, &RAM[0x6c16], 4);
    osd_fclose(f);
  }
}


static int dkngjrjp_hiload(void) {

  unsigned char *RAM = Machine->memory_region[Machine->drv->cpu[0].memory_region];

  /* check if the hi score table has already been initialized */
  if (memcmp(&RAM[0x611d], "\x00\x18\x01", 3) == 0 && memcmp(&RAM[0x61a5], "\x00\x57\x00", 3) == 0 && memcmp(&RAM[0x60b8], "\x00\x18\x01", 3) == 0) /* high score */
  {
    void *f;


    if ((f = osd_fopen(Machine->gamedrv->name, 0, OSD_FILETYPE_HIGHSCORE, 0)) != 0) {
      osd_fread(f, &RAM[0x6100], 34 * 5);
      osd_fclose(f);
      RAM[0x60b8] = RAM[0x611d];
      RAM[0x60b9] = RAM[0x611e];
      RAM[0x60ba] = RAM[0x611f];

      /* also copy the high score to the screen, otherwise it won't be */
      /* updated until a new game is started */
      videoram_w(0x0241, RAM[0x6107]);
      videoram_w(0x0221, RAM[0x6108]);
      videoram_w(0x0201, RAM[0x6109]);
      videoram_w(0x01e1, RAM[0x610a]);
      videoram_w(0x01c1, RAM[0x610b]);
      videoram_w(0x01a1, RAM[0x610c]);
    }

    return 1;
  } else return 0; /* we can't load the hi scores yet */
}



struct GameDriver radarscp_driver = {
  __FILE__,
  0,
  "radarscp",
  "Radar Scope",
  "1980",
  "Nintendo",
  "Andy White (protection workaround)\nGary Shepherdson (Kong emulator)\nBrad Thomas (hardware info)\nEdward Massey (MageX emulator)\nNicola Salmoria (MAME driver)\nMarco Cassili\nAndy White (color info)\nTim Lindquist (color info)",
  0,
  &dkong_machine_driver,
  0,

  radarscp_rom,
  radarscp_unprotect, 0,
  0,
  0, /* sound_prom */

  dkong_input_ports,

  PROM_MEMORY_REGION(2), 0, 0,
  ORIENTATION_ROTATE_90,

  radarscp_hiload, radarscp_hisave
};

struct GameDriver dkong_driver = {
  __FILE__,
  0,
  "dkong",
  "Donkey Kong (US)",
  "1981",
  "Nintendo of America",
  "Gary Shepherdson (Kong emulator)\nBrad Thomas (hardware info)\nEdward Massey (MageX emulator)\nNicola Salmoria (MAME driver)\nRon Fries (sound)\nGary Walton (color info)\nSimon Walls (color info)\nMarco Cassili",
  0,
  &dkong_machine_driver,
  0,

  dkong_rom,
  0, 0,
  sample_names,
  0, /* sound_prom */

  dkong_input_ports,

  PROM_MEMORY_REGION(2), 0, 0,
  ORIENTATION_ROTATE_90,

  hiload, hisave
};

struct GameDriver dkongjp_driver = {
  __FILE__,
  &dkong_driver,
  "dkongjp",
  "Donkey Kong (Japan)",
  "1981",
  "Nintendo",
  "Gary Shepherdson (Kong emulator)\nBrad Thomas (hardware info)\nEdward Massey (MageX emulator)\nNicola Salmoria (MAME driver)\nRon Fries (sound)\nGary Walton (color info)\nSimon Walls (color info)\nMarco Cassili",
  0,
  &dkong_machine_driver,
  0,

  dkongjp_rom,
  0, 0,
  sample_names,
  0, /* sound_prom */

  dkong_input_ports,

  PROM_MEMORY_REGION(2), 0, 0,
  ORIENTATION_ROTATE_90,

  hiload, hisave
};

struct GameDriver dkongjr_driver = {
  __FILE__,
  0,
  "dkongjr",
  "Donkey Kong Junior (US)",
  "1982",
  "Nintendo of America",
  "Gary Shepherdson (Kong emulator)\nBrad Thomas (hardware info)\nNicola Salmoria (MAME driver)\nTim Lindquist (color info)\nMarco Cassili",
  0,
  &dkongjr_machine_driver,
  0,

  dkongjr_rom,
  0, 0,
  dkongjr_sample_names,
  0, /* sound_prom */

  dkong_input_ports,

  PROM_MEMORY_REGION(2), 0, 0,
  ORIENTATION_ROTATE_90,

  hiload, hisave
};

struct GameDriver dkngjrjp_driver = {
  __FILE__,
  &dkongjr_driver,
  "dkngjrjp",
  "Donkey Kong Jr. (Original Japanese)",
  "1982",
  "bootleg?",
  "Gary Shepherdson (Kong emulator)\nBrad Thomas (hardware info)\nNicola Salmoria (MAME driver)\nTim Lindquist (color info)\nMarco Cassili",
  0,
  &dkongjr_machine_driver,
  0,

  dkngjrjp_rom,
  0, 0,
  dkongjr_sample_names,
  0, /* sound_prom */

  dkong_input_ports,

  PROM_MEMORY_REGION(2), 0, 0,
  ORIENTATION_ROTATE_90,

  dkngjrjp_hiload, hisave
};

struct GameDriver dkjrjp_driver = {
  __FILE__,
  &dkongjr_driver,
  "dkjrjp",
  "Donkey Kong Junior (Japan)",
  "1982",
  "Nintendo",
  "Gary Shepherdson (Kong emulator)\nBrad Thomas (hardware info)\nNicola Salmoria (MAME driver)\nTim Lindquist (color info)\nMarco Cassili",
  0,
  &dkongjr_machine_driver,
  0,

  dkjrjp_rom,
  0, 0,
  dkongjr_sample_names,
  0, /* sound_prom */

  dkong_input_ports,

  PROM_MEMORY_REGION(2), 0, 0,
  ORIENTATION_ROTATE_90,

  hiload, hisave
};

struct GameDriver dkjrbl_driver = {
  __FILE__,
  &dkongjr_driver,
  "dkjrbl",
  "Donkey Kong Junior (bootleg?)",
  "1982",
  "Nintendo of America",
  "Gary Shepherdson (Kong emulator)\nBrad Thomas (hardware info)\nNicola Salmoria (MAME driver)\nTim Lindquist (color info)\nMarco Cassili",
  0,
  &dkongjr_machine_driver,
  0,

  dkjrbl_rom,
  0, 0,
  dkongjr_sample_names,
  0, /* sound_prom */

  dkong_input_ports,

  PROM_MEMORY_REGION(2), 0, 0,
  ORIENTATION_ROTATE_90,

  hiload, hisave
};

struct GameDriver dkong3_driver = {
  __FILE__,
  0,
  "dkong3",
  "Donkey Kong 3",
  "1983",
  "Nintendo of America",
  "Mirko Buffoni (MAME driver)\nNicola Salmoria (additional code)\nTim Lindquist (color info)\nMarco Cassili",
  0,
  &dkong3_machine_driver,
  0,

  dkong3_rom,
  0, 0,
  0,
  0, /* sound_prom */

  dkong3_input_ports,

  PROM_MEMORY_REGION(2), 0, 0,
  ORIENTATION_ROTATE_90,

  dkong3_hiload, dkong3_hisave
};


/* Since this game does not work the input ports and dip switches
 have not been tested */
struct GameDriver hunchy_driver = {
  __FILE__,
  0,
  "hunchy",
  "Hunchback",
  "????",
  "?????",
  "Nicola Salmoria (MAME driver)\nTim Lindquist (color info)",
  GAME_NOT_WORKING,
  &dkongjr_machine_driver,
  0,

  hunchy_rom,
  0, 0,
  0,
  0, /* sound_prom */

  dkong_input_ports,

  hunchy_color_prom, 0, 0,
  ORIENTATION_ROTATE_90,

  0, 0
};

struct GameDriver herocast_driver = {
  __FILE__,
  0,
  "herocast",
  "herocast",
  "1984",
  "Seatongrove (Crown license)",
  "Nicola Salmoria",
  GAME_NOT_WORKING,
  &dkong_machine_driver,
  0,

  herocast_rom,
  herocast_decode, 0,
  0,
  0, /* sound_prom */

  dkong_input_ports,

  hunchy_color_prom, 0, 0,
  ORIENTATION_ROTATE_90,

  0, 0
};
