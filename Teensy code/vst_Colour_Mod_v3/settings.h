/*
   VSTCM

   Vector Signal Transceiver Colour Mod using MCP4922 DACs on the Teensy 4.1

   Code to read and write settings to SD card and draw menus on screen

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
const int COLOUR_SWITCH = 1;  // 1 = Colour display, 0 = monochrome
// how long in milliseconds to wait for data before displaying a test pattern
// this can be increased if the test pattern appears during gameplay
const int SERIAL_WAIT_TIME = 150;
const int AUDIO_PIN = 10;  // Connect audio output to GND and pin 10

#define IR_RECEIVE_PIN 32  //Put this outside the ifdef so that it doesn't break the menu

enum {settings, emu_vecsim, emu_cinemu, emu_6809, emu_68000};    // Possible values for emulator

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


// Forward declaration for Menu struct
struct Menu;

typedef enum {
    MENU_ITEM_TYPE_NUMERIC,
    MENU_ITEM_TYPE_BOOLEAN,
    MENU_ITEM_TYPE_ACTION,    // For items that trigger a custom function
    MENU_ITEM_TYPE_SUB_MENU   // For items that open another menu
} MenuItemType;

typedef struct MenuItem {
    char label[40];
    MenuItemType type;
    int32_t value;          // Current value for NUMERIC, 0 or 1 for BOOLEAN
    int32_t min_val;        // For NUMERIC type
    int32_t max_val;        // For NUMERIC type
    void (*action_func)(struct MenuItem* item, void* context); // Function pointer for ACTION type or on-select
    // The context can be used to pass global state or the current menu
    struct Menu* sub_menu;  // Pointer to a sub-menu for SUB_MENU type
    char ini_label[20];     // For config saving
    uint8_t emulator;       // Specific to game selection
} MenuItem;

#define MAX_MENU_ITEMS 30 // Define a maximum, similar to NB_SETTINGS

typedef struct Menu {
    char title[40];
    MenuItem items[MAX_MENU_ITEMS];
    int item_count;
    int selected_item_index;
    int display_offset;      // For scrolling
    struct Menu* parent_menu; // To navigate back
} Menu;


//
// Function prototypes
//
void read_vstcm_config();
void write_vstcm_config();
void make_test_pattern();
void moveto(int, int, int, int, int, int);
void draw_test_pattern(int);

// --- Menu Function Prototypes ---
void menu_item_increment(MenuItem* item);
void menu_item_decrement(MenuItem* item);
void menu_item_execute_action(MenuItem* item, void* context);
void menu_init(Menu* menu, const char* title, Menu* parent);
void menu_add_item(Menu* menu, MenuItem item);
void menu_navigate_down(Menu* menu);
void menu_navigate_up(Menu* menu);
void setup_all_menus();
void handle_menu_input(int input_key);
void draw_menu_on_screen(Menu* menu);

MenuItem* find_menu_item_by_ini_label(Menu* menu, const char* ini_label);
MenuItem* menu_get_selected_item(Menu* menu);

#endif