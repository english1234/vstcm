/************************************************************

  VSTCM firmware v3 for v3 PCB

  Robin Champion - March 2023

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

volatile bool overlay_settings = false;

#ifdef VSTCM
extern Bounce button1;
extern Bounce button3;
#endif

//For spot killer fix - if the total distance in x or y is less than SPOT_MAX, it will go to the corners to try to stop
//the spot killer from triggering
const int SPOT_MAX = 3400;
const int SPOT_GOTOMAX = 4076;
const int SPOT_GOTOMIN = 20;

bool spot_triggered;

//EXPERIMENTAL automatic draw rate adjustment based on how much idle time there is between frames
//Defines and global for the auto-speed feature
#define NORMAL_SHIFT_SCALING 2.0
#define MAX_DELTA_SHIFT 6  // These are the limits on the auto-shift for speeding up drawing complex frames
#define MIN_DELTA_SHIFT -3
#define DELTA_SHIFT_INCREMENT 0.1
#define SPEEDUP_THRESHOLD_MS 2   // If the dwell time is less than this then the drawing rate will try to speed up (lower resolution)
#define SLOWDOWN_THRESHOLD_MS 8  // If the dwell time is greater than this then the drawing rate will slow down (higher resolution)
//If the thresholds are too close together there can be "blooming" as the rate goes up and down too quickly - maybe make it limit the
//speed it can change??
float delta_shift = 0;

long fps;  // Approximate FPS used to benchmark code performance improvements

#ifdef VSTCM
volatile int show_vstcm_settings;  // Shows settings screen if true
volatile bool show_something;      // Shows either settings or splash screen if true
#else
int show_vstcm_settings;  // Shows settings screen if true
bool show_something;      // Shows either settings or splash screen if true

// THIS VARIABLE CAN BE REPLACED BY show_vstcm_settings = NO_MENU?

#endif

unsigned long dwell_time = 0;
//static uint8_t gRed, gGreen, gBlue;  // Global variables to store current draw colour
int gX, gY;  // Last position of beam
static uint32_t current_time = 0;
static uint32_t last_time = 0;
static uint32_t dt = 0;

extern int frame_max_x;
extern int frame_min_x;
extern int frame_max_y;
extern int frame_min_y;
extern float line_draw_speed;
extern params_t v_setting[2][NB_SETTINGS];

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
  // fprintf( stderr, "The supported games are:\n" );
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

// Code for a screen saver similar to Mystify

#define NUM_LINES 4  // Number of bouncing lines
#define Z_MAX 255    // Maximum Z depth value
#define Z_SPEED 0.1  // Speed of Z oscillation

typedef struct {
  int x1, y1, x2, y2;      // Line endpoints
  int dx1, dy1, dx2, dy2;  // Velocities for each endpoint
  int color;               // RGB565 packed color value
  int z;                   // Z-depth (0-255)
  float z_phase;           // Phase for Z oscillation
} Line;

// Array of bouncing lines
Line lines[NUM_LINES];

// Function to convert RGB to a 16-bit color (assuming 5-6-5 format)
uint16_t rgb_to_565(uint8_t r, uint8_t g, uint8_t b) {
  return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}

// Initialize lines with random positions, velocities, colors, and Z-depth
void init_mystify() {
  for (int i = 0; i < NUM_LINES; i++) {
    unsigned int r;

#ifdef VSTCM
    // Use standard rand() on Teensy
    lines[i].x1 = (rand() % (4096 - 2)) + 1;
    lines[i].y1 = (rand() % (4096 - 2)) + 1;
    lines[i].x2 = (rand() % (4096 - 2)) + 1;
    lines[i].y2 = (rand() % (4096 - 2)) + 1;
#else
    // Use secure rand_s() on Windows
    rand_s(&r);
    lines[i].x1 = (r % (4096 - 2)) + 1;
    rand_s(&r);
    lines[i].y1 = (r % (4096 - 2)) + 1;
    rand_s(&r);
    lines[i].x2 = (r % (4096 - 2)) + 1;
    rand_s(&r);
    lines[i].y2 = (r % (4096 - 2)) + 1;
#endif

    // Random direction (-1 or 1)
    lines[i].dx1 = (rand() % 2) ? 1 : -1;
    lines[i].dy1 = (rand() % 2) ? 1 : -1;
    lines[i].dx2 = (rand() % 2) ? 1 : -1;
    lines[i].dy2 = (rand() % 2) ? 1 : -1;

    // Random color and Z depth
#ifdef VSTCM
    lines[i].color = rand() % 256;  // 256 color range
    lines[i].z = rand() % 256;      // Z value (0-255)
#else
    rand_s(&r);
    lines[i].color = r % 256;
    rand_s(&r);
    lines[i].z = r % 256;
#endif
  }
}

// Update the positions of the lines, their colors, and z-depth
void update_mystify() {
  static float t = 0;  // Time variable for smooth Z oscillation

  for (int i = 0; i < NUM_LINES; i++) {
    // Move each endpoint
    lines[i].x1 += lines[i].dx1;
    lines[i].y1 += lines[i].dy1;
    lines[i].x2 += lines[i].dx2;
    lines[i].y2 += lines[i].dy2;

    // Bounce off the edges
    if (lines[i].x1 <= 0 || lines[i].x1 >= 4096) lines[i].dx1 = -lines[i].dx1;
    if (lines[i].y1 <= 0 || lines[i].y1 >= 4096) lines[i].dy1 = -lines[i].dy1;
    if (lines[i].x2 <= 0 || lines[i].x2 >= 4096) lines[i].dx2 = -lines[i].dx2;
    if (lines[i].y2 <= 0 || lines[i].y2 >= 4096) lines[i].dy2 = -lines[i].dy2;

    // Change color smoothly over time
    uint8_t r = (lines[i].color >> 11) & 0x1F;
    uint8_t g = (lines[i].color >> 5) & 0x3F;
    uint8_t b = (lines[i].color) & 0x1F;

    r = (r + 1) % 32;
    g = (g + 1) % 64;
    b = (b + 1) % 32;

    lines[i].color = rgb_to_565(r << 3, g << 2, b << 3);

    // Update Z-depth dynamically using a sine wave function
    lines[i].z = (int)((sinf(t + lines[i].z_phase * 6.28) * 0.5 + 0.5) * Z_MAX);
  }

  t += Z_SPEED;  // Increment time for Z-depth animation
}

//extern inline void draw_line( int32_t x1, int32_t y1, int32_t x2, int32_t y2, int color, int z );

// Render the lines on the vector screen with colors and z-depth
void draw_mystify() {
  for (int i = 0; i < NUM_LINES; i++) {
    //draw_line(lines[i].x1, lines[i].y1, lines[i].x2, lines[i].y2, lines[i].color, lines[i].z);
    draw_moveto(lines[i].x1, lines[i].y1);
    draw_to_xyrgb(lines[i].x2, lines[i].y2, 128, 128, 128);
  }
}

// Wrapper function to enable calling by VSTCM without creating confusion about
// location of main() function (allows compilation on either Teensy or Visual Studio)
void mainloop() {

  current_time = TickCount();

  dt = current_time - last_time;

#ifdef VSTCM
  elapsedMicros waiting;  // Auto updating, used for FPS calculation
#else
  unsigned long waiting;
#endif

  unsigned long draw_start_time, loop_start_time;
  int serial_flag = 0;


  // DO THESE NEED TO BE UPDATED HERE ON EVERY LOOP???

  // This is specific to some code to manage a spot killer - perhaps needs to be an option in the settings
  frame_max_x = 0;
  frame_min_x = 4095;
  frame_max_y = 0;
  frame_min_y = 4095;

  loop_start_time = TickCount();

#ifdef VSTCM
  if (!Serial) {
    read_data(1);  //init read_data if the serial port is not open
    Serial.flush();
  }

  draw_start_time = 0;  // Just to prevent a compiler warning

  while (1) {
    if (Serial.available()) {
      if (serial_flag == 0) {
        draw_start_time = millis();
        serial_flag = 1;
      }

      if (!overlay_settings)
        show_something = false;  // Turn off splash or settings screen

      if (read_data(0) == 1) {  // Try to read some incoming data from MAME

#ifdef VSTCM
        button1.update();
        button3.update();

        static bool toggle_armed = false;

        if (button1.read() == LOW && button3.read() == LOW && !toggle_armed) {
          overlay_settings = !overlay_settings;
          toggle_armed = true;
        }

        if (button1.read() == HIGH || button3.read() == HIGH) {
          toggle_armed = false;
        }

#else
        static bool left_down = false;
        static bool right_down = false;
        static bool toggle_armed = false;

        SDL_Event e;
        while (SDL_PollEvent(&e)) {
          if (e.type == SDL_KEYDOWN && e.key.repeat == 0) {
            if (e.key.keysym.scancode == SDL_SCANCODE_LEFT) left_down = true;
            if (e.key.keysym.scancode == SDL_SCANCODE_RIGHT) right_down = true;

            if (left_down && right_down && !toggle_armed) {
              overlay_settings = !overlay_settings;
              toggle_armed = true;

              if (overlay_settings)
                printf("Overlay ON\n");
              else
                printf("Overlay OFF\n");
            }
          }
          if (e.type == SDL_KEYUP) {
            if (e.key.keysym.scancode == SDL_SCANCODE_LEFT) left_down = false;
            if (e.key.keysym.scancode == SDL_SCANCODE_RIGHT) right_down = false;

            if (!left_down || !right_down) {
              toggle_armed = false;
            }
          }
        }
#endif

        break;
      }
    } else if ((millis() - loop_start_time) > v_setting[SETTINGS_MENU][15].pval)
      show_something = true;  // Show splash screen

    if (show_something)
      break;
  }

  dwell_time = draw_start_time - loop_start_time;  //This is how long it waited after drawing a frame - better than FPS for tuning
#else
  show_something = true;
#endif

#ifndef VSTCM
  SDL_RenderClear(rend_2D_orig);
#endif

  if (show_something || overlay_settings) {
    delta_shift = 0;
    line_draw_speed = (float)v_setting[SETTINGS_MENU][5].pval / NORMAL_SHIFT_SCALING + 3.0;  //Make things a little bit faster for the menu

    if (overlay_settings)
      show_vstcm_menu_screen(SETTINGS_MENU);
    else {
      show_vstcm_menu_screen(show_vstcm_settings);
      update_mystify();
      draw_mystify();
    }
  } else {
    if (dwell_time < SPEEDUP_THRESHOLD_MS) {
      delta_shift += DELTA_SHIFT_INCREMENT;

      if (delta_shift > MAX_DELTA_SHIFT)
        delta_shift = MAX_DELTA_SHIFT;
    }
    //Try to only allow speedups
    //   else if (dwell_time > SLOWDOWN_THRESHOLD_MS) {
    //     delta_shift -= DELTA_SHIFT_INCREMENT;
    //     if (delta_shift < MIN_DELTA_SHIFT) delta_shift = MIN_DELTA_SHIFT;
    //   }

    line_draw_speed = (float)v_setting[SETTINGS_MENU][5].pval / NORMAL_SHIFT_SCALING + delta_shift;

    if (line_draw_speed < 1)
      line_draw_speed = 1;
  }

  // Go to the center of the screen, turn the beam off (prevents stray coloured lines from appearing)
  brightness(0, 0, 0);
  dwell(v_setting[SETTINGS_MENU][3].pval);

  // This is only needed to handle dwell times on some real vector monitors
#ifdef VSTCM
  if (!show_something) {
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
        draw_moveto(SPOT_GOTOMAX, SPOT_GOTOMAX);  //If we have time, do the moves again
      SPI_flush();
      delayMicroseconds(200);
      draw_moveto(SPOT_GOTOMIN, SPOT_GOTOMIN);  //Try to move back to the min again
      SPI_flush();
      delayMicroseconds(200);
    } else spot_triggered = false;
  }

  goto_xy(REST_X, REST_Y);
  SPI_flush();
#endif

  if (show_something || overlay_settings)  // If we are not playing MAME, we need to show one of the menu screens instead
    manage_buttons();                      // Moved here to avoid bright spot on the monitor when doing SD card operations

#ifdef VSTCM
  fps = 1000000 / waiting;

  if (show_something)
    delay(5);  // The 6100 monitor likes to spend some time in the middle
  else
    delayMicroseconds(100);  // Wait 100 microseconds in the center if displaying a game (tune this?)
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

  init_gamma();         // Set up gamma colour table
  read_vstcm_config();  // Read saved settings from Teensy SD card
  IR_remote_setup();    // Configure the infra red remote control, if present
  buttons_setup();      // Configure control buttons on vstcm or PC
  SPI_init();           // Set up pins and SPI registers on Teensy
  make_test_pattern();  // Prepare buffer of data to draw test patterns faster
  init_mystify();

  line_draw_speed = (float)v_setting[SETTINGS_MENU][5].pval / NORMAL_SHIFT_SCALING;
  show_something = true;
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
