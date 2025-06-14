/*
   VSTCM

   Vector Signal Transceiver Colour Mod using MCP4922 DACs on the Teensy 4.1

   Code to manage onboard and IR remote control buttons

   IMPROVED BUTTON HANDLING - Integrates with new Menu struct system

*/
#include "main.h"

#ifdef VSTCM
#include <Bounce2.h>
#include <Audio.h>

#else
#include <cstdint>
#include <cstring>
#include SDL_PATH
#endif

#include "buttons.h"
#include "settings.h"

#ifdef IR_REMOTE
#define SUPPRESS_ERROR_MESSAGE_FOR_BEGIN
#include <IRremote.hpp>
#endif

#ifdef VSTCM
// Bounce objects to read five pushbuttons (pins 0-4)
Bounce button0;
Bounce button1;
Bounce button2;
Bounce button3;
Bounce button4;
#endif

const uint8_t DEBOUNCE_INTERVAL = 25;  // Measured in ms

bool has_focus = false;
static bool menu_toggle_armed = false;

extern bool should_quit;
extern int show_vstcm_settings;

// External menu system variables
extern Menu g_main_menu;
extern Menu g_settings_menu;
extern Menu g_games_menu;
extern Menu* g_current_menu;

#ifdef VSTCM
extern AudioPlaySdWav playWav1;
#endif

extern int vecsim(char*);
extern bool cinemu_setup(const char*);

void buttons_setup() {
#ifdef VSTCM
    button0 = Bounce();
    button1 = Bounce();
    button2 = Bounce();
    button3 = Bounce();
    button4 = Bounce();

    button0.attach(0, INPUT_PULLUP);
    button0.interval(DEBOUNCE_INTERVAL);
    button1.attach(1, INPUT_PULLUP);
    button1.interval(DEBOUNCE_INTERVAL);
    button2.attach(2, INPUT_PULLUP);
    button2.interval(DEBOUNCE_INTERVAL);
    button3.attach(3, INPUT_PULLUP);
    button3.interval(DEBOUNCE_INTERVAL);
    button4.attach(4, INPUT_PULLUP);
    button4.interval(DEBOUNCE_INTERVAL);

    // Set up pins for input/output using external PCB connector to Asteroids arcade harness
    pinMode(BUTT1, INPUT_PULLUP);  // Fire
    pinMode(BUTT2, INPUT_PULLUP);  // Player 2 signal
    pinMode(BUTT3, INPUT_PULLUP);  // Player 1 signal
    pinMode(BUTT4, INPUT_PULLUP);  // Rotate right
    pinMode(BUTT5, INPUT_PULLUP);  // Rotate left
    pinMode(BUTT6, INPUT_PULLUP);  // Hyperspace
    pinMode(SW1, INPUT_PULLUP);    // Thrust
    pinMode(SW2, INPUT_PULLUP);    // Start 2 player
    pinMode(SW3, INPUT_PULLUP);    // Left coin in
    pinMode(SW4, INPUT_PULLUP);    // Centre coin in
    pinMode(SW5, INPUT_PULLUP);    // Start 1 player
    pinMode(P1_LED, OUTPUT);       // Player 1 LED
    digitalWrite(P1_LED, HIGH);    // TEST TO LIGHT UP PLAYER 1 LED
    pinMode(P2_LED, OUTPUT);       // Player 2 LED
    digitalWrite(P2_LED, HIGH);
#endif
}

extern int menu_is_active;

// New unified button handling function
void manage_buttons() {
   // Serial.println("manage_buttons");
#ifdef VSTCM
    int com = 0;  // Command received from IR remote

#ifdef IR_REMOTE
    if (IrReceiver.decode()) {
        IrReceiver.resume();
        com = IrReceiver.decodedIRData.command;
    }
#endif

    // Update all the button objects
    button0.update();
    button1.update();
    button2.update();
    button3.update();
    button4.update();

 // DEBUG: Check if any buttons are pressed (add this temporarily)
 /* if (button0.fell() || button1.fell() || button2.fell() || button3.fell() || button4.fell()) {
    // If you have Serial available, print which button was pressed
    if (button0.fell()) Serial.println("Button 0 pressed");
    if (button1.fell()) Serial.println("Button 1 pressed");
    if (button2.fell()) Serial.println("Button 2 pressed");
    if (button3.fell()) Serial.println("Button 3 pressed");
    if (button4.fell()) Serial.println("Button 4 pressed");
  }*/

    // Handle menu navigation using new menu system
   // if (show_vstcm_settings == SPLASH_MENU || show_vstcm_settings == SETTINGS_MENU) {
    if (menu_is_active) {
        // Map buttons to input keys
        int input_key = 0;

        if (button4.fell() || com == 0x18) {  // SW5 Up button
            input_key = INPUT_KEY_UP;
        }
        else if (button0.fell() || com == 0x52) {  // SW2 Down button
            input_key = INPUT_KEY_DOWN;
        }
        else if (button3.fell() || com == 0x08) {  // SW3 Left button
            input_key = INPUT_KEY_LEFT;
        }
        else if (button1.fell() || com == 0x5A) {  // SW4 Right button
            input_key = INPUT_KEY_RIGHT;
        }
        else if (button2.fell() || com == 0x1C) {  // SW3 Middle button or OK on IR remote
            input_key = INPUT_KEY_SELECT;
        }

        // Handle the input using the new menu system
        if (input_key != 0) {
            handle_menu_input(input_key);
        }
    }

#else
    // SDL/Windows input handling
    SDL_Event e;
    bool keydown = false;
    while (SDL_PollEvent(&e) != 0) {
        if (e.type == SDL_QUIT) {
            should_quit = true;
        }
        else if (e.type == SDL_WINDOWEVENT) {
            if (e.window.event == SDL_WINDOWEVENT_FOCUS_GAINED) {
                has_focus = true;
            }
            else if (e.window.event == SDL_WINDOWEVENT_FOCUS_LOST) {
                has_focus = false;
            }
        }
        else if (e.type == SDL_KEYDOWN && e.key.repeat == 0 && keydown == false) {
            keydown = true;

            int input_key = 0;

            switch (e.key.keysym.scancode) {
            case SDL_SCANCODE_1:  // Switch to settings menu
                g_current_menu = &g_settings_menu;
                show_vstcm_settings = SETTINGS_MENU;
                break;
            case SDL_SCANCODE_ESCAPE:
            case SDL_SCANCODE_2:  // Switch to games menu
                g_current_menu = &g_games_menu;
                show_vstcm_settings = SPLASH_MENU;
                break;

            case SDL_SCANCODE_UP:
                input_key = INPUT_KEY_UP;
                break;
            case SDL_SCANCODE_DOWN:
                input_key = INPUT_KEY_DOWN;
                break;
            case SDL_SCANCODE_LEFT:
                input_key = INPUT_KEY_LEFT;
                break;
            case SDL_SCANCODE_RIGHT:
                input_key = INPUT_KEY_RIGHT;
                break;
            case SDL_SCANCODE_RETURN:
            case SDL_SCANCODE_M:
                input_key = INPUT_KEY_SELECT;
                break;
            case SDL_SCANCODE_BACKSPACE:
                input_key = INPUT_KEY_BACK;
                break;
            case SDL_SCANCODE_TAB:
                break;
            default:
                break;
            }

            // Handle the input using the new menu system
            if (input_key != 0) {
                handle_menu_input(input_key);
            }
        }
        else if (e.type == SDL_KEYUP) {
            keydown = false;
        }
    }
#endif
}



// New button setup function with improved organization
void buttons_setup_new() {
    buttons_setup();

    // Initialize menu system
    setup_all_menus();

    // Load configuration
    read_vstcm_config();

    // Set initial menu state
    g_current_menu = &g_main_menu;
    show_vstcm_settings = SPLASH_MENU;  // Start with games menu
}

// Function to get current button state for external use
int get_button_press_new() {
    manage_buttons();

    // Return current input state if needed by other parts of the system
    // This can be expanded based on requirements
    return 0;
}

// IR Remote setup function
void IR_remote_setup() {
#ifdef IR_REMOTE
#ifdef VSTCM
    IrReceiver.begin(IR_RECEIVE_PIN, ENABLE_LED_FEEDBACK);
#endif
#endif
}

// Function to check if menu is currently active
bool is_menu_active() {
    return (show_vstcm_settings == SPLASH_MENU || show_vstcm_settings == SETTINGS_MENU);
}

// Function to toggle menu on/off
void toggle_menu() {
    if (show_vstcm_settings == NO_MENU) {
        g_current_menu = &g_main_menu;
        show_vstcm_settings = SPLASH_MENU;
    }
    else {
        show_vstcm_settings = NO_MENU;
    }
}

// Function to get current menu type for compatibility
int get_current_menu_type() {
    if (g_current_menu == &g_settings_menu) {
        return SETTINGS_MENU;
    }
    else if (g_current_menu == &g_games_menu) {
        return SPLASH_MENU;
    }
    return NO_MENU;
}

// Function to set menu type for compatibility
void set_menu_type(int menu_type) {
    switch (menu_type) {
    case SETTINGS_MENU:
        g_current_menu = &g_settings_menu;
        show_vstcm_settings = SETTINGS_MENU;
        break;
    case SPLASH_MENU:
        g_current_menu = &g_games_menu;
        show_vstcm_settings = SPLASH_MENU;
        break;
    case NO_MENU:
    default:
        show_vstcm_settings = NO_MENU;
        break;
    }
}

