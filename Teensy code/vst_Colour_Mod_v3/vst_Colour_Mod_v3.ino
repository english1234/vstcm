/*
   VSTCM

   Vector Signal Transceiver Colour Mod using MCP4922 DACs on the Teensy 4.1

   Based on: https://trmm.net/V.st
   incorporating mods made by "Swapfile" (Github) for Advanced Mame compatibility

   robin@robinchampion.com 2022

   (This is a modified version by fcawth which attempts to speed things up)

*/


#include <SPI.h>
#include <SD.h>
#include <Bounce2.h>
#include "asteroids_font.h"
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
#include <IRremote.hpp>

#endif

uint32_t mytcr, mytcr1; //Keeps track of what the TCR register should be put back to after 16 bit mode - bit of a hack but reads and writes are a bit funny for this register (FIFOs?)
volatile int Spiflag, Spi1flag; //Keeps track of an active SPI transaction in progress
volatile bool Beam_on;
volatile int activepin;                // Active CS pin of DAC receiving data for SPI (not SPI1 since it has just one CS)
volatile bool show_vstcm_config;       // Shows settings if true

//DMAMEM char dmabuf[2] __attribute__((aligned(32)));

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
const bool FLIP_Y         = false;
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
  {"TEST_PATTERN",     "TEST PATTERN",                     0,                0,         4},
  {"OFF_SHIFT",        "BEAM TRANSIT SPEED",               OFF_SHIFT,        0,        50},
  {"OFF_DWELL0",       "BEAM SETTLING DELAY", OFF_DWELL0,       0,        50},
  {"OFF_DWELL1",       "WAIT BEFORE BEAM TRANSIT",         OFF_DWELL1,       0,        50},
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
  //Experimental - 2.2 is NTSC standard but seems too much?
  make_gammatable(.9, 255, 2047, gamma_red);  //Only go up to half scale on the output because of the speedup on V3 boards
  make_gammatable(.9, 255, 2047, gamma_green);
  make_gammatable(.9, 255, 2047, gamma_blue);


  //TODO: maybe if a button is pressed it will load defaults instead of reading the sdcard?
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
  /* Some debugging printfs when testing SPI
    Serial.print("CFGR0=:");
    Serial.println(IMXRT_LPSPI3_S.CFGR0);
    Serial.print("CFGR1=:");
    Serial.println(IMXRT_LPSPI3_S.CFGR1);
    Serial.print("DER=:");
    Serial.println(IMXRT_LPSPI3_S.DER);
    Serial.print("IER=:");
    Serial.println(IMXRT_LPSPI3_S.IER);
    Serial.print("TCR=");
    Serial.println(IMXRT_LPSPI3_S.TCR); /**/
  line_draw_speed = (float)v_config[5].pval / NORMAL_SHIFT_SCALING;
  show_vstcm_config = true;   // Start off showing the settings screen until serial data received

  make_test_pattern();        // Prepare buffer of data to draw test patterns quicker
}
unsigned long dwell_time = 0;
void loop()
{
  elapsedMicros waiting;      // Auto updating, used for FPS calculation

  unsigned long draw_start_time, loop_start_time ;

  int serial_flag;

  frame_max_x=0;
  frame_min_x=4095;
  frame_max_y=0;
  frame_min_y=4095;
  
  serial_flag = 0;
  loop_start_time = millis();
  if (!Serial) {
    read_data(1); //init read_data if the serial port is not open
    Serial.flush();
  }
  while (1)
  {
    if (Serial.available())
    {
      if (serial_flag == 0) {
        draw_start_time = millis();
        serial_flag = 1;
      }
      show_vstcm_config = false;
      if (read_data(0) == 1)
        break;
    }
    else if ((millis() - loop_start_time) > SERIAL_WAIT_TIME)  //Changed this to check only if serial is not available
      show_vstcm_config = true;

    if (show_vstcm_config)
      break;
  }
  dwell_time = draw_start_time - loop_start_time; //This is how long it waited after drawing a frame - better than FPS for tuning




  if (show_vstcm_config)
  { delta_shift = 0;  
    line_draw_speed = (float)v_config[5].pval / NORMAL_SHIFT_SCALING + 1.0 ; 
    show_vstcm_config_screen();      // Show settings screen and manage associated control buttons
    
  }
  else {
    if (dwell_time < SPEEDUP_THRESHOLD_MS) { 
      delta_shift += DELTA_SHIFT_INCREMENT;
      if (delta_shift > MAX_DELTA_SHIFT) delta_shift = MAX_DELTA_SHIFT;
    }
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
  if (show_vstcm_config)  manage_buttons(); //Moved here to avoid bright spot on the montior when doing SD card operations
  fps = 1000000 / waiting;
  if (show_vstcm_config) delay(5); //The 6100 monitor likes to spend some time in the middle
  else delayMicroseconds(100); //Wait 100 microseconds in the center if displaying a game (tune this?)


}

void make_gammatable(float gamma, uint16_t maxinput, uint16_t maxoutput, uint16_t *table) {
  for (int i = 0; i < (maxinput + 1); i++) {
    table[i] = (pow((float)i / (float)maxinput, gamma) * (float)maxoutput );
  }
}

//Changed this to only go to half scale on the dac to make it go twice as fast (increased gain on output opamp)
//TODO: Add gamma correction?
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


void dwell(int count)
{
  // can work better or faster without this on some monitors
  SPI_flush(); //Get the dacs set to their latest values before we wait
  for (int i = 0 ; i < count ; i++)
  {
    delayNanoseconds(500);  //NOTE this used to write the X and Y position but now the dacs won't get updated with repeated values
  }
}

void draw_to_xyrgb(int x, int y, uint8_t red, uint8_t green, uint8_t blue)
{
  brightness(red, green, blue);   // Set RGB intensity levels from 0 to 255
  _draw_lineto(x, y, line_draw_speed);
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



//This is a modification of the original drawing routine to use the "bright shift" differently
//Before everything was done at a lower and lower resolution so there were more steps in the lines
//Now we run at full resolution (one step of the 12-bit DAC at a time) but we only update to the DAC every "bright shift" counts
//Since we are now updating the DACs for x and y at the same time, the draws are much more smooth because
//they are points along a line instead of separate x and y steps.  Also there is much more resolution
//for tuning the speeds this way since you aren't changing by powers of two like before.  The Teensy goes so fast that this seems
//to work well but it could probably still be fixed up to use a different line drawing algorithm
void old_draw_lineto(int x1, int y1, const int bright_shift)
{
  int dx, dy, sx, sy;
  int flag;
  int x1_orig;
  int y1_orig;
  int count;


  int x0 = x_pos;
  int y0 = y_pos;

  x1_orig = x1;
  y1_orig = y1;
  // delayNanoseconds(4000);
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
        goto_xy(x0, y0);
        count = 0;
      }

    }
  }
  // ensure that we end up exactly where we want
  goto_xy(x1_orig, y1_orig);
  // SPI_flush();

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

// Read data from Raspberry Pi or other external computer
// using AdvanceMAME protocol published here
// https://github.com/amadvance/advancemame/blob/master/advance/osd/dvg.c

#define NUM_JSON_OPTS 12
#define MAX_JSON_STR_LEN 512
char *json_opts[] = {"\"productName\"", "\"version\"", "\"flipx\"", "\"flipy\"", "\"swapxy\"", "\"bwDisplay\"", "\"vertical\"", "\"crtSpeed\"", "\"crtJumpSpeed\"", "\"remote\"", "\"crtType\"", "\"defaultGame\""};
char *json_vals[] = {"\"VSTCM\"", "\"V3.0FC\"", "false", "false", "false", "false", "false", "15", "9", "true", "\"CUSTOM\"", "\"none\""};

//Build up the information string and return the length including two nulls at the end.
//Currently does not check for overflow so make sure the string is long enough!!
uint32_t build_json_info_str(char *str) {
  int i;
  uint32_t len;
  str[0] = 0;
  strcat (str, "{\n");
  for (i = 0; i < NUM_JSON_OPTS; i++) {
    strcat (str, json_opts[i]);
    strcat (str, ":");
    strcat (str, json_vals[i]);
    if (i < (NUM_JSON_OPTS - 1)) strcat(str, ",");
    strcat (str, "\n");
  }
  strcat (str, "}\n");
  len = strlen(str);
  str[len + 1] = 0; //Double null terminate
  return (len + 2); //Length includes both nulls

}
char json_str[MAX_JSON_STR_LEN];
int read_data(int init)
{
  static uint32_t cmd = 0;

  static uint8_t gl_red, gl_green, gl_blue;
  static int frame_offset = 0;

  char buf1[5] = "";
  uint8_t c = -1;
  uint32_t len;
  int i;

  if (init) {
    frame_offset = 0;
    return 0;
  }

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
      if (gl_red == 0 && gl_green == 0 && gl_blue == 0) draw_moveto( x, y );
      else _draw_lineto(x, y, line_draw_speed);
    }
  }
  else if (header == FLAG_RGB)
  {
    // encode brightness for R, G and B
    gl_red   = (cmd >> 16) & 0xFF;
    gl_green = (cmd >> 8)  & 0xFF;
    gl_blue  = (cmd >> 0)  & 0xFF;
  }

  else if (header == FLAG_FRAME)
  {
    uint32_t frame_complexity = cmd & 0b0001111111111111111111111111111;
    // TODO: Use frame_complexity to adjust screen writing algorithms
  }
  else if (header == FLAG_COMPLETE)
  {
    // Check FLAG_COMPLETE_MONOCHROME like "if(cmd&FLAG_COMPLETE_MONOCHROME) ... "
    // Not sure what to do differently if monochrome frame complete??
    // Add FPS on games as a guide for optimisation
    if (v_config[9].pval == true) {
      draw_string("DT:", 3000, 150, 6, v_config[13].pval);
     // draw_string("DS:", 3000, 150, 6, v_config[13].pval);
     // draw_string(itoa(line_draw_speed*NORMAL_SHIFT_SCALING, buf1, 10), 3400, 150, 6, v_config[13].pval);
      draw_string(itoa(dwell_time, buf1, 10), 3400, 150, 6, v_config[13].pval);
    }
    return 1;
  }
  else if (header == FLAG_EXIT)
  {
    Serial.flush();          // not sure if this useful, may help in case of manual quit on MAME
    return -1;
  }
  else if (header == FLAG_CMD && ((cmd & 0xFF) == FLAG_CMD_GET_DVG_INFO))
  { // Some info about the host comes in as follows from the advmame code:
    // sscanf(ADV_VERSION, "%u.%u", &major, &minor);
    // version = ((major & 0xf) << 12) | ((minor & 0xf) << 8) | (DVG_RELEASE << 4) | (DVG_BUILD);
    //  cmd |= version << 8;
    //  Currently DVG_RELEASE and DVG_BUILD are both zero
    // The response needs to be the following:
    // 1. Echo back the command in reverse order (least significant first)  like: 01 00 00 A0
    // 2. Send the length of the JSON string including two nulls at the end and all whitespace etc
    // 3. Send the JSON string followed by two nulls
    len = build_json_info_str(json_str);
    Serial.write(cmd & 0xFF);
    Serial.write((cmd >> 8) & 0xFF);
    Serial.write((cmd >> 16) & 0xFF);
    Serial.write((cmd >> 24) & 0xFF);
    Serial.write(len & 0xFF);
    Serial.write((len >> 8) & 0xFF);
    Serial.write(0); //Only send the first 16 bits since we better not have a strong more than 64K long!
    Serial.write(0);
    Serial.write(json_str, len);

    return 0;
  }
  else
    // Serial.println("Unknown");  //This might be messing things up?

    return 0;
}
/*
  void callback(EventResponderRef eventResponder)
  {
  // End SPI DMA write to DAC
  SPI.endTransaction();
  digitalWriteFast(activepin, HIGH);
  activepin = 0;
  }
*/
void show_vstcm_config_screen()
{
  int i;
  char buf1[25] = "";

  if (v_config[0].pval != 0)      // show test pattern instead of settings
    draw_test_pattern(0);
  else
  {
    draw_string("v.st Colour Mod v3.0", 950, 3800, 10, v_config[14].pval);

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

  for (i = 0, j = 31 ; j <= 255 ; i += 8, j += 32)  //Start at 31 to end up at full intensity?
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
    //Serial.println("Card failed, or not present");
    // don't do anything more:
    return;
  }
  // else
  //  Serial.println("Card initialised.");

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
            //Serial.println("SD card read timeout");
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
            //Serial.println("SD card read timeout");
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
            /* Serial.print(param_name);
              Serial.print(" ");
              Serial.print(pos_pn);
              Serial.print(" characters long, AKA ");
              Serial.print(v_config[j].ini_label);
              Serial.print(" ");
              Serial.print(sizeof v_config[j].ini_label);
              Serial.print(" characters long, changed from ");
              Serial.print(v_config[j].pval); */
            v_config[j].pval = atoi(param_value);
            //  Serial.print(" to ");
            //  Serial.println(v_config[j].pval);
            bChanged = true;
            break;
          }
        }

        if (bChanged == false)
        {
          //    Serial.print(param_name);
          //    Serial.println(" not found");
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
    //  Serial.println("Error opening file for reading");

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
      //  Serial.print("Writing ");
      //  Serial.print(v_config[i].ini_label);

      dataFile.write(v_config[i].ini_label);
      dataFile.write("=");
      memset(buf, 0, sizeof buf);
      ltoa(v_config[i].pval, buf, 10);

      //   Serial.print(" with value ");
      //   Serial.print(v_config[i].pval);
      //   Serial.print(" AKA ");
      //   Serial.println(buf);

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
    //  Serial.println("Error opening file for writing");
  }
}
