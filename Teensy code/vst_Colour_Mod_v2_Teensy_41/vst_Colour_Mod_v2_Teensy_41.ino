/*
   VSTCM v2

   Vector Signal Transceiver Colour Mod using MCP4922 DACs on the Teensy 4.1

   Based on: https://trmm.net/V.st
   incorporating mods made by "Swapfile" (Github) for Advanced Mame compatibility
   and mods by fcawth to increase speed

   robin@robinchampion.com 2022

   NOTE: This code is for v2 of the standard PCB only. Wiring mods are required to run v3 code.
   
*/

#include <SPI.h>
#include <SD.h>
#include <Bounce2.h>
#include "asteroids_font.h"

// SPI register bits
#define LPSPI_SR_WCF    ((uint32_t)(1<<8))  // Received Word complete flag
#define LPSPI_SR_FCF    ((uint32_t)(1<<9))  // Frame complete flag
#define LPSPI_SR_TCF    ((uint32_t)(1<<10)) // Transfer complete flag
#define LPSPI_SR_MBF    ((uint32_t)(1<<24)) // Module busy flag
#define LPSPI_TCR_RXMSK ((uint32_t)(1<<19)) // Receive Data Mask (when 1 no data received to FIFO)

// Teensy SS pins connected to DACs
const int SS0_IC5_RED     =  8;       // RED output
const int SS1_IC4_X_Y     =  6;       // X and Y outputs
const int SS2_IC3_GRE_BLU = 22;       // GREEN and BLUE outputs

#define SDI                 11        // MOSI on SPI0
#define SCK                 13        // SCK on SPI0
#define BUFFERED                      // If defined, uses buffer on DACs

const int REST_X          = 2048;     // Wait in the middle of the screen
const int REST_Y          = 2048;

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

#define DAC_X_CHAN 0                  // Used to flip X & Y axis if needed
#define DAC_Y_CHAN 1

typedef struct ColourIntensity {      // Stores current levels of brightness for each colour
  uint8_t red;
  uint8_t green;
  uint8_t blue;
} ColourIntensity_t;

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

#define IR_RECEIVE_PIN      32         // Put this outside the ifdef so that it doesn't break the menu
#define IR_REMOTE                      // Define if IR remote is fitted  TODO:deactivate if the menu is not shown? Has about a 10% reduction of frame rate when active
#ifdef IR_REMOTE
#define SUPPRESS_ERROR_MESSAGE_FOR_BEGIN
#include <IRremote.hpp>
#endif

uint32_t mytcr; // Keeps track of what the TCR register should be put back to after 16 bit mode - bit of a hack but reads and writes are a bit funny for this register (FIFOs?)
int spiflag;                           // Keeps track of an active SPI transaction in progress
volatile int activepin;                // Active CS pin of DAC receiving data
volatile bool show_vstcm_config;       // Shows settings if true

static uint16_t x_pos;                 // Current position of beam
static uint16_t y_pos;

// Settings with default values
const int  OFF_SHIFT      =    20;     // Smaller numbers == slower transits (the higher the number, the less flicker and faster draw but more wavy lines)
const int  OFF_DWELL0     =     0;     // Time to sit beam on before starting a transit
const int  OFF_DWELL1     =     0;     // Time to sit before starting a transit
const int  OFF_DWELL2     =     0;     // Time to sit after finishing a transit
const int  NORMAL_SHIFT   =     5;     // The higher the number, the less flicker and faster draw but more wavy lines
const bool OFF_JUMP       = false;
const bool FLIP_X         = false;     // Sometimes the X and Y need to be flipped and/or swapped
const bool FLIP_Y         = false;
const bool SWAP_XY        = false;
const uint32_t CLOCKSPEED = 60000000;  // This is 3x the max clock in the datasheet but seems to work!!
const int  NORMAL1        =   100;     // Brightness of text in parameter list
const int  BRIGHTER       =   128;
// how long in milliseconds to wait for data before displaying a test pattern
// this can be increased if the test pattern appears during gameplay
const int  SERIAL_WAIT_TIME = 100;
const int  AUDIO_PIN        = 10;      // Connect audio output to GND and pin 10

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
  {"TEST_PATTERN",     "TEST PATTERN",                     0,                0,         4},
  {"OFF_SHIFT",        "BEAM TRANSIT SPEED",               OFF_SHIFT,        0,        50},
  {"OFF_DWELL0",       "WAIT WITH BEAM ON BEFORE TRANSIT", OFF_DWELL0,       0,        50},
  {"OFF_DWELL1",       "WAIT BEFORE BEAM TRANSIT",         OFF_DWELL1,       0,        50},
  {"OFF_DWELL2",       "WAIT AFTER BEAM TRANSIT",          OFF_DWELL2,       0,        50},
  {"NORMAL_SHIFT",     "NORMAL SHIFT",                     NORMAL_SHIFT,     0,       255},
  {"FLIP_X",           "FLIP X AXIS",                      FLIP_X,           0,         1},
  {"FLIP_Y",           "FLIP Y AXIS",                      FLIP_Y,           0,         1},
  {"SWAP_XY",          "SWAP XY",                          SWAP_XY,          0,         1},
  {"OFF_JUMP",         "OFF JUMP",                         OFF_JUMP,         0,         1},
  {"CLOCKSPEED",       "CLOCKSPEED",                       CLOCKSPEED, 2000000, 120000000},
  {"IR_RECEIVE_PIN",   "IR RECEIVE PIN",                   IR_RECEIVE_PIN,   0,        54},
  {"AUDIO_PIN",        "AUDIO PIN",                        AUDIO_PIN,        0,        54},
  {"NORMAL1",          "NORMAL TEXT",                      NORMAL1,          0,       255},
  {"BRIGHTER",         "BRIGHT TEXT",                      BRIGHTER,         0,       255},
  {"SERIAL_WAIT_TIME", "TEST PATTERN DELAY",               SERIAL_WAIT_TIME, 0,       255}
};

static int opt_select;    // Currently selected setting

// Bounce objects to read five pushbuttons (pins 0-4)
Bounce button0 = Bounce();
Bounce button1 = Bounce();
Bounce button2 = Bounce();
Bounce button3 = Bounce();
Bounce button4 = Bounce();

const uint8_t DEBOUNCE_INTERVAL = 25;    // Measured in ms

void setup()
{
  Serial.begin(115200);
  while ( !Serial && millis() < 4000 );

  read_vstcm_config();      // Read saved settings from Teensy SD card

  IR_remote_setup();

  // Configure buttons on vstcm for input using built in pullup resistors

  button0.attach(0, INPUT_PULLUP);        // Attach the debouncer to a pin and use internal pullup resistor
  button0.interval(DEBOUNCE_INTERVAL);
  button1.attach(1, INPUT_PULLUP);
  button1.interval(DEBOUNCE_INTERVAL);
  button2.attach(2, INPUT_PULLUP);
  button2.interval(DEBOUNCE_INTERVAL);
  button3.attach(3, INPUT_PULLUP);
  button3.interval(DEBOUNCE_INTERVAL);
  button4.attach(4, INPUT_PULLUP);
  button4.interval(DEBOUNCE_INTERVAL);

  // Apparently, pin 10 has to be defined as an OUTPUT pin to designate the Arduino as the SPI master.
  // Even if pin 10 is not being used... Is this true for Teensy 4.1?
  // The default mode is INPUT. You must explicitly set pin 10 to OUTPUT (and leave it as OUTPUT).
  pinMode(10, OUTPUT);
  digitalWriteFast(10, HIGH);
  delayNanoseconds(100);

  // Set chip select pins going to DACs to output
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

  delay(1);                   // https://www.pjrc.com/better-spi-bus-design-in-3-steps/

  SPI.setCS(10);
  SPI.begin();

  // Some posts seem to indicate that doing a begin and end like this will help conflicts with other things on the Teensy??
  SPI.beginTransaction(SPISettings(v_config[10].pval, MSBFIRST, SPI_MODE0));  // Doing this begin and end here should make it so we don't have to do it each time
  SPI.endTransaction();

  mytcr = IMXRT_LPSPI4_S.TCR ;
  // This will break all stock SPI transactions from this point on - disable receiver and go to 16 bit mode
  IMXRT_LPSPI4_S.TCR = (mytcr & 0xfffff000) | LPSPI_TCR_FRAMESZ(15) | LPSPI_TCR_RXMSK;
  mytcr = (mytcr & 0xfffff000) | LPSPI_TCR_FRAMESZ(15) | LPSPI_TCR_RXMSK;

  show_vstcm_config = true;   // Start off showing the settings screen until serial data received

  make_test_pattern();        // Prepare buffer of data to draw test patterns quicker
}

void loop()
{
  elapsedMicros waiting;      // Auto updating, used for FPS calculation

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

  draw_start_time = 0;

  if (show_vstcm_config)
  {
    show_vstcm_config_screen();      // Show settings screen and manage associated control buttons
    if (millis() - draw_start_time > SERIAL_WAIT_TIME / 4)  // Don't process the buttons every single loop
      manage_buttons();
  }

  // Go to the center of the screen, turn the beam off (prevents stray coloured lines from appearing)
  brightness(0, 0, 0);
  goto_x(REST_X);
  goto_y(REST_Y);
  SPI_flush();

  fps = 1000000 / waiting;
}

void brightness(uint8_t red, uint8_t green, uint8_t blue)
{
  if (LastColInt.red == red && LastColInt.green == green && LastColInt.blue == blue)
    return;

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
  if (x != x_pos)     // no point if the beam is already in the right place
  {
    x_pos = x;

    if (v_config[6].pval == false)      // If FLIP X then invert x axis
      MCP4922_write(SS1_IC4_X_Y, DAC_X_CHAN, 4095 - x);
    else
      MCP4922_write(SS1_IC4_X_Y, DAC_X_CHAN, x);
  }
}

void goto_y(uint16_t y)
{
  if (y != y_pos)     // no point if the beam is already in the right place
  {
    y_pos = y;

    if (v_config[7].pval == false)     // If FLIP Y then invert y axis
      MCP4922_write(SS1_IC4_X_Y, DAC_Y_CHAN, 4095 - y);
    else
      MCP4922_write(SS1_IC4_X_Y, DAC_Y_CHAN, y);
  }
}

void dwell(const int count)
{
  SPI_flush();              // Get the dacs set to their latest values before we wait

  // can work better or faster without this on some monitors
  for (int i = 0 ; i < count ; i++)
  {
    delayNanoseconds(200);  // NOTE this used to write the X and Y position but now the dacs won't get updated with repeated values
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

// This is a modification of the original drawing routine to use the "bright shift" differently
// Before everything was done at a lower and lower resolution so there were more steps in the lines
// Now we run at full resolution (one step of the 12-bit DAC at a time) but we only update to the DAC every "bright shift" counts
// Since we are now updating the DACs for x and y at the same time, the draws are much more smooth because
// they are points along a line instead of separate x and y steps.  Also there is much more resolution
// for tuning the speeds this way since you aren't changing by powers of two like before.  The Teensy goes so fast that this seems
// to work well but it could probably still be fixed up to use a different line drawing algorithm
void _draw_lineto(int x1, int y1, const int bright_shift)
{
  int dx, dy, sx, sy;
  int flag;
  const int x1_orig = x1;
  const int y1_orig = y1;
  int count;

  int x0 = x_pos;
  int y0 = y_pos;

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
  count = 0;
  while (1)
  {
    if (x0 == x1 && y0 == y1)
      break;
    flag = 0;
    int e2 = 2 * err;
    if (e2 > -dy)
    {
      err = err - dy;
      x0 += sx;
      flag = 1;
    }
    if (e2 < dx)
    {
      err = err + dx;
      y0 += sy;
      flag = 1;
    }
    if (flag) {
      count++;
      if (count >= bright_shift) {
        goto_x(x0);
        goto_y(y0);
        count = 0;
      }
    }
  }

  // ensure that we end up exactly where we want
  goto_x(x1_orig);
  goto_y(y1_orig);
}

// Finish the last SPI transactions
void SPI_flush()
{
  // Wait for the last transaction to finish and then set CS high from the last transaction
  // By doing this the code can do other things instead of busy waiting for the SPI transaction
  // like it does with the stock functions.
  if (spiflag)
    while (!(IMXRT_LPSPI4_S.SR & LPSPI_SR_FCF));  // Loop until the last frame is complete

  digitalWriteFast(activepin, HIGH);              // Set the CS from the last transaction high

  IMXRT_LPSPI4_S.SR = LPSPI_SR_FCF;                 // Clear the flag
  spiflag = 0;
  activepin = 0;
}

void MCP4922_write(int cs_pin, byte dac, uint16_t value)
{
  // Wait for the last transaction to finish and then set CS high from the last transaction
  // By doing this the code can do other things instead of busy waiting for the SPI transaction
  // like it does with the stock functions.
  if (spiflag)
    while (!(IMXRT_LPSPI4_S.SR & LPSPI_SR_FCF));  // Loop until the last frame is complete

  digitalWriteFast(activepin, HIGH);              // Set the CS from the last transaction high

  // Everything between here and setting the CS pin low determines how long the CS signal is high
  // Right now (with clearing the flag, masking the value, select channel, store new CS) it is high about 50ns
  IMXRT_LPSPI4_S.SR = LPSPI_SR_FCF;               // Clear the flag

  value &= 0x0FFF;                                // mask out just the 12 bits of data

  // add the output channel A or B on the selected DAC, and buffer flag
#ifdef BUFFERED
  // select the output channel on the selected DAC, buffered, no gain
  value |= 0x7000 | (dac  ? 0x8000 : 0x0000);
#else
  // select the output channel on the selected DAC, unbuffered, no gain
  value |= 0x3000 | (dac  ? 0x8000 : 0x0000);
#endif

  activepin = cs_pin;                             // store to deactivate at end of transfer

  digitalWriteFast(cs_pin, LOW);

  // Set up the transaction directly with the SPI registers because the normal transfer16
  // function will busy wait for the SPI transfer to complete.  We will wait for completion
  // and de-assert CS the next time around to speed things up.
  // By doing this the code can do other things instead of busy waiting for the SPI transaction
  // like it does with the stock functions.
  spiflag = 1;
  IMXRT_LPSPI4_S.TDR = value; // Send data to the SPI fifo and start transaction but don't wait for it to be done
}

// Read data from Raspberry Pi or other external computer
// using AdvanceMAME protocol published here
// https://github.com/amadvance/advancemame/blob/master/advance/osd/dvg.c

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

    // As an optimisation there is a blank flag in the XY coord which
    // allows blanking of the beam without updating the RGB color DACs.

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
    return 0;
  }
  else
    Serial.println("Unknown");

  return 0;
}

void show_vstcm_config_screen()
{
  int i;
  char buf1[25] = "";

  if (v_config[0].pval != 0)      // show test pattern instead of settings
    draw_test_pattern(0);
  else
  {
    draw_string("v.st Colour Mod v2.1", 950, 3800, 10, v_config[14].pval);

    draw_test_pattern(1);

    // Show parameters on screen

    const int x = 300;
    int y = 2800;
    int intensity;
    const int line_size = 140;
    const int char_size = 7;
    const int x_offset = 3000;

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

    draw_string("PRESS CENTRE BUTTON / OK TO SAVE SETTINGS", 550, 400, 6, v_config[13].pval);

    draw_string("FPS:", 3000, 150, 6, v_config[13].pval);
    draw_string(itoa(fps, buf1, 10), 3400, 150, 6, v_config[13].pval);
  }
}

void manage_buttons()
{
  // Use the buttons on the PCB to adjust and save settings

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

  // Update all the button objects
  button0.update();
  button1.update();
  button2.update();
  button3.update();
  button4.update();

  if (button4.fell() || com == 0x18)          // SW5 Up button - go up list of options and loop around
  {
    if (opt_select -- < 0)
      opt_select = 12;
  }

  if (button0.fell() || com == 0x52)          // SW2 Down button - go down list of options and loop around
  {
    if (opt_select ++ > NB_PARAMS - 1)
      opt_select = 0;
  }

  if (button3.fell() || com == 0x08)          // SW3 Left button - decrease value of current parameter
  {
    if (v_config[opt_select].pval > v_config[opt_select].min)
      v_config[opt_select].pval --;
  }

  if (button1.fell() || com == 0x5A)          // SW4 Right button - increase value of current parameter
  {
    if (v_config[opt_select].pval < v_config[opt_select].max)
      v_config[opt_select].pval ++;
  }

  if (button2.fell() || com == 0x1C)          // SW3 Middle button or OK on IR remote
    write_vstcm_config();                    // Update the settings on the SD card
}

// An IR remote can be used instead of the onboard buttons, as the PCB
// may be mounted in an arcade cabinet, making it difficult to see changes on the screen
// when using the physical buttons

void IR_remote_setup()
{
#ifdef IR_REMOTE

  // Start the receiver and if not 3. parameter specified,
  // take LED_BUILTIN pin from the internal boards definition as default feedback LED
  IrReceiver.begin(v_config[11].pval, ENABLE_LED_FEEDBACK);

  // attachInterrupt(digitalPinToInterrupt(IR_RECEIVE_PIN), IR_remote_loop, CHANGE);
#endif
}

void make_test_pattern()
{
  // Prepare buffer of test pattern data as a speed optimisation

  int offset, i, j;

  offset = 0;   // Draw Asteroids style test pattern in Red, Green or Blue

  nb_points[offset] = 0;
  int intensity = 150;

  moveto(offset, 4095, 4095, 0, 0, 0);
  moveto(offset, 4095, 0, intensity, intensity, intensity);
  moveto(offset, 0, 0, intensity, intensity, intensity);
  moveto(offset, 0, 4095, intensity, intensity, intensity);
  moveto(offset, 4095, 4095, intensity, intensity, intensity);

  moveto(offset, 0, 0, 0, 0, 0);
  moveto(offset, 3071, 4095, intensity, intensity, intensity);
  moveto(offset, 4095, 2731, intensity, intensity, intensity);
  moveto(offset, 2048, 0, intensity, intensity, intensity);
  moveto(offset, 0, 2731, intensity, intensity, intensity);
  moveto(offset, 1024, 4095, intensity, intensity, intensity);
  moveto(offset, 4095, 0, intensity, intensity, intensity);

  moveto(offset, 0, 4095, 0, 0, 0);
  moveto(offset, 3071, 0, intensity, intensity, intensity);
  moveto(offset, 4095, 1365, intensity, intensity, intensity);
  moveto(offset, 2048, 4095, intensity, intensity, intensity);
  moveto(offset, 0, 1365, intensity, intensity, intensity);
  moveto(offset, 1024, 0, intensity, intensity, intensity);
  moveto(offset, 4095, 4095, intensity, intensity, intensity);
  moveto(offset, 4095, 4095, 0, 0, 0);

  // Prepare buffer for fixed part of settings screen

  offset = 1;
  nb_points[offset] = 0;

  // cross
  moveto(offset, 4095, 4095, 0, 0, 0);
  moveto(offset, 4095 - 512, 4095, 128, 128, 128);
  moveto(offset, 4095 - 512, 4095 - 512, 128, 128, 128);
  moveto(offset, 4095, 4095 - 512, 128, 128, 128);
  moveto(offset, 4095, 4095, 128, 128, 128);

  moveto(offset, 0, 4095, 0, 0, 0);
  moveto(offset, 512, 4095, 128, 128, 128);
  moveto(offset, 0, 4095 - 512, 128, 128, 128);
  moveto(offset, 512, 4095 - 512, 128, 128, 128);
  moveto(offset, 0, 4095, 128, 128, 128);

  // Square
  moveto(offset, 0, 0, 0, 0, 0);
  moveto(offset, 512, 0, 128, 128, 128);
  moveto(offset, 512, 512, 128, 128, 128);
  moveto(offset, 0, 512, 128, 128, 128);
  moveto(offset, 0, 0, 128, 128, 128);

  // triangle
  moveto(offset, 4095, 0, 0, 0, 0);
  moveto(offset, 4095 - 512, 0, 128, 128, 128);
  moveto(offset, 4095 - 0, 512, 128, 128, 128);
  moveto(offset, 4095, 0, 128, 128, 128);

  // RGB gradiant scale

  const uint16_t height = 3072;
  const int mult = 5;

  for (i = 0, j = 0 ; j <= 255 ; i += 8, j += 32)
  {
    moveto(offset, 1100, height + i * mult, 0, 0, 0);
    moveto(offset, 1500, height + i * mult, j, 0, 0);     // Red
    moveto(offset, 1600, height + i * mult, 0, 0, 0);
    moveto(offset, 2000, height + i * mult, 0, j, 0);     // Green
    moveto(offset, 2100, height + i * mult, 0, 0, 0);
    moveto(offset, 2500, height + i * mult, 0, 0, j);     // Blue
    moveto(offset, 2600, height + i * mult, 0, 0, 0);
    moveto(offset, 3000, height + i * mult, j, j, j);     // all 3 colours combined
  }
}

void moveto(int offset, int x, int y, int red, int green, int blue)
{
  // Store coordinates of vectors and colour info in a buffer

  DataChunk_t *localChunk = &Chunk[offset][nb_points[offset]];

  localChunk->x = x;
  localChunk->y = y;
  localChunk->red = red;
  localChunk->green = green;
  localChunk->blue = blue;

  nb_points[offset] ++;
}

void draw_test_pattern(int offset)
{
  int i, red = 0, green = 0, blue = 0;

  if (offset == 0)      // Determine what colour to draw the test pattern
  {
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

    for (i = 0; i < nb_points[offset]; i++)
    {
      if (Chunk[offset][i].red == 0)
        draw_moveto(Chunk[offset][i].x, Chunk[offset][i].y);
      //   draw_to_xyrgb(Chunk[offset][i].x, Chunk[offset][i].y, 0, 0, 0);
      else
        // draw_to_xyrgb(Chunk[i].x, Chunk[i].y, Chunk[i].red, Chunk[i].green, Chunk[i].blue);
        draw_to_xyrgb(Chunk[offset][i].x, Chunk[offset][i].y, red, green, blue);
    }
  }
  else if (offset == 1)
  {
    for (i = 0; i < nb_points[offset]; i++)
      draw_to_xyrgb(Chunk[offset][i].x, Chunk[offset][i].y, Chunk[offset][i].red, Chunk[offset][i].green, Chunk[offset][i].blue);
  }
}

/* ***************** SETTINGS CODE ********************/

void read_vstcm_config()
{
  int i, j;
  const int chipSelect = BUILTIN_SDCARD;
  char buf;
  char param_name[20];
  char param_value[20];
  uint8_t pos_pn, pos_pv;

  // see if the SD card is present and can be initialised:
  if (!SD.begin(chipSelect))
  {
    Serial.println("Card failed, or not present");
    // don't do anything more:
    return;
  }
  else
    Serial.println("Card initialised.");

  // open the vstcm.ini file on the sd card
  File dataFile = SD.open("vstcm.ini", FILE_READ);

  if (dataFile)
  {
    while (dataFile.available())
    {
      for (i = 1; i < NB_PARAMS; i++)
      {
        pos_pn = 0;

        memset(param_name, 0, sizeof param_name);

        uint32_t read_start_time = millis();

        while (1)   // read the parameter name until an equals sign is encountered
        {

          // provide code for a timeout in case there's a problem reading the file

          if (millis() - read_start_time > 2000u)
          {
            Serial.println("SD card read timeout");
            break;
          }

          buf = dataFile.read();

          if (buf == 0x3D)      // stop reading if it's an equals sign
            break;
          else if (buf != 0x0A && buf != 0x0d)      // ignore carriage return
          {
            param_name[pos_pn] = buf;
            pos_pn ++;
          }
        }

        pos_pv = 0;

        memset(param_value, 0, sizeof param_value);

        while (1)   // read the parameter value until a semicolon is encountered
        {

          // provide code for a timeout in case there's a problem reading the file
          if (millis() - read_start_time > 2000u)
          {
            Serial.println("SD card read timeout");
            break;
          }

          buf = dataFile.read();

          if (buf == 0x3B)      // stop reading if it's a semicolon
            break;
          else if (buf != 0x0A && buf != 0x0d)      // ignore carriage return
          {
            param_value[pos_pv] = buf;
            pos_pv ++;
          }
        }

        // Find the setting in the predefined array and update the value with the one stored on SD

        bool bChanged = false;

        for (j = 0; j < NB_PARAMS; j++)
        {
          if (!memcmp(param_name, v_config[j].ini_label, pos_pn))
          {
            Serial.print(param_name);
            Serial.print(" ");
            Serial.print(pos_pn);
            Serial.print(" characters long, AKA ");
            Serial.print(v_config[j].ini_label);
            Serial.print(" ");
            Serial.print(sizeof v_config[j].ini_label);
            Serial.print(" characters long, changed from ");
            Serial.print(v_config[j].pval);
            v_config[j].pval = atoi(param_value);
            Serial.print(" to ");
            Serial.println(v_config[j].pval);
            bChanged = true;
            break;
          }
        }

        if (bChanged == false)
        {
          Serial.print(param_name);
          Serial.println(" not found");
        }
      } // end of for i loop

      break;
    }

    // close the file:
    dataFile.close();
  }
  else
  {
    // if the file didn't open, print an error:
    Serial.println("Error opening file for reading");

    // If the vstcm.ini file doesn't exist, then write the default one
    write_vstcm_config();
  }

  opt_select = 0;     // Start at beginning of parameter list
}

void write_vstcm_config()
{
  int i;
  char buf[20];

  // Write the settings file to the SD card with currently selected values
  // Format of each line is <PARAMETER NAME>=<PARAMETER VALUE>; followed by a newline

  File dataFile = SD.open("vstcm.ini", O_RDWR);

  if (dataFile)
  {
    for (i = 0; i < NB_PARAMS; i++)
    {
      Serial.print("Writing ");
      Serial.print(v_config[i].ini_label);

      dataFile.write(v_config[i].ini_label);
      dataFile.write("=");
      memset(buf, 0, sizeof buf);
      ltoa(v_config[i].pval, buf, 10);

      Serial.print(" with value ");
      Serial.print(v_config[i].pval);
      Serial.print(" AKA ");
      Serial.println(buf);

      dataFile.write(buf);
      dataFile.write(";");
      dataFile.write(0x0d);
      dataFile.write(0x0a);
    }

    // close the file:
    dataFile.close();
  }
  else
  {
    // if the file didn't open, print an error:
    Serial.println("Error opening file for writing");
  }
}
