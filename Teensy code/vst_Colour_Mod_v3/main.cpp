/************************************************************

  VSTCM firmware v3 for v3 PCB - IMPROVED MENU SYSTEM

  Robin Champion - March 2023
  Menu System Improvements - June 2025

  (also works on Windows with SDL2 by removing define VSTCM)

************************************************************/

#include "main.h"

#ifdef VSTCM
#include <arduino.h>  // Prevents compiler warnings on Teensy
#include "advmame.h"
#include <Bounce2.h>
#else
#include SDL_PATH
#define _CRT_RAND_S
#include <stdlib.h>
#endif

#include "settings.h"
#include "drawing.h"
#include "spi_fct.h"
#include "buttons.h"

bool should_quit = false;

int menu_is_active = 1;  // Start with menu active, or 0 to start with game

#ifdef VSTCM
extern Bounce button1;
extern Bounce button3;
#endif

// For spot killer fix - if the total distance in x or y is less than SPOT_MAX, it will go to the corners to try to stop
// the spot killer from triggering
const int SPOT_MAX = 3400;
const int SPOT_GOTOMAX = 4076;
const int SPOT_GOTOMIN = 20;

bool spot_triggered;

// EXPERIMENTAL automatic draw rate adjustment based on how much idle time there is between frames
// Defines and global for the auto-speed feature
#define NORMAL_SHIFT_SCALING 2.0
#define MAX_DELTA_SHIFT 6  // These are the limits on the auto-shift for speeding up drawing complex frames
#define MIN_DELTA_SHIFT -3
#define DELTA_SHIFT_INCREMENT 0.1
#define SPEEDUP_THRESHOLD_MS 2   // If the dwell time is less than this then the drawing rate will try to speed up (lower resolution)
#define SLOWDOWN_THRESHOLD_MS 8  // If the dwell time is greater than this then the drawing rate will slow down (higher resolution)

float delta_shift = 0;

long fps;  // Approximate FPS used to benchmark code performance improvements

#ifdef VSTCM
volatile int show_vstcm_settings;  // Shows settings screen if true
#else
int show_vstcm_settings;  // Shows settings screen if true
#endif

unsigned long dwell_time = 0;
int gX, gY;  // Last position of beam
static uint32_t current_time = 0;
static uint32_t last_time = 0;
static uint32_t dt = 0;

extern int frame_max_x;
extern int frame_min_x;
extern int frame_max_y;
extern int frame_min_y;
extern float line_draw_speed;

extern int dwell_before;
extern int dwell_after;
extern int move_speed;

// Global menu instances (externed from settings.cpp)
extern Menu g_main_menu;
extern Menu g_settings_menu;
extern Menu g_games_menu;
extern Menu* g_current_menu;

#ifndef VSTCM
SDL_Renderer* rend_2D_orig = NULL;     // Renderer for original 2D game
static SDL_Texture* text_orig = NULL;  // Texture for original 2D game
static SDL_GameController* controller = NULL;
static SDL_AudioDeviceID audio_device = 0;
#endif

#ifdef VSTCM
extern void _emu_printf(char* msg);
extern void Update_Vector_Screen();
#endif

void emu_printf(char* msg) {
#ifdef VSTCM
  _emu_printf(msg);
#else
  fprintf(stdout, msg);
#endif
}

uint32_t TickCount() {
#ifdef VSTCM
  return millis();
#else
  return SDL_GetTicks();
#endif
}

// Helper function to get setting value from menu system
int32_t get_setting_value(const char* ini_label, int32_t default_value) {
  MenuItem* item = find_menu_item_by_ini_label(&g_settings_menu, ini_label);
  return item ? item->value : default_value;
}

bool flip_x, flip_y, swap_xy, pincushion;
int normal_brightness, bright_brightness, offdwell0;
bool colour_switch;
extern bool dac_swap;
extern bool correction_enabled;

// Function to update global variables from menu settings
void update_goto_xy_settings() {
    // Update global variables based on current menu settings
    extern bool flip_x, flip_y, swap_xy;
    extern int normal_brightness, bright_brightness;
    extern bool colour_switch;
    extern bool dac_swap;
    extern uint16_t x_axis_invert_mask, y_axis_invert_mask;  // Add these externs
    
    flip_x = get_setting_value("FLIP_X", FLIP_X);
    flip_y = get_setting_value("FLIP_Y", FLIP_Y);
    swap_xy = get_setting_value("SWAP_XY", SWAP_XY);
    
    // Update the cached variables used by drawing functions
    dac_swap = swap_xy;
    x_axis_invert_mask = flip_x ? 0xFFFF : 0;  // Invert all bits if flip_x is true
    y_axis_invert_mask = flip_y ? 0xFFFF : 0;  // Invert all bits if flip_y is true
    
    correction_enabled = get_setting_value("PINCUSHION", PINCUSHION);
    normal_brightness = get_setting_value("NORMAL1", NORMAL1);
    bright_brightness = get_setting_value("BRIGHTER", BRIGHTER);
    colour_switch = get_setting_value("COLOUR_SWITCH", COLOUR_SWITCH);
    
    // Update drawing speed
    line_draw_speed = (float)get_setting_value("NORMAL_SHIFT", NORMAL_SHIFT) / NORMAL_SHIFT_SCALING;
    
    // Update dwell times
    dwell_before = get_setting_value("OFF_DWELL1", OFF_DWELL1);
    dwell_after = get_setting_value("OFF_DWELL2", OFF_DWELL2);
    move_speed = get_setting_value("OFF_SHIFT", OFF_SHIFT);
	offdwell0 = get_setting_value("OFF_DWELL0", OFF_DWELL0);
}

void mainloop() {

  current_time = TickCount();
  dt = current_time - last_time;

#ifdef VSTCM
  elapsedMicros waiting;  // Auto updating, used for FPS calculation
#else
  unsigned long waiting;  // For non-Teensy compilation
#endif

  unsigned long draw_start_time, loop_start_time;
  int serial_flag = 0;

  // This is specific to some code to manage a spot killer - perhaps needs to be an option in the settings
  frame_max_x = 0;
  frame_min_x = 4095;
  frame_max_y = 0;
  frame_min_y = 4095;

  loop_start_time = TickCount();

#ifdef VSTCM
  if (!Serial) {
    read_data(1);  // init read_data if the serial port is not open
    Serial.flush();
  }

  draw_start_time = 0;  // Just to prevent a compiler warning

  while (1) {
    if (Serial.available()) {
      if (serial_flag == 0) {
        draw_start_time = millis();
        serial_flag = 1;
      }

      // If serial data is coming, assume we are in game mode unless menu is explicitly active
      if (!menu_is_active) {
        show_vstcm_settings = NO_MENU;
      }

      if (read_data(0) == 1) {  // Try to read some incoming data from MAME

        break;  // Exit while loop after reading data
      }
    } else {
      // No serial data, check if it's time to show the splash screen/menu
      int serial_wait_time_val = get_setting_value("SERIAL_WAIT_TIME", SERIAL_WAIT_TIME);

      if ((millis() - loop_start_time) > serial_wait_time_val) {
        menu_is_active = 1;  // Show menu/splash if no serial data for a while
        show_vstcm_settings = SPLASH_MENU;
        g_current_menu = &g_games_menu;
      }
    }

    if (menu_is_active) {
      // Handle button presses for menu navigation/toggle
      manage_buttons();
      break;  // Exit while loop to draw menu
    }
  }

  dwell_time = draw_start_time - loop_start_time;  // This is how long it waited after drawing a frame
#else                                              // Non-VSTCM (PC) compilation path
  // Handle SDL events and menu toggle for PC
  manage_buttons();
#endif

#ifndef VSTCM
  // Clear SDL renderer for PC version
  SDL_RenderClear(rend_2D_orig);
#endif

  if (menu_is_active) {
    delta_shift = 0;
    // Get NORMAL_SHIFT from the new menu system
    float normal_shift_val = (float)get_setting_value("NORMAL_SHIFT", NORMAL_SHIFT);
    line_draw_speed = normal_shift_val / NORMAL_SHIFT_SCALING + 3.0;  // Faster for menu

    // Draw the current menu
    draw_menu_on_screen(g_current_menu);
  } else {  // Menu is not active, run game/application logic
    // Original game drawing speed adjustment
    if (dwell_time < SPEEDUP_THRESHOLD_MS) {
      delta_shift += DELTA_SHIFT_INCREMENT;
      if (delta_shift > MAX_DELTA_SHIFT)
        delta_shift = MAX_DELTA_SHIFT;
    }

    float normal_shift_val = (float)get_setting_value("NORMAL_SHIFT", NORMAL_SHIFT);
    line_draw_speed = normal_shift_val / NORMAL_SHIFT_SCALING + delta_shift;

    if (line_draw_speed < 1)
      line_draw_speed = 1;
  }

  // Go to the center of the screen, turn the beam off (prevents stray coloured lines from appearing)
  brightness(0, 0, 0);
  dwell(dwell_before);

  // This is only needed to handle dwell times on some real vector monitors
#ifdef VSTCM
  if (!menu_is_active) {  // Only apply spot killer if not in menu
    if (((frame_max_x - frame_min_x) < SPOT_MAX) || ((frame_max_y - frame_min_y) < SPOT_MAX) || (dwell_time > 10)) {
      spot_triggered = true;
      draw_moveto(SPOT_GOTOMAX, SPOT_GOTOMAX);
      SPI_flush();
      if (dwell_time > 5) delayMicroseconds(200);
      else delayMicroseconds(100);
      draw_moveto(SPOT_GOTOMIN, SPOT_GOTOMIN);
      SPI_flush();
      if (dwell_time > 5) delayMicroseconds(200);
      else delayMicroseconds(100);
      if (dwell_time > 10)                        // For really long dwell times, do the moves again
        draw_moveto(SPOT_GOTOMAX, SPOT_GOTOMAX);  // If we have time, do the moves again
      SPI_flush();
      delayMicroseconds(200);
      draw_moveto(SPOT_GOTOMIN, SPOT_GOTOMIN);  // Try to move back to the min again
      SPI_flush();
      delayMicroseconds(200);
    } else spot_triggered = false;
  }

  goto_xy(REST_X, REST_Y);
  SPI_flush();
#endif

#ifdef VSTCM
  fps = 1000000 / waiting;

  if (menu_is_active) {
    delay(5);  // The 6100 monitor likes to spend some time in the middle
  } else {
    delayMicroseconds(100);  // Wait 100 microseconds in the center if displaying a game (tune this?)
  }
#else
  SDL_SetRenderDrawColor(rend_2D_orig, 0, 0, 0, 255);
  SDL_RenderPresent(rend_2D_orig);
#endif

  last_time = current_time;
}

void vstcm_setup() {
#ifdef VSTCM
  Serial.begin(115200);
  while (!Serial && millis() < 4000)
    ;
#endif

  init_gamma();                 // Set up gamma colour table
  IR_remote_setup();            // Configure the infra red remote control, if present
  buttons_setup_new();          // Configure control buttons and initialize menu system
  SPI_init();                   // Set up pins and SPI registers on Teensy
  make_test_pattern();          // Prepare buffer of data to draw test patterns faster
  update_goto_xy_settings();    // Update global variables from loaded settings

  // Set initial state
  menu_is_active = 1;
  show_vstcm_settings = SPLASH_MENU;  // Start off showing the splash screen until serial data received

#ifdef VSTCM
  // Nothing specific needed for VSTCM
#else

  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER) != 0) {
    SDL_Log("Unable to initialise SDL: %s", SDL_GetError());
    return;
  }

  SDL_SetHint(SDL_HINT_BMP_SAVE_LEGACY_FORMAT, "1");

  // create SDL window for original 2D game view
  SDL_Window* window_orig = SDL_CreateWindow("VSTCM",
                                             SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, SCREEN_WIDTH,
                                             SCREEN_HEIGHT, SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
  if (window_orig == NULL) {
    SDL_Log("Unable to create window_orig: %s", SDL_GetError());
    return;
  }

  SDL_SetWindowMinimumSize(window_orig, SCREEN_WIDTH, SCREEN_HEIGHT);

  // create rend_2D_orig
  rend_2D_orig = SDL_CreateRenderer(window_orig, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
  if (rend_2D_orig == NULL) {
    SDL_Log("Unable to create rend_2D_orig: %s", SDL_GetError());
    return;
  }

  // Has the effect of zooming out to fit 4096x4096 into 1024x1024 actual pixels
  SDL_RenderSetLogicalSize(rend_2D_orig, SCREEN_WIDTH, SCREEN_HEIGHT);

  // print info on rend_2D_orig:
  SDL_RendererInfo renderer_info;
  SDL_GetRendererInfo(rend_2D_orig, &renderer_info);
  SDL_Log("Using rend_2D_orig %s", renderer_info.name);

  // Create texture for original 2D game view
  text_orig = SDL_CreateTexture(rend_2D_orig, SDL_PIXELFORMAT_RGB24,
                                SDL_TEXTUREACCESS_STREAMING, SCREEN_WIDTH, SCREEN_HEIGHT);
  if (text_orig == NULL) {
    SDL_Log("Unable to create text_orig: %s", SDL_GetError());
    return;
  }

  // audio init
  SDL_AudioSpec audio_spec;
  SDL_zero(audio_spec);
  audio_spec.freq = 44100;
  audio_spec.format = AUDIO_S16SYS;
  audio_spec.channels = 1;
  audio_spec.samples = 1024;
  audio_spec.callback = NULL;

  audio_device = SDL_OpenAudioDevice(NULL, 0, &audio_spec, NULL, 0);

  if (audio_device == 0) {
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "failed to open audio: %s",
                 SDL_GetError());
    should_quit = true;
    return;
  } else {
    const char* driver_name = SDL_GetCurrentAudioDriver();
    SDL_Log("audio device has been opened (%s)", driver_name);
  }

  SDL_PauseAudioDevice(audio_device, 0);  // start playing

  // controller init: opening the first available controller
  controller = NULL;
  for (int i = 0; i < SDL_NumJoysticks(); i++) {
    if (SDL_IsGameController(i)) {
      controller = SDL_GameControllerOpen(i);
      if (controller) {
        SDL_Log(
          "game controller detected: %s", SDL_GameControllerNameForIndex(i));
        break;
      } else {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "could not open game controller: %s", SDL_GetError());
      }
    }
  }
#endif

  // main loop
  current_time = TickCount();
  last_time = TickCount();

#ifndef VSTCM
  while (!should_quit)
    mainloop();  // When running on VSTCM this will be called from loop function in .INO

  if (controller != NULL)
    SDL_GameControllerClose(controller);

  SDL_DestroyTexture(text_orig);
  SDL_DestroyRenderer(rend_2D_orig);
  SDL_DestroyWindow(window_orig);
  SDL_CloseAudioDevice(audio_device);
  SDL_Quit();
#else
  // free(p);
#endif
}

// If running on PC then we need a main() function to replace loop() in the .INO
#ifndef VSTCM
int main(int argc, char** argv) {
  vstcm_setup();
  return 0;
}
#endif
