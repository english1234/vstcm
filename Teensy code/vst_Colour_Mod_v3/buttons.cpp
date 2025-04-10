/*
   VSTCM

   Vector Signal Transceiver Colour Mod using MCP4922 DACs on the Teensy 4.1

   Code to manage onboard and IR remote control buttons

*/
#include "main.h"

#ifdef VSTCM
#include <Bounce2.h>
#include <Audio.h>
#include "buttons.h"
#else
#include <cstdint>
#include <cstring>
#include SDL_PATH
#endif

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

int menu_offset = 0;               // Start at the top of the list
const int MAX_VISIBLE_ITEMS = 18;  // Number of items shown at once

const uint8_t DEBOUNCE_INTERVAL = 25;  // Measured in ms

bool has_focus = false;

extern bool should_quit;  // already defined in main.cpp
extern params_t v_setting[2][NB_SETTINGS];
extern int sel_setting;  // Currently selected setting
extern int show_vstcm_settings;

#ifdef VSTCM
extern AudioPlaySdWav playWav1;  //xy=137,426
#endif

extern int vecsim(char *);
extern bool cinemu_setup(const char *);

void buttons_setup() {
#ifdef VSTCM
  button0 = Bounce();
  button1 = Bounce();
  button2 = Bounce();
  button3 = Bounce();
  button4 = Bounce();

  button0.attach(0, INPUT_PULLUP);  // Attach the debouncer to a pin and use internal pullup resistor
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
  digitalWrite(P1_LED, HIGH);    // TEST TO LIGHT UP PLAYER 1 LED TO JUST SEE IF IT WORKS
  pinMode(P2_LED, OUTPUT);       // Player 2 LED
  digitalWrite(P2_LED, HIGH);
#endif
}

void manage_buttons() {
#ifdef VSTCM
  // Use the buttons on the PCB or IR remote to adjust and save settings

  int com = 0;  // Command received from IR remote

#ifdef IR_REMOTE
  if (IrReceiver.decode()) {  // Check if a button has been pressed on the IR remote
    IrReceiver.resume();      // Enable receiving of the next value
    /*
       HX1838 Infrared Remote Control Module (£1/$1/1€ on Aliexpress)

       1     0x45 | 2     0x46 | 3     0x47
       4     0x44 | 5     0x40 | 6     0x43
       7     0x07 | 8     0x15 | 9     0x09
     * *     0x00 | 0     ???? | #     0x0D -> need to test value for 0
       OK    0x1C |
       Left  0x08 | Right 0x5A
       Up    0x18 | Down  0x52

    */
    com = IrReceiver.decodedIRData.command;
  }
#endif

  // Update all the button objects
  button0.update();
  button1.update();
  button2.update();
  button3.update();
  button4.update();

  if (show_vstcm_settings == SPLASH_MENU)  // The splash screen is active, so manage buttons accordingly
  {
    if (button4.fell() || com == 0x18)  // SW5 Up button
    {
      sel_setting--;

      // If selection moves past first item, wrap around
      if (sel_setting < 0) {
        sel_setting = NB_SPLASH_CHOICES - 1;
        menu_offset = NB_SPLASH_CHOICES - MAX_VISIBLE_ITEMS;  // Jump to bottom items
      } else if (sel_setting < menu_offset) {
        menu_offset--;  // Scroll up when reaching the top of the screen
      }
    }

    if (button0.fell() || com == 0x52)  // SW2 Down button
    {
      sel_setting++;

      // If selection moves past last item, wrap around
      if (sel_setting >= NB_SPLASH_CHOICES) {
        sel_setting = 0;
        menu_offset = 0;  // Reset scrolling when wrapping
      } else if (sel_setting >= menu_offset + MAX_VISIBLE_ITEMS) {
        menu_offset++;  // Scroll down when reaching the bottom of the screen
      }
    }

    if (button2.fell() || com == 0x1C)  // SW3 Middle button or OK on IR remote
    {
      // Act on the OK button being pressed depending on the selected menu option
      show_vstcm_settings = NO_MENU;  // Stop showing splash screen, something else has been selected

      // Choose either settings or the appropriate emulator for the selected game
      if (v_setting[SPLASH_MENU][sel_setting].emulator == settings)
        show_vstcm_settings = SETTINGS_MENU;

      if (v_setting[SPLASH_MENU][sel_setting].emulator == emu_vecsim) {
        vecsim(v_setting[SPLASH_MENU][sel_setting].ini_label);
        // After game ends, we return here
        show_vstcm_settings = SPLASH_MENU;
      }

      if (v_setting[SPLASH_MENU][sel_setting].emulator == emu_cinemu) {
        cinemu_setup(v_setting[SPLASH_MENU][sel_setting].ini_label);
        // After Battlezone ends, we return here
        show_vstcm_settings = SPLASH_MENU;
      }
    }
  } else  // The settings screen is active, so manage buttons accordingly
  {
    if (button4.fell() || com == 0x18)  // SW5 Up button - go up list of options and loop around
    {
      sel_setting--;
      if (sel_setting < 0)
        sel_setting = NB_SETTINGS - 1;
    }

    if (button0.fell() || com == 0x52)  // SW2 Down button - go down list of options and loop around
    {
      sel_setting++;
      if (sel_setting >= NB_SETTINGS)
        sel_setting = 0;
    }

    if (button3.fell() || com == 0x08)  // SW3 Left button - decrease value of current parameter
    {
      if (v_setting[SETTINGS_MENU][sel_setting].pval > v_setting[SETTINGS_MENU][sel_setting].min)
        v_setting[SETTINGS_MENU][sel_setting].pval--;
    }

    if (button1.fell() || com == 0x5A)  // SW4 Right button - increase value of current parameter
    {
      if (v_setting[SETTINGS_MENU][sel_setting].pval < v_setting[SETTINGS_MENU][sel_setting].max)
        v_setting[SETTINGS_MENU][sel_setting].pval++;
    }

    if (button2.fell() || com == 0x1C)  // SW3 Middle button or OK on IR remote
    {
      write_vstcm_config();  // Update the settings on the SD card
      show_vstcm_settings = SPLASH_MENU;
    }
  }
#else
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
    }
    // check on repeat is to debounce (RC)
    else if (e.type == SDL_KEYDOWN && e.key.repeat == 0 && keydown == false) {
      keydown = true;  // another attempt to debounce
      switch (e.key.keysym.scancode) {

        case SDL_SCANCODE_1:  // Stop showing splash screen, settings has been selected
          show_vstcm_settings = SETTINGS_MENU;
          sel_setting = 0;
          break;
        case SDL_SCANCODE_ESCAPE:
        case SDL_SCANCODE_2:  // Stop showing settings screen, splash screen has been selected
          show_vstcm_settings = SPLASH_MENU;
          sel_setting = 0;
          break;

        case SDL_SCANCODE_UP:
          if (sel_setting-- < 1)
            sel_setting = 0;
          break;  // up
        case SDL_SCANCODE_DOWN:
          if (sel_setting++ > (NB_SPLASH_CHOICES - 1))
            sel_setting = 0;
          break;  // down
        case SDL_SCANCODE_LEFT:
          if (v_setting[SETTINGS_MENU][sel_setting].pval > v_setting[SETTINGS_MENU][sel_setting].min)
            v_setting[SETTINGS_MENU][sel_setting].pval--;
          break;  // left
        case SDL_SCANCODE_RIGHT:
          if (v_setting[SETTINGS_MENU][sel_setting].pval < v_setting[SETTINGS_MENU][sel_setting].max)
            v_setting[SETTINGS_MENU][sel_setting].pval++;
          break;  // right

          // nb THIS IS DETECTED AS A US KEYBOARD, SO IT WILL BE THE ? KEY ON A FRENCH KEYBOARD!!!
        case SDL_SCANCODE_RETURN:
        case SDL_SCANCODE_M:  // Act on the OK button being pressed depending on the selected menu option

          // Act on the OK button being pressed depending on the selected menu option
          show_vstcm_settings = NO_MENU;  // Stop showing splash screen, something else has been selected

          // Choose either settings or the appropriate emulator for the selected game
          if (v_setting[SPLASH_MENU][sel_setting].emulator == settings)
            show_vstcm_settings = SETTINGS_MENU;

          if (v_setting[SPLASH_MENU][sel_setting].emulator == emu_vecsim) {
            vecsim(v_setting[SPLASH_MENU][sel_setting].ini_label);
            // After game ends, we return here
            show_vstcm_settings = SPLASH_MENU;
          }

          if (v_setting[SPLASH_MENU][sel_setting].emulator == emu_cinemu) {
            cinemu_setup(v_setting[SPLASH_MENU][sel_setting].ini_label);
            // After Battlezone ends, we return here
            show_vstcm_settings = SPLASH_MENU;
          }

          break;
        case SDL_SCANCODE_TAB: break;
        default: break;
      }
    } else if (e.type == SDL_KEYUP) {
      keydown = false;  // Reset keydown flag
      switch (e.key.keysym.scancode) {
        case SDL_SCANCODE_RETURN:
        case SDL_SCANCODE_UP:    //if (sel_setting-- < 1)
                                 //   sel_splash = (NB_SPLASH_CHOICES - 1); break;        // up
        case SDL_SCANCODE_DOWN:  //if (sel_splash++ > (NB_SPLASH_CHOICES - 1))
                                 //   sel_splash = 0; break;    // down
        case SDL_SCANCODE_LEFT:
          if (v_setting[SETTINGS_MENU][sel_setting].pval > v_setting[SETTINGS_MENU][sel_setting].min)
            //   v_setting[SETTINGS_MENU][sel_setting].pval--; break;    // left
          case SDL_SCANCODE_RIGHT:
            if (v_setting[SETTINGS_MENU][sel_setting].pval < v_setting[SETTINGS_MENU][sel_setting].max)
            //    v_setting[SETTINGS_MENU][sel_setting].pval++; break;  // right
            case SDL_SCANCODE_TAB:

              // clear the queued audio to avoid audio delays
              //   SDL_ClearQueuedAudio(audio_device);
              break;
        default: break;
      }
    } else if (e.type == SDL_CONTROLLERBUTTONDOWN) {
      switch (e.cbutton.button) {
        case SDL_CONTROLLER_BUTTON_A: break;
        case SDL_CONTROLLER_BUTTON_DPAD_UP: break;
        case SDL_CONTROLLER_BUTTON_DPAD_DOWN: break;
        case SDL_CONTROLLER_BUTTON_DPAD_LEFT: break;
        case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: break;
        case SDL_CONTROLLER_BUTTON_START: break;
        case SDL_CONTROLLER_BUTTON_BACK: break;
      }
    } else if (e.type == SDL_CONTROLLERBUTTONUP) {
      switch (e.cbutton.button) {
        case SDL_CONTROLLER_BUTTON_A: break;
        case SDL_CONTROLLER_BUTTON_DPAD_UP: break;
        case SDL_CONTROLLER_BUTTON_DPAD_DOWN: break;
        case SDL_CONTROLLER_BUTTON_DPAD_LEFT: break;
        case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: break;
        case SDL_CONTROLLER_BUTTON_START: break;
        case SDL_CONTROLLER_BUTTON_BACK: break;
      }
    } else if (e.type == SDL_CONTROLLERAXISMOTION) {
      /*  switch (e.caxis.axis) {
        case SDL_CONTROLLER_AXIS_LEFTX:
            if (e.caxis.value < -CONTROLLER_DEADZONE) {
               
            }
            else if (e.caxis.value > CONTROLLER_DEADZONE) {
                
            }
            else {
                
            }
            break;
        case SDL_CONTROLLER_AXIS_LEFTY:
            if (e.caxis.value < -CONTROLLER_DEADZONE) {
               
            }
            else if (e.caxis.value > CONTROLLER_DEADZONE) {
               
            }
            else {
                
            }
            break;
        }*/
    }
    /*  else if (e.type == SDL_CONTROLLERDEVICEADDED) {
        const int controller_id = e.cdevice.which;
        if (controller == NULL && SDL_IsGameController(controller_id)) {
            controller = SDL_GameControllerOpen(controller_id);
        }
    }
    else if (e.type == SDL_CONTROLLERDEVICEREMOVED) {
        if (controller != NULL) {
            SDL_GameControllerClose(controller);
            controller = NULL;
        }
    }*/
  }
#endif
}

// An IR remote can be used instead of the onboard buttons, as the PCB
// may be mounted in a arcade cabinet, making it difficult to see changes on the screen
// when using the physical buttons

void IR_remote_setup() {
#ifdef VSTCM
#ifdef IR_REMOTE

  // Start the receiver and if not 3. parameter specified,
  // take LED_BUILTIN pin from the internal boards definition as default feedback LED
  IrReceiver.begin(v_setting[11].pval, ENABLE_LED_FEEDBACK);
  attachInterrupt(digitalPinToInterrupt(v_setting[SETTINGS_MENU][11].pval), IR_remote_loop, CHANGE);
#endif
#endif
}