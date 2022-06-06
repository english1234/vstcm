/*
   VSTCM

   Vector Signal Transceiver Colour Mod using MCP4922 DACs on the Teensy 4.1

   Based on: https://trmm.net/V.st
   incorporating mods made by "Swapfile" (Github) for Advanced Mame compatibility

   robin@robinchampion.com 2022

*/

#include <SPI.h>
#include <avr/eeprom.h>

#include "asteroids_font.h"

// Teensy SS pins connected to DACs
const int SS0_IC5_RED     =  8;       // RED output
const int SS1_IC4_X_Y     =  6;       // X and Y outputs
const int SS2_IC3_GRE_BLU = 22;       // GREEN and BLUE outputs

#define SDI                 11        // MOSI on SPI0
#define SCK                 13        // SCK on SPI0
#define BUFFERED                      // If defined, uses buffer on DACs
#define REST_X            2048        // Wait in the middle of the screen
#define REST_Y            2048

// how long in milliseconds to wait for data before displaying a test pattern
// this can be increased if the test pattern appears during gameplay
#define SERIAL_WAIT_TIME 100

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

typedef struct ColourIntensity {      // Stores current levels of brightness for each colour
  uint8_t red;
  uint8_t green;
  uint8_t blue;
} ColourIntensity_t;

// How often should a frame be drawn if we haven't received any serial
// data from MAME (in ms).
#define REFRESH_RATE 20000u

static long fps;                       // Approximate FPS used to benchmark code performance improvements

#define IR_REMOTE                      // define if IR remote is fitted
#ifdef IR_REMOTE
  #define SUPPRESS_ERROR_MESSAGE_FOR_BEGIN
  #include <IRremote.hpp>
  #define IR_RECEIVE_PIN      32
#endif

EventResponder callbackHandler;        // DMA SPI callback
volatile int activepin;                // Active CS pin of DAC receiving data
volatile bool show_vstcm_config;       // Shows settings if true

// Don't think using DMAMEM or aligned makes any difference to a local variable in this case
DMAMEM char dmabuf[2] __attribute__((aligned(32)));

static uint16_t x_pos;                 // Current position of beam
static uint16_t y_pos;

static ColourIntensity_t LastColInt;   // Stores last colour intensity levels

// Settings
const int  OFF_SHIFT    =     5;       // Smaller numbers == slower transits (the higher the number, the less flicker and faster draw but more wavy lines)
const int  OFF_DWELL0   =     0;       // Time to sit beam on before starting a transit
const int  OFF_DWELL1   =     0;       // Time to sit before starting a transit
const int  OFF_DWELL2   =     0;       // Time to sit after finishing a transit
const int  NORMAL_SHIFT =     3;       // The higher the number, the less flicker and faster draw but more wavy lines
const bool OFF_JUMP     = false;
const bool FLIP_X       = false;       // Sometimes the X and Y need to be flipped and/or swapped
const bool FLIP_Y       = false;
const bool SWAP_XY      = false;
const int  DAC_X_CHAN   =     0;       // Used to flip X & Y axis if needed
const int  DAC_Y_CHAN   =     1;
const uint32_t CLOCKSPEED   = 115000000;
const int  NORMAL       =    100;      // Brightness of text in parameter list
const int  BRIGHTER     =    128;

// Settings stored in Teensy EPROM
typedef struct params {
  const char *param;      // Parameter label
  uint32_t pval;          // Parameter value
  uint32_t min;           // Min value of parameter
  uint32_t max;           // Max value of parameter
} params_t;

#define NB_PARAMS 15
static params_t v_config[NB_PARAMS];

static int opt_select;

void setup()
{
  read_vstcm_config();      // Read saved settings

  IR_remote_setup();

  // Configure buttons on vstcm for input using built in pullup resistors
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

  // Shutdown the unused DAC channel A (0) on IC5
  // (not sure if this works as is or if it's even particularly useful)
  digitalWriteFast(SS0_IC5_RED, LOW);
  delayNanoseconds(100);
  SPI.transfer16(0b0110000000000000);
  digitalWriteFast(SS0_IC5_RED, HIGH);

  // Setup the SPI DMA callback
  callbackHandler.attachImmediate(&callback);
  callbackHandler.clearEvent();

  show_vstcm_config = true;

  make_test_pattern();
}

void loop()
{
  elapsedMicros waiting;    // Auto updating, used for FPS calculation

  uint32_t draw_start_time = millis();

  while (1)
  {
    if (Serial.available())
    {
      show_vstcm_config = false;
      if (read_data() == 1)
        break;
    }

    if (millis() - draw_start_time > SERIAL_WAIT_TIME)
      show_vstcm_config = true;

    if (show_vstcm_config)
      break;
  }

  if (show_vstcm_config)
  {
    show_vstcm_config_screen();      // Show settings screen and manage associated control buttons
    if (millis() - draw_start_time > SERIAL_WAIT_TIME/4)    // Don't process the buttons every single loop
      manage_buttons();
  }

  // Go to the center of the screen, turn the beam off
  brightness(0, 0, 0);
  goto_x(REST_X);
  goto_y(REST_Y);

  fps = 1000000 / waiting;
}

void brightness(uint8_t red, uint8_t green, uint8_t blue)
{
  dwell(v_config[2].pval);

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

void goto_x(uint16_t x)
{
  x_pos = x;

  if (v_config[6].pval == false)      // If FLIP X then invert x axis
    MCP4922_write(SS1_IC4_X_Y, 0, 4095 - x);
  else
    MCP4922_write(SS1_IC4_X_Y, 0, x);
}

void goto_y(uint16_t y)
{
  y_pos = y;

  if (v_config[7].pval == false)     // If FLIP Y then invert y axis
    MCP4922_write(SS1_IC4_X_Y, 1, 4095 - y);
  else
    MCP4922_write(SS1_IC4_X_Y, 1, y);
}

void dwell(const int count)
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

void draw_to_xyrgb(int x, int y, uint8_t red, uint8_t green, uint8_t blue)
{
  brightness(red, green, blue);   // Set RGB intensity levels from 0 to 255
  _draw_lineto(x, y, v_config[5].pval);
}

void draw_string(const char *s, int x, int y, int size, int brightness)
{
  while (*s)
  {
    char c = *s++;
    x += draw_character(c, x, y, size, brightness);
  }
}

int draw_character(char c, int x, int y, int size, int brightness)
{
  // Asteroids font only has upper case
  if ('a' <= c && c <= 'z')
    c -= 'a' - 'A';

  const uint8_t * const pts = asteroids_font[c - ' '].points;
  int next_moveto = 1;

  for (int i = 0 ; i < 8 ; i++)
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
      draw_to_xyrgb(x + dx, y + dy, brightness, brightness, brightness);

    next_moveto = 0;
  }

  return 12 * size;
}

void draw_moveto(int x1, int y1)
{
  brightness(0, 0, 0);

  if (v_config[9].pval == true)
  {
    goto_x(x1);
    goto_y(y1);
  }
  else
  {
    // hold the current position for a few clocks
    // with the beam off
    dwell(v_config[3].pval);
    _draw_lineto(x1, y1, v_config[1].pval);
    dwell(v_config[4].pval);
  }
}

void _draw_lineto(int x1, int y1, const int bright_shift)
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

  while (activepin != 0)  // wait until previous transfer is complete
    ;

  activepin = cs_pin;     // store to deactivate at end of transfer
  digitalWriteFast(cs_pin, LOW);

  dmabuf[0] = dac | 0x30 | ((value >> 8) & 0x0f);
  dmabuf[1] = value & 0xff;

  // if we don't use a clean begin & end transaction then other code stops working properly, such as button presses
  SPI.beginTransaction(SPISettings(v_config[10].pval, MSBFIRST, SPI_MODE0));

  // This uses non blocking SPI with DMA
  SPI.transfer(dmabuf, nullptr, 2, callbackHandler);
}

// Read data from Raspberry Pi or other external computer
int read_data()
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
      _draw_lineto(x, y, v_config[5].pval);
    }
  }
  else if (header == FLAG_RGB)
  {
    // encode brightness for R, G and B
    gl_red   = (cmd >> 16) & 0xFF;
    gl_green = (cmd >> 8)  & 0xFF;
    gl_blue  = (cmd >> 0)  & 0xFF;
  }
  else if (header == FLAG_COMPLETE_MONOCHROME)
  {
    // pick the max brightness from r g and b and mix colours
    uint8_t mix = max(max((cmd >> 0) & 0xFF, (cmd >> 8) & 0xFF), (cmd >> 16) & 0xFF);

    //  gl_red = mix * 3 / 5;
    //  gl_green = gl_green / 2;
    //  gl_blue = gl_green / 3;
    gl_red = 0;
    gl_green = mix;
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
    draw_string("FPS:", 3000, 150, 6, v_config[13].pval);
    draw_string(itoa(fps, buf1, 10), 3400, 150, 6, v_config[13].pval);

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
  SPI.endTransaction();
  digitalWriteFast(activepin, HIGH);
  activepin = 0;
}

/* ***************** SETTINGS CODE ********************/

void read_vstcm_config()
{
  params_t *vstcm_par;
  vstcm_par = &v_config[0];

  // Read saved settings
  // eeprom_read_block((void*)&vstcm_config, (void*)0, sizeof(settingsType));

  // If settings have not been previously definedthen use default values
  //  if (vstcm_config->config_ok != 999)
  //    {
  //              param name      value            min     max
  vstcm_par[0]  = {"TEST PATTERN", 0,                0,         4};
  vstcm_par[1]  = {"OFF SHIFT",    OFF_SHIFT,        0,        50};
  vstcm_par[2]  = {"OFF DWELL 0",  OFF_DWELL0,       0,        50};
  vstcm_par[3]  = {"OFF DWELL 1",  OFF_DWELL1,       0,        50};
  vstcm_par[4]  = {"OFF DWELL 2",  OFF_DWELL2,       0,        50};
  vstcm_par[5]  = {"NORMAL SHIFT", NORMAL_SHIFT,     0,       255};
  vstcm_par[6]  = {"FLIP X",       FLIP_X,           0,         1};
  vstcm_par[7]  = {"FLIP Y",       FLIP_Y,           0,         1};
  vstcm_par[8]  = {"SWAP XY",      SWAP_XY,          0,         1};
  vstcm_par[9]  = {"OFF JUMP",     OFF_JUMP,         0,         1};
  vstcm_par[10] = {"CLOCKSPEED",   CLOCKSPEED, 2000000, 120000000};
  vstcm_par[11] = {"DAC X (FYI)",  DAC_X_CHAN,       0,         1};
  vstcm_par[12] = {"DAC Y (FYI)",  DAC_Y_CHAN,       0,         1};
  vstcm_par[13] = {"NORMAL TEXT",  NORMAL,           0,       255};
  vstcm_par[14] = {"BRIGHT TEXT",  BRIGHTER,         0,       255};
  //   }

  opt_select = 0;     // Start at beginning of parameter list
}

void show_vstcm_config_screen()
{
  int i, j;
  char buf1[15] = "";

  if (v_config[0].pval != 0)      // show test pattern instead of settings
    draw_test_pattern();
  else
  {
    // cross
    draw_moveto(4095, 4095);
    draw_to_xyrgb(4095 - 512, 4095, 128, 128, 128);
    draw_to_xyrgb(4095 - 512, 4095 - 512, 128, 128, 128);
    draw_to_xyrgb(4095, 4095 - 512, 128, 128, 128);
    draw_to_xyrgb(4095, 4095, 128, 128, 128);

    draw_moveto(0, 4095);
    draw_to_xyrgb(512, 4095, 128, 128, 128);
    draw_to_xyrgb(0, 4095 - 512, 128, 128, 128);
    draw_to_xyrgb(512, 4095 - 512, 128, 128, 128);
    draw_to_xyrgb(0, 4095, 128, 128, 128);

    // Square
    draw_moveto(0, 0);
    draw_to_xyrgb(512, 0, 128, 128, 128);
    draw_to_xyrgb(512, 512, 128, 128, 128);
    draw_to_xyrgb(0, 512, 128, 128, 128);
    draw_to_xyrgb(0, 0, 128, 128, 128);

    draw_string("FPS:", 3000, 150, 6, v_config[13].pval);
    draw_string(itoa(fps, buf1, 10), 3400, 150, 6, v_config[13].pval);

    // triangle
    draw_moveto(4095, 0);
    draw_to_xyrgb(4095 - 512, 0, 128, 128, 128);
    draw_to_xyrgb(4095 - 0, 512, 128, 128, 128);
    draw_to_xyrgb(4095, 0, 128, 128, 128);

    // RGB gradiant scale

    const uint16_t height = 3072;
    const int mult = 5;

    for (i = 0, j = 0 ; j <= 255 ; i += 8, j += 32)
    {
      draw_moveto(1100, height + i * mult);
      draw_to_xyrgb(1500, height + i * mult, j, 0, 0);     // Red
      draw_moveto(1600, height + i * mult);
      draw_to_xyrgb(2000, height + i * mult, 0, j, 0);     // Green
      draw_moveto(2100, height + i * mult);
      draw_to_xyrgb(2500, height + i * mult, 0, 0, j);     // Blue
      draw_moveto(2600, height + i * mult);
      draw_to_xyrgb(3000, height + i * mult, j, j, j);     // all 3 colours combined
    }

    draw_string("v.st Colour Mod v2.1", 950, 3800, 10, v_config[14].pval);
  }

  // Show parameters on screen

  const int x = 1300;
  int y = 2800;
  int intensity;
  const int line_size = 140;
  const int char_size = 7;
  const int x_offset = 1100;

  for (i = 0; i < NB_PARAMS; i++)
  {
    if (i == opt_select)      // Highlight currently selected parameter
      intensity = v_config[14].pval;
    else
      intensity = v_config[13].pval;

    draw_string(v_config[i].param, x, y, char_size, intensity);
    itoa(v_config[i].pval, buf1, 10);
    draw_string(buf1, x + x_offset, y, char_size, intensity);
    y -= line_size;
  }
}

void manage_buttons()
{
  // Use the buttons on the PCB to adjust and save settings

  params_t *vstcm_par;
  vstcm_par = &v_config[opt_select];
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

  bool write_vstcm_config = false;

  if (digitalReadFast(3) == 0 || com == 0x08)           // SW3 Left button - decrease value of current parameter
  {
    if (v_config[opt_select].pval > v_config[opt_select].min)
    {
      vstcm_par->pval --;
      write_vstcm_config = true;
    }
  }

  if (digitalReadFast(0) == 0 || com == 0x52)          // SW2 Down button - go down list of options and loop around
  {
    if (opt_select ++ > NB_PARAMS - 1)
      opt_select = 0;
  }

  if (digitalReadFast(1) == 0 || com == 0x5A)          // SW4 Right button - increase value of current parameter
  {
    if (v_config[opt_select].pval < v_config[opt_select].max)
    {
      vstcm_par->pval ++;
      write_vstcm_config = true;
    }
  }

  if (digitalReadFast(2) == 0 || com == 0x1C)          // SW3 Middle button or OK on IR remote
  {
    //    Serial.println("2");
  }

  if (digitalReadFast(4) == 0 || com == 0x18)          // SW5 Up button - go up list of options and loop around
  {
    if (opt_select -- < 0)
      opt_select = 12;
  }

  if (write_vstcm_config == true)       // If something has changed, then update the settings in EPROM
  {
    // write settings (command below needs rewriting)
    //   eeprom_write_block((const void*)&v_config, (void*)0, sizeof(settingsType));   // disabled for now while testing
  }
}

// An IR remote can be used instead of the onboard buttons, as the PCB
// may be mounted in an arcade cabinet, making it difficult to see changes on the screen
// when using the physical buttons

void IR_remote_setup()
{
#ifdef IR_REMOTE
  Serial.begin(115200);

  // Start the receiver and if not 3. parameter specified,
  // take LED_BUILTIN pin from the internal boards definition as default feedback LED
  IrReceiver.begin(IR_RECEIVE_PIN, ENABLE_LED_FEEDBACK);

  // attachInterrupt(digitalPinToInterrupt(IR_RECEIVE_PIN), IR_remote_loop, CHANGE);
#endif  
}

const int MAX_PTS = 3000;

// Chunk of data to process using DMA or SPI
typedef struct DataChunk {
  uint16_t x;                                   // We'll just use 12 bits of X & Y for a 4096 point resolution
  uint16_t y;
  uint8_t red;                                  // Max value of each colour is 255
  uint8_t green;
  uint8_t blue;
} DataChunk_t;

static DataChunk_t Chunk[MAX_PTS];
static int nb_points;

void make_test_pattern()
{
  nb_points = 0;
  int intensity = 150;
  
  moveto(4095, 4095, 0, 0, 0);
  moveto(4095, 0, intensity, intensity, intensity);
  moveto(0, 0, intensity, intensity, intensity);
  moveto(0, 4095, intensity, intensity, intensity);
  moveto(4095, 4095, intensity, intensity, intensity);

  moveto(0, 0, 0, 0, 0);
  moveto(3071, 4095, intensity, intensity, intensity);
  moveto(4095, 2731, intensity, intensity, intensity);
  moveto(2048, 0, intensity, intensity, intensity);
  moveto(0, 2731, intensity, intensity, intensity);
  moveto(1024, 4095, intensity, intensity, intensity);
  moveto(4095, 0, intensity, intensity, intensity);

  moveto(0, 4095, 0, 0, 0);
  moveto(3071, 0, intensity, intensity, intensity);
  moveto(4095, 1365, intensity, intensity, intensity);
  moveto(2048, 4095, intensity, intensity, intensity);
  moveto(0, 1365, intensity, intensity, intensity);
  moveto(1024, 0, intensity, intensity, intensity);
  moveto(4095, 4095, intensity, intensity, intensity);
  moveto(4095, 4095, 0, 0, 0);
}

void moveto( int x, int y, int red, int green, int blue)
{
  Chunk[nb_points].x = x;
  Chunk[nb_points].y = y;
  Chunk[nb_points].red = red;
  Chunk[nb_points].green = green;
  Chunk[nb_points].blue = blue;

  nb_points ++;
}

void draw_test_pattern()
{
  int red = 0, green = 0, blue = 0;
  
  if (v_config[0].pval == 1)
    red = 140;
  else if (v_config[0].pval == 2)
    green = 140;
  else if (v_config[0].pval == 3)
    blue = 140;
  else if (v_config[0].pval == 4)
  {
    red = 140;
    green = 140;
    blue = 140;
  }
  
  for (int i = 0; i < nb_points; i++)
  {
    if (Chunk[i].red == 0)
      draw_to_xyrgb(Chunk[i].x, Chunk[i].y, 0, 0, 0);
    else
      // draw_to_xyrgb(Chunk[i].x, Chunk[i].y, Chunk[i].red, Chunk[i].green, Chunk[i].blue);
      draw_to_xyrgb(Chunk[i].x, Chunk[i].y, red, green, blue);
  }
}
