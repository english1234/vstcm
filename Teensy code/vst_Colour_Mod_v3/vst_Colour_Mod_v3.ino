/*
   VSTCM

   Vector Signal Transceiver Colour Mod using MCP4922 DACs on the Teensy 4.1

   Based on: https://trmm.net/V.st by Trammell Hudson
   incorporating mods made by "Swapfile" (Github) for Advanced Mame compatibility

   robin@robinchampion.com 2022

   (This is a modified version by fcawth which attempts to speed things up)

*/

#include "main.h"

extern bool should_quit;

void setup() {
  vstcm_setup();
}

void loop() {
  while (!should_quit)
    mainloop();

}