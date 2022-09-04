/*
   VSTCM

   Vector Signal Transceiver Colour Mod using MCP4922 DACs on the Teensy 4.1

   Code to manage onboard and IR remote control buttons

*/

#include <Bounce2.h>
#include "settings.h"
#ifdef IR_REMOTE
#define SUPPRESS_ERROR_MESSAGE_FOR_BEGIN
#include <IRremote.hpp>
#endif
// Bounce objects to read five pushbuttons (pins 0-4)
static Bounce button0 = Bounce();
static Bounce button1 = Bounce();
static Bounce button2 = Bounce();
static Bounce button3 = Bounce();
static Bounce button4 = Bounce();

const uint8_t DEBOUNCE_INTERVAL = 25;    // Measured in ms

extern params_t v_config[NB_PARAMS];
extern int opt_select;    // Currently selected setting

void buttons_setup()
{
  button0.attach(0, INPUT_PULLUP);        // Attach the debouncer to a pin and use internal pullup resistor
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

void manage_buttons()
{
  // Use the buttons on the PCB to adjust and save settings

  int com = 0;    // Command received from IR remote

#ifdef IR_REMOTE
  if (IrReceiver.decode())    // Check if a button has been pressed on the IR remote
  {
    IrReceiver.resume(); // Enable receiving of the next value
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

  if (button4.fell() || com == 0x18)          // SW5 Up button - go up list of options and loop around
  {
    if (opt_select -- < 0)
      opt_select = 12;
  }

  if (button0.fell() || com == 0x52)          // SW2 Down button - go down list of options and loop around
  {
    if (opt_select ++ > NB_PARAMS - 1)
      opt_select = 0;
  }

  if (button3.fell() || com == 0x08)          // SW3 Left button - decrease value of current parameter
  {
    if (v_config[opt_select].pval > v_config[opt_select].min)
      v_config[opt_select].pval --;
  }

  if (button1.fell() || com == 0x5A)          // SW4 Right button - increase value of current parameter
  {
    if (v_config[opt_select].pval < v_config[opt_select].max)
      v_config[opt_select].pval ++;
  }

  if (button2.fell() || com == 0x1C)          // SW3 Middle button or OK on IR remote
    write_vstcm_config();                    // Update the settings on the SD card
}

// An IR remote can be used instead of the onboard buttons, as the PCB
// may be mounted in a arcade cabinet, making it difficult to see changes on the screen
// when using the physical buttons

void IR_remote_setup()
{
#ifdef IR_REMOTE

  // Start the receiver and if not 3. parameter specified,
  // take LED_BUILTIN pin from the internal boards definition as default feedback LED
  IrReceiver.begin(v_config[11].pval, ENABLE_LED_FEEDBACK);

  // attachInterrupt(digitalPinToInterrupt(IR_RECEIVE_PIN), IR_remote_loop, CHANGE);
#endif
}
