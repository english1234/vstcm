/*
   VSTCM

   Vector Signal Transceiver Colour Mod using MCP4922 DACs on the Teensy 4.1

   Code to read and write settings to SD card

*/

#ifndef _settings_h_
#define _settings_h_

#include <stdio.h>
//
// Test pattern definitions
//
typedef struct DataChunk {
  uint16_t x;                         // We'll just use 12 bits of X & Y for a 4096 point resolution
  uint16_t y;
  uint8_t  red;                       // Max value of each colour is 255
  uint8_t  green;
  uint8_t  blue;
} DataChunk_t;

#define NUMBER_OF_TEST_PATTERNS 2
const int MAX_PTS = 3000;

//
// Definitions related to settings with default values
//
const int  OFF_SHIFT      =     8;     // Smaller numbers == slower transits (the higher the number, the less flicker and faster draw but more wavy lines)
const int  OFF_DWELL0     =     6;     // Time to wait after changing the beam intensity (settling time for intensity DACs and monitor)
const int  OFF_DWELL1     =     0;     // Time to sit before starting a transit
const int  OFF_DWELL2     =     0;     // Time to sit after finishing a transit
const int  NORMAL_SHIFT   =     2;     // The higher the number, the less flicker and faster draw but more wavy lines
const bool SHOW_DT       = true;
const bool FLIP_X         = false;     // Sometimes the X and Y need to be flipped and/or swapped
const bool FLIP_Y         = false;
const bool SWAP_XY        = false;
const bool PINCUSHION     = false;
const int  NORMAL1        =   150;     // Brightness of text in parameter list
const int  BRIGHTER       =   230;
// how long in milliseconds to wait for data before displaying a test pattern
// this can be increased if the test pattern appears during gameplay
const int  SERIAL_WAIT_TIME = 150;
const int  AUDIO_PIN        = 10;      // Connect audio output to GND and pin 10

#define IR_RECEIVE_PIN      32 //Put this outside the ifdef so that it doesn't break the menu

// Structure of settings stored on Teensy SD card
typedef struct {
  char ini_label[20];     // Text string of parameter label in vstcm.ini
  char param[40];         // Parameter label displayed on screen
  uint32_t pval;          // Parameter value
  uint32_t min;           // Min value of parameter
  uint32_t max;           // Max value of parameter
} params_t;

#define NB_PARAMS 16

void read_vstcm_config();
void write_vstcm_config();
void show_vstcm_config_screen();
void make_test_pattern();
void moveto(int, int, int, int, int, int);
void draw_test_pattern(int);

#endif
