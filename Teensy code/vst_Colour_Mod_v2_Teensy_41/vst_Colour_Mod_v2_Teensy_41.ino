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
#define SDI                 11
#define SCK                 13
#define DELAY_PIN            7

#define REST_X            2048        // wait in the middle of the screen
#define REST_Y            2048

// Settings
static int OFF_SHIFT    =    5;       // smaller numbers == slower transits (the higher the number, the less flicker and faster draw but more wavy lines)
static int OFF_DWELL0   =    0;       // time to sit beam on before starting a transit 
static int OFF_DWELL1   =    2;       // time to sit before starting a transit
static int OFF_DWELL2   =    2;       // time to sit after finishing a transit
static int NORMAL_SHIFT =    3;       // The higher the number, the less flicker and faster draw but more wavy lines
static int OFF_JUMP     = false;      // Causes tails on vectors
static int FLIP_X       = false;      // Sometimes the X and Y need to be flipped and/or swapped
static int FLIP_Y       = false;
static int SWAP_XY      = true;
static int CLOCKSPEED   = 110000000;  // Can't seem to drive it any faster than this

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

#define MAX_PTS 3000
static unsigned rx_points;
static unsigned num_points;

typedef struct ColourIntensity {                  // Stores current levels of brightness for each colour
    uint8_t red;
    uint8_t green;
    uint8_t blue;
} ColourIntensity_t;

static ColourIntensity_t LastColInt;              // Stores last colour intensity levels

// Chunk of data to process using DMA or SPI
typedef struct DataChunk {
    uint16_t x;                                   // We'll just use 12 bits of X & Y for a 4096 point resolution
    uint16_t y;
    uint8_t red;                                  // Max value of each colour is 255
    uint8_t green;
    uint8_t blue;
} DataChunk_t;

static DataChunk_t Chunk[MAX_PTS];

// Current intensity values
static uint8_t gl_red, gl_green, gl_blue;

// How often should a frame be drawn if we haven't received any serial
// data from MAME (in ms).
#define REFRESH_RATE 20000u

// Settings stored in Teensy EPROM
typedef struct
{
  int config_ok;
  int off_shift;
  int off_dwell0;
  int off_dwell1;
  int off_dwell2;
  int normal_shift;
  int flip_x;
  int flip_y;
  int swap_xy;
  int off_jump;
  int clockspeed;
} settingsType;

settingsType settings;

// Approximate Frames Per Second used to benchmark code performance improvements
long fps;

EventResponder callbackHandler;
volatile int activepin = 0;

void setup()
{  
  // Read saved settings
  eeprom_read_block((void*)&settings, (void*)0, sizeof(settingsType));

  // If settings have been previously defined, then use them, otherwise use default values
  if (settings.config_ok == 998)
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

  pinMode(DELAY_PIN, OUTPUT);
  digitalWriteFast(DELAY_PIN, 0);
  delayNanoseconds(100);
  
  pinMode(SDI, OUTPUT);
  pinMode(SCK, OUTPUT);
  
  delay(1);         // https://www.pjrc.com/better-spi-bus-design-in-3-steps/

  draw_test_pattern();

  SPI.begin();
  
  // Setup the SPI DMA callback
  callbackHandler.attachImmediate(&callback); 
  callbackHandler.clearEvent();  
  SPI.beginTransaction(SPISettings(CLOCKSPEED, MSBFIRST, SPI_MODE0));
  SPI.endTransaction();
}

void loop()
{
  static uint32_t frame_micros;
  uint32_t now;
  elapsedMicros waiting;    // Auto updating, used for FPS calculation

  // while loop waits until any DMA operation is complete, 
  // and serial port is available and read from serial is finished
  while(1)
    {
    now = micros();

    if (Serial.available())     // there is something to read from MAME
      if (read_data() == 1)     // read it, then draw it
        break;

    // rx_points = 0 when MAME has just sent a complete frame, 
    // and at the end of the test pattern redraw; 
    // num_points then contains the number of points to draw 
    // if rx_points = 0 then we can start to fill the buffer again 
       
    if (rx_points == 0 && now - frame_micros > REFRESH_RATE)
      break;    
    }
 
  frame_micros = now;

  for(unsigned n = 0 ; n < num_points ; n++)
    {
    const DataChunk_t pt = Chunk[n];
    
    if (pt.red + pt.green + pt.blue == 0) 
      draw_moveto(pt.x, pt.y);
    else
      {
      brightness(pt.red, pt.green, pt.blue);   // Set RGB intensity levels
      _draw_lineto(pt.x, pt.y, NORMAL_SHIFT);
      } 
    }
    
  // Go to the center of the screen, turn the beam off
  brightness(0, 0, 0);
  goto_x(REST_X);
  goto_y(REST_Y);

  // Use the buttons on the PCB to adjust and save settings
  // This needs to be rewritten in order to use the buttons to navigate and modify a list of settings
  // Also it doesn't need to test all this stuff on every cycle, so add a test to do it 1/100 times
 
  bool write_settings = false;
  
  if (!digitalRead(3) == HIGH)           
    {
    settings.config_ok = 999;           // Indicate that settings have been defined
    write_settings = true;
    }
    
  if (!digitalRead(0) == HIGH)  
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
 
  if (!digitalRead(1) == HIGH)      // Flip X axis
    {
    settings.flip_x = !settings.flip_x;  
    write_settings = true;   
    }
 
  if (!digitalRead(2) == HIGH)      // Flip Y axis
    {
    settings.flip_y = !settings.flip_y;  
    write_settings = true;    
    }
    
  if (!digitalRead(4) == HIGH)    // Restore some default settings
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
    draw_test_pattern();   
    delay(200);
    }

  fps = 1000000 / waiting;  
}

static void draw_test_pattern()
{
  int i;
  unsigned j;
  
  rx_points = 0;
  
  // fill in some points for test and calibration
  rx_append(0, 0, 0, 0, 0);
  lineto(512,0);
  lineto(512,512);
  lineto(0,512);
  lineto(0,0);

  // triangle
  rx_append(4095, 0, 0, 0, 0);
  lineto(4095-512, 0);
  lineto(4095-0, 512);
  lineto(4095,0);

  // cross
  moveto(4095,4095);
  lineto(4095-512,4095);
  lineto(4095-512,4095-512);
  lineto(4095,4095-512);
  lineto(4095,4095);

  moveto(0,4095);
  lineto(512,4095);
  lineto(0,4095-512);
  lineto(512, 4095-512);
  lineto(0,4095);

  const uint16_t height = 3072; 
  
  // and a multi-coloured gradiant scale

  int mult = 4;
 
  for(i = 0, j = 0 ; j <= 256 ; i += 8, j+= 32)
    {
    moveto(1600, height + i * mult);
    rx_append(1900, height + i * mult, j, 0, 0);     // Red
    moveto(2000, height + i * mult);
    rx_append(2300, height + i * mult, 0, j, 0);     // Green
    moveto(2400, height + i * mult);
    rx_append(2700, height + i * mult, 0, 0, j);     // Blue
    moveto(2800, height + i * mult);
    rx_append(3100, height + i * mult, j, j, j);     // all 3 colours combined
    }

  draw_string("v.st Colour Mod v2.1", 1300, 3500, 8);

  char buf1[15] = "";
   
  int x = 1500;
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
/*  draw_string("SPI CLOCK", x, y, 6); 
  ltoa(settings.clockspeed, buf1, 15);
  draw_string(buf1, x + 900, y, 6); 
  y -= line_size;
  draw_string("SERIAL PORT", x, y, 6); 
  if (Serial.available())  
    draw_string("AVAILABLE", x + 900, y, 6); 
  else
    draw_string("BUSY", x + 900, y, 6); 
*/
  draw_string("FPS:", 3000, 150, 6);
  draw_string(itoa(fps, buf1, 10), 3400, 150, 6);
  
  num_points = rx_points;
  rx_points = 0;
}

void moveto(int x, int y)
{
  rx_append(x, y, 0, 0, 0);
}

// Colour intensity can be any value from 0 to 255
void rx_append(int x, int y, uint8_t red, uint8_t green, uint8_t blue)
{
  rx_points ++;
  Chunk[rx_points].x = x & 0xFFF;
  Chunk[rx_points].y = y & 0xFFF;
  Chunk[rx_points].red = red;
  Chunk[rx_points].green = green;
  Chunk[rx_points].blue = blue;
}

void lineto(int x, int y)
{
  rx_append(x, y, 128, 128, 128); // Green normal brightness
}

void draw_string(const char * s, int x,int y, int size)
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
      moveto(x + dx, y + dy);
    else
      lineto(x + dx, y + dy);

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
  // works better without this on an Amplifone
/* for (int i = 0 ; i < count ; i++)
  {
    if (i & 1)
      goto_x(x_pos);
    else
      goto_y(y_pos);
  } */
}

static inline void _draw_lineto(int x1, int y1, const int bright_shift)
{
  int dx;
  int dy;
  int sx;
  int sy;

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
#if 1
  // select the output channel on the selected DAC, buffered, no gain
  value |= 0x7000 | (dac == 128 ? 0x8000 : 0x0000);
#else
  // select the output channel on the selected DAC, unbuffered, no gain
  value |= 0x3000 | (dac == 128 ? 0x8000 : 0x0000);
#endif

//  byte low = value & 0xff;
//  byte high = dac | 0x30 | ((value >> 8) & 0x0f);

  while(activepin != 0)   // wait until previous transfer is complete
    ;
      
  activepin = cs_pin;   // store to deactivate at end of transfer
  
  digitalWriteFast(cs_pin, LOW);

  char buf[2];
  buf[0] = dac | 0x30 | ((value >> 8) & 0x0f);
  buf[1] = value & 0xff;

  // This uses non blocking SPI with DMA
  SPI.transfer(buf, nullptr, 2, callbackHandler); 
  
 // SPI.transfer16(((high << 8) + low));    // Standard blocking SPI
 // digitalWriteFast(cs_pin, HIGH);
 // delayNanoseconds(100);
}

//return 1 when frame is complete, otherwise return 0
static int read_data()
{
  static uint32_t cmd = 0;
  static int frame_offset = 0;
  char buf1[5] = "";
    
  uint8_t c = Serial.read();    // read one byte at a time

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
    //blanking flag
    if ((cmd >> 28) & 0x01)
      rx_append(x, y, 0, 0, 0);
    else
      rx_append(x, y, gl_red, gl_green, gl_blue);
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
   // uint32_t frame_complexity = cmd & 0b0001111111111111111111111111111;
    // TODO: Use frame_complexity to adjust screen writing algorithms
    }
  else if (header == FLAG_COMPLETE)
    {
    // Add FPS on games as a guide for optimisation
    draw_string("FPS:", 3000, 150, 6);
    draw_string(itoa(fps, buf1, 10), 3400, 150, 6);
    
    num_points = rx_points;
    rx_points = 0;
    return 1; //start rendering!
    }
  else if (header == FLAG_EXIT)
    {
    Serial.flush(); // not sure if this useful, may help in case of manual quit on MAME
    // quit gracefully back to test pattern
    draw_test_pattern();
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
  // End screen update
  digitalWriteFast(activepin, HIGH);
  activepin = 0;
}
