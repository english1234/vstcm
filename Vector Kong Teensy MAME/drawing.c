/*
   VSTCM

   Vector Signal Transceiver Colour Mod using MCP4922 DACs on the Teensy 4.1

   Code to draw on the vector monitor

*/

#include <arduino.h>
#include "hershey_font.h"
#include "drawing.h"
#include "settings.h"
#include "spi_fct.h"

float line_draw_speed;

int frame_max_x;
int frame_min_x;
int frame_max_y;
int frame_min_y;

static uint16_t x_pos;  // Current position of beam
static uint16_t y_pos;

volatile bool Beam_on;                // causes a compiler warning if placed in drawing.h ... why?
static ColourIntensity_t LastColInt;  // Stores last colour intensity levels

uint16_t gamma_red[256];
uint16_t gamma_green[256];
uint16_t gamma_blue[256];

extern params_t v_setting[NB_SETTINGS];
extern int Spiflag, Spi1flag;  //Keeps track of an active SPI transaction in progress

enum { TOP = 0x1,
       BOTTOM = 0x2,
       RIGHT = 0x4,
       LEFT = 0x8 };
enum { FALSE,
       TRUE };
typedef unsigned int outcode;
outcode compute_outcode(int x, int y, int xmin, int ymin, int xmax, int ymax) {
  outcode oc = 0;
  if (y > ymax)
    oc |= TOP;
  else if (y < ymin)
    oc |= BOTTOM;
  if (x > xmax)
    oc |= RIGHT;
  else if (x < xmin)
    oc |= LEFT;
  return oc;
}

// returns x1 = 1000000 on complete outside!
void cohen_sutherlandCustom(int32_t *x1, int32_t *y1, int32_t *x2, int32_t *y2, int xmin, int ymin, int xmax, int ymax) {
  int accept;
  int done;
  outcode outcode1, outcode2;
  accept = FALSE;
  done = FALSE;
  outcode1 = compute_outcode(*x1, *y1, xmin, ymin, xmax, ymax);
  outcode2 = compute_outcode(*x2, *y2, xmin, ymin, xmax, ymax);
  do {
    if (outcode1 == 0 && outcode2 == 0) {
      accept = TRUE;
      done = TRUE;
    } else if (outcode1 & outcode2) {
      done = TRUE;
    } else {
      int x, y;
      int outcode_ex = outcode1 ? outcode1 : outcode2;
      if (outcode_ex & TOP) {
        x = *x1 + (*x2 - *x1) * (ymax - *y1) / (*y2 - *y1);
        y = ymax;
      } else if (outcode_ex & BOTTOM) {
        x = *x1 + (*x2 - *x1) * (ymin - *y1) / (*y2 - *y1);
        y = ymin;
      } else if (outcode_ex & RIGHT) {
        y = *y1 + (*y2 - *y1) * (xmax - *x1) / (*x2 - *x1);
        x = xmax;
      } else {
        y = *y1 + (*y2 - *y1) * (xmin - *x1) / (*x2 - *x1);
        x = xmin;
      }
      if (outcode_ex == outcode1) {
        *x1 = x;
        *y1 = y;
        outcode1 = compute_outcode(*x1, *y1, xmin, ymin, xmax, ymax);
      } else {
        *x2 = x;
        *y2 = y;
        outcode2 = compute_outcode(*x2, *y2, xmin, ymin, xmax, ymax);
      }
    }
  } while (done == FALSE);

  if (accept == TRUE)
    return;

  *x1 = 1000000;
  return;
}

#define BRIGHT_SHIFT 4

void draw_to_xy(int x, int y) {

  // if (x > 4095 || x < 1 || y > 4095 || y < 1)   // Ignore offscreen vectors as a temporary measure while getting this going
  //   return;

  // _draw_lineto(x, y, line_draw_speed);
  _draw_lineto(x, y, BRIGHT_SHIFT);
}

void draw_moveto(int x1, int y1) {
  brightness(0, 0, 0);

  //  if (x1 > 4095 || x1 < 1 || y1 > 4095 || y1 < 1)   // Ignore offscreen vectors as a temporary measure while getting this going
  //   return;

  // hold the current position for a few clocks
  // with the beam off
  dwell(v_setting[3].pval);
  _draw_lineto(x1, y1, BRIGHT_SHIFT);
  //  _draw_lineto(x1, y1, 1);
  dwell(v_setting[4].pval);
  /* if (x1 > frame_max_x) frame_max_x = x1;
  else if (x1 < frame_min_x) frame_min_x = x1;
  if (y1 > frame_max_y) frame_max_y = y1;
  else if (y1 < frame_min_y) frame_min_y = y1;*/
}

//Trying out using floating point to compute the line
//Also try to make sure the move has at least a couple of points in it
//if we are drawing
void _draw_lineto(const int x1, const int y1, float bright_shift) {
  int dx, dy;
  float dxf, dyf;
  int dxmag, dymag;
  int max_dist;
  int x0, y0;
  int numsteps;
  float numstepsf;
  float xcur, ycur;
  int i;

  if (x1 > frame_max_x) frame_max_x = x1;
  else if (x1 < frame_min_x) frame_min_x = x1;
  if (y1 > frame_max_y) frame_max_y = y1;
  else if (y1 < frame_min_y) frame_min_y = y1;

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

  if (Beam_on && (max_dist * 2 < bright_shift)) {  //Force at least 2 points in each line segment (cleans up text at high rates)
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

void make_gammatable(float gamma, uint16_t maxinput, uint16_t maxoutput, uint16_t *table) {
  for (int i = 0; i < (maxinput + 1); i++) {
    table[i] = (pow((float)i / (float)maxinput, gamma) * (float)maxoutput);
  }
}

void init_gamma() {
  //Experimental - 2.2 is NTSC standard but seems too much?
  make_gammatable(.9, 255, 2047, gamma_red);  //Only go up to half scale on the output because of the speedup on V3 boards
  make_gammatable(.9, 255, 2047, gamma_green);
  make_gammatable(.9, 255, 2047, gamma_blue);
}

//Changed this to only go to half scale on the dac to make it go twice as fast (increased gain on output opamp)
//TODO: Add gamma correction?
void brightness(uint8_t red, uint8_t green, uint8_t blue) {
  if ((LastColInt.red == red) && (LastColInt.green == green) && (LastColInt.blue == blue)) return;

  if (green == blue) {  //We can write all 3 at the same time if green is the same as blue
    LastColInt.red = red;
    LastColInt.green = green;
    LastColInt.blue = blue;
    // MCP4922_write2(DAC_CHAN_RGB, red << 3, green << 3 , 1);    //Shift by 3 to go to half scale maximum
    MCP4922_write2(DAC_CHAN_RGB, gamma_red[red], gamma_green[green], 1);
  }

  if ((LastColInt.red != red) || (LastColInt.green != green)) {  //We can write red and green at the same time
    LastColInt.red = red;
    LastColInt.green = green;
    // MCP4922_write2(DAC_CHAN_RGB, red << 3, green << 3 , 0);
    MCP4922_write2(DAC_CHAN_RGB, gamma_red[red], gamma_green[green], 0);
  }

  if (LastColInt.blue != blue) {
    LastColInt.blue = blue;
    MCP4922_write1(DAC_CHAN_RGB, gamma_blue[blue]);
  }

  //Dwell moved here since it takes about 4us to fully turn on or off the beam
  //Possibly change where this is depending on if the beam is being turned on or off??
  if (LastColInt.red || LastColInt.green || LastColInt.blue) Beam_on = true;
  else Beam_on = false;
  dwell(v_setting[2].pval);  //Wait this amount before changing the beam (turning it on or off)

}

void goto_xy(uint16_t x, uint16_t y) {
  float xf, yf;
  float xcorr, ycorr;

  if ((x_pos == x) && (y_pos == y)) return;

  // Prevent drawing off the screen with hard clipping
  /* if (x<1) return;
  if (x>4095) return;
  if (y<1) return;
  if (y>4095) return;*/

  x_pos = x;
  y_pos = y;

  if (v_setting[10].pval == true) {
    xf = x - 2048;
    yf = y - 2048;
    xcorr = xf * (1.0 - yf * yf * .000000013) + 2048.0;  //These are experimental at this point but seem to do OK on the 6100 monitor
    ycorr = yf * (1.0 - xf * xf * .000000005) + 2048.0;
    x = xcorr;
    y = ycorr;
  }

  // Swap X & Y axes if defined in settings
  if (v_setting[6].pval == false) x = 4095 - x;
  if (v_setting[7].pval == false) y = 4095 - y;
  MCP4922_write2(DAC_CHAN_XY, x, y, 0);
}

void dwell(int count) {
  // can work better or faster without this on some monitors
  SPI_flush();  //Get the dacs set to their latest values before we wait
  for (int i = 0; i < count; i++) {
    delayNanoseconds(500);  //NOTE this used to write the X and Y position but now the dacs won't get updated with repeated values
  }
}

//Write out to the one dac that is by itself
void MCP4922_write1(int dac, uint16_t value) {
  // uint32_t temp;

  //Wait for the last transaction to finish and then set CS high from the last transaction
  //By doing this the code can do other things instead of busy waiting for the SPI transaction
  //like it does with the stock functions.
  if (Spiflag)
    while (!(IMXRT_LPSPI4_S.SR & LPSPI_SR_FCF))
      ;  //Loop until the last frame is complete
  if (Spi1flag)
    while (!(IMXRT_LPSPI3_S.SR & LPSPI_SR_FCF))
      ;  //Loop until the last frame is complete
  Spi1flag = 0;
  digitalWriteFast(CS_R_G_X_Y, HIGH);  //Set the CS from the last transaction high
  digitalWriteFast(CS_B, HIGH);        //Set the CS from the last transaction high for the blue channel in case it was active (possibly use a flag to check??)

  //Everything between here and setting the CS pin low determines how long the CS signal is high

  IMXRT_LPSPI4_S.SR = LPSPI_SR_FCF;  //Clear the flag
  IMXRT_LPSPI3_S.SR = LPSPI_SR_FCF;  //Clear the flag
  //IMXRT_LPSPI4_S.TCR=mytcr; //Go back to 8 bit mode (can stay in 16 bit mode)
  //temp = IMXRT_LPSPI4_S.RDR; //Go ahead and read the receive FIFO (not necessary since we have masked receive data above)

  value &= 0x0FFF;  // mask out just the 12 bits of data

  // add the output channel A or B on the selected DAC, and buffer flag
#ifdef BUFFERED
  // select the output channel on the selected DAC, buffered, no gain
  value |= 0x7000 | (dac ? 0x8000 : 0x0000);

#else
  // select the output channel on the selected DAC, unbuffered, no gain
  value |= 0x3000 | (dac ? 0x8000 : 0x0000);

#endif

  digitalWriteFast(CS_B, LOW);

  //Set up the transaction directly with the SPI registers because the normal transfer16
  //function will busy wait for the SPI transfer to complete.  We will wait for completion
  //and de-assert CS the next time around to speed things up.
  //By doing this the code can do other things instead of busy waiting for the SPI transaction
  //like it does with the stock functions.
  //IMXRT_LPSPI4_S.TCR=(mytcr & 0xfffff000) | LPSPI_TCR_FRAMESZ(15);  // turn on 16 bit mode  (this is done above and we keep it on now)
  Spiflag = 1;
  IMXRT_LPSPI4_S.TDR = value;  //Send data to the SPI fifo and start transaction but don't wait for it to be done
}

//Using both SPI and SPI1, write out two or three dacs at once
//if allchannels is set, all 3 dacs are written so blue and green are set to value2
//otherwise value is x or red, and value2 is y or green

void MCP4922_write2(int dac, uint16_t value, uint16_t value2, int allchannels) {
  // uint32_t temp;

  //Wait for the last transaction to finish and then set CS high from the last transaction
  //By doing this the code can do other things instead of busy waiting for the SPI transaction
  //like it does with the stock functions.
  if (Spiflag)
    while (!(IMXRT_LPSPI4_S.SR & LPSPI_SR_FCF))
      ;  //Loop until the last frame is complete
  if (Spi1flag)
    while (!(IMXRT_LPSPI3_S.SR & LPSPI_SR_FCF))
      ;                                //Loop until the last frame is complete
  digitalWriteFast(CS_R_G_X_Y, HIGH);  //Set the CS from the last transaction high
  digitalWriteFast(CS_B, HIGH);        //Set the CS from the last transaction high for the blue channel in case it was active (possibly use a flag to check??)

  //Everything between here and setting the CS pin low determines how long the CS signal is high

  IMXRT_LPSPI4_S.SR = LPSPI_SR_FCF;  //Clear the flag
  IMXRT_LPSPI3_S.SR = LPSPI_SR_FCF;  //Clear the flag
  //IMXRT_LPSPI4_S.TCR=mytcr; //Go back to 8 bit mode (can stay in 16 bit mode)
  //temp = IMXRT_LPSPI4_S.RDR; //Go ahead and read the receive FIFO (not necessary since we have masked receive data above)

  value &= 0x0FFF;   // mask out just the 12 bits of data
  value2 &= 0x0FFF;  // mask out just the 12 bits of data

  // add the output channel A or B on the selected DAC, and buffer flag
#ifdef BUFFERED
  // select the output channel on the selected DAC, buffered, no gain
  value |= 0x7000 | (dac ? 0x8000 : 0x0000);
  value2 |= 0x7000 | (dac ? 0x8000 : 0x0000);
#else
  // select the output channel on the selected DAC, unbuffered, no gain
  value |= 0x3000 | (dac ? 0x8000 : 0x0000);
  value2 |= 0x3000 | (dac ? 0x8000 : 0x0000);
#endif

  digitalWriteFast(CS_R_G_X_Y, LOW);
  if (allchannels) digitalWriteFast(CS_B, LOW);  //Write out the blue with the same value as green

  //Set up the transaction directly with the SPI registers because the normal transfer16
  //function will busy wait for the SPI transfer to complete.  We will wait for completion
  //and de-assert CS the next time around to speed things up.
  //By doing this the code can do other things instead of busy waiting for the SPI transaction
  //like it does with the stock functions.
  //IMXRT_LPSPI4_S.TCR=(mytcr & 0xfffff000) | LPSPI_TCR_FRAMESZ(15);  // turn on 16 bit mode  (this is done above and we keep it on now)
  Spiflag = 1;
  IMXRT_LPSPI4_S.TDR = value2;  //Send data to the SPI fifo and start transaction but don't wait for it to be done

  Spi1flag = 1;
  IMXRT_LPSPI3_S.TDR = value;  //Send data to the SPI1 fifo and start transaction but don't wait for it to be done
}