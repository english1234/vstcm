#ifndef main_h
#define main_h

#define VSTCM 1   // Comment this out to compile on Windows

#define CONTROLLER_DEADZONE 8000

#ifdef VSTCM    // Provide the prototypes for the functions called by .INO

void vstcm_setup();
void mainloop();

const int REST_X = 2048;  // Wait in the middle of the screen
const int REST_Y = 2048;

#else

#define SCREEN_WIDTH 1024   // Used for SDL graphics
#define SCREEN_HEIGHT 1024

#define MY_ROMPATH "C:/Users/robin/Documents/Vector_monitor_project/"
#define SDL_PATH "C:\Users\robin\Documents\Vector_monitor_project\SDL2\include\SDL.h"
#endif

#endif