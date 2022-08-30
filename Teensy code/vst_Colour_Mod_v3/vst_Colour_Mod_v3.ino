/*
   VSTCM

   Vector Signal Transceiver Colour Mod using MCP4922 DACs on the Teensy 4.1

   Based on: https://trmm.net/V.st by Trammell Hudson
   incorporating mods made by "Swapfile" (Github) for Advanced Mame compatibility

   robin@robinchampion.com 2022

   (This is a modified version by fcawth which attempts to speed things up)

*/

#include "advmame.h"
#include "drawing.h"
#include "settings.h"
#include "spi_fct.h"
#include "buttons.h"

#define IR_REMOTE                      // define if IR remote is fitted  TODO:deactivate if the menu is not shown? Has about a 10% reduction of frame rate when active
#ifdef IR_REMOTE
#define SUPPRESS_ERROR_MESSAGE_FOR_BEGIN
#include <IRremote.hpp>
#endif

const int REST_X       = 2048;     // Wait in the middle of the screen
const int REST_Y       = 2048;

//For spot killer fix - if the min or max is beyond these limits we will go to the appropriate side
const int SPOT_MAX     = 3500;
const int SPOT_GOTOMAX = 4050;
const int SPOT_GOTOMIN = 40;

//EXPERIMENTAL automatic draw rate adjustment based on how much idle time there is between frames
//Defines and global for the auto-speed feature
#define NORMAL_SHIFT_SCALING   2.0
#define MAX_DELTA_SHIFT        6    // These are the limits on the auto-shift for speeding up drawing complex frames
#define MIN_DELTA_SHIFT       -3
#define DELTA_SHIFT_INCREMENT  0.2
#define SPEEDUP_THRESHOLD_MS   2    // If the dwell time is less than this then the drawing rate will try to speed up (lower resolution)
#define SLOWDOWN_THRESHOLD_MS  8    // If the dwell time is greater than this then the drawing rate will slow down (higher resolution)
//If the thresholds are too close together there can be "blooming" as the rate goes up and down too quickly - maybe make it limit the
//speed it can change??
float delta_shift = 0 ;

long fps;                           // Approximate FPS used to benchmark code performance improvements

volatile bool show_vstcm_config;    // Shows settings if true

unsigned long dwell_time = 0;

extern params_t v_config[NB_PARAMS];
extern float line_draw_speed;
extern int frame_max_x;
extern int frame_min_x;
extern int frame_max_y;
extern int frame_min_y;

void setup()
{
  Serial.begin(115200);
  while ( !Serial && millis() < 4000 );

  init_gamma();

  //TODO: maybe if a button is pressed it will load defaults instead of reading the sdcard?
  read_vstcm_config();      // Read saved settings from Teensy SD card

  IR_remote_setup();

  buttons_setup();          // Configure buttons on vstcm for input using built in pullup resistors

  SPI_init();               // Set up pins on Teensy as well as SPI registers

  line_draw_speed = (float)v_config[5].pval / NORMAL_SHIFT_SCALING;
  //Serial.println(line_draw_speed);
  show_vstcm_config = true; // Start off showing the settings screen until serial data received

  make_test_pattern();      // Prepare buffer of data to draw test patterns quicker
}

void loop()
{
  elapsedMicros waiting;     // Auto updating, used for FPS calculation
  unsigned long draw_start_time, loop_start_time ;
  int serial_flag;

  frame_max_x = 0;
  frame_min_x = 4095;
  frame_max_y = 0;
  frame_min_y = 4095;

  serial_flag = 0;
  loop_start_time = millis();
  
  if (!Serial) 
  {
    read_data(1); //init read_data if the serial port is not open
    Serial.flush();
  }

  draw_start_time = 0; // Just to prevent a compiler warning

  while (1)
  {
    if (Serial.available())
    {
      if (serial_flag == 0)
      {
        draw_start_time = millis();
        serial_flag = 1;
      }

      show_vstcm_config = false;

      if (read_data(0) == 1)
        break;
    }
    else if ((millis() - loop_start_time) > SERIAL_WAIT_TIME)  //Changed this to check only if serial is not available
      show_vstcm_config = true;

    if (show_vstcm_config)
      break;
  }

  dwell_time = draw_start_time - loop_start_time; //This is how long it waited after drawing a frame - better than FPS for tuning

  if (show_vstcm_config)
  {
    delta_shift = 0;
    line_draw_speed = (float)v_config[5].pval / NORMAL_SHIFT_SCALING + 1.0 ;
    show_vstcm_config_screen();      // Show settings screen and manage associated control buttons
  }
  else
  {
    if (dwell_time < SPEEDUP_THRESHOLD_MS)
    {
      delta_shift += DELTA_SHIFT_INCREMENT;

      if (delta_shift > MAX_DELTA_SHIFT)
        delta_shift = MAX_DELTA_SHIFT;
    }
    //Try to only allow speedups
    //   else if (dwell_time > SLOWDOWN_THRESHOLD_MS) {
    //     delta_shift -= DELTA_SHIFT_INCREMENT;
    //     if (delta_shift < MIN_DELTA_SHIFT) delta_shift = MIN_DELTA_SHIFT;
    //   }

    line_draw_speed = (float)v_config[5].pval / NORMAL_SHIFT_SCALING + delta_shift;

    if (line_draw_speed < 1)
      line_draw_speed = 1;
  }
  
  // Go to the center of the screen, turn the beam off (prevents stray coloured lines from appearing)
  brightness(0, 0, 0);
  dwell(v_config[3].pval);
  
  if (!show_vstcm_config)
  {
    if (((frame_max_x - frame_min_x) < SPOT_MAX) || ((frame_max_y - frame_min_y) < SPOT_MAX))
    {
      draw_moveto (SPOT_GOTOMAX, SPOT_GOTOMAX);
      draw_moveto (SPOT_GOTOMIN, SPOT_GOTOMIN);

      if (dwell_time > 3)
        draw_moveto (SPOT_GOTOMAX, SPOT_GOTOMAX); //If we have time, do a move back all the way to the max
    }
  }
  
  goto_xy(REST_X, REST_Y);
  SPI_flush();
  
  if (show_vstcm_config)
    manage_buttons(); //Moved here to avoid bright spot on the monitor when doing SD card operations
  
  fps = 1000000 / waiting;

  if (show_vstcm_config)
    delay(5); //The 6100 monitor likes to spend some time in the middle
  else
    delayMicroseconds(100); //Wait 100 microseconds in the center if displaying a game (tune this?)
}
