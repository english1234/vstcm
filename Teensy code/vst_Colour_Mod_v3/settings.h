/*
   VSTCM

   Vector Signal Transceiver Colour Mod using MCP4922 DACs on the Teensy 4.1

   Code to read and write settings to SD card

*/

#ifndef _settings_h_
#define _settings_h_

#ifdef VSTCM
// No specific includes for VSTCM
#else
#include <cstdint>
#include <stdio.h>
#endif

//#define IR_REMOTE                      // define if IR remote is fitted  TODO:deactivate if the menu is not shown? Has about a 10% reduction of frame rate when active

//
// Test pattern definitions
//
typedef struct DataChunk {
  uint16_t x;  // We'll just use 12 bits of X & Y for a 4096 point resolution
  uint16_t y;
  uint8_t red;  // Max value of each colour is 255
  uint8_t green;
  uint8_t blue;
} DataChunk_t;

#define NUMBER_OF_TEST_PATTERNS 2
const int MAX_PTS = 3000;

//
// Definitions related to settings with default values
//
const int OFF_SHIFT = 8;     // Smaller numbers == slower transits (the higher the number, the less flicker and faster draw but more wavy lines)
const int OFF_DWELL0 = 6;    // Time to wait after changing the beam intensity (settling time for intensity DACs and monitor)
const int OFF_DWELL1 = 0;    // Time to sit before starting a transit
const int OFF_DWELL2 = 0;    // Time to sit after finishing a transit
const int NORMAL_SHIFT = 3;  // The higher the number, the less flicker and faster draw but more wavy lines
const bool SHOW_DT = true;
const bool FLIP_X = false;  // Sometimes the X and Y need to be flipped and/or swapped
const bool FLIP_Y = false;
const bool SWAP_XY = false;
const bool PINCUSHION = false;
const int NORMAL1 = 150;  // Brightness of text in parameter list
const int BRIGHTER = 230;
const int COLOUR_SWITCH = 0;  // 0 = Colour display, 1 = monochrome
// how long in milliseconds to wait for data before displaying a test pattern
// this can be increased if the test pattern appears during gameplay
const int SERIAL_WAIT_TIME = 150;
const int AUDIO_PIN = 10;  // Connect audio output to GND and pin 10

#define IR_RECEIVE_PIN 32  //Put this outside the ifdef so that it doesn't break the menu

enum {settings, emu_vecsim, emu_cinemu, emu_6809, emu_68000};    // Possible values for emulator

// Structure holding choices of various menus, such as
// settings stored on Teensy SD card, game choices, etc.
typedef struct {
  char ini_label[20];  // Text string of parameter label in vstcm.ini
  char param[40];      // Parameter label displayed on screen
  uint32_t pval;       // Parameter value
  uint32_t min;        // Min value of parameter
  uint32_t max;        // Max value of parameter
  uint8_t emulator;    // Emulator to use to run game
} params_t;

typedef struct {
  int nb_menu_items;
  params_t *choices;
} list_t;

#define NB_SETTINGS 30        // Number of items in settings menu
#define NB_SPLASH_CHOICES 30  // Number of items in splash screen menu

enum { SETTINGS_MENU, SPLASH_MENU, NO_MENU };  // Menu screen ID

const int intensity = 150;
const int intensity2 = 128;

static const uint16_t positions[] = {
  4095, 4095, 0, 0, 0,
  4095, 0, intensity, intensity, intensity,
  0, 0, intensity, intensity, intensity,
  0, 4095, intensity, intensity, intensity,
  4095, 4095, intensity, intensity, intensity,
  0, 0, 0, 0, 0,
  3071, 4095, intensity, intensity, intensity,
  4095, 2731, intensity, intensity, intensity,
  2048, 0, intensity, intensity, intensity,
  0, 2731, intensity, intensity, intensity,
  1024, 4095, intensity, intensity, intensity,
  4095, 0, intensity, intensity, intensity,
  0, 4095, 0, 0, 0,
  3071, 0, intensity, intensity, intensity,
  4095, 1365, intensity, intensity, intensity,
  2048, 4095, intensity, intensity, intensity,
  0, 1365, intensity, intensity, intensity,
  1024, 0, intensity, intensity, intensity,
  4095, 4095, intensity, intensity, intensity,
  4095, 4095, 0, 0, 0
};

static const uint16_t positions2[] = {
  // cross
  4095, 4095, 0, 0, 0,
  3583, 4095, intensity2, intensity2, intensity2,
  3583, 3583, intensity2, intensity2, intensity2,
  4095, 3583, intensity2, intensity2, intensity2,
  4095, 4095, intensity2, intensity2, intensity2,
  0, 4095, 0, 0, 0,
  512, 4095, intensity2, intensity2, intensity2,
  0, 3583, intensity2, intensity2, intensity2,
  512, 3583, intensity2, intensity2, intensity2,
  0, 4095, intensity2, intensity2, intensity2,
  // Square
  0, 0, 0, 0, 0,
  512, 0, intensity2, intensity2, intensity2,
  512, 512, intensity2, intensity2, intensity2,
  0, 512, intensity2, intensity2, intensity2,
  0, 0, intensity2, intensity2, intensity2,
  // triangle
  4095, 0, 0, 0, 0,
  4095 - 512, 0, intensity2, intensity2, intensity2,
  4095 - 0, 512, intensity2, intensity2, intensity2,
  4095, 0, intensity2, intensity2, intensity2
};
//
// Function prototypes
//
void read_vstcm_config();
void write_vstcm_config();
void show_vstcm_menu_screen(int);
void make_test_pattern();
void moveto(int, int, int, int, int, int);
void draw_test_pattern(int);

#endif