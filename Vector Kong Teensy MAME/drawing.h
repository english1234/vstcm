/*
   VSTCM

   Vector Signal Transceiver Colour Mod using MCP4922 DACs on the Teensy 4.1

   Code to draw on the vector monitor

*/

#ifndef _drawing_h_
#define _drawing_h_

#define DAC_CHAN_RGB 1
#define DAC_CHAN_XY 0

typedef struct ColourIntensity {      // Stores current levels of brightness for each colour
  uint8_t red;
  uint8_t green;
  uint8_t blue;
} ColourIntensity_t;

void cohen_sutherlandCustom(int32_t *, int32_t *, int32_t *, int32_t *, int, int, int, int);
void draw_to_xy(int, int);
void draw_string(const char *, int, int, int, int);
int  draw_character(char, int, int, int, int);
void draw_moveto(int, int);
void _draw_lineto(const int, const int, float);
void old_draw_lineto(int, int, const int);
void make_gammatable(float, uint16_t, uint16_t, uint16_t *);
void init_gamma();
void brightness(uint8_t, uint8_t, uint8_t);
void goto_xy(uint16_t, uint16_t);
void dwell(int);
void MCP4922_write1(int, uint16_t);
void MCP4922_write2(int, uint16_t, uint16_t, int);

#endif