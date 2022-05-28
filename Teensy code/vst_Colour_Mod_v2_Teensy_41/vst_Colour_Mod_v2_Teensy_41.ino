/*
 * VSTCM
 * 
 * Vector display using MCP4922 DACs on the Teensy 4.1
 *
 * Based on: https://trmm.net/V.st
 * incorporating mods made by "Swapfile" (Github) for Advanced Mame compatibility
 *
 * robin@robinchampion.com 2022
 * 
*/

#include <SPI.h>
#include "asteroids_font.h"
#include <avr/eeprom.h>

// Teensy SS pins connected to DACs
const int SS0_IC5_RED     =  8;       // RED output
const int SS1_IC4_X_Y     =  6;       // X and Y outputs
const int SS2_IC3_GRE_BLU = 22;       // GREEN and BLUE outputs

// Split control of DACs between 2 SPI busses to hopefully increase speed
#define SDI                 11        // MOSI on SPI0
#define SCK                 13        // SCK on SPI0
#define BUFFERED                      // If defined, uses buffer on DACs
//#undef BUFFERED
#define REST_X            2048        // Wait in the middle of the screen
#define REST_Y            2048

// how long in milliseconds to wait for data before displaying a test pattern
#define SERIAL_WAIT_TIME 30

// Settings
static int NORMAL_SHIFT =    2;       // The higher the number, the less flicker and faster draw but more wavy lines
static int OFF_SHIFT    =    5;       // 35111 Smaller numbers == slower transits (the higher the number, the less flicker and faster draw but more wavy lines)
static int OFF_DWELL0   =    0;       // Time to sit beam on before starting a transit 
static int OFF_DWELL1   =    2;       // Time to sit before starting a transit
static int OFF_DWELL2   =    2;       // Time to sit after finishing a transit
static int OFF_JUMP     = false;       
static int FLIP_X       = false;      // Sometimes the X and Y need to be flipped and/or swapped
static int FLIP_Y       = false;
static int SWAP_XY      = true;
static int CLOCKSPEED   = 115000000;  // Can't seem to drive it any faster than this!

// Current position of beam
static uint16_t x_pos;
static uint16_t y_pos;

// Protocol flags from AdvanceMAME
#define FLAG_COMPLETE         0x0
#define FLAG_RGB              0x1
#define FLAG_XY               0x2
#define FLAG_EXIT             0x7
#define FLAG_FRAME            0x4
#define FLAG_CMD              0x5
#define FLAG_CMD_GET_DVG_INFO 0x1
#define FLAG_COMPLETE_MONOCHROME (1 << 28)

// Defines channel A or B of each MCP4922 dual DAC
#define DAC_CHAN_A 0
#define DAC_CHAN_B 1

// Used to flip X & Y axis if needed
static int DAC_X_CHAN = 1;
static int DAC_Y_CHAN = 0;

typedef struct ColourIntensity {                  // Stores current levels of brightness for each colour
    uint8_t red;
    uint8_t green;
    uint8_t blue;
} ColourIntensity_t;

static ColourIntensity_t LastColInt;              // Stores last colour intensity levels

// How often should a frame be drawn if we haven't received any serial
// data from MAME (in ms).
#define REFRESH_RATE 20000u

// Settings stored in Teensy EPROM
typedef struct
{
  int config_ok;
  int off_shift = OFF_SHIFT;
  int off_dwell0 = OFF_DWELL0;
  int off_dwell1 = OFF_DWELL1;
  int off_dwell2 = OFF_DWELL2;
  int normal_shift = NORMAL_SHIFT;
  int flip_x = FLIP_X;
  int flip_y = FLIP_Y;
  int swap_xy = SWAP_XY;
  int off_jump = OFF_JUMP;
  int clockspeed = CLOCKSPEED;
} settingsType;

settingsType settings;

// Approximate Frames Per Second used to benchmark code performance improvements
long fps;

EventResponder callbackHandler;
volatile int activepin;
volatile bool show_test_pattern;

// Don't think using DMAMEM or aligned makes any difference to a local variable in this case
DMAMEM char dmabuf[2] __attribute__((aligned(32)));

void setup()
{  
  // Read saved settings
  eeprom_read_block((void*)&settings, (void*)0, sizeof(settingsType));

  // If settings have been previously defined, then use them, otherwise use default values
/*  if (settings.config_ok == 999)
    {
    OFF_SHIFT = settings.off_shift;
    OFF_DWELL0 = settings.off_dwell0;
    OFF_DWELL1 = settings.off_dwell1;
    OFF_DWELL2 = settings.off_dwell2;
    NORMAL_SHIFT = settings.normal_shift;
    FLIP_X = settings.flip_x;
    FLIP_Y = settings.flip_y;
    SWAP_XY = settings.swap_xy;
    OFF_JUMP = settings.off_jump;  
    CLOCKSPEED = settings.clockspeed;
    }
*/
  if (settings.swap_xy == true)
    {
    DAC_X_CHAN = 0;
    DAC_Y_CHAN = 1;
    }
    
  // Configure buttons on vstcm for input
  pinMode(0, INPUT_PULLUP);
  pinMode(1, INPUT_PULLUP);
  pinMode(2, INPUT_PULLUP);
  pinMode(3, INPUT_PULLUP);
  pinMode(4, INPUT_PULLUP); 

  // Apparently, pin 10 has to be defined as an OUTPUT pin to designate the Arduino as the SPI master.
  // Even if pin 10 is not being used... Is this true for Teensy 4.1?
  // The default mode is INPUT. You must explicitly set pin 10 to OUTPUT (and leave it as OUTPUT).
  pinMode(10, OUTPUT);
  digitalWriteFast(10, HIGH);  
  delayNanoseconds(100);
 
  // Set chip select pins to output
  pinMode(SS0_IC5_RED, OUTPUT);
  digitalWriteFast(SS0_IC5_RED, HIGH);
  delayNanoseconds(100);
  pinMode(SS1_IC4_X_Y, OUTPUT);
  digitalWriteFast(SS1_IC4_X_Y, HIGH);
  delayNanoseconds(100);
  pinMode(SS2_IC3_GRE_BLU, OUTPUT);
  digitalWriteFast(SS2_IC3_GRE_BLU, HIGH);
  delayNanoseconds(100);

  pinMode(SDI, OUTPUT);       // Set up clock and data output to DACs
  pinMode(SCK, OUTPUT);
    
  delay(1);         // https://www.pjrc.com/better-spi-bus-design-in-3-steps/

  SPI.begin();
  
  // Setup the SPI DMA callback
  callbackHandler.attachImmediate(&callback); 
  callbackHandler.clearEvent();  
  SPI.beginTransaction(SPISettings(CLOCKSPEED, MSBFIRST, SPI_MODE0));
  SPI.endTransaction();

  show_test_pattern = true;
}

void loop()
{
  elapsedMicros waiting;    // Auto updating, used for FPS calculation

  uint32_t draw_start_time = millis();

  while (1) 
    {
    if (Serial.available()) 
      {
      show_test_pattern = false;
      if (read_data() == 1)
        break;
      }
      
    if (millis() - draw_start_time > SERIAL_WAIT_TIME)
      show_test_pattern = true;
   
    if (show_test_pattern)
      break;
    }

  if (show_test_pattern)
    draw_test_pattern();
    
  // Go to the center of the screen, turn the beam off
  brightness(0, 0, 0);
  goto_x(REST_X);
  goto_y(REST_Y);

  // Use the buttons on the PCB to adjust and save settings
  // This needs to be rewritten in order to use the buttons to navigate and modify a list of settings
  // Also it doesn't need to test all this stuff on every cycle, so add a test to do it 1/100 times or so
  
  bool write_settings = false;
  
  if (!digitalReadFast(3) == HIGH)           
    {
    settings.config_ok = 999;           // Indicate that settings have been defined
    write_settings = true;
    }

  if (!digitalReadFast(0) == HIGH)  
    {
    settings.swap_xy = !settings.swap_xy;  // Swap X & Y axes
    write_settings = true;    

    if (DAC_X_CHAN == 1)
      {
      DAC_X_CHAN = 0;
      DAC_Y_CHAN = 1;
      }
    else
      {
      DAC_X_CHAN = 1;
      DAC_Y_CHAN = 0;
      }
    }
 
  if (!digitalReadFast(1) == HIGH)      // Flip X axis
    {
    settings.flip_x = !settings.flip_x;  
    write_settings = true; 
    }
 
  if (!digitalReadFast(2) == HIGH)      // Flip Y axis
    {
    settings.flip_y = !settings.flip_y;  
    write_settings = true;    
    }
   
  if (!digitalReadFast(4) == HIGH)    // Restore some default settings
    {
    settings.off_shift =5;
    settings.off_dwell0 = 0;
    settings.off_dwell1 = 2;
    settings.off_dwell2 = 2;
    settings.normal_shift = 2;
    write_settings = true;    
    }
    
  if (write_settings == true)     // If something has changed, then update the settings in EPROM
    {
    // write settings
 //   eeprom_write_block((const void*)&settings, (void*)0, sizeof(settingsType));   // disabled for now while testing
    }

  fps = 1000000 / waiting;  
}

static void draw_test_pattern()
{
  unsigned i, j;
  
  // fill in some points for test and calibration
  draw_moveto(0, 0);
  draw_to_xyrgb(512,0, 128, 128, 128);
  draw_to_xyrgb(512,512, 128, 128, 128);
  draw_to_xyrgb(0,512, 128, 128, 128);
  draw_to_xyrgb(0,0, 128, 128, 128);

  // triangle
  draw_moveto(4095, 0);
  draw_to_xyrgb(4095-512, 0, 128, 128, 128);
  draw_to_xyrgb(4095-0, 512, 128, 128, 128);
  draw_to_xyrgb(4095,0, 128, 128, 128);

  // cross
  draw_moveto(4095,4095);
  draw_to_xyrgb(4095-512,4095, 128, 128, 128);
  draw_to_xyrgb(4095-512,4095-512, 128, 128, 128);
  draw_to_xyrgb(4095,4095-512, 128, 128, 128);
  draw_to_xyrgb(4095,4095, 128, 128, 128);

  draw_moveto(0,4095);
  draw_to_xyrgb(512,4095, 128, 128, 128);
  draw_to_xyrgb(0,4095-512, 128, 128, 128);
  draw_to_xyrgb(512, 4095-512, 128, 128, 128);
  draw_to_xyrgb(0,4095, 128, 128, 128);

  const uint16_t height = 3072; 
  
  // and a multi-coloured gradiant scale

  const int mult = 4;
 
  for(i = 0, j = 0 ; j <= 256 ; i += 8, j+= 32)
    {
    draw_moveto(1600, height + i * mult);
    draw_to_xyrgb(1900, height + i * mult, j, 0, 0);     // Red
    draw_moveto(2000, height + i * mult);
    draw_to_xyrgb(2300, height + i * mult, 0, j, 0);     // Green
    draw_moveto(2400, height + i * mult);
    draw_to_xyrgb(2700, height + i * mult, 0, 0, j);     // Blue
    draw_moveto(2800, height + i * mult);
    draw_to_xyrgb(3100, height + i * mult, j, j, j);     // all 3 colours combined
    }

  draw_string("v.st Colour Mod v2.1", 1300, 3500, 8);

  char buf1[15] = "";
   
  const int x = 1500;
  int y = 2400;
  const int line_size = 100;

  draw_string("AMPLIFONE / WG6100", x, y, 6); 
  y -= line_size;
  draw_string("CONFIG OK", x, y, 6); 
  itoa(settings.config_ok, buf1, 10);
  draw_string(buf1, x + 900, y, 6); 
  y -= line_size;
  draw_string("OFF DWELL0", x, y, 6); 
  itoa(settings.off_dwell0, buf1, 10);
  draw_string(buf1, x + 900, y, 6); 
  y -= line_size;
  draw_string("OFF DWELL1", x, y, 6); 
  itoa(settings.off_dwell1, buf1, 10);
  draw_string(buf1, x + 900, y, 6); 
  y -= line_size;
  draw_string("OFF DWELL2", x, y, 6); 
  itoa(settings.off_dwell2, buf1, 10);
  draw_string(buf1, x + 900, y, 6); 
  y -= line_size;
  draw_string("NORMAL SHIFT", x, y, 6); 
  itoa(settings.normal_shift, buf1, 10);
  draw_string(buf1, x + 900, y, 6); 
  y -= line_size;
  draw_string("FLIP X", x, y, 6); 
  itoa(settings.flip_x, buf1, 10);
  draw_string(buf1, x + 900, y, 6); 
  y -= line_size;
  draw_string("FLIP Y", x, y, 6); 
  itoa(settings.flip_y, buf1, 10);
  draw_string(buf1, x + 900, y, 6); 
  y -= line_size;
  draw_string("SWAP XY", x, y, 6); 
  itoa(settings.swap_xy, buf1, 10);
  draw_string(buf1, x + 900, y, 6); 
  y -= line_size;
  draw_string("OFF JUMP", x, y, 6); 
  itoa(settings.off_jump, buf1, 10);
  draw_string(buf1, x + 900, y, 6); 
  y -= line_size;

  draw_string("FPS:", 3000, 150, 6);
  draw_string(itoa(fps, buf1, 10), 3400, 150, 6);
}

void draw_to_xyrgb(int x, int y, uint8_t red, uint8_t green, uint8_t blue)
{
  brightness(red, green, blue);   // Set RGB intensity levels from 0 to 255     
  _draw_lineto(x, y, NORMAL_SHIFT);   
}

void draw_string(const char *s, int x, int y, int size)
{
  while(*s)
    {
    char c = *s++;
    x += draw_character(c, x, y, size);
    }
}

int draw_character(char c, int x, int y, int size)
{
  // Asteroids font only has upper case
  if ('a' <= c && c <= 'z')
    c -= 'a' - 'A';

  const uint8_t * const pts = asteroids_font[c - ' '].points;
  int next_moveto = 1;

  for(int i = 0 ; i < 8 ; i++)
    {
    uint8_t delta = pts[i];
    if (delta == FONT_LAST)
      break;
    if (delta == FONT_UP)
      {
      next_moveto = 1;
      continue;
      }

    unsigned dx = ((delta >> 4) & 0xF) * size;
    unsigned dy = ((delta >> 0) & 0xF) * size;

    if (next_moveto)
      draw_moveto(x + dx, y + dy);
    else
      draw_to_xyrgb(x + dx, y + dy, 128, 128, 128);

    next_moveto = 0;
    }

  return 12 * size;
}

void draw_moveto(int x1, int y1)
{
  brightness(0, 0, 0);

  if (settings.off_jump == true)
    {
    goto_x(x1);
    goto_y(y1);
    }
  else
    {
    // hold the current position for a few clocks
    // with the beam off
    dwell(OFF_DWELL1);
    _draw_lineto(x1, y1, OFF_SHIFT);
    dwell(OFF_DWELL2);
    }
}

static inline void brightness(uint8_t red, uint8_t green, uint8_t blue)
{
  dwell(OFF_DWELL0);

  if (LastColInt.red != red)
    {
    LastColInt.red = red;
    MCP4922_write(SS0_IC5_RED, DAC_CHAN_B, red << 4);
    }
 
  if (LastColInt.green != green)
    {
    LastColInt.green = green;
    MCP4922_write(SS2_IC3_GRE_BLU, DAC_CHAN_A, green << 4);  
    }
  
  if (LastColInt.blue != blue)
    {
    LastColInt.blue = blue;
    MCP4922_write(SS2_IC3_GRE_BLU, DAC_CHAN_B, blue << 4); 
    }
}

// x and y position are in 12-bit range

static inline void goto_x(uint16_t x)
{
  x_pos = x;

  if (settings.flip_x == true)
    MCP4922_write(SS1_IC4_X_Y, DAC_X_CHAN, 4095 - x);  
  else
    MCP4922_write(SS1_IC4_X_Y, DAC_X_CHAN, x); 
}

static inline void goto_y(uint16_t y)
{
  y_pos = y;

  if (settings.flip_y == true)
    MCP4922_write(SS1_IC4_X_Y, DAC_Y_CHAN, 4095 - y); 
  else
    MCP4922_write(SS1_IC4_X_Y, DAC_Y_CHAN, y);
}

static void dwell(const int count)
{
 // can work better or faster without this on some monitors
 for (int i = 0 ; i < count ; i++)
  {
    if (i & 1)
      goto_x(x_pos);
    else
      goto_y(y_pos);
  } 
}

static inline void _draw_lineto(int x1, int y1, const int bright_shift)
{
  int dx, dy, sx, sy;

  const int x1_orig = x1;
  const int y1_orig = y1;

  int x_off = x1 & ((1 << bright_shift) - 1);
  int y_off = y1 & ((1 << bright_shift) - 1);
  x1 >>= bright_shift;
  y1 >>= bright_shift;
  int x0 = x_pos >> bright_shift;
  int y0 = y_pos >> bright_shift;

  goto_x(x_pos);
  goto_y(y_pos);

  if (x0 <= x1)
    {
    dx = x1 - x0;
    sx = 1;
    }
  else
    {
    dx = x0 - x1;
    sx = -1;
    }

  if (y0 <= y1)
    {
    dy = y1 - y0;
    sy = 1;
    }
  else
    {
    dy = y0 - y1;
    sy = -1;
    }

  int err = dx - dy;

  while (1)
    {
    if (x0 == x1 && y0 == y1)
      break;

    int e2 = 2 * err;
    if (e2 > -dy)
      {
      err = err - dy;
      x0 += sx;
      goto_x(x_off + (x0 << bright_shift));
      }
    if (e2 < dx)
      {
      err = err + dx;
      y0 += sy;
      goto_y(y_off + (y0 << bright_shift));
      }
    }

  // ensure that we end up exactly where we want
  goto_x(x1_orig);
  goto_y(y1_orig);
}

void MCP4922_write(int cs_pin, byte dac, uint16_t value) 
{
  dac = dac << 7; // dac value is either 0 or 128
  
  value &= 0x0FFF; // mask out just the 12 bits of data

  // add the output channel A or B on the selected DAC, and buffer flag
#ifdef BUFFERED
  // select the output channel on the selected DAC, buffered, no gain
  value |= 0x7000 | (dac == 128 ? 0x8000 : 0x0000);
#else
  // select the output channel on the selected DAC, unbuffered, no gain
  value |= 0x3000 | (dac == 128 ? 0x8000 : 0x0000);
#endif

  while(activepin != 0)   // wait until previous transfer is complete
    ;
        
  activepin = cs_pin;     // store to deactivate at end of transfer
  digitalWriteFast(cs_pin, LOW);
 
  dmabuf[0] = dac | 0x30 | ((value >> 8) & 0x0f);
  dmabuf[1] = value & 0xff;
  
  // This uses non blocking SPI with DMA
  SPI.transfer(dmabuf, nullptr, 2, callbackHandler);  
}

static int read_data()
{
  static uint32_t cmd = 0;
  static uint8_t gl_red, gl_green, gl_blue;
  static int frame_offset = 0;
  char buf1[5] = "";
  uint8_t c = -1;
  
  c = Serial.read();    // read one byte at a time

  if (c == -1) // if serial port returns nothing then exit
     return -1;
    
  cmd = cmd << 8 | c;
  frame_offset++;
  if (frame_offset < 4)
    return 0;

  frame_offset = 0;

  uint8_t header = (cmd >> 29) & 0b00000111;

  //common case first
  if (header == FLAG_XY)
    {
    uint32_t y = (cmd >> 0) & 0x3fff;
    uint32_t x = (cmd >> 14) & 0x3fff;
    
    // As an optimization there is a blank flag in the XY coord which
    // allows USB-DVG to blank the beam without updating the RGB color DACs.

    if ((cmd >> 28) & 0x01)
      draw_moveto( x, y );
     else
      {
      brightness(gl_red, gl_green, gl_blue);   // Set RGB intensity levels      
      _draw_lineto(x, y, NORMAL_SHIFT);   
      }
    }
  else if (header == FLAG_RGB)
    {
    // encode brightness for R, G and B using 2 bits for each (values 0, 1, 2 or 3), as total of 6 bits are available
    // these calculations can be optimised for speed
    gl_red   = (cmd >> 16) & 0xFF;
    gl_green = (cmd >> 8)  & 0xFF;
    gl_blue  = (cmd >> 0)  & 0xFF;  
    }
  else if (header == FLAG_COMPLETE_MONOCHROME)
    {
    // pick the max brightness from r g and b and use just Green
    gl_red = 0;
    gl_green = max(max((cmd >> 0) & 0xFF, (cmd >> 8) & 0xFF), (cmd >> 16) & 0xFF);
    gl_blue = 0;
    }
  else if (header == FLAG_FRAME)
    {
  //  uint32_t frame_complexity = cmd & 0b0001111111111111111111111111111;
    // TODO: Use frame_complexity to adjust screen writing algorithms
    }
  else if (header == FLAG_COMPLETE)
    {
    // Add FPS on games as a guide for optimisation
    draw_string("FPS:", 3000, 150, 6);
    draw_string(itoa(fps, buf1, 10), 3400, 150, 6);
    
    return 1;
    }
  else if (header == FLAG_EXIT)
    {
    Serial.flush();          // not sure if this useful, may help in case of manual quit on MAME
    return -1;
    }
  else if (header == FLAG_CMD_GET_DVG_INFO)
    {
    // provide a reply of some sort
    Serial.write(0xFFFFFFFF);
    }
  
  return 0;
}

void callback(EventResponderRef eventResponder)
{
  // End SPI DMA write to DAC
  digitalWriteFast(activepin, HIGH);
  activepin = 0;
}
