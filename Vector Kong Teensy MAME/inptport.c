/***************************************************************************

  inptport.c

  Input ports handling

TODO:	remove the 1 analog device per port limitation
		support more than 1 "real" analog device
		support for inputports producing interrupts
		support for extra "real" hardware (PC throttle's, spinners etc)

***************************************************************************/

#include "driver.h"
#include <math.h>

/* Use the MRU code for 4way joysticks */
#define MRU_JOYSTICK

/* header identifying the version of the game.cfg file */
#define MAMECFGSTRING "MAMECFG\2"

extern int nocheat;
extern void *record;
extern void *playback;

extern unsigned int dispensed_tickets;
extern unsigned int coins[COIN_COUNTERS];
extern unsigned int lastcoin[COIN_COUNTERS];
extern unsigned int coinlockedout[COIN_COUNTERS];

static unsigned char input_port_value[MAX_INPUT_PORTS];
static unsigned char input_vblank[MAX_INPUT_PORTS];

/* Assuming a maxium of one analog input device per port BW 101297 */
static struct InputPort *input_analog[MAX_INPUT_PORTS];
static int input_analog_value[MAX_INPUT_PORTS];
static char input_analog_init[MAX_INPUT_PORTS];

static int mouse_last_x, mouse_last_y;
static int mouse_current_x, mouse_current_y;
static int mouse_previous_x, mouse_previous_y;
static int analog_current_x, analog_current_y;
static int analog_previous_x, analog_previous_y;


/***************************************************************************

  Configuration load/save

***************************************************************************/

static int readint(void *f, int *num) {
  int i;


  *num = 0;
  for (i = 0; i < sizeof(int); i++) {
    unsigned char c;


    *num <<= 8;
    if (osd_fread(f, &c, 1) != 1)
      return -1;
    *num |= c;
  }

  return 0;
}

static void writeint(void *f, int num) {
  int i;


  for (i = 0; i < sizeof(int); i++) {
    unsigned char c;


    c = (num >> 8 * (sizeof(int) - 1)) & 0xff;
    osd_fwrite(f, &c, 1);
    num <<= 8;
  }
}

static int readip(void *f, struct InputPort *in) {
  if (readint(f, &in->type) != 0)
    return -1;
  if (osd_fread(f, &in->mask, 1) != 1)
    return -1;
  if (osd_fread(f, &in->default_value, 1) != 1)
    return -1;
  if (readint(f, &in->keyboard) != 0)
    return -1;
  if (readint(f, &in->joystick) != 0)
    return -1;
  if (readint(f, &in->arg) != 0)
    return -1;

  return 0;
}

static void writeip(void *f, struct InputPort *in) {
  writeint(f, in->type);
  osd_fwrite(f, &in->mask, 1);
  osd_fwrite(f, &in->default_value, 1);
  writeint(f, in->keyboard);
  writeint(f, in->joystick);
  writeint(f, in->arg);
}



int load_input_port_settings(void) {
  void *f;


  if ((f = osd_fopen(Machine->gamedrv->name, 0, OSD_FILETYPE_CONFIG, 0)) != 0) {
    struct InputPort *in;
    int total, savedtotal;
    char buf[8];
    int i;


    in = Machine->gamedrv->input_ports;

    /* calculate the size of the array */
    total = 0;
    while (in->type != IPT_END) {
      total++;
      in++;
    }

    /* read header */
    if (osd_fread(f, buf, 8) != 8)
      goto getout;
    if (memcmp(buf, MAMECFGSTRING, 8) != 0)
      goto getout; /* header invalid */

    /* read array size */
    if (readint(f, &savedtotal) != 0)
      goto getout;
    if (total != savedtotal)
      goto getout; /* different size */

    /* read the original settings and compare them with the ones defined in the driver */
    in = Machine->gamedrv->input_ports;
    while (in->type != IPT_END) {
      struct InputPort saved;


      if (readip(f, &saved) != 0)
        goto getout;
      if (in->type != saved.type || in->mask != saved.mask || in->default_value != saved.default_value || in->keyboard != saved.keyboard || in->joystick != saved.joystick || in->arg != saved.arg)
        goto getout; /* the default values are different */

      in++;
    }

    /* read the current settings */
    in = Machine->input_ports;
    while (in->type != IPT_END) {
      if (readip(f, in) != 0)
        goto getout;

      in++;
    }

    /* Clear the coin & ticket counters/flags - LBO 042898 */
    for (i = 0; i < COIN_COUNTERS; i++)
      coins[i] = lastcoin[i] = coinlockedout[i] = 0;
    dispensed_tickets = 0;

    /* read in the coin/ticket counters */
    for (i = 0; i < COIN_COUNTERS; i++) {
      if (readint(f, (int *)&coins[i]) != 0)
        goto getout;
    }
    if (readint(f, (int *)&dispensed_tickets) != 0)
      goto getout;

getout:
    osd_fclose(f);
  }

  /* All analog ports need initialization */
  {
    int i;
    for (i = 0; i < MAX_INPUT_PORTS; i++)
      input_analog_init[i] = 1;
  }

  update_input_ports();

  /* if we didn't find a saved config, return 0 so the main core knows that it */
  /* is the first time the game is run and it should diplay the disclaimer. */
  if (f) return 1;
  else return 0;
}



void save_input_port_settings(void) {
  void *f;


  if ((f = osd_fopen(Machine->gamedrv->name, 0, OSD_FILETYPE_CONFIG, 1)) != 0) {
    struct InputPort *in;
    int total;
    int i;


    in = Machine->gamedrv->input_ports;

    /* calculate the size of the array */
    total = 0;
    while (in->type != IPT_END) {
      total++;
      in++;
    }

    /* write header */
    osd_fwrite(f, MAMECFGSTRING, 8);
    /* write array size */
    writeint(f, total);
    /* write the original settings as defined in the driver */
    in = Machine->gamedrv->input_ports;
    while (in->type != IPT_END) {
      writeip(f, in);
      in++;
    }
    /* write the current settings */
    in = Machine->input_ports;
    while (in->type != IPT_END) {
      writeip(f, in);
      in++;
    }

    /* write out the coin/ticket counters for this machine - LBO 042898 */
    for (i = 0; i < COIN_COUNTERS; i++)
      writeint(f, coins[i]);
    writeint(f, dispensed_tickets);

    osd_fclose(f);
  }
}

struct ipd {
  int type;
  const char *name;
  int keyboard;
  int joystick;
};

// VSTCM version using buttons on PCB
// OSD_KEY_1 down
// OSD_KEY_2 right
// OSD_KEY_3 middle
// OSD_KEY_4 left
// OSD_KEY_5 up
struct ipd inputport_defaults[] = {
  { IPT_JOYSTICK_UP, "Up", OSD_KEY_5, OSD_KEY_5 },
  { IPT_JOYSTICK_DOWN, "Down", OSD_KEY_1, OSD_KEY_1 },
  { IPT_JOYSTICK_LEFT, "Left", OSD_KEY_4, OSD_KEY_4 },
  { IPT_JOYSTICK_RIGHT, "Right", OSD_KEY_2, OSD_KEY_2 },

  { IPT_JOYSTICK_UP | IPF_PLAYER2, "2 Up", OSD_KEY_5, OSD_KEY_5 },
  { IPT_JOYSTICK_DOWN | IPF_PLAYER2, "2 Down", OSD_KEY_1, OSD_KEY_1 },
  { IPT_JOYSTICK_LEFT | IPF_PLAYER2, "2 Left", OSD_KEY_4, OSD_KEY_4 },
  { IPT_JOYSTICK_RIGHT | IPF_PLAYER2, "2 Right", OSD_KEY_2, OSD_KEY_2 },

 /* { IPT_JOYSTICK_UP | IPF_PLAYER3, "3 Up", OSD_KEY_I, 0 },
  { IPT_JOYSTICK_DOWN | IPF_PLAYER3, "3 Down", OSD_KEY_K, 0 },
  { IPT_JOYSTICK_LEFT | IPF_PLAYER3, "3 Left", OSD_KEY_J, 0 },
  { IPT_JOYSTICK_RIGHT | IPF_PLAYER3, "3 Right", OSD_KEY_L, 0 },
  { IPT_JOYSTICK_UP | IPF_PLAYER4, "4 Up", 0, 0 },
  { IPT_JOYSTICK_DOWN | IPF_PLAYER4, "4 Down", 0, 0 },
  { IPT_JOYSTICK_LEFT | IPF_PLAYER4, "4 Left", 0, 0 },
  { IPT_JOYSTICK_RIGHT | IPF_PLAYER4, "4 Right", 0, 0 },
  { IPT_JOYSTICKRIGHT_UP, "Right/Up", OSD_KEY_I, OSD_JOY_FIRE2 },
  { IPT_JOYSTICKRIGHT_DOWN, "Right/Down", OSD_KEY_K, OSD_JOY_FIRE3 },
  { IPT_JOYSTICKRIGHT_LEFT, "Right/Left", OSD_KEY_J, OSD_JOY_FIRE1 },
  { IPT_JOYSTICKRIGHT_RIGHT, "Right/Right", OSD_KEY_L, OSD_JOY_FIRE4 },
  { IPT_JOYSTICKLEFT_UP, "Left/Up", OSD_KEY_E, OSD_JOY_UP },
  { IPT_JOYSTICKLEFT_DOWN, "Left/Down", OSD_KEY_D, OSD_JOY_DOWN },
  { IPT_JOYSTICKLEFT_LEFT, "Left/Left", OSD_KEY_S, OSD_JOY_LEFT },
  { IPT_JOYSTICKLEFT_RIGHT, "Left/Right", OSD_KEY_F, OSD_JOY_RIGHT },
  { IPT_JOYSTICKRIGHT_UP | IPF_PLAYER2, "2 Right/Up", 0, 0 },
  { IPT_JOYSTICKRIGHT_DOWN | IPF_PLAYER2, "2 Right/Down", 0, 0 },
  { IPT_JOYSTICKRIGHT_LEFT | IPF_PLAYER2, "2 Right/Left", 0, 0 },
  { IPT_JOYSTICKRIGHT_RIGHT | IPF_PLAYER2, "2 Right/Right", 0, 0 },
  { IPT_JOYSTICKLEFT_UP | IPF_PLAYER2, "2 Left/Up", 0, 0 },
  { IPT_JOYSTICKLEFT_DOWN | IPF_PLAYER2, "2 Left/Down", 0, 0 },
  { IPT_JOYSTICKLEFT_LEFT | IPF_PLAYER2, "2 Left/Left", 0, 0 },
  { IPT_JOYSTICKLEFT_RIGHT | IPF_PLAYER2, "2 Left/Right", 0, 0 }, */

  { IPT_BUTTON1, "Button 1", OSD_KEY_3, OSD_KEY_3 },  // This should be jump

  { IPT_BUTTON2, "Button 2", OSD_KEY_ALT, OSD_JOY_FIRE2 },
  { IPT_BUTTON3, "Button 3", OSD_KEY_SPACE, OSD_JOY_FIRE3 },
  { IPT_BUTTON4, "Button 4", OSD_KEY_LSHIFT, OSD_JOY_FIRE4 },
  { IPT_BUTTON5, "Button 5", OSD_KEY_Z, OSD_JOY_FIRE5 },
  { IPT_BUTTON6, "Button 6", OSD_KEY_X, OSD_JOY_FIRE6 },
  { IPT_BUTTON7, "Button 7", OSD_KEY_C, OSD_JOY_FIRE7 },
  { IPT_BUTTON8, "Button 8", OSD_KEY_V, OSD_JOY_FIRE8 },

  { IPT_BUTTON1 | IPF_PLAYER2, "2 Button 1", OSD_KEY_3, OSD_KEY_3 },  // This should be jump

  { IPT_BUTTON2 | IPF_PLAYER2, "2 Button 2", OSD_KEY_S, 0 },
  { IPT_BUTTON3 | IPF_PLAYER2, "2 Button 3", OSD_KEY_Q, 0 },
  { IPT_BUTTON4 | IPF_PLAYER2, "2 Button 4", OSD_KEY_W, 0 },
  { IPT_BUTTON1 | IPF_PLAYER3, "3 Button 1", OSD_KEY_RCONTROL, 0 },
  { IPT_BUTTON2 | IPF_PLAYER3, "3 Button 2", OSD_KEY_RSHIFT, 0 },
  { IPT_BUTTON3 | IPF_PLAYER3, "3 Button 3", OSD_KEY_ENTER, 0 },
  { IPT_BUTTON4 | IPF_PLAYER3, "3 Button 4", 0, 0 },
  { IPT_BUTTON1 | IPF_PLAYER4, "4 Button 1", 0, 0 },
  { IPT_BUTTON2 | IPF_PLAYER4, "4 Button 2", 0, 0 },
  { IPT_BUTTON3 | IPF_PLAYER4, "4 Button 3", 0, 0 },
  { IPT_BUTTON4 | IPF_PLAYER4, "4 Button 4", 0, 0 },
  { IPT_COIN1, "Coin A", OSD_KEY_3, 0 },        // Middle button on VSTCM coins up
  { IPT_COIN2, "Coin B", OSD_KEY_4, 0 },
  { IPT_COIN3, "Coin C", OSD_KEY_5, 0 },
  { IPT_COIN4, "Coin D", OSD_KEY_6, 0 },
  { IPT_TILT, "Tilt", OSD_KEY_T, IP_JOY_NONE },
  { IPT_START1, "1 Player Start", OSD_KEY_1, 0 },   // down buttons starts game
  { IPT_START2, "2 Players Start", OSD_KEY_2, 0 },
  { IPT_START3, "3 Players Start", OSD_KEY_7, 0 },
  { IPT_START4, "4 Players Start", OSD_KEY_8, 0 },
  { IPT_PADDLE, "Paddle", IPF_DEC(OSD_KEY_LEFT) | IPF_INC(OSD_KEY_RIGHT) | IPF_DELTA(4),
    IPF_DEC(OSD_JOY_LEFT) | IPF_INC(OSD_JOY_RIGHT) | IPF_DELTA(4) },
  { IPT_PADDLE | IPF_PLAYER2, "Paddle 2", IPF_DELTA(4), IPF_DELTA(4) },
  { IPT_PADDLE | IPF_PLAYER3, "Paddle 3", IPF_DELTA(4), IPF_DELTA(4) },
  { IPT_PADDLE | IPF_PLAYER4, "Paddle 4", IPF_DELTA(4), IPF_DELTA(4) },
  { IPT_DIAL, "Dial", IPF_DEC(OSD_KEY_LEFT) | IPF_INC(OSD_KEY_RIGHT) | IPF_DELTA(4),
    IPF_DEC(OSD_JOY_LEFT) | IPF_INC(OSD_JOY_RIGHT) | IPF_DELTA(4) },
  { IPT_DIAL | IPF_PLAYER2, "Dial 2", IPF_DELTA(4), IPF_DELTA(4) },
  { IPT_DIAL | IPF_PLAYER3, "Dial 3", IPF_DELTA(4), IPF_DELTA(4) },
  { IPT_DIAL | IPF_PLAYER4, "Dial 4", IPF_DELTA(4), IPF_DELTA(4) },
  { IPT_TRACKBALL_X, "Trak X", IPF_DEC(OSD_KEY_LEFT) | IPF_INC(OSD_KEY_RIGHT) | IPF_DELTA(4),
    IPF_DEC(OSD_JOY_LEFT) | IPF_INC(OSD_JOY_RIGHT) | IPF_DELTA(4) },
  { IPT_TRACKBALL_X | IPF_PLAYER2, "Track X 2", IPF_DELTA(4), IPF_DELTA(4) },
  { IPT_TRACKBALL_X | IPF_PLAYER3, "Track X 3", IPF_DELTA(4), IPF_DELTA(4) },
  { IPT_TRACKBALL_X | IPF_PLAYER4, "Track X 4", IPF_DELTA(4), IPF_DELTA(4) },
  { IPT_TRACKBALL_Y, "Trak Y", IPF_DEC(OSD_KEY_UP) | IPF_INC(OSD_KEY_DOWN) | IPF_DELTA(4),
    IPF_DEC(OSD_JOY_UP) | IPF_INC(OSD_JOY_DOWN) | IPF_DELTA(4) },
  { IPT_TRACKBALL_Y | IPF_PLAYER2, "Track Y 2", IPF_DELTA(4), IPF_DELTA(4) },
  { IPT_TRACKBALL_Y | IPF_PLAYER3, "Track Y 3", IPF_DELTA(4), IPF_DELTA(4) },
  { IPT_TRACKBALL_Y | IPF_PLAYER4, "Track Y 4", IPF_DELTA(4), IPF_DELTA(4) },
  { IPT_AD_STICK_X, "AD Stick X", IPF_DEC(OSD_KEY_LEFT) | IPF_INC(OSD_KEY_RIGHT) | IPF_DELTA(4),
    IPF_DEC(OSD_JOY_LEFT) | IPF_INC(OSD_JOY_RIGHT) | IPF_DELTA(4) },
  { IPT_AD_STICK_X | IPF_PLAYER2, "AD Stick X 2", IPF_DELTA(4), IPF_DELTA(4) },
  { IPT_AD_STICK_X | IPF_PLAYER3, "AD Stick X 3", IPF_DELTA(4), IPF_DELTA(4) },
  { IPT_AD_STICK_X | IPF_PLAYER4, "AD Stick X 4", IPF_DELTA(4), IPF_DELTA(4) },
  { IPT_AD_STICK_Y, "AD Stick Y", IPF_DEC(OSD_KEY_UP) | IPF_INC(OSD_KEY_DOWN) | IPF_DELTA(4),
    IPF_DEC(OSD_JOY_UP) | IPF_INC(OSD_JOY_DOWN) | IPF_DELTA(4) },
  { IPT_AD_STICK_Y | IPF_PLAYER2, "AD Stick Y 2", IPF_DELTA(4), IPF_DELTA(4) },
  { IPT_AD_STICK_Y | IPF_PLAYER3, "AD Stick Y 3", IPF_DELTA(4), IPF_DELTA(4) },
  { IPT_AD_STICK_Y | IPF_PLAYER4, "AD Stick Y 4", IPF_DELTA(4), IPF_DELTA(4) },
  { IPT_UNKNOWN, "UNKNOWN", IP_KEY_NONE, IP_JOY_NONE },
  { IPT_END, 0, IP_KEY_NONE, IP_JOY_NONE }
};

// Original version
/*struct ipd _inputport_defaults[] = {
  { IPT_JOYSTICK_UP, "Up", OSD_KEY_UP, OSD_JOY_UP },
  { IPT_JOYSTICK_DOWN, "Down", OSD_KEY_DOWN, OSD_JOY_DOWN },
  { IPT_JOYSTICK_LEFT, "Left", OSD_KEY_LEFT, OSD_JOY_LEFT },
  { IPT_JOYSTICK_RIGHT, "Right", OSD_KEY_RIGHT, OSD_JOY_RIGHT },
  { IPT_JOYSTICK_UP | IPF_PLAYER2, "2 Up", OSD_KEY_R, 0 },
  { IPT_JOYSTICK_DOWN | IPF_PLAYER2, "2 Down", OSD_KEY_F, 0 },
  { IPT_JOYSTICK_LEFT | IPF_PLAYER2, "2 Left", OSD_KEY_D, 0 },
  { IPT_JOYSTICK_RIGHT | IPF_PLAYER2, "2 Right", OSD_KEY_G, 0 },
  { IPT_JOYSTICK_UP | IPF_PLAYER3, "3 Up", OSD_KEY_I, 0 },
  { IPT_JOYSTICK_DOWN | IPF_PLAYER3, "3 Down", OSD_KEY_K, 0 },
  { IPT_JOYSTICK_LEFT | IPF_PLAYER3, "3 Left", OSD_KEY_J, 0 },
  { IPT_JOYSTICK_RIGHT | IPF_PLAYER3, "3 Right", OSD_KEY_L, 0 },
  { IPT_JOYSTICK_UP | IPF_PLAYER4, "4 Up", 0, 0 },
  { IPT_JOYSTICK_DOWN | IPF_PLAYER4, "4 Down", 0, 0 },
  { IPT_JOYSTICK_LEFT | IPF_PLAYER4, "4 Left", 0, 0 },
  { IPT_JOYSTICK_RIGHT | IPF_PLAYER4, "4 Right", 0, 0 },
  { IPT_JOYSTICKRIGHT_UP, "Right/Up", OSD_KEY_I, OSD_JOY_FIRE2 },
  { IPT_JOYSTICKRIGHT_DOWN, "Right/Down", OSD_KEY_K, OSD_JOY_FIRE3 },
  { IPT_JOYSTICKRIGHT_LEFT, "Right/Left", OSD_KEY_J, OSD_JOY_FIRE1 },
  { IPT_JOYSTICKRIGHT_RIGHT, "Right/Right", OSD_KEY_L, OSD_JOY_FIRE4 },
  { IPT_JOYSTICKLEFT_UP, "Left/Up", OSD_KEY_E, OSD_JOY_UP },
  { IPT_JOYSTICKLEFT_DOWN, "Left/Down", OSD_KEY_D, OSD_JOY_DOWN },
  { IPT_JOYSTICKLEFT_LEFT, "Left/Left", OSD_KEY_S, OSD_JOY_LEFT },
  { IPT_JOYSTICKLEFT_RIGHT, "Left/Right", OSD_KEY_F, OSD_JOY_RIGHT },
  { IPT_JOYSTICKRIGHT_UP | IPF_PLAYER2, "2 Right/Up", 0, 0 },
  { IPT_JOYSTICKRIGHT_DOWN | IPF_PLAYER2, "2 Right/Down", 0, 0 },
  { IPT_JOYSTICKRIGHT_LEFT | IPF_PLAYER2, "2 Right/Left", 0, 0 },
  { IPT_JOYSTICKRIGHT_RIGHT | IPF_PLAYER2, "2 Right/Right", 0, 0 },
  { IPT_JOYSTICKLEFT_UP | IPF_PLAYER2, "2 Left/Up", 0, 0 },
  { IPT_JOYSTICKLEFT_DOWN | IPF_PLAYER2, "2 Left/Down", 0, 0 },
  { IPT_JOYSTICKLEFT_LEFT | IPF_PLAYER2, "2 Left/Left", 0, 0 },
  { IPT_JOYSTICKLEFT_RIGHT | IPF_PLAYER2, "2 Left/Right", 0, 0 },
  { IPT_BUTTON1, "Button 1", OSD_KEY_LCONTROL, OSD_JOY_FIRE1 },
  { IPT_BUTTON2, "Button 2", OSD_KEY_ALT, OSD_JOY_FIRE2 },
  { IPT_BUTTON3, "Button 3", OSD_KEY_SPACE, OSD_JOY_FIRE3 },
  { IPT_BUTTON4, "Button 4", OSD_KEY_LSHIFT, OSD_JOY_FIRE4 },
  { IPT_BUTTON5, "Button 5", OSD_KEY_Z, OSD_JOY_FIRE5 },
  { IPT_BUTTON6, "Button 6", OSD_KEY_X, OSD_JOY_FIRE6 },
  { IPT_BUTTON7, "Button 7", OSD_KEY_C, OSD_JOY_FIRE7 },
  { IPT_BUTTON8, "Button 8", OSD_KEY_V, OSD_JOY_FIRE8 },
  { IPT_BUTTON1 | IPF_PLAYER2, "2 Button 1", OSD_KEY_A, 0 },
  { IPT_BUTTON2 | IPF_PLAYER2, "2 Button 2", OSD_KEY_S, 0 },
  { IPT_BUTTON3 | IPF_PLAYER2, "2 Button 3", OSD_KEY_Q, 0 },
  { IPT_BUTTON4 | IPF_PLAYER2, "2 Button 4", OSD_KEY_W, 0 },
  { IPT_BUTTON1 | IPF_PLAYER3, "3 Button 1", OSD_KEY_RCONTROL, 0 },
  { IPT_BUTTON2 | IPF_PLAYER3, "3 Button 2", OSD_KEY_RSHIFT, 0 },
  { IPT_BUTTON3 | IPF_PLAYER3, "3 Button 3", OSD_KEY_ENTER, 0 },
  { IPT_BUTTON4 | IPF_PLAYER3, "3 Button 4", 0, 0 },
  { IPT_BUTTON1 | IPF_PLAYER4, "4 Button 1", 0, 0 },
  { IPT_BUTTON2 | IPF_PLAYER4, "4 Button 2", 0, 0 },
  { IPT_BUTTON3 | IPF_PLAYER4, "4 Button 3", 0, 0 },
  { IPT_BUTTON4 | IPF_PLAYER4, "4 Button 4", 0, 0 },
  { IPT_COIN1, "Coin A", OSD_KEY_3, 0 },
  { IPT_COIN2, "Coin B", OSD_KEY_4, 0 },
  { IPT_COIN3, "Coin C", OSD_KEY_5, 0 },
  { IPT_COIN4, "Coin D", OSD_KEY_6, 0 },
  { IPT_TILT, "Tilt", OSD_KEY_T, IP_JOY_NONE },
  { IPT_START1, "1 Player Start", OSD_KEY_1, 0 },
  { IPT_START2, "2 Players Start", OSD_KEY_2, 0 },
  { IPT_START3, "3 Players Start", OSD_KEY_7, 0 },
  { IPT_START4, "4 Players Start", OSD_KEY_8, 0 },
  { IPT_PADDLE, "Paddle", IPF_DEC(OSD_KEY_LEFT) | IPF_INC(OSD_KEY_RIGHT) | IPF_DELTA(4),
    IPF_DEC(OSD_JOY_LEFT) | IPF_INC(OSD_JOY_RIGHT) | IPF_DELTA(4) },
  { IPT_PADDLE | IPF_PLAYER2, "Paddle 2", IPF_DELTA(4), IPF_DELTA(4) },
  { IPT_PADDLE | IPF_PLAYER3, "Paddle 3", IPF_DELTA(4), IPF_DELTA(4) },
  { IPT_PADDLE | IPF_PLAYER4, "Paddle 4", IPF_DELTA(4), IPF_DELTA(4) },
  { IPT_DIAL, "Dial", IPF_DEC(OSD_KEY_LEFT) | IPF_INC(OSD_KEY_RIGHT) | IPF_DELTA(4),
    IPF_DEC(OSD_JOY_LEFT) | IPF_INC(OSD_JOY_RIGHT) | IPF_DELTA(4) },
  { IPT_DIAL | IPF_PLAYER2, "Dial 2", IPF_DELTA(4), IPF_DELTA(4) },
  { IPT_DIAL | IPF_PLAYER3, "Dial 3", IPF_DELTA(4), IPF_DELTA(4) },
  { IPT_DIAL | IPF_PLAYER4, "Dial 4", IPF_DELTA(4), IPF_DELTA(4) },
  { IPT_TRACKBALL_X, "Trak X", IPF_DEC(OSD_KEY_LEFT) | IPF_INC(OSD_KEY_RIGHT) | IPF_DELTA(4),
    IPF_DEC(OSD_JOY_LEFT) | IPF_INC(OSD_JOY_RIGHT) | IPF_DELTA(4) },
  { IPT_TRACKBALL_X | IPF_PLAYER2, "Track X 2", IPF_DELTA(4), IPF_DELTA(4) },
  { IPT_TRACKBALL_X | IPF_PLAYER3, "Track X 3", IPF_DELTA(4), IPF_DELTA(4) },
  { IPT_TRACKBALL_X | IPF_PLAYER4, "Track X 4", IPF_DELTA(4), IPF_DELTA(4) },
  { IPT_TRACKBALL_Y, "Trak Y", IPF_DEC(OSD_KEY_UP) | IPF_INC(OSD_KEY_DOWN) | IPF_DELTA(4),
    IPF_DEC(OSD_JOY_UP) | IPF_INC(OSD_JOY_DOWN) | IPF_DELTA(4) },
  { IPT_TRACKBALL_Y | IPF_PLAYER2, "Track Y 2", IPF_DELTA(4), IPF_DELTA(4) },
  { IPT_TRACKBALL_Y | IPF_PLAYER3, "Track Y 3", IPF_DELTA(4), IPF_DELTA(4) },
  { IPT_TRACKBALL_Y | IPF_PLAYER4, "Track Y 4", IPF_DELTA(4), IPF_DELTA(4) },
  { IPT_AD_STICK_X, "AD Stick X", IPF_DEC(OSD_KEY_LEFT) | IPF_INC(OSD_KEY_RIGHT) | IPF_DELTA(4),
    IPF_DEC(OSD_JOY_LEFT) | IPF_INC(OSD_JOY_RIGHT) | IPF_DELTA(4) },
  { IPT_AD_STICK_X | IPF_PLAYER2, "AD Stick X 2", IPF_DELTA(4), IPF_DELTA(4) },
  { IPT_AD_STICK_X | IPF_PLAYER3, "AD Stick X 3", IPF_DELTA(4), IPF_DELTA(4) },
  { IPT_AD_STICK_X | IPF_PLAYER4, "AD Stick X 4", IPF_DELTA(4), IPF_DELTA(4) },
  { IPT_AD_STICK_Y, "AD Stick Y", IPF_DEC(OSD_KEY_UP) | IPF_INC(OSD_KEY_DOWN) | IPF_DELTA(4),
    IPF_DEC(OSD_JOY_UP) | IPF_INC(OSD_JOY_DOWN) | IPF_DELTA(4) },
  { IPT_AD_STICK_Y | IPF_PLAYER2, "AD Stick Y 2", IPF_DELTA(4), IPF_DELTA(4) },
  { IPT_AD_STICK_Y | IPF_PLAYER3, "AD Stick Y 3", IPF_DELTA(4), IPF_DELTA(4) },
  { IPT_AD_STICK_Y | IPF_PLAYER4, "AD Stick Y 4", IPF_DELTA(4), IPF_DELTA(4) },
  { IPT_UNKNOWN, "UNKNOWN", IP_KEY_NONE, IP_JOY_NONE },
  { IPT_END, 0, IP_KEY_NONE, IP_JOY_NONE }
};
*/
/* Note that the following 3 routines have slightly different meanings with analog ports */
const char *default_name(const struct InputPort *in) {
  int i;


  if (in->name != IP_NAME_DEFAULT) return in->name;

  i = 0;
  while (inputport_defaults[i].type != IPT_END && inputport_defaults[i].type != (in->type & (~IPF_MASK | IPF_PLAYERMASK)))
    i++;

  return inputport_defaults[i].name;
}

int default_key(const struct InputPort *in) {
  int i;


  while (in->keyboard == IP_KEY_PREVIOUS) in--;

  if (in->keyboard != IP_KEY_DEFAULT) return in->keyboard;

  i = 0;
  while (inputport_defaults[i].type != IPT_END && inputport_defaults[i].type != (in->type & (~IPF_MASK | IPF_PLAYERMASK)))
    i++;

  return inputport_defaults[i].keyboard;
}

int default_joy(const struct InputPort *in) {
  int i;


  while (in->joystick == IP_JOY_PREVIOUS) in--;

  if (in->joystick != IP_JOY_DEFAULT) return in->joystick;

  i = 0;
  while (inputport_defaults[i].type != IPT_END && inputport_defaults[i].type != (in->type & (~IPF_MASK | IPF_PLAYERMASK)))
    i++;

  return inputport_defaults[i].joystick;
}

void update_analog_port(int port) {
  struct InputPort *in;
  int current, delta, type, sensitivity, clip, min, max, default_value;
  int axis, is_stick, check_bounds;
  int inckey, deckey, keydelta, incjoy, decjoy, joydelta;
  int key, joy;

  /* get input definition */
  in = input_analog[port];
  if (nocheat && (in->type & IPF_CHEAT)) return;
  type = (in->type & ~IPF_MASK);


  key = default_key(in);
  joy = default_joy(in);

  deckey = key & 0x000000ff;
  inckey = (key & 0x0000ff00) >> 8;
  keydelta = (key & 0x00ff0000) >> 16;
  decjoy = joy & 0x000000ff;
  incjoy = (joy & 0x0000ff00) >> 8;
  /* I am undecided if the joydelta is really needed. LBO 120897 */
  /* We probably don't, but this teases the compiler. BW 120997 */
  joydelta = (joy & 0x00ff0000) >> 16;

  switch (type) {
    case IPT_PADDLE:
      axis = X_AXIS;
      is_stick = 0;
      check_bounds = 1;
      break;
    case IPT_DIAL:
      axis = X_AXIS;
      is_stick = 0;
      check_bounds = 0;
      break;
    case IPT_TRACKBALL_X:
      axis = X_AXIS;
      is_stick = 0;
      check_bounds = 0;
      break;
    case IPT_TRACKBALL_Y:
      axis = Y_AXIS;
      is_stick = 0;
      check_bounds = 0;
      break;
    case IPT_AD_STICK_X:
      axis = X_AXIS;
      is_stick = 1;
      check_bounds = 1;
      break;
    case IPT_AD_STICK_Y:
      axis = Y_AXIS;
      is_stick = 1;
      check_bounds = 1;
      break;
    default:
      /* Use some defaults to prevent crash */
      axis = X_AXIS;
      is_stick = 0;
      check_bounds = 0;
      if (errorlog)
        fprintf(errorlog, "Oops, polling non analog device in update_analog_port()????\n");
  }


  sensitivity = in->arg & 0x000000ff;
  clip = (in->arg & 0x0000ff00) >> 8;
  min = (in->arg & 0x00ff0000) >> 16;
  max = (in->arg & 0xff000000) >> 24;
  default_value = in->default_value * 100 / sensitivity;
  /* extremes can be either signed or unsigned */
  if (min > max) min = min - 256;

  /* if IPF_CENTER go back to the default position, but without */
  /* throwing away sub-precision movements which might have been done. */
  /* sticks are handled later... */
  if ((in->type & IPF_CENTER) && (!is_stick))
    input_analog_value[port] -=
      (input_analog_value[port] * sensitivity / 100 - in->default_value) * 100 / sensitivity;

  current = input_analog_value[port];

  delta = 0;

  /* we can't support more than one analog input for now */
  if ((in->type & IPF_PLAYERMASK) == IPF_PLAYER1) {
    if (axis == X_AXIS) {
      int now;

      now = cpu_scalebyfcount(mouse_current_x - mouse_previous_x) + mouse_previous_x;
      delta = now - mouse_last_x;
      mouse_last_x = now;
    } else {
      int now;

      now = cpu_scalebyfcount(mouse_current_y - mouse_previous_y) + mouse_previous_y;
      delta = now - mouse_last_y;
      mouse_last_y = now;
    }
  }

  if (osd_key_pressed(deckey)) delta -= keydelta;
  if (osd_key_pressed(inckey)) delta += keydelta;
  if (osd_joy_pressed(decjoy)) delta -= keydelta; /* LBO 120897 */
  if (osd_joy_pressed(incjoy)) delta += keydelta;

  if (clip != 0) {
    if (delta * sensitivity / 100 < -clip)
      delta = -clip * 100 / sensitivity;
    else if (delta * sensitivity / 100 > clip)
      delta = clip * 100 / sensitivity;
  }

  if (in->type & IPF_REVERSE) delta = -delta;

  /* we can't support more than one analog input for now */
  if ((in->type & IPF_PLAYERMASK) == IPF_PLAYER1) {
    if (is_stick) {
      int new, prev;

      /* center stick */
      if ((delta == 0) && (in->type & IPF_CENTER)) {
        if (current > default_value)
          delta = -100 / sensitivity;
        if (current < default_value)
          delta = 100 / sensitivity;
      }

      /* An analog joystick which is not at zero position (or has just */
      /* moved there) takes precedence over all other computations */
      /* analog_x/y holds values from -128 to 128 (yes, 128, not 127) */

      if (axis == X_AXIS) {
        new = analog_current_x;
        prev = analog_previous_x;
      } else {
        new = analog_current_y;
        prev = analog_previous_y;
      }

      if ((new != 0) || (new - prev != 0)) {
        delta = 0;

        if (in->type & IPF_REVERSE) {
          new = -new;
          prev = -prev;
        }

        /* scale by time */

        new = cpu_scalebyfcount(new - prev) + prev;

#if 1 /* logarithmic scale */
        if (new > 0) {
          current = (pow(new / 128.0, 100.0 / sensitivity) * (max - in->default_value)
                     + in->default_value)
                    * 100 / sensitivity;
        } else {
          current = (pow(-new / 128.0, 100.0 / sensitivity) * (min - in->default_value)
                     + in->default_value)
                    * 100 / sensitivity;
        }
#else
        current = default_value + (new *(max - min) * 100) / (256 * sensitivity);
#endif
      }
    }
  }

  current += delta;

  if (check_bounds) {
    if (current * sensitivity / 100 < min)
      current = min * 100 / sensitivity;
    if (current * sensitivity / 100 > max)
      current = max * 100 / sensitivity;
  }

  input_analog_value[port] = current;

  input_port_value[port] &= ~in->mask;
  input_port_value[port] |= (current * sensitivity / 100) & in->mask;

  if (playback)
    osd_fread(playback, &input_port_value[port], 1);
  else if (record)
    osd_fwrite(record, &input_port_value[port], 1);
}


void update_input_ports(void) {
  int port, ib;
  struct InputPort *in;
#define MAX_INPUT_BITS 256  //1024
  static int impulsecount[MAX_INPUT_BITS];
  static int waspressed[MAX_INPUT_BITS];
#define MAX_JOYSTICKS 3
#define MAX_PLAYERS 4
#ifdef MRU_JOYSTICK
  static int update_serial_number = 1;
  static int joyserial[MAX_JOYSTICKS * MAX_PLAYERS][4];
#else
  int joystick[MAX_JOYSTICKS * MAX_PLAYERS][4];
#endif

#ifdef MAME_NET
  osd_net_sync();
#endif /* MAME_NET */

  /* clear all the values before proceeding */
  for (port = 0; port < MAX_INPUT_PORTS; port++) {
    input_port_value[port] = 0;
    input_vblank[port] = 0;
    input_analog[port] = 0;
  }

#ifndef MRU_JOYSTICK
  for (i = 0; i < 4 * MAX_JOYSTICKS * MAX_PLAYERS; i++)
    joystick[i / 4][i % 4] = 0;
#endif

  in = Machine->input_ports;

  if (in->type == IPT_END) return; /* nothing to do */

  /* make sure the InputPort definition is correct */
  if (in->type != IPT_PORT) {
    if (errorlog) fprintf(errorlog, "Error in InputPort definition: expecting PORT_START\n");
    return;
  } else in++;

#ifdef MRU_JOYSTICK
  /* scan all the joystick ports */
  port = 0;
  while (in->type != IPT_END && port < MAX_INPUT_PORTS) {
    while (in->type != IPT_END && in->type != IPT_PORT) {
      if ((in->type & ~IPF_MASK) >= IPT_JOYSTICK_UP && (in->type & ~IPF_MASK) <= IPT_JOYSTICKLEFT_RIGHT) {
        int key, joy;

        key = default_key(in);
        joy = default_joy(in);

        if ((key != 0 && key != IP_KEY_NONE) || (joy != 0 && joy != IP_JOY_NONE)) {
          int joynum, joydir, player;

          player = 0;
          if ((in->type & IPF_PLAYERMASK) == IPF_PLAYER2)
            player = 1;
          else if ((in->type & IPF_PLAYERMASK) == IPF_PLAYER3)
            player = 2;
          else if ((in->type & IPF_PLAYERMASK) == IPF_PLAYER4)
            player = 3;

          joynum = player * MAX_JOYSTICKS + ((in->type & ~IPF_MASK) - IPT_JOYSTICK_UP) / 4;
          joydir = ((in->type & ~IPF_MASK) - IPT_JOYSTICK_UP) % 4;

          if ((key != 0 && key != IP_KEY_NONE && osd_key_pressed(key)) || (joy != 0 && joy != IP_JOY_NONE && osd_joy_pressed(joy))) {
            if (joyserial[joynum][joydir] == 0)
              joyserial[joynum][joydir] = update_serial_number;
          } else
            joyserial[joynum][joydir] = 0;
        }
      }
      in++;
    }

    port++;
    if (in->type == IPT_PORT) in++;
  }
  update_serial_number += 1;

  in = Machine->input_ports;

  /* already made sure the InputPort definition is correct */
  in++;
#endif


  /* scan all the input ports */
  port = 0;
  ib = 0;
  while (in->type != IPT_END && port < MAX_INPUT_PORTS) {
    struct InputPort *start;


    /* first of all, scan the whole input port definition and build the */
    /* default value. I must do it before checking for input because otherwise */
    /* multiple keys associated with the same input bit wouldn't work (the bit */
    /* would be reset to its default value by the second entry, regardless if */
    /* the key associated with the first entry was pressed) */
    start = in;
    while (in->type != IPT_END && in->type != IPT_PORT) {
      if ((in->type & ~IPF_MASK) != IPT_DIPSWITCH_SETTING) /* skip dipswitch definitions */
      {
        input_port_value[port] =
          (input_port_value[port] & ~in->mask) | (in->default_value & in->mask);
      }

      in++;
    }

    /* now get back to the beginning of the input port and check the input bits. */
    for (in = start;
         in->type != IPT_END && in->type != IPT_PORT;
         in++, ib++) {
      if ((in->type & ~IPF_MASK) != IPT_DIPSWITCH_SETTING && /* skip dipswitch definitions */
          (in->type & IPF_UNUSED) == 0 &&                    /* skip unused bits */
          !(nocheat && (in->type & IPF_CHEAT)))              /* skip cheats if cheats disabled */
      {
        if ((in->type & ~IPF_MASK) == IPT_VBLANK) {
          input_vblank[port] ^= in->mask;
          input_port_value[port] ^= in->mask;
          if (errorlog && Machine->drv->vblank_duration == 0)
            fprintf(errorlog, "Warning: you are using IPT_VBLANK with vblank_duration = 0. You need to increase vblank_duration for IPT_VBLANK to work.\n");
        }
        /* If it's an analog control, handle it appropriately */
        else if (((in->type & ~IPF_MASK) > IPT_ANALOG_START)
                 && ((in->type & ~IPF_MASK) < IPT_ANALOG_END)) /* LBO 120897 */
        {
          input_analog[port] = in;
          /* reset the analog port on first access */
          if (input_analog_init[port]) {
            input_analog_init[port] = 0;
            input_analog_value[port] = in->default_value * 100 / (in->arg & 0x000000ff);
          }
        } else {
          int key, joy;


          key = default_key(in);
          joy = default_joy(in);

          if ((key != 0 && key != IP_KEY_NONE && osd_key_pressed(key)) || (joy != 0 && joy != IP_JOY_NONE && osd_joy_pressed(joy))) {
            /* skip if coin input and it's locked out */
            if ((in->type & ~IPF_MASK) >= IPT_COIN1 && (in->type & ~IPF_MASK) <= IPT_COIN4 && coinlockedout[(in->type & ~IPF_MASK) - IPT_COIN1]) {
              continue;
            }

            /* if IPF_RESET set, reset the first CPU */
            if (in->type & IPF_RESETCPU && waspressed[ib] == 0)
              cpu_reset(0);

            if (in->type & IPF_IMPULSE) {
              if (errorlog && in->arg == 0)
                fprintf(errorlog, "error in input port definition: IPF_IMPULSE with length = 0\n");
              if (waspressed[ib] == 0)
                impulsecount[ib] = in->arg;
              /* the input bit will be toggled later */
            } else if (in->type & IPF_TOGGLE) {
              if (waspressed[ib] == 0) {
                in->default_value ^= in->mask;
                input_port_value[port] ^= in->mask;
              }
            } else if ((in->type & ~IPF_MASK) >= IPT_JOYSTICK_UP && (in->type & ~IPF_MASK) <= IPT_JOYSTICKLEFT_RIGHT) {
              int joynum, joydir, mask, player;


              player = 0;
              if ((in->type & IPF_PLAYERMASK) == IPF_PLAYER2) player = 1;
              else if ((in->type & IPF_PLAYERMASK) == IPF_PLAYER3) player = 2;
              else if ((in->type & IPF_PLAYERMASK) == IPF_PLAYER4) player = 3;

              joynum = player * MAX_JOYSTICKS + ((in->type & ~IPF_MASK) - IPT_JOYSTICK_UP) / 4;
              joydir = ((in->type & ~IPF_MASK) - IPT_JOYSTICK_UP) % 4;

              mask = in->mask;

#ifndef MRU_JOYSTICK
              /* avoid movement in two opposite directions */
              if (joystick[joynum][joydir ^ 1] != 0)
                mask = 0;
              else if (in->type & IPF_4WAY) {
                int dir;


                /* avoid diagonal movements */
                for (dir = 0; dir < 4; dir++) {
                  if (joystick[joynum][dir] != 0)
                    mask = 0;
                }
              }

              joystick[joynum][joydir] = 1;
#else
              /* avoid movement in two opposite directions */
              if (joyserial[joynum][joydir ^ 1] != 0)
                mask = 0;
              else if (in->type & IPF_4WAY) {
                int mru_dir = joydir;
                int mru_serial = 0;
                int dir;


                /* avoid diagonal movements, use mru button */
                for (dir = 0; dir < 4; dir++) {
                  if (joyserial[joynum][dir] > mru_serial) {
                    mru_serial = joyserial[joynum][dir];
                    mru_dir = dir;
                  }
                }

                if (mru_dir != joydir)
                  mask = 0;
              }
#endif

              input_port_value[port] ^= mask;
            } else
              input_port_value[port] ^= in->mask;

            waspressed[ib] = 1;
          } else
            waspressed[ib] = 0;

          if ((in->type & IPF_IMPULSE) && impulsecount[ib] > 0) {
            impulsecount[ib]--;
            waspressed[ib] = 1;
            input_port_value[port] ^= in->mask;
          }
        }
      }
    }

    port++;
    if (in->type == IPT_PORT) in++;
  }

  if (playback)
    osd_fread(playback, input_port_value, MAX_INPUT_PORTS);
  else if (record)
    osd_fwrite(record, input_port_value, MAX_INPUT_PORTS);
}



/* used the the CPU interface to notify that VBlank has ended, so we can update */
/* IPT_VBLANK input ports. */
void inputport_vblank_end(void) {
  int port;
  int deltax, deltay;


  for (port = 0; port < MAX_INPUT_PORTS; port++) {
    if (input_vblank[port]) {
      input_port_value[port] ^= input_vblank[port];
      input_vblank[port] = 0;
    }
  }

  /* update joysticks */
  analog_previous_x = analog_current_x;
  analog_previous_y = analog_current_y;
  osd_poll_joystick();
  osd_analogjoy_read(&analog_current_x, &analog_current_y);

  /* update mouse position */
  mouse_previous_x = mouse_current_x;
  mouse_previous_y = mouse_current_y;
  osd_trak_read(&deltax, &deltay);
  mouse_current_x += deltax;
  mouse_current_y += deltay;
}



int readinputport(int port) {
  struct InputPort *in;

  /* Update analog ports on demand, unless IPF_CUSTOM_UPDATE type */
  /* In that case, the driver must call osd_update_port (int port) */
  /* directly. BW 980112 */
  in = input_analog[port];
  if (in) {
    if (!(in->type & IPF_CUSTOM_UPDATE))
      update_analog_port(port);
  }

  return input_port_value[port];
}

int input_port_0_r(int offset) {
  return readinputport(0);
}
int input_port_1_r(int offset) {
  return readinputport(1);
}
int input_port_2_r(int offset) {
  return readinputport(2);
}
int input_port_3_r(int offset) {
  return readinputport(3);
}
int input_port_4_r(int offset) {
  return readinputport(4);
}
int input_port_5_r(int offset) {
  return readinputport(5);
}
int input_port_6_r(int offset) {
  return readinputport(6);
}
int input_port_7_r(int offset) {
  return readinputport(7);
}
int input_port_8_r(int offset) {
  return readinputport(8);
}
int input_port_9_r(int offset) {
  return readinputport(9);
}
int input_port_10_r(int offset) {
  return readinputport(10);
}
int input_port_11_r(int offset) {
  return readinputport(11);
}
int input_port_12_r(int offset) {
  return readinputport(12);
}
int input_port_13_r(int offset) {
  return readinputport(13);
}
int input_port_14_r(int offset) {
  return readinputport(14);
}
int input_port_15_r(int offset) {
  return readinputport(15);
}
