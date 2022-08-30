/*
   VSTCM

   Vector Signal Transceiver Colour Mod using MCP4922 DACs on the Teensy 4.1

   Code to manage onboard and IR remote control buttons

*/

#ifndef _buttons_h_
#define _buttons_h_

void buttons_setup();
void manage_buttons();
void IR_remote_setup();

#endif 
