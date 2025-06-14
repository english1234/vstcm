/*
   VSTCM

   Vector Signal Transceiver Colour Mod using MCP4922 DACs on the Teensy 4.1

   Code to manage onboard and IR remote control buttons

*/

#ifndef _buttons_h_
#define _buttons_h_

//AUDIO1      Output 1 from PT8211 DAC
//AUDIO2      Output 2 from PT8211 DAC
#define BUTT1   33  // Fire
#define BUTT2   34  // Player 2 signal
#define BUTT3   35  // Player 1 signal
#define BUTT4   36  // Rotate right
#define BUTT5   37  // Rotate left
#define BUTT6   40  // Hyperspace
#define SW1      3  // Thrust
#define SW2      0  // Start 2 player
#define SW3      2  // Left coin in
#define SW4      1  // Centre coin in
#define SW5      4  // Start 1 player
#define P1_LED  10  // Player 1 LED
#define P2_LED  25  // Player 2 LED

// Define input key constants (should match those in menu.h and mainloop)
#define INPUT_KEY_UP          1
#define INPUT_KEY_DOWN        2
#define INPUT_KEY_LEFT        3
#define INPUT_KEY_RIGHT       4
#define INPUT_KEY_SELECT      5
#define INPUT_KEY_BACK        6
#define INPUT_KEY_MENU_TOGGLE 7

// Function prototypes
void buttons_setup_new();
void manage_buttons();
void IR_remote_setup();

#endif 