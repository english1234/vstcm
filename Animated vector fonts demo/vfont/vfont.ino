//
// A Vector font library based upon the Hershey font set
//
// Michael McElligott
// okio@users.sourceforge.net
// https://youtu.be/T0WgGcm7ujM
//
//  Copyright (c) 2005-2017  Michael McElligott
//
//  This program is free software; you can redistribute it and/or
//  modify it under the terms of the GNU LIBRARY GENERAL PUBLIC LICENSE
//  as published by the Free Software Foundation; either version 2
//  of the License, or (at your option) any later version.
//
//  This program is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//  GNU LIBRARY GENERAL PUBLIC LICENSE for more details.
//
//	You should have received a copy of the GNU Library General Public
//	License along with this library; if not, write to the Free
//	Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

#include <SPI.h>
#include <SD.h>
#include <string.h>

//Some additional SPI register bits to possibly work with
#define LPSPI_SR_WCF ((uint32_t)(1<<8)) //received Word complete flag
#define LPSPI_SR_FCF ((uint32_t)(1<<9)) //Frame complete flag
#define LPSPI_SR_TCF ((uint32_t)(1<<10)) //Transfer complete flag
#define LPSPI_SR_MBF ((uint32_t)(1<<24)) //Module busy flag
#define LPSPI_TCR_RXMSK ((uint32_t)(1<<19)) //Receive Data Mask (when 1 no data received to FIFO)
// Teensy SS pins connected to DACs
const int CS_R_G_X_Y       =  6;       // Shared CS for red,green,x,y dacs
const int CS_B = 22;       // Blue CS

#define SDI                 11        // MOSI on SPI0
#define SCK                 13        // SCK on SPI0
//#define BUFFERED                      // If defined, uses buffer on DACs (According to the datasheet it should not be buffered since our Vref is Vdd)

#define SDI1                26       // MOSI for SPI1
#define MISO1               39        //MISO for SPI1 - remap to not interfere with buttons
#define SCK1                27        //SCK for SPI1
#define CS1                 38        //CS for SPI1 - remap to not interfere with buttons

const int REST_X          = 2048;     // Wait in the middle of the screen
const int REST_Y          = 2048;
//For spot killer fix - if the min or max is beyond these limits we will go to the appropriate side

const int SPOT_MAX = 3500;
const int SPOT_GOTOMAX = 4050;
const int SPOT_GOTOMIN = 40;


//EXPERIMENTAL automatic draw rate adjustment based on how much idle time there is between frames
//Defines and global for the auto-speed feature
#define NORMAL_SHIFT_SCALING 2.0
#define MAX_DELTA_SHIFT 6  //These are the limits on the auto-shift for speeding up drawing complex frames
#define MIN_DELTA_SHIFT -3
#define DELTA_SHIFT_INCREMENT 0.2
#define SPEEDUP_THRESHOLD_MS 2  //If the dwell time is less than this then the drawing rate will try to speed up (lower resolution)
#define SLOWDOWN_THRESHOLD_MS 8 //If the dwell time is greater than this then the drawing rate will slow down (higher resolution)
//If the thresholds are too close together there can be "blooming" as the rate goes up and down too quickly - maybe make it limit the
//speed it can change??
float delta_shift = 0 ;
float line_draw_speed;
int frame_max_x;
int frame_min_x;
int frame_max_y;
int frame_min_y;


// Protocol flags from AdvanceMAME
#define FLAG_COMPLETE         0x0
#define FLAG_RGB              0x1
#define FLAG_XY               0x2
#define FLAG_EXIT             0x7
#define FLAG_FRAME            0x4
#define FLAG_CMD              0x5
#define FLAG_CMD_GET_DVG_INFO 0x1
#define FLAG_COMPLETE_MONOCHROME (1 << 28)

// Defines channel A and B of each MCP4922 dual DAC
#define DAC_CHAN_A 0
#define DAC_CHAN_B 1

#define DAC_CHAN_XY 0
#define DAC_CHAN_RGB 1

typedef struct ColourIntensity {      // Stores current levels of brightness for each colour
  uint8_t red;
  uint8_t green;
  uint8_t blue;
} ColourIntensity_t;

uint16_t gamma_red[256];
uint16_t gamma_green[256];
uint16_t gamma_blue[256];

static ColourIntensity_t LastColInt;  // Stores last colour intensity levels

const int MAX_PTS = 3000;

// Chunk of data to process using DMA or SPI
typedef struct DataChunk {
  uint16_t x;                         // We'll just use 12 bits of X & Y for a 4096 point resolution
  uint16_t y;
  uint8_t  red;                       // Max value of each colour is 255
  uint8_t  green;
  uint8_t  blue;
} DataChunk_t;

#define NUMBER_OF_TEST_PATTERNS 2
static DataChunk_t Chunk[NUMBER_OF_TEST_PATTERNS][MAX_PTS];
static int nb_points[NUMBER_OF_TEST_PATTERNS];

static long fps;                       // Approximate FPS used to benchmark code performance improvements
#define IR_RECEIVE_PIN      32 //Put this outside the ifdef so that it doesn't break the menu
#define IR_REMOTE                      // define if IR remote is fitted  TODO:deactivate if the menu is not shown? Has about a 10% reduction of frame rate when active
#ifdef IR_REMOTE
#define SUPPRESS_ERROR_MESSAGE_FOR_BEGIN

#endif

uint32_t mytcr, mytcr1; //Keeps track of what the TCR register should be put back to after 16 bit mode - bit of a hack but reads and writes are a bit funny for this register (FIFOs?)
volatile int Spiflag, Spi1flag; //Keeps track of an active SPI transaction in progress
volatile bool Beam_on;
volatile int activepin;                // Active CS pin of DAC receiving data for SPI (not SPI1 since it has just one CS)
volatile bool show_vstcm_config;       // Shows settings if true

static uint16_t x_pos;                 // Current position of beam
static uint16_t y_pos;

// Settings with default values
const int  OFF_SHIFT      =     9;     // Smaller numbers == slower transits (the higher the number, the less flicker and faster draw but more wavy lines)
const int  OFF_DWELL0     =     6;     // Time to wait after changing the beam intensity (settling time for intensity DACs and monitor)
const int  OFF_DWELL1     =     0;     // Time to sit before starting a transit
const int  OFF_DWELL2     =     0;     // Time to sit after finishing a transit
const int  NORMAL_SHIFT   =     4;     // The higher the number, the less flicker and faster draw but more wavy lines
const bool SHOW_DT       = true;
const bool FLIP_X         = false;     // Sometimes the X and Y need to be flipped and/or swapped
const bool FLIP_Y         = true;
const bool SWAP_XY        = false;
const bool PINCUSHION     = false;
const int  NORMAL1        =   150;     // Brightness of text in parameter list
const int  BRIGHTER       =   230;
// how long in milliseconds to wait for data before displaying a test pattern
// this can be increased if the test pattern appears during gameplay
const int  SERIAL_WAIT_TIME = 150;
const int  AUDIO_PIN        = 10;      // Connect audio output to GND and pin 10

const uint32_t CLOCKSPEED = 60000000; //This is 3x the max clock in the datasheet but seems to work!!

// Settings stored on Teensy SD card
typedef struct {
  char ini_label[20];     // Text string of parameter label in vstcm.ini
  char param[40];         // Parameter label displayed on screen
  uint32_t pval;          // Parameter value
  uint32_t min;           // Min value of parameter
  uint32_t max;           // Max value of parameter
} params_t;

#define NB_PARAMS 16
static params_t v_config[NB_PARAMS] = {
  {"TEST_PATTERN",     "Test pattern",                     0,                0,         4},
  {"OFF_SHIFT",        "Beam transit speed",               OFF_SHIFT,        0,        50},
  {"OFF_DWELL0",       "Beam settling delay", OFF_DWELL0,       0,        50},
  {"OFF_DWELL1",       "Wait before beam transit",         OFF_DWELL1,       0,        50},
  {"OFF_DWELL2",       "WAIT AFTER BEAM TRANSIT",          OFF_DWELL2,       0,        50},
  {"NORMAL_SHIFT",     "NORMAL SHIFT",                     NORMAL_SHIFT,     1,       255},
  {"FLIP_X",           "FLIP X AXIS",                      FLIP_X,           0,         1},
  {"FLIP_Y",           "FLIP Y AXIS",                      FLIP_Y,           0,         1},
  {"SWAP_XY",          "SWAP XY",                          SWAP_XY,          0,         1},
  {"SHOW DT",         "SHOW DT",                         SHOW_DT,         0,         1},
  {"PINCUSHION",       "PINCUSHION",                       PINCUSHION, 0, 1},
  {"IR_RECEIVE_PIN",   "IR_RECEIVE_PIN",                   IR_RECEIVE_PIN,   0,        54},
  {"AUDIO_PIN",        "AUDIO_PIN",                        AUDIO_PIN,        0,        54},
  {"NORMAL1",          "NORMAL TEXT",                      NORMAL1,          0,       255},
  {"BRIGHTER",         "BRIGHT TEXT",                      BRIGHTER,         0,       255},
  {"SERIAL_WAIT_TIME", "TEST PATTERN DELAY",               SERIAL_WAIT_TIME, 0,       255}
};

static int opt_select;    // Currently selected setting

#include "vfont.h"

#define VWIDTH    (480)
#define VHEIGHT   (272)

uint8_t renderBuffer[VWIDTH*VHEIGHT];  // our render destination. defined in primitives.cpp

// i'm a C guy at heart..
static vfont_t context;
static vfont_t *ctx = &context;
static int doTests = 0;

enum _pal {
  COLOUR_PAL_BLACK,

  COLOUR_PAL_RED,
  COLOUR_PAL_GREEN,
  COLOUR_PAL_BLUE,

  COLOUR_PAL_DARKGREEN,
  COLOUR_PAL_DARKGREY,
  COLOUR_PAL_HOMER,
  COLOUR_PAL_REDISH,
  COLOUR_PAL_MAGENTA,
  COLOUR_PAL_CREAM,
  COLOUR_PAL_CYAN,
  COLOUR_PAL_YELLOW,

  COLOUR_PAL_WHITE,
  COLOUR_PAL_TOTAL
};

static inline void clearFrame (const uint8_t palIdx)
{
  memset(renderBuffer, palIdx, VWIDTH * VHEIGHT);
}

static inline void buildPalette ()
{
  /*  usbd480.paletteSet(COLOUR_PAL_BLACK,		COLOUR_BLACK);
    usbd480.paletteSet(COLOUR_PAL_RED,			COLOUR_RED);
    usbd480.paletteSet(COLOUR_PAL_GREEN,		COLOUR_GREEN);
    usbd480.paletteSet(COLOUR_PAL_BLUE,			COLOUR_BLUE);
    usbd480.paletteSet(COLOUR_PAL_WHITE,		COLOUR_WHITE);
    usbd480.paletteSet(COLOUR_PAL_CREAM,		COLOUR_CREAM);
    usbd480.paletteSet(COLOUR_PAL_DARKGREEN,	COLOUR_24TO16(0x00AA55));
    usbd480.paletteSet(COLOUR_PAL_DARKGREY,		COLOUR_24TO16(0x444444));
    usbd480.paletteSet(COLOUR_PAL_HOMER,		COLOUR_24TO16(0xFFBF33));
    usbd480.paletteSet(COLOUR_PAL_REDISH,		COLOUR_24TO16(0xFF0045));
    usbd480.paletteSet(COLOUR_PAL_MAGENTA,		COLOUR_RED | COLOUR_BLUE);
    usbd480.paletteSet(COLOUR_PAL_CYAN,			COLOUR_CYAN);
    usbd480.paletteSet(COLOUR_PAL_YELLOW,		COLOUR_YELLOW);

    usbd480.paletteSet(COLOUR_PAL_TOTAL - 1,		COLOUR_WHITE);
  */
}

void make_gammatable(float gamma, uint16_t maxinput, uint16_t maxoutput, uint16_t *table) {
  for (int i = 0; i < (maxinput + 1); i++) {
    table[i] = (pow((float)i / (float)maxinput, gamma) * (float)maxoutput );
  }
}

void setup()
{
   Serial.begin(115200);
  while ( !Serial && millis() < 4000 );
  //Experimental - 2.2 is NTSC standard but seems too much?
  make_gammatable(.9, 255, 2047, gamma_red);  //Only go up to half scale on the output because of the speedup on V3 boards
  make_gammatable(.9, 255, 2047, gamma_green);
  make_gammatable(.9, 255, 2047, gamma_blue);


  // Apparently, pin 10 has to be defined as an OUTPUT pin to designate the Arduino as the SPI master.
  // Even if pin 10 is not being used... Is this true for Teensy 4.1?
  // The default mode is INPUT. You must explicitly set pin 10 to OUTPUT (and leave it as OUTPUT).
  pinMode(10, OUTPUT);
  digitalWriteFast(10, HIGH);
  pinMode(CS1, OUTPUT);
  digitalWriteFast(CS1, HIGH);
  delayNanoseconds(100);

  // Set chip select pins going to DACs to output
  pinMode(CS_R_G_X_Y, OUTPUT);
  digitalWriteFast(CS_R_G_X_Y, HIGH);
  delayNanoseconds(100);

  pinMode(CS_B, OUTPUT);
  digitalWriteFast(CS_B, HIGH);
  delayNanoseconds(100);

  pinMode(SDI, OUTPUT);       // Set up clock and data output to DACs
  pinMode(SCK, OUTPUT);
  pinMode(SDI1, OUTPUT);       // Set up clock and data output to DACs
  pinMode(SCK1, OUTPUT);
  delay(1);                   // https://www.pjrc.com/better-spi-bus-design-in-3-steps/

  //NOTE:  SPI uses LPSPI4 and SPI1 uses LPSPI3
  Spiflag = 0;
  Spi1flag = 0;
  SPI.setCS(10);
  SPI.begin();

  //Hopefully this will properly map the SPI1 pins
  SPI1.setMISO(MISO1);
  SPI1.setCS(CS1);
  SPI1.setMOSI(SDI1);
  SPI1.setSCK(SCK1);

  SPI1.begin();

  //Some posts seem to indicate that doing a begin and end like this will help conflicts with other things on the Teensy??
  SPI.beginTransaction(SPISettings(CLOCKSPEED, MSBFIRST, SPI_MODE0));  //Doing this begin and end here should make it so we don't have to do it each time
  SPI.endTransaction();
  SPI1.beginTransaction(SPISettings(CLOCKSPEED, MSBFIRST, SPI_MODE0));  //Doing this begin and end here should make it so we don't have to do it each time
  SPI1.endTransaction();
  SPI.beginTransaction(SPISettings(CLOCKSPEED, MSBFIRST, SPI_MODE0));
  SPI1.beginTransaction(SPISettings(CLOCKSPEED, MSBFIRST, SPI_MODE0));
  mytcr = IMXRT_LPSPI4_S.TCR ;
  IMXRT_LPSPI4_S.TCR = (mytcr & 0xfffff000) | LPSPI_TCR_FRAMESZ(15) | LPSPI_TCR_RXMSK; //This will break all stock SPI transactions from this point on - disable receiver and go to 16 bit mode
  IMXRT_LPSPI3_S.TCR = (mytcr & 0xfffff000) | LPSPI_TCR_FRAMESZ(15) | LPSPI_TCR_RXMSK; //This will break all stock SPI transactions from this point on - disable receiver and go to 16 bit mode
  mytcr = (mytcr & 0xfffff000) | LPSPI_TCR_FRAMESZ(15) | LPSPI_TCR_RXMSK;

  line_draw_speed = (float)v_config[5].pval / NORMAL_SHIFT_SCALING;
  show_vstcm_config = true;   // Start off showing the settings screen until serial data received

  buildPalette();

  vfontInitialise(ctx);
  setFont(ctx, &gothiceng);
  //setFont(ctx, &futural);

  setGlyphPadding(ctx, -2.5f);
  setGlyphScale(ctx, 2.0f);
  setBrushColour(ctx, COLOUR_PAL_RED);
  setRenderFilter(ctx, RENDEROP_NONE);

  //setRenderFilter(ctx, RENDEROP_ROTATE_GLYPHS | RENDEROP_ROTATE_STRING);
  setRotationAngle(ctx, -45.0f, 55.0f);
  setShearAngle(ctx, 35.0f, 0.0f);
  setBrushBitmap(ctx, smiley16x16, 16, 16);
}

unsigned long dwell_time = 0;

void loop()
{
  static char tbuffer[16];

  clearFrame(COLOUR_PAL_CREAM);
  uint32_t t2 = micros();
  setBrush(ctx, BRUSH_DISK);
  setAspect(ctx, 1.0f, 1.0f);

#if 0
  const int x = 50;
  const int y = 100;

  setFont(ctx, &gothiceng);
  setGlyphScale(ctx, 4.0f);
  setRenderFilter(ctx, RENDEROP_NONE);
  setBrush(ctx, BRUSH_DISK);
  setBrushStep(ctx, 5.0f);
  setBrushSize(ctx, 40.0f);
  setBrushColour(ctx, COLOUR_PAL_DARKGREEN);
  drawString(ctx, "Gothic", x, y);

  setBrushStep(ctx, 3.0f);
  setBrushSize(ctx, 10.0f);
  setBrushColour(ctx, COLOUR_PAL_DARKGREY);
  drawString(ctx, "Gothic", x, y);

  setBrushStep(ctx, 1.0f);
  setBrushSize(ctx, 3.0f);
  setBrushColour(ctx, COLOUR_PAL_HOMER);
  drawString(ctx, "Gothic", x, y);

  setBrushColour(ctx, COLOUR_PAL_DARKGREY);
  setBrushStep(ctx, 1.0f);
  setBrushSize(ctx, 1.0f);
  drawString(ctx, "Gothic", x, y);

  setFont(ctx, &scriptc);
  setGlyphScale(ctx, 3.0f);
  setBrush(ctx, BRUSH_STROKE_6);
  setBrushStep(ctx, 1.0f);
  setBrushSize(ctx, 13.0f);
  setBrushColour(ctx, COLOUR_PAL_REDISH);
  drawString(ctx, "Script", 20, (VHEIGHT >> 1) + 65);

  setFont(ctx, &scripts);
  setGlyphScale(ctx, 1.5f);
  setBrush(ctx, BRUSH_STROKE_1);
  setBrushStep(ctx, 1.0f);
  setBrushSize(ctx, 5.0f);
  setBrushColour(ctx, COLOUR_PAL_MAGENTA);
  drawString(ctx, "Script", (VWIDTH >> 1) + 20, (VHEIGHT >> 1) + 80);

  setFont(ctx, &futural);
  setGlyphScale(ctx, 3.7f);
  setBrush(ctx, BRUSH_BITMAP);
  setBrushStep(ctx, 75.0f);
  setBrushSize(ctx, 10.0f);
  setBrushColour(ctx, COLOUR_PAL_BLUE);
  drawString(ctx, "B", VWIDTH - 90, VHEIGHT - 60);
#endif

#if 0
  const int x = 50;
  const int y = 60;

  setFont(ctx, &timesr);
  setBrush(ctx, BRUSH_STROKE_1);
  setGlyphScale(ctx, 3.0f);
  setBrushStep(ctx, 2.0f);
  setBrushSize(ctx, 17.0f);
  setBrushColour(ctx, COLOUR_PAL_REDISH);
  drawString(ctx, "Stroked", x, y);

  setFont(ctx, &timesrb);
  setBrush(ctx, BRUSH_TRIANGLE_FILLED);
  setGlyphScale(ctx, 3.0f);
  setBrushStep(ctx, 2.0f);
  setBrushSize(ctx, 15.0f);
  setBrushColour(ctx, COLOUR_PAL_DARKGREEN);
  drawString(ctx, "again", 10, y + 70);

  setFont(ctx, &timesrb);
  setBrush(ctx, BRUSH_SQUARE_FILLED);
  setGlyphScale(ctx, 3.0f);
  setBrushStep(ctx, 2.0f);
  setBrushSize(ctx, 15.0f);
  setBrushColour(ctx, COLOUR_PAL_DARKGREY);
  drawString(ctx, "and..", (VWIDTH >> 1) - 50, y + 135);

  setFont(ctx, &timesi);
  setGlyphScale(ctx, 2.0f);
  setBrushStep(ctx, 1.0f);
  setBrushSize(ctx, 1.0f);		// brushsize of of 1.0 or less defaults to polyline
  setBrushColour(ctx, COLOUR_PAL_MAGENTA);
  drawString(ctx, "line", 10, VHEIGHT - 50);

#endif

#if 1
  static float angle = 0.0f;
  setGlyphScale(ctx, 1.0f);
  setBrushStep(ctx, 1.0f);
  setBrushSize(ctx, 1.0f);
  setFont(ctx, &gothiceng);

  angle = fmod(angle + 1.0f, 360.0f);
  setRotationAngle(ctx, -angle, angle);

  setRenderFilter(ctx, RENDEROP_ROTATE_STRING);
  setBrushColour(ctx, COLOUR_PAL_REDISH);
  drawString(ctx, "Vector Fonts", (VWIDTH >> 1) - 30, (VHEIGHT >> 1) - 30);

  setRenderFilter(ctx, RENDEROP_ROTATE_GLYPHS | RENDEROP_ROTATE_STRING);
  setBrushColour(ctx, COLOUR_PAL_DARKGREEN);
  drawString(ctx, "Teensy3.6", (VWIDTH >> 1) + 20, (VHEIGHT >> 1) + 20);

  setFont(ctx, &futuram);
  setRenderFilter(ctx, RENDEROP_SHEAR_X);
  setShearAngle(ctx, angle, 0.0f);
  setBrushColour(ctx, COLOUR_PAL_DARKGREY);
  drawString(ctx, "Shearing H", (VWIDTH >> 1), (VHEIGHT >> 1) - 100);

  setFont(ctx, &futural);
  setRenderFilter(ctx, RENDEROP_ROTATE_GLYPHS);
  setBrushColour(ctx, COLOUR_PAL_BLUE);
  drawString(ctx, "Hello World!", (VWIDTH >> 1) - 150, (VHEIGHT >> 1) + 100);

  setBrushColour(ctx, COLOUR_PAL_DARKGREY);
  setRenderFilter(ctx, RENDEROP_SHEAR_Y);
  setShearAngle(ctx, 0.0f, angle);
  drawString(ctx, "Shearing V", (VWIDTH >> 1) - 230, (VHEIGHT >> 1));

  setBrushColour(ctx, COLOUR_PAL_HOMER);
  setRenderFilter(ctx, RENDEROP_NONE);
  setGlyphScale(ctx, 2.0f);
  snprintf(tbuffer, sizeof(tbuffer), "%.0f", angle);
  drawString(ctx, tbuffer, (VWIDTH >> 1) + 110, (VHEIGHT >> 1) + 35);

  setFont(ctx, &romand);
  setBrushColour(ctx, COLOUR_PAL_RED);
  setAspect(ctx, 1.0f, -1.0f);
  setRenderFilter(ctx, RENDEROP_NONE);
  setGlyphScale(ctx, 2.0f);
  snprintf(tbuffer, sizeof(tbuffer), "%.0f", angle);
  drawString(ctx, tbuffer, (VWIDTH >> 1) + 110, (VHEIGHT >> 1) + 80);
  setAspect(ctx, 1.0f, 01.0f);

  int x = 60;
  int y = (VHEIGHT >> 1) + 10;
  setFont(ctx, &romans);
  setGlyphScale(ctx, 1.0f);
  setAspect(ctx, 1.0f, angle / 45.0f);
  setBrushColour(ctx, COLOUR_PAL_MAGENTA);
  drawString(ctx, "Aspect", x, y);

  box_t box;
  getStringMetrics(ctx, "Aspect", &box);
  drawRectangle(x + box.x1, y + box.y1, x + box.x2, y + box.y2, COLOUR_PAL_GREEN);

#endif

#if 1
  // measure time-to-render only as time-to-display is not the benchmark we are looking for
  uint32_t t3 = micros();

  setFont(ctx, &futural);
  setBrush(ctx, BRUSH_DISK);
  setBrushStep(ctx, 1.0f);
  setBrushSize(ctx, 1.0f);
  setAspect(ctx, 1.0f, 1.0f);
  setBrushColour(ctx, COLOUR_PAL_BLACK);
  setGlyphScale(ctx, 0.7f);
  snprintf(tbuffer, sizeof(tbuffer), "%.4fs", (t3 - t2) / 1000000.0f);
  drawString(ctx, tbuffer, 5, 25);
#endif

  elapsedMicros waiting;      // Auto updating, used for FPS calculation

  unsigned long draw_start_time, loop_start_time ;

  int serial_flag;

  frame_max_x=0;
  frame_min_x=4095;
  frame_max_y=0;
  frame_min_y=4095;
  
  serial_flag = 0;
  loop_start_time = millis();
 
 
  dwell_time = draw_start_time - loop_start_time; //This is how long it waited after drawing a frame - better than FPS for tuning





    if (dwell_time < SPEEDUP_THRESHOLD_MS) { 
      delta_shift += DELTA_SHIFT_INCREMENT;
      if (delta_shift > MAX_DELTA_SHIFT) delta_shift = MAX_DELTA_SHIFT;
    
 //Try to only allow speedups   
 //   else if (dwell_time > SLOWDOWN_THRESHOLD_MS) {
 //     delta_shift -= DELTA_SHIFT_INCREMENT;
 //     if (delta_shift < MIN_DELTA_SHIFT) delta_shift = MIN_DELTA_SHIFT;
 //   }

    line_draw_speed = (float)v_config[5].pval / NORMAL_SHIFT_SCALING + delta_shift;
    if (line_draw_speed < 1) line_draw_speed = 1;

  }
  // Go to the center of the screen, turn the beam off (prevents stray coloured lines from appearing)
  brightness(0, 0, 0);
  dwell(v_config[3].pval);
  if (!show_vstcm_config) {
    if (((frame_max_x - frame_min_x) < SPOT_MAX) || ((frame_max_y-frame_min_y) < SPOT_MAX))  {
      draw_moveto (SPOT_GOTOMAX,SPOT_GOTOMAX);
     
       draw_moveto (SPOT_GOTOMIN,SPOT_GOTOMIN);
       
       if (dwell_time>3) draw_moveto (SPOT_GOTOMAX,SPOT_GOTOMAX); //If we have time, do a move back all the way to the max
    }
    
  }
  goto_xy(REST_X, REST_Y);
  SPI_flush();
//  if (show_vstcm_config)  manage_buttons(); //Moved here to avoid bright spot on the montior when doing SD card operations
  fps = 1000000 / waiting;
  if (show_vstcm_config) delay(5); //The 6100 monitor likes to spend some time in the middle
  else delayMicroseconds(100); //Wait 100 microseconds in the center if displaying a game (tune this?)

}

static inline int getPixel1 (const uint8_t *pixels, const int pitch, const int x, const int y)
{
  return *(pixels + ((y * pitch) + (x >> 3))) >> (x & 7) & 0x01;
}

void drawBitmap (image_t *img, int x, int y, const uint16_t colour)
{
  const int srcPitch = CALC_PITCH_1(img->width);

  // center image
  x -= img->width >> 1;
  y -= img->height >> 1;

  for (int y1 = 0; y1 < img->height; y1++, y++) {
    int _x = x;
    for (int x1 = 0; x1 < img->width; x1++, _x++) {
      if (getPixel1(img->pixels, srcPitch, x1, y1))
        drawPixel(_x, y, colour);
    }
  }
}

static inline void swapf (float *a, float *b)
{
  float tmp = *a;
  *a = *b;
  *b = tmp;
}

static inline void swap32 (int *a, int *b)
{
  *a ^= *b;
  *b ^= *a;
  *a ^= *b;
}

static inline void drawHLine (const int y, int x1, int x2, const uint16_t colour)
{
  if (x2 < x1) swap32(&x1, &x2);
  if (x1 < 0) x1 = 0;
  if (x2 >= VWIDTH) x2 = VWIDTH - 1;

  for (int x = x1; x <= x2; x++)
    drawPixel(x, y, colour);
}

static inline int findRegion (const int x, const int y)
{
  int code = 0;

  if (y >= VHEIGHT)
    code = 1;   // top
  else if (y < 0)
    code |= 2;    // bottom

  if (x >= VWIDTH)
    code |= 4;    // right
  else if ( x < 0)
    code |= 8;    // left

  return code;
}

// clip using Cohen-Sutherland algorithm
static inline int clipLine (int x1, int y1, int x2, int y2, int *x3, int *y3, int *x4, int *y4)
{
  int accept = 0, done = 0;
  int code1 = findRegion(x1, y1); //the region outcodes for the endpoints
  int code2 = findRegion(x2, y2);

  const int h = VHEIGHT;
  const int w = VWIDTH;

  do {
    if (!(code1 | code2))
    {
      accept = done = 1;  //accept because both endpoints are in screen or on the border, trivial accept
    }
    else if (code1 & code2)
    {
      done = 1; //the line isn't visible on screen, trivial reject
    }
    else
    { //if no trivial reject or accept, continue the loop
      int x, y;
      int codeout = code1 ? code1 : code2;
      if (codeout & 1)
      { //top
        x = x1 + (x2 - x1) * (h - y1) / (y2 - y1);
        y = h - 1;
      }
      else if (codeout & 2)
      { //bottom
        x = x1 + (x2 - x1) * -y1 / (y2 - y1);
        y = 0;
      }
      else if (codeout & 4)
      { //right
        y = y1 + (y2 - y1) * (w - x1) / (x2 - x1);
        x = w - 1;
      }
      else
      { //left
        y = y1 + (y2 - y1) * -x1 / (x2 - x1);
        x = 0;
      }

      if (codeout == code1)
      { //first endpoint was clipped
        x1 = x; y1 = y;
        code1 = findRegion(x1, y1);
      }
      else
      { //second endpoint was clipped
        x2 = x; y2 = y;
        code2 = findRegion(x2, y2);
      }
    }
  } while (!done);

  if (accept)
  {
    *x3 = x1;
    *x4 = x2;
    *y3 = y1;
    *y4 = y2;
    return 1;
  }
  else
  {
    return 0;
  }
}

#ifdef LINE_FAST
static inline void drawLineFast (int x, int y, int x2, int y2, const int colour)
{
  if (!clipLine(x, y, x2, y2, &x, &y, &x2, &y2))
    return;

  int yLonger = 0;
  int shortLen = y2 - y;
  int longLen = x2 - x;

  if (abs(shortLen) > abs(longLen)) 
  {
    swap32(&shortLen, &longLen);
    yLonger = 1;
  }
  
  int decInc;

  if (!longLen)
    decInc = 0;
  else
    decInc = (shortLen << 16) / longLen;

  if (yLonger) 
  {
    if (longLen > 0) 
    {
      longLen += y;

      for (int j = 0x8000 + (x << 16); y <= longLen; ++y) 
      {
        drawPixel(j >> 16, y, colour);
        j += decInc;
      }
      
      return;
    }
    
    longLen += y;

    for (int j = 0x8000 + (x << 16); y >= longLen; --y) 
    {
      drawPixel(j >> 16, y, colour);
      j -= decInc;
    }
    
    return;
  }

  if (longLen > 0) {
    longLen += x;

    for (int j = 0x8000 + (y << 16); x <= longLen; ++x) {
      drawPixel(x, j >> 16, colour);
      j += decInc;
    }
    return;
  }

  longLen += x;

  for (int j = 0x8000 + (y << 16); x >= longLen; --x) {
    drawPixel(x, j >> 16, colour);
    j -= decInc;
  }

}
#endif

#ifdef LINE_STD
static inline void drawLineStd (int x1, int y1, int x2, int y2, const uint16_t colour)
{
  if (!clipLine(x1, y1, x2, y2, &x1, &y1, &x2, &y2))
    return;

  const int dx = x2 - x1;
  const int dy = y2 - y1;

  if (dx || dy) {
    if (abs(dx) >= abs(dy)) {
      float y = y1 + 0.5f;
      float dly = dy / (float)dx;

      if (dx > 0) {
        for (int xx = x1; xx <= x2; xx++) {
          drawPixel(xx, (int)y, colour);
          y += dly;
        }
      } else {
        for (int xx = x1; xx >= x2; xx--) {
          drawPixel(xx, (int)y, colour);
          y -= dly;
        }
      }
    } else {
      float x = x1 + 0.5f;
      float dlx = dx / (float)dy;

      if (dy > 0) {
        for (int yy = y1; yy <= y2; yy++) {
          drawPixel((int)x, yy, colour);
          x += dlx;
        }
      } else {
        for (int yy = y1; yy >= y2; yy--) {
          drawPixel((int)x, yy, colour);
          x -= dlx;
        }
      }
    }
  } else if (!(dx & dy)) {
    drawPixel(x1, y1, colour);
  }
}
#endif

#ifdef LINE_FASTEST8
static inline void drawLine8 (int x0, int y0, int x1, int y1, const uint8_t colour)
{
  if (!clipLine(x0, y0, x1, y1, &x0, &y0, &x1, &y1))
    return;

  int stepx, stepy;

  int dy = y1 - y0;
  if (dy < 0) {
    dy = -dy;
    stepy = -VWIDTH;
  } else {
    stepy = VWIDTH;
  }
  dy <<= 1;

  int dx = x1 - x0;
  if (dx < 0) {
    dx = -dx;
    stepx = -1;
  } else {
    stepx = 1;
  }
  dx <<= 1;

  y0 *= VWIDTH;
  y1 *= VWIDTH;

  uint8_t *pixels = (uint8_t*)renderBuffer;
  pixels[x0 + y0] = colour;

  if (dx > dy) {
    int fraction = dy - (dx >> 1);

    while (x0 != x1) {
      if (fraction >= 0) {
        y0 += stepy;
        fraction -= dx;
      }
      x0 += stepx;
      fraction += dy;
      pixels[x0 + y0] = colour;
    }
  } else {
    int fraction = dx - (dy >> 1);

    while (y0 != y1) {
      if (fraction >= 0) {
        x0 += stepx;
        fraction -= dy;
      }
      y0 += stepy;
      fraction += dx;
      pixels[x0 + y0] = colour;
    }
  }
  return;
}
#endif

#ifdef LINE_FASTEST16
static inline void drawLine16 (int x0, int y0, int x1, int y1, const uint16_t colour)
{
  if (!clipLine(x0, y0, x1, y1, &x0, &y0, &x1, &y1))
    return;

  int stepx, stepy;

  int dy = y1 - y0;
  if (dy < 0) {
    dy = -dy;
    stepy = -VWIDTH;
  } else {
    stepy = VWIDTH;
  }
  dy <<= 1;

  int dx = x1 - x0;
  if (dx < 0) {
    dx = -dx;
    stepx = -1;
  } else {
    stepx = 1;
  }
  dx <<= 1;

  y0 *= VWIDTH;
  y1 *= VWIDTH;

  uint16_t *pixels = (uint16_t*)renderBuffer;
  pixels[x0 + y0] = colour;

  if (dx > dy) {
    int fraction = dy - (dx >> 1);

    while (x0 != x1) {
      if (fraction >= 0) {
        y0 += stepy;
        fraction -= dx;
      }
      x0 += stepx;
      fraction += dy;
      pixels[x0 + y0] = colour;
    }
  } else {
    int fraction = dx - (dy >> 1);

    while (y0 != y1) {
      if (fraction >= 0) {
        x0 += stepx;
        fraction -= dy;
      }
      y0 += stepy;
      fraction += dx;
      pixels[x0 + y0] = colour;
    }
  }
  return;
}
#endif

//Using both SPI and SPI1, write out two or three dacs at once
//if allchannels is set, all 3 dacs are written so blue and green are set to value2
//otherwise value is x or red, and value2 is y or green

void MCP4922_write2(int dac, uint16_t value, uint16_t value2, int allchannels)
{
  // uint32_t temp;

  //Wait for the last transaction to finish and then set CS high from the last transaction
  //By doing this the code can do other things instead of busy waiting for the SPI transaction
  //like it does with the stock functions.
  if (Spiflag) while (!(IMXRT_LPSPI4_S.SR & LPSPI_SR_FCF));  //Loop until the last frame is complete
  if (Spi1flag) while (!(IMXRT_LPSPI3_S.SR & LPSPI_SR_FCF));  //Loop until the last frame is complete
  digitalWriteFast(CS_R_G_X_Y, HIGH); //Set the CS from the last transaction high
  digitalWriteFast(CS_B, HIGH); //Set the CS from the last transaction high for the blue channel in case it was active (possibly use a flag to check??)


  //Everything between here and setting the CS pin low determines how long the CS signal is high

  IMXRT_LPSPI4_S.SR = LPSPI_SR_FCF; //Clear the flag
  IMXRT_LPSPI3_S.SR = LPSPI_SR_FCF; //Clear the flag
  //IMXRT_LPSPI4_S.TCR=mytcr; //Go back to 8 bit mode (can stay in 16 bit mode)
  //temp = IMXRT_LPSPI4_S.RDR; //Go ahead and read the receive FIFO (not necessary since we have masked receive data above)

  value &= 0x0FFF; // mask out just the 12 bits of data
  value2 &= 0x0FFF; // mask out just the 12 bits of data

  // add the output channel A or B on the selected DAC, and buffer flag
#ifdef BUFFERED
  // select the output channel on the selected DAC, buffered, no gain
  value |= 0x7000 | (dac  ? 0x8000 : 0x0000);
  value2 |= 0x7000 | (dac  ? 0x8000 : 0x0000);
#else
  // select the output channel on the selected DAC, unbuffered, no gain
  value |= 0x3000 | (dac  ? 0x8000 : 0x0000);
  value2 |= 0x3000 | (dac  ? 0x8000 : 0x0000);
#endif



  digitalWriteFast(CS_R_G_X_Y, LOW);
  if (allchannels) digitalWriteFast(CS_B, LOW); //Write out the blue with the same value as green


  //Set up the transaction directly with the SPI registers because the normal transfer16
  //function will busy wait for the SPI transfer to complete.  We will wait for completion
  //and de-assert CS the next time around to speed things up.
  //By doing this the code can do other things instead of busy waiting for the SPI transaction
  //like it does with the stock functions.
  //IMXRT_LPSPI4_S.TCR=(mytcr & 0xfffff000) | LPSPI_TCR_FRAMESZ(15);  // turn on 16 bit mode  (this is done above and we keep it on now)
  Spiflag = 1;
  IMXRT_LPSPI4_S.TDR = value2; //Send data to the SPI fifo and start transaction but don't wait for it to be done

  Spi1flag = 1;
  IMXRT_LPSPI3_S.TDR = value; //Send data to the SPI1 fifo and start transaction but don't wait for it to be done

}

//Write just out the one dac that is by itself
void MCP4922_write1(int dac, uint16_t value)
{
  // uint32_t temp;

  //Wait for the last transaction to finish and then set CS high from the last transaction
  //By doing this the code can do other things instead of busy waiting for the SPI transaction
  //like it does with the stock functions.
  if (Spiflag) while (!(IMXRT_LPSPI4_S.SR & LPSPI_SR_FCF));  //Loop until the last frame is complete
  if (Spi1flag) while (!(IMXRT_LPSPI3_S.SR & LPSPI_SR_FCF));  //Loop until the last frame is complete
  Spi1flag = 0;
  digitalWriteFast(CS_R_G_X_Y, HIGH); //Set the CS from the last transaction high
  digitalWriteFast(CS_B, HIGH); //Set the CS from the last transaction high for the blue channel in case it was active (possibly use a flag to check??)

  //Everything between here and setting the CS pin low determines how long the CS signal is high

  IMXRT_LPSPI4_S.SR = LPSPI_SR_FCF; //Clear the flag
  IMXRT_LPSPI3_S.SR = LPSPI_SR_FCF; //Clear the flag
  //IMXRT_LPSPI4_S.TCR=mytcr; //Go back to 8 bit mode (can stay in 16 bit mode)
  //temp = IMXRT_LPSPI4_S.RDR; //Go ahead and read the receive FIFO (not necessary since we have masked receive data above)

  value &= 0x0FFF; // mask out just the 12 bits of data

  // add the output channel A or B on the selected DAC, and buffer flag
#ifdef BUFFERED
  // select the output channel on the selected DAC, buffered, no gain
  value |= 0x7000 | (dac  ? 0x8000 : 0x0000);

#else
  // select the output channel on the selected DAC, unbuffered, no gain
  value |= 0x3000 | (dac  ? 0x8000 : 0x0000);

#endif

  digitalWriteFast(CS_B, LOW);

  //Set up the transaction directly with the SPI registers because the normal transfer16
  //function will busy wait for the SPI transfer to complete.  We will wait for completion
  //and de-assert CS the next time around to speed things up.
  //By doing this the code can do other things instead of busy waiting for the SPI transaction
  //like it does with the stock functions.
  //IMXRT_LPSPI4_S.TCR=(mytcr & 0xfffff000) | LPSPI_TCR_FRAMESZ(15);  // turn on 16 bit mode  (this is done above and we keep it on now)
  Spiflag = 1;
  IMXRT_LPSPI4_S.TDR = value; //Send data to the SPI fifo and start transaction but don't wait for it to be done


}

//Finish the last SPI transactions
void SPI_flush() {
  //Wait for the last transaction to finish and then set CS high from the last transaction
  //By doing this the code can do other things instead of busy waiting for the SPI transaction
  //like it does with the stock functions.
  if (Spiflag) while (!(IMXRT_LPSPI4_S.SR & LPSPI_SR_FCF));  //Loop until the last frame is complete
  if (Spi1flag) while (!(IMXRT_LPSPI3_S.SR & LPSPI_SR_FCF));  //Loop until the last frame is complete
  digitalWriteFast(CS_R_G_X_Y, HIGH); //Set the CS from the last transaction high
  digitalWriteFast(CS_B, HIGH); //Set the CS from the last transaction high for the blue channel in case it was active (possibly use a flag to check??)

  IMXRT_LPSPI4_S.SR = LPSPI_SR_FCF; //Clear the flag
  IMXRT_LPSPI3_S.SR = LPSPI_SR_FCF; //Clear the flag
  Spiflag = 0;
  Spi1flag = 0;
}

void dwell(int count)
{
  // can work better or faster without this on some monitors
  SPI_flush(); //Get the dacs set to their latest values before we wait
  for (int i = 0 ; i < count ; i++)
  {
    delayNanoseconds(500);  //NOTE this used to write the X and Y position but now the dacs won't get updated with repeated values
  }
}

void brightness(uint8_t red, uint8_t green, uint8_t blue)
{
  if ((LastColInt.red == red) && (LastColInt.green == green) && (LastColInt.blue == blue)) return;

  if (green == blue) { //We can write all 3 at the same time if green is the same as blue
    LastColInt.red = red;
    LastColInt.green = green;
    LastColInt.blue = blue;
    // MCP4922_write2(DAC_CHAN_RGB, red << 3, green << 3 , 1);  //Shift by 3 to go to half scale maximum
    MCP4922_write2(DAC_CHAN_RGB, gamma_red[red], gamma_green[green] , 1);
  }
  { //We can write red and green at the same time
    if ((LastColInt.red != red) || (LastColInt.green != green) )
    {
      LastColInt.red = red;
      LastColInt.green = green;
      // MCP4922_write2(DAC_CHAN_RGB, red << 3, green << 3 , 0);
      MCP4922_write2(DAC_CHAN_RGB, gamma_red[red], gamma_green[green] , 0);
    }
    if (LastColInt.blue != blue)
    {
      LastColInt.blue = blue;
      MCP4922_write1(DAC_CHAN_RGB, gamma_blue[blue]);
    }
  }
  //Dwell moved here since it takes about 4us to fully turn on or off the beam
  //Possibly change where this is depending on if the beam is being turned on or off??
  if (LastColInt.red || LastColInt.green || LastColInt.blue) Beam_on = true;
  else Beam_on = false;
  dwell(v_config[2].pval); //Wait this amount before changing the beam (turning it on or off)

}

void goto_xy(uint16_t x, uint16_t y) {
  float xf, yf;
  float xcorr, ycorr;
  if ((x_pos == x) && (y_pos == y)) return;
   x_pos = x;
   y_pos = y;
  if (v_config[10].pval == true ) {
    xf = x - 2048;
    yf = y - 2048;
    xcorr = xf * (1.0 - yf * yf * .000000013) + 2048.0; //These are experimental at this point but seem to do OK on the 6100 monitor
    ycorr = yf * (1.0 - xf * xf * .000000005) + 2048.0;
    x = xcorr;
    y = ycorr;
  } 
  if (v_config[6].pval == false) x = 4095 - x;
  if (v_config[7].pval == false) y = 4095 - y;
  MCP4922_write2(DAC_CHAN_XY, x, y , 0);
}


//Trying out using floating point to compute the line
//Also try to make sure the move has at least a couple of points in it
//if we are drawing
void _draw_lineto(const int x1, const int y1, float bright_shift)
{
  int dx, dy;
  float dxf, dyf;
  int dxmag, dymag;
  int max_dist;
  int x0, y0;
  int numsteps;
  float numstepsf;
  float xcur, ycur;
  int i;


  if (x1> frame_max_x) frame_max_x=x1;
  else if (x1<frame_min_x) frame_min_x=x1;
  if (y1> frame_max_y) frame_max_y=y1;
  else if (y1<frame_min_y) frame_min_y=y1;

  x0 = x_pos;
  y0 = y_pos;
  dx = x1 - x0;
  dy = y1 - y0;
  dxmag = abs(dx);
  dymag = abs(dy);

  xcur = x0;
  ycur = y0;
  if (dxmag > dymag) max_dist = dxmag;
  else max_dist = dymag;

  if (Beam_on && (max_dist * 2 < bright_shift)) { //Force at least 2 points in each line segment (cleans up text at high rates)
    bright_shift = max_dist >> 1;
    if (bright_shift < 1) bright_shift = 1;
  }

  numstepsf = ceilf((float)max_dist / (float)bright_shift);
  dxf = (float)dx / numstepsf;
  dyf = (float)dy / numstepsf;
  numsteps = numstepsf;
  for (i = 0; i < numsteps; i++) {
    xcur += dxf;
    ycur += dyf;
    goto_xy(round(xcur), round(ycur));
  }
  goto_xy(x1, y1);
  SPI_flush();
}

void draw_moveto(int x1, int y1)
{
  brightness(0, 0, 0);


  // hold the current position for a few clocks
  // with the beam off
  dwell(v_config[3].pval);
  _draw_lineto(x1, y1, v_config[1].pval);
  dwell(v_config[4].pval);
  if (x1> frame_max_x) frame_max_x=x1;
  else if (x1<frame_min_x) frame_min_x=x1;
  if (y1> frame_max_y) frame_max_y=y1;
  else if (y1<frame_min_y) frame_min_y=y1;
}

void draw_to_xyrgb(int x, int y, uint8_t red, uint8_t green, uint8_t blue)
{
  brightness(red, green, blue);   // Set RGB intensity levels from 0 to 255
  _draw_lineto(x, y, line_draw_speed);
}

void drawLine(const int x1, const int y1, const int x2, const int y2, const uint16_t colour)
{
  int xx1, xx2, yy1, yy2;

  int red = 0, green = 0, blue = 0;

  if (colour==1) { red = 160; green = 0; blue = 0; }
  if (colour==2) { red = 0; green = 160; blue = 0; }
  if (colour==3) { red = 0; green = 0; blue = 160; }
  if (colour==4) { red = 160; green = 160; blue = 0; }
  if (colour==5) { red = 0; green = 160; blue = 160; }
  if (colour==6) { red = 160; green = 0; blue = 160; }
  if (colour==7) { red = 160; green = 160; blue = 160; }
  if (colour==8) { red = 80; green = 80; blue = 80; }
  
  xx1 = x1<<3;
  xx2 = x2<<3;
  yy1 = (y1<<3)+1024;
  yy2 = (y2<<3)+1024;

  if (xx1 < 0) xx1 = 0;
  if (xx2 < 0) xx2 = 0;
  if (yy1 < 0) yy1 = 0;
  if (yy2 < 0) yy2 = 0;

  if (xx1 > 4095) xx1 = 4095;
  if (xx2 > 4095) xx2 = 4095;
  if (yy1 > 4095) yy1 = 4095;
  if (yy2 > 4095) yy2 = 4095;
  
  Serial.print("x1 ");
  Serial.print(x1<<3);
  Serial.print(" y1 ");
  Serial.print(y1<<3);
  Serial.print(" x2 ");
  Serial.print(x2<<3);
  Serial.print(" y2 ");
  Serial.print(y2<<3);
  Serial.print(" colour ");
  Serial.println(colour);
  
  draw_moveto(xx1, yy1);
  draw_to_xyrgb(xx2, yy2, red, green, blue);
 
  
/*
#if LINE_STD
  // slowest: standard Bresenham algorithm
  drawLineStd(x1, y1, x2, y2, colour);    // 94.0

#elif LINE_FAST
  // fast: similar to Bresenham but forgoes accuracy for performance
  drawLineFast(x1, y1, x2, y2, colour);   // 58.7

#elif LINE_FASTEST16
  // faster line rountine - draw direct to 16bit buffer
  drawLine16(x1, y1, x2, y2, colour);     // 27.9

#elif LINE_FASTEST8
  // fastest line rountine - draw direct to 8bit buffer
  drawLine8(x1, y1, x2, y2, colour);
#endif
*/
}

static inline void drawVLine (const int x, const int y, const int h, const uint16_t colour)
{
  drawLine(x, y, x, y + h - 1, colour);
}

void drawCircleFilled (const int x0, const int y0, const float radius, const uint16_t colour)
{
  int f = 1 - radius;
  int ddF_x = 1;
  int ddF_y = -2.0f * radius;
  int x = 0;
  int y = radius;
  int ylm = x0 - radius;

  while (x < y) {
    if (f >= 0) {
      drawVLine(x0 + y, y0 - x, 2 * x + 1, colour);
      drawVLine(x0 - y, y0 - x, 2 * x + 1, colour);

      ylm = x0 - y;
      y--;
      ddF_y += 2;
      f += ddF_y;
    }

    x++;
    ddF_x += 2;
    f += ddF_x;

    if ((x0 - x) > ylm) {
      drawVLine(x0 + x, y0 - y, 2 * y + 1, colour);
      drawVLine(x0 - x, y0 - y, 2 * y + 1, colour);
    }
  }
  
  drawVLine(x0, y0 - radius, 2.0f * radius + 1.0f, colour);
}

static inline void drawCirclePts (const int xc, const int yc, const int x, const int y, const uint16_t colour)
{
  drawPixel(xc + y, yc - x, colour);
  drawPixel(xc - y, yc - x, colour);
  drawPixel(xc + y, yc + x, colour);
  drawPixel(xc - y, yc + x, colour);
  drawPixel(xc + x, yc + y, colour);
  drawPixel(xc - x, yc + y, colour);
  drawPixel(xc + x, yc - y, colour);
  drawPixel(xc - x, yc - y, colour);
}

void drawCircle (const int xc, const int yc, const int radius, const uint16_t colour)
{
  float x = 0.0f;
  float y = radius;
  float p = 1.25f - radius;

  drawCirclePts(xc, yc, x, y, colour);

  while (x < y) {
    x += 1.0f;
    if (p < 0.0f) {
      p += 2.0f * x + 1.0f;
    } else {
      y -= 1.0f;
      p += 2.0f * x + 1.0f - 2.0f * y;
    }
    drawCirclePts(xc, yc, x, y, colour);
  }
}

static inline void clipRect (int *x1, int *y1, int *x2, int *y2)
{
  if (*x1 < 0)
    *x1 = 0;
  else if (*x1 >= VWIDTH)
    *x1 = VWIDTH - 1;
  if (x2 < 0)
    *x2 = 0;
  else if (*x2 >= VWIDTH)
    *x2 = VWIDTH - 1;
  if (*y1 < 0)
    *y1 = 0;
  else if (*y1 >= VHEIGHT)
    *y1 = VHEIGHT - 1;
  if (*y2 < 0)
    *y2 = 0;
  else if (*y2 >= VHEIGHT)
    *y2 = VHEIGHT - 1;
}

void drawRectangleFilled (int x1, int y1, int x2, int y2, const uint16_t colour)
{
  clipRect(&x1, &y1, &x2, &y2);

  for (int y = y1; y <= y2; y++)
    drawHLine(y, x1, x2, colour);
}

void drawRectangle (int x1, int y1, int x2, int y2, const uint16_t colour)
{
  clipRect(&x1, &y1, &x2, &y2);

  drawLine(x1, y1, x2, y1, colour);   // top
  drawLine(x1, y2, x2, y2, colour);   // bottom
  drawLine(x1, y1 + 1, x1, y2 - 1, colour); // left
  drawLine(x2, y1 + 1, x2, y2 - 1, colour); // right
}

void drawTriangle (const int x1, const int y1, const int x2, const int y2, const int x3, const int y3, const uint16_t colour)
{
  drawLine(x1, y1, x2, y2, colour);
  drawLine(x2, y2, x3, y3, colour);
  drawLine(x1, y1, x3, y3, colour);
}

void drawTriangleFilled (const int x0, const int y0, const int x1, const int y1, const int x2, const int y2, const uint16_t colour)
{
  float XA = 0.0f, XB = 0.0f;
  float XA1 = 0.0f, XB1 = 0.0f, XC1 = 0.0f;
  float XA2 = 0.0f, XB2 = 0.0f;
  float XAd, XBd;
  float HALF = 0.0f;

  int t = y0;
  int b = y0;
  int CAS = 0;

  if (y1 < t) {
    t = y1;
    CAS = 1;
  }
  if (y1 > b)
    b = y1;

  if (y2 < t) {
    t = y2;
    CAS = 2;
  }
  if (y2 > b)
    b = y2;

  if (CAS == 0) {
    XA = x0;
    XB = x0;
    XA1 = (x1 - x0) / (float)(y1 - y0);
    XB1 = (x2 - x0) / (float)(y2 - y0);
    XC1 = (x2 - x1) / (float)(y2 - y1);

    if (y1 < y2) {
      HALF = y1;
      XA2 = XC1;
      XB2 = XB1;
    } else {
      HALF = y2;
      XA2 = XA1;
      XB2 = XC1;
    }
    if (y0 == y1)
      XA = x1;
    if (y0 == y2)
      XB = x2;
  } else if (CAS == 1) {
    XA = x1;
    XB = x1;
    XA1 = (x2 - x1) / (float)(y2 - y1);
    XB1 = (x0 - x1) / (float)(y0 - y1);
    XC1 = (x0 - x2) / (float)(y0 - y2);

    if ( y2 < y0) {
      HALF = y2;
      XA2 = XC1;
      XB2 = XB1;
    } else {
      HALF = y0;
      XA2 = XA1;
      XB2 = XC1;
    }
    if (y1 == y2)
      XA = x2;
    if (y1 == y0)
      XB = x0;
  } else if (CAS == 2) {
    XA = x2;
    XB = x2;
    XA1 = (x0 - x2) / (float)(y0 - y2);
    XB1 = (x1 - x2) / (float)(y1 - y2);
    XC1 = (x1 - x0) / (float)(y1 - y0);
    if (y0 < y1) {
      HALF = y0;
      XA2 = XC1;
      XB2 = XB1;
    } else {
      HALF = y1;
      XA2 = XA1;
      XB2 = XC1;
    }
    if (y2 == y0)
      XA = x0;
    if (y2 == y1)
      XB = x1;
  }

  if (XA1 > XB1) {
    swapf(&XA, &XB);
    swapf(&XA1, &XB1);
    swapf(&XA2, &XB2);
  }

  for (int y = t; y < HALF; y++) {
    XAd = XA;
    XBd = XB;

    for (int x = XAd; x <= XBd; x++)
      drawPixel(x, y, colour);
    XA += XA1;
    XB += XB1;
  }

  for (int y = HALF; y <= b; y++) {
    XAd = XA;
    XBd = XB;

    for (int x = XAd; x <= XBd; x++)
      drawPixel(x, y, colour);
    XA += XA2;
    XB += XB2;
  }
}

#if 0
static inline void drawPixel16 (const int x, const int y, const uint16_t colour)
{
  uint16_t *pixels = (uint16_t*)renderBuffer;
  pixels[(y * VWIDTH) + x] = colour;
}

static const inline void drawPixel8 (const int x, const int y, const uint8_t colour)
{
  uint8_t *pixels = (uint8_t*)renderBuffer;
  pixels[(y * VWIDTH) + x] = colour;
}
#endif

static inline void drawBrushBitmap (vfont_t *ctx, const int x, const int y, const uint16_t colour)
{
  drawBitmap(&ctx->brush.image, x, y, colour);
}

static inline void drawBrush (vfont_t *ctx, float xc, float yc, const float radius, const uint16_t colour)
{
  if (ctx->brush.type == BRUSH_DISK) {
    drawCircleFilled(xc, yc, radius, colour);

  } else if (ctx->brush.type == BRUSH_SQUARE_FILLED) {
    int d = (int)radius >> 1;
    drawRectangleFilled(xc - d, yc - d, xc + d, yc + d, colour);

  } else if (ctx->brush.type == BRUSH_SQUARE) {
    int d = (int)radius >> 1;
    drawRectangle(xc - d, yc - d, xc + d, yc + d, colour);

  } else if (ctx->brush.type == BRUSH_TRIANGLE_FILLED) {
    int d = (int)radius >> 1;
    drawTriangleFilled(xc - d, yc + d, xc, yc - d, xc + d, yc + d, colour);

  } else if (ctx->brush.type == BRUSH_TRIANGLE) {
    int d = (int)radius >> 1;
    drawTriangle(xc - d, yc + d, xc, yc - d, xc + d, yc + d, colour);

  } else if (ctx->brush.type == BRUSH_CIRCLE) {
    drawCircle(xc, yc, radius, colour);

  } else if (ctx->brush.type == BRUSH_STROKE_1) {
    int d = (int)radius >> 1;
    drawLine(xc - d, yc + d, xc + d, yc - d, colour); // slope up
    xc++;
    drawLine(xc - d, yc + d, xc + d, yc - d, colour);

  } else if (ctx->brush.type == BRUSH_STROKE_2) {
    int d = (int)radius >> 1;
    drawLine(xc - d, yc - d, xc + d, yc + d, colour); // slope down
    xc++;
    drawLine(xc - d, yc - d, xc + d, yc + d, colour);

  } else if (ctx->brush.type == BRUSH_STROKE_3) {
    int d = (int)radius >> 1;
    drawLine(xc - d, yc, xc + d, yc, colour); // horizontal
    yc++;
    drawLine(xc - d, yc, xc + d, yc, colour);

  } else if (ctx->brush.type == BRUSH_STROKE_4) {
    int d = (int)radius >> 1;
    drawLine(xc, yc - d, xc, yc + d, colour); // vertical
    xc++;
    drawLine(xc, yc - d, xc, yc + d, colour);

  } else if (ctx->brush.type == BRUSH_STROKE_5) {
    int d = (int)radius >> 1;
    drawLine(xc - d, yc + d, xc + d, yc - d, colour); // forward slope up with smaller siblings either side
    d = radius * 0.3f;
    xc++;
    drawLine(xc - d, yc + d, xc + d, yc - d, colour);
    xc -= 2;
    //yc -= 2;
    drawLine(xc - d, yc + d, xc + d, yc - d, colour);

  } else if (ctx->brush.type == BRUSH_STROKE_6) {
    int d = (int)radius >> 1;
    drawLine(xc - d, yc + d, xc + d, yc - d, colour); // BRUSH_STROKE_5 but thicker
    xc++;
    drawLine(xc - d, yc + d, xc + d, yc - d, colour);
    d = radius * 0.3f;
    xc++;
    drawLine(xc - d, yc + d, xc + d, yc - d, colour); // right side
    xc -= 2; yc -= 1;
    drawLine(xc - d, yc + d, xc + d, yc - d, colour); // left side

  } else if (ctx->brush.type == BRUSH_STAR) {
    int d = (int)radius >> 1;
    drawLine(xc - d, yc + d, xc + d, yc - d, colour); // slope up
    drawLine(xc - d, yc - d, xc + d, yc + d, colour); // slope down
    drawLine(xc - d, yc, xc + d, yc, colour); // horizontal
    drawLine(xc, yc - d, xc, yc + d, colour); // vertical

  } else if (ctx->brush.type == BRUSH_X) {
    int d = (int)radius >> 1;
    drawLine(xc - d, yc + d, xc + d, yc - d, colour); // slope up
    drawLine(xc - d, yc - d, xc + d, yc + d, colour); // slope down

  } else if (ctx->brush.type == BRUSH_PLUS) {
    int d = (int)radius >> 1;
    drawLine(xc - d, yc, xc + d, yc, colour); // horizontal
    drawLine(xc, yc - d, xc, yc + d, colour); // vertical

  }
  else if (ctx->brush.type == BRUSH_CARET) {
    int d = (int)radius >> 1;           // ^ (XOR operator)
    drawLine(xc - d, yc + d, xc, yc - d, colour); // left
    drawLine(xc, yc - d, xc + d, yc + d, colour); // right

  }
  else if (ctx->brush.type == BRUSH_BITMAP) {
    drawBrushBitmap(ctx, xc, yc, colour);

  }
  else
  { // BRUSH_POINT
    drawPixel(xc, yc, colour);
  }
}

static inline float distance (const float x1, const float y1, const float x2, const float y2)
{
  return sqrtf((x1 - x2) * (x1 - x2) + (y1 - y2) * (y1 - y2));
}

//check if point2 is between point1 and point3 (the 3 points should be on the same line)
static inline int isBetween (const float x1, const float y1, const float x2, const float y2, const float x3, const float y3)
{
  return ((int)(x1 - x2) * (int)(x3 - x2) <= 0) && ((int)(y1 - y2) * (int)(y3 - y2) <= 0);
}

static inline void drawBrushVector (vfont_t *ctx, const float x1, const float y1, const float x2, const float y2, const uint16_t colour)
{
  if (ctx->brush.size > 1.0f) {
    float i = 0.0f;
    float x = x1;
    float y = y1;
    const float bmul = ctx->brush.advanceMult;

    while (distance(x, y, x2, y2) > bmul && isBetween(x1, y1, x, y, x2, y2)) {
      i += 1.0f;
      x = (x1 + i * bmul * (x2 - x1) / distance(x1, y1, x2, y2));
      y = (y1 + i * bmul * (y2 - y1) / distance(x1, y1, x2, y2));

      drawBrush(ctx, x, y, ctx->brush.size / 2.0f, colour);
    }
  }
  else
  {
    drawLine(x1, y1, x2, y2, colour);
  }
}

static inline void rotateZ (rotate_t *rot, const float x, const float y, float *xr, float *yr)
{
  *xr = x * rot->cos - y * rot->sin;
  *yr = x * rot->sin + y * rot->cos;
}

static inline void drawVector (vfont_t *ctx, float x1, float y1, float x2, float y2)
{
  if (ctx->renderOp == RENDEROP_NONE) {
    drawBrushVector(ctx, x1, y1, x2, y2, ctx->brush.colour);
    return;

  } else {
    // transform to 0
    x1 -= ctx->x; x2 -= ctx->x;
    y1 -= ctx->y; y2 -= ctx->y;
  }


  if (ctx->renderOp & RENDEROP_SHEAR_X) {
    x1 += y1 * ctx->shear.tan;
    x2 += y2 * ctx->shear.tan;
  }

  if (ctx->renderOp & RENDEROP_SHEAR_Y) {
    y1 = (x1 * ctx->shear.sin + y1 * ctx->shear.cos) + 1.0f;
    y2 = (x2 * ctx->shear.sin + y2 * ctx->shear.cos) + 1.0f;
  }

  if (ctx->renderOp & RENDEROP_ROTATE_GLYPHS) {
    //float x1r, y1r, x2r, y2r;

    // undo string transform
    x1 += ctx->x; x2 += ctx->x;
    y1 += ctx->y; y2 += ctx->y;

    // apply glyph transform
    x1 -= ctx->pos.x; x2 -= ctx->pos.x;
    y1 -= ctx->pos.y; y2 -= ctx->pos.y;

    rotateZ(&ctx->rotate.glyph, x1, y1, &x1, &y1);
    rotateZ(&ctx->rotate.glyph, x2, y2, &x2, &y2);

    x1 += ctx->pos.x - ctx->x; x2 += ctx->pos.x - ctx->x;
    y1 += ctx->pos.y - ctx->y; y2 += ctx->pos.y - ctx->y;

  }

  if (ctx->renderOp & RENDEROP_ROTATE_STRING) {
    rotateZ(&ctx->rotate.string, x1, y1, &x1, &y1);
    rotateZ(&ctx->rotate.string, x2, y2, &x2, &y2);
  }

  //  transform back
  x1 += ctx->x; x2 += ctx->x;
  y1 += ctx->y; y2 += ctx->y;

  // now draw the brush
  drawBrushVector(ctx, x1, y1, x2, y2, ctx->brush.colour);

}

static inline float char2float (vfont_t *ctx, const uint8_t c)
{
  return ctx->scale.glyph * (float)(c - 'R');
}

// returns horizontal glyph advance
static inline float drawGlyph (vfont_t *ctx, const hfont_t *font, const uint16_t c)
{

  if (c >= font->glyphCount) return 0.0f;

  //const uint8_t *hc = (uint8_t*)font->glyphs[c].data;
  const uint8_t *hc = (uint8_t*)font->glyphs[c];
  const float lm = char2float(ctx, *hc++) * fabs(ctx->scale.horizontal);
  const float rm = char2float(ctx, *hc++) * fabs(ctx->scale.horizontal);

  ctx->pos.x -= lm;

  float x1 = 0.0f;
  float y1 = 0.0f;
  float x2, y2;
  int newPath = 1;


  while (*hc) {
    if (*hc == ' ') {
      hc++;
      newPath = 1;

    } else {
      const float x = char2float(ctx, *hc++) * ctx->scale.horizontal;
      const float y = char2float(ctx, *hc++) * ctx->scale.vertical;

      if (newPath) {
        newPath = 0;
        x1 = ctx->pos.x + x;
        y1 = ctx->pos.y + y;
        //drawCircleFilled(x1, y1, 3, COLOUR_GREEN);  // path start

      } else {
        x2 = ctx->pos.x + x;
        y2 = ctx->pos.y + y;

        drawVector(ctx, x1, y1, x2, y2);
        x1 = x2;
        y1 = y2;
        //if (*hc == ' ' || !*hc) drawCircleFilled(x1, y1, 3, COLOUR_BLUE); // path end
      }
    }
  }

  ctx->pos.x += rm + ctx->xpad;
  return (rm - lm) + ctx->xpad;
}

// returns scaled glyph stride
float getCharMetrics (vfont_t *ctx, const hfont_t *font, const uint16_t c, float *adv, box_t *box)
{

  if (c >= font->glyphCount) return 0.0f;

  //const uint8_t *hc = (uint8_t*)font->glyphs[c].data;
  const uint8_t *hc = (uint8_t*)font->glyphs[c];
  const float lm = char2float(ctx, *hc++) * fabs(ctx->scale.horizontal);
  const float rm = char2float(ctx, *hc++) * fabs(ctx->scale.horizontal);

  float startX;
  if (adv)
    startX = *adv - lm;
  else
    startX = 0.0f - lm;


  float startY = 0.0f;
  float miny = 9999.0f;
  float maxy = -9999.0f;
  float minx = 9999.0f;
  float maxx = -9999.0f;


  while (*hc) {
    if (*hc == ' ') {
      hc++;

    } else {
      const float x = char2float(ctx, *hc++);
      if (x > maxx) maxx = x;
      if (x < minx) minx = x;

      const float y = char2float(ctx, *hc++);
      if (y > maxy) maxy = y;
      if (y < miny) miny = y;
    }
  }

  minx *= fabs(ctx->scale.horizontal);
  maxx *= fabs(ctx->scale.horizontal);
  miny *= fabs(ctx->scale.vertical);
  maxy *= fabs(ctx->scale.vertical);

  float brushSize = ctx->brush.size / 2.0f;
  box->x1 = (startX + minx) - brushSize;
  box->y1 = (startY + miny) - brushSize;
  box->x2 = (startX + maxx) + brushSize;
  box->y2 = (startY + maxy) + brushSize;

  if (adv) *adv = startX + rm + ctx->xpad;
  return startX + rm + ctx->xpad;
}

void getGlyphMetrics (vfont_t *ctx, const uint16_t c, int *w, int *h)
{
  box_t box = {0};
  getCharMetrics(ctx, ctx->font, c - 32, NULL, &box);

  if (w) *w = ((box.x2 - box.x1) + 1.0f) + 0.5f;
  if (h) *h = ((box.y2 - box.y1) + 1.0f) + 0.5f;
}

void getStringMetrics (vfont_t *ctx, const char *text, box_t *box)
{
#if 0
  int x = ctx->x;
  int y = ctx->y;
#endif

  float miny = 9999.0f;
  float maxy = -9999.0f;
  float minx = 9999.0f;
  float maxx = -9999.0f;
  float adv = 0.0f;

  while (*text) {
    getCharMetrics(ctx, ctx->font, (*text++) - 32, &adv, box);
#if 0
    drawRectangle(x + box->x1, y + box->y1, x + box->x2, y + box->y2, COLOUR_BLUE);
#endif

    if (box->x1 < minx) minx = box->x1;
    if (box->x2 > maxx) maxx = box->x2;
    if (box->y1 < miny) miny = box->y1;
    if (box->y2 > maxy) maxy = box->y2;
  }

  box->x1 = minx;
  box->y1 = miny;
  box->x2 = maxx;
  box->y2 = maxy;
}

void drawString (vfont_t *ctx, const char *text, const int x, const int y)
{
  ctx->x = ctx->pos.x = x;
  ctx->y = ctx->pos.y = y;


  while (*text)
    drawGlyph(ctx, ctx->font, (*text++) - 32);
}

int setBrushBitmap (vfont_t *ctx, const void *bitmap, const uint8_t width, const uint8_t height)
{
  ctx->brush.image.pixels = (uint8_t*)bitmap;
  ctx->brush.image.width = width;
  ctx->brush.image.height = height;

  return (bitmap && width && height);
}

void setFont (vfont_t *ctx, const hfont_t *font)
{
  if (font)
    ctx->font = font;
}

const hfont_t *getFont (vfont_t *ctx)
{
  return ctx->font;
}

int setBrush (vfont_t *ctx, const int brush)
{
  if (brush < BRUSH_TOTAL) {
    int old = ctx->brush.type;
    ctx->brush.type = brush;
    return old;
  }
  return ctx->brush.type;
}

// calculate glyph stride
static inline void brushCalcAdvance (vfont_t *ctx)
{
  ctx->brush.advanceMult = (ctx->brush.size * ctx->brush.step) / 100.0f;
}

float setBrushSize (vfont_t *ctx, const float size)
{
  if (size >= 0.5f) {
    float old = ctx->brush.size;
    ctx->brush.size = size;
    brushCalcAdvance(ctx);
    return old;
  }
  return ctx->brush.size;
}

// 0.5 to 100.0
void setBrushStep (vfont_t *ctx, const float step)
{
  if (step >= 0.5f) {
    ctx->brush.step = step;
    brushCalcAdvance(ctx);
  }
}

float getBrushStep (vfont_t *ctx)
{
  return ctx->brush.step;
}

void setGlyphScale (vfont_t *ctx, const float scale)
{
  if (scale >= 0.1f)
    ctx->scale.glyph = scale;
}

float getGlyphScale (vfont_t *ctx)
{
  return ctx->scale.glyph;
}

// Extra space added to every glyph. (can be 0 or minus)
void setGlyphPadding (vfont_t *ctx, const float pad)
{
  ctx->xpad = pad;
}

float getGlyphPadding (vfont_t *ctx)
{
  return ctx->xpad;
}

uint16_t setBrushColour (vfont_t *ctx, const uint16_t colour)
{
  uint16_t old = ctx->brush.colour;
  ctx->brush.colour = colour;
  return old;
}

uint16_t getBrushColour (vfont_t *ctx)
{
  return ctx->brush.colour;
}

void setRenderFilter (vfont_t *ctx, const uint32_t op)
{
  ctx->renderOp = op & 0xFFFF;
}

uint32_t getRenderFilter (vfont_t *ctx)
{
  return ctx->renderOp;
}

// when rotation from horizontal, when enabled via setRenderFilter()
void setRotationAngle (vfont_t *ctx, const float rotGlyph, const float rotString)
{
  const float oldG = ctx->rotate.glyph.angle;
  ctx->rotate.glyph.angle = fmodf(rotGlyph, 360.0f);

  if (oldG != ctx->rotate.glyph.angle) {
    ctx->rotate.glyph.cos = cosf(DEG2RAD(ctx->rotate.glyph.angle));
    ctx->rotate.glyph.sin = sinf(DEG2RAD(ctx->rotate.glyph.angle));
  }

  const float oldS = ctx->rotate.string.angle;
  ctx->rotate.string.angle = fmodf(rotString, 360.0f);

  if (oldS != ctx->rotate.string.angle) {
    ctx->rotate.string.cos = cosf(DEG2RAD(ctx->rotate.string.angle));
    ctx->rotate.string.sin = sinf(DEG2RAD(ctx->rotate.string.angle));
  }

}

void setShearAngle (vfont_t *ctx, const float shrX, const float shrY)
{
  ctx->shear.angleX = shrX;
  ctx->shear.angleY = shrY;

  ctx->shear.tan = -tanf(DEG2RAD(ctx->shear.angleX) /*/ 2.0f*/);
  ctx->shear.cos = cosf(DEG2RAD(ctx->shear.angleY));
  ctx->shear.sin = sinf(DEG2RAD(ctx->shear.angleY));
}


void setAspect (vfont_t *ctx, const float hori, const float vert)
{
  if (hori >= 0.05f || hori <= -0.05f)
    ctx->scale.horizontal = hori;

  if (vert >= 0.05f || vert <= -0.05f)
    ctx->scale.vertical = vert;
}

void vfontInitialise (vfont_t *ctx)
{
  memset(ctx, 0, sizeof(*ctx));

  //renderBuffer = (uint8_t*)frame->pixels;

  setAspect(ctx, 1.0f, 1.0f);
  setGlyphPadding(ctx, -1.0f);
  setGlyphScale(ctx, 1.0f);
  setBrush(ctx, BRUSH_DISK);
  setBrushSize(ctx, 1.0f);
  setBrushStep(ctx, 1.0f);
  // vstcm mod needed
  //  setBrushColour(ctx, COLOUR_RED);
  setRenderFilter(ctx, RENDEROP_NONE);
}

#if 0
int main (int argc, char **argv)
{
  vfont_t context;
  vfont_t *ctx = &context;
  vfontInitialise(ctx);

  setFont(ctx, FONT_Roman_S);
  setGlyphPadding(ctx, -1.5f);
  setGlyphScale(ctx, 3.8f);
  setBrushColour(ctx, COLOUR_RED);
  setRenderFilter(ctx, RENDEROP_NONE);

  //setRenderFilter(ctx, RENDEROP_ROTATE_GLYPHS | RENDEROP_ROTATE_STRING);
  setRotationAngle(ctx, -45.0f, 25.0f);
  setShearAngle(ctx, 35.0f, 0.0f);

  //setBrush(ctx, BRUSH_BITMAP);
  //setBrushBitmap(ctx, smiley16x16, 16, 16);

  int x = 25; int y = 80;

  setGlyphScale(ctx, 4.0f);
  setGlyphPadding(ctx, -2.5f);
  setBrush(ctx, BRUSH_DISK);

  const int n = 1;

  for (int i = 0; i < n; i++) 
  {
    setAspect(ctx, 1.0f, 1.0f);
    setGlyphScale(ctx, 4.0f);
    setGlyphPadding(ctx, -2.5f);
    setBrush(ctx, BRUSH_DISK);
    setBrushStep(ctx, 8.0f);
    setBrushSize(ctx, 60.0f);
    setBrushColour(ctx, COLOUR_24TO16(0x00AA55));
    drawString(ctx, "Hello World!", x, y);

    setBrushStep(ctx, 4.0f);
    setBrushSize(ctx, 19.0f);
    setBrushColour(ctx, COLOUR_24TO16(0x444444));
    drawString(ctx, "Hello World!", x, y);

    setBrushStep(ctx, 2.0f);
    setBrushSize(ctx, 4.0f);
    setBrushColour(ctx, COLOUR_24TO16(0xFFBF33));
    drawString(ctx, "Hello World!", x, y);

    setBrushColour(ctx, COLOUR_24TO16(0x444444));
    setBrushStep(ctx, 1.0f);
    setBrushSize(ctx, 1.0f);
    drawString(ctx, "Hello World!", x, y);

    setBrush(ctx, BRUSH_STROKE_6);
    setGlyphPadding(ctx, -2.5f);
    setBrushStep(ctx, 1.0f);
    setBrushSize(ctx, 13.0f);
    setBrushColour(ctx, COLOUR_24TO16(0xFF0045));
    drawString(ctx, "0123456789", 5, y + 125);

    setAspect(ctx, -1.0f, 1.0f);
    //setGlyphPadding(ctx, 0.0f);
    setGlyphScale(ctx, 4.0f);
    setBrushStep(ctx, 1.0f);
    setBrushSize(ctx, 1.0f);
    setBrushColour(ctx, COLOUR_24TO16(0xFF0045));
    drawString(ctx, "0123456789", 5, y + 220);
  }
}
#endif
