/*
   VSTCM

   Vector Signal Transceiver Colour Mod using MCP4922 DACs on the Teensy 4.1

   Code to draw on the vector monitor

*/

#include <SD.h>
#include "hershey_font.h"
#include "drawing.h"
#include "settings.h"
#include "spi_fct.h"

float line_draw_speed;

int frame_max_x;
int frame_min_x;
int frame_max_y;
int frame_min_y;

static uint16_t x_pos;                 // Current position of beam
static uint16_t y_pos;

volatile bool Beam_on;      // causes a compiler warning if placed in drawing.h ... why?
static ColourIntensity_t LastColInt;  // Stores last colour intensity levels

uint16_t gamma_red[256];
uint16_t gamma_green[256];
uint16_t gamma_blue[256];

extern params_t v_config[NB_PARAMS];
extern int Spiflag, Spi1flag; //Keeps track of an active SPI transaction in progress

//extern void SPI_flush();

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
  const hershey_char_t * const f = &hershey_simplex[c - ' '];
  int next_moveto = 1;

  for(int i = 0 ; i < f->count ; i++)
  {
    int dx = f->points[2*i+0];
    int dy = f->points[2*i+1];
    if (dx == -1)
    {
      next_moveto = 1;
      continue;
    }

    dx = (dx * size) * 3 / 4;
    dy = (dy * size) * 3 / 4;

    if (next_moveto)
      draw_moveto(x + dx, y + dy);
    else
      draw_to_xyrgb(x + dx, y + dy, brightness, brightness, brightness);

    next_moveto = 0;
  }

  return (f->width * size) * 3 / 4;
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

void make_gammatable(float gamma, uint16_t maxinput, uint16_t maxoutput, uint16_t *table) {
  for (int i = 0; i < (maxinput + 1); i++) {
    table[i] = (pow((float)i / (float)maxinput, gamma) * (float)maxoutput );
  }
}

void init_gamma()
{
  //Experimental - 2.2 is NTSC standard but seems too much?
  make_gammatable(.9, 255, 2047, gamma_red);  //Only go up to half scale on the output because of the speedup on V3 boards
  make_gammatable(.9, 255, 2047, gamma_green);
  make_gammatable(.9, 255, 2047, gamma_blue);
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
