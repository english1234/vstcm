#ifndef main_h
#define main_h

#define VSTCM 1   // Comment this out to compile on Windows

#define SCREEN_WIDTH 1024
#define SCREEN_HEIGHT 1024

#define CONTROLLER_DEADZONE 8000

#ifdef VSTCM    // Provide the prototypes for the functions called by .INO
void vstcm_setup();
void mainloop();
#else
//#define MY_ROMPATH "D:/Documents/Vector monitor project/"
//#define SDL_PATH "D:\Documents\Vector monitor project\SDL2\include\SDL.h"
#define MY_ROMPATH "C:/Users/robin/Documents/Vector monitor project/"
#define SDL_PATH "C:\Users\robin\Documents\Vector monitor project\SDL2\include\SDL.h"
#endif

#endif