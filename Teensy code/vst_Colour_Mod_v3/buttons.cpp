/*
   VSTCM

   Vector Signal Transceiver Colour Mod using MCP4922 DACs on the Teensy 4.1

   Code to manage onboard and IR remote control buttons

*/

#include <Bounce2.h>
#include <Audio.h>
#include "settings.h"
#ifdef IR_REMOTE
#define SUPPRESS_ERROR_MESSAGE_FOR_BEGIN
#include <IRremote.hpp>
#endif

// Bounce objects to read five pushbuttons (pins 0-4)
Bounce button0;
Bounce button1;
Bounce button2;
Bounce button3;
Bounce button4;

const uint8_t DEBOUNCE_INTERVAL = 25;  // Measured in ms

extern params_t v_setting[NB_SETTINGS];
extern int sel_setting;  // Currently selected setting
extern params_t v_splash[NB_SPLASH_CHOICES];
extern bool show_vstcm_splash;
extern bool show_vstcm_settings;
extern int sel_splash;           // Currently selected splash screen menu choice
extern AudioPlaySdWav playWav1;  //xy=137,426
extern void bzone();
void cinemu_setup(const char *);

void buttons_setup() {
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
}

void manage_buttons() {
  // Use the buttons on the PCB or IR remote to adjust and save settings

  int com = 0;  // Command received from IR remote

#ifdef IR_REMOTE
  if (IrReceiver.decode())  // Check if a button has been pressed on the IR remote
  {
    IrReceiver.resume();  // Enable receiving of the next value
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

  if (show_vstcm_splash)  // The splash screen is active, so manage buttons accordingly
  {
    if (button4.fell() || com == 0x18)  // SW5 Up button - go up list of options and loop around
    {
      //  playWav1.play("roms/Battlezone/explode3.wav");
      if (sel_splash-- < 1)
        sel_splash = (NB_SPLASH_CHOICES - 1);
    }

    if (button0.fell() || com == 0x52)  // SW2 Down button - go down list of options and loop around
    {
      //  playWav1.play("roms/Battlezone/explode3.wav");
      sel_splash++;
      if (sel_splash > (NB_SPLASH_CHOICES - 1))
        sel_splash = 0;
    }

    if (button2.fell() || com == 0x1C)  // SW3 Middle button or OK on IR remote
    {
      //  playWav1.play("roms/Battlezone/explode3.wav");
      // Act on the OK button being pressed depending on the selected menu option
      show_vstcm_splash = false;  // Stop showing splash screen, something else has been selected

      // only one choice for now for testing purposes
      if (!memcmp(v_splash[sel_splash].ini_label, "SETTINGS", 8))
        show_vstcm_settings = true;
      else if (!memcmp(v_splash[sel_splash].ini_label, "BZONE", 5)) {
        bzone();
        // After Battlezone ends, we return here 
        show_vstcm_splash = true;  // Stop showing splash screen, something else has been selected
        show_vstcm_settings = false;
      }
      else {
        // Launch the selected Cinematronics game
        cinemu_setup(v_splash[sel_splash].ini_label);

        // After the selected Cinematronics game ends, we return here 
        show_vstcm_splash = true;  // Stop showing splash screen, something else has been selected
        show_vstcm_settings = false;
      }
    }
  } else  // The settings screen is active, so manage buttons accordingly
  {
    if (button4.fell() || com == 0x18)  // SW5 Up button - go up list of options and loop around
    {
      //  playWav1.play("roms/Battlezone/explode3.wav");
      if (sel_setting-- < 0)
        sel_setting = 12;
    }

    if (button0.fell() || com == 0x52)  // SW2 Down button - go down list of options and loop around
    {
      //  playWav1.play("roms/Battlezone/explode3.wav");
      if (sel_setting++ > NB_SETTINGS - 1)
        sel_setting = 0;
    }

    if (button3.fell() || com == 0x08)  // SW3 Left button - decrease value of current parameter
    {
      //  playWav1.play("roms/Battlezone/explode3.wav");
      if (v_setting[sel_setting].pval > v_setting[sel_setting].min)
        v_setting[sel_setting].pval--;
    }

    if (button1.fell() || com == 0x5A)  // SW4 Right button - increase value of current parameter
    {
      //  playWav1.play("roms/Battlezone/explode3.wav");
      if (v_setting[sel_setting].pval < v_setting[sel_setting].max)
        v_setting[sel_setting].pval++;
    }

    if (button2.fell() || com == 0x1C)  // SW3 Middle button or OK on IR remote
    {
      // playWav1.play("roms/Battlezone/explode3.wav");
      write_vstcm_config();  // Update the settings on the SD card

      // Act on the OK button being pressed depending on the selected menu option
      show_vstcm_splash = true;  // Stop showing splash screen, something else has been selected

      // only one choice for now for testing purposes, also need to add return from settings screen choice

      show_vstcm_settings = false;
    }
  }
}

// An IR remote can be used instead of the onboard buttons, as the PCB
// may be mounted in a arcade cabinet, making it difficult to see changes on the screen
// when using the physical buttons

void IR_remote_setup() {
#ifdef IR_REMOTE

  // Start the receiver and if not 3. parameter specified,
  // take LED_BUILTIN pin from the internal boards definition as default feedback LED
  IrReceiver.begin(v_setting[11].pval, ENABLE_LED_FEEDBACK);

  // attachInterrupt(digitalPinToInterrupt(IR_RECEIVE_PIN), IR_remote_loop, CHANGE);
#endif
}