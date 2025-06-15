/*
   VSTCM

   Vector Signal Transceiver Colour Mod using MCP4922 DACs on the Teensy 4.1

   Code to draw on the vector monitor

*/
#include "main.h"

#ifdef VSTCM
#include <SD.h>
#else
#include <corecrt_math.h>
#include SDL_PATH
#endif

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

// Cached settings for fast access
uint16_t x_axis_invert_mask = 0;
uint16_t y_axis_invert_mask = 0;
bool dac_swap = false;
bool correction_enabled = false;        // pincushion correction enabled
int dwell_before;
int dwell_after;
int move_speed;

volatile bool Beam_on;                // causes a compiler warning if placed in drawing.h ... why?
static ColourIntensity_t LastColInt;  // Stores last colour intensity levels

uint16_t gamma_red[256];
uint16_t gamma_green[256];
uint16_t gamma_blue[256];

extern int32_t get_setting_value(const char* ini_label, int32_t default_value);
extern int offdwell0;

#ifdef VSTCM
extern int Spiflag, Spi1flag;  //Keeps track of an active SPI transaction in progress
#else
extern SDL_Renderer* rend_2D_orig;  // Renderer for original 2D game
extern int gX, gY;                  // Last position of beam
static int untransformed_gX = 0, untransformed_gY = 0;  // Store untransformed coordinates
#endif

enum { TOP = 0x1,
       BOTTOM = 0x2,
       RIGHT = 0x4,
       LEFT = 0x8 };
enum { FALSE,
       TRUE };

typedef unsigned int outcode;

inline outcode compute_outcode(int x, int y, int xmin, int ymin, int xmax, int ymax) {
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

void cohen_sutherlandCustom(int32_t* x1, int32_t* y1, int32_t* x2, int32_t* y2, int xmin, int ymin, int xmax, int ymax) {
  outcode outcode1 = compute_outcode(*x1, *y1, xmin, ymin, xmax, ymax);
  outcode outcode2 = compute_outcode(*x2, *y2, xmin, ymin, xmax, ymax);

  while (true) {
    if (!(outcode1 | outcode2)) {  // Bitwise OR is 0 -> both points inside
      return;
    }
    if (outcode1 & outcode2) {  // Bitwise AND is non-zero -> both points outside on the same side
      *x1 = 1000000;            // Reject line
      return;
    }

    // Pick the point outside
    outcode outcode_ex = outcode1 ? outcode1 : outcode2;
    int x, y;

    // Compute intersection
    if (outcode_ex & TOP) {
      x = *x1 + (*x2 - *x1) * (ymax - *y1) / (*y2 - *y1);
      y = ymax;
    } else if (outcode_ex & BOTTOM) {
      x = *x1 + (*x2 - *x1) * (ymin - *y1) / (*y2 - *y1);
      y = ymin;
    } else if (outcode_ex & RIGHT) {
      y = *y1 + (*y2 - *y1) * (xmax - *x1) / (*x2 - *x1);
      x = xmax;
    } else {  // LEFT
      y = *y1 + (*y2 - *y1) * (xmin - *x1) / (*x2 - *x1);
      x = xmin;
    }

    // Replace the outside point and recompute its outcode
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
}

inline void update_frame_extents(int x, int y) {
    if (x > frame_max_x) frame_max_x = x;
    if (x < frame_min_x) frame_min_x = x;
    if (y > frame_max_y) frame_max_y = y;
    if (y < frame_min_y) frame_min_y = y;
}

// Precomputed constants for fixed-point correction
int32_t correction_x_factor = 0;  // scaled by 2^24
int32_t correction_y_factor = 0;

extern bool flip_x, flip_y, swap_xy;

static inline void transform_point(int x, int y, int* x_out, int* y_out) {
    // Apply pincushion correction
    if (correction_enabled) {
        int32_t xf = (int32_t)x - 2048;
        int32_t yf = (int32_t)y - 2048;

        // Fixed-point square calculation (Q8.24)
        int64_t yf_sq = (int64_t)yf * (int64_t)yf; // Q0.32
        int64_t xf_sq = (int64_t)xf * (int64_t)xf; // Q0.32

        // Apply correction: xf * (1 - yf^2 * factor)
        int32_t xcorr = xf - (int32_t)(((yf_sq * correction_x_factor) >> 24) * xf >> 24);
        int32_t ycorr = yf - (int32_t)(((xf_sq * correction_y_factor) >> 24) * yf >> 24);

        x = (uint16_t)(xcorr + 2048);
        y = (uint16_t)(ycorr + 2048);
    }

    // Apply axis inversion
    if (flip_x) x = 4095 - x;
    if (flip_y) y = 4095 - y;

    // Apply axis swap
    if (swap_xy) {
        int temp = x;
        x = y;
        y = temp;
    }

    // Apply DAC swap if needed
    if (dac_swap) {
        int temp = x;
        x = y;
        y = temp;
    }

    *x_out = x;
    *y_out = y;
}



#define FIXED_SHIFT 16
#define FIXED_ONE (1 << FIXED_SHIFT)

// Optimized fixed-point line drawing for Teensy 4.x
void _draw_lineto(const int x1, const int y1, int bright_shift) {
#ifdef VSTCM
  int dx = x1 - x_pos;
  int dy = y1 - y_pos;

  // Update frame extents for spot killer logic
  update_frame_extents(x1, y1);

  int dxmag = abs(dx);
  int dymag = abs(dy);
  int max_dist = (dxmag > dymag) ? dxmag : dymag;

  // Enforce minimum number of steps for visible beam segments
  if (Beam_on && (max_dist * 2 < bright_shift)) {
    bright_shift = max_dist >> 1;
    if (bright_shift < 1) bright_shift = 1;
  }

  // Calculate number of steps (manual integer ceiling)
  int numsteps = (max_dist + bright_shift - 1) / bright_shift;
  if (numsteps < 1) numsteps = 1;

  // Calculate fixed-point increments
  int dxf = (dx << FIXED_SHIFT) / numsteps;
  int dyf = (dy << FIXED_SHIFT) / numsteps;

  // Start position in fixed-point
  int xcur = x_pos << FIXED_SHIFT;
  int ycur = y_pos << FIXED_SHIFT;

  int last_xout = -1, last_yout = -1;

  for (int i = 0; i < numsteps; i++) {
      xcur += dxf;
      ycur += dyf;

      int xout = (xcur + (FIXED_ONE >> 1)) >> FIXED_SHIFT;
      int yout = (ycur + (FIXED_ONE >> 1)) >> FIXED_SHIFT;

      if (xout != last_xout || yout != last_yout) {
          goto_xy(xout, yout);
          last_xout = xout;
          last_yout = yout;
      }
  }

  // Ensure final point is exact
  goto_xy(x1, y1);
  SPI_flush();
#else
    int dx = x1 - x_pos;
    int dy = y1 - y_pos;

    int dxmag = abs(dx);
    int dymag = abs(dy);
    int max_dist = (dxmag > dymag) ? dxmag : dymag;

    // Enforce minimum number of steps for visible beam segments
    if (Beam_on && (max_dist * 2 < bright_shift)) {
        bright_shift = max_dist >> 1;
        if (bright_shift < 1) bright_shift = 1;
    }

    int numsteps = max_dist / bright_shift;
    if (numsteps < 1) numsteps = 1;

    // Use floating point for stepping calculations
    float x_step = (float)dx / numsteps;
    float y_step = (float)dy / numsteps;

    float x_cur = x_pos;
    float y_cur = y_pos;

    // Transform starting point
    int transformed_old_x, transformed_old_y;
    transform_point(x_pos, y_pos, &transformed_old_x, &transformed_old_y);

    for (int i = 0; i < numsteps; i++) {
        x_cur += x_step;
        y_cur += y_step;

        int x_int = (int)(x_cur + 0.5); // rounding
        int y_int = (int)(y_cur + 0.5);

        int transformed_x, transformed_y;
        transform_point(x_int, y_int, &transformed_x, &transformed_y);

        // Draw line segment
        SDL_RenderDrawLine(rend_2D_orig,
            transformed_old_x / 4, (4096 - transformed_old_y) / 4,
            transformed_x / 4, (4096 - transformed_y) / 4);

        transformed_old_x = transformed_x;
        transformed_old_y = transformed_y;
    }

    // Draw final segment to exact endpoint
    int transformed_x1, transformed_y1;
    transform_point(x1, y1, &transformed_x1, &transformed_y1);
    SDL_RenderDrawLine(rend_2D_orig,
        transformed_old_x / 4, (4096 - transformed_old_y) / 4,
        transformed_x1 / 4, (4096 - transformed_y1) / 4);

    // Update position state
    x_pos = x1;
    y_pos = y1;
    untransformed_gX = x1;
    untransformed_gY = y1;
    update_frame_extents(x1, y1);
    gX = transformed_x1;
    gY = transformed_y1;
#endif


}

void old_draw_lineto(const int x1, const int y1, int bright_shift) {
#ifdef VSTCM
    int dx = x1 - x_pos;
    int dy = y1 - y_pos;

    // Update frame extents for spot killer logic
    update_frame_extents(x1, y1);

    int dxmag = abs(dx);
    int dymag = abs(dy);
    int max_dist = (dxmag > dymag) ? dxmag : dymag;

    // Enforce minimum number of steps for visible beam segments
    if (Beam_on && (max_dist * 2 < bright_shift)) {
        bright_shift = max_dist >> 1;
        if (bright_shift < 1) bright_shift = 1;
    }

    // Calculate number of steps (manual integer ceiling)
    int numsteps = (max_dist + bright_shift - 1) / bright_shift;
    if (numsteps < 1) numsteps = 1;

    // Calculate fixed-point increments
    int dxf = (dx << FIXED_SHIFT) / numsteps;
    int dyf = (dy << FIXED_SHIFT) / numsteps;

    // Start position in fixed-point
    int xcur = x_pos << FIXED_SHIFT;
    int ycur = y_pos << FIXED_SHIFT;

    int last_xout = -1, last_yout = -1;

    for (int i = 0; i < numsteps; i++) {
        xcur += dxf;
        ycur += dyf;

        int xout = (xcur + (FIXED_ONE >> 1)) >> FIXED_SHIFT;
        int yout = (ycur + (FIXED_ONE >> 1)) >> FIXED_SHIFT;

        if (xout != last_xout || yout != last_yout) {
            goto_xy(xout, yout);
            last_xout = xout;
            last_yout = yout;
        }
    }

    // Ensure final point is exact
    goto_xy(x1, y1);
    SPI_flush();
#else
    // Apply transformations for SDL version
    int transformed_gX = untransformed_gX;
    int transformed_gY = untransformed_gY;
    int transformed_x1 = x1;
    int transformed_y1 = y1;

    // Apply flip transformations
    if (flip_x) {
        transformed_gX = 4095 - transformed_gX;
        transformed_x1 = 4095 - transformed_x1;
    }
    if (flip_y) {
        transformed_gY = 4095 - transformed_gY;
        transformed_y1 = 4095 - transformed_y1;
    }

    // Apply swap transformation
    if (swap_xy) {
        int temp_gX = transformed_gX;
        int temp_x1 = transformed_x1;
        transformed_gX = transformed_gY;
        transformed_x1 = transformed_y1;
        transformed_gY = temp_gX;
        transformed_y1 = temp_x1;
    }

    SDL_RenderDrawLine(rend_2D_orig, transformed_gX / 4, (4096 - transformed_gY) / 4,
        transformed_x1 / 4, (4096 - transformed_y1) / 4);

    // Store untransformed coordinates for next time
    untransformed_gX = x1;
    untransformed_gY = y1;

    // Update transformed coordinates for compatibility
    gX = transformed_x1;
    gY = transformed_y1;
#endif


}
void draw_to_xyrgb(int x, int y, uint8_t red, uint8_t green, uint8_t blue) {
  brightness(red, green, blue);  // Set RGB intensity levels from 0 to 255
  _draw_lineto(x, y, line_draw_speed);
}

void draw_string(const char* s, int x, int y, int size, int intensity) {
  while (*s) {
    char c = *s++;
    x += draw_character(c, x, y, size, intensity);
  }
}

int draw_character(char c, int x, int y, int size, int brightness) {
  const hershey_char_t* const f = &hershey_simplex[c - ' '];
  int next_moveto = 1;

  for (int i = 0; i < f->count; i++) {
    int dx = f->points[2 * i + 0];
    int dy = f->points[2 * i + 1];
    if (dx == -1) {
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

void draw_moveto(int x1, int y1) {
    brightness(0, 0, 0);

    // hold the current position for a few clocks
    // with the beam off
    dwell(dwell_before);
    _draw_lineto(x1, y1, move_speed);
    dwell(dwell_after);

    // This is only needed to handle dwell times on some real vector monitors
    frame_max_x = max(frame_max_x, x1);
    frame_min_x = min(frame_min_x, x1);
    frame_max_y = max(frame_max_y, y1);
    frame_min_y = min(frame_min_y, y1);

    // Save the start position for drawing
#ifdef VSTCM
#else  // only needed for SDL
  // Transform and store position
    transform_point(x1, y1, &gX, &gY);
    untransformed_gX = x1;
    untransformed_gY = y1;
#endif
}

void old_draw_moveto(int x1, int y1) {
  brightness(0, 0, 0);

  // hold the current position for a few clocks
  // with the beam off
  dwell(dwell_before);
  _draw_lineto(x1, y1, move_speed);
  dwell(dwell_after);

  // This is only needed to handle dwell times on some real vector monitors
  frame_max_x = max(frame_max_x, x1);
  frame_min_x = min(frame_min_x, x1);
  frame_max_y = max(frame_max_y, y1);
  frame_min_y = min(frame_min_y, y1);

// Save the start position for drawing
#ifdef VSTCM
#else  // only needed for SDL
  // Store untransformed coordinates but don't draw anything
  untransformed_gX = x1;
  untransformed_gY = y1;

  // Update transformed coordinates for compatibility
  int transformed_x1 = x1;
  int transformed_y1 = y1;

  if (flip_x) {
      transformed_x1 = 4095 - transformed_x1;
  }
  if (flip_y) {
      transformed_y1 = 4095 - transformed_y1;
  }
  if (swap_xy) {
      int temp = transformed_x1;
      transformed_x1 = transformed_y1;
      transformed_y1 = temp;
  }

  gX = transformed_x1;
  gY = transformed_y1;
#endif
}

void make_gammatable(float gamma, uint16_t maxinput, uint16_t maxoutput, uint16_t* table) {
  for (int i = 0; i < (maxinput + 1); i++)
    table[i] = (pow((float)i / (float)maxinput, gamma) * (float)maxoutput);
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
  // Do nothing if we haven't changed colour since last time
  if ((LastColInt.red == red) && (LastColInt.green == green) && (LastColInt.blue == blue)) return;

  if (get_setting_value("COLOUR_SWITCH", COLOUR_SWITCH) == 0)
  {
    // Mix colours if using monochrome monitor using average value
    // uint8_t avg = (red + green + blue) / 3;
    // Mix colours if using monochrome monitor using maximum value
    uint8_t avg = (red > green) ? ((red > blue) ? red : blue) : ((green > blue) ? green : blue);
    red = green = blue = avg;
  }

#ifdef VSTCM
  if (green == blue) {  //We can write all 3 at the same time if green is the same as blue
    LastColInt.red = red;
    LastColInt.green = green;
    LastColInt.blue = blue;
     MCP4922_write2(DAC_CHAN_RGB, red << 3, green << 3 , 1);    //Shift by 3 to go to half scale maximum
   // MCP4922_write2(DAC_CHAN_RGB, gamma_red[red], gamma_green[green], 1);
  }

  if ((LastColInt.red != red) || (LastColInt.green != green)) {  //We can write red and green at the same time
    LastColInt.red = red;
    LastColInt.green = green;
     MCP4922_write2(DAC_CHAN_RGB, red << 3, green << 3 , 0);
   // MCP4922_write2(DAC_CHAN_RGB, gamma_red[red], gamma_green[green], 0);
  }

  if (LastColInt.blue != blue) {
    LastColInt.blue = blue;
    MCP4922_write1(DAC_CHAN_RGB, blue << 3);
   // MCP4922_write1(DAC_CHAN_RGB, gamma_blue[blue]);
  }

  //Dwell moved here since it takes about 4us to fully turn on or off the beam
  //Possibly change where this is depending on if the beam is being turned on or off??

  Beam_on = (red || green || blue);  // Should be faster than if ...

  dwell(offdwell0);  //Wait this amount before changing the beam (turning it on or off)
#else
  LastColInt.red = red;
  LastColInt.green = green;
  LastColInt.blue = blue;
  // Ignore gamma values on PC, colours are more faithful
   // SDL_SetRenderDrawColor(rend_2D_orig, gamma_red[red], gamma_green[green], gamma_blue[blue], 255);
  SDL_SetRenderDrawColor(rend_2D_orig, red, green, blue, 255);

#endif
}



static inline void goto_xy(uint16_t x, uint16_t y) {
    if ((x_pos == x) && (y_pos == y)) return;
#ifdef VSTCM
    x_pos = x;
    y_pos = y;
#else

    // Store last position of beam for later use
    int16_t oldx = x_pos = x;
    int16_t oldy = y_pos = y;
#endif


	// pincushion correction
    if (correction_enabled) {
        int32_t xf = (int32_t)x - 2048;
        int32_t yf = (int32_t)y - 2048;

        // Fixed-point square calculation (Q8.24)
        int64_t yf_sq = (int64_t)yf * (int64_t)yf; // Q0.32
        int64_t xf_sq = (int64_t)xf * (int64_t)xf; // Q0.32

        // Apply correction: xf * (1 - yf^2 * factor)
        int32_t xcorr = xf - (int32_t)(((yf_sq * correction_x_factor) >> 24) * xf >> 24);
        int32_t ycorr = yf - (int32_t)(((xf_sq * correction_y_factor) >> 24) * yf >> 24);

        x = (uint16_t)(xcorr + 2048);
        y = (uint16_t)(ycorr + 2048);
    }

    // Axis inversion via XOR masks
    x ^= x_axis_invert_mask;
    y ^= y_axis_invert_mask;
#ifdef VSTCM
    // Fast DAC write with inline swap
    MCP4922_write2(DAC_CHAN_XY, dac_swap ? x : y, dac_swap ? y : x, 0);
#else
    SDL_RenderDrawLine(rend_2D_orig, dac_swap ? oldx : oldy, dac_swap ? oldy : oldx, dac_swap ? x : y, dac_swap ? y : x);
#endif

}

// Doing it this way is meant to force the compiler to not create an empty function which is called many times needlessly on PC
// but it doesn't work yet

void dwell(int count) {
#ifdef VSTCM
  SPI_flush();  // Get the DACs set to their latest values before we wait
  for (int i = 0; i < count; i++) {
    delayNanoseconds(500);
  }

#else
  (void)count;  // Expands to nothing, removing overhead on PC

#endif
}

//Write out to the one dac that is by itself
void MCP4922_write1(int dac, uint16_t value) {
#ifdef VSTCM
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
#endif
}

//Using both SPI and SPI1, write out two or three dacs at once
//if allchannels is set, all 3 dacs are written so blue and green are set to value2
//otherwise value is x or red, and value2 is y or green

void MCP4922_write2(int dac, uint16_t value, uint16_t value2, int allchannels) {
#ifdef VSTCM
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
#endif
}