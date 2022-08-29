//
// Tail Gunner (not working properly yet, no z blanking, sound or controls)
//
// Made for the GP32 console by Graham Toal
//
// Adapted for VSTCM on Teensy
// by Robin Champion - July 2022
//
#include <SPI.h>
#include "externs.h"
#include "tailgunner-data.h"  /* Preload whole eprom here */

void setup()
{
  int  n;
  long *pal;

  Serial.begin(115200);
  while ( !Serial && millis() < 4000 );

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

  SPI.begin();

  // copy palette..
  rLCDCON1 &= ~1;
  pal = (long *)PALETTE;
  // set every colour to white except the background
  pal[0] = 0x31 << 1; //  so why is the background still black?  it shouldn't be!
  for (n = 1; n < 16; n++)
    pal[n] = 0xfffe;  // max white, for now

  // Ensure 4KB alignment..
  framebuf = (unsigned char*)((((long)localbuf) + 0xfffL) & 0xfffff000L);

  // appears to be 320x240x4 mode despite comment below about 16bit mode
  surface1.ptbuffer = framebuf;
  surface1.bpp = 4;
  surface1.buf_w = SCREEN_X;
  surface1.buf_h = SCREEN_Y;
  surface1.ox = 0;
  surface1.oy = 0;
  surface1.o_buffer = NULL; // WHAT IS THIS???

  surface2.ptbuffer = (unsigned char *)(framebuf + (FRAMEBUFFER_SIZE >> 1));
  surface2.bpp = 4;
  surface2.buf_w = SCREEN_X;
  surface2.buf_h = SCREEN_Y;
  surface2.ox = 0;
  surface2.oy = 0;
  surface2.o_buffer = NULL;

  framebuffer_strt = framebuf;
  framebuffer_stop = framebuffer_strt + FRAMEBUFFER_SIZE;

  //   ARMDisableInterrupt();

  // Do the MMU mapping.. remember 4KB pages!
  //   for the mmu flags: 0xff2 -> no cache, no writeback

  //  swi_mmu_change(framebuffer_strt,framebuffer_stop-1,0x00000ff2);

  // 320x240x16 screen mode, 0 pixel offscreen - this comment appears to be wrong...
  // expects 33MHz HCLK

  rLCDCON1 = ((3 << 8) | (0 << 7) | (3 << 5) | (10 << 1) | (0));
  // CLKVAL=3, VNMODE=0, PNRMODE=TFT, BPPMODE=1000 (4bits), ENDID=0

  rLCDCON2 = ((1 << 24) | (319 << 14) | (2 << 6) | 1);
  // VBPD=1, LINEVAL=319, VFPD=2, VSPW=1

  rLCDCON3 = ((6 << 19) | (239 << 8) | (2));
  // HBPD=6, HOZVAL=239, HFPD=2

  rLCDCON4 = ((1 << 24) | (0 << 16) | (4));
  // PALADDEN=0, ADDVAL=0, HSPW=4

  rLCDCON5 = ((1 << 10) | (1 << 9) | (1 << 8) | (0 << 7) | (0 << 6) |
              (0 << 4) | (1 << 2) | (0));
  // INVVCLK=1, INVVLINE=1, INVVFRAME=1, INVVD=0, INVVDEN=0
  // INVENDLINE=0, ENLEND=0, BSWP=0, HWSWP=1

  rTPAL = 0;

  DisplayBuffer(&surface2); // dispay surface 2 initially
  rLCDSADDR3 = ((0 << 11) | (60));
  // OFFSIZE=0, PAGEWIDTH=60 halfwords -> 240 pix in 4bpp
  rLCDCON1 |= 1;  // enable lcd

  n = 0;
  gpb = rPBDAT; // 0x156
  gpe = rPEDAT;

  waitline(2);
  waitline(1);

  surface = &surface1;  // allow writing to surface one just in case init does any

  initTailGunner();
}

void loop()
{
  cineExecuteFrame();

  waitline(5);

  // Go to the center of the screen, turn the beam off (prevents stray coloured lines from appearing)
  /*  brightness(0, 0, 0);
    dwell(8);
    goto_x(2048);
    goto_y(2048);*/
  //  SPI_flush();
}

static void SlowSetPixel(int32_t x, int32_t y, int vgColour)
{
  unsigned char *p;

  x ^= 7; // funny screen layout
  p = &surface->ptbuffer[(y * 240 + x) >> 1];
  *p = *p | (15 << ((x & 1) << 2)); // draw in colour 15 (brightest, when we add grey scale)

  // scale up the coordinates to better suit 4096 x 4096
  // x = x * 12;
  //  y = y * 12;

  /*  Serial.print(x);
    Serial.print(";");
    Serial.print(y);
    Serial.print(";");
    Serial.print(*p);
    Serial.print(";");
    Serial.println(vgColour);*/

  brightness(0, vgColour * 10, 0);

  goto_x(x);
  goto_y(y);
}

// I have faster algorithms than this - even an anti-aliased one - but
// this is rock-solid reliable code that can be trusted during the debugging phase
// (and also it appears to be fast enough already!)

static void SlowDrawLine(int32_t y1, int32_t x1, int32_t y2, int32_t x2, int vgColour)
{
  int32_t temp, dx, dy;
  uint32_t x, y;
  int32_t x_sign, y_sign, flag;

  dx = abs(x2 - x1); // Delta of X
  dy = abs(y2 - y1); // Delta of Y

  // Make sure that first coordinate is the one with least value
  if (((dx >= dy) && (x1 > x2)) || ((dy > dx) && (y1 > y2)))
  {
    temp = x1;
    x1 = x2;
    x2 = temp;
    temp = y1;
    y1 = y2;
    y2 = temp;
  }

  if ((y2 - y1) < 0)
    y_sign = -1; // The direction into which Y-coord shall travel
  else
    y_sign = 1;            // Same for X

  if ((x2 - x1) < 0)
    x_sign = -1; // ---- " ----
  else
    x_sign = 1;            // ---- " ----

  if (dx >= dy) // Which one of the deltas is the greatest one
  {
    for (x = x1, y = y1, flag = 0; x <= x2; x++, flag += dy) // From x1 to x2
    { // Also increase the
      if (flag >= dx) // Increase/decrease     // flag (displacement value)
      { // y!
        flag -= dx;
        y += y_sign;
      }

      SlowSetPixel(x, y, vgColour); // Plot the pixel
    }
  }
  else
  {
    // This is the same as above, just with x as y and vice versa.
    for (x = x1, y = y1, flag = 0; y <= y2; y++, flag += dx)
    {
      if (flag >= dy)
      {
        flag -= dy;
        x += x_sign;
      }

      SlowSetPixel(x, y, vgColour);
    }
  }
}

void DisplayBuffer(GPDRAWSURFACE *surface)
{
  //  Serial.println("Starting DisplayBuffer");
  long ptr = (long)surface->ptbuffer;

  rLCDSADDR1 = ((ptr & 0x0fc00000) >> 1) | ((ptr & 0x3ffffe) >> 1);
  rLCDSADDR2 = ((ptr & 0x003ffffe) >> 1) + (SCREEN_X) * (60) ;
}

inline void waitline( unsigned int line )
{
  // Serial.println("Starting waitline");
  // There's nothing to make this state change anymore, so leave it out for the moment
  //  while (line != (rLCDCON1 >> 18));
  delay(8);
}

void CinemaClearScreen()
{
  mousecode = 0;
  mousecode |= IO_LEFT;
  mousecode |= IO_RIGHT;

  if ((gpb & rKEY_RIGHT) == 0)
    mousecode &= ~IO_RIGHT;

  if ((gpb & rKEY_LEFT) == 0)
    mousecode &= ~IO_LEFT;

  // don't ask me why these are all reversed - crazy system!
  if ((gpb & rKEY_DOWN) == 0)
    mousecode |= IO_UP;
  else
    mousecode &= ~IO_UP;

  if ((gpb & rKEY_UP) == 0)
    mousecode |= IO_DOWN;
  else
    mousecode &= ~IO_DOWN;

  /*
     if (... fire button is pressed ...) fireflag &= ~IO_FIRE; else fireflag |= IO_FIRE;
     if (... shields button is pressed ...) shieldsflag &= ~IO_SHIELDS; else shieldsflag |= IO_SHIELDS;

     If the start button is pressed, set "startflag = 0;" here
     If the coin button is pressed, set
     ioSwitches &= ~IO_COIN; / * Clear coin counter the bodgy way * /
     coinflag = 0;
     if the shields button is pressed, set "shieldsflag = IO_SHIELDS;"

     HOWEVER if *nothing* is pressed,

     startflag = IO_START; / * Bodge.  Clear start button. Allows it to be down for 1/50sec * /

  */

  // NOTE: AT THIS STAGE, THERE IS NO DEBOUNCING, SO ONE PRESS OF THE COIN GIVES 9 LIVES!
  if ((gpe & rKEY_START) == 0) startflag = 0; else startflag = IO_START;
  if ((gpb & rKEY_B) == 0) shieldsflag &= ~IO_SHIELDS; else shieldsflag |= IO_SHIELDS;
  if ((gpb & rKEY_A) == 0) fireflag &= ~IO_FIRE; else fireflag |= IO_FIRE;

  if ((gpe & rKEY_SELECT) == 0) {
    if ((gpe & rKEY_SELECT) != debounce_coin) {
      ioSwitches &= ~IO_COIN; coinflag = 0;
    } else {
      ioSwitches |= IO_COIN; coinflag = IO_COIN;  // only on down-press, for 1 frame
    }
  } else {
    ioSwitches |= IO_COIN; coinflag = IO_COIN;  // only on down-press, for 1 frame
  }
  debounce_coin = gpe & rKEY_SELECT;

  if (debounce_oneshots > 0) {
    debounce_oneshots -= 1;
    if (debounce_oneshots == 0) {
      startflag = IO_START; fireflag |= IO_FIRE;
    }
  }

  if (debounce_shields > 0) {
    debounce_shields -= 1;
    if (debounce_shields == 0) {
      shieldsflag |= IO_SHIELDS;
    }
  }

  // Force the game to start with no buttons connected
  startflag = IO_START;
  // coinflag = 0;

  ioInputs = (unsigned short) (mousecode | fireflag | shieldsflag | startflag);

  /* CAN WE DO SOMETHING LIKE: vsync(); */
}

void CinemaVectorData(uint32_t FromX, uint32_t FromY, uint32_t ToX, uint32_t ToY, uint32_t vgColour)
{
  /* TWEAK THESE TO MAKE THEM FIT... */

  if (vgColour == 0)
  {
    brightness(0, 0, 0);
    dwell(8);
    goto_x(ToX);
    goto_y(ToY);
    dwell(8);
    return;
  }

  // scale 1024x768 to 340x240, and rotate
  /* FromX = FromX / 3;
    ToX = ToX / 3;
    FromY = FromY / 3;
    ToY = ToY / 3;*/

  if (FromX < 0) FromX = 0;
  if (FromY < 0) FromY = 0;
  if (ToY >= SCREEN_Y) ToY = SCREEN_Y - 1;
  if (ToX >= SCREEN_X) ToX = SCREEN_X - 1;
  if (ToX < 0) ToX = 0;
  if (ToY < 0) ToY = 0;
  if (FromY >= SCREEN_Y) FromY = SCREEN_Y - 1;
  if (FromX >= SCREEN_X) FromX = SCREEN_X - 1;


  // sign extend short to long
  /*  ToX = ToX << 16;
    ToX = ToX >> 16;
    FromX = FromX << 16;
    FromX = FromX >> 16;*/

  SlowDrawLine(FromX, FromY, ToX, ToY, vgColour);
}

/* Reset C-CPU registers, flags, etc to default starting values
*/

void cineSetJMI (uint8_t j)
{
  ccpu_jmi_dip = j;
}

void cineSetMSize (uint8_t m)
{
  ccpu_msize = m;
}

void cineSetMonitor (uint8_t m)
{
  ccpu_monitor = m;
}

void cineReleaseTimeslice (void)
{
  bBailOut = true;
}

void initTailGunner()
{
  // Serial.println("Starting initTailGunner");
  int i;
  init_graph();

  bNewFrame = 0;
  bFlipX = 0;
  bFlipY = 0;
  bSwapXY = 0;
  ioInputs = 0xffff;

  ioSwitches = 0xffff;		/* Tweaked  testing bottom bit = quarters/game */
  ioSwitches = (ioSwitches & (~SW_SHIELDMASK)) | SW_SHIELDS;	/* GT */

  ioSwitches &= (unsigned short) ~SW_QUARTERS_PER_GAME;	/* One quarter per game */
  ioSwitches |= (unsigned short) quarterflag;

  cineSetJMI (0);		/* Use external input */
  cineSetMSize (1);		/* 8K */
  cineSetMonitor (0);		/* bi-level */

  CinemaClearScreen ();		/* Initialise ioInputs etc before game starts (optional) */

  for (i = 0; i < 256; i++)
    ram[i] = 0;
  return;
}

void brightness(uint8_t red, uint8_t green, uint8_t blue)
{
  if (LastColInt.red == red && LastColInt.green == green && LastColInt.blue == blue)
    return;

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

  //Dwell moved here since it takes about 4us to fully turn on or off the beam
  //Possibly change where this is depending on if the beam is being turned on or off??

  dwell(10); //Wait this amount before changing the beam (turning it on or off)
}

void goto_x(int32_t x)
{
  // 11 to 783

  x = x * 5;
  Serial.print(x);
  Serial.print(";");

  if (x > 4095) x = 4095;
  if (x < 1) x = 1;

  if (x != x_pos)     // no point if the beam is already in the right place
  {
    x_pos = x;

    //   if (v_config[6].pval == false)      // If FLIP X then invert x axis
    x = 4095 - x;

    MCP4922_write(SS1_IC4_X_Y, DAC_X_CHAN, x);
  }
}

void goto_y(int32_t y)
{
  // 2 to 1012

  y = y * 4;
  Serial.print(y);
  Serial.println(";");

  if (y > 4095) y = 4095;
  if (y < 1) y = 1;
  if (y != y_pos)     // no point if the beam is already in the right place
  {
    y_pos = y;

    //   if (v_config[7].pval == false)     // If FLIP Y then invert y axis
    y = 4095 - y;

    MCP4922_write(SS1_IC4_X_Y, DAC_Y_CHAN, y);
  }
}

void dwell(const int count)
{
  SPI_flush();              // Get the dacs set to their latest values before we wait

  // can work better or faster without this on some monitors
  for (int i = 0 ; i < count ; i++)
  {
    delayNanoseconds(500);  // NOTE this used to write the X and Y position but now the dacs won't get updated with repeated values
  }
}

// Finish the last SPI transactions
void SPI_flush()
{
  // Wait for the last transaction to finish and then set CS high from the last transaction
  // By doing this the code can do other things instead of busy waiting for the SPI transaction
  // like it does with the stock functions.
  // if (spiflag)
  //   while (!(IMXRT_LPSPI4_S.SR & LPSPI_SR_FCF));  // Loop until the last frame is complete

  // digitalWriteFast(activepin, HIGH);              // Set the CS from the last transaction high

  //  IMXRT_LPSPI4_S.SR = LPSPI_SR_FCF;                 // Clear the flag
  //  spiflag = 0;
  //  activepin = 0;
}

void MCP4922_write(int cs_pin, byte dac, uint16_t value)
{
  SPI.beginTransaction(SPISettings(5000000, MSBFIRST, SPI_MODE0));

  dac = (dac & 1) << 7;
  value &= 0x0FFF;                                // mask out just the 12 bits of data

  // add the output channel A or B on the selected DAC, and buffer flag
#ifdef BUFFERED
  // select the output channel on the selected DAC, buffered, no gain
  value |= 0x7000 | (dac  ? 0x8000 : 0x0000);
#else
  // select the output channel on the selected DAC, unbuffered, no gain
  value |= 0x3000 | (dac  ? 0x8000 : 0x0000);
#endif
  byte low = value & 0xff;
  //byte high = dac | 0x30 | ((value >> 8) & 0x0f);
  byte high = (value >> 8) & 0x0f;
  digitalWriteFast(cs_pin, LOW);
  delayNanoseconds(100);
  // better to use SPI.transfer16? spi.transfer with eventresponder works in non blocking mode with DMA apparently
  //  SPI.transfer(high);
  SPI.transfer(dac | 0x30 | high);
  SPI.transfer(low);

  // activepin = cs_pin;                             // store to deactivate at end of transfer

  delayNanoseconds(100);
  SPI.endTransaction();
  digitalWriteFast(cs_pin, HIGH);
}

// SBT code

// ops1c00.c

void cineExecute1c00(void)
{
  switch (register_PC)
  {
    case 0x1d70:

      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opADDimmX_A_AA (20) */
      cmp_old = register_A; cmp_new = 0x1a;
      register_A = flag_C = acc_a0 = 0x001a;
      /* opOUTbi_A_A (93) */
      /* opOUTsnd_A (93) */
      set_sound_data(0);
      /* opLSRe_A_AA (eb) */
      cmp_new = 0x0BEB; cmp_old = acc_a0 = register_A; flag_C = (0x0BEB + register_A);
      register_A >>= 1;
      /* opOUTbi_A_A (90) */
      /* opOUTsnd_A (90) */
      set_sound_addr_A(register_A & 1);
      /* opLSRe_A_AA (eb) */
      cmp_new = 0x0BEB; cmp_old = acc_a0 = register_A; flag_C = (0x0BEB + register_A);
      register_A >>= 1;
      /* opOUTbi_A_A (91) */
      /* opOUTsnd_A (91) */
      set_sound_addr_B(register_A & 1);
      /* opLSRe_A_AA (eb) */
      cmp_new = 0x0BEB; cmp_old = acc_a0 = register_A; flag_C = (0x0BEB + register_A);
      register_A >>= 1;
      /* opOUTbi_A_A (92) */
      /* opOUTsnd_A (92) */
      set_sound_addr_C(register_A & 1);
      /* opLSRe_A_AA (eb) */
      cmp_new = 0x0BEB; cmp_old = acc_a0 = register_A; flag_C = (0x0BEB + register_A);
      register_A >>= 1;
      /* opOUTbi_A_A (94) */
      /* opOUTsnd_A (94) */
      if (register_A & 1) strobe_sound_on(); else strobe_sound_off();
      /* opLSRe_A_AA (eb) */
      cmp_new = 0x0BEB; cmp_old = acc_a0 = register_A; flag_C = (0x0BEB + register_A);
      register_A >>= 1;
      /* opOUTbi_A_A (94) */
      /* opOUTsnd_A (94) */
      if (register_A & 1) strobe_sound_on(); else strobe_sound_off();
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opADDimmX_A_AA (20) */
      cmp_old = register_A; cmp_new = 0x14;
      register_A = flag_C = acc_a0 = 0x0014;
      /* opOUTbi_A_A (93) */
      /* opOUTsnd_A (93) */
      set_sound_data(0);
      /* opLSRe_A_AA (eb) */
      cmp_new = 0x0BEB; cmp_old = acc_a0 = register_A; flag_C = (0x0BEB + register_A);
      register_A >>= 1;
      /* opOUTbi_A_A (90) */
      /* opOUTsnd_A (90) */
      set_sound_addr_A(register_A & 1);
      /* opLSRe_A_AA (eb) */
      cmp_new = 0x0BEB; cmp_old = acc_a0 = register_A; flag_C = (0x0BEB + register_A);
      register_A >>= 1;
      /* opOUTbi_A_A (91) */
      /* opOUTsnd_A (91) */
      set_sound_addr_B(register_A & 1);
      /* opLSRe_A_AA (eb) */
      cmp_new = 0x0BEB; cmp_old = acc_a0 = register_A; flag_C = (0x0BEB + register_A);
      register_A >>= 1;
      /* opOUTbi_A_A (92) */
      /* opOUTsnd_A (92) */
      set_sound_addr_C(register_A & 1);
      /* opLSRe_A_AA (eb) */
      cmp_new = 0x0BEB; cmp_old = acc_a0 = register_A; flag_C = (0x0BEB + register_A);
      register_A >>= 1;
      /* opOUTbi_A_A (94) */
      /* opOUTsnd_A (94) */
      if (register_A & 1) strobe_sound_on(); else strobe_sound_off();
      /* opLSRe_A_AA (eb) */
      cmp_new = 0x0BEB; cmp_old = acc_a0 = register_A; flag_C = (0x0BEB + register_A);
      register_A >>= 1;
      /* opOUTbi_A_A (94) */
      /* opOUTsnd_A (94) */
      if (register_A & 1) strobe_sound_on(); else strobe_sound_off();
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opADDimmX_A_AA (20) */
      cmp_old = register_A; cmp_new = 0x1c;
      register_A = flag_C = acc_a0 = 0x001c;
      /* opOUTbi_A_A (93) */
      /* opOUTsnd_A (93) */
      set_sound_data(0);
      /* opLSRe_A_AA (eb) */
      cmp_new = 0x0BEB; cmp_old = acc_a0 = register_A; flag_C = (0x0BEB + register_A);
      register_A >>= 1;
      /* opOUTbi_A_A (90) */
      /* opOUTsnd_A (90) */
      set_sound_addr_A(register_A & 1);
      /* opLSRe_A_AA (eb) */
      cmp_new = 0x0BEB; cmp_old = acc_a0 = register_A; flag_C = (0x0BEB + register_A);
      register_A >>= 1;
      /* opOUTbi_A_A (91) */
      /* opOUTsnd_A (91) */
      set_sound_addr_B(register_A & 1);
      /* opLSRe_A_AA (eb) */
      cmp_new = 0x0BEB; cmp_old = acc_a0 = register_A; flag_C = (0x0BEB + register_A);
      register_A >>= 1;
      /* opOUTbi_A_A (92) */
      /* opOUTsnd_A (92) */
      set_sound_addr_C(register_A & 1);
      /* opLSRe_A_AA (eb) */
      cmp_new = 0x0BEB; cmp_old = acc_a0 = register_A; flag_C = (0x0BEB + register_A);
      register_A >>= 1;
      /* opOUTbi_A_A (94) */
      /* opOUTsnd_A (94) */
      if (register_A & 1) strobe_sound_on(); else strobe_sound_off();
      /* opLSRe_A_AA (eb) */
      cmp_new = 0x0BEB; cmp_old = acc_a0 = register_A; flag_C = (0x0BEB + register_A);
      register_A >>= 1;
      /* opOUTbi_A_A (94) */
      /* opOUTsnd_A (94) */
      if (register_A & 1) strobe_sound_on(); else strobe_sound_off();
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opADDimmX_A_AA (20) */
      cmp_old = register_A; cmp_new = 0x18;
      register_A = flag_C = acc_a0 = 0x0018;
      /* opOUTbi_A_A (93) */
      /* opOUTsnd_A (93) */
      set_sound_data(0);
      /* opLSRe_A_AA (eb) */
      cmp_new = 0x0BEB; cmp_old = acc_a0 = register_A; flag_C = (0x0BEB + register_A);
      register_A >>= 1;
      /* opOUTbi_A_A (90) */
      /* opOUTsnd_A (90) */
      set_sound_addr_A(register_A & 1);
      /* opLSRe_A_AA (eb) */
      cmp_new = 0x0BEB; cmp_old = acc_a0 = register_A; flag_C = (0x0BEB + register_A);
      register_A >>= 1;
      /* opOUTbi_A_A (91) */
      /* opOUTsnd_A (91) */
      set_sound_addr_B(register_A & 1);
      /* opLSRe_A_AA (eb) */
      cmp_new = 0x0BEB; cmp_old = acc_a0 = register_A; flag_C = (0x0BEB + register_A);
      register_A >>= 1;
      /* opOUTbi_A_A (92) */
      /* opOUTsnd_A (92) */
      set_sound_addr_C(register_A & 1);
      /* opLSRe_A_AA (eb) */
      cmp_new = 0x0BEB; cmp_old = acc_a0 = register_A; flag_C = (0x0BEB + register_A);
      register_A >>= 1;
      /* opOUTbi_A_A (94) */
      /* opOUTsnd_A (94) */
      if (register_A & 1) strobe_sound_on(); else strobe_sound_off();
      /* opLSRe_A_AA (eb) */
      cmp_new = 0x0BEB; cmp_old = acc_a0 = register_A; flag_C = (0x0BEB + register_A);
      register_A >>= 1;
      /* opOUTbi_A_A (94) */
      /* opOUTsnd_A (94) */
      if (register_A & 1) strobe_sound_on(); else strobe_sound_off();
      /* opLDJimm_A_A (45) */
      register_J = 0x0005;
      /* opJMP_A_A (58) */
      {
        register_PC = 0x1005;
        break;
      };

    case 0x1dc0:

      /* Invariants: register_P = 0x2 register_I = 0x1f register_A = 0x308 */;
      /* opNOP_A_A (5f) */
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opLDPimm_A_A (86) */
      register_P = 0x6; /* set page register */
      /* opCMPdir_A_AA (b9) */
      cmp_old = acc_a0 = register_A; /* backup old acc */
      flag_C = (((cmp_new = ram[register_I = 0x69]) ^ 0xFFF) + 1 + register_A);
      /* opLDJimm_A_A (45) */
      register_J = 0x0dd5;
      /* opJEQ_A_AA (5c) */
      if (cmp_new == cmp_old) {
        register_PC = 0x1dd5;
        break;
      };
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opADDimmX_A_AA (20) */
      cmp_old = register_A; cmp_new = 0x11;
      register_A = flag_C = acc_a0 = 0x0011;
      /* opOUTbi_A_A (93) */
      /* opOUTsnd_A (93) */
      set_sound_data(1);
      /* opLSRe_A_AA (eb) */
      cmp_new = 0x0BEB; cmp_old = acc_a0 = register_A; flag_C = (0x0BEB + register_A);
      register_A >>= 1;
      /* opOUTbi_A_A (90) */
      /* opOUTsnd_A (90) */
      set_sound_addr_A(register_A & 1);
      /* opLSRe_A_AA (eb) */
      cmp_new = 0x0BEB; cmp_old = acc_a0 = register_A; flag_C = (0x0BEB + register_A);
      register_A >>= 1;
      /* opOUTbi_A_A (91) */
      /* opOUTsnd_A (91) */
      set_sound_addr_B(register_A & 1);
      /* opLSRe_A_AA (eb) */
      cmp_new = 0x0BEB; cmp_old = acc_a0 = register_A; flag_C = (0x0BEB + register_A);
      register_A >>= 1;
      /* opOUTbi_A_A (92) */
      /* opOUTsnd_A (92) */
      set_sound_addr_C(register_A & 1);
      /* opLSRe_A_AA (eb) */
      cmp_new = 0x0BEB; cmp_old = acc_a0 = register_A; flag_C = (0x0BEB + register_A);
      register_A >>= 1;
      /* opOUTbi_A_A (94) */
      /* opOUTsnd_A (94) */
      if (register_A & 1) strobe_sound_on(); else strobe_sound_off();
      /* opLSRe_A_AA (eb) */
      cmp_new = 0x0BEB; cmp_old = acc_a0 = register_A; flag_C = (0x0BEB + register_A);
      register_A >>= 1;
      /* opOUTbi_A_A (94) */
      /* opOUTsnd_A (94) */
      if (register_A & 1) strobe_sound_on(); else strobe_sound_off();

    case 0x1dd5:

      /* Invariants: register_P = 0x6 register_I = 0x69 register_A = 0x00 */;
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opLDPimm_A_A (82) */
      register_P = 0x2; /* set page register */
      /* opSTAdir_A_A (d9) */
      ram[register_I = 0x29] = 0x000; /* store acc to RAM */
      /* opLDPimm_A_A (87) */
      register_P = 0x7; /* set page register */
      /* opSTAdir_A_A (d3) */
      ram[register_I = 0x73] = 0x000; /* store acc to RAM */
      /* opLDPimm_A_A (83) */
      register_P = 0x3; /* set page register */
      /* opSTAdir_A_A (d7) */
      ram[register_I = 0x37] = 0x000; /* store acc to RAM */
      /* opSTAdir_A_A (d9) */
      ram[register_I = 0x39] = 0x000; /* store acc to RAM */
      /* opSTAdir_A_A (dd) */
      ram[register_I = 0x3d] = 0x000; /* store acc to RAM */
      /* opLDPimm_A_A (84) */
      register_P = 0x4; /* set page register */
      /* opSTAdir_A_A (d7) */
      ram[register_I = 0x47] = 0x000; /* store acc to RAM */
      /* opSTAdir_A_A (d9) */
      ram[register_I = 0x49] = 0x000; /* store acc to RAM */
      /* opSTAdir_A_A (dd) */
      ram[register_I = 0x4d] = 0x000; /* store acc to RAM */
      /* opLDPimm_A_A (85) */
      register_P = 0x5; /* set page register */
      /* opSTAdir_A_A (d7) */
      ram[register_I = 0x57] = 0x000; /* store acc to RAM */
      /* opSTAdir_A_A (d9) */
      ram[register_I = 0x59] = 0x000; /* store acc to RAM */
      /* opSTAdir_A_A (dd) */
      ram[register_I = 0x5d] = 0x000; /* store acc to RAM */
      /* opLDAimm_A_AA (0f) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0x0f00;
      /* opLDPimm_A_A (83) */
      register_P = 0x3; /* set page register */
      /* opSTAdir_A_A (d3) */
      ram[register_I = 0x33] = 0xf00; /* store acc to RAM */
      /* opLDPimm_A_A (84) */
      register_P = 0x4; /* set page register */
      /* opSTAdir_A_A (d3) */
      ram[register_I = 0x43] = 0xf00; /* store acc to RAM */
      /* opLDPimm_A_A (85) */
      register_P = 0x5; /* set page register */
      /* opSTAdir_A_A (d3) */
      ram[register_I = 0x53] = 0xf00; /* store acc to RAM */
      /* opLDPimm_A_A (82) */
      register_P = 0x2; /* set page register */
      /* opNOP_A_B (57) */
      /* opLDAdir_B_AA (a3) */
      flag_C = acc_a0 = register_A;
      cmp_old = register_B; /* store old acc */
      register_B = cmp_new = ram[register_I = (register_P << 4) + 0x03]; /* new acc value */
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opASRDe_A_AA (ee) */
      cmp_new = 0x0EEE; cmp_old = acc_a0 = register_A; flag_C = (0x0EEE + register_A);
      register_A = (register_A >> 1) | ((register_B & 1) << 11);
      register_B = (register_B >> 1) | (register_B & 0x800);
      /* opASRDe_A_AA (ee) */
      cmp_new = 0x0EEE; cmp_old = acc_a0 = register_A; flag_C = (0x0EEE + register_A);
      register_A = (register_A >> 1) | ((register_B & 1) << 11);
      register_B = (register_B >> 1) | (register_B & 0x800);
      /* opASRDe_A_AA (ee) */
      cmp_new = 0x0EEE; cmp_old = acc_a0 = register_A; flag_C = (0x0EEE + register_A);
      register_A = (register_A >> 1) | ((register_B & 1) << 11);
      register_B = (register_B >> 1) | (register_B & 0x800);
      /* opADDdir_A_AA (62) */
      register_A = (flag_C = ((cmp_old = acc_a0 = register_A) + (cmp_new = ram[register_I = 0x22]))) & 0xFFF; /* do acc operation */
      /* opNOP_A_B (57) */
      /* opLDAirg_B_AA (ea) */
      flag_C = acc_a0 = register_A;
      cmp_old = register_B;
      register_B = cmp_new = ram[register_I];

      /* opSTAirg_A_A (e6) */
      ram[0x22] = register_A; /* store acc */
      /* opLDAimm_A_AA (02) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0x0200;
      /* opANDirg_A_AA (e9) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A &= (cmp_new = ram[register_I]);
      /* opLSLe_A_AA (ec) */
      cmp_new = 0x0CEC;
      cmp_old = acc_a0 = register_A;
      flag_C = (0x0CEC + register_A);
      register_A = (register_A << 1) & 0x0FFF;
      /* opSTAirg_A_A (e6) */
      ram[0x22] = register_A; /* store acc */
      /* opNOP_A_B (57) */
      /* opAWDirg_B_AA (e7) */
      acc_a0 = register_A;
      cmp_old = register_B;
      register_B = (flag_C = (register_B + (cmp_new = ram[register_I]))) & 0xFFF;
      set_watchdog();
      /* opLDAdir_A_AA (a3) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = 0x23]; /* set I register */

      /* opASRDe_A_AA (ee) */
      cmp_new = 0x0EEE; cmp_old = acc_a0 = register_A; flag_C = (0x0EEE + register_A);
      register_A = (register_A >> 1) | ((register_B & 1) << 11);
      register_B = (register_B >> 1) | (register_B & 0x800);
      /* opSTAirg_A_A (e6) */
      ram[0x23] = register_A; /* store acc */
      /* opNOP_A_B (57) */
      /* opSTAdir_B_BB (d2) */
      ram[register_I = (register_P << 4) + 0x2] = register_B; /* set I register and store B to ram */
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opADDimmX_A_AA (20) */
      cmp_old = register_A; cmp_new = 0xff;
      register_A = flag_C = acc_a0 = 0x00ff;
      /* opLDPimm_A_A (82) */
      /* opCMPdir_A_AA (b3) */
      cmp_old = acc_a0 = register_A; /* backup old acc */
      flag_C = (((cmp_new = ram[register_I = 0x23]) ^ 0xFFF) + 1 + register_A);
      /* opANDirg_A_AA (e9) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A &= (cmp_new = ram[register_I]);
      /* opADDimmX_A_AA (20) */
      register_A = (flag_C = ((acc_a0 = cmp_old = register_A) + (cmp_new = 0x00ff))) & 0xFFF; /* add values */
      /* opLDPimm_A_A (85) */
      register_P = 0x5; /* set page register */
      /* opSTAdir_A_A (d8) */
      ram[register_I = 0x58] = register_A; /* store acc to RAM */
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opLDPimm_A_A (83) */
      register_P = 0x3; /* set page register */
      /* opSTAdir_A_A (d8) */
      ram[register_I = 0x38] = 0x000; /* store acc to RAM */
      /* opLDPimm_A_A (85) */
      register_P = 0x5; /* set page register */
      /* opSUBdir_A_AA (78) */
      cmp_old = acc_a0 = register_A;
      register_A = (flag_C = (register_A + ((cmp_new = ram[register_I = 0x58]) ^ 0xFFF) + 1)) & 0xFFF; /* set regI addr */
      /* opLDPimm_A_A (84) */
      register_P = 0x4; /* set page register */
      /* opSTAdir_A_A (d8) */
      ram[register_I = 0x48] = register_A; /* store acc to RAM */
      /* opLDAimm_A_AA (0c) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0x0c00;
      /* opSTAdir_A_A (da) */
      ram[register_I = 0x4a] = 0xc00; /* store acc to RAM */
      /* opLDPimm_A_A (85) */
      register_P = 0x5; /* set page register */
      /* opSTAdir_A_A (da) */
      ram[register_I = 0x5a] = 0xc00; /* store acc to RAM */
      /* opLDPimm_A_A (83) */
      register_P = 0x3; /* set page register */
      /* opSTAdir_A_A (da) */
      ram[register_I = 0x3a] = 0xc00; /* store acc to RAM */
      /* opLDPimm_A_A (82) */
      register_P = 0x2; /* set page register */
      /* opLDAdir_A_AA (a2) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = 0x22]; /* set I register */

      /* opLSRe_A_AA (eb) */
      cmp_new = 0x0BEB; cmp_old = acc_a0 = register_A; flag_C = (0x0BEB + register_A);
      register_A >>= 1;
      /* opLSRe_A_AA (eb) */
      cmp_new = 0x0BEB; cmp_old = acc_a0 = register_A; flag_C = (0x0BEB + register_A);
      register_A >>= 1;
      /* opLSRe_A_AA (eb) */
      cmp_new = 0x0BEB; cmp_old = acc_a0 = register_A; flag_C = (0x0BEB + register_A);
      register_A >>= 1;
      /* opLDJimm_A_A (4d) */
      register_J = 0x0e9d;
      /* opJA0_A_A (5e) */
      if (acc_a0 & 0x01) {
        register_PC = 0x1e9d;
        break;
      };

      /* opLSRe_A_AA (eb) */
      cmp_new = 0x0BEB; cmp_old = acc_a0 = register_A; flag_C = (0x0BEB + register_A);
      register_A >>= 1;
      /* opJA0_A_A (5e) */
      if (acc_a0 & 0x01) {
        register_PC = 0x1e9d;
        break;
      };

      /* opLDPimm_A_A (82) */
      /* opLDAdir_A_AA (a3) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = 0x23]; /* set I register */

      /* opASRe_A_AA (ed) */
      cmp_new = 0xDED; cmp_old = flag_C = acc_a0 = register_A;
      register_A = SEX(register_A); /* make signed */
      register_A = (((signed short int)(((signed short int)register_A) >> (signed short int)1)) /* SAR */) & 0xFFF;
      /* opLDJimm_A_A (4b) */
      register_J = 0x0e5b;
      /* opJA0_A_A (5e) */
      if (acc_a0 & 0x01) {
        register_PC = 0x1e5b;
        break;
      };

      /* opLDPimm_A_A (83) */
      register_P = 0x3; /* set page register */
      /* opSTAdir_A_A (d2) */
      ram[register_I = 0x32] = register_A; /* store acc to RAM */
      /* opASRe_A_AA (ed) */
      cmp_new = 0xDED; cmp_old = flag_C = acc_a0 = register_A;
      register_A = SEX(register_A); /* make signed */
      register_A = (((signed short int)(((signed short int)register_A) >> (signed short int)1)) /* SAR */) & 0xFFF;
      /* opAWDirg_A_AA (e7) */
      cmp_old = acc_a0 = register_A;
      register_A = (flag_C = (register_A + (cmp_new = ram[0x32]))) & 0xFFF;
      set_watchdog();
      /* opSTAirg_A_A (e6) */
      ram[0x32] = register_A; /* store acc */
      /* opSUBimmX_A_AA (30) */
      cmp_old = acc_a0 = register_A; /* back up regA */
      register_A = (flag_C = (register_A + (((cmp_new = 0x7f) ^ 0xFFF) + 1))) & 0xFFF; /* add */
      /* opLDPimm_A_A (84) */
      register_P = 0x4; /* set page register */
      /* opSTAdir_A_A (d2) */
      ram[register_I = 0x42] = register_A; /* store acc to RAM */
      /* opADDimmX_A_AA (20) */
      register_A = (flag_C = ((acc_a0 = cmp_old = register_A) + (cmp_new = 0x00fe))) & 0xFFF; /* add values */
      /* opLDPimm_A_A (85) */
      register_P = 0x5; /* set page register */
      /* opSTAdir_A_A (d2) */
      ram[register_I = 0x52] = register_A; /* store acc to RAM */
      /* opASRe_A_AA (ed) */
      cmp_new = 0xDED; cmp_old = flag_C = acc_a0 = register_A;
      register_A = SEX(register_A); /* make signed */
      register_A = (((signed short int)(((signed short int)register_A) >> (signed short int)1)) /* SAR */) & 0xFFF;
      /* opASRe_A_AA (ed) */
      cmp_new = 0xDED; cmp_old = flag_C = acc_a0 = register_A;
      register_A = SEX(register_A); /* make signed */
      register_A = (((signed short int)(((signed short int)register_A) >> (signed short int)1)) /* SAR */) & 0xFFF;
      /* opASRe_A_AA (ed) */
      cmp_new = 0xDED; cmp_old = flag_C = acc_a0 = register_A;
      register_A = SEX(register_A); /* make signed */
      register_A = (((signed short int)(((signed short int)register_A) >> (signed short int)1)) /* SAR */) & 0xFFF;
      /* opASRe_A_AA (ed) */
      cmp_new = 0xDED; cmp_old = flag_C = acc_a0 = register_A;
      register_A = SEX(register_A); /* make signed */
      register_A = (((signed short int)(((signed short int)register_A) >> (signed short int)1)) /* SAR */) & 0xFFF;
      /* opASRe_A_AA (ed) */
      cmp_new = 0xDED; cmp_old = flag_C = acc_a0 = register_A;
      register_A = SEX(register_A); /* make signed */
      register_A = (((signed short int)(((signed short int)register_A) >> (signed short int)1)) /* SAR */) & 0xFFF;
      /* opASRe_A_AA (ed) */
      cmp_new = 0xDED; cmp_old = flag_C = acc_a0 = register_A;
      register_A = SEX(register_A); /* make signed */
      register_A = (((signed short int)(((signed short int)register_A) >> (signed short int)1)) /* SAR */) & 0xFFF;
      /* opASRe_A_AA (ed) */
      cmp_new = 0xDED; cmp_old = flag_C = acc_a0 = register_A;
      register_A = SEX(register_A); /* make signed */
      register_A = (((signed short int)(((signed short int)register_A) >> (signed short int)1)) /* SAR */) & 0xFFF;
      /* opSTAdir_A_A (d5) */
      ram[register_I = 0x55] = register_A; /* store acc to RAM */
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opSUBirg_A_AA (e8) */
      register_A = (flag_C = ((cmp_old = acc_a0 = register_A) + ((cmp_new = ram[register_I]) ^ 0xFFF) + 1)) & 0xFFF; /* ones compliment */
      /* opSTAirg_A_A (e6) */
      ram[0x55] = register_A; /* store acc */
      /* opLDPimm_A_A (84) */
      register_P = 0x4; /* set page register */
      /* opSTAdir_A_A (d5) */
      ram[register_I = 0x45] = register_A; /* store acc to RAM */
      /* opLDPimm_A_A (83) */
      register_P = 0x3; /* set page register */
      /* opSTAdir_A_A (d5) */
      ram[register_I = 0x35] = register_A; /* store acc to RAM */
      /* opLDAimm_A_AA (08) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0x0800;
      /* opLDPimm_A_A (82) */
      register_P = 0x2; /* set page register */
      /* opCMPdir_A_AA (b3) */
      cmp_old = acc_a0 = register_A; /* backup old acc */
      flag_C = (((cmp_new = ram[register_I = 0x23]) ^ 0xFFF) + 1 + register_A);
      /* opLDJimm_A_A (48) */
      register_J = 0x0e88;
      /* opJLT_A_A (5b) */
      if (cmp_new < cmp_old) {
        register_PC = 0x1e88;
        break;
      };
      /* opLDAimm_A_AA (09) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0x0900;
      /* opLDPimm_A_A (83) */
      register_P = 0x3; /* set page register */
      /* opSTAdir_A_A (d1) */
      ram[register_I = 0x31] = 0x900; /* store acc to RAM */
      /* opADDimmX_A_AA (20) */
      cmp_old = register_A; cmp_new = 0xff;
      register_A = flag_C = acc_a0 = 0x09ff;
      /* opLDPimm_A_A (84) */
      register_P = 0x4; /* set page register */
      /* opSTAdir_A_A (d1) */
      ram[register_I = 0x41] = 0x9ff; /* store acc to RAM */
      /* opLDPimm_A_A (85) */
      register_P = 0x5; /* set page register */
      /* opSTAdir_A_A (d1) */
      ram[register_I = 0x51] = 0x9ff; /* store acc to RAM */
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opADDimmX_A_AA (20) */
      cmp_old = register_A; cmp_new = 0x20;
      register_A = flag_C = acc_a0 = 0x0020;
      /* opLDJimm_A_A (44) */
      register_J = 0x0e94;
      /* opJMP_A_A (58) */
      {
        register_PC = 0x1e94;
        break;
      };

    case 0x1e5b:

      /* opLDPimm_A_A (83) */
      register_P = 0x3; /* set page register */
      /* opSTAdir_A_A (d1) */
      ram[register_I = 0x31] = register_A; /* store acc to RAM */
      /* opSUBimmX_A_AA (30) */
      cmp_old = acc_a0 = register_A; /* back up regA */
      register_A = (flag_C = (register_A + (((cmp_new = 0x7f) ^ 0xFFF) + 1))) & 0xFFF; /* add */
      /* opLDPimm_A_A (84) */
      register_P = 0x4; /* set page register */
      /* opSTAdir_A_A (d1) */
      ram[register_I = 0x41] = register_A; /* store acc to RAM */
      /* opADDimmX_A_AA (20) */
      register_A = (flag_C = ((acc_a0 = cmp_old = register_A) + (cmp_new = 0x00fe))) & 0xFFF; /* add values */
      /* opLDPimm_A_A (85) */
      register_P = 0x5; /* set page register */
      /* opSTAdir_A_A (d1) */
      ram[register_I = 0x51] = register_A; /* store acc to RAM */
      /* opASRe_A_AA (ed) */
      cmp_new = 0xDED; cmp_old = flag_C = acc_a0 = register_A;
      register_A = SEX(register_A); /* make signed */
      register_A = (((signed short int)(((signed short int)register_A) >> (signed short int)1)) /* SAR */) & 0xFFF;
      /* opASRe_A_AA (ed) */
      cmp_new = 0xDED; cmp_old = flag_C = acc_a0 = register_A;
      register_A = SEX(register_A); /* make signed */
      register_A = (((signed short int)(((signed short int)register_A) >> (signed short int)1)) /* SAR */) & 0xFFF;
      /* opASRe_A_AA (ed) */
      cmp_new = 0xDED; cmp_old = flag_C = acc_a0 = register_A;
      register_A = SEX(register_A); /* make signed */
      register_A = (((signed short int)(((signed short int)register_A) >> (signed short int)1)) /* SAR */) & 0xFFF;
      /* opASRe_A_AA (ed) */
      cmp_new = 0xDED; cmp_old = flag_C = acc_a0 = register_A;
      register_A = SEX(register_A); /* make signed */
      register_A = (((signed short int)(((signed short int)register_A) >> (signed short int)1)) /* SAR */) & 0xFFF;
      /* opASRe_A_AA (ed) */
      cmp_new = 0xDED; cmp_old = flag_C = acc_a0 = register_A;
      register_A = SEX(register_A); /* make signed */
      register_A = (((signed short int)(((signed short int)register_A) >> (signed short int)1)) /* SAR */) & 0xFFF;
      /* opASRe_A_AA (ed) */
      cmp_new = 0xDED; cmp_old = flag_C = acc_a0 = register_A;
      register_A = SEX(register_A); /* make signed */
      register_A = (((signed short int)(((signed short int)register_A) >> (signed short int)1)) /* SAR */) & 0xFFF;
      /* opASRe_A_AA (ed) */
      cmp_new = 0xDED; cmp_old = flag_C = acc_a0 = register_A;
      register_A = SEX(register_A); /* make signed */
      register_A = (((signed short int)(((signed short int)register_A) >> (signed short int)1)) /* SAR */) & 0xFFF;
      /* opSTAdir_A_A (d4) */
      ram[register_I = 0x54] = register_A; /* store acc to RAM */
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opSUBirg_A_AA (e8) */
      register_A = (flag_C = ((cmp_old = acc_a0 = register_A) + ((cmp_new = ram[register_I]) ^ 0xFFF) + 1)) & 0xFFF; /* ones compliment */
      /* opSTAirg_A_A (e6) */
      ram[0x54] = register_A; /* store acc */
      /* opLDPimm_A_A (84) */
      register_P = 0x4; /* set page register */
      /* opSTAdir_A_A (d4) */
      ram[register_I = 0x44] = register_A; /* store acc to RAM */
      /* opLDPimm_A_A (83) */
      register_P = 0x3; /* set page register */
      /* opSTAdir_A_A (d4) */
      ram[register_I = 0x34] = register_A; /* store acc to RAM */
      /* opLDAimm_A_AA (07) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0x0700;
      /* opLDPimm_A_A (84) */
      register_P = 0x4; /* set page register */
      /* opSTAdir_A_A (d2) */
      ram[register_I = 0x42] = 0x700; /* store acc to RAM */
      /* opLDPimm_A_A (85) */
      register_P = 0x5; /* set page register */
      /* opSTAdir_A_A (d2) */
      ram[register_I = 0x52] = 0x700; /* store acc to RAM */
      /* opSUBimmX_A_AA (30) */
      cmp_old = acc_a0 = register_A; /* back up regA */
      register_A = (flag_C = (register_A + (((cmp_new = 0xff) ^ 0xFFF) + 1))) & 0xFFF; /* add */
      /* opLDPimm_A_A (83) */
      register_P = 0x3; /* set page register */
      /* opSTAdir_A_A (d2) */
      ram[register_I = 0x32] = register_A; /* store acc to RAM */
      /* opLDAimm_A_AA (0f) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0x0f00;
      /* opADDimmX_A_AA (20) */
      cmp_old = register_A; cmp_new = 0xe0;
      register_A = flag_C = acc_a0 = 0x0fe0;
      /* opSTAdir_A_A (d5) */
      ram[register_I = 0x35] = 0xfe0; /* store acc to RAM */
      /* opLDPimm_A_A (85) */
      register_P = 0x5; /* set page register */
      /* opSTAdir_A_A (d5) */
      ram[register_I = 0x55] = 0xfe0; /* store acc to RAM */
      /* opLDPimm_A_A (84) */
      register_P = 0x4; /* set page register */
      /* opSTAdir_A_A (d5) */
      ram[register_I = 0x45] = 0xfe0; /* store acc to RAM */
      /* opLDJimm_A_A (4a) */
      register_J = 0x0e9a;
      /* opJMP_A_A (58) */
      {
        register_PC = 0x1e9a;
        break;
      };

    case 0x1e88:

      /* opLDAimm_A_AA (07) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0x0700;
      /* opLDPimm_A_A (83) */
      register_P = 0x3; /* set page register */
      /* opSTAdir_A_A (d1) */
      ram[register_I = 0x31] = 0x700; /* store acc to RAM */
      /* opSUBimmX_A_AA (30) */
      cmp_old = acc_a0 = register_A; /* back up regA */
      register_A = (flag_C = (register_A + (((cmp_new = 0xff) ^ 0xFFF) + 1))) & 0xFFF; /* add */
      /* opLDPimm_A_A (84) */
      register_P = 0x4; /* set page register */
      /* opSTAdir_A_A (d1) */
      ram[register_I = 0x41] = register_A; /* store acc to RAM */
      /* opLDPimm_A_A (85) */
      register_P = 0x5; /* set page register */
      /* opSTAdir_A_A (d1) */
      ram[register_I = 0x51] = register_A; /* store acc to RAM */
      /* opLDAimm_A_AA (0f) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0x0f00;
      /* opADDimmX_A_AA (20) */
      cmp_old = register_A; cmp_new = 0xe0;
      register_A = flag_C = acc_a0 = 0x0fe0;

    case 0x1e94:

      /* opLDPimm_A_A (83) */
      register_P = 0x3; /* set page register */
      /* opSTAdir_A_A (d4) */
      ram[register_I = 0x34] = register_A; /* store acc to RAM */
      /* opLDPimm_A_A (84) */
      register_P = 0x4; /* set page register */
      /* opSTAdir_A_A (d4) */
      ram[register_I = 0x44] = register_A; /* store acc to RAM */
      /* opLDPimm_A_A (85) */
      register_P = 0x5; /* set page register */
      /* opSTAdir_A_A (d4) */
      ram[register_I = 0x54] = register_A; /* store acc to RAM */

    case 0x1e9a:

      /* opLDJimm_A_A (48) */
      register_J = 0x0ef8;
      /* opJMP_A_A (58) */
      {
        register_PC = 0x1ef8;
        break;
      };

    case 0x1e9d:

      /* Invariants: register_P = 0x2 register_I = 0x22 */;
      /* opLDPimm_A_A (82) */
      /* opLDAdir_A_AA (a3) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = 0x23]; /* set I register */

      /* opASRe_A_AA (ed) */
      cmp_new = 0xDED; cmp_old = flag_C = acc_a0 = register_A;
      register_A = SEX(register_A); /* make signed */
      register_A = (((signed short int)(((signed short int)register_A) >> (signed short int)1)) /* SAR */) & 0xFFF;
      /* opLDPimm_A_A (84) */
      register_P = 0x4; /* set page register */
      /* opSTAdir_A_A (d2) */
      ram[register_I = 0x42] = register_A; /* store acc to RAM */
      /* opASRe_A_AA (ed) */
      cmp_new = 0xDED; cmp_old = flag_C = acc_a0 = register_A;
      register_A = SEX(register_A); /* make signed */
      register_A = (((signed short int)(((signed short int)register_A) >> (signed short int)1)) /* SAR */) & 0xFFF;
      /* opAWDirg_A_AA (e7) */
      cmp_old = acc_a0 = register_A;
      register_A = (flag_C = (register_A + (cmp_new = ram[0x42]))) & 0xFFF;
      set_watchdog();
      /* opSTAirg_A_A (e6) */
      ram[0x42] = register_A; /* store acc */
      /* opASRe_A_AA (ed) */
      cmp_new = 0xDED; cmp_old = flag_C = acc_a0 = register_A;
      register_A = SEX(register_A); /* make signed */
      register_A = (((signed short int)(((signed short int)register_A) >> (signed short int)1)) /* SAR */) & 0xFFF;
      /* opASRe_A_AA (ed) */
      cmp_new = 0xDED; cmp_old = flag_C = acc_a0 = register_A;
      register_A = SEX(register_A); /* make signed */
      register_A = (((signed short int)(((signed short int)register_A) >> (signed short int)1)) /* SAR */) & 0xFFF;
      /* opASRe_A_AA (ed) */
      cmp_new = 0xDED; cmp_old = flag_C = acc_a0 = register_A;
      register_A = SEX(register_A); /* make signed */
      register_A = (((signed short int)(((signed short int)register_A) >> (signed short int)1)) /* SAR */) & 0xFFF;
      /* opASRe_A_AA (ed) */
      cmp_new = 0xDED; cmp_old = flag_C = acc_a0 = register_A;
      register_A = SEX(register_A); /* make signed */
      register_A = (((signed short int)(((signed short int)register_A) >> (signed short int)1)) /* SAR */) & 0xFFF;
      /* opASRe_A_AA (ed) */
      cmp_new = 0xDED; cmp_old = flag_C = acc_a0 = register_A;
      register_A = SEX(register_A); /* make signed */
      register_A = (((signed short int)(((signed short int)register_A) >> (signed short int)1)) /* SAR */) & 0xFFF;
      /* opASRe_A_AA (ed) */
      cmp_new = 0xDED; cmp_old = flag_C = acc_a0 = register_A;
      register_A = SEX(register_A); /* make signed */
      register_A = (((signed short int)(((signed short int)register_A) >> (signed short int)1)) /* SAR */) & 0xFFF;
      /* opASRe_A_AA (ed) */
      cmp_new = 0xDED; cmp_old = flag_C = acc_a0 = register_A;
      register_A = SEX(register_A); /* make signed */
      register_A = (((signed short int)(((signed short int)register_A) >> (signed short int)1)) /* SAR */) & 0xFFF;
      /* opSTAdir_A_A (d5) */
      ram[register_I = 0x45] = register_A; /* store acc to RAM */
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opSUBirg_A_AA (e8) */
      register_A = (flag_C = ((cmp_old = acc_a0 = register_A) + ((cmp_new = ram[register_I]) ^ 0xFFF) + 1)) & 0xFFF; /* ones compliment */
      /* opSTAirg_A_A (e6) */
      ram[0x45] = register_A; /* store acc */
      /* opLDAimm_A_AA (09) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0x0900;
      /* opSTAdir_A_A (d1) */
      ram[register_I = 0x41] = 0x900; /* store acc to RAM */
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opADDimmX_A_AA (20) */
      cmp_old = register_A; cmp_new = 0x18;
      register_A = flag_C = acc_a0 = 0x0018;
      /* opSTAdir_A_A (d4) */
      ram[register_I = 0x44] = 0x018; /* store acc to RAM */
      /* opLDAimm_A_AA (07) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0x0700;
      /* opLDPimm_A_A (83) */
      register_P = 0x3; /* set page register */
      /* opSTAdir_A_A (d2) */
      ram[register_I = 0x32] = 0x700; /* store acc to RAM */
      /* opLDPimm_A_A (85) */
      register_P = 0x5; /* set page register */
      /* opSTAdir_A_A (d1) */
      ram[register_I = 0x51] = 0x700; /* store acc to RAM */
      /* opLDAimm_A_AA (0f) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0x0f00;
      /* opADDimmX_A_AA (20) */
      cmp_old = register_A; cmp_new = 0xe8;
      register_A = flag_C = acc_a0 = 0x0fe8;
      /* opSTAdir_A_A (d4) */
      ram[register_I = 0x54] = 0xfe8; /* store acc to RAM */
      /* opLDPimm_A_A (83) */
      register_P = 0x3; /* set page register */
      /* opSTAdir_A_A (d5) */
      ram[register_I = 0x35] = 0xfe8; /* store acc to RAM */
      /* opLDPimm_A_A (82) */
      register_P = 0x2; /* set page register */
      /* opLDAdir_A_AA (a2) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = 0x22]; /* set I register */

      /* opLSLe_A_AA (ec) */
      cmp_new = 0x0CEC;
      cmp_old = acc_a0 = register_A;
      flag_C = (0x0CEC + register_A);
      register_A = (register_A << 1) & 0x0FFF;
      /* opLSLe_A_AA (ec) */
      cmp_new = 0x0CEC;
      cmp_old = acc_a0 = register_A;
      flag_C = (0x0CEC + register_A);
      register_A = (register_A << 1) & 0x0FFF;
      /* opASRe_A_AA (ed) */
      cmp_new = 0xDED; cmp_old = flag_C = acc_a0 = register_A;
      register_A = SEX(register_A); /* make signed */
      register_A = (((signed short int)(((signed short int)register_A) >> (signed short int)1)) /* SAR */) & 0xFFF;
      /* opLDPimm_A_A (83) */
      register_P = 0x3; /* set page register */
      /* opSTAdir_A_A (d1) */
      ram[register_I = 0x31] = register_A; /* store acc to RAM */
      /* opASRe_A_AA (ed) */
      cmp_new = 0xDED; cmp_old = flag_C = acc_a0 = register_A;
      register_A = SEX(register_A); /* make signed */
      register_A = (((signed short int)(((signed short int)register_A) >> (signed short int)1)) /* SAR */) & 0xFFF;
      /* opASRe_A_AA (ed) */
      cmp_new = 0xDED; cmp_old = flag_C = acc_a0 = register_A;
      register_A = SEX(register_A); /* make signed */
      register_A = (((signed short int)(((signed short int)register_A) >> (signed short int)1)) /* SAR */) & 0xFFF;
      /* opASRe_A_AA (ed) */
      cmp_new = 0xDED; cmp_old = flag_C = acc_a0 = register_A;
      register_A = SEX(register_A); /* make signed */
      register_A = (((signed short int)(((signed short int)register_A) >> (signed short int)1)) /* SAR */) & 0xFFF;
      /* opASRe_A_AA (ed) */
      cmp_new = 0xDED; cmp_old = flag_C = acc_a0 = register_A;
      register_A = SEX(register_A); /* make signed */
      register_A = (((signed short int)(((signed short int)register_A) >> (signed short int)1)) /* SAR */) & 0xFFF;
      /* opASRe_A_AA (ed) */
      cmp_new = 0xDED; cmp_old = flag_C = acc_a0 = register_A;
      register_A = SEX(register_A); /* make signed */
      register_A = (((signed short int)(((signed short int)register_A) >> (signed short int)1)) /* SAR */) & 0xFFF;
      /* opASRe_A_AA (ed) */
      cmp_new = 0xDED; cmp_old = flag_C = acc_a0 = register_A;
      register_A = SEX(register_A); /* make signed */
      register_A = (((signed short int)(((signed short int)register_A) >> (signed short int)1)) /* SAR */) & 0xFFF;
      /* opASRe_A_AA (ed) */
      cmp_new = 0xDED; cmp_old = flag_C = acc_a0 = register_A;
      register_A = SEX(register_A); /* make signed */
      register_A = (((signed short int)(((signed short int)register_A) >> (signed short int)1)) /* SAR */) & 0xFFF;
      /* opSTAdir_A_A (d4) */
      ram[register_I = 0x34] = register_A; /* store acc to RAM */
      /* opLDPimm_A_A (82) */
      register_P = 0x2; /* set page register */
      /* opNOP_A_B (57) */
      /* opLDAdir_B_AA (a3) */
      flag_C = acc_a0 = register_A;
      cmp_old = register_B; /* store old acc */
      register_B = cmp_new = ram[register_I = (register_P << 4) + 0x03]; /* new acc value */
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opASRDe_A_AA (ee) */
      cmp_new = 0x0EEE; cmp_old = acc_a0 = register_A; flag_C = (0x0EEE + register_A);
      register_A = (register_A >> 1) | ((register_B & 1) << 11);
      register_B = (register_B >> 1) | (register_B & 0x800);
      /* opASRDe_A_AA (ee) */
      cmp_new = 0x0EEE; cmp_old = acc_a0 = register_A; flag_C = (0x0EEE + register_A);
      register_A = (register_A >> 1) | ((register_B & 1) << 11);
      register_B = (register_B >> 1) | (register_B & 0x800);
      /* opASRDe_A_AA (ee) */
      cmp_new = 0x0EEE; cmp_old = acc_a0 = register_A; flag_C = (0x0EEE + register_A);
      register_A = (register_A >> 1) | ((register_B & 1) << 11);
      register_B = (register_B >> 1) | (register_B & 0x800);
      /* opADDdir_A_AA (62) */
      register_A = (flag_C = ((cmp_old = acc_a0 = register_A) + (cmp_new = ram[register_I = 0x22]))) & 0xFFF; /* do acc operation */
      /* opNOP_A_B (57) */
      /* opLDAirg_B_AA (ea) */
      flag_C = acc_a0 = register_A;
      cmp_old = register_B;
      register_B = cmp_new = ram[register_I];

      /* opSTAirg_A_A (e6) */
      ram[0x22] = register_A; /* store acc */
      /* opLDAimm_A_AA (02) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0x0200;
      /* opANDirg_A_AA (e9) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A &= (cmp_new = ram[register_I]);
      /* opLSLe_A_AA (ec) */
      cmp_new = 0x0CEC;
      cmp_old = acc_a0 = register_A;
      flag_C = (0x0CEC + register_A);
      register_A = (register_A << 1) & 0x0FFF;
      /* opSTAirg_A_A (e6) */
      ram[0x22] = register_A; /* store acc */
      /* opNOP_A_B (57) */
      /* opAWDirg_B_AA (e7) */
      acc_a0 = register_A;
      cmp_old = register_B;
      register_B = (flag_C = (register_B + (cmp_new = ram[register_I]))) & 0xFFF;
      set_watchdog();
      /* opLDAdir_A_AA (a3) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = 0x23]; /* set I register */

      /* opASRDe_A_AA (ee) */
      cmp_new = 0x0EEE; cmp_old = acc_a0 = register_A; flag_C = (0x0EEE + register_A);
      register_A = (register_A >> 1) | ((register_B & 1) << 11);
      register_B = (register_B >> 1) | (register_B & 0x800);
      /* opSTAirg_A_A (e6) */
      ram[0x23] = register_A; /* store acc */
      /* opNOP_A_B (57) */
      /* opSTAdir_B_BB (d2) */
      ram[register_I = (register_P << 4) + 0x2] = register_B; /* set I register and store B to ram */
      /* opLDAdir_A_AA (a3) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = 0x23]; /* set I register */

      /* opASRe_A_AA (ed) */
      cmp_new = 0xDED; cmp_old = flag_C = acc_a0 = register_A;
      register_A = SEX(register_A); /* make signed */
      register_A = (((signed short int)(((signed short int)register_A) >> (signed short int)1)) /* SAR */) & 0xFFF;
      /* opLDPimm_A_A (85) */
      register_P = 0x5; /* set page register */
      /* opSTAdir_A_A (d2) */
      ram[register_I = 0x52] = register_A; /* store acc to RAM */
      /* opASRe_A_AA (ed) */
      cmp_new = 0xDED; cmp_old = flag_C = acc_a0 = register_A;
      register_A = SEX(register_A); /* make signed */
      register_A = (((signed short int)(((signed short int)register_A) >> (signed short int)1)) /* SAR */) & 0xFFF;
      /* opAWDirg_A_AA (e7) */
      cmp_old = acc_a0 = register_A;
      register_A = (flag_C = (register_A + (cmp_new = ram[0x52]))) & 0xFFF;
      set_watchdog();
      /* opSTAirg_A_A (e6) */
      ram[0x52] = register_A; /* store acc */
      /* opASRe_A_AA (ed) */
      cmp_new = 0xDED; cmp_old = flag_C = acc_a0 = register_A;
      register_A = SEX(register_A); /* make signed */
      register_A = (((signed short int)(((signed short int)register_A) >> (signed short int)1)) /* SAR */) & 0xFFF;
      /* opASRe_A_AA (ed) */
      cmp_new = 0xDED; cmp_old = flag_C = acc_a0 = register_A;
      register_A = SEX(register_A); /* make signed */
      register_A = (((signed short int)(((signed short int)register_A) >> (signed short int)1)) /* SAR */) & 0xFFF;
      /* opASRe_A_AA (ed) */
      cmp_new = 0xDED; cmp_old = flag_C = acc_a0 = register_A;
      register_A = SEX(register_A); /* make signed */
      register_A = (((signed short int)(((signed short int)register_A) >> (signed short int)1)) /* SAR */) & 0xFFF;
      /* opASRe_A_AA (ed) */
      cmp_new = 0xDED; cmp_old = flag_C = acc_a0 = register_A;
      register_A = SEX(register_A); /* make signed */
      register_A = (((signed short int)(((signed short int)register_A) >> (signed short int)1)) /* SAR */) & 0xFFF;
      /* opASRe_A_AA (ed) */
      cmp_new = 0xDED; cmp_old = flag_C = acc_a0 = register_A;
      register_A = SEX(register_A); /* make signed */
      register_A = (((signed short int)(((signed short int)register_A) >> (signed short int)1)) /* SAR */) & 0xFFF;
      /* opASRe_A_AA (ed) */
      cmp_new = 0xDED; cmp_old = flag_C = acc_a0 = register_A;
      register_A = SEX(register_A); /* make signed */
      register_A = (((signed short int)(((signed short int)register_A) >> (signed short int)1)) /* SAR */) & 0xFFF;
      /* opASRe_A_AA (ed) */
      cmp_new = 0xDED; cmp_old = flag_C = acc_a0 = register_A;
      register_A = SEX(register_A); /* make signed */
      register_A = (((signed short int)(((signed short int)register_A) >> (signed short int)1)) /* SAR */) & 0xFFF;
      /* opSTAdir_A_A (d5) */
      ram[register_I = 0x55] = register_A; /* store acc to RAM */
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opSUBirg_A_AA (e8) */
      register_A = (flag_C = ((cmp_old = acc_a0 = register_A) + ((cmp_new = ram[register_I]) ^ 0xFFF) + 1)) & 0xFFF; /* ones compliment */
      /* opSTAirg_A_A (e6) */
      ram[0x55] = register_A; /* store acc */

    case 0x1ef8:

      /* Invariants: register_P = 0x5 register_I = 0x55 */;
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opADDimmX_A_AA (20) */
      cmp_old = register_A; cmp_new = 0x20;
      register_A = flag_C = acc_a0 = 0x0020;
      /* opLDPimm_A_A (82) */
      register_P = 0x2; /* set page register */
      /* opSTAdir_A_A (de) */
      ram[register_I = 0x2e] = 0x020; /* store acc to RAM */
      /* opLDPimm_A_A (86) */
      register_P = 0x6; /* set page register */
      /* opCMPdir_A_AA (ba) */
      cmp_old = acc_a0 = register_A; /* backup old acc */
      flag_C = (((cmp_new = ram[register_I = 0x6a]) ^ 0xFFF) + 1 + register_A);
      /* opLDJimm_A_A (48) */
      register_J = 0x0f48;
      /* opJLT_A_A (5b) */
      if (cmp_new < cmp_old) {
        register_PC = 0x1f48;
        break;
      };
      /* opLDAimm_A_AA (02) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0x0200;
      /* opCMPdir_A_AA (ba) */
      cmp_old = acc_a0 = register_A; /* backup old acc */
      flag_C = (((cmp_new = ram[register_I = 0x6a]) ^ 0xFFF) + 1 + register_A);
      /* opLDJimm_A_A (4e) */
      register_J = 0x0f3e;
      /* opJLT_A_A (5b) */
      if (cmp_new < cmp_old) {
        register_PC = 0x1f3e;
        break;
      };
      /* opLSLe_A_AA (ec) */
      cmp_new = 0x0CEC;
      cmp_old = acc_a0 = register_A;
      flag_C = (0x0CEC + register_A);
      register_A = (register_A << 1) & 0x0FFF;
      /* opCMPdir_A_AA (ba) */
      cmp_old = acc_a0 = register_A; /* backup old acc */
      flag_C = (((cmp_new = ram[register_I = 0x6a]) ^ 0xFFF) + 1 + register_A);
      /* opLDJimm_A_A (40) */
      register_J = 0x0f30;
      /* opJLT_A_A (5b) */
      if (cmp_new < cmp_old) {
        register_PC = 0x1f30;
        break;
      };
      /* opLDAimm_A_AA (06) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0x0600;
      /* opCMPdir_A_AA (ba) */
      cmp_old = acc_a0 = register_A; /* backup old acc */
      flag_C = (((cmp_new = ram[register_I = 0x6a]) ^ 0xFFF) + 1 + register_A);
      /* opLDJimm_A_A (41) */
      register_J = 0x0f21;
      /* opJLT_A_A (5b) */
      if (cmp_new < cmp_old) {
        register_PC = 0x1f21;
        break;
      };
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opADDimmX_A_AA (20) */
      cmp_old = register_A; cmp_new = 0x3c;
      register_A = flag_C = acc_a0 = 0x003c;
      /* opLDPimm_A_A (82) */
      register_P = 0x2; /* set page register */
      /* opSTAdir_A_A (de) */
      ram[register_I = 0x2e] = 0x03c; /* store acc to RAM */
      /* opLDPimm_A_A (86) */
      register_P = 0x6; /* set page register */
      /* opLDAdir_A_AA (aa) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = 0x6a]; /* set I register */

      /* opLSRe_A_AA (eb) */
      cmp_new = 0x0BEB; cmp_old = acc_a0 = register_A; flag_C = (0x0BEB + register_A);
      register_A >>= 1;
      /* opLSRe_A_AA (eb) */
      cmp_new = 0x0BEB; cmp_old = acc_a0 = register_A; flag_C = (0x0BEB + register_A);
      register_A >>= 1;
      /* opLSRe_A_AA (eb) */
      cmp_new = 0x0BEB; cmp_old = acc_a0 = register_A; flag_C = (0x0BEB + register_A);
      register_A >>= 1;
      /* opLSRe_A_AA (eb) */
      cmp_new = 0x0BEB; cmp_old = acc_a0 = register_A; flag_C = (0x0BEB + register_A);
      register_A >>= 1;
      /* opSUBimmX_A_AA (30) */
      cmp_old = acc_a0 = register_A; /* back up regA */
      register_A = (flag_C = (register_A + (((cmp_new = 0x60) ^ 0xFFF) + 1))) & 0xFFF; /* add */
      /* opLDJimm_A_A (41) */
      register_J = 0x0f41;
      /* opJMP_A_A (58) */
      {
        register_PC = 0x1f41;
        break;
      };

    case 0x1f21:

      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opADDimmX_A_AA (20) */
      cmp_old = register_A; cmp_new = 0x38;
      register_A = flag_C = acc_a0 = 0x0038;
      /* opLDPimm_A_A (82) */
      register_P = 0x2; /* set page register */
      /* opSTAdir_A_A (de) */
      ram[register_I = 0x2e] = 0x038; /* store acc to RAM */
      /* opLDPimm_A_A (86) */
      register_P = 0x6; /* set page register */
      /* opLDAdir_A_AA (aa) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = 0x6a]; /* set I register */

      /* opLSRe_A_AA (eb) */
      cmp_new = 0x0BEB; cmp_old = acc_a0 = register_A; flag_C = (0x0BEB + register_A);
      register_A >>= 1;
      /* opLSRe_A_AA (eb) */
      cmp_new = 0x0BEB; cmp_old = acc_a0 = register_A; flag_C = (0x0BEB + register_A);
      register_A >>= 1;
      /* opLSRe_A_AA (eb) */
      cmp_new = 0x0BEB; cmp_old = acc_a0 = register_A; flag_C = (0x0BEB + register_A);
      register_A >>= 1;
      /* opSUBimmX_A_AA (30) */
      cmp_old = acc_a0 = register_A; /* back up regA */
      register_A = (flag_C = (register_A + (((cmp_new = 0x80) ^ 0xFFF) + 1))) & 0xFFF; /* add */
      /* opLDJimm_A_A (41) */
      register_J = 0x0f41;
      /* opJMP_A_A (58) */
      {
        register_PC = 0x1f41;
        break;
      };

    case 0x1f30:

      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opADDimmX_A_AA (20) */
      cmp_old = register_A; cmp_new = 0x30;
      register_A = flag_C = acc_a0 = 0x0030;
      /* opLDPimm_A_A (82) */
      register_P = 0x2; /* set page register */
      /* opSTAdir_A_A (de) */
      ram[register_I = 0x2e] = 0x030; /* store acc to RAM */
      /* opLDPimm_A_A (86) */
      register_P = 0x6; /* set page register */
      /* opLDAdir_A_AA (aa) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = 0x6a]; /* set I register */

      /* opLSRe_A_AA (eb) */
      cmp_new = 0x0BEB; cmp_old = acc_a0 = register_A; flag_C = (0x0BEB + register_A);
      register_A >>= 1;
      /* opLSRe_A_AA (eb) */
      cmp_new = 0x0BEB; cmp_old = acc_a0 = register_A; flag_C = (0x0BEB + register_A);
      register_A >>= 1;
      /* opSUBimmX_A_AA (30) */
      cmp_old = acc_a0 = register_A; /* back up regA */
      register_A = (flag_C = (register_A + (((cmp_new = 0x80) ^ 0xFFF) + 1))) & 0xFFF; /* add */
      /* opLDJimm_A_A (41) */
      register_J = 0x0f41;
      /* opJMP_A_A (58) */
      {
        register_PC = 0x1f41;
        break;
      };

    case 0x1f3e:

      /* opLDPimm_A_A (86) */
      register_P = 0x6; /* set page register */
      /* opLDAdir_A_AA (aa) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = 0x6a]; /* set I register */

      /* opLSRe_A_AA (eb) */
      cmp_new = 0x0BEB; cmp_old = acc_a0 = register_A; flag_C = (0x0BEB + register_A);
      register_A >>= 1;

    case 0x1f41:

      /* opLSRe_A_AA (eb) */
      cmp_new = 0x0BEB; cmp_old = acc_a0 = register_A; flag_C = (0x0BEB + register_A);
      register_A >>= 1;
      /* opLSRe_A_AA (eb) */
      cmp_new = 0x0BEB; cmp_old = acc_a0 = register_A; flag_C = (0x0BEB + register_A);
      register_A >>= 1;
      /* opLSRe_A_AA (eb) */
      cmp_new = 0x0BEB; cmp_old = acc_a0 = register_A; flag_C = (0x0BEB + register_A);
      register_A >>= 1;
      /* opLSRe_A_AA (eb) */
      cmp_new = 0x0BEB; cmp_old = acc_a0 = register_A; flag_C = (0x0BEB + register_A);
      register_A >>= 1;
      /* opLDPimm_A_A (82) */
      register_P = 0x2; /* set page register */
      /* opADDdir_A_AA (6e) */
      register_A = (flag_C = ((cmp_old = acc_a0 = register_A) + (cmp_new = ram[register_I = 0x2e]))) & 0xFFF; /* do acc operation */
      /* opSTAirg_A_A (e6) */
      ram[0x2e] = register_A; /* store acc */

    case 0x1f48:

      /* Invariants: register_P = 0x6 register_I = 0x6a register_A = 0x20 */;
      /* opLDPimm_A_A (82) */
      register_P = 0x2; /* set page register */
      /* opLDAdir_A_AA (a3) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = 0x23]; /* set I register */

      /* opLSRe_A_AA (eb) */
      cmp_new = 0x0BEB; cmp_old = acc_a0 = register_A; flag_C = (0x0BEB + register_A);
      register_A >>= 1;
      /* opLDJimm_A_A (4c) */
      register_J = 0x0f6c;
      /* opJA0_A_A (5e) */
      if (acc_a0 & 0x01) {
        register_PC = 0x1f6c;
        break;
      };

      /* opLSRe_A_AA (eb) */
      cmp_new = 0x0BEB; cmp_old = acc_a0 = register_A; flag_C = (0x0BEB + register_A);
      register_A >>= 1;
      /* opLDJimm_A_A (4f) */
      register_J = 0x0f5f;
      /* opJA0_A_A (5e) */
      if (acc_a0 & 0x01) {
        register_PC = 0x1f5f;
        break;
      };

      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opADDimmX_A_AA (20) */
      cmp_old = register_A; cmp_new = 0x23;
      register_A = flag_C = acc_a0 = 0x0023;
      /* opSTAdir_A_A (d4) */
      ram[register_I = 0x24] = 0x023; /* store acc to RAM */
      /* opLDAdir_A_AA (ae) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = 0x2e]; /* set I register */

      /* opADDimm_A_AA (2c) */
      register_A = (flag_C = ((cmp_old = acc_a0 = register_A) + (cmp_new = 0xc))) & 0xFFF; /* add values, save carry */

      /* opSTAirg_A_A (e6) */
      ram[0x2e] = register_A; /* store acc */
      /* opLDAimm_A_AA (05) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0x0500;
      /* opADDimmX_A_AA (20) */
      cmp_old = register_A; cmp_new = 0x39;
      register_A = flag_C = acc_a0 = 0x0539;
      /* opLDJimm_A_A (43) */
      register_J = 0x0f83;
      /* opJMP_A_A (58) */
      {
        register_PC = 0x1f83;
        break;
      };

    case 0x1f5f:

      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opADDimmX_A_AA (20) */
      cmp_old = register_A; cmp_new = 0x17;
      register_A = flag_C = acc_a0 = 0x0017;
      /* opSTAdir_A_A (d4) */
      ram[register_I = (register_P << 4) + 0x4] = 0x017; /* store acc to RAM */
      /* opLDAdir_A_AA (ae) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = (register_P << 4) + 0x0e]; /* set I register */

      /* opADDimm_A_AA (28) */
      register_A = (flag_C = ((cmp_old = acc_a0 = register_A) + (cmp_new = 0x8))) & 0xFFF; /* add values, save carry */

      /* opSTAirg_A_A (e6) */
      ram[register_I] = register_A; /* store acc */
      /* opLDAimm_A_AA (05) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0x0500;
      /* opADDimmX_A_AA (20) */
      cmp_old = register_A; cmp_new = 0x03;
      register_A = flag_C = acc_a0 = 0x0503;
      /* opLDJimm_A_A (43) */
      register_J = 0x0f83;
      /* opJMP_A_A (58) */
      {
        register_PC = 0x1f83;
        break;
      };

    case 0x1f6c:

      /* Invariants: register_P = 0x2 register_I = 0x23 register_A = 0x196 */;
      /* opLSRe_A_AA (eb) */
      cmp_new = 0x0BEB; cmp_old = acc_a0 = register_A; flag_C = (0x0BEB + register_A);
      register_A >>= 1;
      /* opLDJimm_A_A (4d) */
      register_J = 0x0f7d;
      /* opJA0_A_A (5e) */
      if (acc_a0 & 0x01) {
        register_PC = 0x1f7d;
        break;
      };

      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opADDimmX_A_AA (20) */
      cmp_old = register_A; cmp_new = 0x11;
      register_A = flag_C = acc_a0 = 0x0011;
      /* opSTAdir_A_A (d4) */
      ram[register_I = 0x24] = 0x011; /* store acc to RAM */
      /* opLDAdir_A_AA (ae) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = 0x2e]; /* set I register */

      /* opADDimm_A_AA (24) */
      register_A = (flag_C = ((cmp_old = acc_a0 = register_A) + (cmp_new = 0x4))) & 0xFFF; /* add values, save carry */

      /* opSTAirg_A_A (e6) */
      ram[0x2e] = register_A; /* store acc */
      /* opLDAimm_A_AA (04) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0x0400;
      /* opADDimmX_A_AA (20) */
      cmp_old = register_A; cmp_new = 0xc7;
      register_A = flag_C = acc_a0 = 0x04c7;
      /* opLDJimm_A_A (43) */
      register_J = 0x0f83;
      /* opJMP_A_A (58) */
      {
        register_PC = 0x1f83;
        break;
      };

    case 0x1f7d:

      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opADDimm_A_AA (27) */
      cmp_new = 0x7; cmp_old = acc_a0 = register_A; register_A = flag_C = 0x0007;

      /* opSTAdir_A_A (d4) */
      ram[register_I = (register_P << 4) + 0x4] = 0x007; /* store acc to RAM */
      /* opLDAimm_A_AA (04) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0x0400;
      /* opADDimmX_A_AA (20) */
      cmp_old = register_A; cmp_new = 0x81;
      register_A = flag_C = acc_a0 = 0x0481;

    case 0x1f83:

      /* Invariants: register_P = 0x2 register_I = 0x2e */;
      /* opLDPimm_A_A (83) */
      register_P = 0x3; /* set page register */
      /* opSTAdir_A_A (d0) */
      ram[register_I = 0x30] = register_A; /* store acc to RAM */
      /* opLDPimm_A_A (84) */
      register_P = 0x4; /* set page register */
      /* opSTAdir_A_A (d0) */
      ram[register_I = 0x40] = register_A; /* store acc to RAM */
      /* opLDPimm_A_A (85) */
      register_P = 0x5; /* set page register */
      /* opSTAdir_A_A (d0) */
      ram[register_I = 0x50] = register_A; /* store acc to RAM */
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opLDPimm_A_A (86) */
      register_P = 0x6; /* set page register */
      /* opCMPdir_A_AA (b9) */
      cmp_old = acc_a0 = register_A; /* backup old acc */
      flag_C = (((cmp_new = ram[register_I = 0x69]) ^ 0xFFF) + 1 + register_A);
      /* opLDJimm_A_A (49) */
      register_J = 0x0f99;
      /* opJEQ_A_AA (5c) */
      if (cmp_new == cmp_old) {
        register_PC = 0x1f99;
        break;
      };
      /* opADDimmX_A_AA (20) */
      cmp_old = register_A; cmp_new = 0x10;
      register_A = flag_C = acc_a0 = 0x0010;
      /* opLDPimm_A_A (86) */
      /* opCMPdir_A_AA (ba) */
      cmp_old = acc_a0 = register_A; /* backup old acc */
      flag_C = (((cmp_new = ram[register_I = 0x6a]) ^ 0xFFF) + 1 + register_A);
      /* opJNC_A_AA (5d) */
      if (!(flag_C & CARRYBIT)) {
        register_PC = 0x1f99;
        break;
      };
      /* opLDAirg_A_AA (ea) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = ram[register_I];
      /* opLSLe_A_AA (ec) */
      cmp_new = 0x0CEC;
      cmp_old = acc_a0 = register_A;
      flag_C = (0x0CEC + register_A);
      register_A = (register_A << 1) & 0x0FFF;
      /* opADDimm_A_AA (2f) */
      register_A = (flag_C = ((cmp_old = acc_a0 = register_A) + (cmp_new = 0xf))) & 0xFFF; /* add values, save carry */

      /* opLDPimm_A_A (82) */
      register_P = 0x2; /* set page register */
      /* opSTAdir_A_A (de) */
      ram[register_I = 0x2e] = register_A; /* store acc to RAM */

    case 0x1f99:

      /* Invariants: register_P = 0xdeadbeef register_I = 0x2e */;
      /* opLDPimm_A_A (82) */
      register_P = 0x2; /* set page register */
      /* opLDAdir_A_AA (ae) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = 0x2e]; /* set I register */

      /* opLDPimm_A_A (83) */
      register_P = 0x3; /* set page register */
      /* opSTAdir_A_A (d6) */
      ram[register_I = 0x36] = register_A; /* store acc to RAM */
      /* opLDPimm_A_A (84) */
      register_P = 0x4; /* set page register */
      /* opSTAdir_A_A (d6) */
      ram[register_I = 0x46] = register_A; /* store acc to RAM */
      /* opLDPimm_A_A (85) */
      register_P = 0x5; /* set page register */
      /* opSTAdir_A_A (d6) */
      ram[register_I = 0x56] = register_A; /* store acc to RAM */
      /* opLDAimm_A_AA (0c) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0x0c00;
      /* opADDimmX_A_AA (20) */
      cmp_old = register_A; cmp_new = 0x7e;
      register_A = flag_C = acc_a0 = 0x0c7e;
      /* opLDPimm_A_A (82) */
      register_P = 0x2; /* set page register */
      /* opSTAdir_A_A (de) */
      ram[register_I = 0x2e] = 0xc7e; /* store acc to RAM */
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opADDimm_A_AA (27) */
      cmp_new = 0x7; cmp_old = acc_a0 = register_A; register_A = flag_C = 0x0007;

      /* opLDPimm_A_A (82) */
      /* opCMPdir_A_AA (b2) */
      cmp_old = acc_a0 = register_A; /* backup old acc */
      flag_C = (((cmp_new = ram[register_I = 0x22]) ^ 0xFFF) + 1 + register_A);
      /* opANDirg_A_AA (e9) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A &= (cmp_new = ram[register_I]);
      /* opLSLe_A_AA (ec) */
      cmp_new = 0x0CEC;
      cmp_old = acc_a0 = register_A;
      flag_C = (0x0CEC + register_A);
      register_A = (register_A << 1) & 0x0FFF;
      /* opLDPimm_A_A (82) */
      /* opADDdir_A_AA (6e) */
      register_A = (flag_C = ((cmp_old = acc_a0 = register_A) + (cmp_new = ram[register_I = 0x2e]))) & 0xFFF; /* do acc operation */
      /* opSTAirg_A_A (e6) */
      ram[0x2e] = register_A; /* store acc */
      /* opADDimm_A_AA (21) */
      register_A = (flag_C = ((cmp_old = acc_a0 = register_A) + (cmp_new = 0x1))) & 0xFFF; /* add values, save carry */

      /* opXLT_A_AA (e2) */
      cmp_old = register_A; register_A = cmp_new = rom[0x1000 | register_A]; /* new acc value */
      /* opNOP_A_A (5f) */
      /* opSTAdir_A_A (d5) */
      ram[register_I = (register_P << 4) + 0x5] = register_A; /* store acc to RAM */
      /* opNOP_A_B (57) */
      /* opLDAirg_B_AA (ea) */
      flag_C = acc_a0 = register_A;
      cmp_old = register_B;
      register_B = cmp_new = ram[register_I];

      /* opLDAdir_A_AA (ae) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = (register_P << 4) + 0x0e]; /* set I register */

      /* opXLT_A_AA (e2) */
      cmp_old = register_A; register_A = cmp_new = rom[0x1000 | register_A]; /* new acc value */
      /* opNOP_A_A (5f) */
      /* opSTAirg_A_A (e6) */
      ram[register_I] = register_A; /* store acc */
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opASRDe_A_AA (ee) */
      cmp_new = 0x0EEE; cmp_old = acc_a0 = register_A; flag_C = (0x0EEE + register_A);
      register_A = (register_A >> 1) | ((register_B & 1) << 11);
      register_B = (register_B >> 1) | (register_B & 0x800);
      /* opASRDe_A_AA (ee) */
      cmp_new = 0x0EEE; cmp_old = acc_a0 = register_A; flag_C = (0x0EEE + register_A);
      register_A = (register_A >> 1) | ((register_B & 1) << 11);
      register_B = (register_B >> 1) | (register_B & 0x800);
      /* opASRDe_A_AA (ee) */
      cmp_new = 0x0EEE; cmp_old = acc_a0 = register_A; flag_C = (0x0EEE + register_A);
      register_A = (register_A >> 1) | ((register_B & 1) << 11);
      register_B = (register_B >> 1) | (register_B & 0x800);
      /* opASRDe_A_AA (ee) */
      cmp_new = 0x0EEE; cmp_old = acc_a0 = register_A; flag_C = (0x0EEE + register_A);
      register_A = (register_A >> 1) | ((register_B & 1) << 11);
      register_B = (register_B >> 1) | (register_B & 0x800);
      /* opAWDirg_A_AA (e7) */
      cmp_old = acc_a0 = register_A;
      register_A = (flag_C = (register_A + (cmp_new = ram[register_I]))) & 0xFFF;
      set_watchdog();
      /* opSTAdir_A_A (d5) */
      ram[register_I = (register_P << 4) + 0x5] = register_A; /* store acc to RAM */
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opADDimm_A_AA (27) */
      cmp_new = 0x7; cmp_old = acc_a0 = register_A; register_A = flag_C = 0x0007;

      /* opSTAdir_A_A (d0) */
      ram[register_I = (register_P << 4) + 0x0] = 0x007; /* store acc to RAM */
      /* opLDPimm_A_A (81) */
      register_P = 0x1; /* set page register */
      /* opCMPdir_A_AA (bf) */
      cmp_old = acc_a0 = register_A; /* backup old acc */
      flag_C = (((cmp_new = ram[register_I = 0x1f]) ^ 0xFFF) + 1 + register_A);

    case 0x1fc5:

      /* Invariants: register_P = 0x1 register_I = 0x1f register_A = 0x07 */;
      /* opLDJirg_A_A (e1) */
      /* load J reg from value at last dir addr */
      register_J = ram[0x1f];
      /* opLDPimm_A_A (81) */
      /* opJPP8_A_B (50) */
      register_PC = register_J; /* Jump to other rom bank */
      /* WARNING: UNKNOWN JUMP DESTINATION - MAY FOUL UP CODE OPTIMISATIONS */
      break;

    default:
      /* Jumping to any illegal address or end of eprom will come here */
      /* we ought to reinitialise or something */
      ;
  }
}

// ops1800.c

void cineExecute1800(void)
{
  switch (register_PC)
  {
    case 0x180f:

      /* Invariants: register_P = 0x0 register_I = 0x00 register_A = 0xff */;
      /* opLDAimm_A_AA (02) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0x0200;
      /* opADDimmX_A_AA (20) */
      cmp_old = register_A; cmp_new = 0xf8;
      register_A = flag_C = acc_a0 = 0x02f8;
      /* opLDPimm_A_A (80) */
      /* opSTAdir_A_A (d3) */
      ram[register_I = 0x03] = 0x2f8; /* store acc to RAM */
      /* opLDAimm_A_AA (02) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0x0200;
      /* opADDimmX_A_AA (20) */
      cmp_old = register_A; cmp_new = 0x58;
      register_A = flag_C = acc_a0 = 0x0258;
      /* opSTAdir_A_A (d4) */
      ram[register_I = 0x04] = 0x258; /* store acc to RAM */
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opADDimmX_A_AA (20) */
      cmp_old = register_A; cmp_new = 0x07;
      register_A = flag_C = acc_a0 = 0x0007;
      /* opSTAdir_A_A (de) */
      ram[register_I = 0x0e] = 0x007; /* store acc to RAM */
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opADDimm_A_AA (23) */
      cmp_new = 0x3; cmp_old = acc_a0 = register_A; register_A = flag_C = 0x0003;

      /* opSTAdir_A_A (df) */
      ram[register_I = 0x0f] = 0x003; /* store acc to RAM */
      /* opADDimmX_A_AA (20) */
      cmp_old = register_A; cmp_new = 0x01;
      register_A = flag_C = acc_a0 = 0x0004;
      /* opSTAdir_A_A (d2) */
      ram[register_I = 0x02] = 0x004; /* store acc to RAM */
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opADDimmX_A_AA (20) */
      cmp_old = register_A; cmp_new = 0x6c;
      register_A = flag_C = acc_a0 = 0x006c;
      /* opSTAdir_A_A (d5) */
      ram[register_I = 0x05] = 0x06c; /* store acc to RAM */
      /* opLDAimm_A_AA (08) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0x0800;
      /* opADDimmX_A_AA (20) */
      cmp_old = register_A; cmp_new = 0x2d;
      register_A = flag_C = acc_a0 = 0x082d;
      /* opSTAdir_A_A (d0) */
      ram[register_I = 0x00] = 0x82d; /* store acc to RAM */
      /* opLDJimm_A_A (43) */
      register_J = 0x0af3;
      /* opJMP_A_A (58) */
      {
        register_PC = 0x1af3;
        break;
      };

    case 0x182d:

      /* Invariants: register_P = 0x0 register_I = 0x00 register_A = 0x00 */;
      /* opLDAimm_A_AA (03) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0x0300;
      /* opADDimmX_A_AA (20) */
      cmp_old = register_A; cmp_new = 0x10;
      register_A = flag_C = acc_a0 = 0x0310;
      /* opLDPimm_A_A (80) */
      /* opSTAdir_A_A (d3) */
      ram[register_I = 0x03] = 0x310; /* store acc to RAM */
      /* opLDAimm_A_AA (02) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0x0200;
      /* opADDimmX_A_AA (20) */
      cmp_old = register_A; cmp_new = 0x58;
      register_A = flag_C = acc_a0 = 0x0258;
      /* opSTAdir_A_A (d4) */
      ram[register_I = 0x04] = 0x258; /* store acc to RAM */
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opADDimmX_A_AA (20) */
      cmp_old = register_A; cmp_new = 0x07;
      register_A = flag_C = acc_a0 = 0x0007;
      /* opSTAdir_A_A (de) */
      ram[register_I = 0x0e] = 0x007; /* store acc to RAM */
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opADDimm_A_AA (23) */
      cmp_new = 0x3; cmp_old = acc_a0 = register_A; register_A = flag_C = 0x0003;

      /* opSTAdir_A_A (df) */
      ram[register_I = 0x0f] = 0x003; /* store acc to RAM */
      /* opADDimmX_A_AA (20) */
      cmp_old = register_A; cmp_new = 0x01;
      register_A = flag_C = acc_a0 = 0x0004;
      /* opSTAdir_A_A (d2) */
      ram[register_I = 0x02] = 0x004; /* store acc to RAM */
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opADDimmX_A_AA (20) */
      cmp_old = register_A; cmp_new = 0x6d;
      register_A = flag_C = acc_a0 = 0x006d;
      /* opSTAdir_A_A (d5) */
      ram[register_I = 0x05] = 0x06d; /* store acc to RAM */
      /* opLDAimm_A_AA (08) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0x0800;
      /* opADDimmX_A_AA (20) */
      cmp_old = register_A; cmp_new = 0x4b;
      register_A = flag_C = acc_a0 = 0x084b;
      /* opSTAdir_A_A (d0) */
      ram[register_I = 0x00] = 0x84b; /* store acc to RAM */
      /* opLDJimm_A_A (43) */
      register_J = 0x0af3;
      /* opJMP_A_A (58) */
      {
        register_PC = 0x1af3;
        break;
      };

    case 0x184b:

      /* Invariants: register_P = 0x0 register_I = 0x00 register_A = 0x00 */;
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opLDPimm_A_A (88) */
      register_P = 0x8; /* set page register */
      /* opCMPdir_A_AA (b9) */
      cmp_old = acc_a0 = register_A; /* backup old acc */
      flag_C = (((cmp_new = ram[register_I = 0x89]) ^ 0xFFF) + 1 + register_A);
      /* opLDJimm_A_A (4c) */
      register_J = 0x095c;
      /* opJEQ_A_AA (5c) */
      if (cmp_new == cmp_old) {
        register_PC = 0x195c;
        break;
      };
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opOUTbi_A_A (96) */
      vgColour = 0x07;
      /* opADDimm_A_AA (21) */
      cmp_new = 0x1; cmp_old = acc_a0 = register_A; register_A = flag_C = 0x0001;

      /* opLDPimm_A_A (87) */
      register_P = 0x7; /* set page register */
      /* opSTAdir_A_A (da) */
      ram[register_I = 0x7a] = 0x001; /* store acc to RAM */
      /* opAWDirg_A_AA (f7) */
      cmp_old = acc_a0 = register_A;
      register_A = (flag_C = (register_A + (cmp_new = ram[0x7a]))) & 0xFFF;
      set_watchdog();
      /* opLDAimm_A_AA (01) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0x0100;
      /* opADDimmX_A_AA (20) */
      cmp_old = register_A; cmp_new = 0x90;
      register_A = flag_C = acc_a0 = 0x0190;
      /* opLDPimm_A_A (80) */
      register_P = 0x0; /* set page register */
      /* opSTAdir_A_A (d3) */
      ram[register_I = 0x03] = 0x190; /* store acc to RAM */
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opADDimmX_A_AA (20) */
      cmp_old = register_A; cmp_new = 0x32;
      register_A = flag_C = acc_a0 = 0x0032;
      /* opSTAdir_A_A (d4) */
      ram[register_I = 0x04] = 0x032; /* store acc to RAM */
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opADDimmX_A_AA (20) */
      cmp_old = register_A; cmp_new = 0x4d;
      register_A = flag_C = acc_a0 = 0x004d;
      /* opSTAdir_A_A (de) */
      ram[register_I = 0x0e] = 0x04d; /* store acc to RAM */
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opSTAdir_A_A (df) */
      ram[register_I = 0x0f] = 0x000; /* store acc to RAM */
      /* opLDAimm_A_AA (08) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0x0800;
      /* opADDimmX_A_AA (20) */
      cmp_old = register_A; cmp_new = 0x6f;
      register_A = flag_C = acc_a0 = 0x086f;
      /* opSTAdir_A_A (d5) */
      ram[register_I = 0x05] = 0x86f; /* store acc to RAM */
      /* opADDimmX_A_AA (20) */
      cmp_old = register_A; cmp_new = 0x09;
      register_A = flag_C = acc_a0 = 0x0878;
      /* opSTAdir_A_A (d0) */
      ram[register_I = 0x00] = 0x878; /* store acc to RAM */
      /* opLDJimm_A_A (43) */
      register_J = 0x0af3;
      /* opJMP_A_A (58) */
      {
        register_PC = 0x1af3;
        break;
      };

    case 0x1878:

      /* Invariants: register_P = 0x0 register_I = 0x00 register_A = 0xff */;
      /* opLDPimm_A_A (88) */
      register_P = 0x8; /* set page register */
      /* opLDAdir_A_AA (a9) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = 0x89]; /* set I register */

      /* opSTAdir_A_A (dc) */
      ram[register_I = 0x8c] = register_A; /* store acc to RAM */
      /* opSUBimm_A_AA (39) */
      cmp_old = acc_a0 = register_A;
      register_A = (flag_C = (register_A + (((cmp_new = 0x9) ^ 0xFFF) + 1))) & 0xFFF; /* 1's-comp add */
      /* opLDJimm_A_A (42) */
      register_J = 0x0882;
      /* opJNC_A_AA (5d) */
      if (!(flag_C & CARRYBIT)) {
        register_PC = 0x1882;
        break;
      };
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opADDimm_A_AA (29) */
      cmp_new = 0x9; cmp_old = acc_a0 = register_A; register_A = flag_C = 0x0009;

      /* opSTAirg_A_A (e6) */
      ram[0x8c] = 0x009; /* store acc */

    case 0x1882:

      /* Invariants: register_P = 0x8 register_I = 0x8c register_A = 0xff8 */;
      /* opLDAimm_A_AA (02) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0x0200;
      /* opADDimmX_A_AA (20) */
      cmp_old = register_A; cmp_new = 0x58;
      register_A = flag_C = acc_a0 = 0x0258;
      /* opLDPimm_A_A (80) */
      register_P = 0x0; /* set page register */
      /* opSTAdir_A_A (d3) */
      ram[register_I = 0x03] = 0x258; /* store acc to RAM */
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opADDimmX_A_AA (20) */
      cmp_old = register_A; cmp_new = 0x32;
      register_A = flag_C = acc_a0 = 0x0032;
      /* opSTAdir_A_A (d4) */
      ram[register_I = 0x04] = 0x032; /* store acc to RAM */
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opADDimmX_A_AA (20) */
      cmp_old = register_A; cmp_new = 0x0b;
      register_A = flag_C = acc_a0 = 0x000b;
      /* opSTAdir_A_A (de) */
      ram[register_I = 0x0e] = 0x00b; /* store acc to RAM */
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opADDimm_A_AA (23) */
      cmp_new = 0x3; cmp_old = acc_a0 = register_A; register_A = flag_C = 0x0003;

      /* opSTAdir_A_A (df) */
      ram[register_I = 0x0f] = 0x003; /* store acc to RAM */
      /* opADDimmX_A_AA (20) */
      cmp_old = register_A; cmp_new = 0x01;
      register_A = flag_C = acc_a0 = 0x0004;
      /* opSTAdir_A_A (d2) */
      ram[register_I = 0x02] = 0x004; /* store acc to RAM */
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opADDimmX_A_AA (20) */
      cmp_old = register_A; cmp_new = 0x8c;
      register_A = flag_C = acc_a0 = 0x008c;
      /* opSTAdir_A_A (d5) */
      ram[register_I = 0x05] = 0x08c; /* store acc to RAM */
      /* opLDAimm_A_AA (08) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0x0800;
      /* opADDimmX_A_AA (20) */
      cmp_old = register_A; cmp_new = 0xa0;
      register_A = flag_C = acc_a0 = 0x08a0;
      /* opSTAdir_A_A (d0) */
      ram[register_I = 0x00] = 0x8a0; /* store acc to RAM */
      /* opLDJimm_A_A (43) */
      register_J = 0x0af3;
      /* opJMP_A_A (58) */
      {
        register_PC = 0x1af3;
        break;
      };

    case 0x18a0:

      /* Invariants: register_P = 0x0 register_I = 0x00 register_A = 0x00 */;
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opADDimmX_A_AA (20) */
      cmp_old = register_A; cmp_new = 0x1e;
      register_A = flag_C = acc_a0 = 0x001e;
      /* opOUTbi_A_A (93) */
      /* opOUTsnd_A (93) */
      set_sound_data(0);
      /* opLSRe_A_AA (eb) */
      cmp_new = 0x0BEB; cmp_old = acc_a0 = register_A; flag_C = (0x0BEB + register_A);
      register_A >>= 1;
      /* opOUTbi_A_A (90) */
      /* opOUTsnd_A (90) */
      set_sound_addr_A(register_A & 1);
      /* opLSRe_A_AA (eb) */
      cmp_new = 0x0BEB; cmp_old = acc_a0 = register_A; flag_C = (0x0BEB + register_A);
      register_A >>= 1;
      /* opOUTbi_A_A (91) */
      /* opOUTsnd_A (91) */
      set_sound_addr_B(register_A & 1);
      /* opLSRe_A_AA (eb) */
      cmp_new = 0x0BEB; cmp_old = acc_a0 = register_A; flag_C = (0x0BEB + register_A);
      register_A >>= 1;
      /* opOUTbi_A_A (92) */
      /* opOUTsnd_A (92) */
      set_sound_addr_C(register_A & 1);
      /* opLSRe_A_AA (eb) */
      cmp_new = 0x0BEB; cmp_old = acc_a0 = register_A; flag_C = (0x0BEB + register_A);
      register_A >>= 1;
      /* opOUTbi_A_A (94) */
      /* opOUTsnd_A (94) */
      if (register_A & 1) strobe_sound_on(); else strobe_sound_off();
      /* opLSRe_A_AA (eb) */
      cmp_new = 0x0BEB; cmp_old = acc_a0 = register_A; flag_C = (0x0BEB + register_A);
      register_A >>= 1;
      /* opOUTbi_A_A (94) */
      /* opOUTsnd_A (94) */
      if (register_A & 1) strobe_sound_on(); else strobe_sound_off();
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opADDimmX_A_AA (20) */
      cmp_old = register_A; cmp_new = 0x1a;
      register_A = flag_C = acc_a0 = 0x001a;
      /* opOUTbi_A_A (93) */
      /* opOUTsnd_A (93) */
      set_sound_data(0);
      /* opLSRe_A_AA (eb) */
      cmp_new = 0x0BEB; cmp_old = acc_a0 = register_A; flag_C = (0x0BEB + register_A);
      register_A >>= 1;
      /* opOUTbi_A_A (90) */
      /* opOUTsnd_A (90) */
      set_sound_addr_A(register_A & 1);
      /* opLSRe_A_AA (eb) */
      cmp_new = 0x0BEB; cmp_old = acc_a0 = register_A; flag_C = (0x0BEB + register_A);
      register_A >>= 1;
      /* opOUTbi_A_A (91) */
      /* opOUTsnd_A (91) */
      set_sound_addr_B(register_A & 1);
      /* opLSRe_A_AA (eb) */
      cmp_new = 0x0BEB; cmp_old = acc_a0 = register_A; flag_C = (0x0BEB + register_A);
      register_A >>= 1;
      /* opOUTbi_A_A (92) */
      /* opOUTsnd_A (92) */
      set_sound_addr_C(register_A & 1);
      /* opLSRe_A_AA (eb) */
      cmp_new = 0x0BEB; cmp_old = acc_a0 = register_A; flag_C = (0x0BEB + register_A);
      register_A >>= 1;
      /* opOUTbi_A_A (94) */
      /* opOUTsnd_A (94) */
      if (register_A & 1) strobe_sound_on(); else strobe_sound_off();
      /* opLSRe_A_AA (eb) */
      cmp_new = 0x0BEB; cmp_old = acc_a0 = register_A; flag_C = (0x0BEB + register_A);
      register_A >>= 1;
      /* opOUTbi_A_A (94) */
      /* opOUTsnd_A (94) */
      if (register_A & 1) strobe_sound_on(); else strobe_sound_off();
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opADDimmX_A_AA (20) */
      cmp_old = register_A; cmp_new = 0x14;
      register_A = flag_C = acc_a0 = 0x0014;
      /* opOUTbi_A_A (93) */
      /* opOUTsnd_A (93) */
      set_sound_data(0);
      /* opLSRe_A_AA (eb) */
      cmp_new = 0x0BEB; cmp_old = acc_a0 = register_A; flag_C = (0x0BEB + register_A);
      register_A >>= 1;
      /* opOUTbi_A_A (90) */
      /* opOUTsnd_A (90) */
      set_sound_addr_A(register_A & 1);
      /* opLSRe_A_AA (eb) */
      cmp_new = 0x0BEB; cmp_old = acc_a0 = register_A; flag_C = (0x0BEB + register_A);
      register_A >>= 1;
      /* opOUTbi_A_A (91) */
      /* opOUTsnd_A (91) */
      set_sound_addr_B(register_A & 1);
      /* opLSRe_A_AA (eb) */
      cmp_new = 0x0BEB; cmp_old = acc_a0 = register_A; flag_C = (0x0BEB + register_A);
      register_A >>= 1;
      /* opOUTbi_A_A (92) */
      /* opOUTsnd_A (92) */
      set_sound_addr_C(register_A & 1);
      /* opLSRe_A_AA (eb) */
      cmp_new = 0x0BEB; cmp_old = acc_a0 = register_A; flag_C = (0x0BEB + register_A);
      register_A >>= 1;
      /* opOUTbi_A_A (94) */
      /* opOUTsnd_A (94) */
      if (register_A & 1) strobe_sound_on(); else strobe_sound_off();
      /* opLSRe_A_AA (eb) */
      cmp_new = 0x0BEB; cmp_old = acc_a0 = register_A; flag_C = (0x0BEB + register_A);
      register_A >>= 1;
      /* opOUTbi_A_A (94) */
      /* opOUTsnd_A (94) */
      if (register_A & 1) strobe_sound_on(); else strobe_sound_off();
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opADDimmX_A_AA (20) */
      cmp_old = register_A; cmp_new = 0x16;
      register_A = flag_C = acc_a0 = 0x0016;
      /* opOUTbi_A_A (93) */
      /* opOUTsnd_A (93) */
      set_sound_data(0);
      /* opLSRe_A_AA (eb) */
      cmp_new = 0x0BEB; cmp_old = acc_a0 = register_A; flag_C = (0x0BEB + register_A);
      register_A >>= 1;
      /* opOUTbi_A_A (90) */
      /* opOUTsnd_A (90) */
      set_sound_addr_A(register_A & 1);
      /* opLSRe_A_AA (eb) */
      cmp_new = 0x0BEB; cmp_old = acc_a0 = register_A; flag_C = (0x0BEB + register_A);
      register_A >>= 1;
      /* opOUTbi_A_A (91) */
      /* opOUTsnd_A (91) */
      set_sound_addr_B(register_A & 1);
      /* opLSRe_A_AA (eb) */
      cmp_new = 0x0BEB; cmp_old = acc_a0 = register_A; flag_C = (0x0BEB + register_A);
      register_A >>= 1;
      /* opOUTbi_A_A (92) */
      /* opOUTsnd_A (92) */
      set_sound_addr_C(register_A & 1);
      /* opLSRe_A_AA (eb) */
      cmp_new = 0x0BEB; cmp_old = acc_a0 = register_A; flag_C = (0x0BEB + register_A);
      register_A >>= 1;
      /* opOUTbi_A_A (94) */
      /* opOUTsnd_A (94) */
      if (register_A & 1) strobe_sound_on(); else strobe_sound_off();
      /* opLSRe_A_AA (eb) */
      cmp_new = 0x0BEB; cmp_old = acc_a0 = register_A; flag_C = (0x0BEB + register_A);
      register_A >>= 1;
      /* opOUTbi_A_A (94) */
      /* opOUTsnd_A (94) */
      if (register_A & 1) strobe_sound_on(); else strobe_sound_off();
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opADDimmX_A_AA (20) */
      cmp_old = register_A; cmp_new = 0x18;
      register_A = flag_C = acc_a0 = 0x0018;
      /* opOUTbi_A_A (93) */
      /* opOUTsnd_A (93) */
      set_sound_data(0);
      /* opLSRe_A_AA (eb) */
      cmp_new = 0x0BEB; cmp_old = acc_a0 = register_A; flag_C = (0x0BEB + register_A);
      register_A >>= 1;
      /* opOUTbi_A_A (90) */
      /* opOUTsnd_A (90) */
      set_sound_addr_A(register_A & 1);
      /* opLSRe_A_AA (eb) */
      cmp_new = 0x0BEB; cmp_old = acc_a0 = register_A; flag_C = (0x0BEB + register_A);
      register_A >>= 1;
      /* opOUTbi_A_A (91) */
      /* opOUTsnd_A (91) */
      set_sound_addr_B(register_A & 1);
      /* opLSRe_A_AA (eb) */
      cmp_new = 0x0BEB; cmp_old = acc_a0 = register_A; flag_C = (0x0BEB + register_A);
      register_A >>= 1;
      /* opOUTbi_A_A (92) */
      /* opOUTsnd_A (92) */
      set_sound_addr_C(register_A & 1);
      /* opLSRe_A_AA (eb) */
      cmp_new = 0x0BEB; cmp_old = acc_a0 = register_A; flag_C = (0x0BEB + register_A);
      register_A >>= 1;
      /* opOUTbi_A_A (94) */
      /* opOUTsnd_A (94) */
      if (register_A & 1) strobe_sound_on(); else strobe_sound_off();
      /* opLSRe_A_AA (eb) */
      cmp_new = 0x0BEB; cmp_old = acc_a0 = register_A; flag_C = (0x0BEB + register_A);
      register_A >>= 1;
      /* opOUTbi_A_A (94) */
      /* opOUTsnd_A (94) */
      if (register_A & 1) strobe_sound_on(); else strobe_sound_off();
      /* opLDPimm_A_A (87) */
      register_P = 0x7; /* set page register */
      /* opLDAdir_A_AA (ab) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = 0x7b]; /* set I register */

      /* opADDimm_A_AA (21) */
      register_A = (flag_C = ((cmp_old = acc_a0 = register_A) + (cmp_new = 0x1))) & 0xFFF; /* add values, save carry */

      /* opSTAirg_A_A (e6) */
      ram[0x7b] = register_A; /* store acc */
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opADDimm_A_AA (28) */
      cmp_new = 0x8; cmp_old = acc_a0 = register_A; register_A = flag_C = 0x0008;

      /* opANDirg_A_AA (e9) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A &= (cmp_new = ram[register_I]);
      /* opLSRe_A_AA (eb) */
      cmp_new = 0x0BEB; cmp_old = acc_a0 = register_A; flag_C = (0x0BEB + register_A);
      register_A >>= 1;
      /* opLSRe_A_AA (eb) */
      cmp_new = 0x0BEB; cmp_old = acc_a0 = register_A; flag_C = (0x0BEB + register_A);
      register_A >>= 1;
      /* opLSRe_A_AA (eb) */
      cmp_new = 0x0BEB; cmp_old = acc_a0 = register_A; flag_C = (0x0BEB + register_A);
      register_A >>= 1;
      /* opOUTbi_A_A (96) */
      vgColour = register_A & 0x01 ? 0x0f : 0x07;
      /* opADDimmX_A_AA (20) */
      register_A = (flag_C = ((acc_a0 = cmp_old = register_A) + (cmp_new = 0x0012))) & 0xFFF; /* add values */
      /* opOUTbi_A_A (93) */
      /* opOUTsnd_A (93) */
      set_sound_data(register_A & 1);
      /* opLSRe_A_AA (eb) */
      cmp_new = 0x0BEB; cmp_old = acc_a0 = register_A; flag_C = (0x0BEB + register_A);
      register_A >>= 1;
      /* opOUTbi_A_A (90) */
      /* opOUTsnd_A (90) */
      set_sound_addr_A(register_A & 1);
      /* opLSRe_A_AA (eb) */
      cmp_new = 0x0BEB; cmp_old = acc_a0 = register_A; flag_C = (0x0BEB + register_A);
      register_A >>= 1;
      /* opOUTbi_A_A (91) */
      /* opOUTsnd_A (91) */
      set_sound_addr_B(register_A & 1);
      /* opLSRe_A_AA (eb) */
      cmp_new = 0x0BEB; cmp_old = acc_a0 = register_A; flag_C = (0x0BEB + register_A);
      register_A >>= 1;
      /* opOUTbi_A_A (92) */
      /* opOUTsnd_A (92) */
      set_sound_addr_C(register_A & 1);
      /* opLSRe_A_AA (eb) */
      cmp_new = 0x0BEB; cmp_old = acc_a0 = register_A; flag_C = (0x0BEB + register_A);
      register_A >>= 1;
      /* opOUTbi_A_A (94) */
      /* opOUTsnd_A (94) */
      if (register_A & 1) strobe_sound_on(); else strobe_sound_off();
      /* opLSRe_A_AA (eb) */
      cmp_new = 0x0BEB; cmp_old = acc_a0 = register_A; flag_C = (0x0BEB + register_A);
      register_A >>= 1;
      /* opOUTbi_A_A (94) */
      /* opOUTsnd_A (94) */
      if (register_A & 1) strobe_sound_on(); else strobe_sound_off();
      /* opAWDirg_A_AA (f7) */
      cmp_old = acc_a0 = register_A;
      register_A = (flag_C = (register_A + (cmp_new = ram[0x7b]))) & 0xFFF;
      set_watchdog();
      /* opLDAimm_A_AA (01) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0x0100;
      /* opADDimmX_A_AA (20) */
      cmp_old = register_A; cmp_new = 0x90;
      register_A = flag_C = acc_a0 = 0x0190;
      /* opLDPimm_A_A (80) */
      register_P = 0x0; /* set page register */
      /* opSTAdir_A_A (d3) */
      ram[register_I = 0x03] = 0x190; /* store acc to RAM */
      /* opLDAimm_A_AA (01) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0x0100;
      /* opADDimmX_A_AA (20) */
      cmp_old = register_A; cmp_new = 0x90;
      register_A = flag_C = acc_a0 = 0x0190;
      /* opSTAdir_A_A (d4) */
      ram[register_I = 0x04] = 0x190; /* store acc to RAM */
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opADDimmX_A_AA (20) */
      cmp_old = register_A; cmp_new = 0x2c;
      register_A = flag_C = acc_a0 = 0x002c;
      /* opSTAdir_A_A (de) */
      ram[register_I = 0x0e] = 0x02c; /* store acc to RAM */
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opSTAdir_A_A (df) */
      ram[register_I = 0x0f] = 0x000; /* store acc to RAM */
      /* opLDAimm_A_AA (09) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0x0900;
      /* opADDimmX_A_AA (20) */
      cmp_old = register_A; cmp_new = 0x17;
      register_A = flag_C = acc_a0 = 0x0917;
      /* opSTAdir_A_A (d5) */
      ram[register_I = 0x05] = 0x917; /* store acc to RAM */
      /* opADDimmX_A_AA (20) */
      cmp_old = register_A; cmp_new = 0x0c;
      register_A = flag_C = acc_a0 = 0x0923;
      /* opSTAdir_A_A (d0) */
      ram[register_I = 0x00] = 0x923; /* store acc to RAM */
      /* opLDJimm_A_A (43) */
      register_J = 0x0af3;
      /* opJMP_A_A (58) */
      {
        register_PC = 0x1af3;
        break;
      };

    case 0x1923:

      /* Invariants: register_P = 0x0 register_I = 0x00 register_A = 0xff */;
      /* opINP_A_AA (17) */
      cmp_old = flag_C = acc_a0 = register_A;
#ifdef RAWIO
      register_A = cmp_new = get_io_bit(0x7);
#else
      register_A = cmp_new = get_io_startbutton();
#endif
      /* opADDimm_A_AA (21) */
      register_A = (flag_C = ((cmp_old = acc_a0 = register_A) + (cmp_new = 0x1))) & 0xFFF; /* add values, save carry */

      /* opLDJimm_A_A (43) */
      register_J = 0x09c3;
      /* opJEQ_A_AA (5c) */
      if (cmp_new == cmp_old) {
        register_PC = 0x19c3;
        break;
      };
      /* opLDPimm_A_A (88) */
      register_P = 0x8; /* set page register */
      /* opLDAdir_A_AA (a9) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = 0x89]; /* set I register */

      /* opSUBimm_A_AA (31) */
      cmp_old = acc_a0 = register_A;
      register_A = (flag_C = (register_A + (((cmp_new = 0x1) ^ 0xFFF) + 1))) & 0xFFF; /* 1's-comp add */
      /* opSTAirg_A_A (e6) */
      ram[0x89] = register_A; /* store acc */
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opADDimmX_A_AA (20) */
      cmp_old = register_A; cmp_new = 0x12;
      register_A = flag_C = acc_a0 = 0x0012;
      /* opOUTbi_A_A (93) */
      /* opOUTsnd_A (93) */
      set_sound_data(0);
      /* opLSRe_A_AA (eb) */
      cmp_new = 0x0BEB; cmp_old = acc_a0 = register_A; flag_C = (0x0BEB + register_A);
      register_A >>= 1;
      /* opOUTbi_A_A (90) */
      /* opOUTsnd_A (90) */
      set_sound_addr_A(register_A & 1);
      /* opLSRe_A_AA (eb) */
      cmp_new = 0x0BEB; cmp_old = acc_a0 = register_A; flag_C = (0x0BEB + register_A);
      register_A >>= 1;
      /* opOUTbi_A_A (91) */
      /* opOUTsnd_A (91) */
      set_sound_addr_B(register_A & 1);
      /* opLSRe_A_AA (eb) */
      cmp_new = 0x0BEB; cmp_old = acc_a0 = register_A; flag_C = (0x0BEB + register_A);
      register_A >>= 1;
      /* opOUTbi_A_A (92) */
      /* opOUTsnd_A (92) */
      set_sound_addr_C(register_A & 1);
      /* opLSRe_A_AA (eb) */
      cmp_new = 0x0BEB; cmp_old = acc_a0 = register_A; flag_C = (0x0BEB + register_A);
      register_A >>= 1;
      /* opOUTbi_A_A (94) */
      /* opOUTsnd_A (94) */
      if (register_A & 1) strobe_sound_on(); else strobe_sound_off();
      /* opLSRe_A_AA (eb) */
      cmp_new = 0x0BEB; cmp_old = acc_a0 = register_A; flag_C = (0x0BEB + register_A);
      register_A >>= 1;
      /* opOUTbi_A_A (94) */
      /* opOUTsnd_A (94) */
      if (register_A & 1) strobe_sound_on(); else strobe_sound_off();
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opLDPimm_A_A (82) */
      register_P = 0x2; /* set page register */
      /* opSTAdir_A_A (da) */
      ram[register_I = 0x2a] = 0x000; /* store acc to RAM */
      /* opSTAdir_A_A (db) */
      ram[register_I = 0x2b] = 0x000; /* store acc to RAM */
      /* opLDPimm_A_A (87) */
      register_P = 0x7; /* set page register */
      /* opSTAdir_A_A (da) */
      ram[register_I = 0x7a] = 0x000; /* store acc to RAM */
      /* opSTAdir_A_A (dd) */
      ram[register_I = 0x7d] = 0x000; /* store acc to RAM */
      /* opLDPimm_A_A (86) */
      register_P = 0x6; /* set page register */
      /* opSTAdir_A_A (da) */
      ram[register_I = 0x6a] = 0x000; /* store acc to RAM */
      /* opSTAdir_A_A (db) */
      ram[register_I = 0x6b] = 0x000; /* store acc to RAM */
      /* opLDPimm_A_A (83) */
      register_P = 0x3; /* set page register */
      /* opSTAdir_A_A (d0) */
      ram[register_I = 0x30] = 0x000; /* store acc to RAM */
      /* opLDPimm_A_A (84) */
      register_P = 0x4; /* set page register */
      /* opSTAdir_A_A (d0) */
      ram[register_I = 0x40] = 0x000; /* store acc to RAM */
      /* opLDPimm_A_A (85) */
      register_P = 0x5; /* set page register */
      /* opSTAdir_A_A (d0) */
      ram[register_I = 0x50] = 0x000; /* store acc to RAM */
      /* opLDPimm_A_A (87) */
      register_P = 0x7; /* set page register */
      /* opSTAdir_A_A (db) */
      ram[register_I = 0x7b] = 0x000; /* store acc to RAM */
      /* opLDPimm_A_A (88) */
      register_P = 0x8; /* set page register */
      /* opSTAdir_A_A (d0) */
      ram[register_I = 0x80] = 0x000; /* store acc to RAM */
      /* opLDAdir_A_AA (a2) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = 0x82]; /* set I register */

      /* opSTAdir_A_A (d1) */
      ram[register_I = 0x81] = register_A; /* store acc to RAM */
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opADDimm_A_AA (2a) */
      cmp_new = 0xa; cmp_old = acc_a0 = register_A; register_A = flag_C = 0x000a;

      /* opLDPimm_A_A (86) */
      register_P = 0x6; /* set page register */
      /* opSTAdir_A_A (d9) */
      ram[register_I = 0x69] = 0x00a; /* store acc to RAM */
      /* opLDPimm_A_A (88) */
      register_P = 0x8; /* set page register */
      /* opLDAdir_A_AA (aa) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = 0x8a]; /* set I register */

      /* opADDimm_A_AA (21) */
      register_A = (flag_C = ((cmp_old = acc_a0 = register_A) + (cmp_new = 0x1))) & 0xFFF; /* add values, save carry */

      /* opSTAirg_A_A (e6) */
      ram[0x8a] = register_A; /* store acc */
      /* opLDJimm_A_A (48) */
      register_J = 0x0058;
      /* opLDPimm_A_A (81) */
      register_P = 0x1; /* set page register */
      /* opJPP8_A_B (50) */
      register_PC = 0x0058; /* Jump to other rom bank */
      break;

    case 0x195c:

      /* Invariants: register_P = 0x8 register_I = 0x89 register_A = 0x00 */;
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opADDimmX_A_AA (20) */
      cmp_old = register_A; cmp_new = 0x12;
      register_A = flag_C = acc_a0 = 0x0012;
      /* opOUTbi_A_A (93) */
      /* opOUTsnd_A (93) */
      set_sound_data(0);
      /* opLSRe_A_AA (eb) */
      cmp_new = 0x0BEB; cmp_old = acc_a0 = register_A; flag_C = (0x0BEB + register_A);
      register_A >>= 1;
      /* opOUTbi_A_A (90) */
      /* opOUTsnd_A (90) */
      set_sound_addr_A(register_A & 1);
      /* opLSRe_A_AA (eb) */
      cmp_new = 0x0BEB; cmp_old = acc_a0 = register_A; flag_C = (0x0BEB + register_A);
      register_A >>= 1;
      /* opOUTbi_A_A (91) */
      /* opOUTsnd_A (91) */
      set_sound_addr_B(register_A & 1);
      /* opLSRe_A_AA (eb) */
      cmp_new = 0x0BEB; cmp_old = acc_a0 = register_A; flag_C = (0x0BEB + register_A);
      register_A >>= 1;
      /* opOUTbi_A_A (92) */
      /* opOUTsnd_A (92) */
      set_sound_addr_C(register_A & 1);
      /* opLSRe_A_AA (eb) */
      cmp_new = 0x0BEB; cmp_old = acc_a0 = register_A; flag_C = (0x0BEB + register_A);
      register_A >>= 1;
      /* opOUTbi_A_A (94) */
      /* opOUTsnd_A (94) */
      if (register_A & 1) strobe_sound_on(); else strobe_sound_off();
      /* opLSRe_A_AA (eb) */
      cmp_new = 0x0BEB; cmp_old = acc_a0 = register_A; flag_C = (0x0BEB + register_A);
      register_A >>= 1;
      /* opOUTbi_A_A (94) */
      /* opOUTsnd_A (94) */
      if (register_A & 1) strobe_sound_on(); else strobe_sound_off();
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opLDPimm_A_A (87) */
      register_P = 0x7; /* set page register */
      /* opLDAdir_A_AA (ad) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = 0x7d]; /* set I register */

      /* opLDJimm_A_A (43) */
      register_J = 0x09c3;
      /* opJEQ_A_AA (5c) */
      if (cmp_new == cmp_old) {
        register_PC = 0x19c3;
        break;
      };
      /* opLSRe_A_AA (eb) */
      cmp_new = 0x0BEB; cmp_old = acc_a0 = register_A; flag_C = (0x0BEB + register_A);
      register_A >>= 1;
      /* opLSRe_A_AA (eb) */
      cmp_new = 0x0BEB; cmp_old = acc_a0 = register_A; flag_C = (0x0BEB + register_A);
      register_A >>= 1;
      /* opOUTbi_A_A (96) */
      vgColour = register_A & 0x01 ? 0x0f : 0x07;
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opLDPimm_A_A (81) */
      register_P = 0x1; /* set page register */
      /* opSTAdir_A_A (d9) */
      ram[register_I = 0x19] = 0x000; /* store acc to RAM */
      /* opSTAdir_A_A (d0) */
      ram[register_I = 0x10] = 0x000; /* store acc to RAM */
      /* opSTAdir_A_A (d1) */
      ram[register_I = 0x11] = 0x000; /* store acc to RAM */
      /* opSTAdir_A_A (dc) */
      ram[register_I = 0x1c] = 0x000; /* store acc to RAM */
      /* opADDimmX_A_AA (20) */
      cmp_old = register_A; cmp_new = 0xff;
      register_A = flag_C = acc_a0 = 0x00ff;
      /* opSTAdir_A_A (dd) */
      ram[register_I = 0x1d] = 0x0ff; /* store acc to RAM */
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opADDimm_A_AA (22) */
      cmp_new = 0x2; cmp_old = acc_a0 = register_A; register_A = flag_C = 0x0002;

      /* opLDPimm_A_A (87) */
      register_P = 0x7; /* set page register */
      /* opCMPdir_A_AA (bd) */
      cmp_old = acc_a0 = register_A; /* backup old acc */
      flag_C = (((cmp_new = ram[register_I = 0x7d]) ^ 0xFFF) + 1 + register_A);
      /* opLDJimm_A_A (42) */
      register_J = 0x09b2;
      /* opJNC_A_AA (5d) */
      if (!(flag_C & CARRYBIT)) {
        register_PC = 0x19b2;
        break;
      };
      /* opLDAdir_A_AA (ac) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = 0x7c]; /* set I register */

      /* opSUBimm_A_AA (33) */
      cmp_old = acc_a0 = register_A;
      register_A = (flag_C = (register_A + (((cmp_new = 0x3) ^ 0xFFF) + 1))) & 0xFFF; /* 1's-comp add */
      /* opSTAirg_A_A (e6) */
      ram[0x7c] = register_A; /* store acc */
      /* opLDPimm_A_A (81) */
      register_P = 0x1; /* set page register */
      /* opSTAdir_A_A (de) */
      ram[register_I = 0x1e] = register_A; /* store acc to RAM */
      /* opLDAimm_A_AA (09) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0x0900;
      /* opADDimmX_A_AA (20) */
      cmp_old = register_A; cmp_new = 0x8f;
      register_A = flag_C = acc_a0 = 0x098f;
      /* opSTAdir_A_A (df) */
      ram[register_I = 0x1f] = 0x98f; /* store acc to RAM */
      /* opLDJimm_A_A (45) */
      register_J = 0x0575;
      /* opJMP_A_A (58) */
      {
        register_PC = 0x1575;
        break;
      };

    case 0x198f:

      /* Invariants: register_P = 0x1 register_I = 0x1f */;
      /* opLDAimm_A_AA (02) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0x0200;
      /* opLDPimm_A_A (87) */
      register_P = 0x7; /* set page register */
      /* opCMPdir_A_AA (be) */
      cmp_old = acc_a0 = register_A; /* backup old acc */
      flag_C = (((cmp_new = ram[register_I = 0x7e]) ^ 0xFFF) + 1 + register_A);
      /* opLDJimm_A_A (48) */
      register_J = 0x0998;
      /* opJNC_A_AA (5d) */
      if (!(flag_C & CARRYBIT)) {
        register_PC = 0x1998;
        break;
      };
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opADDimm_A_AA (23) */
      cmp_new = 0x3; cmp_old = acc_a0 = register_A; register_A = flag_C = 0x0003;

      /* opSTAdir_A_A (dd) */
      ram[register_I = 0x7d] = 0x003; /* store acc to RAM */

    case 0x1998:

      /* Invariants: register_P = 0x7 register_I = 0x7e register_A = 0x200 */;
      /* opLDAdir_A_AA (ae) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = 0x7e]; /* set I register */

      /* opSUBimm_A_AA (3b) */
      cmp_old = acc_a0 = register_A;
      register_A = (flag_C = (register_A + (((cmp_new = 0xb) ^ 0xFFF) + 1))) & 0xFFF; /* 1's-comp add */
      /* opSTAirg_A_A (e6) */
      ram[0x7e] = register_A; /* store acc */
      /* opLDPimm_A_A (81) */
      register_P = 0x1; /* set page register */
      /* opSTAdir_A_A (d2) */
      ram[register_I = 0x12] = register_A; /* store acc to RAM */

    case 0x199d:

      /* Invariants: register_P = 0x1 register_I = 0x12 */;
      /* opLDAimm_A_AA (09) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0x0900;
      /* opADDimmX_A_AA (20) */
      cmp_old = register_A; cmp_new = 0xc3;
      register_A = flag_C = acc_a0 = 0x09c3;
      /* opLDPimm_A_A (80) */
      register_P = 0x0; /* set page register */
      /* opSTAdir_A_A (d0) */
      ram[register_I = 0x00] = 0x9c3; /* store acc to RAM */
      /* opNOP_A_B (57) */
      /* opLDAimm_B_AA (03) */
      flag_C = acc_a0 = register_A;
      cmp_old = register_B; /* step back cmp flag */
      register_B = cmp_new = 0x0300;
      /* opASRDe_A_AA (ee) */
      cmp_new = 0x0EEE; cmp_old = acc_a0 = register_A; flag_C = (0x0EEE + register_A);
      register_A = (register_A >> 1) | ((register_B & 1) << 11);
      register_B = (register_B >> 1) | (register_B & 0x800);
      /* opNOP_A_B (57) */
      /* opADDimmX_B_AA (20) */
      cmp_old = register_B; acc_a0 = register_A; /* save old accA bit0 */
      register_B = (flag_C = (register_B + (cmp_new = 0x32))) & 0xFFF; /* add values */
      /* opLDAimm_A_AA (02) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0x0200;
      /* opSUBimmX_A_AA (30) */
      cmp_old = acc_a0 = register_A; /* back up regA */
      register_A = (flag_C = (register_A + (((cmp_new = 0x32) ^ 0xFFF) + 1))) & 0xFFF; /* add */
      /* opLDJimm_A_A (4d) */
      register_J = 0x09ad;
      /* opJDR_A_A (5a) */
      /* opVIN_A_A (f0) */

      FromX = register_A & 0xFFF; /* regA goes to x-coord */
      FromY = register_B & 0xFFF; /* regB goes to y-coord */

      /* opLDJimm_A_A (47) */
      register_J = 0x09c7;
      /* opJMP_A_A (58) */
      {
        register_PC = 0x19c7;
        break;
      };

    case 0x19b2:

      /* opLDAirg_A_AA (ea) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = ram[register_I];
      /* opADDimm_A_AA (21) */
      register_A = (flag_C = ((cmp_old = acc_a0 = register_A) + (cmp_new = 0x1))) & 0xFFF; /* add values, save carry */

      /* opSTAirg_A_A (e6) */
      ram[register_I] = register_A; /* store acc */
      /* opLDAdir_A_AA (ae) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = (register_P << 4) + 0x0e]; /* set I register */

      /* opLDPimm_A_A (81) */
      register_P = 0x1; /* set page register */
      /* opSTAdir_A_A (d2) */
      ram[register_I = 0x12] = register_A; /* store acc to RAM */
      /* opLDPimm_A_A (87) */
      register_P = 0x7; /* set page register */
      /* opLDAdir_A_AA (ac) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = 0x7c]; /* set I register */

      /* opLDPimm_A_A (81) */
      register_P = 0x1; /* set page register */
      /* opSTAdir_A_A (de) */
      ram[register_I = 0x1e] = register_A; /* store acc to RAM */
      /* opLDAimm_A_AA (09) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0x0900;
      /* opADDimmX_A_AA (20) */
      cmp_old = register_A; cmp_new = 0x9d;
      register_A = flag_C = acc_a0 = 0x099d;
      /* opSTAdir_A_A (df) */
      ram[register_I = 0x1f] = 0x99d; /* store acc to RAM */
      /* opLDJimm_A_A (45) */
      register_J = 0x0575;
      /* opJMP_A_A (58) */
      {
        register_PC = 0x1575;
        break;
      };

    case 0x19c3:

      /* Invariants: register_P = 0x0 register_I = 0x00 */;
      /* opLDJimm_A_A (48) */
      register_J = 0x0058;
      /* opLDPimm_A_A (81) */
      register_P = 0x1; /* set page register */
      /* opJPP8_A_B (50) */
      register_PC = 0x0058; /* Jump to other rom bank */
      break;

    case 0x19c7:

      /* Invariants: register_P = 0x0 register_I = 0x00 register_A = 0x1ce */;
      /* opLDAimm_A_AA (0a) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0x0a00;
      /* opADDimmX_A_AA (20) */
      cmp_old = register_A; cmp_new = 0x1b;
      register_A = flag_C = acc_a0 = 0x0a1b;
      /* opLDPimm_A_A (81) */
      register_P = 0x1; /* set page register */
      /* opSTAdir_A_A (d6) */
      ram[register_I = 0x16] = 0xa1b; /* store acc to RAM */
      /* opADDimmX_A_AA (20) */
      cmp_old = register_A; cmp_new = 0x1a;
      register_A = flag_C = acc_a0 = 0x0a35;
      /* opSTAdir_A_A (d7) */
      ram[register_I = 0x17] = 0xa35; /* store acc to RAM */
      /* opLDAimm_A_AA (09) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0x0900;
      /* opADDimmX_A_AA (20) */
      cmp_old = register_A; cmp_new = 0xd7;
      register_A = flag_C = acc_a0 = 0x09d7;
      /* opLDPimm_A_A (82) */
      register_P = 0x2; /* set page register */
      /* opSTAdir_A_A (df) */
      ram[register_I = 0x2f] = 0x9d7; /* store acc to RAM */
      /* opLDJimm_A_A (48) */
      register_J = 0x0128;
      /* opJMP_A_A (58) */
      {
        register_PC = 0x1128;
        break;
      };

    case 0x19d7:

      /* Invariants: register_P = 0x2 register_I = 0x2f register_A = 0xff */;
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opADDimmX_A_AA (20) */
      cmp_old = register_A; cmp_new = 0x7f;
      register_A = flag_C = acc_a0 = 0x007f;
      /* opLDPimm_A_A (87) */
      register_P = 0x7; /* set page register */
      /* opCMPdir_A_AA (bc) */
      cmp_old = acc_a0 = register_A; /* backup old acc */
      flag_C = (((cmp_new = ram[register_I = 0x7c]) ^ 0xFFF) + 1 + register_A);
      /* opANDirg_A_AA (e9) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A &= (cmp_new = ram[register_I]);
      /* opSTAirg_A_A (e6) */
      ram[0x7c] = register_A; /* store acc */
      /* opSUBimmX_A_AA (30) */
      cmp_old = acc_a0 = register_A; /* back up regA */
      register_A = (flag_C = (register_A + (((cmp_new = 0x20) ^ 0xFFF) + 1))) & 0xFFF; /* add */
      /* opLDJimm_A_A (4b) */
      register_J = 0x09eb;
      /* opJNC_A_AA (5d) */
      if (!(flag_C & CARRYBIT)) {
        register_PC = 0x19eb;
        break;
      };
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opADDimmX_A_AA (20) */
      cmp_old = register_A; cmp_new = 0x60;
      register_A = flag_C = acc_a0 = 0x0060;
      /* opCMPdir_A_AA (bc) */
      cmp_old = acc_a0 = register_A; /* backup old acc */
      flag_C = (((cmp_new = ram[register_I = 0x7c]) ^ 0xFFF) + 1 + register_A);
      /* opJNC_A_AA (5d) */
      if (!(flag_C & CARRYBIT)) {
        register_PC = 0x19eb;
        break;
      };
      /* opLDJimm_A_A (4d) */
      register_J = 0x047d;
      /* opJMP_A_A (58) */
      {
        register_PC = 0x147d;
        break;
      };

    case 0x19eb:

      /* Invariants: register_P = 0x7 register_I = 0x7c */;
      /* opLDAimm_A_AA (0a) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0x0a00;
      /* opADDimmX_A_AA (20) */
      cmp_old = register_A; cmp_new = 0x3e;
      register_A = flag_C = acc_a0 = 0x0a3e;
      /* opLDPimm_A_A (81) */
      register_P = 0x1; /* set page register */
      /* opSTAdir_A_A (d6) */
      ram[register_I = 0x16] = 0xa3e; /* store acc to RAM */
      /* opADDimmX_A_AA (20) */
      cmp_old = register_A; cmp_new = 0x32;
      register_A = flag_C = acc_a0 = 0x0a70;
      /* opSTAdir_A_A (d7) */
      ram[register_I = 0x17] = 0xa70; /* store acc to RAM */
      /* opLDAimm_A_AA (09) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0x0900;
      /* opADDimmX_A_AA (20) */
      cmp_old = register_A; cmp_new = 0xfb;
      register_A = flag_C = acc_a0 = 0x09fb;
      /* opLDPimm_A_A (82) */
      register_P = 0x2; /* set page register */
      /* opSTAdir_A_A (df) */
      ram[register_I = 0x2f] = 0x9fb; /* store acc to RAM */
      /* opLDJimm_A_A (48) */
      register_J = 0x0128;
      /* opJMP_A_A (58) */
      {
        register_PC = 0x1128;
        break;
      };

    case 0x19fb:

      /* Invariants: register_P = 0x2 register_I = 0x2f register_A = 0xff */;
      /* opLDAimm_A_AA (0a) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0x0a00;
      /* opADDimmX_A_AA (20) */
      cmp_old = register_A; cmp_new = 0x7a;
      register_A = flag_C = acc_a0 = 0x0a7a;
      /* opLDPimm_A_A (81) */
      register_P = 0x1; /* set page register */
      /* opSTAdir_A_A (d6) */
      ram[register_I = 0x16] = 0xa7a; /* store acc to RAM */
      /* opADDimmX_A_AA (20) */
      cmp_old = register_A; cmp_new = 0x32;
      register_A = flag_C = acc_a0 = 0x0aac;
      /* opSTAdir_A_A (d7) */
      ram[register_I = 0x17] = 0xaac; /* store acc to RAM */
      /* opLDAimm_A_AA (0a) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0x0a00;
      /* opADDimmX_A_AA (20) */
      cmp_old = register_A; cmp_new = 0x0b;
      register_A = flag_C = acc_a0 = 0x0a0b;
      /* opLDPimm_A_A (82) */
      register_P = 0x2; /* set page register */
      /* opSTAdir_A_A (df) */
      ram[register_I = 0x2f] = 0xa0b; /* store acc to RAM */
      /* opLDJimm_A_A (48) */
      register_J = 0x0128;
      /* opJMP_A_A (58) */
      {
        register_PC = 0x1128;
        break;
      };

    case 0x1a0b:

      /* Invariants: register_P = 0x2 register_I = 0x2f register_A = 0xff */;
      /* opLDAimm_A_AA (0a) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0x0a00;
      /* opADDimmX_A_AA (20) */
      cmp_old = register_A; cmp_new = 0xb8;
      register_A = flag_C = acc_a0 = 0x0ab8;
      /* opLDPimm_A_A (81) */
      register_P = 0x1; /* set page register */
      /* opSTAdir_A_A (d6) */
      ram[register_I = 0x16] = 0xab8; /* store acc to RAM */
      /* opADDimmX_A_AA (20) */
      cmp_old = register_A; cmp_new = 0x2f;
      register_A = flag_C = acc_a0 = 0x0ae7;
      /* opSTAdir_A_A (d7) */
      ram[register_I = 0x17] = 0xae7; /* store acc to RAM */
      /* opLDAimm_A_AA (04) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0x0400;
      /* opADDimmX_A_AA (20) */
      cmp_old = register_A; cmp_new = 0x7d;
      register_A = flag_C = acc_a0 = 0x047d;
      /* opLDPimm_A_A (82) */
      register_P = 0x2; /* set page register */
      /* opSTAdir_A_A (df) */
      ram[register_I = 0x2f] = 0x47d; /* store acc to RAM */
      /* opLDJimm_A_A (48) */
      register_J = 0x0128;
      /* opJMP_A_A (58) */
      {
        register_PC = 0x1128;
        break;
      };

    case 0x1af3:

      /* Invariants: register_P = 0x0 register_I = 0x00 */;
      /* opLDAdir_A_AA (a3) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = 0x03]; /* set I register */

      /* opNOP_A_B (57) */
      /* opLDAdir_B_AA (a4) */
      flag_C = acc_a0 = register_A;
      cmp_old = register_B; /* store old acc */
      register_B = cmp_new = ram[register_I = (register_P << 4) + 0x04]; /* new acc value */
      /* opLDJimm_A_A (46) */
      register_J = 0x0af6;
      /* opJDR_A_A (5a) */
      /* opVIN_A_A (f0) */

      FromX = register_A & 0xFFF; /* regA goes to x-coord */
      FromY = register_B & 0xFFF; /* regB goes to y-coord */

      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opSUBdir_A_AA (7e) */
      cmp_old = acc_a0 = register_A;
      register_A = (flag_C = (register_A + ((cmp_new = ram[register_I = 0x0e]) ^ 0xFFF) + 1)) & 0xFFF; /* set regI addr */
      /* opSUBimm_A_AA (31) */
      cmp_old = acc_a0 = register_A;
      register_A = (flag_C = (register_A + (((cmp_new = 0x1) ^ 0xFFF) + 1))) & 0xFFF; /* 1's-comp add */

    case 0x1afd:

      /* Invariants: register_P = 0x0 register_I = 0x0e */;
      /* opADDimm_A_AA (21) */
      register_A = (flag_C = ((cmp_old = acc_a0 = register_A) + (cmp_new = 0x1))) & 0xFFF; /* add values, save carry */

      /* opLDJimm_A_A (4d) */
      register_J = 0x0afd;
      /* opJNC_A_AA (5d) */
      if (!(flag_C & CARRYBIT)) {
        register_PC = 0x1afd;
        break;
      };
      /* opSTAdir_A_A (de) */
      ram[register_I = 0x0e] = register_A; /* store acc to RAM */
      /* opADDimmX_A_AA (20) */
      register_A = (flag_C = ((acc_a0 = cmp_old = register_A) + (cmp_new = 0x0018))) & 0xFFF; /* add values */
      /* opSTAdir_A_A (d6) */
      ram[register_I = 0x06] = register_A; /* store acc to RAM */
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opADDimmX_A_AA (20) */
      cmp_old = register_A; cmp_new = 0xff;
      register_A = flag_C = acc_a0 = 0x00ff;
      /* opSTAdir_A_A (d1) */
      ram[register_I = 0x01] = 0x0ff; /* store acc to RAM */

    case 0x1b09:

      /* Invariants: register_P = 0x0 */;
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opCMPdir_A_AA (bf) */
      cmp_old = acc_a0 = register_A; /* backup old acc */
      flag_C = (((cmp_new = ram[register_I = 0x0f]) ^ 0xFFF) + 1 + register_A);
      /* opLDJimm_A_A (41) */
      register_J = 0x0b21;
      /* opJNC_A_AA (5d) */
      if (!(flag_C & CARRYBIT)) {
        register_PC = 0x1b21;
        break;
      };
      /* opLDAdir_A_AA (a5) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = 0x05]; /* set I register */

      /* opADDimm_A_AA (21) */
      register_A = (flag_C = ((cmp_old = acc_a0 = register_A) + (cmp_new = 0x1))) & 0xFFF; /* add values, save carry */

      /* opSTAirg_A_A (e6) */
      ram[0x05] = register_A; /* store acc */
      /* opXLT_A_AA (e2) */
      cmp_old = register_A; register_A = cmp_new = rom[0x1000 | register_A]; /* new acc value */
    /* opNOP_A_A (5f) */

    case 0x1b13:

      /* Invariants: register_P = 0x0 */;
      /* opSTAdir_A_A (d7) */
      ram[register_I = 0x07] = register_A; /* store acc to RAM */
      /* opCMPdir_A_AA (b1) */
      cmp_old = acc_a0 = register_A; /* backup old acc */
      flag_C = (((cmp_new = ram[register_I = 0x01]) ^ 0xFFF) + 1 + register_A);
      /* opLDJimm_A_A (4b) */
      register_J = 0x0b6b;
      /* opJEQ_A_AA (5c) */
      if (cmp_new == cmp_old) {
        register_PC = 0x1b6b;
        break;
      };
      /* opSUBimmX_A_AA (30) */
      cmp_old = acc_a0 = register_A; /* back up regA */
      register_A = (flag_C = (register_A + (((cmp_new = 0x40) ^ 0xFFF) + 1))) & 0xFFF; /* add */
      /* opLDJimm_A_A (4e) */
      register_J = 0x0b6e;
      /* opJNC_A_AA (5d) */
      if (!(flag_C & CARRYBIT)) {
        register_PC = 0x1b6e;
        break;
      };
      /* opADDimmX_A_AA (20) */
      register_A = (flag_C = ((acc_a0 = cmp_old = register_A) + (cmp_new = 0x0039))) & 0xFFF; /* add values */
      /* opSTAdir_A_A (d7) */
      ram[register_I = 0x07] = register_A; /* store acc to RAM */
      /* opJMP_A_A (58) */
      {
        register_PC = 0x1b6e;
        break;
      };

    case 0x1b21:

      /* Invariants: register_P = 0x0 register_I = 0x0f register_A = 0x00 */;
      /* opLDAdir_A_AA (a2) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = 0x02]; /* set I register */

      /* opSUBimm_A_AA (31) */
      cmp_old = acc_a0 = register_A;
      register_A = (flag_C = (register_A + (((cmp_new = 0x1) ^ 0xFFF) + 1))) & 0xFFF; /* 1's-comp add */
      /* opLDJimm_A_A (4b) */
      register_J = 0x0b6b;
      /* opJEQ_A_AA (5c) */
      if (cmp_new == cmp_old) {
        register_PC = 0x1b6b;
        break;
      };
      /* opSTAirg_A_A (e6) */
      ram[0x02] = register_A; /* store acc */
      /* opLDIdir_A_A (c5) */
      register_I = ram[0x05] & 0xff; /* set new register_I (8 bits) */
      /* opLDAirg_A_AA (ea) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = ram[register_I];
      /* opSTAdir_A_A (d7) */
      ram[register_I = 0x07] = register_A; /* store acc to RAM */
      /* opLDAdir_A_AA (af) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = 0x0f]; /* set I register */

      /* opSUBimm_A_AA (31) */
      cmp_old = acc_a0 = register_A;
      register_A = (flag_C = (register_A + (((cmp_new = 0x1) ^ 0xFFF) + 1))) & 0xFFF; /* 1's-comp add */
      /* opSTAirg_A_A (e6) */
      ram[0x0f] = register_A; /* store acc */
      /* opLDJimm_A_A (43) */
      register_J = 0x0b63;
      /* opJEQ_A_AA (5c) */
      if (cmp_new == cmp_old) {
        register_PC = 0x1b63;
        break;
      };
      /* opSUBimm_A_AA (31) */
      cmp_old = acc_a0 = register_A;
      register_A = (flag_C = (register_A + (((cmp_new = 0x1) ^ 0xFFF) + 1))) & 0xFFF; /* 1's-comp add */
      /* opLDJimm_A_A (4c) */
      register_J = 0x0b3c;
      /* opJEQ_A_AA (5c) */
      if (cmp_new == cmp_old) {
        register_PC = 0x1b3c;
        break;
      };
      /* opLDAdir_A_AA (a7) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = 0x07]; /* set I register */

      /* opLSRe_A_AA (eb) */
      cmp_new = 0x0BEB; cmp_old = acc_a0 = register_A; flag_C = (0x0BEB + register_A);
      register_A >>= 1;
      /* opLSRe_A_AA (eb) */
      cmp_new = 0x0BEB; cmp_old = acc_a0 = register_A; flag_C = (0x0BEB + register_A);
      register_A >>= 1;
      /* opLSRe_A_AA (eb) */
      cmp_new = 0x0BEB; cmp_old = acc_a0 = register_A; flag_C = (0x0BEB + register_A);
      register_A >>= 1;
      /* opLSRe_A_AA (eb) */
      cmp_new = 0x0BEB; cmp_old = acc_a0 = register_A; flag_C = (0x0BEB + register_A);
      register_A >>= 1;
      /* opLDJimm_A_A (4d) */
      register_J = 0x0b3d;
      /* opJMP_A_A (58) */
      {
        register_PC = 0x1b3d;
        break;
      };

    case 0x1b3c:

      /* Invariants: register_P = 0x0 register_I = 0x0f register_A = 0x00 */;
      /* opLDAdir_A_AA (a7) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = 0x07]; /* set I register */


    case 0x1b3d:

      /* Invariants: register_P = 0x0 register_I = 0x07 */;
      /* opLSRe_A_AA (eb) */
      cmp_new = 0x0BEB; cmp_old = acc_a0 = register_A; flag_C = (0x0BEB + register_A);
      register_A >>= 1;
      /* opLSRe_A_AA (eb) */
      cmp_new = 0x0BEB; cmp_old = acc_a0 = register_A; flag_C = (0x0BEB + register_A);
      register_A >>= 1;
      /* opLSRe_A_AA (eb) */
      cmp_new = 0x0BEB; cmp_old = acc_a0 = register_A; flag_C = (0x0BEB + register_A);
      register_A >>= 1;
      /* opLSRe_A_AA (eb) */
      cmp_new = 0x0BEB; cmp_old = acc_a0 = register_A; flag_C = (0x0BEB + register_A);
      register_A >>= 1;
      /* opSTAirg_A_A (e6) */
      ram[0x07] = register_A; /* store acc */

    case 0x1b42:

      /* Invariants: register_P = 0x0 */;
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opADDimm_A_AA (2f) */
      cmp_new = 0xf; cmp_old = acc_a0 = register_A; register_A = flag_C = 0x000f;

      /* opCMPdir_A_AA (b7) */
      cmp_old = acc_a0 = register_A; /* backup old acc */
      flag_C = (((cmp_new = ram[register_I = 0x07]) ^ 0xFFF) + 1 + register_A);
      /* opANDirg_A_AA (e9) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A &= (cmp_new = ram[register_I]);
      /* opSTAirg_A_A (e6) */
      ram[0x07] = register_A; /* store acc */
      /* opLDAdir_A_AA (ae) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = 0x0e]; /* set I register */

      /* opLSRe_A_AA (eb) */
      cmp_new = 0x0BEB; cmp_old = acc_a0 = register_A; flag_C = (0x0BEB + register_A);
      register_A >>= 1;
      /* opLDJimm_A_A (42) */
      register_J = 0x0b52;
      /* opJA0_A_A (5e) */
      if (acc_a0 & 0x01) {
        register_PC = 0x1b52;
        break;
      };

      /* opCMPdir_A_AA (b7) */
      cmp_old = acc_a0 = register_A; /* backup old acc */
      flag_C = (((cmp_new = ram[register_I = 0x07]) ^ 0xFFF) + 1 + register_A);
      /* opLDJimm_A_A (48) */
      register_J = 0x0b58;
      /* opJEQ_A_AA (5c) */
      if (cmp_new == cmp_old) {
        register_PC = 0x1b58;
        break;
      };
      /* opADDimm_A_AA (21) */
      register_A = (flag_C = ((cmp_old = acc_a0 = register_A) + (cmp_new = 0x1))) & 0xFFF; /* add values, save carry */

      /* opSTAdir_A_A (de) */
      ram[register_I = 0x0e] = register_A; /* store acc to RAM */

    case 0x1b52:

      /* Invariants: register_P = 0x0 */;
      /* opLDAdir_A_AA (a7) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = 0x07]; /* set I register */

      /* opADDimmX_A_AA (20) */
      register_A = (flag_C = ((acc_a0 = cmp_old = register_A) + (cmp_new = 0x0030))) & 0xFFF; /* add values */
      /* opLDJimm_A_A (43) */
      register_J = 0x0b13;
      /* opJMP_A_A (58) */
      {
        register_PC = 0x1b13;
        break;
      };

    case 0x1b58:

      /* Invariants: register_P = 0x0 register_I = 0x07 register_A = 0x00 */;
      /* opLDAdir_A_AA (a2) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = 0x02]; /* set I register */

      /* opSUBimm_A_AA (31) */
      cmp_old = acc_a0 = register_A;
      register_A = (flag_C = (register_A + (((cmp_new = 0x1) ^ 0xFFF) + 1))) & 0xFFF; /* 1's-comp add */
      /* opLDJimm_A_A (42) */
      register_J = 0x0b52;
      /* opJEQ_A_AA (5c) */
      if (cmp_new == cmp_old) {
        register_PC = 0x1b52;
        break;
      };
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opADDimmX_A_AA (20) */
      cmp_old = register_A; cmp_new = 0x5b;
      register_A = flag_C = acc_a0 = 0x005b;
      /* opLDJimm_A_A (43) */
      register_J = 0x0b13;
      /* opJMP_A_A (58) */
      {
        register_PC = 0x1b13;
        break;
      };

    case 0x1b63:

      /* Invariants: register_P = 0x0 register_I = 0x0f register_A = 0x00 */;
      /* opADDimm_A_AA (23) */
      cmp_new = 0x3; cmp_old = acc_a0 = register_A; register_A = flag_C = 0x0003;

      /* opSTAdir_A_A (df) */
      ram[register_I = 0x0f] = 0x003; /* store acc to RAM */
      /* opLDAdir_A_AA (a5) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = 0x05]; /* set I register */

      /* opADDimm_A_AA (21) */
      register_A = (flag_C = ((cmp_old = acc_a0 = register_A) + (cmp_new = 0x1))) & 0xFFF; /* add values, save carry */

      /* opSTAirg_A_A (e6) */
      ram[0x05] = register_A; /* store acc */
      /* opLDJimm_A_A (42) */
      register_J = 0x0b42;
      /* opJMP_A_A (58) */
      {
        register_PC = 0x1b42;
        break;
      };

    case 0x1b6b:

      /* Invariants: register_P = 0x0 */;
      /* opCMPdir_A_AA (b0) */
      cmp_old = acc_a0 = register_A; /* backup old acc */
      flag_C = (((cmp_new = ram[register_I = 0x00]) ^ 0xFFF) + 1 + register_A);
      /* opLDJirg_A_A (e1) */
      /* load J reg from value at last dir addr */
      register_J = ram[0x00];
      /* opJMP_A_A (58) */
      {
        register_PC = 0x1000 | register_J;
        break;
      };

    case 0x1b6e:

      /* Invariants: register_P = 0x0 */;
      /* opLDAimm_A_AA (0c) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0x0c00;
      /* opADDimmX_A_AA (20) */
      cmp_old = register_A; cmp_new = 0x29;
      register_A = flag_C = acc_a0 = 0x0c29;
      /* opADDdir_A_AA (67) */
      register_A = (flag_C = ((cmp_old = acc_a0 = register_A) + (cmp_new = ram[register_I = 0x07]))) & 0xFFF; /* do acc operation */
      /* opXLT_A_AA (e2) */
      cmp_old = register_A; register_A = cmp_new = rom[0x1000 | register_A]; /* new acc value */
      /* opNOP_A_A (5f) */
      /* opSTAirg_A_A (e6) */
      ram[register_I] = register_A; /* store acc */
      /* opLDAimm_A_AA (0b) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0x0b00;
      /* opADDimmX_A_AA (20) */
      cmp_old = register_A; cmp_new = 0xef;
      register_A = flag_C = acc_a0 = 0x0bef;
      /* opAWDirg_A_AA (e7) */
      cmp_old = acc_a0 = register_A;
      register_A = (flag_C = (register_A + (cmp_new = ram[register_I]))) & 0xFFF;
      set_watchdog();
      /* opSTAirg_A_A (e6) */
      ram[register_I] = register_A; /* store acc */

    case 0x1b7a:

      /* Invariants: register_P = 0x0 */;
      /* opLDAdir_A_AA (a7) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = 0x07]; /* set I register */

      /* opADDimm_A_AA (21) */
      register_A = (flag_C = ((cmp_old = acc_a0 = register_A) + (cmp_new = 0x1))) & 0xFFF; /* add values, save carry */

      /* opSTAirg_A_A (e6) */
      ram[0x07] = register_A; /* store acc */
      /* opXLT_A_AA (e2) */
      cmp_old = register_A; register_A = cmp_new = rom[0x1000 | register_A]; /* new acc value */
      /* opNOP_A_A (5f) */
      /* opCMPdir_A_AA (b1) */
      cmp_old = acc_a0 = register_A; /* backup old acc */
      flag_C = (((cmp_new = ram[register_I = (register_P << 4) + 0x1]) ^ 0xFFF) + 1 + register_A);
      /* opLDJimm_A_A (41) */
      register_J = 0x0bc1;
      /* opJEQ_A_AA (5c) */
      if (cmp_new == cmp_old) {
        register_PC = 0x1bc1;
        break;
      };
      /* opSTAdir_A_A (dd) */
      ram[register_I = (register_P << 4) + 0xd] = register_A; /* store acc to RAM */
      /* opLSRe_A_AA (eb) */
      cmp_new = 0x0BEB; cmp_old = acc_a0 = register_A; flag_C = (0x0BEB + register_A);
      register_A >>= 1;
      /* opLSRe_A_AA (eb) */
      cmp_new = 0x0BEB; cmp_old = acc_a0 = register_A; flag_C = (0x0BEB + register_A);
      register_A >>= 1;
      /* opLSRe_A_AA (eb) */
      cmp_new = 0x0BEB; cmp_old = acc_a0 = register_A; flag_C = (0x0BEB + register_A);
      register_A >>= 1;
      /* opLSRe_A_AA (eb) */
      cmp_new = 0x0BEB; cmp_old = acc_a0 = register_A; flag_C = (0x0BEB + register_A);
      register_A >>= 1;
      /* opLSLe_A_AA (ec) */
      cmp_new = 0x0CEC;
      cmp_old = acc_a0 = register_A;
      flag_C = (0x0CEC + register_A);
      register_A = (register_A << 1) & 0x0FFF;
      /* opSTAdir_A_A (dc) */
      ram[register_I = (register_P << 4) + 0xc] = register_A; /* store acc to RAM */
      /* opLDAimm_A_AA (0b) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0x0b00;
      /* opADDimmX_A_AA (20) */
      cmp_old = register_A; cmp_new = 0xd2;
      register_A = flag_C = acc_a0 = 0x0bd2;
      /* opAWDirg_A_AA (e7) */
      cmp_old = acc_a0 = register_A;
      register_A = (flag_C = (register_A + (cmp_new = ram[register_I]))) & 0xFFF;
      set_watchdog();
      /* opSTAirg_A_A (e6) */
      ram[register_I] = register_A; /* store acc */
      /* opADDimm_A_AA (21) */
      register_A = (flag_C = ((cmp_old = acc_a0 = register_A) + (cmp_new = 0x1))) & 0xFFF; /* add values, save carry */

      /* opXLT_A_AA (e2) */
      cmp_old = register_A; register_A = cmp_new = rom[0x1000 | register_A]; /* new acc value */
      /* opNOP_A_A (5f) */
      /* opADDdir_A_AA (64) */
      register_A = (flag_C = ((cmp_old = acc_a0 = register_A) + (cmp_new = ram[register_I = (register_P << 4) + 0x4]))) & 0xFFF; /* do acc operation */
      /* opSTAdir_A_A (d9) */
      ram[register_I = (register_P << 4) + 0x9] = register_A; /* store acc to RAM */
      /* opLDAdir_A_AA (ac) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = (register_P << 4) + 0x0c]; /* set I register */

      /* opXLT_A_AA (e2) */
      cmp_old = register_A; register_A = cmp_new = rom[0x1000 | register_A]; /* new acc value */
      /* opNOP_A_A (5f) */
      /* opADDdir_A_AA (63) */
      register_A = (flag_C = ((cmp_old = acc_a0 = register_A) + (cmp_new = ram[register_I = (register_P << 4) + 0x3]))) & 0xFFF; /* do acc operation */
      /* opSTAdir_A_A (d8) */
      ram[register_I = (register_P << 4) + 0x8] = register_A; /* store acc to RAM */
      /* opNOP_A_B (57) */
      /* opLDAdir_B_AA (a9) */
      flag_C = acc_a0 = register_A;
      cmp_old = register_B; /* store old acc */
      register_B = cmp_new = ram[register_I = (register_P << 4) + 0x09]; /* new acc value */
      /* opLDJimm_A_A (4b) */
      register_J = 0x0b9b;
      /* opJDR_A_A (5a) */
      /* opVIN_A_A (f0) */

      FromX = register_A & 0xFFF; /* regA goes to x-coord */
      FromY = register_B & 0xFFF; /* regB goes to y-coord */

      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opADDimm_A_AA (2f) */
      cmp_new = 0xf; cmp_old = acc_a0 = register_A; register_A = flag_C = 0x000f;

      /* opCMPdir_A_AA (bd) */
      cmp_old = acc_a0 = register_A; /* backup old acc */
      flag_C = (((cmp_new = ram[register_I = (register_P << 4) + 0xd]) ^ 0xFFF) + 1 + register_A);
      /* opANDirg_A_AA (e9) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A &= (cmp_new = ram[register_I]);
      /* opLSLe_A_AA (ec) */
      cmp_new = 0x0CEC;
      cmp_old = acc_a0 = register_A;
      flag_C = (0x0CEC + register_A);
      register_A = (register_A << 1) & 0x0FFF;
      /* opSTAirg_A_A (e6) */
      ram[register_I] = register_A; /* store acc */
      /* opLDAimm_A_AA (0b) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0x0b00;
      /* opADDimmX_A_AA (20) */
      cmp_old = register_A; cmp_new = 0xd2;
      register_A = flag_C = acc_a0 = 0x0bd2;
      /* opAWDirg_A_AA (e7) */
      cmp_old = acc_a0 = register_A;
      register_A = (flag_C = (register_A + (cmp_new = ram[register_I]))) & 0xFFF;
      set_watchdog();
      /* opSTAirg_A_A (e6) */
      ram[register_I] = register_A; /* store acc */
      /* opADDimm_A_AA (21) */
      register_A = (flag_C = ((cmp_old = acc_a0 = register_A) + (cmp_new = 0x1))) & 0xFFF; /* add values, save carry */

      /* opXLT_A_AA (e2) */
      cmp_old = register_A; register_A = cmp_new = rom[0x1000 | register_A]; /* new acc value */
      /* opNOP_A_A (5f) */
      /* opADDdir_A_AA (64) */
      register_A = (flag_C = ((cmp_old = acc_a0 = register_A) + (cmp_new = ram[register_I = (register_P << 4) + 0x4]))) & 0xFFF; /* do acc operation */
      /* opSUBdir_A_AA (79) */
      cmp_old = acc_a0 = register_A;
      register_A = (flag_C = (register_A + ((cmp_new = ram[register_I = (register_P << 4) + 0x9]) ^ 0xFFF) + 1)) & 0xFFF; /* set regI addr */
      /* opSTAdir_A_A (db) */
      ram[register_I = (register_P << 4) + 0xb] = register_A; /* store acc to RAM */
      /* opLDAdir_A_AA (ad) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = (register_P << 4) + 0x0d]; /* set I register */

      /* opXLT_A_AA (e2) */
      cmp_old = register_A; register_A = cmp_new = rom[0x1000 | register_A]; /* new acc value */
      /* opNOP_A_A (5f) */
      /* opADDdir_A_AA (63) */
      register_A = (flag_C = ((cmp_old = acc_a0 = register_A) + (cmp_new = ram[register_I = (register_P << 4) + 0x3]))) & 0xFFF; /* do acc operation */
      /* opSUBdir_A_AA (78) */
      cmp_old = acc_a0 = register_A;
      register_A = (flag_C = (register_A + ((cmp_new = ram[register_I = (register_P << 4) + 0x8]) ^ 0xFFF) + 1)) & 0xFFF; /* set regI addr */
      /* opSTAdir_A_A (da) */
      ram[register_I = (register_P << 4) + 0xa] = register_A; /* store acc to RAM */
      /* opLDAdir_A_AA (aa) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = (register_P << 4) + 0x0a]; /* set I register */

      /* opNOP_A_B (57) */
      /* opLDAdir_B_AA (ab) */
      flag_C = acc_a0 = register_A;
      cmp_old = register_B; /* store old acc */
      register_B = cmp_new = ram[register_I = (register_P << 4) + 0x0b]; /* new acc value */
      /* opLLT_A_AA (e4) */
      { CINEBYTE temp_byte = 0;
        for (;;) {
          if (   (((register_A >> 8) & 0x0A) && (((register_A >> 8) & 0x0A) ^ 0x0A))
                 ||   (((register_B >> 8) & 0x0A) && (((register_B >> 8) & 0x0A) ^ 0x0A))  ) break;
          register_A <<= 1; register_B <<= 1;
          if (!(++temp_byte)) break /* This may not be correct */;
        }
        vgShiftLength = temp_byte & 0xfff; register_A &= 0x0FFF; register_B &= 0x0FFF;
      }
      /* opADDdir_A_AA (68) */
      register_A = (flag_C = ((cmp_old = acc_a0 = register_A) + (cmp_new = ram[register_I = (register_P << 4) + 0x8]))) & 0xFFF; /* do acc operation */
      /* opNOP_A_B (57) */
      /* opADDdir_B_AA (69) */
      acc_a0 = register_A; /* store old acc value */
      register_B = (flag_C = ((cmp_old = register_B) + (cmp_new = ram[register_I = (register_P << 4) + 0x09]))) & 0xFFF; /* do acc operation */
      /* opVDR_A_A (e0) */
      {
        /* set ending points and draw the vector, or buffer for a later draw. */
        int ToX = register_A & 0xFFF;
        int ToY = register_B & 0xFFF;

        /* Sign extend from 20 bit CCPU to 32bit target machine */
        FromX = SEX(FromX);
        ToX = SEX(ToX);
        FromY = SEX(FromY);
        ToY = SEX(ToY);

        /* figure out the vector */
        ToX -= FromX;
        ToX = ((signed short int)(((signed short int)ToX) >> (signed short int)vgShiftLength)) /* SAR */;
        ToX += FromX;

        ToY -= FromY;
        ToY = ((signed short int)(((signed short int)ToY) >> (signed short int)vgShiftLength)) /* SAR */;
        ToY += FromY;

        /* render the line */
#ifndef DUALCPU
        CinemaVectorData (FromX, FromY, ToX, ToY, vgColour);
#endif

      }
      /* opLDJimm_A_A (4a) */
      register_J = 0x0b7a;
      /* opJMP_A_A (58) */
      {
        register_PC = 0x1b7a;
        break;
      };

    case 0x1bc1:

      /* Invariants: register_P = 0x0 register_I = 0x01 register_A = 0xff */;
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opADDimm_A_AA (29) */
      cmp_new = 0x9; cmp_old = acc_a0 = register_A; register_A = flag_C = 0x0009;

      /* opADDdir_A_AA (64) */
      register_A = (flag_C = ((cmp_old = acc_a0 = register_A) + (cmp_new = ram[register_I = 0x04]))) & 0xFFF; /* do acc operation */
      /* opSTAdir_A_A (d9) */
      ram[register_I = 0x09] = register_A; /* store acc to RAM */
      /* opLDAdir_A_AA (a6) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = 0x06]; /* set I register */

      /* opADDdir_A_AA (63) */
      register_A = (flag_C = ((cmp_old = acc_a0 = register_A) + (cmp_new = ram[register_I = 0x03]))) & 0xFFF; /* do acc operation */
      /* opSTAirg_A_A (e6) */
      ram[0x03] = register_A; /* store acc */
      /* opADDimm_A_AA (23) */
      register_A = (flag_C = ((cmp_old = acc_a0 = register_A) + (cmp_new = 0x3))) & 0xFFF; /* add values, save carry */

      /* opNOP_A_B (57) */
      /* opLDAdir_B_AA (a9) */
      flag_C = acc_a0 = register_A;
      cmp_old = register_B; /* store old acc */
      register_B = cmp_new = ram[register_I = (register_P << 4) + 0x09]; /* new acc value */
      /* opLDJimm_A_A (4b) */
      register_J = 0x0bcb;
      /* opJDR_A_A (5a) */
      /* opVIN_A_A (f0) */

      FromX = register_A & 0xFFF; /* regA goes to x-coord */
      FromY = register_B & 0xFFF; /* regB goes to y-coord */

      /* opLDJimm_A_A (49) */
      register_J = 0x0b09;
      /* opJMP_A_A (58) */
      {
        register_PC = 0x1b09;
        break;
      };

    default:
      /* Jumping to any illegal address or end of eprom will come here */
      /* we ought to reinitialise or something */
      ;
  }
}

// ops1400.c

void cineExecute1400(void)
{
  switch (register_PC)
  {
    case 0x141e:

      /* opLDPimm_A_A (80) */
      register_P = 0x0; /* set page register */
      /* opLDAdir_A_AA (a5) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = 0x05]; /* set I register */

      /* opNOP_A_B (57) */
      /* opLDAdir_B_AA (a6) */
      flag_C = acc_a0 = register_A;
      cmp_old = register_B; /* store old acc */
      register_B = cmp_new = ram[register_I = (register_P << 4) + 0x06]; /* new acc value */
      /* opLDJimm_A_A (42) */
      register_J = 0x0422;
      /* opJDR_A_A (5a) */
      /* opVIN_A_A (f0) */

      FromX = register_A & 0xFFF; /* regA goes to x-coord */
      FromY = register_B & 0xFFF; /* regB goes to y-coord */

      /* opLDAimm_A_AA (08) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0x0800;
      /* opLDAdir_A_AA (a7) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = 0x07]; /* set I register */

      /* opLDJimm_A_A (4d) */
      register_J = 0x042d;
      /* opJLT_A_A (5b) */
      if (cmp_new < cmp_old) {
        register_PC = 0x142d;
        break;
      };
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opSUBirg_A_AA (e8) */
      register_A = (flag_C = ((cmp_old = acc_a0 = register_A) + ((cmp_new = ram[register_I]) ^ 0xFFF) + 1)) & 0xFFF; /* ones compliment */

    case 0x142d:

      /* Invariants: register_P = 0x0 register_I = 0x07 */;
      /* opSTAdir_A_A (dc) */
      ram[register_I = 0x0c] = register_A; /* store acc to RAM */
      /* opSTAdir_A_A (de) */
      ram[register_I = 0x0e] = register_A; /* store acc to RAM */
      /* opLDAimm_A_AA (08) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0x0800;
      /* opLDAdir_A_AA (a8) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = 0x08]; /* set I register */

      /* opLDJimm_A_A (46) */
      register_J = 0x0436;
      /* opJLT_A_A (5b) */
      if (cmp_new < cmp_old) {
        register_PC = 0x1436;
        break;
      };
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opSUBirg_A_AA (e8) */
      register_A = (flag_C = ((cmp_old = acc_a0 = register_A) + ((cmp_new = ram[register_I]) ^ 0xFFF) + 1)) & 0xFFF; /* ones compliment */

    case 0x1436:

      /* Invariants: register_P = 0x0 register_I = 0x08 */;
      /* opSTAdir_A_A (df) */
      ram[register_I = 0x0f] = register_A; /* store acc to RAM */
      /* opADDdir_A_AA (6e) */
      register_A = (flag_C = ((cmp_old = acc_a0 = register_A) + (cmp_new = ram[register_I = 0x0e]))) & 0xFFF; /* do acc operation */
      /* opSTAirg_A_A (e6) */
      ram[0x0e] = register_A; /* store acc */
      /* opSUBimm_A_AA (33) */
      cmp_old = acc_a0 = register_A;
      register_A = (flag_C = (register_A + (((cmp_new = 0x3) ^ 0xFFF) + 1))) & 0xFFF; /* 1's-comp add */
      /* opLDJimm_A_A (44) */
      register_J = 0x0454;
      /* opJNC_A_AA (5d) */
      if (!(flag_C & CARRYBIT)) {
        register_PC = 0x1454;
        break;
      };
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opSUBimmX_A_AA (30) */
      cmp_old = acc_a0 = register_A; /* back up regA */
      register_A = (flag_C = (register_A + (((cmp_new = 0x08) ^ 0xFFF) + 1))) & 0xFFF; /* add */

    case 0x1440:

      /* Invariants: register_P = 0x0 register_I = 0x0e */;
      /* opADDimm_A_AA (21) */
      register_A = (flag_C = ((cmp_old = acc_a0 = register_A) + (cmp_new = 0x1))) & 0xFFF; /* add values, save carry */

      /* opLDJimm_A_A (40) */
      register_J = 0x0440;
      /* opJNC_A_AA (5d) */
      if (!(flag_C & CARRYBIT)) {
        register_PC = 0x1440;
        break;
      };
      /* opLDAimm_A_AA (02) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0x0200;
      /* opSUBimm_A_AA (37) */
      cmp_old = acc_a0 = register_A;
      register_A = (flag_C = (register_A + (((cmp_new = 0x7) ^ 0xFFF) + 1))) & 0xFFF; /* 1's-comp add */
      /* opCMPdir_A_AA (bf) */
      cmp_old = acc_a0 = register_A; /* backup old acc */
      flag_C = (((cmp_new = ram[register_I = 0x0f]) ^ 0xFFF) + 1 + register_A);
      /* opLDJimm_A_A (4b) */
      register_J = 0x045b;
      /* opJNC_A_AA (5d) */
      if (!(flag_C & CARRYBIT)) {
        register_PC = 0x145b;
        break;
      };
      /* opCMPdir_A_AA (bc) */
      cmp_old = acc_a0 = register_A; /* backup old acc */
      flag_C = (((cmp_new = ram[register_I = 0x0c]) ^ 0xFFF) + 1 + register_A);
      /* opJNC_A_AA (5d) */
      if (!(flag_C & CARRYBIT)) {
        register_PC = 0x145b;
        break;
      };
      /* opLDAdir_A_AA (a7) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = 0x07]; /* set I register */

      /* opNOP_A_B (57) */
      /* opLDAdir_B_AA (a8) */
      flag_C = acc_a0 = register_A;
      cmp_old = register_B; /* store old acc */
      register_B = cmp_new = ram[register_I = (register_P << 4) + 0x08]; /* new acc value */
      /* opLLT_A_AA (e4) */
      { CINEBYTE temp_byte = 0;
        for (;;) {
          if (   (((register_A >> 8) & 0x0A) && (((register_A >> 8) & 0x0A) ^ 0x0A))
                 ||   (((register_B >> 8) & 0x0A) && (((register_B >> 8) & 0x0A) ^ 0x0A))  ) break;
          register_A <<= 1; register_B <<= 1;
          if (!(++temp_byte)) break /* This may not be correct */;
        }
        vgShiftLength = temp_byte & 0xfff; register_A &= 0x0FFF; register_B &= 0x0FFF;
      }
      /* opADDdir_A_AA (65) */
      register_A = (flag_C = ((cmp_old = acc_a0 = register_A) + (cmp_new = ram[register_I = 0x05]))) & 0xFFF; /* do acc operation */
      /* opNOP_A_B (57) */
      /* opADDdir_B_AA (66) */
      acc_a0 = register_A; /* store old acc value */
      register_B = (flag_C = ((cmp_old = register_B) + (cmp_new = ram[register_I = 0x06]))) & 0xFFF; /* do acc operation */
      /* opVDR_A_A (e0) */
      {
        /* set ending points and draw the vector, or buffer for a later draw. */
        int ToX = register_A & 0xFFF;
        int ToY = register_B & 0xFFF;

        /* Sign extend from 20 bit CCPU to 32bit target machine */
        FromX = SEX(FromX);
        ToX = SEX(ToX);
        FromY = SEX(FromY);
        ToY = SEX(ToY);

        /* figure out the vector */
        ToX -= FromX;
        ToX = ((signed short int)(((signed short int)ToX) >> (signed short int)vgShiftLength)) /* SAR */;
        ToX += FromX;

        ToY -= FromY;
        ToY = ((signed short int)(((signed short int)ToY) >> (signed short int)vgShiftLength)) /* SAR */;
        ToY += FromY;

        /* render the line */
#ifndef DUALCPU
        CinemaVectorData (FromX, FromY, ToX, ToY, vgColour);
#endif

      }

    case 0x1454:

      /* Invariants: register_P = 0x0 */;
      /* opLDPimm_A_A (81) */
      register_P = 0x1; /* set page register */
      /* opLDAdir_A_AA (a7) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = 0x17]; /* set I register */

      /* opADDimm_A_AA (21) */
      register_A = (flag_C = ((cmp_old = acc_a0 = register_A) + (cmp_new = 0x1))) & 0xFFF; /* add values, save carry */

      /* opSTAirg_A_A (e6) */
      ram[0x17] = register_A; /* store acc */
      /* opLDJimm_A_A (4c) */
      register_J = 0x039c;
      /* opJMP_A_A (58) */
      {
        register_PC = 0x139c;
        break;
      };

    case 0x145b:

      /* opNOP_A_B (57) */
      /* opLDAdir_B_AA (a8) */
      flag_C = acc_a0 = register_A;
      cmp_old = register_B; /* store old acc */
      register_B = cmp_new = ram[register_I = (register_P << 4) + 0x08]; /* new acc value */
      /* opASRDe_A_AA (ee) */
      cmp_new = 0x0EEE; cmp_old = acc_a0 = register_A; flag_C = (0x0EEE + register_A);
      register_A = (register_A >> 1) | ((register_B & 1) << 11);
      register_B = (register_B >> 1) | (register_B & 0x800);
      /* opNOP_A_B (57) */
      /* opSTAirg_B_BB (e6) */
      ram[register_I] = register_B; /* store acc */
      /* opLDAdir_A_AA (a7) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = (register_P << 4) + 0x07]; /* set I register */

      /* opASRe_A_AA (ed) */
      cmp_new = 0xDED; cmp_old = flag_C = acc_a0 = register_A;
      register_A = SEX(register_A); /* make signed */
      register_A = (((signed short int)(((signed short int)register_A) >> (signed short int)1)) /* SAR */) & 0xFFF;
      /* opSTAirg_A_A (e6) */
      ram[register_I] = register_A; /* store acc */
      /* opLLT_A_AA (e4) */
      { CINEBYTE temp_byte = 0;
        for (;;) {
          if (   (((register_A >> 8) & 0x0A) && (((register_A >> 8) & 0x0A) ^ 0x0A))
                 ||   (((register_B >> 8) & 0x0A) && (((register_B >> 8) & 0x0A) ^ 0x0A))  ) break;
          register_A <<= 1; register_B <<= 1;
          if (!(++temp_byte)) break /* This may not be correct */;
        }
        vgShiftLength = temp_byte & 0xfff; register_A &= 0x0FFF; register_B &= 0x0FFF;
      }
      /* opADDdir_A_AA (65) */
      register_A = (flag_C = ((cmp_old = acc_a0 = register_A) + (cmp_new = ram[register_I = (register_P << 4) + 0x5]))) & 0xFFF; /* do acc operation */
      /* opNOP_A_B (57) */
      /* opADDdir_B_AA (66) */
      acc_a0 = register_A; /* store old acc value */
      register_B = (flag_C = ((cmp_old = register_B) + (cmp_new = ram[register_I = (register_P << 4) + 0x06]))) & 0xFFF; /* do acc operation */
      /* opVDR_A_A (e0) */
      {
        /* set ending points and draw the vector, or buffer for a later draw. */
        int ToX = register_A & 0xFFF;
        int ToY = register_B & 0xFFF;

        /* Sign extend from 20 bit CCPU to 32bit target machine */
        FromX = SEX(FromX);
        ToX = SEX(ToX);
        FromY = SEX(FromY);
        ToY = SEX(ToY);

        /* figure out the vector */
        ToX -= FromX;
        ToX = ((signed short int)(((signed short int)ToX) >> (signed short int)vgShiftLength)) /* SAR */;
        ToX += FromX;

        ToY -= FromY;
        ToY = ((signed short int)(((signed short int)ToY) >> (signed short int)vgShiftLength)) /* SAR */;
        ToY += FromY;

        /* render the line */
#ifndef DUALCPU
        CinemaVectorData (FromX, FromY, ToX, ToY, vgColour);
#endif

      }
      /* opLDAdir_A_AA (a8) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = (register_P << 4) + 0x08]; /* set I register */

      /* opADDdir_A_AA (66) */
      register_A = (flag_C = ((cmp_old = acc_a0 = register_A) + (cmp_new = ram[register_I = (register_P << 4) + 0x6]))) & 0xFFF; /* do acc operation */
      /* opSTAirg_A_A (e6) */
      ram[register_I] = register_A; /* store acc */
      /* opLDAdir_A_AA (a7) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = (register_P << 4) + 0x07]; /* set I register */

      /* opADDdir_A_AA (65) */
      register_A = (flag_C = ((cmp_old = acc_a0 = register_A) + (cmp_new = ram[register_I = (register_P << 4) + 0x5]))) & 0xFFF; /* do acc operation */
      /* opSTAirg_A_A (e6) */
      ram[register_I] = register_A; /* store acc */
      /* opLDJimm_A_A (4e) */
      register_J = 0x041e;
      /* opJMP_A_A (58) */
      {
        register_PC = 0x141e;
        break;
      };

    case 0x1471:

      /* opLDPimm_A_A (80) */
      register_P = 0x0; /* set page register */
      /* opLDAdir_A_AA (a3) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = 0x03]; /* set I register */

      /* opADDimm_A_AA (21) */
      register_A = (flag_C = ((cmp_old = acc_a0 = register_A) + (cmp_new = 0x1))) & 0xFFF; /* add values, save carry */

      /* opSTAirg_A_A (e6) */
      ram[0x03] = register_A; /* store acc */
      /* opLDPimm_A_A (81) */
      register_P = 0x1; /* set page register */
      /* opLDJimm_A_A (4c) */
      register_J = 0x039c;
      /* opJMP_A_A (58) */
      {
        register_PC = 0x139c;
        break;
      };

    case 0x1479:

      /* opLDPimm_A_A (82) */
      register_P = 0x2; /* set page register */
      /* opCMPdir_A_AA (bf) */
      cmp_old = acc_a0 = register_A; /* backup old acc */
      flag_C = (((cmp_new = ram[register_I = 0x2f]) ^ 0xFFF) + 1 + register_A);
      /* opLDJirg_A_A (e1) */
      /* load J reg from value at last dir addr */
      register_J = ram[0x2f];
      /* opJMP_A_A (58) */
      {
        register_PC = 0x1000 | register_J;
        break;
      };

    case 0x147d:

      /* opLDPimm_A_A (80) */
      register_P = 0x0; /* set page register */
      /* opCMPdir_A_AA (b0) */
      cmp_old = acc_a0 = register_A; /* backup old acc */
      flag_C = (((cmp_new = ram[register_I = 0x00]) ^ 0xFFF) + 1 + register_A);
      /* opLDJirg_A_A (e1) */
      /* load J reg from value at last dir addr */
      register_J = ram[0x00];
      /* opJMP_A_A (58) */
      {
        register_PC = 0x1000 | register_J;
        break;
      };

    case 0x1575:

      /* Invariants: register_P = 0x1 register_I = 0x1f */;
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opADDimmX_A_AA (20) */
      cmp_old = register_A; cmp_new = 0x7f;
      register_A = flag_C = acc_a0 = 0x007f;
      /* opCMPdir_A_AA (be) */
      cmp_old = acc_a0 = register_A; /* backup old acc */
      flag_C = (((cmp_new = ram[register_I = 0x1e]) ^ 0xFFF) + 1 + register_A);
      /* opANDirg_A_AA (e9) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A &= (cmp_new = ram[register_I]);
      /* opSTAirg_A_A (e6) */
      ram[0x1e] = register_A; /* store acc */
      /* opLDAimm_A_AA (05) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0x0500;
      /* opADDimmX_A_AA (20) */
      cmp_old = register_A; cmp_new = 0xf6;
      register_A = flag_C = acc_a0 = 0x05f6;
      /* opSTAdir_A_A (da) */
      ram[register_I = 0x1a] = 0x5f6; /* store acc to RAM */
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opADDimmX_A_AA (20) */
      cmp_old = register_A; cmp_new = 0x20;
      register_A = flag_C = acc_a0 = 0x0020;
      /* opCMPdir_A_AA (be) */
      cmp_old = acc_a0 = register_A; /* backup old acc */
      flag_C = (((cmp_new = ram[register_I = 0x1e]) ^ 0xFFF) + 1 + register_A);
      /* opLDJimm_A_A (4f) */
      register_J = 0x059f;
      /* opJLT_A_A (5b) */
      if (cmp_new < cmp_old) {
        register_PC = 0x159f;
        break;
      };
      /* opLSLe_A_AA (ec) */
      cmp_new = 0x0CEC;
      cmp_old = acc_a0 = register_A;
      flag_C = (0x0CEC + register_A);
      register_A = (register_A << 1) & 0x0FFF;
      /* opCMPdir_A_AA (be) */
      cmp_old = acc_a0 = register_A; /* backup old acc */
      flag_C = (((cmp_new = ram[register_I = 0x1e]) ^ 0xFFF) + 1 + register_A);
      /* opLDJimm_A_A (4b) */
      register_J = 0x05ab;
      /* opJLT_A_A (5b) */
      if (cmp_new < cmp_old) {
        register_PC = 0x15ab;
        break;
      };
      /* opADDimmX_A_AA (20) */
      register_A = (flag_C = ((acc_a0 = cmp_old = register_A) + (cmp_new = 0x0020))) & 0xFFF; /* add values */
      /* opCMPdir_A_AA (be) */
      cmp_old = acc_a0 = register_A; /* backup old acc */
      flag_C = (((cmp_new = ram[register_I = 0x1e]) ^ 0xFFF) + 1 + register_A);
      /* opLDJimm_A_A (4f) */
      register_J = 0x05bf;
      /* opJLT_A_A (5b) */
      if (cmp_new < cmp_old) {
        register_PC = 0x15bf;
        break;
      };
      /* opLDAdir_A_AA (aa) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = 0x1a]; /* set I register */

      /* opADDdir_A_AA (6e) */
      register_A = (flag_C = ((cmp_old = acc_a0 = register_A) + (cmp_new = ram[register_I = 0x1e]))) & 0xFFF; /* do acc operation */
      /* opSUBimmX_A_AA (30) */
      cmp_old = acc_a0 = register_A; /* back up regA */
      register_A = (flag_C = (register_A + (((cmp_new = 0x80) ^ 0xFFF) + 1))) & 0xFFF; /* add */
      /* opXLT_A_AA (e2) */
      cmp_old = register_A; register_A = cmp_new = rom[0x1000 | register_A]; /* new acc value */
      /* opNOP_A_A (5f) */
      /* opSTAdir_A_A (db) */
      ram[register_I = (register_P << 4) + 0xb] = register_A; /* store acc to RAM */
      /* opLDAdir_A_AA (aa) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = (register_P << 4) + 0x0a]; /* set I register */

      /* opADDimmX_A_AA (20) */
      register_A = (flag_C = ((acc_a0 = cmp_old = register_A) + (cmp_new = 0x0060))) & 0xFFF; /* add values */
      /* opSUBdir_A_AA (7e) */
      cmp_old = acc_a0 = register_A;
      register_A = (flag_C = (register_A + ((cmp_new = ram[register_I = (register_P << 4) + 0xe]) ^ 0xFFF) + 1)) & 0xFFF; /* set regI addr */
      /* opLDJimm_A_A (4d) */
      register_J = 0x05cd;
      /* opJMP_A_A (58) */
      {
        register_PC = 0x15cd;
        break;
      };

    case 0x159f:

      /* Invariants: register_P = 0x1 register_I = 0x1e register_A = 0x20 */;
      /* opLDAdir_A_AA (aa) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = 0x1a]; /* set I register */

      /* opSUBdir_A_AA (7e) */
      cmp_old = acc_a0 = register_A;
      register_A = (flag_C = (register_A + ((cmp_new = ram[register_I = 0x1e]) ^ 0xFFF) + 1)) & 0xFFF; /* set regI addr */
      /* opXLT_A_AA (e2) */
      cmp_old = register_A; register_A = cmp_new = rom[0x1000 | register_A]; /* new acc value */
      /* opNOP_A_A (5f) */
      /* opSTAdir_A_A (db) */
      ram[register_I = (register_P << 4) + 0xb] = register_A; /* store acc to RAM */
      /* opLDAimm_A_AA (05) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0x0500;
      /* opADDimmX_A_AA (20) */
      cmp_old = register_A; cmp_new = 0xd6;
      register_A = flag_C = acc_a0 = 0x05d6;
      /* opADDdir_A_AA (6e) */
      register_A = (flag_C = ((cmp_old = acc_a0 = register_A) + (cmp_new = ram[register_I = (register_P << 4) + 0xe]))) & 0xFFF; /* do acc operation */
      /* opLDJimm_A_A (49) */
      register_J = 0x05b9;
      /* opJMP_A_A (58) */
      {
        register_PC = 0x15b9;
        break;
      };

    case 0x15ab:

      /* Invariants: register_P = 0x1 register_I = 0x1e register_A = 0x40 */;
      /* opLDAdir_A_AA (aa) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = 0x1a]; /* set I register */

      /* opADDdir_A_AA (6e) */
      register_A = (flag_C = ((cmp_old = acc_a0 = register_A) + (cmp_new = ram[register_I = 0x1e]))) & 0xFFF; /* do acc operation */
      /* opSUBimmX_A_AA (30) */
      cmp_old = acc_a0 = register_A; /* back up regA */
      register_A = (flag_C = (register_A + (((cmp_new = 0x40) ^ 0xFFF) + 1))) & 0xFFF; /* add */
      /* opXLT_A_AA (e2) */
      cmp_old = register_A; register_A = cmp_new = rom[0x1000 | register_A]; /* new acc value */
      /* opNOP_A_A (5f) */
      /* opSTAdir_A_A (db) */
      ram[register_I = (register_P << 4) + 0xb] = register_A; /* store acc to RAM */
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opSUBirg_A_AA (e8) */
      register_A = (flag_C = ((cmp_old = acc_a0 = register_A) + ((cmp_new = ram[register_I]) ^ 0xFFF) + 1)) & 0xFFF; /* ones compliment */
      /* opSTAirg_A_A (e6) */
      ram[register_I] = register_A; /* store acc */
      /* opLDAdir_A_AA (aa) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = (register_P << 4) + 0x0a]; /* set I register */

      /* opADDimmX_A_AA (20) */
      register_A = (flag_C = ((acc_a0 = cmp_old = register_A) + (cmp_new = 0x0020))) & 0xFFF; /* add values */
      /* opSUBdir_A_AA (7e) */
      cmp_old = acc_a0 = register_A;
      register_A = (flag_C = (register_A + ((cmp_new = ram[register_I = (register_P << 4) + 0xe]) ^ 0xFFF) + 1)) & 0xFFF; /* set regI addr */

    case 0x15b9:

      /* Invariants: register_P = 0x1 register_I = 0x1e */;
      /* opXLT_A_AA (e2) */
      cmp_old = register_A; register_A = cmp_new = rom[0x1000 | register_A]; /* new acc value */
      /* opNOP_A_A (5f) */
      /* opSTAdir_A_A (da) */
      ram[register_I = (register_P << 4) + 0xa] = register_A; /* store acc to RAM */
      /* opLDJimm_A_A (43) */
      register_J = 0x05d3;
      /* opJMP_A_A (58) */
      {
        register_PC = 0x15d3;
        break;
      };

    case 0x15bf:

      /* Invariants: register_P = 0x1 register_I = 0x1e register_A = 0x60 */;
      /* opLDAdir_A_AA (aa) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = 0x1a]; /* set I register */

      /* opADDimmX_A_AA (20) */
      register_A = (flag_C = ((acc_a0 = cmp_old = register_A) + (cmp_new = 0x0040))) & 0xFFF; /* add values */
      /* opSUBdir_A_AA (7e) */
      cmp_old = acc_a0 = register_A;
      register_A = (flag_C = (register_A + ((cmp_new = ram[register_I = 0x1e]) ^ 0xFFF) + 1)) & 0xFFF; /* set regI addr */
      /* opXLT_A_AA (e2) */
      cmp_old = register_A; register_A = cmp_new = rom[0x1000 | register_A]; /* new acc value */
      /* opNOP_A_A (5f) */
      /* opSTAdir_A_A (db) */
      ram[register_I = (register_P << 4) + 0xb] = register_A; /* store acc to RAM */
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opSUBirg_A_AA (e8) */
      register_A = (flag_C = ((cmp_old = acc_a0 = register_A) + ((cmp_new = ram[register_I]) ^ 0xFFF) + 1)) & 0xFFF; /* ones compliment */
      /* opSTAirg_A_A (e6) */
      ram[register_I] = register_A; /* store acc */
      /* opLDAdir_A_AA (aa) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = (register_P << 4) + 0x0a]; /* set I register */

      /* opADDdir_A_AA (6e) */
      register_A = (flag_C = ((cmp_old = acc_a0 = register_A) + (cmp_new = ram[register_I = (register_P << 4) + 0xe]))) & 0xFFF; /* do acc operation */
      /* opSUBimmX_A_AA (30) */
      cmp_old = acc_a0 = register_A; /* back up regA */
      register_A = (flag_C = (register_A + (((cmp_new = 0x60) ^ 0xFFF) + 1))) & 0xFFF; /* add */

    case 0x15cd:

      /* Invariants: register_P = 0x1 register_I = 0x1e */;
      /* opXLT_A_AA (e2) */
      cmp_old = register_A; register_A = cmp_new = rom[0x1000 | register_A]; /* new acc value */
      /* opNOP_A_A (5f) */
      /* opSTAdir_A_A (da) */
      ram[register_I = (register_P << 4) + 0xa] = register_A; /* store acc to RAM */
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opSUBirg_A_AA (e8) */
      register_A = (flag_C = ((cmp_old = acc_a0 = register_A) + ((cmp_new = ram[register_I]) ^ 0xFFF) + 1)) & 0xFFF; /* ones compliment */
      /* opSTAirg_A_A (e6) */
      ram[register_I] = register_A; /* store acc */

    case 0x15d3:

      /* Invariants: register_P = 0x1 register_I = 0x1a */;
      /* opCMPdir_A_AA (bf) */
      cmp_old = acc_a0 = register_A; /* backup old acc */
      flag_C = (((cmp_new = ram[register_I = 0x1f]) ^ 0xFFF) + 1 + register_A);
      /* opLDJirg_A_A (e1) */
      /* load J reg from value at last dir addr */
      register_J = ram[0x1f];
      /* opJMP_A_A (58) */
      {
        register_PC = 0x1000 | register_J;
        break;
      };

    case 0x1600:

      /* Invariants: register_P = 0x0 register_I = 0x00 */;
      /* opNOP_A_A (5f) */
      /* opNOP_A_B (57) */
      /* opLDAimm_B_AA (02) */
      flag_C = acc_a0 = register_A;
      cmp_old = register_B; /* step back cmp flag */
      register_B = cmp_new = 0x0200;
      /* opNOP_A_B (57) */
      /* opADDimmX_B_AA (20) */
      cmp_old = register_B; acc_a0 = register_A; /* save old accA bit0 */
      cmp_new = 0x76;
      register_B = flag_C = 0x0276; cmp_new = 0x76; /* No carry */
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opADDimmX_A_AA (20) */
      cmp_old = register_A; cmp_new = 0xa0;
      register_A = flag_C = acc_a0 = 0x00a0;
      /* opLDJimm_A_A (4b) */
      register_J = 0x060b;
      /* opJDR_A_A (5a) */
      /* opVIN_A_A (f0) */

      FromX = register_A & 0xFFF; /* regA goes to x-coord */
      FromY = register_B & 0xFFF; /* regB goes to y-coord */

      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opSUBimmX_A_AA (30) */
      cmp_old = acc_a0 = register_A; /* back up regA */
      register_A = (flag_C = (register_A + (((cmp_new = 0x38) ^ 0xFFF) + 1))) & 0xFFF; /* add */

    case 0x1610:

      /* Invariants: register_P = 0x0 register_I = 0x00 */;
      /* opADDimm_A_AA (21) */
      register_A = (flag_C = ((cmp_old = acc_a0 = register_A) + (cmp_new = 0x1))) & 0xFFF; /* add values, save carry */

      /* opLDJimm_A_A (40) */
      register_J = 0x0610;
      /* opJNC_A_AA (5d) */
      if (!(flag_C & CARRYBIT)) {
        register_PC = 0x1610;
        break;
      };
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opLDPimm_A_A (86) */
      register_P = 0x6; /* set page register */
      /* opCMPdir_A_AA (b9) */
      cmp_old = acc_a0 = register_A; /* backup old acc */
      flag_C = (((cmp_new = ram[register_I = 0x69]) ^ 0xFFF) + 1 + register_A);
      /* opLDJimm_A_A (4a) */
      register_J = 0x063a;
      /* opJNC_A_AA (5d) */
      if (!(flag_C & CARRYBIT)) {
        register_PC = 0x163a;
        break;
      };
      /* opOUTbi_A_A (96) */
      vgColour = 0x07;
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opADDimmX_A_AA (20) */
      cmp_old = register_A; cmp_new = 0x96;
      register_A = flag_C = acc_a0 = 0x0096;
      /* opLDPimm_A_A (80) */
      register_P = 0x0; /* set page register */
      /* opSTAdir_A_A (d3) */
      ram[register_I = 0x03] = 0x096; /* store acc to RAM */
      /* opLDAimm_A_AA (02) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0x0200;
      /* opADDimmX_A_AA (20) */
      cmp_old = register_A; cmp_new = 0x8a;
      register_A = flag_C = acc_a0 = 0x028a;
      /* opSTAdir_A_A (d4) */
      ram[register_I = 0x04] = 0x28a; /* store acc to RAM */
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opADDimmX_A_AA (20) */
      cmp_old = register_A; cmp_new = 0x11;
      register_A = flag_C = acc_a0 = 0x0011;
      /* opSTAdir_A_A (de) */
      ram[register_I = 0x0e] = 0x011; /* store acc to RAM */
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opSTAdir_A_A (df) */
      ram[register_I = 0x0f] = 0x000; /* store acc to RAM */
      /* opLDAimm_A_AA (06) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0x0600;
      /* opADDimmX_A_AA (20) */
      cmp_old = register_A; cmp_new = 0x33;
      register_A = flag_C = acc_a0 = 0x0633;
      /* opSTAdir_A_A (d5) */
      ram[register_I = 0x05] = 0x633; /* store acc to RAM */
      /* opADDimmX_A_AA (20) */
      cmp_old = register_A; cmp_new = 0x07;
      register_A = flag_C = acc_a0 = 0x063a;
      /* opSTAdir_A_A (d0) */
      ram[register_I = 0x00] = 0x63a; /* store acc to RAM */
      /* opLDJimm_A_A (43) */
      register_J = 0x0af3;
      /* opJMP_A_A (58) */
      {
        register_PC = 0x1af3;
        break;
      };

    case 0x163a:

      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opLDPimm_A_A (87) */
      register_P = 0x7; /* set page register */
      /* opLDAdir_A_AA (a1) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = 0x71]; /* set I register */

      /* opLDJimm_A_A (44) */
      register_J = 0x0644;
      /* opJEQ_A_AA (5c) */
      if (cmp_new == cmp_old) {
        register_PC = 0x1644;
        break;
      };
      /* opSUBimm_A_AA (31) */
      cmp_old = acc_a0 = register_A;
      register_A = (flag_C = (register_A + (((cmp_new = 0x1) ^ 0xFFF) + 1))) & 0xFFF; /* 1's-comp add */
      /* opSTAirg_A_A (e6) */
      ram[0x71] = register_A; /* store acc */
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opADDimm_A_AA (21) */
      cmp_new = 0x1; cmp_old = acc_a0 = register_A; register_A = flag_C = 0x0001;


    case 0x1644:

      /* Invariants: register_P = 0x7 register_I = 0x71 */;
      /* opOUTbi_A_A (96) */
      vgColour = register_A & 0x01 ? 0x0f : 0x07;
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opADDimmX_A_AA (20) */
      cmp_old = register_A; cmp_new = 0x10;
      register_A = flag_C = acc_a0 = 0x0010;
      /* opOUTbi_A_A (93) */
      /* opOUTsnd_A (93) */
      set_sound_data(0);
      /* opLSRe_A_AA (eb) */
      cmp_new = 0x0BEB; cmp_old = acc_a0 = register_A; flag_C = (0x0BEB + register_A);
      register_A >>= 1;
      /* opOUTbi_A_A (90) */
      /* opOUTsnd_A (90) */
      set_sound_addr_A(register_A & 1);
      /* opLSRe_A_AA (eb) */
      cmp_new = 0x0BEB; cmp_old = acc_a0 = register_A; flag_C = (0x0BEB + register_A);
      register_A >>= 1;
      /* opOUTbi_A_A (91) */
      /* opOUTsnd_A (91) */
      set_sound_addr_B(register_A & 1);
      /* opLSRe_A_AA (eb) */
      cmp_new = 0x0BEB; cmp_old = acc_a0 = register_A; flag_C = (0x0BEB + register_A);
      register_A >>= 1;
      /* opOUTbi_A_A (92) */
      /* opOUTsnd_A (92) */
      set_sound_addr_C(register_A & 1);
      /* opLSRe_A_AA (eb) */
      cmp_new = 0x0BEB; cmp_old = acc_a0 = register_A; flag_C = (0x0BEB + register_A);
      register_A >>= 1;
      /* opOUTbi_A_A (94) */
      /* opOUTsnd_A (94) */
      if (register_A & 1) strobe_sound_on(); else strobe_sound_off();
      /* opLSRe_A_AA (eb) */
      cmp_new = 0x0BEB; cmp_old = acc_a0 = register_A; flag_C = (0x0BEB + register_A);
      register_A >>= 1;
      /* opOUTbi_A_A (94) */
      /* opOUTsnd_A (94) */
      if (register_A & 1) strobe_sound_on(); else strobe_sound_off();
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opADDimmX_A_AA (20) */
      cmp_old = register_A; cmp_new = 0xa0;
      register_A = flag_C = acc_a0 = 0x00a0;
      /* opLDPimm_A_A (80) */
      register_P = 0x0; /* set page register */
      /* opSTAdir_A_A (d3) */
      ram[register_I = 0x03] = 0x0a0; /* store acc to RAM */
      /* opLDAimm_A_AA (02) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0x0200;
      /* opADDimmX_A_AA (20) */
      cmp_old = register_A; cmp_new = 0x58;
      register_A = flag_C = acc_a0 = 0x0258;
      /* opSTAdir_A_A (d4) */
      ram[register_I = 0x04] = 0x258; /* store acc to RAM */
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opADDimmX_A_AA (20) */
      cmp_old = register_A; cmp_new = 0x07;
      register_A = flag_C = acc_a0 = 0x0007;
      /* opSTAdir_A_A (de) */
      ram[register_I = 0x0e] = 0x007; /* store acc to RAM */
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opADDimm_A_AA (23) */
      cmp_new = 0x3; cmp_old = acc_a0 = register_A; register_A = flag_C = 0x0003;

      /* opSTAdir_A_A (df) */
      ram[register_I = 0x0f] = 0x003; /* store acc to RAM */
      /* opADDimmX_A_AA (20) */
      cmp_old = register_A; cmp_new = 0x01;
      register_A = flag_C = acc_a0 = 0x0004;
      /* opSTAdir_A_A (d2) */
      ram[register_I = 0x02] = 0x004; /* store acc to RAM */
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opADDimmX_A_AA (20) */
      cmp_old = register_A; cmp_new = 0x6a;
      register_A = flag_C = acc_a0 = 0x006a;
      /* opSTAdir_A_A (d5) */
      ram[register_I = 0x05] = 0x06a; /* store acc to RAM */
      /* opLDAimm_A_AA (06) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0x0600;
      /* opADDimmX_A_AA (20) */
      cmp_old = register_A; cmp_new = 0x71;
      register_A = flag_C = acc_a0 = 0x0671;
      /* opSTAdir_A_A (d0) */
      ram[register_I = 0x00] = 0x671; /* store acc to RAM */
      /* opLDJimm_A_A (43) */
      register_J = 0x0af3;
      /* opJMP_A_A (58) */
      {
        register_PC = 0x1af3;
        break;
      };

    case 0x1671:

      /* Invariants: register_P = 0x0 register_I = 0x00 register_A = 0x00 */;
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opADDimmX_A_AA (20) */
      cmp_old = register_A; cmp_new = 0xb8;
      register_A = flag_C = acc_a0 = 0x00b8;
      /* opLDPimm_A_A (80) */
      /* opSTAdir_A_A (d3) */
      ram[register_I = 0x03] = 0x0b8; /* store acc to RAM */
      /* opLDAimm_A_AA (02) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0x0200;
      /* opADDimmX_A_AA (20) */
      cmp_old = register_A; cmp_new = 0x58;
      register_A = flag_C = acc_a0 = 0x0258;
      /* opSTAdir_A_A (d4) */
      ram[register_I = 0x04] = 0x258; /* store acc to RAM */
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opADDimmX_A_AA (20) */
      cmp_old = register_A; cmp_new = 0x07;
      register_A = flag_C = acc_a0 = 0x0007;
      /* opSTAdir_A_A (de) */
      ram[register_I = 0x0e] = 0x007; /* store acc to RAM */
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opADDimm_A_AA (23) */
      cmp_new = 0x3; cmp_old = acc_a0 = register_A; register_A = flag_C = 0x0003;

      /* opSTAdir_A_A (df) */
      ram[register_I = 0x0f] = 0x003; /* store acc to RAM */
      /* opADDimmX_A_AA (20) */
      cmp_old = register_A; cmp_new = 0x01;
      register_A = flag_C = acc_a0 = 0x0004;
      /* opSTAdir_A_A (d2) */
      ram[register_I = 0x02] = 0x004; /* store acc to RAM */
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opADDimmX_A_AA (20) */
      cmp_old = register_A; cmp_new = 0x6b;
      register_A = flag_C = acc_a0 = 0x006b;
      /* opSTAdir_A_A (d5) */
      ram[register_I = 0x05] = 0x06b; /* store acc to RAM */
      /* opLDAimm_A_AA (06) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0x0600;
      /* opADDimmX_A_AA (20) */
      cmp_old = register_A; cmp_new = 0x8f;
      register_A = flag_C = acc_a0 = 0x068f;
      /* opSTAdir_A_A (d0) */
      ram[register_I = 0x00] = 0x68f; /* store acc to RAM */
      /* opLDJimm_A_A (43) */
      register_J = 0x0af3;
      /* opJMP_A_A (58) */
      {
        register_PC = 0x1af3;
        break;
      };

    case 0x168f:

      /* Invariants: register_P = 0x0 register_I = 0x00 register_A = 0x00 */;
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opLDPimm_A_A (82) */
      register_P = 0x2; /* set page register */
      /* opLDAdir_A_AA (a0) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = 0x20]; /* set I register */

      /* opLDJimm_A_A (4a) */
      register_J = 0x069a;
      /* opJEQ_A_AA (5c) */
      if (cmp_new == cmp_old) {
        register_PC = 0x169a;
        break;
      };
      /* opSUBimm_A_AA (31) */
      cmp_old = acc_a0 = register_A;
      register_A = (flag_C = (register_A + (((cmp_new = 0x1) ^ 0xFFF) + 1))) & 0xFFF; /* 1's-comp add */
      /* opSTAirg_A_A (e6) */
      ram[0x20] = register_A; /* store acc */
      /* opLDJimm_A_A (44) */
      register_J = 0x0704;
      /* opJMP_A_A (58) */
      {
        register_PC = 0x1704;
        break;
      };

    case 0x169a:

      /* Invariants: register_P = 0x2 register_I = 0x20 register_A = 0x00 */;
      /* opLDAdir_A_AA (a5) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = 0x25]; /* set I register */

      /* opLDPimm_A_A (83) */
      register_P = 0x3; /* set page register */
      /* opSTAdir_A_A (df) */
      ram[register_I = 0x3f] = register_A; /* store acc to RAM */
      /* opADDimm_A_AA (23) */
      register_A = (flag_C = ((cmp_old = acc_a0 = register_A) + (cmp_new = 0x3))) & 0xFFF; /* add values, save carry */

      /* opLDPimm_A_A (84) */
      register_P = 0x4; /* set page register */
      /* opSTAdir_A_A (df) */
      ram[register_I = 0x4f] = register_A; /* store acc to RAM */
      /* opADDimm_A_AA (23) */
      register_A = (flag_C = ((cmp_old = acc_a0 = register_A) + (cmp_new = 0x3))) & 0xFFF; /* add values, save carry */

      /* opLDPimm_A_A (85) */
      register_P = 0x5; /* set page register */
      /* opSTAdir_A_A (df) */
      ram[register_I = 0x5f] = register_A; /* store acc to RAM */
      /* opADDimm_A_AA (24) */
      register_A = (flag_C = ((cmp_old = acc_a0 = register_A) + (cmp_new = 0x4))) & 0xFFF; /* add values, save carry */

      /* opLDPimm_A_A (82) */
      register_P = 0x2; /* set page register */
      /* opSTAdir_A_A (d5) */
      ram[register_I = 0x25] = register_A; /* store acc to RAM */
      /* opSUBimm_A_AA (31) */
      cmp_old = acc_a0 = register_A;
      register_A = (flag_C = (register_A + (((cmp_new = 0x1) ^ 0xFFF) + 1))) & 0xFFF; /* 1's-comp add */
      /* opXLT_A_AA (e2) */
      cmp_old = register_A; register_A = cmp_new = rom[0x1000 | register_A]; /* new acc value */
      /* opNOP_A_A (5f) */
      /* opSTAdir_A_A (d0) */
      ram[register_I = (register_P << 4) + 0x0] = register_A; /* store acc to RAM */
      /* opSUBimmX_A_AA (30) */
      cmp_old = acc_a0 = register_A; /* back up regA */
      register_A = (flag_C = (register_A + (((cmp_new = 0xff) ^ 0xFFF) + 1))) & 0xFFF; /* add */
      /* opLDJimm_A_A (43) */
      register_J = 0x06b3;
      /* opJNC_A_AA (5d) */
      if (!(flag_C & CARRYBIT)) {
        register_PC = 0x16b3;
        break;
      };
      /* opLDAimm_A_AA (02) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0x0200;
      /* opADDimmX_A_AA (20) */
      cmp_old = register_A; cmp_new = 0xff;
      register_A = flag_C = acc_a0 = 0x02ff;
      /* opSTAirg_A_A (e6) */
      ram[register_I] = register_A; /* store acc */

    case 0x16b3:

      /* Invariants: register_P = 0x2 register_I = 0x20 */;
      /* opLDPimm_A_A (83) */
      register_P = 0x3; /* set page register */
      /* opLDAdir_A_AA (af) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = 0x3f]; /* set I register */

      /* opXLT_A_AA (e2) */
      cmp_old = register_A; register_A = cmp_new = rom[0x1000 | register_A]; /* new acc value */
      /* opNOP_A_A (5f) */
      /* opLSLe_A_AA (ec) */
      cmp_new = 0x0CEC;
      cmp_old = acc_a0 = register_A;
      flag_C = (0x0CEC + register_A);
      register_A = (register_A << 1) & 0x0FFF;
      /* opLSLe_A_AA (ec) */
      cmp_new = 0x0CEC;
      cmp_old = acc_a0 = register_A;
      flag_C = (0x0CEC + register_A);
      register_A = (register_A << 1) & 0x0FFF;
      /* opLSLe_A_AA (ec) */
      cmp_new = 0x0CEC;
      cmp_old = acc_a0 = register_A;
      flag_C = (0x0CEC + register_A);
      register_A = (register_A << 1) & 0x0FFF;
      /* opLSLe_A_AA (ec) */
      cmp_new = 0x0CEC;
      cmp_old = acc_a0 = register_A;
      flag_C = (0x0CEC + register_A);
      register_A = (register_A << 1) & 0x0FFF;
      /* opSTAdir_A_A (d8) */
      ram[register_I = (register_P << 4) + 0x8] = register_A; /* store acc to RAM */
      /* opLDAdir_A_AA (af) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = (register_P << 4) + 0x0f]; /* set I register */

      /* opADDimm_A_AA (21) */
      register_A = (flag_C = ((cmp_old = acc_a0 = register_A) + (cmp_new = 0x1))) & 0xFFF; /* add values, save carry */

      /* opXLT_A_AA (e2) */
      cmp_old = register_A; register_A = cmp_new = rom[0x1000 | register_A]; /* new acc value */
      /* opNOP_A_A (5f) */
      /* opLSLe_A_AA (ec) */
      cmp_new = 0x0CEC;
      cmp_old = acc_a0 = register_A;
      flag_C = (0x0CEC + register_A);
      register_A = (register_A << 1) & 0x0FFF;
      /* opLSLe_A_AA (ec) */
      cmp_new = 0x0CEC;
      cmp_old = acc_a0 = register_A;
      flag_C = (0x0CEC + register_A);
      register_A = (register_A << 1) & 0x0FFF;
      /* opLSLe_A_AA (ec) */
      cmp_new = 0x0CEC;
      cmp_old = acc_a0 = register_A;
      flag_C = (0x0CEC + register_A);
      register_A = (register_A << 1) & 0x0FFF;
      /* opLSLe_A_AA (ec) */
      cmp_new = 0x0CEC;
      cmp_old = acc_a0 = register_A;
      flag_C = (0x0CEC + register_A);
      register_A = (register_A << 1) & 0x0FFF;
      /* opSTAdir_A_A (d9) */
      ram[register_I = (register_P << 4) + 0x9] = register_A; /* store acc to RAM */
      /* opLDAdir_A_AA (af) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = (register_P << 4) + 0x0f]; /* set I register */

      /* opADDimm_A_AA (22) */
      register_A = (flag_C = ((cmp_old = acc_a0 = register_A) + (cmp_new = 0x2))) & 0xFFF; /* add values, save carry */

      /* opXLT_A_AA (e2) */
      cmp_old = register_A; register_A = cmp_new = rom[0x1000 | register_A]; /* new acc value */
      /* opNOP_A_A (5f) */
      /* opLSLe_A_AA (ec) */
      cmp_new = 0x0CEC;
      cmp_old = acc_a0 = register_A;
      flag_C = (0x0CEC + register_A);
      register_A = (register_A << 1) & 0x0FFF;
      /* opLSLe_A_AA (ec) */
      cmp_new = 0x0CEC;
      cmp_old = acc_a0 = register_A;
      flag_C = (0x0CEC + register_A);
      register_A = (register_A << 1) & 0x0FFF;
      /* opLSLe_A_AA (ec) */
      cmp_new = 0x0CEC;
      cmp_old = acc_a0 = register_A;
      flag_C = (0x0CEC + register_A);
      register_A = (register_A << 1) & 0x0FFF;
      /* opLSLe_A_AA (ec) */
      cmp_new = 0x0CEC;
      cmp_old = acc_a0 = register_A;
      flag_C = (0x0CEC + register_A);
      register_A = (register_A << 1) & 0x0FFF;
      /* opSTAdir_A_A (da) */
      ram[register_I = (register_P << 4) + 0xa] = register_A; /* store acc to RAM */
      /* opLDPimm_A_A (84) */
      register_P = 0x4; /* set page register */
      /* opLDAdir_A_AA (af) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = 0x4f]; /* set I register */

      /* opXLT_A_AA (e2) */
      cmp_old = register_A; register_A = cmp_new = rom[0x1000 | register_A]; /* new acc value */
      /* opNOP_A_A (5f) */
      /* opLSLe_A_AA (ec) */
      cmp_new = 0x0CEC;
      cmp_old = acc_a0 = register_A;
      flag_C = (0x0CEC + register_A);
      register_A = (register_A << 1) & 0x0FFF;
      /* opLSLe_A_AA (ec) */
      cmp_new = 0x0CEC;
      cmp_old = acc_a0 = register_A;
      flag_C = (0x0CEC + register_A);
      register_A = (register_A << 1) & 0x0FFF;
      /* opLSLe_A_AA (ec) */
      cmp_new = 0x0CEC;
      cmp_old = acc_a0 = register_A;
      flag_C = (0x0CEC + register_A);
      register_A = (register_A << 1) & 0x0FFF;
      /* opLSLe_A_AA (ec) */
      cmp_new = 0x0CEC;
      cmp_old = acc_a0 = register_A;
      flag_C = (0x0CEC + register_A);
      register_A = (register_A << 1) & 0x0FFF;
      /* opSTAdir_A_A (d8) */
      ram[register_I = (register_P << 4) + 0x8] = register_A; /* store acc to RAM */
      /* opLDAdir_A_AA (af) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = (register_P << 4) + 0x0f]; /* set I register */

      /* opADDimm_A_AA (21) */
      register_A = (flag_C = ((cmp_old = acc_a0 = register_A) + (cmp_new = 0x1))) & 0xFFF; /* add values, save carry */

      /* opXLT_A_AA (e2) */
      cmp_old = register_A; register_A = cmp_new = rom[0x1000 | register_A]; /* new acc value */
      /* opNOP_A_A (5f) */
      /* opLSLe_A_AA (ec) */
      cmp_new = 0x0CEC;
      cmp_old = acc_a0 = register_A;
      flag_C = (0x0CEC + register_A);
      register_A = (register_A << 1) & 0x0FFF;
      /* opLSLe_A_AA (ec) */
      cmp_new = 0x0CEC;
      cmp_old = acc_a0 = register_A;
      flag_C = (0x0CEC + register_A);
      register_A = (register_A << 1) & 0x0FFF;
      /* opLSLe_A_AA (ec) */
      cmp_new = 0x0CEC;
      cmp_old = acc_a0 = register_A;
      flag_C = (0x0CEC + register_A);
      register_A = (register_A << 1) & 0x0FFF;
      /* opLSLe_A_AA (ec) */
      cmp_new = 0x0CEC;
      cmp_old = acc_a0 = register_A;
      flag_C = (0x0CEC + register_A);
      register_A = (register_A << 1) & 0x0FFF;
      /* opSTAdir_A_A (d9) */
      ram[register_I = (register_P << 4) + 0x9] = register_A; /* store acc to RAM */
      /* opLDAdir_A_AA (af) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = (register_P << 4) + 0x0f]; /* set I register */

      /* opADDimm_A_AA (22) */
      register_A = (flag_C = ((cmp_old = acc_a0 = register_A) + (cmp_new = 0x2))) & 0xFFF; /* add values, save carry */

      /* opXLT_A_AA (e2) */
      cmp_old = register_A; register_A = cmp_new = rom[0x1000 | register_A]; /* new acc value */
      /* opNOP_A_A (5f) */
      /* opLSLe_A_AA (ec) */
      cmp_new = 0x0CEC;
      cmp_old = acc_a0 = register_A;
      flag_C = (0x0CEC + register_A);
      register_A = (register_A << 1) & 0x0FFF;
      /* opLSLe_A_AA (ec) */
      cmp_new = 0x0CEC;
      cmp_old = acc_a0 = register_A;
      flag_C = (0x0CEC + register_A);
      register_A = (register_A << 1) & 0x0FFF;
      /* opLSLe_A_AA (ec) */
      cmp_new = 0x0CEC;
      cmp_old = acc_a0 = register_A;
      flag_C = (0x0CEC + register_A);
      register_A = (register_A << 1) & 0x0FFF;
      /* opLSLe_A_AA (ec) */
      cmp_new = 0x0CEC;
      cmp_old = acc_a0 = register_A;
      flag_C = (0x0CEC + register_A);
      register_A = (register_A << 1) & 0x0FFF;
      /* opSTAdir_A_A (da) */
      ram[register_I = (register_P << 4) + 0xa] = register_A; /* store acc to RAM */
      /* opLDPimm_A_A (85) */
      register_P = 0x5; /* set page register */
      /* opLDAdir_A_AA (af) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = 0x5f]; /* set I register */

      /* opXLT_A_AA (e2) */
      cmp_old = register_A; register_A = cmp_new = rom[0x1000 | register_A]; /* new acc value */
      /* opNOP_A_A (5f) */
      /* opLSLe_A_AA (ec) */
      cmp_new = 0x0CEC;
      cmp_old = acc_a0 = register_A;
      flag_C = (0x0CEC + register_A);
      register_A = (register_A << 1) & 0x0FFF;
      /* opLSLe_A_AA (ec) */
      cmp_new = 0x0CEC;
      cmp_old = acc_a0 = register_A;
      flag_C = (0x0CEC + register_A);
      register_A = (register_A << 1) & 0x0FFF;
      /* opLSLe_A_AA (ec) */
      cmp_new = 0x0CEC;
      cmp_old = acc_a0 = register_A;
      flag_C = (0x0CEC + register_A);
      register_A = (register_A << 1) & 0x0FFF;
      /* opLSLe_A_AA (ec) */
      cmp_new = 0x0CEC;
      cmp_old = acc_a0 = register_A;
      flag_C = (0x0CEC + register_A);
      register_A = (register_A << 1) & 0x0FFF;
      /* opSTAdir_A_A (d8) */
      ram[register_I = (register_P << 4) + 0x8] = register_A; /* store acc to RAM */
      /* opLDAdir_A_AA (af) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = (register_P << 4) + 0x0f]; /* set I register */

      /* opADDimm_A_AA (21) */
      register_A = (flag_C = ((cmp_old = acc_a0 = register_A) + (cmp_new = 0x1))) & 0xFFF; /* add values, save carry */

      /* opXLT_A_AA (e2) */
      cmp_old = register_A; register_A = cmp_new = rom[0x1000 | register_A]; /* new acc value */
      /* opNOP_A_A (5f) */
      /* opLSLe_A_AA (ec) */
      cmp_new = 0x0CEC;
      cmp_old = acc_a0 = register_A;
      flag_C = (0x0CEC + register_A);
      register_A = (register_A << 1) & 0x0FFF;
      /* opLSLe_A_AA (ec) */
      cmp_new = 0x0CEC;
      cmp_old = acc_a0 = register_A;
      flag_C = (0x0CEC + register_A);
      register_A = (register_A << 1) & 0x0FFF;
      /* opLSLe_A_AA (ec) */
      cmp_new = 0x0CEC;
      cmp_old = acc_a0 = register_A;
      flag_C = (0x0CEC + register_A);
      register_A = (register_A << 1) & 0x0FFF;
      /* opLSLe_A_AA (ec) */
      cmp_new = 0x0CEC;
      cmp_old = acc_a0 = register_A;
      flag_C = (0x0CEC + register_A);
      register_A = (register_A << 1) & 0x0FFF;
      /* opSTAdir_A_A (d9) */
      ram[register_I = (register_P << 4) + 0x9] = register_A; /* store acc to RAM */
      /* opLDAdir_A_AA (af) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = (register_P << 4) + 0x0f]; /* set I register */

      /* opADDimm_A_AA (22) */
      register_A = (flag_C = ((cmp_old = acc_a0 = register_A) + (cmp_new = 0x2))) & 0xFFF; /* add values, save carry */

      /* opXLT_A_AA (e2) */
      cmp_old = register_A; register_A = cmp_new = rom[0x1000 | register_A]; /* new acc value */
      /* opNOP_A_A (5f) */
      /* opLSLe_A_AA (ec) */
      cmp_new = 0x0CEC;
      cmp_old = acc_a0 = register_A;
      flag_C = (0x0CEC + register_A);
      register_A = (register_A << 1) & 0x0FFF;
      /* opLSLe_A_AA (ec) */
      cmp_new = 0x0CEC;
      cmp_old = acc_a0 = register_A;
      flag_C = (0x0CEC + register_A);
      register_A = (register_A << 1) & 0x0FFF;
      /* opLSLe_A_AA (ec) */
      cmp_new = 0x0CEC;
      cmp_old = acc_a0 = register_A;
      flag_C = (0x0CEC + register_A);
      register_A = (register_A << 1) & 0x0FFF;
      /* opLSLe_A_AA (ec) */
      cmp_new = 0x0CEC;
      cmp_old = acc_a0 = register_A;
      flag_C = (0x0CEC + register_A);
      register_A = (register_A << 1) & 0x0FFF;
      /* opSTAdir_A_A (da) */
      ram[register_I = (register_P << 4) + 0xa] = register_A; /* store acc to RAM */

    case 0x1704:

      /* Invariants: register_P = 0xdeadbeef */;
      /* opLDPimm_A_A (88) */
      register_P = 0x8; /* set page register */
      /* opLDAdir_A_AA (a9) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = 0x89]; /* set I register */

      /* opLDPimm_A_A (87) */
      register_P = 0x7; /* set page register */
      /* opSTAdir_A_A (da) */
      ram[register_I = 0x7a] = register_A; /* store acc to RAM */
      /* opLDAimm_A_AA (08) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0x0800;
      /* opLDPimm_A_A (87) */
      /* opSTAdir_A_A (d8) */
      ram[register_I = 0x78] = 0x800; /* store acc to RAM */
      /* opNOP_A_B (57) */
      /* opINP_B_AA (17) */
      flag_C = acc_a0 = register_A;
      cmp_old = register_B; /* save old accB */
#ifdef RAWIO
      register_B = cmp_new = (( ioSwitches >> 0x7 ) & 0x01);
#else
      register_B = cmp_new = get_coin_state(); /* apparently not used in tailgunner??? */
#endif
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opADDimm_A_AA (21) */
      cmp_new = 0x1; cmp_old = acc_a0 = register_A; register_A = flag_C = 0x0001;

      /* opOUTbi_A_A (95) */
      /* opOUTsnd_A (95) */
      reset_coin_counter(1);
      /* opASRDe_A_AA (ee) */
      cmp_new = 0x0EEE; cmp_old = acc_a0 = register_A; flag_C = (0x0EEE + register_A);
      register_A = (register_A >> 1) | ((register_B & 1) << 11);
      register_B = (register_B >> 1) | (register_B & 0x800);
      /* opOUTbi_A_A (95) */
      /* opOUTsnd_A (95) */
      reset_coin_counter(register_A & 1);
      /* opCMPdir_A_AA (b8) */
      cmp_old = acc_a0 = register_A; /* backup old acc */
      flag_C = (((cmp_new = ram[register_I = 0x78]) ^ 0xFFF) + 1 + register_A);
      /* opLDJimm_A_A (4e) */
      register_J = 0x072e;
      /* opJEQ_A_AA (5c) */
      if (cmp_new == cmp_old) {
        register_PC = 0x172e;
        break;
      };
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opADDimm_A_AA (21) */
      cmp_new = 0x1; cmp_old = acc_a0 = register_A; register_A = flag_C = 0x0001;

      /* opSTAirg_A_A (e6) */
      ram[0x78] = 0x001; /* store acc */
      /* opNOP_A_B (57) */
      /* opINP_B_AA (10) */
      flag_C = acc_a0 = register_A;
      cmp_old = register_B; /* save old accB */
#ifdef RAWIO
      register_B = cmp_new = (( ioSwitches >> 0x0 ) & 0x01);
#else
      register_B = cmp_new = get_quarters_per_game(); /* 1 => 1q/game, 0 => 2q/game */
#endif
      /* opNOP_A_B (57) */
      /* opCMPdir_B_AA (b8) */
      acc_a0 = register_A;
      flag_C = ((((cmp_new = ram[register_I = (register_P << 4) + 0x8]) ^ 0xFFF) + 1) + (cmp_old = register_B)); /* ones compliment */
      /* opLDJimm_A_A (4b) */
      register_J = 0x072b;
      /* opJEQ_A_AA (5c) */
      if (cmp_new == cmp_old) {
        register_PC = 0x172b;
        break;
      };
      /* opLDAdir_A_AA (a9) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = 0x79]; /* set I register */

      /* opADDimm_A_AA (21) */
      register_A = (flag_C = ((cmp_old = acc_a0 = register_A) + (cmp_new = 0x1))) & 0xFFF; /* add values, save carry */

      /* opSTAirg_A_A (e6) */
      ram[0x79] = register_A; /* store acc */
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opADDimm_A_AA (21) */
      cmp_new = 0x1; cmp_old = acc_a0 = register_A; register_A = flag_C = 0x0001;

      /* opCMPdir_A_AA (b9) */
      cmp_old = acc_a0 = register_A; /* backup old acc */
      flag_C = (((cmp_new = ram[register_I = 0x79]) ^ 0xFFF) + 1 + register_A);
      /* opLDJimm_A_A (4e) */
      register_J = 0x072e;
      /* opJEQ_A_AA (5c) */
      if (cmp_new == cmp_old) {
        register_PC = 0x172e;
        break;
      };
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opSTAirg_A_A (e6) */
      ram[0x79] = 0x000; /* store acc */

    case 0x172b:

      /* Invariants: register_P = 0x7 register_I = 0x78 register_A = 0x01 */;
      /* opLDAdir_A_AA (aa) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = 0x7a]; /* set I register */

      /* opADDimm_A_AA (21) */
      register_A = (flag_C = ((cmp_old = acc_a0 = register_A) + (cmp_new = 0x1))) & 0xFFF; /* add values, save carry */

      /* opSTAirg_A_A (e6) */
      ram[0x7a] = register_A; /* store acc */

    case 0x172e:

      /* Invariants: register_P = 0x7 */;
      /* opLDPimm_A_A (87) */
      /* opLDAdir_A_AA (aa) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = 0x7a]; /* set I register */

      /* opLDPimm_A_A (88) */
      register_P = 0x8; /* set page register */
      /* opSTAdir_A_A (d9) */
      ram[register_I = 0x89] = register_A; /* store acc to RAM */
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opLDPimm_A_A (87) */
      register_P = 0x7; /* set page register */
      /* opSTAdir_A_A (da) */
      ram[register_I = 0x7a] = 0x000; /* store acc to RAM */
      /* opLDPimm_A_A (86) */
      register_P = 0x6; /* set page register */
      /* opCMPdir_A_AA (b9) */
      cmp_old = acc_a0 = register_A; /* backup old acc */
      flag_C = (((cmp_new = ram[register_I = 0x69]) ^ 0xFFF) + 1 + register_A);
      /* opLDJimm_A_A (49) */
      register_J = 0x07b9;
      /* opJEQ_A_AA (5c) */
      if (cmp_new == cmp_old) {
        register_PC = 0x17b9;
        break;
      };
      /* opOUTbi_A_A (96) */
      vgColour = 0x07;
      /* opLDPimm_A_A (88) */
      register_P = 0x8; /* set page register */
      /* opLDAdir_A_AA (a1) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = 0x81]; /* set I register */

      /* opLDJimm_A_A (49) */
      register_J = 0x0749;
      /* opJEQ_A_AA (5c) */
      if (cmp_new == cmp_old) {
        register_PC = 0x1749;
        break;
      };
      /* opSUBimm_A_AA (3a) */
      cmp_old = acc_a0 = register_A;
      register_A = (flag_C = (register_A + (((cmp_new = 0xa) ^ 0xFFF) + 1))) & 0xFFF; /* 1's-comp add */
      /* opJLT_A_A (5b) */
      if (cmp_new < cmp_old) {
        register_PC = 0x1749;
        break;
      };
      /* opLDAdir_A_AA (ab) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = 0x8b]; /* set I register */

      /* opADDimm_A_AA (21) */
      register_A = (flag_C = ((cmp_old = acc_a0 = register_A) + (cmp_new = 0x1))) & 0xFFF; /* add values, save carry */

      /* opSTAirg_A_A (e6) */
      ram[0x8b] = register_A; /* store acc */
      /* opLSRe_A_AA (eb) */
      cmp_new = 0x0BEB; cmp_old = acc_a0 = register_A; flag_C = (0x0BEB + register_A);
      register_A >>= 1;
      /* opLSRe_A_AA (eb) */
      cmp_new = 0x0BEB; cmp_old = acc_a0 = register_A; flag_C = (0x0BEB + register_A);
      register_A >>= 1;
      /* opLSRe_A_AA (eb) */
      cmp_new = 0x0BEB; cmp_old = acc_a0 = register_A; flag_C = (0x0BEB + register_A);
      register_A >>= 1;
      /* opOUTbi_A_A (96) */
      vgColour = register_A & 0x01 ? 0x0f : 0x07;

    case 0x1749:

      /* Invariants: register_P = 0x8 register_I = 0x81 */;
      /* opLDAimm_A_AA (01) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0x0100;
      /* opADDimmX_A_AA (20) */
      cmp_old = register_A; cmp_new = 0xcc;
      register_A = flag_C = acc_a0 = 0x01cc;
      /* opLDPimm_A_A (80) */
      register_P = 0x0; /* set page register */
      /* opSTAdir_A_A (d3) */
      ram[register_I = 0x03] = 0x1cc; /* store acc to RAM */
      /* opLDAimm_A_AA (02) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0x0200;
      /* opADDimmX_A_AA (20) */
      cmp_old = register_A; cmp_new = 0x8a;
      register_A = flag_C = acc_a0 = 0x028a;
      /* opSTAdir_A_A (d4) */
      ram[register_I = 0x04] = 0x28a; /* store acc to RAM */
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opADDimmX_A_AA (20) */
      cmp_old = register_A; cmp_new = 0x21;
      register_A = flag_C = acc_a0 = 0x0021;
      /* opSTAdir_A_A (de) */
      ram[register_I = 0x0e] = 0x021; /* store acc to RAM */
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opADDimm_A_AA (23) */
      cmp_new = 0x3; cmp_old = acc_a0 = register_A; register_A = flag_C = 0x0003;

      /* opSTAdir_A_A (df) */
      ram[register_I = 0x0f] = 0x003; /* store acc to RAM */
      /* opADDimmX_A_AA (20) */
      cmp_old = register_A; cmp_new = 0x01;
      register_A = flag_C = acc_a0 = 0x0004;
      /* opSTAdir_A_A (d2) */
      ram[register_I = 0x02] = 0x004; /* store acc to RAM */
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opADDimmX_A_AA (20) */
      cmp_old = register_A; cmp_new = 0x81;
      register_A = flag_C = acc_a0 = 0x0081;
      /* opSTAdir_A_A (d5) */
      ram[register_I = 0x05] = 0x081; /* store acc to RAM */
      /* opLDAimm_A_AA (07) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0x0700;
      /* opADDimmX_A_AA (20) */
      cmp_old = register_A; cmp_new = 0x67;
      register_A = flag_C = acc_a0 = 0x0767;
      /* opSTAdir_A_A (d0) */
      ram[register_I = 0x00] = 0x767; /* store acc to RAM */
      /* opLDJimm_A_A (43) */
      register_J = 0x0af3;
      /* opJMP_A_A (58) */
      {
        register_PC = 0x1af3;
        break;
      };

    case 0x1767:

      /* Invariants: register_P = 0x0 register_I = 0x00 register_A = 0x00 */;
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opLDPimm_A_A (87) */
      register_P = 0x7; /* set page register */
      /* opLDAdir_A_AA (a2) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = 0x72]; /* set I register */

      /* opLDJimm_A_A (41) */
      register_J = 0x0771;
      /* opJEQ_A_AA (5c) */
      if (cmp_new == cmp_old) {
        register_PC = 0x1771;
        break;
      };
      /* opSUBimm_A_AA (31) */
      cmp_old = acc_a0 = register_A;
      register_A = (flag_C = (register_A + (((cmp_new = 0x1) ^ 0xFFF) + 1))) & 0xFFF; /* 1's-comp add */
      /* opSTAirg_A_A (e6) */
      ram[0x72] = register_A; /* store acc */
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opADDimm_A_AA (21) */
      cmp_new = 0x1; cmp_old = acc_a0 = register_A; register_A = flag_C = 0x0001;


    case 0x1771:

      /* Invariants: register_P = 0x7 register_I = 0x72 register_A = 0x00 */;
      /* opOUTbi_A_A (96) */
      vgColour = 0x07;
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opADDimm_A_AA (2a) */
      cmp_new = 0xa; cmp_old = acc_a0 = register_A; register_A = flag_C = 0x000a;

      /* opLDPimm_A_A (86) */
      register_P = 0x6; /* set page register */
      /* opSUBdir_A_AA (79) */
      cmp_old = acc_a0 = register_A;
      register_A = (flag_C = (register_A + ((cmp_new = ram[register_I = 0x69]) ^ 0xFFF) + 1)) & 0xFFF; /* set regI addr */
      /* opSTAirg_A_A (e6) */
      ram[0x69] = register_A; /* store acc */
      /* opSUBimm_A_AA (39) */
      cmp_old = acc_a0 = register_A;
      register_A = (flag_C = (register_A + (((cmp_new = 0x9) ^ 0xFFF) + 1))) & 0xFFF; /* 1's-comp add */
      /* opLDJimm_A_A (44) */
      register_J = 0x0784;
      /* opJNC_A_AA (5d) */
      if (!(flag_C & CARRYBIT)) {
        register_PC = 0x1784;
        break;
      };
      /* opLDPimm_A_A (87) */
      register_P = 0x7; /* set page register */
      /* opLDAdir_A_AA (ab) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = 0x7b]; /* set I register */

      /* opADDimm_A_AA (21) */
      register_A = (flag_C = ((cmp_old = acc_a0 = register_A) + (cmp_new = 0x1))) & 0xFFF; /* add values, save carry */

      /* opSTAirg_A_A (e6) */
      ram[0x7b] = register_A; /* store acc */
      /* opLSRe_A_AA (eb) */
      cmp_new = 0x0BEB; cmp_old = acc_a0 = register_A; flag_C = (0x0BEB + register_A);
      register_A >>= 1;
      /* opLSRe_A_AA (eb) */
      cmp_new = 0x0BEB; cmp_old = acc_a0 = register_A; flag_C = (0x0BEB + register_A);
      register_A >>= 1;
      /* opLSRe_A_AA (eb) */
      cmp_new = 0x0BEB; cmp_old = acc_a0 = register_A; flag_C = (0x0BEB + register_A);
      register_A >>= 1;
      /* opADDimm_A_AA (21) */
      register_A = (flag_C = ((cmp_old = acc_a0 = register_A) + (cmp_new = 0x1))) & 0xFFF; /* add values, save carry */

      /* opOUTbi_A_A (96) */
      vgColour = register_A & 0x01 ? 0x0f : 0x07;

    case 0x1784:

      /* Invariants: register_P = 0x6 register_I = 0x69 register_A = 0xff7 */;
      /* opLDAimm_A_AA (03) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0x0300;
      /* opADDimmX_A_AA (20) */
      cmp_old = register_A; cmp_new = 0x20;
      register_A = flag_C = acc_a0 = 0x0320;
      /* opLDPimm_A_A (80) */
      register_P = 0x0; /* set page register */
      /* opSTAdir_A_A (d3) */
      ram[register_I = 0x03] = 0x320; /* store acc to RAM */
      /* opLDAimm_A_AA (02) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0x0200;
      /* opADDimmX_A_AA (20) */
      cmp_old = register_A; cmp_new = 0x58;
      register_A = flag_C = acc_a0 = 0x0258;
      /* opSTAdir_A_A (d4) */
      ram[register_I = 0x04] = 0x258; /* store acc to RAM */
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opADDimmX_A_AA (20) */
      cmp_old = register_A; cmp_new = 0x21;
      register_A = flag_C = acc_a0 = 0x0021;
      /* opSTAdir_A_A (de) */
      ram[register_I = 0x0e] = 0x021; /* store acc to RAM */
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opADDimm_A_AA (23) */
      cmp_new = 0x3; cmp_old = acc_a0 = register_A; register_A = flag_C = 0x0003;

      /* opSTAdir_A_A (df) */
      ram[register_I = 0x0f] = 0x003; /* store acc to RAM */
      /* opADDimmX_A_AA (20) */
      cmp_old = register_A; cmp_new = 0x01;
      register_A = flag_C = acc_a0 = 0x0004;
      /* opSTAdir_A_A (d2) */
      ram[register_I = 0x02] = 0x004; /* store acc to RAM */
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opADDimmX_A_AA (20) */
      cmp_old = register_A; cmp_new = 0x69;
      register_A = flag_C = acc_a0 = 0x0069;
      /* opSTAdir_A_A (d5) */
      ram[register_I = 0x05] = 0x069; /* store acc to RAM */
      /* opLDAimm_A_AA (07) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0x0700;
      /* opADDimmX_A_AA (20) */
      cmp_old = register_A; cmp_new = 0xa2;
      register_A = flag_C = acc_a0 = 0x07a2;
      /* opSTAdir_A_A (d0) */
      ram[register_I = 0x00] = 0x7a2; /* store acc to RAM */
      /* opLDJimm_A_A (43) */
      register_J = 0x0af3;
      /* opJMP_A_A (58) */
      {
        register_PC = 0x1af3;
        break;
      };

    case 0x17a2:

      /* Invariants: register_P = 0x0 register_I = 0x00 register_A = 0x00 */;
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opADDimm_A_AA (2a) */
      cmp_new = 0xa; cmp_old = acc_a0 = register_A; register_A = flag_C = 0x000a;

      /* opLDPimm_A_A (86) */
      register_P = 0x6; /* set page register */
      /* opSUBdir_A_AA (79) */
      cmp_old = acc_a0 = register_A;
      register_A = (flag_C = (register_A + ((cmp_new = ram[register_I = 0x69]) ^ 0xFFF) + 1)) & 0xFFF; /* set regI addr */
      /* opSTAirg_A_A (e6) */
      ram[0x69] = register_A; /* store acc */
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opADDimmX_A_AA (20) */
      cmp_old = register_A; cmp_new = 0x1d;
      register_A = flag_C = acc_a0 = 0x001d;
      /* opOUTbi_A_A (93) */
      /* opOUTsnd_A (93) */
      set_sound_data(1);
      /* opLSRe_A_AA (eb) */
      cmp_new = 0x0BEB; cmp_old = acc_a0 = register_A; flag_C = (0x0BEB + register_A);
      register_A >>= 1;
      /* opOUTbi_A_A (90) */
      /* opOUTsnd_A (90) */
      set_sound_addr_A(register_A & 1);
      /* opLSRe_A_AA (eb) */
      cmp_new = 0x0BEB; cmp_old = acc_a0 = register_A; flag_C = (0x0BEB + register_A);
      register_A >>= 1;
      /* opOUTbi_A_A (91) */
      /* opOUTsnd_A (91) */
      set_sound_addr_B(register_A & 1);
      /* opLSRe_A_AA (eb) */
      cmp_new = 0x0BEB; cmp_old = acc_a0 = register_A; flag_C = (0x0BEB + register_A);
      register_A >>= 1;
      /* opOUTbi_A_A (92) */
      /* opOUTsnd_A (92) */
      set_sound_addr_C(register_A & 1);
      /* opLSRe_A_AA (eb) */
      cmp_new = 0x0BEB; cmp_old = acc_a0 = register_A; flag_C = (0x0BEB + register_A);
      register_A >>= 1;
      /* opOUTbi_A_A (94) */
      /* opOUTsnd_A (94) */
      if (register_A & 1) strobe_sound_on(); else strobe_sound_off();
      /* opLSRe_A_AA (eb) */
      cmp_new = 0x0BEB; cmp_old = acc_a0 = register_A; flag_C = (0x0BEB + register_A);
      register_A >>= 1;
      /* opOUTbi_A_A (94) */
      /* opOUTsnd_A (94) */
      if (register_A & 1) strobe_sound_on(); else strobe_sound_off();
      /* opLDJimm_A_A (48) */
      register_J = 0x0058;
      /* opLDPimm_A_A (81) */
      register_P = 0x1; /* set page register */
      /* opJPP8_A_B (50) */
      register_PC = 0x0058; /* Jump to other rom bank */
      break;

    case 0x17b9:

      /* Invariants: register_P = 0x6 register_I = 0x69 register_A = 0x00 */;
      /* opLDPimm_A_A (86) */
      /* opLDAdir_A_AA (aa) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = 0x6a]; /* set I register */

      /* opCMPdir_A_AA (bc) */
      cmp_old = acc_a0 = register_A; /* backup old acc */
      flag_C = (((cmp_new = ram[register_I = 0x6c]) ^ 0xFFF) + 1 + register_A);
      /* opLDJimm_A_A (4b) */
      register_J = 0x07cb;
      /* opJNC_A_AA (5d) */
      if (!(flag_C & CARRYBIT)) {
        register_PC = 0x17cb;
        break;
      };
      /* opLDJimm_A_A (45) */
      register_J = 0x07c5;
      /* opJEQ_A_AA (5c) */
      if (cmp_new == cmp_old) {
        register_PC = 0x17c5;
        break;
      };
      /* opSTAirg_A_A (e6) */
      ram[0x6c] = register_A; /* store acc */
      /* opLDAdir_A_AA (ab) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = 0x6b]; /* set I register */

      /* opSTAdir_A_A (dd) */
      ram[register_I = 0x6d] = register_A; /* store acc to RAM */

    case 0x17c5:

      /* Invariants: register_P = 0x6 register_I = 0x6c register_A = 0x00 */;
      /* opLDAdir_A_AA (ab) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = 0x6b]; /* set I register */

      /* opCMPdir_A_AA (bd) */
      cmp_old = acc_a0 = register_A; /* backup old acc */
      flag_C = (((cmp_new = ram[register_I = 0x6d]) ^ 0xFFF) + 1 + register_A);
      /* opLDJimm_A_A (4b) */
      register_J = 0x07cb;
      /* opJNC_A_AA (5d) */
      if (!(flag_C & CARRYBIT)) {
        register_PC = 0x17cb;
        break;
      };
      /* opSTAirg_A_A (e6) */
      ram[0x6d] = register_A; /* store acc */

    case 0x17cb:

      /* Invariants: register_P = 0x6 register_I = 0x6d register_A = 0x00 */;
      /* opJDR_A_A (5a) */
      /* opNOP_A_B (57) */
      /* opLDAimm_B_AA (02) */
      flag_C = acc_a0 = register_A;
      cmp_old = register_B; /* step back cmp flag */
      register_B = cmp_new = 0x0200;
      /* opNOP_A_B (57) */
      /* opADDimmX_B_AA (20) */
      cmp_old = register_B; acc_a0 = register_A; /* save old accA bit0 */
      cmp_new = 0x76;
      register_B = flag_C = 0x0276; cmp_new = 0x76; /* No carry */
      /* opLDAimm_A_AA (02) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0x0200;
      /* opADDimmX_A_AA (20) */
      cmp_old = register_A; cmp_new = 0xda;
      register_A = flag_C = acc_a0 = 0x02da;
      /* opVIN_A_A (f0) */

      FromX = register_A & 0xFFF; /* regA goes to x-coord */
      FromY = register_B & 0xFFF; /* regB goes to y-coord */

      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opOUTbi_A_A (96) */
      vgColour = 0x07;
      /* opADDimm_A_AA (21) */
      cmp_new = 0x1; cmp_old = acc_a0 = register_A; register_A = flag_C = 0x0001;

      /* opLDPimm_A_A (86) */
      /* opSTAdir_A_A (d1) */
      ram[register_I = 0x61] = 0x001; /* store acc to RAM */
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opSTAdir_A_A (d2) */
      ram[register_I = 0x62] = 0x000; /* store acc to RAM */
      /* opSTAdir_A_A (d0) */
      ram[register_I = 0x60] = 0x000; /* store acc to RAM */
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opADDimmX_A_AA (20) */
      cmp_old = register_A; cmp_new = 0x1c;
      register_A = flag_C = acc_a0 = 0x001c;
      /* opOUTbi_A_A (93) */
      /* opOUTsnd_A (93) */
      set_sound_data(0);
      /* opLSRe_A_AA (eb) */
      cmp_new = 0x0BEB; cmp_old = acc_a0 = register_A; flag_C = (0x0BEB + register_A);
      register_A >>= 1;
      /* opOUTbi_A_A (90) */
      /* opOUTsnd_A (90) */
      set_sound_addr_A(register_A & 1);
      /* opLSRe_A_AA (eb) */
      cmp_new = 0x0BEB; cmp_old = acc_a0 = register_A; flag_C = (0x0BEB + register_A);
      register_A >>= 1;
      /* opOUTbi_A_A (91) */
      /* opOUTsnd_A (91) */
      set_sound_addr_B(register_A & 1);
      /* opLSRe_A_AA (eb) */
      cmp_new = 0x0BEB; cmp_old = acc_a0 = register_A; flag_C = (0x0BEB + register_A);
      register_A >>= 1;
      /* opOUTbi_A_A (92) */
      /* opOUTsnd_A (92) */
      set_sound_addr_C(register_A & 1);
      /* opLSRe_A_AA (eb) */
      cmp_new = 0x0BEB; cmp_old = acc_a0 = register_A; flag_C = (0x0BEB + register_A);
      register_A >>= 1;
      /* opOUTbi_A_A (94) */
      /* opOUTsnd_A (94) */
      if (register_A & 1) strobe_sound_on(); else strobe_sound_off();
      /* opLSRe_A_AA (eb) */
      cmp_new = 0x0BEB; cmp_old = acc_a0 = register_A; flag_C = (0x0BEB + register_A);
      register_A >>= 1;
      /* opOUTbi_A_A (94) */
      /* opOUTsnd_A (94) */
      if (register_A & 1) strobe_sound_on(); else strobe_sound_off();
      /* opLDAimm_A_AA (02) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0x0200;
      /* opADDimmX_A_AA (20) */
      cmp_old = register_A; cmp_new = 0xbc;
      register_A = flag_C = acc_a0 = 0x02bc;
      /* opLDPimm_A_A (80) */
      register_P = 0x0; /* set page register */
      /* opSTAdir_A_A (d3) */
      ram[register_I = 0x03] = 0x2bc; /* store acc to RAM */
      /* opLDAimm_A_AA (02) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0x0200;
      /* opADDimmX_A_AA (20) */
      cmp_old = register_A; cmp_new = 0x8a;
      register_A = flag_C = acc_a0 = 0x028a;
      /* opSTAdir_A_A (d4) */
      ram[register_I = 0x04] = 0x28a; /* store acc to RAM */
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opADDimmX_A_AA (20) */
      cmp_old = register_A; cmp_new = 0x21;
      register_A = flag_C = acc_a0 = 0x0021;
      /* opSTAdir_A_A (de) */
      ram[register_I = 0x0e] = 0x021; /* store acc to RAM */
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opSTAdir_A_A (df) */
      ram[register_I = 0x0f] = 0x000; /* store acc to RAM */
      /* opLDAimm_A_AA (08) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0x0800;
      /* opADDimmX_A_AA (20) */
      cmp_old = register_A; cmp_new = 0x03;
      register_A = flag_C = acc_a0 = 0x0803;
      /* opSTAdir_A_A (d5) */
      ram[register_I = 0x05] = 0x803; /* store acc to RAM */
      /* opADDimmX_A_AA (20) */
      cmp_old = register_A; cmp_new = 0x0c;
      register_A = flag_C = acc_a0 = 0x080f;
      /* opSTAdir_A_A (d0) */
      ram[register_I = 0x00] = 0x80f; /* store acc to RAM */
      /* opLDJimm_A_A (43) */
      register_J = 0x0af3;
      /* opJMP_A_A (58) */
      {
        register_PC = 0x1af3;
        break;
      };

    default:
      /* Jumping to any illegal address or end of eprom will come here */
      /* we ought to reinitialise or something */
      ;
  }
}

// ops0c00.c

void cineExecute0c00(void)
{
  switch (register_PC)
  {
    case 0x0c00:

      /* Invariants: register_P = 0x1 register_I = 0x19 */;
      /* opLDPimm_A_A (81) */
      /* opLDIdir_A_A (c9) */
      register_I = ram[0x19] & 0xff; /* set new register_I (8 bits) */
      /* opLDAirg_A_AA (ea) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = ram[register_I];
      /* opSTAdir_A_A (d0) */
      ram[register_I = 0x10] = register_A; /* store acc to RAM */
      /* opLDAdir_A_AA (a9) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = 0x19]; /* set I register */

      /* opADDimm_A_AA (21) */
      register_A = (flag_C = ((cmp_old = acc_a0 = register_A) + (cmp_new = 0x1))) & 0xFFF; /* add values, save carry */

      /* opSTAirg_A_A (e6) */
      ram[0x19] = register_A; /* store acc */
      /* opLDIdir_A_A (c9) */
      register_I = ram[0x19] & 0xff; /* set new register_I (8 bits) */
      /* opLDAirg_A_AA (ea) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = ram[register_I];
      /* opSTAdir_A_A (d1) */
      ram[register_I = 0x11] = register_A; /* store acc to RAM */
      /* opLDAdir_A_AA (a9) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = 0x19]; /* set I register */

      /* opADDimm_A_AA (21) */
      register_A = (flag_C = ((cmp_old = acc_a0 = register_A) + (cmp_new = 0x1))) & 0xFFF; /* add values, save carry */

      /* opSTAirg_A_A (e6) */
      ram[0x19] = register_A; /* store acc */
      /* opLDIdir_A_A (c9) */
      register_I = ram[0x19] & 0xff; /* set new register_I (8 bits) */
      /* opLDAirg_A_AA (ea) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = ram[register_I];
      /* opSTAdir_A_A (d2) */
      ram[register_I = 0x12] = register_A; /* store acc to RAM */
      /* opLDAdir_A_AA (a9) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = 0x19]; /* set I register */

      /* opADDimm_A_AA (21) */
      register_A = (flag_C = ((cmp_old = acc_a0 = register_A) + (cmp_new = 0x1))) & 0xFFF; /* add values, save carry */

      /* opSTAirg_A_A (e6) */
      ram[0x19] = register_A; /* store acc */
      /* opLDIdir_A_A (c9) */
      register_I = ram[0x19] & 0xff; /* set new register_I (8 bits) */
      /* opLDAirg_A_AA (ea) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = ram[register_I];
      /* opSTAdir_A_A (d3) */
      ram[register_I = 0x13] = register_A; /* store acc to RAM */
      /* opLDAdir_A_AA (a9) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = 0x19]; /* set I register */

      /* opADDimm_A_AA (21) */
      register_A = (flag_C = ((cmp_old = acc_a0 = register_A) + (cmp_new = 0x1))) & 0xFFF; /* add values, save carry */

      /* opSTAirg_A_A (e6) */
      ram[0x19] = register_A; /* store acc */
      /* opLDIdir_A_A (c9) */
      register_I = ram[0x19] & 0xff; /* set new register_I (8 bits) */
      /* opLDAirg_A_AA (ea) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = ram[register_I];
      /* opSTAdir_A_A (d4) */
      ram[register_I = 0x14] = register_A; /* store acc to RAM */
      /* opLDAdir_A_AA (a9) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = 0x19]; /* set I register */

      /* opADDimm_A_AA (21) */
      register_A = (flag_C = ((cmp_old = acc_a0 = register_A) + (cmp_new = 0x1))) & 0xFFF; /* add values, save carry */

      /* opSTAirg_A_A (e6) */
      ram[0x19] = register_A; /* store acc */
      /* opLDIdir_A_A (c9) */
      register_I = ram[0x19] & 0xff; /* set new register_I (8 bits) */
      /* opLDAirg_A_AA (ea) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = ram[register_I];
      /* opSTAdir_A_A (d5) */
      ram[register_I = 0x15] = register_A; /* store acc to RAM */
      /* opLDAdir_A_AA (a9) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = 0x19]; /* set I register */

      /* opADDimm_A_AA (22) */
      register_A = (flag_C = ((cmp_old = acc_a0 = register_A) + (cmp_new = 0x2))) & 0xFFF; /* add values, save carry */

      /* opSTAirg_A_A (e6) */
      ram[0x19] = register_A; /* store acc */
      /* opLDIdir_A_A (c9) */
      register_I = ram[0x19] & 0xff; /* set new register_I (8 bits) */
      /* opLDAirg_A_AA (ea) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = ram[register_I];
      /* opSTAdir_A_A (da) */
      ram[register_I = 0x1a] = register_A; /* store acc to RAM */
      /* opSUBdir_A_AA (70) */
      cmp_old = acc_a0 = register_A;
      register_A = (flag_C = (register_A + ((cmp_new = ram[register_I = 0x10]) ^ 0xFFF) + 1)) & 0xFFF; /* set regI addr */
      /* opASRe_A_AA (ed) */
      cmp_new = 0xDED; cmp_old = flag_C = acc_a0 = register_A;
      register_A = SEX(register_A); /* make signed */
      register_A = (((signed short int)(((signed short int)register_A) >> (signed short int)1)) /* SAR */) & 0xFFF;
      /* opASRe_A_AA (ed) */
      cmp_new = 0xDED; cmp_old = flag_C = acc_a0 = register_A;
      register_A = SEX(register_A); /* make signed */
      register_A = (((signed short int)(((signed short int)register_A) >> (signed short int)1)) /* SAR */) & 0xFFF;
      /* opASRe_A_AA (ed) */
      cmp_new = 0xDED; cmp_old = flag_C = acc_a0 = register_A;
      register_A = SEX(register_A); /* make signed */
      register_A = (((signed short int)(((signed short int)register_A) >> (signed short int)1)) /* SAR */) & 0xFFF;
      /* opASRe_A_AA (ed) */
      cmp_new = 0xDED; cmp_old = flag_C = acc_a0 = register_A;
      register_A = SEX(register_A); /* make signed */
      register_A = (((signed short int)(((signed short int)register_A) >> (signed short int)1)) /* SAR */) & 0xFFF;
      /* opSTAdir_A_A (d6) */
      ram[register_I = 0x16] = register_A; /* store acc to RAM */
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opADDimmX_A_AA (20) */
      cmp_old = register_A; cmp_new = 0x1e;
      register_A = flag_C = acc_a0 = 0x001e;
      /* opCMPdir_A_AA (b6) */
      cmp_old = acc_a0 = register_A; /* backup old acc */
      flag_C = (((cmp_new = ram[register_I = 0x16]) ^ 0xFFF) + 1 + register_A);
      /* opLDJimm_A_A (48) */
      register_J = 0x0c48;
      /* opJLT_A_A (5b) */
      if (cmp_new < cmp_old) {
        register_PC = 0x0c48;
        break;
      };
      /* opLDAimm_A_AA (08) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0x0800;
      /* opCMPdir_A_AA (b6) */
      cmp_old = acc_a0 = register_A; /* backup old acc */
      flag_C = (((cmp_new = ram[register_I = 0x16]) ^ 0xFFF) + 1 + register_A);
      /* opLDJimm_A_A (44) */
      register_J = 0x0c44;
      /* opJLT_A_A (5b) */
      if (cmp_new < cmp_old) {
        register_PC = 0x0c44;
        break;
      };
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opSUBimmX_A_AA (30) */
      cmp_old = acc_a0 = register_A; /* back up regA */
      register_A = (flag_C = (register_A + (((cmp_new = 0x1e) ^ 0xFFF) + 1))) & 0xFFF; /* add */
      /* opCMPdir_A_AA (b6) */
      cmp_old = acc_a0 = register_A; /* backup old acc */
      flag_C = (((cmp_new = ram[register_I = 0x16]) ^ 0xFFF) + 1 + register_A);
      /* opLDJimm_A_A (47) */
      register_J = 0x0c47;
      /* opJLT_A_A (5b) */
      if (cmp_new < cmp_old) {
        register_PC = 0x0c47;
        break;
      };
      /* opLDJimm_A_A (48) */
      register_J = 0x0c48;
      /* opJMP_A_A (58) */
      {
        register_PC = 0x0c48;
        break;
      };

    case 0x0c44:

      /* Invariants: register_P = 0x1 register_I = 0x16 register_A = 0x800 */;
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opADDimmX_A_AA (20) */
      cmp_old = register_A; cmp_new = 0x1e;
      register_A = flag_C = acc_a0 = 0x001e;

    case 0x0c47:

      /* Invariants: register_P = 0x1 register_I = 0x16 */;
      /* opSTAdir_A_A (d6) */
      ram[register_I = 0x16] = register_A; /* store acc to RAM */

    case 0x0c48:

      /* Invariants: register_P = 0x1 register_I = 0x16 */;
      /* opLDAdir_A_AA (a9) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = 0x19]; /* set I register */

      /* opADDimm_A_AA (21) */
      register_A = (flag_C = ((cmp_old = acc_a0 = register_A) + (cmp_new = 0x1))) & 0xFFF; /* add values, save carry */

      /* opSTAirg_A_A (e6) */
      ram[0x19] = register_A; /* store acc */
      /* opLDIdir_A_A (c9) */
      register_I = ram[0x19] & 0xff; /* set new register_I (8 bits) */
      /* opLDAirg_A_AA (ea) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = ram[register_I];
      /* opSTAdir_A_A (db) */
      ram[register_I = 0x1b] = register_A; /* store acc to RAM */
      /* opSUBdir_A_AA (71) */
      cmp_old = acc_a0 = register_A;
      register_A = (flag_C = (register_A + ((cmp_new = ram[register_I = 0x11]) ^ 0xFFF) + 1)) & 0xFFF; /* set regI addr */
      /* opASRe_A_AA (ed) */
      cmp_new = 0xDED; cmp_old = flag_C = acc_a0 = register_A;
      register_A = SEX(register_A); /* make signed */
      register_A = (((signed short int)(((signed short int)register_A) >> (signed short int)1)) /* SAR */) & 0xFFF;
      /* opASRe_A_AA (ed) */
      cmp_new = 0xDED; cmp_old = flag_C = acc_a0 = register_A;
      register_A = SEX(register_A); /* make signed */
      register_A = (((signed short int)(((signed short int)register_A) >> (signed short int)1)) /* SAR */) & 0xFFF;
      /* opASRe_A_AA (ed) */
      cmp_new = 0xDED; cmp_old = flag_C = acc_a0 = register_A;
      register_A = SEX(register_A); /* make signed */
      register_A = (((signed short int)(((signed short int)register_A) >> (signed short int)1)) /* SAR */) & 0xFFF;
      /* opASRe_A_AA (ed) */
      cmp_new = 0xDED; cmp_old = flag_C = acc_a0 = register_A;
      register_A = SEX(register_A); /* make signed */
      register_A = (((signed short int)(((signed short int)register_A) >> (signed short int)1)) /* SAR */) & 0xFFF;
      /* opSTAdir_A_A (d7) */
      ram[register_I = 0x17] = register_A; /* store acc to RAM */
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opADDimmX_A_AA (20) */
      cmp_old = register_A; cmp_new = 0x1e;
      register_A = flag_C = acc_a0 = 0x001e;
      /* opCMPdir_A_AA (b7) */
      cmp_old = acc_a0 = register_A; /* backup old acc */
      flag_C = (((cmp_new = ram[register_I = 0x17]) ^ 0xFFF) + 1 + register_A);
      /* opLDJimm_A_A (4e) */
      register_J = 0x0c6e;
      /* opJLT_A_A (5b) */
      if (cmp_new < cmp_old) {
        register_PC = 0x0c6e;
        break;
      };
      /* opLDAimm_A_AA (08) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0x0800;
      /* opCMPdir_A_AA (b7) */
      cmp_old = acc_a0 = register_A; /* backup old acc */
      flag_C = (((cmp_new = ram[register_I = 0x17]) ^ 0xFFF) + 1 + register_A);
      /* opLDJimm_A_A (4a) */
      register_J = 0x0c6a;
      /* opJLT_A_A (5b) */
      if (cmp_new < cmp_old) {
        register_PC = 0x0c6a;
        break;
      };
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opSUBimmX_A_AA (30) */
      cmp_old = acc_a0 = register_A; /* back up regA */
      register_A = (flag_C = (register_A + (((cmp_new = 0x1e) ^ 0xFFF) + 1))) & 0xFFF; /* add */
      /* opCMPdir_A_AA (b7) */
      cmp_old = acc_a0 = register_A; /* backup old acc */
      flag_C = (((cmp_new = ram[register_I = 0x17]) ^ 0xFFF) + 1 + register_A);
      /* opLDJimm_A_A (4d) */
      register_J = 0x0c6d;
      /* opJLT_A_A (5b) */
      if (cmp_new < cmp_old) {
        register_PC = 0x0c6d;
        break;
      };
      /* opLDJimm_A_A (4e) */
      register_J = 0x0c6e;
      /* opJMP_A_A (58) */
      {
        register_PC = 0x0c6e;
        break;
      };

    case 0x0c6a:

      /* Invariants: register_P = 0x1 register_I = 0x17 register_A = 0x800 */;
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opADDimmX_A_AA (20) */
      cmp_old = register_A; cmp_new = 0x1e;
      register_A = flag_C = acc_a0 = 0x001e;

    case 0x0c6d:

      /* Invariants: register_P = 0x1 register_I = 0x17 */;
      /* opSTAdir_A_A (d7) */
      ram[register_I = 0x17] = register_A; /* store acc to RAM */

    case 0x0c6e:

      /* Invariants: register_P = 0x1 register_I = 0x17 */;
      /* opLDAdir_A_AA (a9) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = 0x19]; /* set I register */

      /* opADDimm_A_AA (21) */
      register_A = (flag_C = ((cmp_old = acc_a0 = register_A) + (cmp_new = 0x1))) & 0xFFF; /* add values, save carry */

      /* opSTAirg_A_A (e6) */
      ram[0x19] = register_A; /* store acc */
      /* opLDIdir_A_A (c9) */
      register_I = ram[0x19] & 0xff; /* set new register_I (8 bits) */
      /* opLDAirg_A_AA (ea) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = ram[register_I];
      /* opCMPdir_A_AA (b2) */
      cmp_old = acc_a0 = register_A; /* backup old acc */
      flag_C = (((cmp_new = ram[register_I = 0x12]) ^ 0xFFF) + 1 + register_A);
      /* opLDJimm_A_A (41) */
      register_J = 0x0c81;
      /* opJNC_A_AA (5d) */
      if (!(flag_C & CARRYBIT)) {
        register_PC = 0x0c81;
        break;
      };
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opADDimmX_A_AA (20) */
      cmp_old = register_A; cmp_new = 0x40;
      register_A = flag_C = acc_a0 = 0x0040;
      /* opSUBdir_A_AA (76) */
      cmp_old = acc_a0 = register_A;
      register_A = (flag_C = (register_A + ((cmp_new = ram[register_I = 0x16]) ^ 0xFFF) + 1)) & 0xFFF; /* set regI addr */
      /* opSTAirg_A_A (e6) */
      ram[0x16] = register_A; /* store acc */
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opADDimmX_A_AA (20) */
      cmp_old = register_A; cmp_new = 0x40;
      register_A = flag_C = acc_a0 = 0x0040;
      /* opSUBdir_A_AA (77) */
      cmp_old = acc_a0 = register_A;
      register_A = (flag_C = (register_A + ((cmp_new = ram[register_I = 0x17]) ^ 0xFFF) + 1)) & 0xFFF; /* set regI addr */
      /* opSTAirg_A_A (e6) */
      ram[0x17] = register_A; /* store acc */

    case 0x0c81:

      /* Invariants: register_P = 0x1 register_I = 0x12 */;
      /* opLDAdir_A_AA (a9) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = 0x19]; /* set I register */

      /* opSUBimm_A_AA (36) */
      cmp_old = acc_a0 = register_A;
      register_A = (flag_C = (register_A + (((cmp_new = 0x6) ^ 0xFFF) + 1))) & 0xFFF; /* 1's-comp add */
      /* opSTAirg_A_A (e6) */
      ram[0x19] = register_A; /* store acc */
      /* opLDAdir_A_AA (a6) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = 0x16]; /* set I register */

      /* opSUBdir_A_AA (73) */
      cmp_old = acc_a0 = register_A;
      register_A = (flag_C = (register_A + ((cmp_new = ram[register_I = 0x13]) ^ 0xFFF) + 1)) & 0xFFF; /* set regI addr */
      /* opSTAdir_A_A (de) */
      ram[register_I = 0x1e] = register_A; /* store acc to RAM */
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opADDimmX_A_AA (20) */
      cmp_old = register_A; cmp_new = 0x7f;
      register_A = flag_C = acc_a0 = 0x007f;
      /* opANDirg_A_AA (e9) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A &= (cmp_new = ram[register_I]);
      /* opSTAirg_A_A (e6) */
      ram[0x1e] = register_A; /* store acc */
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opCMPdir_A_AA (be) */
      cmp_old = acc_a0 = register_A; /* backup old acc */
      flag_C = (((cmp_new = ram[register_I = 0x1e]) ^ 0xFFF) + 1 + register_A);
      /* opLDJimm_A_A (4e) */
      register_J = 0x0c9e;
      /* opJEQ_A_AA (5c) */
      if (cmp_new == cmp_old) {
        register_PC = 0x0c9e;
        break;
      };
      /* opADDimmX_A_AA (20) */
      cmp_old = register_A; cmp_new = 0x40;
      register_A = flag_C = acc_a0 = 0x0040;
      /* opCMPdir_A_AA (be) */
      cmp_old = acc_a0 = register_A; /* backup old acc */
      flag_C = (((cmp_new = ram[register_I = 0x1e]) ^ 0xFFF) + 1 + register_A);
      /* opLDJimm_A_A (4c) */
      register_J = 0x0c9c;
      /* opJLT_A_A (5b) */
      if (cmp_new < cmp_old) {
        register_PC = 0x0c9c;
        break;
      };
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opSUBimm_A_AA (31) */
      cmp_old = acc_a0 = register_A;
      register_A = (flag_C = (register_A + (((cmp_new = 0x1) ^ 0xFFF) + 1))) & 0xFFF; /* 1's-comp add */
      /* opLDJimm_A_A (4e) */
      register_J = 0x0c9e;
      /* opJMP_A_A (58) */
      {
        register_PC = 0x0c9e;
        break;
      };

    case 0x0c9c:

      /* Invariants: register_P = 0x1 register_I = 0x1e register_A = 0x40 */;
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opADDimm_A_AA (21) */
      cmp_new = 0x1; cmp_old = acc_a0 = register_A; register_A = flag_C = 0x0001;


    case 0x0c9e:

      /* Invariants: register_P = 0x1 register_I = 0x1e */;
      /* opADDdir_A_AA (63) */
      register_A = (flag_C = ((cmp_old = acc_a0 = register_A) + (cmp_new = ram[register_I = 0x13]))) & 0xFFF; /* do acc operation */
      /* opSTAdir_A_A (de) */
      ram[register_I = 0x1e] = register_A; /* store acc to RAM */
      /* opLDIdir_A_A (c9) */
      register_I = ram[0x19] & 0xff; /* set new register_I (8 bits) */
      /* opSTAirg_A_A (e6) */
      ram[register_I] = register_A; /* store acc */
      /* opLDAimm_A_AA (0c) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0x0c00;
      /* opADDimmX_A_AA (20) */
      cmp_old = register_A; cmp_new = 0xa9;
      register_A = flag_C = acc_a0 = 0x0ca9;
      /* opSTAdir_A_A (df) */
      ram[register_I = 0x1f] = 0xca9; /* store acc to RAM */
      /* opLDJimm_A_A (4b) */
      register_J = 0x0eeb;
      /* opJMP_A_A (58) */
      {
        register_PC = 0x0eeb;
        break;
      };

    case 0x0ca9:

      /* Invariants: register_P = 0x1 register_I = 0x1f */;
      /* opLDAdir_A_AA (aa) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = 0x1a]; /* set I register */

      /* opSTAdir_A_A (dc) */
      ram[register_I = 0x1c] = register_A; /* store acc to RAM */
      /* opLDAdir_A_AA (ab) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = 0x1b]; /* set I register */

      /* opSTAdir_A_A (dd) */
      ram[register_I = 0x1d] = register_A; /* store acc to RAM */
      /* opLDAdir_A_AA (a9) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = 0x19]; /* set I register */

      /* opADDimm_A_AA (21) */
      register_A = (flag_C = ((cmp_old = acc_a0 = register_A) + (cmp_new = 0x1))) & 0xFFF; /* add values, save carry */

      /* opSTAirg_A_A (e6) */
      ram[0x19] = register_A; /* store acc */
      /* opLDAdir_A_AA (a7) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = 0x17]; /* set I register */

      /* opSUBdir_A_AA (74) */
      cmp_old = acc_a0 = register_A;
      register_A = (flag_C = (register_A + ((cmp_new = ram[register_I = 0x14]) ^ 0xFFF) + 1)) & 0xFFF; /* set regI addr */
      /* opSTAdir_A_A (de) */
      ram[register_I = 0x1e] = register_A; /* store acc to RAM */
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opADDimmX_A_AA (20) */
      cmp_old = register_A; cmp_new = 0x7f;
      register_A = flag_C = acc_a0 = 0x007f;
      /* opANDirg_A_AA (e9) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A &= (cmp_new = ram[register_I]);
      /* opSTAirg_A_A (e6) */
      ram[0x1e] = register_A; /* store acc */
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opCMPdir_A_AA (be) */
      cmp_old = acc_a0 = register_A; /* backup old acc */
      flag_C = (((cmp_new = ram[register_I = 0x1e]) ^ 0xFFF) + 1 + register_A);
      /* opLDJimm_A_A (4a) */
      register_J = 0x0cca;
      /* opJEQ_A_AA (5c) */
      if (cmp_new == cmp_old) {
        register_PC = 0x0cca;
        break;
      };
      /* opADDimmX_A_AA (20) */
      cmp_old = register_A; cmp_new = 0x40;
      register_A = flag_C = acc_a0 = 0x0040;
      /* opCMPdir_A_AA (be) */
      cmp_old = acc_a0 = register_A; /* backup old acc */
      flag_C = (((cmp_new = ram[register_I = 0x1e]) ^ 0xFFF) + 1 + register_A);
      /* opLDJimm_A_A (48) */
      register_J = 0x0cc8;
      /* opJLT_A_A (5b) */
      if (cmp_new < cmp_old) {
        register_PC = 0x0cc8;
        break;
      };
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opSUBimm_A_AA (31) */
      cmp_old = acc_a0 = register_A;
      register_A = (flag_C = (register_A + (((cmp_new = 0x1) ^ 0xFFF) + 1))) & 0xFFF; /* 1's-comp add */
      /* opLDJimm_A_A (4a) */
      register_J = 0x0cca;
      /* opJMP_A_A (58) */
      {
        register_PC = 0x0cca;
        break;
      };

    case 0x0cc8:

      /* Invariants: register_P = 0x1 register_I = 0x1e register_A = 0x40 */;
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opADDimm_A_AA (21) */
      cmp_new = 0x1; cmp_old = acc_a0 = register_A; register_A = flag_C = 0x0001;


    case 0x0cca:

      /* Invariants: register_P = 0x1 register_I = 0x1e */;
      /* opADDdir_A_AA (64) */
      register_A = (flag_C = ((cmp_old = acc_a0 = register_A) + (cmp_new = ram[register_I = 0x14]))) & 0xFFF; /* do acc operation */
      /* opSTAdir_A_A (de) */
      ram[register_I = 0x1e] = register_A; /* store acc to RAM */
      /* opLDIdir_A_A (c9) */
      register_I = ram[0x19] & 0xff; /* set new register_I (8 bits) */
      /* opSTAirg_A_A (e6) */
      ram[register_I] = register_A; /* store acc */
      /* opLDAimm_A_AA (0c) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0x0c00;
      /* opADDimmX_A_AA (20) */
      cmp_old = register_A; cmp_new = 0xd5;
      register_A = flag_C = acc_a0 = 0x0cd5;
      /* opSTAdir_A_A (df) */
      ram[register_I = 0x1f] = 0xcd5; /* store acc to RAM */
      /* opLDJimm_A_A (4b) */
      register_J = 0x0eeb;
      /* opJMP_A_A (58) */
      {
        register_PC = 0x0eeb;
        break;
      };

    case 0x0cd5:

      /* Invariants: register_P = 0x1 register_I = 0x1f */;
      /* opNOP_A_B (57) */
      /* opLDAimm_B_AA (00) */
      flag_C = acc_a0 = register_A;
      cmp_old = register_B; /* step back cmp flag */
      register_B = cmp_new = 0;
      /* opLDAimm_A_AA (08) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0x0800;
      /* opLDAdir_A_AA (ac) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = 0x1c]; /* set I register */

      /* opLDJimm_A_A (4e) */
      register_J = 0x0cde;
      /* opJLT_A_A (5b) */
      if (cmp_new < cmp_old) {
        register_PC = 0x0cde;
        break;
      };
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opSUBirg_A_AA (e8) */
      register_A = (flag_C = ((cmp_old = acc_a0 = register_A) + ((cmp_new = ram[register_I]) ^ 0xFFF) + 1)) & 0xFFF; /* ones compliment */

    case 0x0cde:

      /* Invariants: register_P = 0x1 register_I = 0x1c */;
      /* opCMPdir_A_AA (bb) */
      cmp_old = acc_a0 = register_A; /* backup old acc */
      flag_C = (((cmp_new = ram[register_I = 0x1b]) ^ 0xFFF) + 1 + register_A);
      /* opMULirg_A_AA (e3) */
      MUL();
      /* opMULirg_A_AA (e3) */
      MUL();
      /* opMULirg_A_AA (e3) */
      MUL();
      /* opMULirg_A_AA (e3) */
      MUL();
      /* opMULirg_A_AA (e3) */
      MUL();
      /* opMULirg_A_AA (e3) */
      MUL();
      /* opMULirg_A_AA (e3) */
      MUL();
      /* opMULirg_A_AA (e3) */
      MUL();
      /* opMULirg_A_AA (e3) */
      MUL();
      /* opLDAimm_A_AA (08) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0x0800;
      /* opCMPdir_A_AA (bc) */
      cmp_old = acc_a0 = register_A; /* backup old acc */
      flag_C = (((cmp_new = ram[register_I = 0x1c]) ^ 0xFFF) + 1 + register_A);
      /* opNOP_A_B (57) */
      /* opSTAdir_B_BB (d3) */
      ram[register_I = (register_P << 4) + 0x3] = register_B; /* set I register and store B to ram */
      /* opLDJimm_A_A (42) */
      register_J = 0x0cf2;
      /* opJLT_A_A (5b) */
      if (cmp_new < cmp_old) {
        register_PC = 0x0cf2;
        break;
      };
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opSUBdir_A_AA (73) */
      cmp_old = acc_a0 = register_A;
      register_A = (flag_C = (register_A + ((cmp_new = ram[register_I = 0x13]) ^ 0xFFF) + 1)) & 0xFFF; /* set regI addr */
      /* opSTAirg_A_A (e6) */
      ram[0x13] = register_A; /* store acc */

    case 0x0cf2:

      /* Invariants: register_P = 0x1 register_I = 0x13 */;
      /* opNOP_A_B (57) */
      /* opLDAimm_B_AA (00) */
      flag_C = acc_a0 = register_A;
      cmp_old = register_B; /* step back cmp flag */
      register_B = cmp_new = 0;
      /* opLDAimm_A_AA (08) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0x0800;
      /* opLDAdir_A_AA (a3) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = 0x13]; /* set I register */

      /* opLDJimm_A_A (4b) */
      register_J = 0x0cfb;
      /* opJLT_A_A (5b) */
      if (cmp_new < cmp_old) {
        register_PC = 0x0cfb;
        break;
      };
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opSUBirg_A_AA (e8) */
      register_A = (flag_C = ((cmp_old = acc_a0 = register_A) + ((cmp_new = ram[register_I]) ^ 0xFFF) + 1)) & 0xFFF; /* ones compliment */

    case 0x0cfb:

      /* Invariants: register_P = 0x1 register_I = 0x13 */;
      /* opCMPdir_A_AA (b5) */
      cmp_old = acc_a0 = register_A; /* backup old acc */
      flag_C = (((cmp_new = ram[register_I = 0x15]) ^ 0xFFF) + 1 + register_A);
      /* opMULirg_A_AA (e3) */
      MUL();
      /* opMULirg_A_AA (e3) */
      MUL();
      /* opMULirg_A_AA (e3) */
      MUL();
      /* opMULirg_A_AA (e3) */
      MUL();
      /* opMULirg_A_AA (e3) */
      MUL();
      /* opMULirg_A_AA (e3) */
      MUL();
      /* opMULirg_A_AA (e3) */
      MUL();
      /* opMULirg_A_AA (e3) */
      MUL();
      /* opMULirg_A_AA (e3) */
      MUL();
      /* opLDAimm_A_AA (08) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0x0800;
      /* opCMPdir_A_AA (b3) */
      cmp_old = acc_a0 = register_A; /* backup old acc */
      flag_C = (((cmp_new = ram[register_I = 0x13]) ^ 0xFFF) + 1 + register_A);
      /* opNOP_A_B (57) */
      /* opSTAdir_B_BB (d3) */
      ram[register_I = (register_P << 4) + 0x3] = register_B; /* set I register and store B to ram */
      /* opLDJimm_A_A (4f) */
      register_J = 0x0d0f;
      /* opJLT_A_A (5b) */
      if (cmp_new < cmp_old) {
        register_PC = 0x0d0f;
        break;
      };
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opSUBdir_A_AA (73) */
      cmp_old = acc_a0 = register_A;
      register_A = (flag_C = (register_A + ((cmp_new = ram[register_I = 0x13]) ^ 0xFFF) + 1)) & 0xFFF; /* set regI addr */
      /* opSTAirg_A_A (e6) */
      ram[0x13] = register_A; /* store acc */

    case 0x0d0f:

      /* Invariants: register_P = 0x1 register_I = 0x13 */;
      /* opNOP_A_B (57) */
      /* opLDAimm_B_AA (00) */
      flag_C = acc_a0 = register_A;
      cmp_old = register_B; /* step back cmp flag */
      register_B = cmp_new = 0;
      /* opLDAimm_A_AA (08) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0x0800;
      /* opLDAdir_A_AA (aa) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = 0x1a]; /* set I register */

      /* opLDJimm_A_A (48) */
      register_J = 0x0d18;
      /* opJLT_A_A (5b) */
      if (cmp_new < cmp_old) {
        register_PC = 0x0d18;
        break;
      };
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opSUBirg_A_AA (e8) */
      register_A = (flag_C = ((cmp_old = acc_a0 = register_A) + ((cmp_new = ram[register_I]) ^ 0xFFF) + 1)) & 0xFFF; /* ones compliment */

    case 0x0d18:

      /* Invariants: register_P = 0x1 register_I = 0x1a */;
      /* opCMPdir_A_AA (b5) */
      cmp_old = acc_a0 = register_A; /* backup old acc */
      flag_C = (((cmp_new = ram[register_I = 0x15]) ^ 0xFFF) + 1 + register_A);
      /* opMULirg_A_AA (e3) */
      MUL();
      /* opMULirg_A_AA (e3) */
      MUL();
      /* opMULirg_A_AA (e3) */
      MUL();
      /* opMULirg_A_AA (e3) */
      MUL();
      /* opMULirg_A_AA (e3) */
      MUL();
      /* opMULirg_A_AA (e3) */
      MUL();
      /* opMULirg_A_AA (e3) */
      MUL();
      /* opMULirg_A_AA (e3) */
      MUL();
      /* opMULirg_A_AA (e3) */
      MUL();
      /* opLDAimm_A_AA (08) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0x0800;
      /* opCMPdir_A_AA (ba) */
      cmp_old = acc_a0 = register_A; /* backup old acc */
      flag_C = (((cmp_new = ram[register_I = 0x1a]) ^ 0xFFF) + 1 + register_A);
      /* opNOP_A_B (57) */
      /* opSTAdir_B_BB (d4) */
      ram[register_I = (register_P << 4) + 0x4] = register_B; /* set I register and store B to ram */
      /* opLDJimm_A_A (4c) */
      register_J = 0x0d2c;
      /* opJLT_A_A (5b) */
      if (cmp_new < cmp_old) {
        register_PC = 0x0d2c;
        break;
      };
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opSUBdir_A_AA (74) */
      cmp_old = acc_a0 = register_A;
      register_A = (flag_C = (register_A + ((cmp_new = ram[register_I = 0x14]) ^ 0xFFF) + 1)) & 0xFFF; /* set regI addr */
      /* opSTAirg_A_A (e6) */
      ram[0x14] = register_A; /* store acc */

    case 0x0d2c:

      /* Invariants: register_P = 0x1 register_I = 0x14 */;
      /* opNOP_A_B (57) */
      /* opLDAimm_B_AA (00) */
      flag_C = acc_a0 = register_A;
      cmp_old = register_B; /* step back cmp flag */
      register_B = cmp_new = 0;
      /* opLDAimm_A_AA (08) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0x0800;
      /* opLDAdir_A_AA (ad) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = 0x1d]; /* set I register */

      /* opLDJimm_A_A (45) */
      register_J = 0x0d35;
      /* opJLT_A_A (5b) */
      if (cmp_new < cmp_old) {
        register_PC = 0x0d35;
        break;
      };
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opSUBirg_A_AA (e8) */
      register_A = (flag_C = ((cmp_old = acc_a0 = register_A) + ((cmp_new = ram[register_I]) ^ 0xFFF) + 1)) & 0xFFF; /* ones compliment */

    case 0x0d35:

      /* Invariants: register_P = 0x1 register_I = 0x1d */;
      /* opCMPdir_A_AA (bb) */
      cmp_old = acc_a0 = register_A; /* backup old acc */
      flag_C = (((cmp_new = ram[register_I = 0x1b]) ^ 0xFFF) + 1 + register_A);
      /* opMULirg_A_AA (e3) */
      MUL();
      /* opMULirg_A_AA (e3) */
      MUL();
      /* opMULirg_A_AA (e3) */
      MUL();
      /* opMULirg_A_AA (e3) */
      MUL();
      /* opMULirg_A_AA (e3) */
      MUL();
      /* opMULirg_A_AA (e3) */
      MUL();
      /* opMULirg_A_AA (e3) */
      MUL();
      /* opMULirg_A_AA (e3) */
      MUL();
      /* opMULirg_A_AA (e3) */
      MUL();
      /* opLDAimm_A_AA (08) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0x0800;
      /* opCMPdir_A_AA (bd) */
      cmp_old = acc_a0 = register_A; /* backup old acc */
      flag_C = (((cmp_new = ram[register_I = 0x1d]) ^ 0xFFF) + 1 + register_A);
      /* opNOP_A_B (57) */
      /* opSTAdir_B_BB (df) */
      ram[register_I = (register_P << 4) + 0xf] = register_B; /* set I register and store B to ram */
      /* opLDJimm_A_A (49) */
      register_J = 0x0d49;
      /* opJLT_A_A (5b) */
      if (cmp_new < cmp_old) {
        register_PC = 0x0d49;
        break;
      };
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opSUBdir_A_AA (7f) */
      cmp_old = acc_a0 = register_A;
      register_A = (flag_C = (register_A + ((cmp_new = ram[register_I = 0x1f]) ^ 0xFFF) + 1)) & 0xFFF; /* set regI addr */
      /* opSTAirg_A_A (e6) */
      ram[0x1f] = register_A; /* store acc */

    case 0x0d49:

      /* Invariants: register_P = 0x1 register_I = 0x1f register_A = 0x800 */;
      /* opNOP_A_B (57) */
      /* opLDAimm_B_AA (00) */
      flag_C = acc_a0 = register_A;
      cmp_old = register_B; /* step back cmp flag */
      register_B = cmp_new = 0;
      /* opLDAimm_A_AA (08) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0x0800;
      /* opLDAdir_A_AA (af) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = 0x1f]; /* set I register */

      /* opLDJimm_A_A (42) */
      register_J = 0x0d52;
      /* opJLT_A_A (5b) */
      if (cmp_new < cmp_old) {
        register_PC = 0x0d52;
        break;
      };
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opSUBirg_A_AA (e8) */
      register_A = (flag_C = ((cmp_old = acc_a0 = register_A) + ((cmp_new = ram[register_I]) ^ 0xFFF) + 1)) & 0xFFF; /* ones compliment */

    case 0x0d52:

      /* Invariants: register_P = 0x1 register_I = 0x1f */;
      /* opCMPdir_A_AA (b5) */
      cmp_old = acc_a0 = register_A; /* backup old acc */
      flag_C = (((cmp_new = ram[register_I = 0x15]) ^ 0xFFF) + 1 + register_A);
      /* opMULirg_A_AA (e3) */
      MUL();
      /* opMULirg_A_AA (e3) */
      MUL();
      /* opMULirg_A_AA (e3) */
      MUL();
      /* opMULirg_A_AA (e3) */
      MUL();
      /* opMULirg_A_AA (e3) */
      MUL();
      /* opMULirg_A_AA (e3) */
      MUL();
      /* opMULirg_A_AA (e3) */
      MUL();
      /* opMULirg_A_AA (e3) */
      MUL();
      /* opMULirg_A_AA (e3) */
      MUL();
      /* opLDAimm_A_AA (08) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0x0800;
      /* opCMPdir_A_AA (bf) */
      cmp_old = acc_a0 = register_A; /* backup old acc */
      flag_C = (((cmp_new = ram[register_I = 0x1f]) ^ 0xFFF) + 1 + register_A);
      /* opNOP_A_B (57) */
      /* opSTAdir_B_BB (d5) */
      ram[register_I = (register_P << 4) + 0x5] = register_B; /* set I register and store B to ram */
      /* opLDJimm_A_A (46) */
      register_J = 0x0d66;
      /* opJLT_A_A (5b) */
      if (cmp_new < cmp_old) {
        register_PC = 0x0d66;
        break;
      };
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opSUBdir_A_AA (75) */
      cmp_old = acc_a0 = register_A;
      register_A = (flag_C = (register_A + ((cmp_new = ram[register_I = 0x15]) ^ 0xFFF) + 1)) & 0xFFF; /* set regI addr */
      /* opSTAirg_A_A (e6) */
      ram[0x15] = register_A; /* store acc */

    case 0x0d66:

      /* Invariants: register_P = 0x1 register_I = 0x15 register_A = 0x800 */;
      /* opLDAdir_A_AA (a9) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = 0x19]; /* set I register */

      /* opSUBimm_A_AA (34) */
      cmp_old = acc_a0 = register_A;
      register_A = (flag_C = (register_A + (((cmp_new = 0x4) ^ 0xFFF) + 1))) & 0xFFF; /* 1's-comp add */
      /* opSTAirg_A_A (e6) */
      ram[0x19] = register_A; /* store acc */
      /* opLDAdir_A_AA (a3) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = 0x13]; /* set I register */

      /* opLDIdir_A_A (c9) */
      register_I = ram[0x19] & 0xff; /* set new register_I (8 bits) */
      /* opAWDirg_A_AA (e7) */
      cmp_old = acc_a0 = register_A;
      register_A = (flag_C = (register_A + (cmp_new = ram[register_I]))) & 0xFFF;
      set_watchdog();
      /* opSTAirg_A_A (e6) */
      ram[register_I] = register_A; /* store acc */
      /* opSTAdir_A_A (d0) */
      ram[register_I = 0x10] = register_A; /* store acc to RAM */
      /* opLDAdir_A_AA (a9) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = 0x19]; /* set I register */

      /* opADDimm_A_AA (21) */
      register_A = (flag_C = ((cmp_old = acc_a0 = register_A) + (cmp_new = 0x1))) & 0xFFF; /* add values, save carry */

      /* opSTAirg_A_A (e6) */
      ram[0x19] = register_A; /* store acc */
      /* opLDAdir_A_AA (a4) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = 0x14]; /* set I register */

      /* opLDIdir_A_A (c9) */
      register_I = ram[0x19] & 0xff; /* set new register_I (8 bits) */
      /* opAWDirg_A_AA (e7) */
      cmp_old = acc_a0 = register_A;
      register_A = (flag_C = (register_A + (cmp_new = ram[register_I]))) & 0xFFF;
      set_watchdog();
      /* opSTAirg_A_A (e6) */
      ram[register_I] = register_A; /* store acc */
      /* opSTAdir_A_A (d1) */
      ram[register_I = 0x11] = register_A; /* store acc to RAM */
      /* opLDAdir_A_AA (a9) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = 0x19]; /* set I register */

      /* opADDimm_A_AA (21) */
      register_A = (flag_C = ((cmp_old = acc_a0 = register_A) + (cmp_new = 0x1))) & 0xFFF; /* add values, save carry */

      /* opSTAirg_A_A (e6) */
      ram[0x19] = register_A; /* store acc */
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opSUBdir_A_AA (75) */
      cmp_old = acc_a0 = register_A;
      register_A = (flag_C = (register_A + ((cmp_new = ram[register_I = 0x15]) ^ 0xFFF) + 1)) & 0xFFF; /* set regI addr */
      /* opLDIdir_A_A (c9) */
      register_I = ram[0x19] & 0xff; /* set new register_I (8 bits) */
      /* opAWDirg_A_AA (e7) */
      cmp_old = acc_a0 = register_A;
      register_A = (flag_C = (register_A + (cmp_new = ram[register_I]))) & 0xFFF;
      set_watchdog();
      /* opSTAirg_A_A (e6) */
      ram[register_I] = register_A; /* store acc */
      /* opSTAdir_A_A (d2) */
      ram[register_I = 0x12] = register_A; /* store acc to RAM */
      /* opLDAdir_A_AA (a9) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = 0x19]; /* set I register */

      /* opADDimm_A_AA (29) */
      register_A = (flag_C = ((cmp_old = acc_a0 = register_A) + (cmp_new = 0x9))) & 0xFFF; /* add values, save carry */

      /* opSTAirg_A_A (e6) */
      ram[0x19] = register_A; /* store acc */
      /* opLDPimm_A_A (81) */
      /* opLDAdir_A_AA (a2) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = 0x12]; /* set I register */

      /* opLSRe_A_AA (eb) */
      cmp_new = 0x0BEB; cmp_old = acc_a0 = register_A; flag_C = (0x0BEB + register_A);
      register_A >>= 1;
      /* opLDPimm_A_A (80) */
      register_P = 0x0; /* set page register */
      /* opSTAdir_A_A (d9) */
      ram[register_I = 0x09] = register_A; /* store acc to RAM */
      /* opLDPimm_A_A (81) */
      register_P = 0x1; /* set page register */
      /* opLDAdir_A_AA (a0) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = 0x10]; /* set I register */

      /* opLDPimm_A_A (80) */
      register_P = 0x0; /* set page register */
      /* opSTAdir_A_A (d5) */
      ram[register_I = 0x05] = register_A; /* store acc to RAM */
      /* opLDPimm_A_A (81) */
      register_P = 0x1; /* set page register */
      /* opLDAdir_A_AA (a1) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = 0x11]; /* set I register */

      /* opLDPimm_A_A (80) */
      register_P = 0x0; /* set page register */
      /* opSTAdir_A_A (d6) */
      ram[register_I = 0x06] = register_A; /* store acc to RAM */
      /* opLDAimm_A_AA (02) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0x0200;
      /* opSTAdir_A_A (dd) */
      ram[register_I = 0x0d] = 0x200; /* store acc to RAM */
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opSTAdir_A_A (d4) */
      ram[register_I = 0x04] = 0x000; /* store acc to RAM */

    case 0x0d93:

      /* Invariants: register_P = 0x0 register_I = 0x04 register_A = 0x00 */;
      /* opLDAdir_A_AA (ad) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = 0x0d]; /* set I register */

      /* opCMPdir_A_AA (b9) */
      cmp_old = acc_a0 = register_A; /* backup old acc */
      flag_C = (((cmp_new = ram[register_I = 0x09]) ^ 0xFFF) + 1 + register_A);
      /* opLDJimm_A_A (43) */
      register_J = 0x0e03;
      /* opJLT_A_A (5b) */
      if (cmp_new < cmp_old) {
        register_PC = 0x0e03;
        break;
      };
      /* opNOP_A_B (57) */
      /* opLDAimm_B_AA (00) */
      flag_C = acc_a0 = register_A;
      cmp_old = register_B; /* step back cmp flag */
      register_B = cmp_new = 0;
      /* opLSLDe_A_AA (ef) */
      cmp_new = 0x0FEF; cmp_old = acc_a0 = register_A; flag_C = (0x0FEF + register_A);
      register_A = (register_A << 1) & 0xFFF; register_B = (register_B << 1) & 0xFFF;
      /* opNOP_A_B (57) */
      /* opADDimm_B_AA (21) */
      acc_a0 = register_A; /* save old accA bit0 */
      cmp_old = register_B; /* store old acc for later */
      register_B = (flag_C = (register_B + (cmp_new = 0x01))) & 0xFFF; /* add values */
      /* opSUBirg_A_AA (e8) */
      register_A = (flag_C = ((cmp_old = acc_a0 = register_A) + ((cmp_new = ram[register_I]) ^ 0xFFF) + 1)) & 0xFFF; /* ones compliment */
      /* opLDJimm_A_A (44) */
      register_J = 0x0da4;
      /* opJLT_A_A (5b) */
      if (cmp_new < cmp_old) {
        register_PC = 0x0da4;
        break;
      };
      /* opAWDirg_A_AA (e7) */
      cmp_old = acc_a0 = register_A;
      register_A = (flag_C = (register_A + (cmp_new = ram[0x09]))) & 0xFFF;
      set_watchdog();
      /* opNOP_A_B (57) */
      /* opSUBimm_B_AA (31) */
      acc_a0 = register_A;
      register_B = (flag_C = ((cmp_old = register_B) + ((cmp_new = 0x1) ^ 0xFFF) + 1)) & 0xFFF; /* 1's-comp add */

    case 0x0da4:

      /* Invariants: register_P = 0x0 register_I = 0x09 register_A = 0x400 */;
      /* opLSLDe_A_AA (ef) */
      cmp_new = 0x0FEF; cmp_old = acc_a0 = register_A; flag_C = (0x0FEF + register_A);
      register_A = (register_A << 1) & 0xFFF; register_B = (register_B << 1) & 0xFFF;
      /* opNOP_A_B (57) */
      /* opADDimm_B_AA (21) */
      acc_a0 = register_A; /* save old accA bit0 */
      cmp_old = register_B; /* store old acc for later */
      register_B = (flag_C = (register_B + (cmp_new = 0x01))) & 0xFFF; /* add values */
      /* opSUBirg_A_AA (e8) */
      register_A = (flag_C = ((cmp_old = acc_a0 = register_A) + ((cmp_new = ram[register_I]) ^ 0xFFF) + 1)) & 0xFFF; /* ones compliment */
      /* opLDJimm_A_A (4e) */
      register_J = 0x0dae;
      /* opJLT_A_A (5b) */
      if (cmp_new < cmp_old) {
        register_PC = 0x0dae;
        break;
      };
      /* opAWDirg_A_AA (e7) */
      cmp_old = acc_a0 = register_A;
      register_A = (flag_C = (register_A + (cmp_new = ram[0x09]))) & 0xFFF;
      set_watchdog();
      /* opNOP_A_B (57) */
      /* opSUBimm_B_AA (31) */
      acc_a0 = register_A;
      register_B = (flag_C = ((cmp_old = register_B) + ((cmp_new = 0x1) ^ 0xFFF) + 1)) & 0xFFF; /* 1's-comp add */

    case 0x0dae:

      /* Invariants: register_P = 0x0 register_I = 0x09 */;
      /* opLSLDe_A_AA (ef) */
      cmp_new = 0x0FEF; cmp_old = acc_a0 = register_A; flag_C = (0x0FEF + register_A);
      register_A = (register_A << 1) & 0xFFF; register_B = (register_B << 1) & 0xFFF;
      /* opNOP_A_B (57) */
      /* opADDimm_B_AA (21) */
      acc_a0 = register_A; /* save old accA bit0 */
      cmp_old = register_B; /* store old acc for later */
      register_B = (flag_C = (register_B + (cmp_new = 0x01))) & 0xFFF; /* add values */
      /* opSUBirg_A_AA (e8) */
      register_A = (flag_C = ((cmp_old = acc_a0 = register_A) + ((cmp_new = ram[register_I]) ^ 0xFFF) + 1)) & 0xFFF; /* ones compliment */
      /* opLDJimm_A_A (48) */
      register_J = 0x0db8;
      /* opJLT_A_A (5b) */
      if (cmp_new < cmp_old) {
        register_PC = 0x0db8;
        break;
      };
      /* opAWDirg_A_AA (e7) */
      cmp_old = acc_a0 = register_A;
      register_A = (flag_C = (register_A + (cmp_new = ram[0x09]))) & 0xFFF;
      set_watchdog();
      /* opNOP_A_B (57) */
      /* opSUBimm_B_AA (31) */
      acc_a0 = register_A;
      register_B = (flag_C = ((cmp_old = register_B) + ((cmp_new = 0x1) ^ 0xFFF) + 1)) & 0xFFF; /* 1's-comp add */

    case 0x0db8:

      /* Invariants: register_P = 0x0 register_I = 0x09 */;
      /* opLSLDe_A_AA (ef) */
      cmp_new = 0x0FEF; cmp_old = acc_a0 = register_A; flag_C = (0x0FEF + register_A);
      register_A = (register_A << 1) & 0xFFF; register_B = (register_B << 1) & 0xFFF;
      /* opNOP_A_B (57) */
      /* opADDimm_B_AA (21) */
      acc_a0 = register_A; /* save old accA bit0 */
      cmp_old = register_B; /* store old acc for later */
      register_B = (flag_C = (register_B + (cmp_new = 0x01))) & 0xFFF; /* add values */
      /* opSUBirg_A_AA (e8) */
      register_A = (flag_C = ((cmp_old = acc_a0 = register_A) + ((cmp_new = ram[register_I]) ^ 0xFFF) + 1)) & 0xFFF; /* ones compliment */
      /* opLDJimm_A_A (42) */
      register_J = 0x0dc2;
      /* opJLT_A_A (5b) */
      if (cmp_new < cmp_old) {
        register_PC = 0x0dc2;
        break;
      };
      /* opAWDirg_A_AA (e7) */
      cmp_old = acc_a0 = register_A;
      register_A = (flag_C = (register_A + (cmp_new = ram[0x09]))) & 0xFFF;
      set_watchdog();
      /* opNOP_A_B (57) */
      /* opSUBimm_B_AA (31) */
      acc_a0 = register_A;
      register_B = (flag_C = ((cmp_old = register_B) + ((cmp_new = 0x1) ^ 0xFFF) + 1)) & 0xFFF; /* 1's-comp add */

    case 0x0dc2:

      /* Invariants: register_P = 0x0 register_I = 0x09 */;
      /* opLSLDe_A_AA (ef) */
      cmp_new = 0x0FEF; cmp_old = acc_a0 = register_A; flag_C = (0x0FEF + register_A);
      register_A = (register_A << 1) & 0xFFF; register_B = (register_B << 1) & 0xFFF;
      /* opNOP_A_B (57) */
      /* opADDimm_B_AA (21) */
      acc_a0 = register_A; /* save old accA bit0 */
      cmp_old = register_B; /* store old acc for later */
      register_B = (flag_C = (register_B + (cmp_new = 0x01))) & 0xFFF; /* add values */
      /* opSUBirg_A_AA (e8) */
      register_A = (flag_C = ((cmp_old = acc_a0 = register_A) + ((cmp_new = ram[register_I]) ^ 0xFFF) + 1)) & 0xFFF; /* ones compliment */
      /* opLDJimm_A_A (4c) */
      register_J = 0x0dcc;
      /* opJLT_A_A (5b) */
      if (cmp_new < cmp_old) {
        register_PC = 0x0dcc;
        break;
      };
      /* opAWDirg_A_AA (e7) */
      cmp_old = acc_a0 = register_A;
      register_A = (flag_C = (register_A + (cmp_new = ram[0x09]))) & 0xFFF;
      set_watchdog();
      /* opNOP_A_B (57) */
      /* opSUBimm_B_AA (31) */
      acc_a0 = register_A;
      register_B = (flag_C = ((cmp_old = register_B) + ((cmp_new = 0x1) ^ 0xFFF) + 1)) & 0xFFF; /* 1's-comp add */

    case 0x0dcc:

      /* Invariants: register_P = 0x0 register_I = 0x09 */;
      /* opLSLDe_A_AA (ef) */
      cmp_new = 0x0FEF; cmp_old = acc_a0 = register_A; flag_C = (0x0FEF + register_A);
      register_A = (register_A << 1) & 0xFFF; register_B = (register_B << 1) & 0xFFF;
      /* opNOP_A_B (57) */
      /* opADDimm_B_AA (21) */
      acc_a0 = register_A; /* save old accA bit0 */
      cmp_old = register_B; /* store old acc for later */
      register_B = (flag_C = (register_B + (cmp_new = 0x01))) & 0xFFF; /* add values */
      /* opSUBirg_A_AA (e8) */
      register_A = (flag_C = ((cmp_old = acc_a0 = register_A) + ((cmp_new = ram[register_I]) ^ 0xFFF) + 1)) & 0xFFF; /* ones compliment */
      /* opLDJimm_A_A (46) */
      register_J = 0x0dd6;
      /* opJLT_A_A (5b) */
      if (cmp_new < cmp_old) {
        register_PC = 0x0dd6;
        break;
      };
      /* opAWDirg_A_AA (e7) */
      cmp_old = acc_a0 = register_A;
      register_A = (flag_C = (register_A + (cmp_new = ram[0x09]))) & 0xFFF;
      set_watchdog();
      /* opNOP_A_B (57) */
      /* opSUBimm_B_AA (31) */
      acc_a0 = register_A;
      register_B = (flag_C = ((cmp_old = register_B) + ((cmp_new = 0x1) ^ 0xFFF) + 1)) & 0xFFF; /* 1's-comp add */

    case 0x0dd6:

      /* Invariants: register_P = 0x0 register_I = 0x09 */;
      /* opLSLDe_A_AA (ef) */
      cmp_new = 0x0FEF; cmp_old = acc_a0 = register_A; flag_C = (0x0FEF + register_A);
      register_A = (register_A << 1) & 0xFFF; register_B = (register_B << 1) & 0xFFF;
      /* opNOP_A_B (57) */
      /* opADDimm_B_AA (21) */
      acc_a0 = register_A; /* save old accA bit0 */
      cmp_old = register_B; /* store old acc for later */
      register_B = (flag_C = (register_B + (cmp_new = 0x01))) & 0xFFF; /* add values */
      /* opSUBirg_A_AA (e8) */
      register_A = (flag_C = ((cmp_old = acc_a0 = register_A) + ((cmp_new = ram[register_I]) ^ 0xFFF) + 1)) & 0xFFF; /* ones compliment */
      /* opLDJimm_A_A (40) */
      register_J = 0x0de0;
      /* opJLT_A_A (5b) */
      if (cmp_new < cmp_old) {
        register_PC = 0x0de0;
        break;
      };
      /* opAWDirg_A_AA (e7) */
      cmp_old = acc_a0 = register_A;
      register_A = (flag_C = (register_A + (cmp_new = ram[0x09]))) & 0xFFF;
      set_watchdog();
      /* opNOP_A_B (57) */
      /* opSUBimm_B_AA (31) */
      acc_a0 = register_A;
      register_B = (flag_C = ((cmp_old = register_B) + ((cmp_new = 0x1) ^ 0xFFF) + 1)) & 0xFFF; /* 1's-comp add */

    case 0x0de0:

      /* Invariants: register_P = 0x0 register_I = 0x09 */;
      /* opLSLDe_A_AA (ef) */
      cmp_new = 0x0FEF; cmp_old = acc_a0 = register_A; flag_C = (0x0FEF + register_A);
      register_A = (register_A << 1) & 0xFFF; register_B = (register_B << 1) & 0xFFF;
      /* opNOP_A_B (57) */
      /* opADDimm_B_AA (21) */
      acc_a0 = register_A; /* save old accA bit0 */
      cmp_old = register_B; /* store old acc for later */
      register_B = (flag_C = (register_B + (cmp_new = 0x01))) & 0xFFF; /* add values */
      /* opSUBirg_A_AA (e8) */
      register_A = (flag_C = ((cmp_old = acc_a0 = register_A) + ((cmp_new = ram[register_I]) ^ 0xFFF) + 1)) & 0xFFF; /* ones compliment */
      /* opLDJimm_A_A (4a) */
      register_J = 0x0dea;
      /* opJLT_A_A (5b) */
      if (cmp_new < cmp_old) {
        register_PC = 0x0dea;
        break;
      };
      /* opAWDirg_A_AA (e7) */
      cmp_old = acc_a0 = register_A;
      register_A = (flag_C = (register_A + (cmp_new = ram[0x09]))) & 0xFFF;
      set_watchdog();
      /* opNOP_A_B (57) */
      /* opSUBimm_B_AA (31) */
      acc_a0 = register_A;
      register_B = (flag_C = ((cmp_old = register_B) + ((cmp_new = 0x1) ^ 0xFFF) + 1)) & 0xFFF; /* 1's-comp add */

    case 0x0dea:

      /* Invariants: register_P = 0x0 register_I = 0x09 */;
      /* opLSLDe_A_AA (ef) */
      cmp_new = 0x0FEF; cmp_old = acc_a0 = register_A; flag_C = (0x0FEF + register_A);
      register_A = (register_A << 1) & 0xFFF; register_B = (register_B << 1) & 0xFFF;
      /* opNOP_A_B (57) */
      /* opADDimm_B_AA (21) */
      acc_a0 = register_A; /* save old accA bit0 */
      cmp_old = register_B; /* store old acc for later */
      register_B = (flag_C = (register_B + (cmp_new = 0x01))) & 0xFFF; /* add values */
      /* opSUBirg_A_AA (e8) */
      register_A = (flag_C = ((cmp_old = acc_a0 = register_A) + ((cmp_new = ram[register_I]) ^ 0xFFF) + 1)) & 0xFFF; /* ones compliment */
      /* opLDJimm_A_A (44) */
      register_J = 0x0df4;
      /* opJLT_A_A (5b) */
      if (cmp_new < cmp_old) {
        register_PC = 0x0df4;
        break;
      };
      /* opAWDirg_A_AA (e7) */
      cmp_old = acc_a0 = register_A;
      register_A = (flag_C = (register_A + (cmp_new = ram[0x09]))) & 0xFFF;
      set_watchdog();
      /* opNOP_A_B (57) */
      /* opSUBimm_B_AA (31) */
      acc_a0 = register_A;
      register_B = (flag_C = ((cmp_old = register_B) + ((cmp_new = 0x1) ^ 0xFFF) + 1)) & 0xFFF; /* 1's-comp add */

    case 0x0df4:

      /* Invariants: register_P = 0x0 register_I = 0x09 */;
      /* opLSLDe_A_AA (ef) */
      cmp_new = 0x0FEF; cmp_old = acc_a0 = register_A; flag_C = (0x0FEF + register_A);
      register_A = (register_A << 1) & 0xFFF; register_B = (register_B << 1) & 0xFFF;
      /* opNOP_A_B (57) */
      /* opADDimm_B_AA (21) */
      acc_a0 = register_A; /* save old accA bit0 */
      cmp_old = register_B; /* store old acc for later */
      register_B = (flag_C = (register_B + (cmp_new = 0x01))) & 0xFFF; /* add values */
      /* opSUBirg_A_AA (e8) */
      register_A = (flag_C = ((cmp_old = acc_a0 = register_A) + ((cmp_new = ram[register_I]) ^ 0xFFF) + 1)) & 0xFFF; /* ones compliment */
      /* opLDJimm_A_A (4e) */
      register_J = 0x0dfe;
      /* opJLT_A_A (5b) */
      if (cmp_new < cmp_old) {
        register_PC = 0x0dfe;
        break;
      };
      /* opAWDirg_A_AA (e7) */
      cmp_old = acc_a0 = register_A;
      register_A = (flag_C = (register_A + (cmp_new = ram[0x09]))) & 0xFFF;
      set_watchdog();
      /* opNOP_A_B (57) */
      /* opSUBimm_B_AA (31) */
      acc_a0 = register_A;
      register_B = (flag_C = ((cmp_old = register_B) + ((cmp_new = 0x1) ^ 0xFFF) + 1)) & 0xFFF; /* 1's-comp add */

    case 0x0dfe:

      /* Invariants: register_P = 0x0 register_I = 0x09 */;
      /* opNOP_A_B (57) */
      /* opSTAdir_B_BB (d9) */
      ram[register_I = (register_P << 4) + 0x9] = register_B; /* set I register and store B to ram */
      /* opLDJimm_A_A (4b) */
      register_J = 0x0e0b;
      /* opJMP_A_A (58) */
      {
        register_PC = 0x0e0b;
        break;
      };

    case 0x0e03:

      /* opSUBirg_A_AA (e8) */
      register_A = (flag_C = ((cmp_old = acc_a0 = register_A) + ((cmp_new = ram[register_I]) ^ 0xFFF) + 1)) & 0xFFF; /* ones compliment */
      /* opSTAdir_A_A (dd) */
      ram[register_I = (register_P << 4) + 0xd] = register_A; /* store acc to RAM */
      /* opLDAdir_A_AA (a4) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = (register_P << 4) + 0x04]; /* set I register */

      /* opADDimm_A_AA (21) */
      register_A = (flag_C = ((cmp_old = acc_a0 = register_A) + (cmp_new = 0x1))) & 0xFFF; /* add values, save carry */

      /* opSTAirg_A_A (e6) */
      ram[register_I] = register_A; /* store acc */
      /* opLDJimm_A_A (43) */
      register_J = 0x0d93;
      /* opJMP_A_A (58) */
      {
        register_PC = 0x0d93;
        break;
      };

    case 0x0e0b:

      /* Invariants: register_P = 0x0 register_I = 0x09 */;
      /* opNOP_A_B (57) */
      /* opLDAimm_B_AA (00) */
      flag_C = acc_a0 = register_A;
      cmp_old = register_B; /* step back cmp flag */
      register_B = cmp_new = 0;
      /* opLDAimm_A_AA (08) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0x0800;
      /* opLDAdir_A_AA (a5) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = 0x05]; /* set I register */

      /* opLDJimm_A_A (44) */
      register_J = 0x0e14;
      /* opJLT_A_A (5b) */
      if (cmp_new < cmp_old) {
        register_PC = 0x0e14;
        break;
      };
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opSUBirg_A_AA (e8) */
      register_A = (flag_C = ((cmp_old = acc_a0 = register_A) + ((cmp_new = ram[register_I]) ^ 0xFFF) + 1)) & 0xFFF; /* ones compliment */

    case 0x0e14:

      /* Invariants: register_P = 0x0 register_I = 0x05 */;
      /* opCMPdir_A_AA (b9) */
      cmp_old = acc_a0 = register_A; /* backup old acc */
      flag_C = (((cmp_new = ram[register_I = 0x09]) ^ 0xFFF) + 1 + register_A);
      /* opMULirg_A_AA (e3) */
      MUL();
      /* opMULirg_A_AA (e3) */
      MUL();
      /* opMULirg_A_AA (e3) */
      MUL();
      /* opMULirg_A_AA (e3) */
      MUL();
      /* opMULirg_A_AA (e3) */
      MUL();
      /* opMULirg_A_AA (e3) */
      MUL();
      /* opMULirg_A_AA (e3) */
      MUL();
      /* opMULirg_A_AA (e3) */
      MUL();
      /* opMULirg_A_AA (e3) */
      MUL();
      /* opMULirg_A_AA (e3) */
      MUL();
      /* opMULirg_A_AA (e3) */
      MUL();
      /* opNOP_A_B (57) */
      /* opSTAdir_B_BB (d7) */
      ram[register_I = (register_P << 4) + 0x7] = register_B; /* set I register and store B to ram */
      /* opLDAimm_A_AA (08) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0x0800;
      /* opCMPdir_A_AA (b5) */
      cmp_old = acc_a0 = register_A; /* backup old acc */
      flag_C = (((cmp_new = ram[register_I = 0x05]) ^ 0xFFF) + 1 + register_A);
      /* opLDJimm_A_A (4a) */
      register_J = 0x0e2a;
      /* opJLT_A_A (5b) */
      if (cmp_new < cmp_old) {
        register_PC = 0x0e2a;
        break;
      };
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opSUBdir_A_AA (77) */
      cmp_old = acc_a0 = register_A;
      register_A = (flag_C = (register_A + ((cmp_new = ram[register_I = 0x07]) ^ 0xFFF) + 1)) & 0xFFF; /* set regI addr */
      /* opSTAirg_A_A (e6) */
      ram[0x07] = register_A; /* store acc */

    case 0x0e2a:

      /* Invariants: register_P = 0x0 */;
      /* opNOP_A_B (57) */
      /* opLDAimm_B_AA (00) */
      flag_C = acc_a0 = register_A;
      cmp_old = register_B; /* step back cmp flag */
      register_B = cmp_new = 0;
      /* opLDAimm_A_AA (08) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0x0800;
      /* opLDAdir_A_AA (a6) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = 0x06]; /* set I register */

      /* opLDJimm_A_A (43) */
      register_J = 0x0e33;
      /* opJLT_A_A (5b) */
      if (cmp_new < cmp_old) {
        register_PC = 0x0e33;
        break;
      };
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opSUBirg_A_AA (e8) */
      register_A = (flag_C = ((cmp_old = acc_a0 = register_A) + ((cmp_new = ram[register_I]) ^ 0xFFF) + 1)) & 0xFFF; /* ones compliment */

    case 0x0e33:

      /* Invariants: register_P = 0x0 register_I = 0x06 */;
      /* opCMPdir_A_AA (b9) */
      cmp_old = acc_a0 = register_A; /* backup old acc */
      flag_C = (((cmp_new = ram[register_I = 0x09]) ^ 0xFFF) + 1 + register_A);
      /* opMULirg_A_AA (e3) */
      MUL();
      /* opMULirg_A_AA (e3) */
      MUL();
      /* opMULirg_A_AA (e3) */
      MUL();
      /* opMULirg_A_AA (e3) */
      MUL();
      /* opMULirg_A_AA (e3) */
      MUL();
      /* opMULirg_A_AA (e3) */
      MUL();
      /* opMULirg_A_AA (e3) */
      MUL();
      /* opMULirg_A_AA (e3) */
      MUL();
      /* opMULirg_A_AA (e3) */
      MUL();
      /* opMULirg_A_AA (e3) */
      MUL();
      /* opMULirg_A_AA (e3) */
      MUL();
      /* opNOP_A_B (57) */
      /* opSTAdir_B_BB (d8) */
      ram[register_I = (register_P << 4) + 0x8] = register_B; /* set I register and store B to ram */
      /* opLDAimm_A_AA (08) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0x0800;
      /* opCMPdir_A_AA (b6) */
      cmp_old = acc_a0 = register_A; /* backup old acc */
      flag_C = (((cmp_new = ram[register_I = 0x06]) ^ 0xFFF) + 1 + register_A);
      /* opLDJimm_A_A (49) */
      register_J = 0x0e49;
      /* opJLT_A_A (5b) */
      if (cmp_new < cmp_old) {
        register_PC = 0x0e49;
        break;
      };
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opSUBdir_A_AA (78) */
      cmp_old = acc_a0 = register_A;
      register_A = (flag_C = (register_A + ((cmp_new = ram[register_I = 0x08]) ^ 0xFFF) + 1)) & 0xFFF; /* set regI addr */
      /* opSTAirg_A_A (e6) */
      ram[0x08] = register_A; /* store acc */

    case 0x0e49:

      /* Invariants: register_P = 0x0 */;
      /* opNOP_A_B (57) */
      /* opLDAimm_B_AA (00) */
      flag_C = acc_a0 = register_A;
      cmp_old = register_B; /* step back cmp flag */
      register_B = cmp_new = 0;
      /* opNOP_A_B (57) */
      /* opSUBdir_B_AA (74) */
      acc_a0 = register_A;
      register_B = (flag_C = ((cmp_old = register_B) + ((cmp_new = ram[register_I = (register_P << 4) + 0x04]) ^ 0xFFF) + 1)) & 0xFFF; /* ones compliment */
      /* opLDJimm_A_A (4b) */
      register_J = 0x0e5b;
      /* opJEQ_A_AA (5c) */
      if (cmp_new == cmp_old) {
        register_PC = 0x0e5b;
        break;
      };
      /* opLDJimm_A_A (42) */
      register_J = 0x0e52;

    case 0x0e52:

      /* opLDAdir_A_AA (a5) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = (register_P << 4) + 0x05]; /* set I register */

      /* opADDdir_A_AA (67) */
      register_A = (flag_C = ((cmp_old = acc_a0 = register_A) + (cmp_new = ram[register_I = (register_P << 4) + 0x7]))) & 0xFFF; /* do acc operation */
      /* opSTAirg_A_A (e6) */
      ram[register_I] = register_A; /* store acc */
      /* opLDAdir_A_AA (a6) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = (register_P << 4) + 0x06]; /* set I register */

      /* opADDdir_A_AA (68) */
      register_A = (flag_C = ((cmp_old = acc_a0 = register_A) + (cmp_new = ram[register_I = (register_P << 4) + 0x8]))) & 0xFFF; /* do acc operation */
      /* opSTAirg_A_A (e6) */
      ram[register_I] = register_A; /* store acc */
      /* opNOP_A_B (57) */
      /* opADDimm_B_AA (21) */
      acc_a0 = register_A; /* save old accA bit0 */
      cmp_old = register_B; /* store old acc for later */
      register_B = (flag_C = (register_B + (cmp_new = 0x01))) & 0xFFF; /* add values */
      /* opJNC_A_AA (5d) */
      if (!(flag_C & CARRYBIT)) {
        register_PC = 0x0e52;
        break;
      };

    case 0x0e5b:

      /* Invariants: register_P = 0x0 register_I = 0x04 */;
      /* opLDAimm_A_AA (03) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0x0300;
      /* opLSRe_A_AA (eb) */
      cmp_new = 0x0BEB; cmp_old = acc_a0 = register_A; flag_C = (0x0BEB + register_A);
      register_A >>= 1;
      /* opADDdir_A_AA (68) */
      register_A = (flag_C = ((cmp_old = acc_a0 = register_A) + (cmp_new = ram[register_I = 0x08]))) & 0xFFF; /* do acc operation */
      /* opLDPimm_A_A (81) */
      register_P = 0x1; /* set page register */
      /* opLDIdir_A_A (c9) */
      register_I = ram[0x19] & 0xff; /* set new register_I (8 bits) */
      /* opSTAirg_A_A (e6) */
      ram[register_I] = register_A; /* store acc */
      /* opLDAdir_A_AA (a9) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = 0x19]; /* set I register */

      /* opSUBimm_A_AA (31) */
      cmp_old = acc_a0 = register_A;
      register_A = (flag_C = (register_A + (((cmp_new = 0x1) ^ 0xFFF) + 1))) & 0xFFF; /* 1's-comp add */
      /* opSTAirg_A_A (e6) */
      ram[0x19] = register_A; /* store acc */
      /* opLDAimm_A_AA (02) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0x0200;
      /* opLDPimm_A_A (80) */
      register_P = 0x0; /* set page register */
      /* opADDdir_A_AA (67) */
      register_A = (flag_C = ((cmp_old = acc_a0 = register_A) + (cmp_new = ram[register_I = 0x07]))) & 0xFFF; /* do acc operation */
      /* opLDPimm_A_A (81) */
      register_P = 0x1; /* set page register */
      /* opLDIdir_A_A (c9) */
      register_I = ram[0x19] & 0xff; /* set new register_I (8 bits) */
      /* opSTAirg_A_A (e6) */
      ram[register_I] = register_A; /* store acc */
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opLDPimm_A_A (86) */
      register_P = 0x6; /* set page register */
      /* opCMPdir_A_AA (b2) */
      cmp_old = acc_a0 = register_A; /* backup old acc */
      flag_C = (((cmp_new = ram[register_I = 0x62]) ^ 0xFFF) + 1 + register_A);
      /* opLDJimm_A_A (47) */
      register_J = 0x0ee7;
      /* opJEQ_A_AA (5c) */
      if (cmp_new == cmp_old) {
        register_PC = 0x0ee7;
        break;
      };
      /* opADDimm_A_AA (23) */
      cmp_new = 0x3; cmp_old = acc_a0 = register_A; register_A = flag_C = 0x0003;

      /* opCMPdir_A_AA (b2) */
      cmp_old = acc_a0 = register_A; /* backup old acc */
      flag_C = (((cmp_new = ram[register_I = 0x62]) ^ 0xFFF) + 1 + register_A);
      /* opJNC_A_AA (5d) */
      if (!(flag_C & CARRYBIT)) {
        register_PC = 0x0ee7;
        break;
      };
      /* opLDAimm_A_AA (02) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0x0200;
      /* opLDPimm_A_A (81) */
      register_P = 0x1; /* set page register */
      /* opCMPdir_A_AA (b2) */
      cmp_old = acc_a0 = register_A; /* backup old acc */
      flag_C = (((cmp_new = ram[register_I = 0x12]) ^ 0xFFF) + 1 + register_A);
      /* opJLT_A_A (5b) */
      if (cmp_new < cmp_old) {
        register_PC = 0x0ee7;
        break;
      };
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opSUBirg_A_AA (e8) */
      register_A = (flag_C = ((cmp_old = acc_a0 = register_A) + ((cmp_new = ram[register_I]) ^ 0xFFF) + 1)) & 0xFFF; /* ones compliment */
      /* opLSRe_A_AA (eb) */
      cmp_new = 0x0BEB; cmp_old = acc_a0 = register_A; flag_C = (0x0BEB + register_A);
      register_A >>= 1;
      /* opLSRe_A_AA (eb) */
      cmp_new = 0x0BEB; cmp_old = acc_a0 = register_A; flag_C = (0x0BEB + register_A);
      register_A >>= 1;
      /* opLSRe_A_AA (eb) */
      cmp_new = 0x0BEB; cmp_old = acc_a0 = register_A; flag_C = (0x0BEB + register_A);
      register_A >>= 1;
      /* opLSRe_A_AA (eb) */
      cmp_new = 0x0BEB; cmp_old = acc_a0 = register_A; flag_C = (0x0BEB + register_A);
      register_A >>= 1;
      /* opLSRe_A_AA (eb) */
      cmp_new = 0x0BEB; cmp_old = acc_a0 = register_A; flag_C = (0x0BEB + register_A);
      register_A >>= 1;
      /* opLSRe_A_AA (eb) */
      cmp_new = 0x0BEB; cmp_old = acc_a0 = register_A; flag_C = (0x0BEB + register_A);
      register_A >>= 1;
      /* opLSRe_A_AA (eb) */
      cmp_new = 0x0BEB; cmp_old = acc_a0 = register_A; flag_C = (0x0BEB + register_A);
      register_A >>= 1;
      /* opADDimmX_A_AA (20) */
      register_A = (flag_C = ((acc_a0 = cmp_old = register_A) + (cmp_new = 0x0016))) & 0xFFF; /* add values */
      /* opLDPimm_A_A (80) */
      register_P = 0x0; /* set page register */
      /* opSTAdir_A_A (d9) */
      ram[register_I = 0x09] = register_A; /* store acc to RAM */
      /* opLDPimm_A_A (82) */
      register_P = 0x2; /* set page register */
      /* opLDAdir_A_AA (ac) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = 0x2c]; /* set I register */

      /* opLDPimm_A_A (80) */
      register_P = 0x0; /* set page register */
      /* opSUBdir_A_AA (77) */
      cmp_old = acc_a0 = register_A;
      register_A = (flag_C = (register_A + ((cmp_new = ram[register_I = 0x07]) ^ 0xFFF) + 1)) & 0xFFF; /* set regI addr */
      /* opSTAirg_A_A (e6) */
      ram[0x07] = register_A; /* store acc */
      /* opLDAimm_A_AA (08) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0x0800;
      /* opLDAirg_A_AA (ea) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = ram[register_I];
      /* opLDJimm_A_A (40) */
      register_J = 0x0e90;
      /* opJLT_A_A (5b) */
      if (cmp_new < cmp_old) {
        register_PC = 0x0e90;
        break;
      };
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opSUBirg_A_AA (e8) */
      register_A = (flag_C = ((cmp_old = acc_a0 = register_A) + ((cmp_new = ram[register_I]) ^ 0xFFF) + 1)) & 0xFFF; /* ones compliment */

    case 0x0e90:

      /* Invariants: register_P = 0x0 register_I = 0x07 */;
      /* opSUBdir_A_AA (79) */
      cmp_old = acc_a0 = register_A;
      register_A = (flag_C = (register_A + ((cmp_new = ram[register_I = 0x09]) ^ 0xFFF) + 1)) & 0xFFF; /* set regI addr */
      /* opLDJimm_A_A (47) */
      register_J = 0x0ee7;
      /* opJLT_A_A (5b) */
      if (cmp_new < cmp_old) {
        register_PC = 0x0ee7;
        break;
      };
      /* opLDPimm_A_A (82) */
      register_P = 0x2; /* set page register */
      /* opLDAdir_A_AA (ad) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = 0x2d]; /* set I register */

      /* opLDPimm_A_A (80) */
      register_P = 0x0; /* set page register */
      /* opSUBdir_A_AA (78) */
      cmp_old = acc_a0 = register_A;
      register_A = (flag_C = (register_A + ((cmp_new = ram[register_I = 0x08]) ^ 0xFFF) + 1)) & 0xFFF; /* set regI addr */
      /* opSTAirg_A_A (e6) */
      ram[0x08] = register_A; /* store acc */
      /* opLDAimm_A_AA (08) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0x0800;
      /* opLDAdir_A_AA (a8) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = 0x08]; /* set I register */

      /* opLDJimm_A_A (40) */
      register_J = 0x0ea0;
      /* opJLT_A_A (5b) */
      if (cmp_new < cmp_old) {
        register_PC = 0x0ea0;
        break;
      };
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opSUBirg_A_AA (e8) */
      register_A = (flag_C = ((cmp_old = acc_a0 = register_A) + ((cmp_new = ram[register_I]) ^ 0xFFF) + 1)) & 0xFFF; /* ones compliment */

    case 0x0ea0:

      /* Invariants: register_P = 0x0 register_I = 0x08 */;
      /* opSUBdir_A_AA (79) */
      cmp_old = acc_a0 = register_A;
      register_A = (flag_C = (register_A + ((cmp_new = ram[register_I = 0x09]) ^ 0xFFF) + 1)) & 0xFFF; /* set regI addr */
      /* opLDJimm_A_A (47) */
      register_J = 0x0ee7;
      /* opJLT_A_A (5b) */
      if (cmp_new < cmp_old) {
        register_PC = 0x0ee7;
        break;
      };
      /* opLDPimm_A_A (81) */
      register_P = 0x1; /* set page register */
      /* opLDAdir_A_AA (a9) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = 0x19]; /* set I register */

      /* opSUBimm_A_AA (34) */
      cmp_old = acc_a0 = register_A;
      register_A = (flag_C = (register_A + (((cmp_new = 0x4) ^ 0xFFF) + 1))) & 0xFFF; /* 1's-comp add */
      /* opSTAirg_A_A (e6) */
      ram[0x19] = register_A; /* store acc */
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opADDimm_A_AA (23) */
      cmp_new = 0x3; cmp_old = acc_a0 = register_A; register_A = flag_C = 0x0003;

      /* opLDIdir_A_A (c9) */
      register_I = ram[0x19] & 0xff; /* set new register_I (8 bits) */
      /* opSTAirg_A_A (e6) */
      ram[register_I] = register_A; /* store acc */

    case 0x0eac:

      /* Invariants: register_P = 0xdeadbeef */;
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opLDPimm_A_A (86) */
      register_P = 0x6; /* set page register */
      /* opSTAdir_A_A (df) */
      ram[register_I = 0x6f] = 0x000; /* store acc to RAM */
      /* opADDimm_A_AA (2f) */
      cmp_new = 0xf; cmp_old = acc_a0 = register_A; register_A = flag_C = 0x000f;

      /* opLDPimm_A_A (87) */
      register_P = 0x7; /* set page register */
      /* opSTAdir_A_A (d1) */
      ram[register_I = 0x71] = 0x00f; /* store acc to RAM */
      /* opLDPimm_A_A (82) */
      register_P = 0x2; /* set page register */
      /* opCMPdir_A_AA (b4) */
      cmp_old = acc_a0 = register_A; /* backup old acc */
      flag_C = (((cmp_new = ram[register_I = 0x24]) ^ 0xFFF) + 1 + register_A);
      /* opANDirg_A_AA (e9) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A &= (cmp_new = ram[register_I]);
      /* opLDPimm_A_A (86) */
      register_P = 0x6; /* set page register */
      /* opADDdir_A_AA (6b) */
      register_A = (flag_C = ((cmp_old = acc_a0 = register_A) + (cmp_new = ram[register_I = 0x6b]))) & 0xFFF; /* do acc operation */
      /* opSTAirg_A_A (e6) */
      ram[0x6b] = register_A; /* store acc */
      /* opSUBimm_A_AA (3a) */
      cmp_old = acc_a0 = register_A;
      register_A = (flag_C = (register_A + (((cmp_new = 0xa) ^ 0xFFF) + 1))) & 0xFFF; /* 1's-comp add */
      /* opLDJimm_A_A (40) */
      register_J = 0x0ec0;
      /* opJNC_A_AA (5d) */
      if (!(flag_C & CARRYBIT)) {
        register_PC = 0x0ec0;
        break;
      };
      /* opSTAirg_A_A (e6) */
      ram[0x6b] = register_A; /* store acc */
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opADDimm_A_AA (21) */
      cmp_new = 0x1; cmp_old = acc_a0 = register_A; register_A = flag_C = 0x0001;

      /* opSTAdir_A_A (df) */
      ram[register_I = 0x6f] = 0x001; /* store acc to RAM */

    case 0x0ec0:

      /* Invariants: register_P = 0x6 */;
      /* opLDPimm_A_A (82) */
      register_P = 0x2; /* set page register */
      /* opLDAdir_A_AA (a4) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = 0x24]; /* set I register */

      /* opLSRe_A_AA (eb) */
      cmp_new = 0x0BEB; cmp_old = acc_a0 = register_A; flag_C = (0x0BEB + register_A);
      register_A >>= 1;
      /* opLSRe_A_AA (eb) */
      cmp_new = 0x0BEB; cmp_old = acc_a0 = register_A; flag_C = (0x0BEB + register_A);
      register_A >>= 1;
      /* opLSRe_A_AA (eb) */
      cmp_new = 0x0BEB; cmp_old = acc_a0 = register_A; flag_C = (0x0BEB + register_A);
      register_A >>= 1;
      /* opLSRe_A_AA (eb) */
      cmp_new = 0x0BEB; cmp_old = acc_a0 = register_A; flag_C = (0x0BEB + register_A);
      register_A >>= 1;
      /* opLDPimm_A_A (86) */
      register_P = 0x6; /* set page register */
      /* opADDdir_A_AA (6f) */
      register_A = (flag_C = ((cmp_old = acc_a0 = register_A) + (cmp_new = ram[register_I = 0x6f]))) & 0xFFF; /* do acc operation */
      /* opADDdir_A_AA (6a) */
      register_A = (flag_C = ((cmp_old = acc_a0 = register_A) + (cmp_new = ram[register_I = 0x6a]))) & 0xFFF; /* do acc operation */
      /* opSTAirg_A_A (e6) */
      ram[0x6a] = register_A; /* store acc */
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opADDimm_A_AA (2f) */
      cmp_new = 0xf; cmp_old = acc_a0 = register_A; register_A = flag_C = 0x000f;

      /* opANDirg_A_AA (e9) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A &= (cmp_new = ram[register_I]);
      /* opSUBimm_A_AA (3a) */
      cmp_old = acc_a0 = register_A;
      register_A = (flag_C = (register_A + (((cmp_new = 0xa) ^ 0xFFF) + 1))) & 0xFFF; /* 1's-comp add */
      /* opLDJimm_A_A (4f) */
      register_J = 0x0edf;
      /* opJNC_A_AA (5d) */
      if (!(flag_C & CARRYBIT)) {
        register_PC = 0x0edf;
        break;
      };
      /* opLDAirg_A_AA (ea) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = ram[register_I];
      /* opADDimm_A_AA (26) */
      register_A = (flag_C = ((cmp_old = acc_a0 = register_A) + (cmp_new = 0x6))) & 0xFFF; /* add values, save carry */

      /* opSTAirg_A_A (e6) */
      ram[0x6a] = register_A; /* store acc */
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opADDimmX_A_AA (20) */
      cmp_old = register_A; cmp_new = 0xf0;
      register_A = flag_C = acc_a0 = 0x00f0;
      /* opANDirg_A_AA (e9) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A &= (cmp_new = ram[register_I]);
      /* opSUBimmX_A_AA (30) */
      cmp_old = acc_a0 = register_A; /* back up regA */
      register_A = (flag_C = (register_A + (((cmp_new = 0xa0) ^ 0xFFF) + 1))) & 0xFFF; /* add */
      /* opJNC_A_AA (5d) */
      if (!(flag_C & CARRYBIT)) {
        register_PC = 0x0edf;
        break;
      };
      /* opLDAirg_A_AA (ea) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = ram[register_I];
      /* opADDimmX_A_AA (20) */
      register_A = (flag_C = ((acc_a0 = cmp_old = register_A) + (cmp_new = 0x0060))) & 0xFFF; /* add values */
      /* opSTAirg_A_A (e6) */
      ram[0x6a] = register_A; /* store acc */

    case 0x0edf:

      /* Invariants: register_P = 0x6 register_I = 0x6a */;
      /* opLDPimm_A_A (82) */
      register_P = 0x2; /* set page register */
      /* opLDAdir_A_AA (a9) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = 0x29]; /* set I register */

      /* opADDimm_A_AA (21) */
      register_A = (flag_C = ((cmp_old = acc_a0 = register_A) + (cmp_new = 0x1))) & 0xFFF; /* add values, save carry */

      /* opSTAirg_A_A (e6) */
      ram[0x29] = register_A; /* store acc */
      /* opSUBimm_A_AA (33) */
      cmp_old = acc_a0 = register_A;
      register_A = (flag_C = (register_A + (((cmp_new = 0x3) ^ 0xFFF) + 1))) & 0xFFF; /* 1's-comp add */
      /* opLDJimm_A_A (4c) */
      register_J = 0x0eac;
      /* opJEQ_A_AA (5c) */
      if (cmp_new == cmp_old) {
        register_PC = 0x0eac;
        break;
      };

    case 0x0ee7:

      /* Invariants: register_P = 0xdeadbeef */;
      /* opLDPimm_A_A (80) */
      register_P = 0x0; /* set page register */
      /* opCMPdir_A_AA (b0) */
      cmp_old = acc_a0 = register_A; /* backup old acc */
      flag_C = (((cmp_new = ram[register_I = 0x00]) ^ 0xFFF) + 1 + register_A);
      /* opLDJirg_A_A (e1) */
      /* load J reg from value at last dir addr */
      register_J = ram[0x00];
      /* opJMP_A_A (58) */
      {
        register_PC = register_J;
        break;
      };

    case 0x0eeb:

      /* Invariants: register_P = 0x1 register_I = 0x1f */;
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opADDimmX_A_AA (20) */
      cmp_old = register_A; cmp_new = 0x7f;
      register_A = flag_C = acc_a0 = 0x007f;
      /* opCMPdir_A_AA (be) */
      cmp_old = acc_a0 = register_A; /* backup old acc */
      flag_C = (((cmp_new = ram[register_I = 0x1e]) ^ 0xFFF) + 1 + register_A);
      /* opANDirg_A_AA (e9) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A &= (cmp_new = ram[register_I]);
      /* opSTAirg_A_A (e6) */
      ram[0x1e] = register_A; /* store acc */
      /* opLDAimm_A_AA (0f) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0x0f00;
      /* opADDimmX_A_AA (20) */
      cmp_old = register_A; cmp_new = 0x6c;
      register_A = flag_C = acc_a0 = 0x0f6c;
      /* opSTAdir_A_A (da) */
      ram[register_I = 0x1a] = 0xf6c; /* store acc to RAM */
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opADDimmX_A_AA (20) */
      cmp_old = register_A; cmp_new = 0x20;
      register_A = flag_C = acc_a0 = 0x0020;
      /* opCMPdir_A_AA (be) */
      cmp_old = acc_a0 = register_A; /* backup old acc */
      flag_C = (((cmp_new = ram[register_I = 0x1e]) ^ 0xFFF) + 1 + register_A);
      /* opLDJimm_A_A (45) */
      register_J = 0x0f15;
      /* opJLT_A_A (5b) */
      if (cmp_new < cmp_old) {
        register_PC = 0x0f15;
        break;
      };
      /* opLSLe_A_AA (ec) */
      cmp_new = 0x0CEC;
      cmp_old = acc_a0 = register_A;
      flag_C = (0x0CEC + register_A);
      register_A = (register_A << 1) & 0x0FFF;
      /* opCMPdir_A_AA (be) */
      cmp_old = acc_a0 = register_A; /* backup old acc */
      flag_C = (((cmp_new = ram[register_I = 0x1e]) ^ 0xFFF) + 1 + register_A);
      /* opLDJimm_A_A (41) */
      register_J = 0x0f21;
      /* opJLT_A_A (5b) */
      if (cmp_new < cmp_old) {
        register_PC = 0x0f21;
        break;
      };
      /* opADDimmX_A_AA (20) */
      register_A = (flag_C = ((acc_a0 = cmp_old = register_A) + (cmp_new = 0x0020))) & 0xFFF; /* add values */
      /* opCMPdir_A_AA (be) */
      cmp_old = acc_a0 = register_A; /* backup old acc */
      flag_C = (((cmp_new = ram[register_I = 0x1e]) ^ 0xFFF) + 1 + register_A);
      /* opLDJimm_A_A (45) */
      register_J = 0x0f35;
      /* opJLT_A_A (5b) */
      if (cmp_new < cmp_old) {
        register_PC = 0x0f35;
        break;
      };
      /* opLDAdir_A_AA (aa) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = 0x1a]; /* set I register */

      /* opADDdir_A_AA (6e) */
      register_A = (flag_C = ((cmp_old = acc_a0 = register_A) + (cmp_new = ram[register_I = 0x1e]))) & 0xFFF; /* do acc operation */
      /* opSUBimmX_A_AA (30) */
      cmp_old = acc_a0 = register_A; /* back up regA */
      register_A = (flag_C = (register_A + (((cmp_new = 0x80) ^ 0xFFF) + 1))) & 0xFFF; /* add */
      /* opXLT_A_AA (e2) */
      cmp_old = register_A; register_A = cmp_new = rom[register_A]; /* new acc value */
      /* opNOP_A_A (5f) */
      /* opSTAdir_A_A (db) */
      ram[register_I = (register_P << 4) + 0xb] = register_A; /* store acc to RAM */
      /* opLDAdir_A_AA (aa) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = (register_P << 4) + 0x0a]; /* set I register */

      /* opADDimmX_A_AA (20) */
      register_A = (flag_C = ((acc_a0 = cmp_old = register_A) + (cmp_new = 0x0060))) & 0xFFF; /* add values */
      /* opSUBdir_A_AA (7e) */
      cmp_old = acc_a0 = register_A;
      register_A = (flag_C = (register_A + ((cmp_new = ram[register_I = (register_P << 4) + 0xe]) ^ 0xFFF) + 1)) & 0xFFF; /* set regI addr */
      /* opLDJimm_A_A (43) */
      register_J = 0x0f43;
      /* opJMP_A_A (58) */
      {
        register_PC = 0x0f43;
        break;
      };

    case 0x0f15:

      /* Invariants: register_P = 0x1 register_I = 0x1e register_A = 0x20 */;
      /* opLDAdir_A_AA (aa) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = 0x1a]; /* set I register */

      /* opSUBdir_A_AA (7e) */
      cmp_old = acc_a0 = register_A;
      register_A = (flag_C = (register_A + ((cmp_new = ram[register_I = 0x1e]) ^ 0xFFF) + 1)) & 0xFFF; /* set regI addr */
      /* opXLT_A_AA (e2) */
      cmp_old = register_A; register_A = cmp_new = rom[register_A]; /* new acc value */
      /* opNOP_A_A (5f) */
      /* opSTAdir_A_A (db) */
      ram[register_I = (register_P << 4) + 0xb] = register_A; /* store acc to RAM */
      /* opLDAimm_A_AA (0f) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0x0f00;
      /* opADDimmX_A_AA (20) */
      cmp_old = register_A; cmp_new = 0x4c;
      register_A = flag_C = acc_a0 = 0x0f4c;
      /* opADDdir_A_AA (6e) */
      register_A = (flag_C = ((cmp_old = acc_a0 = register_A) + (cmp_new = ram[register_I = (register_P << 4) + 0xe]))) & 0xFFF; /* do acc operation */
      /* opLDJimm_A_A (4f) */
      register_J = 0x0f2f;
      /* opJMP_A_A (58) */
      {
        register_PC = 0x0f2f;
        break;
      };

    case 0x0f21:

      /* opLDAdir_A_AA (aa) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = (register_P << 4) + 0x0a]; /* set I register */

      /* opADDdir_A_AA (6e) */
      register_A = (flag_C = ((cmp_old = acc_a0 = register_A) + (cmp_new = ram[register_I = (register_P << 4) + 0xe]))) & 0xFFF; /* do acc operation */
      /* opSUBimmX_A_AA (30) */
      cmp_old = acc_a0 = register_A; /* back up regA */
      register_A = (flag_C = (register_A + (((cmp_new = 0x40) ^ 0xFFF) + 1))) & 0xFFF; /* add */
      /* opXLT_A_AA (e2) */
      cmp_old = register_A; register_A = cmp_new = rom[register_A]; /* new acc value */
      /* opNOP_A_A (5f) */
      /* opSTAdir_A_A (db) */
      ram[register_I = (register_P << 4) + 0xb] = register_A; /* store acc to RAM */
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opSUBirg_A_AA (e8) */
      register_A = (flag_C = ((cmp_old = acc_a0 = register_A) + ((cmp_new = ram[register_I]) ^ 0xFFF) + 1)) & 0xFFF; /* ones compliment */
      /* opSTAirg_A_A (e6) */
      ram[register_I] = register_A; /* store acc */
      /* opLDAdir_A_AA (aa) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = (register_P << 4) + 0x0a]; /* set I register */

      /* opADDimmX_A_AA (20) */
      register_A = (flag_C = ((acc_a0 = cmp_old = register_A) + (cmp_new = 0x0020))) & 0xFFF; /* add values */
      /* opSUBdir_A_AA (7e) */
      cmp_old = acc_a0 = register_A;
      register_A = (flag_C = (register_A + ((cmp_new = ram[register_I = (register_P << 4) + 0xe]) ^ 0xFFF) + 1)) & 0xFFF; /* set regI addr */

    case 0x0f2f:

      /* Invariants: register_P = 0x1 register_I = 0x1e */;
      /* opXLT_A_AA (e2) */
      cmp_old = register_A; register_A = cmp_new = rom[register_A]; /* new acc value */
      /* opNOP_A_A (5f) */
      /* opSTAdir_A_A (da) */
      ram[register_I = (register_P << 4) + 0xa] = register_A; /* store acc to RAM */
      /* opLDJimm_A_A (49) */
      register_J = 0x0f49;
      /* opJMP_A_A (58) */
      {
        register_PC = 0x0f49;
        break;
      };

    case 0x0f35:

      /* opLDAdir_A_AA (aa) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = (register_P << 4) + 0x0a]; /* set I register */

      /* opADDimmX_A_AA (20) */
      register_A = (flag_C = ((acc_a0 = cmp_old = register_A) + (cmp_new = 0x0040))) & 0xFFF; /* add values */
      /* opSUBdir_A_AA (7e) */
      cmp_old = acc_a0 = register_A;
      register_A = (flag_C = (register_A + ((cmp_new = ram[register_I = (register_P << 4) + 0xe]) ^ 0xFFF) + 1)) & 0xFFF; /* set regI addr */
      /* opXLT_A_AA (e2) */
      cmp_old = register_A; register_A = cmp_new = rom[register_A]; /* new acc value */
      /* opNOP_A_A (5f) */
      /* opSTAdir_A_A (db) */
      ram[register_I = (register_P << 4) + 0xb] = register_A; /* store acc to RAM */
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opSUBirg_A_AA (e8) */
      register_A = (flag_C = ((cmp_old = acc_a0 = register_A) + ((cmp_new = ram[register_I]) ^ 0xFFF) + 1)) & 0xFFF; /* ones compliment */
      /* opSTAirg_A_A (e6) */
      ram[register_I] = register_A; /* store acc */
      /* opLDAdir_A_AA (aa) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = (register_P << 4) + 0x0a]; /* set I register */

      /* opADDdir_A_AA (6e) */
      register_A = (flag_C = ((cmp_old = acc_a0 = register_A) + (cmp_new = ram[register_I = (register_P << 4) + 0xe]))) & 0xFFF; /* do acc operation */
      /* opSUBimmX_A_AA (30) */
      cmp_old = acc_a0 = register_A; /* back up regA */
      register_A = (flag_C = (register_A + (((cmp_new = 0x60) ^ 0xFFF) + 1))) & 0xFFF; /* add */

    case 0x0f43:

      /* Invariants: register_P = 0x1 register_I = 0x1e */;
      /* opXLT_A_AA (e2) */
      cmp_old = register_A; register_A = cmp_new = rom[register_A]; /* new acc value */
      /* opNOP_A_A (5f) */
      /* opSTAdir_A_A (da) */
      ram[register_I = (register_P << 4) + 0xa] = register_A; /* store acc to RAM */
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opSUBirg_A_AA (e8) */
      register_A = (flag_C = ((cmp_old = acc_a0 = register_A) + ((cmp_new = ram[register_I]) ^ 0xFFF) + 1)) & 0xFFF; /* ones compliment */
      /* opSTAirg_A_A (e6) */
      ram[register_I] = register_A; /* store acc */

    case 0x0f49:

      /* Invariants: register_P = 0x1 register_I = 0x1a */;
      /* opCMPdir_A_AA (bf) */
      cmp_old = acc_a0 = register_A; /* backup old acc */
      flag_C = (((cmp_new = ram[register_I = 0x1f]) ^ 0xFFF) + 1 + register_A);
      /* opLDJirg_A_A (e1) */
      /* load J reg from value at last dir addr */
      register_J = ram[0x1f];
      /* opJMP_A_A (58) */
      {
        register_PC = register_J;
        break;
      };
    /*********************************************************/
    default:
      /* Jumping to any illegal address or end of eprom will come here */
      /* we ought to reinitialise or something */
      ;
  }
}

// ops0800.c

void cineExecute0800(void)
{
  switch (register_PC)
  {
    case 0x0820:

      /* Invariants: register_P = 0x0 register_I = 0x07 */;
      /* opSTAdir_A_A (de) */
      ram[register_I = 0x0e] = register_A; /* store acc to RAM */
      /* opLDAdir_A_AA (a6) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = 0x06]; /* set I register */

      /* opLDPimm_A_A (86) */
      register_P = 0x6; /* set page register */
      /* opSUBdir_A_AA (74) */
      cmp_old = acc_a0 = register_A;
      register_A = (flag_C = (register_A + ((cmp_new = ram[register_I = 0x64]) ^ 0xFFF) + 1)) & 0xFFF; /* set regI addr */
      /* opASRe_A_AA (ed) */
      cmp_new = 0xDED; cmp_old = flag_C = acc_a0 = register_A;
      register_A = SEX(register_A); /* make signed */
      register_A = (((signed short int)(((signed short int)register_A) >> (signed short int)1)) /* SAR */) & 0xFFF;
      /* opLDPimm_A_A (80) */
      register_P = 0x0; /* set page register */
      /* opSTAdir_A_A (d8) */
      ram[register_I = 0x08] = register_A; /* store acc to RAM */
      /* opNOP_A_B (57) */
      /* opLDAirg_B_AA (ea) */
      flag_C = acc_a0 = register_A;
      cmp_old = register_B;
      register_B = cmp_new = ram[register_I];

      /* opLDAimm_A_AA (08) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0x0800;
      /* opLDAirg_A_AA (ea) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = ram[register_I];
      /* opLDJimm_A_A (40) */
      register_J = 0x0830;
      /* opJLT_A_A (5b) */
      if (cmp_new < cmp_old) {
        register_PC = 0x0830;
        break;
      };
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opSUBirg_A_AA (e8) */
      register_A = (flag_C = ((cmp_old = acc_a0 = register_A) + ((cmp_new = ram[register_I]) ^ 0xFFF) + 1)) & 0xFFF; /* ones compliment */

    case 0x0830:

      /* Invariants: register_P = 0x0 register_I = 0x08 */;
      /* opADDdir_A_AA (6e) */
      register_A = (flag_C = ((cmp_old = acc_a0 = register_A) + (cmp_new = ram[register_I = 0x0e]))) & 0xFFF; /* do acc operation */
      /* opSUBimm_A_AA (33) */
      cmp_old = acc_a0 = register_A;
      register_A = (flag_C = (register_A + (((cmp_new = 0x3) ^ 0xFFF) + 1))) & 0xFFF; /* 1's-comp add */
      /* opLDJimm_A_A (45) */
      register_J = 0x0855;
      /* opJNC_A_AA (5d) */
      if (!(flag_C & CARRYBIT)) {
        register_PC = 0x0855;
        break;
      };
      /* opLDAdir_A_AA (a7) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = 0x07]; /* set I register */

      /* opLLT_A_AA (e4) */
      { CINEBYTE temp_byte = 0;
        for (;;) {
          if (   (((register_A >> 8) & 0x0A) && (((register_A >> 8) & 0x0A) ^ 0x0A))
                 ||   (((register_B >> 8) & 0x0A) && (((register_B >> 8) & 0x0A) ^ 0x0A))  ) break;
          register_A <<= 1; register_B <<= 1;
          if (!(++temp_byte)) break /* This may not be correct */;
        }
        vgShiftLength = temp_byte & 0xfff; register_A &= 0x0FFF; register_B &= 0x0FFF;
      }
      /* opADDdir_A_AA (65) */
      register_A = (flag_C = ((cmp_old = acc_a0 = register_A) + (cmp_new = ram[register_I = 0x05]))) & 0xFFF; /* do acc operation */
      /* opNOP_A_B (57) */
      /* opADDdir_B_AA (66) */
      acc_a0 = register_A; /* store old acc value */
      register_B = (flag_C = ((cmp_old = register_B) + (cmp_new = ram[register_I = 0x06]))) & 0xFFF; /* do acc operation */
      /* opVDR_A_A (e0) */
      {
        /* set ending points and draw the vector, or buffer for a later draw. */
        int ToX = register_A & 0xFFF;
        int ToY = register_B & 0xFFF;

        /* Sign extend from 20 bit CCPU to 32bit target machine */
        FromX = SEX(FromX);
        ToX = SEX(ToX);
        FromY = SEX(FromY);
        ToY = SEX(ToY);

        /* figure out the vector */
        ToX -= FromX;
        ToX = ((signed short int)(((signed short int)ToX) >> (signed short int)vgShiftLength)) /* SAR */;
        ToX += FromX;

        ToY -= FromY;
        ToY = ((signed short int)(((signed short int)ToY) >> (signed short int)vgShiftLength)) /* SAR */;
        ToY += FromY;

        /* render the line */
#ifndef DUALCPU
        CinemaVectorData (FromX, FromY, ToX, ToY, vgColour);
#endif

      }
      /* opLDJimm_A_A (45) */
      register_J = 0x0855;
      /* opJMP_A_A (58) */
      {
        register_PC = 0x0855;
        break;
      };

    case 0x083e:

      /* opLDPimm_A_A (80) */
      register_P = 0x0; /* set page register */
      /* opLDAdir_A_AA (aa) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = 0x0a]; /* set I register */

      /* opLDPimm_A_A (86) */
      register_P = 0x6; /* set page register */
      /* opSUBdir_A_AA (73) */
      cmp_old = acc_a0 = register_A;
      register_A = (flag_C = (register_A + ((cmp_new = ram[register_I = 0x63]) ^ 0xFFF) + 1)) & 0xFFF; /* set regI addr */
      /* opASRe_A_AA (ed) */
      cmp_new = 0xDED; cmp_old = flag_C = acc_a0 = register_A;
      register_A = SEX(register_A); /* make signed */
      register_A = (((signed short int)(((signed short int)register_A) >> (signed short int)1)) /* SAR */) & 0xFFF;
      /* opLDPimm_A_A (80) */
      register_P = 0x0; /* set page register */
      /* opSTAdir_A_A (d7) */
      ram[register_I = 0x07] = register_A; /* store acc to RAM */
      /* opLDAdir_A_AA (ab) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = 0x0b]; /* set I register */

      /* opLDPimm_A_A (86) */
      register_P = 0x6; /* set page register */
      /* opSUBdir_A_AA (74) */
      cmp_old = acc_a0 = register_A;
      register_A = (flag_C = (register_A + ((cmp_new = ram[register_I = 0x64]) ^ 0xFFF) + 1)) & 0xFFF; /* set regI addr */
      /* opASRe_A_AA (ed) */
      cmp_new = 0xDED; cmp_old = flag_C = acc_a0 = register_A;
      register_A = SEX(register_A); /* make signed */
      register_A = (((signed short int)(((signed short int)register_A) >> (signed short int)1)) /* SAR */) & 0xFFF;
      /* opLDPimm_A_A (80) */
      register_P = 0x0; /* set page register */
      /* opSTAdir_A_A (d8) */
      ram[register_I = 0x08] = register_A; /* store acc to RAM */
      /* opLDAdir_A_AA (a7) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = 0x07]; /* set I register */

      /* opASRe_A_AA (ed) */
      cmp_new = 0xDED; cmp_old = flag_C = acc_a0 = register_A;
      register_A = SEX(register_A); /* make signed */
      register_A = (((signed short int)(((signed short int)register_A) >> (signed short int)1)) /* SAR */) & 0xFFF;
      /* opASRe_A_AA (ed) */
      cmp_new = 0xDED; cmp_old = flag_C = acc_a0 = register_A;
      register_A = SEX(register_A); /* make signed */
      register_A = (((signed short int)(((signed short int)register_A) >> (signed short int)1)) /* SAR */) & 0xFFF;
      /* opSTAdir_A_A (de) */
      ram[register_I = 0x0e] = register_A; /* store acc to RAM */
      /* opLDAimm_A_AA (08) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0x0800;
      /* opADDimmX_A_AA (20) */
      cmp_old = register_A; cmp_new = 0x55;
      register_A = flag_C = acc_a0 = 0x0855;
      /* opLDJimm_A_A (4b) */
      register_J = 0x073b;
      /* opJMP_A_A (58) */
      {
        register_PC = 0x073b;
        break;
      };

    case 0x0855:

      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opLDPimm_A_A (86) */
      register_P = 0x6; /* set page register */
      /* opLDAdir_A_AA (a2) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = 0x62]; /* set I register */

      /* opLDJimm_A_A (4d) */
      register_J = 0x085d;
      /* opJEQ_A_AA (5c) */
      if (cmp_new == cmp_old) {
        register_PC = 0x085d;
        break;
      };
      /* opSUBimm_A_AA (31) */
      cmp_old = acc_a0 = register_A;
      register_A = (flag_C = (register_A + (((cmp_new = 0x1) ^ 0xFFF) + 1))) & 0xFFF; /* 1's-comp add */
      /* opSTAirg_A_A (e6) */
      ram[0x62] = register_A; /* store acc */

    case 0x085d:

      /* Invariants: register_P = 0x6 register_I = 0x62 */;
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opLDPimm_A_A (86) */
      /* opCMPdir_A_AA (b9) */
      cmp_old = acc_a0 = register_A; /* backup old acc */
      flag_C = (((cmp_new = ram[register_I = 0x69]) ^ 0xFFF) + 1 + register_A);
      /* opLDJimm_A_A (4d) */
      register_J = 0x08ed;
      /* opJEQ_A_AA (5c) */
      if (cmp_new == cmp_old) {
        register_PC = 0x08ed;
        break;
      };
      /* opADDimm_A_AA (22) */
      cmp_new = 0x2; cmp_old = acc_a0 = register_A; register_A = flag_C = 0x0002;

      /* opLDPimm_A_A (83) */
      register_P = 0x3; /* set page register */
      /* opCMPdir_A_AA (b7) */
      cmp_old = acc_a0 = register_A; /* backup old acc */
      flag_C = (((cmp_new = ram[register_I = 0x37]) ^ 0xFFF) + 1 + register_A);
      /* opLDJimm_A_A (4e) */
      register_J = 0x088e;
      /* opJLT_A_A (5b) */
      if (cmp_new < cmp_old) {
        register_PC = 0x088e;
        break;
      };
      /* opADDimm_A_AA (25) */
      cmp_new = 0x5; cmp_old = acc_a0 = register_A; register_A = flag_C = 0x0007;

      /* opCMPdir_A_AA (b7) */
      cmp_old = acc_a0 = register_A; /* backup old acc */
      flag_C = (((cmp_new = ram[register_I = 0x37]) ^ 0xFFF) + 1 + register_A);
      /* opJNC_A_AA (5d) */
      if (!(flag_C & CARRYBIT)) {
        register_PC = 0x088e;
        break;
      };
      /* opLDJimm_A_A (48) */
      register_J = 0x0898;
      /* opJMP_A_A (58) */
      {
        register_PC = 0x0898;
        break;
      };

    case 0x086f:

      /* Invariants: register_P = 0x6 register_I = 0x69 register_A = 0x02 */;
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opADDimmX_A_AA (20) */
      cmp_old = register_A; cmp_new = 0x18;
      register_A = flag_C = acc_a0 = 0x0018;
      /* opOUTbi_A_A (93) */
      /* opOUTsnd_A (93) */
      set_sound_data(0);
      /* opLSRe_A_AA (eb) */
      cmp_new = 0x0BEB; cmp_old = acc_a0 = register_A; flag_C = (0x0BEB + register_A);
      register_A >>= 1;
      /* opOUTbi_A_A (90) */
      /* opOUTsnd_A (90) */
      set_sound_addr_A(register_A & 1);
      /* opLSRe_A_AA (eb) */
      cmp_new = 0x0BEB; cmp_old = acc_a0 = register_A; flag_C = (0x0BEB + register_A);
      register_A >>= 1;
      /* opOUTbi_A_A (91) */
      /* opOUTsnd_A (91) */
      set_sound_addr_B(register_A & 1);
      /* opLSRe_A_AA (eb) */
      cmp_new = 0x0BEB; cmp_old = acc_a0 = register_A; flag_C = (0x0BEB + register_A);
      register_A >>= 1;
      /* opOUTbi_A_A (92) */
      /* opOUTsnd_A (92) */
      set_sound_addr_C(register_A & 1);
      /* opLSRe_A_AA (eb) */
      cmp_new = 0x0BEB; cmp_old = acc_a0 = register_A; flag_C = (0x0BEB + register_A);
      register_A >>= 1;
      /* opOUTbi_A_A (94) */
      /* opOUTsnd_A (94) */
      if (register_A & 1) strobe_sound_on(); else strobe_sound_off();
      /* opLSRe_A_AA (eb) */
      cmp_new = 0x0BEB; cmp_old = acc_a0 = register_A; flag_C = (0x0BEB + register_A);
      register_A >>= 1;
      /* opOUTbi_A_A (94) */
      /* opOUTsnd_A (94) */
      if (register_A & 1) strobe_sound_on(); else strobe_sound_off();
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opADDimmX_A_AA (20) */
      cmp_old = register_A; cmp_new = 0x16;
      register_A = flag_C = acc_a0 = 0x0016;
      /* opOUTbi_A_A (93) */
      /* opOUTsnd_A (93) */
      set_sound_data(0);
      /* opLSRe_A_AA (eb) */
      cmp_new = 0x0BEB; cmp_old = acc_a0 = register_A; flag_C = (0x0BEB + register_A);
      register_A >>= 1;
      /* opOUTbi_A_A (90) */
      /* opOUTsnd_A (90) */
      set_sound_addr_A(register_A & 1);
      /* opLSRe_A_AA (eb) */
      cmp_new = 0x0BEB; cmp_old = acc_a0 = register_A; flag_C = (0x0BEB + register_A);
      register_A >>= 1;
      /* opOUTbi_A_A (91) */
      /* opOUTsnd_A (91) */
      set_sound_addr_B(register_A & 1);
      /* opLSRe_A_AA (eb) */
      cmp_new = 0x0BEB; cmp_old = acc_a0 = register_A; flag_C = (0x0BEB + register_A);
      register_A >>= 1;
      /* opOUTbi_A_A (92) */
      /* opOUTsnd_A (92) */
      set_sound_addr_C(register_A & 1);
      /* opLSRe_A_AA (eb) */
      cmp_new = 0x0BEB; cmp_old = acc_a0 = register_A; flag_C = (0x0BEB + register_A);
      register_A >>= 1;
      /* opOUTbi_A_A (94) */
      /* opOUTsnd_A (94) */
      if (register_A & 1) strobe_sound_on(); else strobe_sound_off();
      /* opLSRe_A_AA (eb) */
      cmp_new = 0x0BEB; cmp_old = acc_a0 = register_A; flag_C = (0x0BEB + register_A);
      register_A >>= 1;
      /* opOUTbi_A_A (94) */
      /* opOUTsnd_A (94) */
      if (register_A & 1) strobe_sound_on(); else strobe_sound_off();
      /* opLDJimm_A_A (4e) */
      register_J = 0x045e;
      /* opJMP_A_A (58) */
      {
        register_PC = 0x045e;
        break;
      };

    case 0x088e:

      /* Invariants: register_P = 0x3 register_I = 0x37 */;
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opADDimm_A_AA (22) */
      cmp_new = 0x2; cmp_old = acc_a0 = register_A; register_A = flag_C = 0x0002;

      /* opLDPimm_A_A (84) */
      register_P = 0x4; /* set page register */
      /* opCMPdir_A_AA (b7) */
      cmp_old = acc_a0 = register_A; /* backup old acc */
      flag_C = (((cmp_new = ram[register_I = 0x47]) ^ 0xFFF) + 1 + register_A);
      /* opLDJimm_A_A (49) */
      register_J = 0x08a9;
      /* opJLT_A_A (5b) */
      if (cmp_new < cmp_old) {
        register_PC = 0x08a9;
        break;
      };
      /* opADDimm_A_AA (25) */
      cmp_new = 0x5; cmp_old = acc_a0 = register_A; register_A = flag_C = 0x0007;

      /* opCMPdir_A_AA (b7) */
      cmp_old = acc_a0 = register_A; /* backup old acc */
      flag_C = (((cmp_new = ram[register_I = 0x47]) ^ 0xFFF) + 1 + register_A);
      /* opJNC_A_AA (5d) */
      if (!(flag_C & CARRYBIT)) {
        register_PC = 0x08a9;
        break;
      };

    case 0x0898:

      /* Invariants: register_P = 0xdeadbeef register_A = 0x07 */;
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opADDimmX_A_AA (20) */
      cmp_old = register_A; cmp_new = 0x1f;
      register_A = flag_C = acc_a0 = 0x001f;
      /* opOUTbi_A_A (93) */
      /* opOUTsnd_A (93) */
      set_sound_data(1);
      /* opLSRe_A_AA (eb) */
      cmp_new = 0x0BEB; cmp_old = acc_a0 = register_A; flag_C = (0x0BEB + register_A);
      register_A >>= 1;
      /* opOUTbi_A_A (90) */
      /* opOUTsnd_A (90) */
      set_sound_addr_A(register_A & 1);
      /* opLSRe_A_AA (eb) */
      cmp_new = 0x0BEB; cmp_old = acc_a0 = register_A; flag_C = (0x0BEB + register_A);
      register_A >>= 1;
      /* opOUTbi_A_A (91) */
      /* opOUTsnd_A (91) */
      set_sound_addr_B(register_A & 1);
      /* opLSRe_A_AA (eb) */
      cmp_new = 0x0BEB; cmp_old = acc_a0 = register_A; flag_C = (0x0BEB + register_A);
      register_A >>= 1;
      /* opOUTbi_A_A (92) */
      /* opOUTsnd_A (92) */
      set_sound_addr_C(register_A & 1);
      /* opLSRe_A_AA (eb) */
      cmp_new = 0x0BEB; cmp_old = acc_a0 = register_A; flag_C = (0x0BEB + register_A);
      register_A >>= 1;
      /* opOUTbi_A_A (94) */
      /* opOUTsnd_A (94) */
      if (register_A & 1) strobe_sound_on(); else strobe_sound_off();
      /* opLSRe_A_AA (eb) */
      cmp_new = 0x0BEB; cmp_old = acc_a0 = register_A; flag_C = (0x0BEB + register_A);
      register_A >>= 1;
      /* opOUTbi_A_A (94) */
      /* opOUTsnd_A (94) */
      if (register_A & 1) strobe_sound_on(); else strobe_sound_off();
      /* opLDJimm_A_A (4b) */
      register_J = 0x08fb;
      /* opJMP_A_A (58) */
      {
        register_PC = 0x08fb;
        break;
      };

    case 0x08a9:

      /* Invariants: register_P = 0x4 register_I = 0x47 */;
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opADDimm_A_AA (22) */
      cmp_new = 0x2; cmp_old = acc_a0 = register_A; register_A = flag_C = 0x0002;

      /* opLDPimm_A_A (85) */
      register_P = 0x5; /* set page register */
      /* opCMPdir_A_AA (b7) */
      cmp_old = acc_a0 = register_A; /* backup old acc */
      flag_C = (((cmp_new = ram[register_I = 0x57]) ^ 0xFFF) + 1 + register_A);
      /* opLDJimm_A_A (4d) */
      register_J = 0x08ed;
      /* opJLT_A_A (5b) */
      if (cmp_new < cmp_old) {
        register_PC = 0x08ed;
        break;
      };
      /* opADDimm_A_AA (25) */
      cmp_new = 0x5; cmp_old = acc_a0 = register_A; register_A = flag_C = 0x0007;

      /* opCMPdir_A_AA (b7) */
      cmp_old = acc_a0 = register_A; /* backup old acc */
      flag_C = (((cmp_new = ram[register_I = 0x57]) ^ 0xFFF) + 1 + register_A);
      /* opJNC_A_AA (5d) */
      if (!(flag_C & CARRYBIT)) {
        register_PC = 0x08ed;
        break;
      };
      /* opLDJimm_A_A (48) */
      register_J = 0x0898;
      /* opJMP_A_A (58) */
      {
        register_PC = 0x0898;
        break;
      };

    case 0x08b6:

      /* Invariants: register_P = 0xc register_I = 0xc1 register_A = 0xc0 */;
      /* opNOP_A_A (5f) */
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opLDPimm_A_A (87) */
      register_P = 0x7; /* set page register */
      /* opCMPdir_A_AA (ba) */
      cmp_old = acc_a0 = register_A; /* backup old acc */
      flag_C = (((cmp_new = ram[register_I = 0x7a]) ^ 0xFFF) + 1 + register_A);
      /* opLDJimm_A_A (4d) */
      register_J = 0x08ed;
      /* opJNC_A_AA (5d) */
      if (!(flag_C & CARRYBIT)) {
        register_PC = 0x08ed;
        break;
      };
      /* opLDPimm_A_A (83) */
      register_P = 0x3; /* set page register */
      /* opCMPdir_A_AA (b0) */
      cmp_old = acc_a0 = register_A; /* backup old acc */
      flag_C = (((cmp_new = ram[register_I = 0x30]) ^ 0xFFF) + 1 + register_A);
      /* opLDJimm_A_A (48) */
      register_J = 0x0308;
      /* opJNC_A_AA (5d) */
      if (!(flag_C & CARRYBIT)) {
        register_PC = 0x0308;
        break;
      };
      /* opLDPimm_A_A (84) */
      register_P = 0x4; /* set page register */
      /* opCMPdir_A_AA (b0) */
      cmp_old = acc_a0 = register_A; /* backup old acc */
      flag_C = (((cmp_new = ram[register_I = 0x40]) ^ 0xFFF) + 1 + register_A);
      /* opJNC_A_AA (5d) */
      if (!(flag_C & CARRYBIT)) {
        register_PC = 0x0308;
        break;
      };
      /* opLDPimm_A_A (85) */
      register_P = 0x5; /* set page register */
      /* opCMPdir_A_AA (b0) */
      cmp_old = acc_a0 = register_A; /* backup old acc */
      flag_C = (((cmp_new = ram[register_I = 0x50]) ^ 0xFFF) + 1 + register_A);
      /* opJNC_A_AA (5d) */
      if (!(flag_C & CARRYBIT)) {
        register_PC = 0x0308;
        break;
      };
      /* opLDPimm_A_A (87) */
      register_P = 0x7; /* set page register */
      /* opCMPdir_A_AA (bd) */
      cmp_old = acc_a0 = register_A; /* backup old acc */
      flag_C = (((cmp_new = ram[register_I = 0x7d]) ^ 0xFFF) + 1 + register_A);
      /* opLDJimm_A_A (46) */
      register_J = 0x02b6;
      /* opJNC_A_AA (5d) */
      if (!(flag_C & CARRYBIT)) {
        register_PC = 0x02b6;
        break;
      };
      /* opLDPimm_A_A (86) */
      register_P = 0x6; /* set page register */
      /* opCMPdir_A_AA (b9) */
      cmp_old = acc_a0 = register_A; /* backup old acc */
      flag_C = (((cmp_new = ram[register_I = 0x69]) ^ 0xFFF) + 1 + register_A);
      /* opLDJimm_A_A (4e) */
      register_J = 0x02de;
      /* opJNC_A_AA (5d) */
      if (!(flag_C & CARRYBIT)) {
        register_PC = 0x02de;
        break;
      };
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opADDimmX_A_AA (20) */
      cmp_old = register_A; cmp_new = 0x14;
      register_A = flag_C = acc_a0 = 0x0014;
      /* opOUTbi_A_A (93) */
      /* opOUTsnd_A (93) */
      set_sound_data(0);
      /* opLSRe_A_AA (eb) */
      cmp_new = 0x0BEB; cmp_old = acc_a0 = register_A; flag_C = (0x0BEB + register_A);
      register_A >>= 1;
      /* opOUTbi_A_A (90) */
      /* opOUTsnd_A (90) */
      set_sound_addr_A(register_A & 1);
      /* opLSRe_A_AA (eb) */
      cmp_new = 0x0BEB; cmp_old = acc_a0 = register_A; flag_C = (0x0BEB + register_A);
      register_A >>= 1;
      /* opOUTbi_A_A (91) */
      /* opOUTsnd_A (91) */
      set_sound_addr_B(register_A & 1);
      /* opLSRe_A_AA (eb) */
      cmp_new = 0x0BEB; cmp_old = acc_a0 = register_A; flag_C = (0x0BEB + register_A);
      register_A >>= 1;
      /* opOUTbi_A_A (92) */
      /* opOUTsnd_A (92) */
      set_sound_addr_C(register_A & 1);
      /* opLSRe_A_AA (eb) */
      cmp_new = 0x0BEB; cmp_old = acc_a0 = register_A; flag_C = (0x0BEB + register_A);
      register_A >>= 1;
      /* opOUTbi_A_A (94) */
      /* opOUTsnd_A (94) */
      if (register_A & 1) strobe_sound_on(); else strobe_sound_off();
      /* opLSRe_A_AA (eb) */
      cmp_new = 0x0BEB; cmp_old = acc_a0 = register_A; flag_C = (0x0BEB + register_A);
      register_A >>= 1;
      /* opOUTbi_A_A (94) */
      /* opOUTsnd_A (94) */
      if (register_A & 1) strobe_sound_on(); else strobe_sound_off();
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opLDPimm_A_A (87) */
      register_P = 0x7; /* set page register */
      /* opSTAdir_A_A (d3) */
      ram[register_I = 0x73] = 0x000; /* store acc to RAM */
      /* opLDAimm_A_AA (0f) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0x0f00;
      /* opLDPimm_A_A (87) */
      /* opSTAdir_A_A (de) */
      ram[register_I = 0x7e] = 0xf00; /* store acc to RAM */
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opADDimmX_A_AA (20) */
      cmp_old = register_A; cmp_new = 0x10;
      register_A = flag_C = acc_a0 = 0x0010;
      /* opSTAdir_A_A (dc) */
      ram[register_I = 0x7c] = 0x010; /* store acc to RAM */
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opADDimm_A_AA (21) */
      cmp_new = 0x1; cmp_old = acc_a0 = register_A; register_A = flag_C = 0x0001;

      /* opSTAdir_A_A (dd) */
      ram[register_I = 0x7d] = 0x001; /* store acc to RAM */

    case 0x08ed:

      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opADDimmX_A_AA (20) */
      cmp_old = register_A; cmp_new = 0x1e;
      register_A = flag_C = acc_a0 = 0x001e;
      /* opOUTbi_A_A (93) */
      /* opOUTsnd_A (93) */
      set_sound_data(0);
      /* opLSRe_A_AA (eb) */
      cmp_new = 0x0BEB; cmp_old = acc_a0 = register_A; flag_C = (0x0BEB + register_A);
      register_A >>= 1;
      /* opOUTbi_A_A (90) */
      /* opOUTsnd_A (90) */
      set_sound_addr_A(register_A & 1);
      /* opLSRe_A_AA (eb) */
      cmp_new = 0x0BEB; cmp_old = acc_a0 = register_A; flag_C = (0x0BEB + register_A);
      register_A >>= 1;
      /* opOUTbi_A_A (91) */
      /* opOUTsnd_A (91) */
      set_sound_addr_B(register_A & 1);
      /* opLSRe_A_AA (eb) */
      cmp_new = 0x0BEB; cmp_old = acc_a0 = register_A; flag_C = (0x0BEB + register_A);
      register_A >>= 1;
      /* opOUTbi_A_A (92) */
      /* opOUTsnd_A (92) */
      set_sound_addr_C(register_A & 1);
      /* opLSRe_A_AA (eb) */
      cmp_new = 0x0BEB; cmp_old = acc_a0 = register_A; flag_C = (0x0BEB + register_A);
      register_A >>= 1;
      /* opOUTbi_A_A (94) */
      /* opOUTsnd_A (94) */
      if (register_A & 1) strobe_sound_on(); else strobe_sound_off();
      /* opLSRe_A_AA (eb) */
      cmp_new = 0x0BEB; cmp_old = acc_a0 = register_A; flag_C = (0x0BEB + register_A);
      register_A >>= 1;
      /* opOUTbi_A_A (94) */
      /* opOUTsnd_A (94) */
      if (register_A & 1) strobe_sound_on(); else strobe_sound_off();

    case 0x08fb:

      /* Invariants: register_P = 0xdeadbeef register_A = 0x00 */;
      /* opLDAimm_A_AA (06) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0x0600;
      /* opADDimmX_A_AA (20) */
      cmp_old = register_A; cmp_new = 0x00;
      register_A = flag_C = acc_a0 = 0x0600;
      /* opLDPimm_A_A (80) */
      register_P = 0x0; /* set page register */
      /* opSTAdir_A_A (d0) */
      ram[register_I = 0x00] = 0x600; /* store acc to RAM */
      /* opLDJimm_A_A (40) */
      register_J = 0x0080;
      /* opLDPimm_A_A (82) */
      register_P = 0x2; /* set page register */
      /* opJPP8_A_B (50) */
      register_PC = 0x1080; /* Jump to other rom bank */
      break;

    case 0x0904:

      /* Invariants: register_P = 0x0 register_I = 0x00 register_A = 0x01 */;
      /* opNOP_A_B (57) */
      /* opINP_B_AA (11) */
      flag_C = acc_a0 = register_A;
      cmp_old = register_B; /* save old accB */
#ifdef RAWIO
      register_B = cmp_new = (( ioSwitches >> 0x1 ) & 0x01);
#else
      register_B = cmp_new = get_shield_bit2();
#endif
      /* opNOP_A_B (57) */
      /* opLSLe_B_AA (ec) */
      cmp_new = 0x0CEC; acc_a0 = register_A; cmp_old = register_B; flag_C = (0x0CEC + register_B);
      register_B = (register_B << 1) & 0xFFF;
      /* opLDPimm_A_A (88) */
      register_P = 0x8; /* set page register */
      /* opNOP_A_B (57) */
      /* opSTAdir_B_BB (d2) */
      ram[register_I = (register_P << 4) + 0x2] = register_B; /* set I register and store B to ram */
      /* opNOP_A_B (57) */
      /* opINP_B_AA (14) */
      flag_C = acc_a0 = register_A;
      cmp_old = register_B; /* save old accB */
#ifdef RAWIO
      register_B = cmp_new = (( ioSwitches >> 0x4 ) & 0x01);
#else
      register_B = cmp_new = get_shield_bit1();
#endif
      /* opNOP_A_B (57) */
      /* opAWDirg_B_AA (e7) */
      acc_a0 = register_A;
      cmp_old = register_B;
      register_B = (flag_C = (register_B + (cmp_new = ram[register_I]))) & 0xFFF;
      set_watchdog();
      /* opNOP_A_B (57) */
      /* opLSLe_B_AA (ec) */
      cmp_new = 0x0CEC; acc_a0 = register_A; cmp_old = register_B; flag_C = (0x0CEC + register_B);
      register_B = (register_B << 1) & 0xFFF;
      /* opNOP_A_B (57) */
      /* opSTAirg_B_BB (e6) */
      ram[register_I] = register_B; /* store acc */
      /* opNOP_A_B (57) */
      /* opINP_B_AA (15) */
      flag_C = acc_a0 = register_A;
      cmp_old = register_B; /* save old accB */
#ifdef RAWIO
      register_B = cmp_new = (( ioSwitches >> 0x5 ) & 0x01);
#else
      register_B = cmp_new = get_shield_bit0();
#endif
      /* opNOP_A_B (57) */
      /* opAWDirg_B_AA (e7) */
      acc_a0 = register_A;
      cmp_old = register_B;
      register_B = (flag_C = (register_B + (cmp_new = ram[register_I]))) & 0xFFF;
      set_watchdog();
      /* opNOP_A_B (57) */
      /* opSTAirg_B_BB (e6) */
      ram[register_I] = register_B; /* store acc */
      /* opLDJimm_A_A (46) */
      register_J = 0x0026;
      /* opJMP_A_A (58) */
      {
        register_PC = 0x0026;
        break;
      };

    default:
      /* Jumping to any illegal address or end of eprom will come here */
      /* we ought to reinitialise or something */
      ;
  }
}

// ops0000.c

void cineExecute0000(void)
{
  switch (register_PC)
  {
    case 0x0000:

      /* Invariants: register_P = 0x0 register_I = 0x00 register_A = 0x00 */;
      /* opNOP_A_B (57) */
      /* opLDAimm_B_AA (00) */
      flag_C = acc_a0 = register_A;
      cmp_old = register_B; /* step back cmp flag */
      register_B = cmp_new = 0;
      /* opLDAimm_A_AA (0f) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0x0f00;
      /* opADDimmX_A_AA (20) */
      cmp_old = register_A; cmp_new = 0x01;
      register_A = flag_C = acc_a0 = 0x0f01;
      /* opLDJimm_A_A (48) */
      register_J = 0x0008;
    /* opLDPimm_A_A (80) */

    case 0x0008:

      /* Invariants: register_P = 0x0 */;
      /* opSTAdir_A_A (d0) */
      ram[register_I = 0x00] = register_A; /* store acc to RAM */
      /* opLDIdir_A_A (c0) */
      register_I = ram[0x00] & 0xff; /* set new register_I (8 bits) */
      /* opNOP_A_B (57) */
      /* opSTAirg_B_BB (e6) */
      ram[register_I] = register_B; /* store acc */
      /* opADDimm_A_AA (21) */
      register_A = (flag_C = ((cmp_old = acc_a0 = register_A) + (cmp_new = 0x1))) & 0xFFF; /* add values, save carry */

      /* opJNC_A_AA (5d) */
      if (!(flag_C & CARRYBIT)) {
        register_PC = 0x0008;
        break;
      };
      /* opSTAdir_A_A (d0) */
      ram[register_I = 0x00] = register_A; /* store acc to RAM */
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opADDimm_A_AA (21) */
      cmp_new = 0x1; cmp_old = acc_a0 = register_A; register_A = flag_C = 0x0001;

      /* opOUTbi_A_A (95) */
      /* opOUTsnd_A (95) */
      reset_coin_counter(1);
      /* opLDJimm_A_A (44) */
      register_J = 0x0904;
      /* opJMP_A_A (58) */
      {
        register_PC = 0x0904;
        break;
      };

    case 0x0026:

      /* Invariants: register_P = 0x8 register_I = 0x82 register_A = 0x01 */;
      /* opLDAimm_A_AA (09) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0x0900;
      /* opADDimmX_A_AA (20) */
      cmp_old = register_A; cmp_new = 0x1c;
      register_A = flag_C = acc_a0 = 0x091c;
      /* opAWDirg_A_AA (e7) */
      cmp_old = acc_a0 = register_A;
      register_A = (flag_C = (register_A + (cmp_new = ram[0x82]))) & 0xFFF;
      set_watchdog();
      /* opXLT_A_AA (e2) */
      cmp_old = register_A; register_A = cmp_new = rom[register_A]; /* new acc value */
      /* opNOP_A_A (5f) */
      /* opSTAirg_A_A (e6) */
      ram[register_I] = register_A; /* store acc */
      /* opLDAimm_A_AA (02) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0x0200;
      /* opADDimmX_A_AA (20) */
      cmp_old = register_A; cmp_new = 0xd3;
      register_A = flag_C = acc_a0 = 0x02d3;
      /* opLDPimm_A_A (88) */
      register_P = 0x8; /* set page register */
      /* opSTAdir_A_A (d3) */
      ram[register_I = 0x83] = 0x2d3; /* store acc to RAM */
      /* opLDPimm_A_A (82) */
      register_P = 0x2; /* set page register */
      /* opSTAdir_A_A (d2) */
      ram[register_I = 0x22] = 0x2d3; /* store acc to RAM */
      /* opSTAdir_A_A (d3) */
      ram[register_I = 0x23] = 0x2d3; /* store acc to RAM */
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opSTAdir_A_A (dc) */
      ram[register_I = 0x2c] = 0x000; /* store acc to RAM */
      /* opSTAdir_A_A (dd) */
      ram[register_I = 0x2d] = 0x000; /* store acc to RAM */
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opLDPimm_A_A (86) */
      register_P = 0x6; /* set page register */
      /* opSTAdir_A_A (d2) */
      ram[register_I = 0x62] = 0x000; /* store acc to RAM */
      /* opSTAdir_A_A (d0) */
      ram[register_I = 0x60] = 0x000; /* store acc to RAM */
      /* opSTAdir_A_A (da) */
      ram[register_I = 0x6a] = 0x000; /* store acc to RAM */
      /* opSTAdir_A_A (dc) */
      ram[register_I = 0x6c] = 0x000; /* store acc to RAM */
      /* opSTAdir_A_A (d9) */
      ram[register_I = 0x69] = 0x000; /* store acc to RAM */
      /* opADDimm_A_AA (21) */
      cmp_new = 0x1; cmp_old = acc_a0 = register_A; register_A = flag_C = 0x0001;

      /* opSTAdir_A_A (d1) */
      ram[register_I = 0x61] = 0x001; /* store acc to RAM */
      /* opINP_A_AA (17) */
      cmp_old = flag_C = acc_a0 = register_A;
#ifdef RAWIO
      register_A = cmp_new = get_io_bit(0x7);
#else
      register_A = cmp_new = get_io_startbutton();
#endif
      /* opADDimm_A_AA (21) */
      register_A = (flag_C = ((cmp_old = acc_a0 = register_A) + (cmp_new = 0x1))) & 0xFFF; /* add values, save carry */

      /* opLDJimm_A_A (48) */
      register_J = 0x0058;
      /* opJEQ_A_AA (5c) */
      if (cmp_new == cmp_old) {
        register_PC = 0x0058;
        break;
      };
      /* opLDJimm_A_A (40) */
      register_J = 0x0000;
      /* opLDPimm_A_A (82) */
      register_P = 0x2; /* set page register */
      /* opJPP8_A_B (50) */
      register_PC = 0x1000; /* Jump to other rom bank */
      break;

    case 0x004a:

      /* opADDimmX_A_AA (20) */
      register_A = (flag_C = ((acc_a0 = cmp_old = register_A) + (cmp_new = 0x001f))) & 0xFFF; /* add values */
      /* opCMPdir_A_AA (b3) */
      cmp_old = acc_a0 = register_A; /* backup old acc */
      flag_C = (((cmp_new = ram[register_I = (register_P << 4) + 0x3]) ^ 0xFFF) + 1 + register_A);
      /* opLDJimm_A_A (46) */
      register_J = 0x0056;
      /* opJNC_A_AA (5d) */
      if (!(flag_C & CARRYBIT)) {
        register_PC = 0x0056;
        break;
      };
      /* opLDAirg_A_AA (ea) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = ram[register_I];
      /* opADDimm_A_AA (21) */
      register_A = (flag_C = ((cmp_old = acc_a0 = register_A) + (cmp_new = 0x1))) & 0xFFF; /* add values, save carry */

      /* opSTAirg_A_A (e6) */
      ram[register_I] = register_A; /* store acc */
      /* opLDJimm_A_A (48) */
      register_J = 0x0058;
      /* opJMP_A_A (58) */
      {
        register_PC = 0x0058;
        break;
      };

    case 0x0056:

      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opSTAirg_A_A (e6) */
      ram[register_I] = register_A; /* store acc */

    case 0x0058:

      /* opNOP_A_A (5f) */
      /* opNOP_A_A (5f) */
      /* opNOP_A_B (57) */
      /* opLDAimm_B_AA (03) */
      flag_C = acc_a0 = register_A;
      cmp_old = register_B; /* step back cmp flag */
      register_B = cmp_new = 0x0300;
      /* opASRDe_A_AA (ee) */
      cmp_new = 0x0EEE; cmp_old = acc_a0 = register_A; flag_C = (0x0EEE + register_A);
      register_A = (register_A >> 1) | ((register_B & 1) << 11);
      register_B = (register_B >> 1) | (register_B & 0x800);
      /* opLDAimm_A_AA (02) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0x0200;
      /* opLDJimm_A_A (40) */
      register_J = 0x0060;
      /* opJDR_A_A (5a) */
      /* opVIN_A_A (f0) */

      FromX = register_A & 0xFFF; /* regA goes to x-coord */
      FromY = register_B & 0xFFF; /* regB goes to y-coord */

      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opSUBimmX_A_AA (30) */
      cmp_old = acc_a0 = register_A; /* back up regA */
      register_A = (flag_C = (register_A + (((cmp_new = 0x12) ^ 0xFFF) + 1))) & 0xFFF; /* add */

    case 0x0065:

      /* Invariants: register_P = 0xdeadbeef */;
      /* opADDimm_A_AA (21) */
      register_A = (flag_C = ((cmp_old = acc_a0 = register_A) + (cmp_new = 0x1))) & 0xFFF; /* add values, save carry */

      /* opLDJimm_A_A (45) */
      register_J = 0x0065;
      /* opJNC_A_AA (5d) */
      if (!(flag_C & CARRYBIT)) {
        register_PC = 0x0065;
        break;
      };
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opADDimm_A_AA (2b) */
      cmp_new = 0xb; cmp_old = acc_a0 = register_A; register_A = flag_C = 0x000b;

      /* opNOP_A_B (57) */
      /* opLDAimm_B_AA (00) */
      flag_C = acc_a0 = register_A;
      cmp_old = register_B; /* step back cmp flag */
      register_B = cmp_new = 0;
      /* opLLT_A_AA (e4) */
      { CINEBYTE temp_byte = 0;
        for (;;) {
          if (   (((register_A >> 8) & 0x0A) && (((register_A >> 8) & 0x0A) ^ 0x0A))
                 ||   (((register_B >> 8) & 0x0A) && (((register_B >> 8) & 0x0A) ^ 0x0A))  ) break;
          register_A <<= 1; register_B <<= 1;
          if (!(++temp_byte)) break /* This may not be correct */;
        }
        vgShiftLength = temp_byte & 0xfff; register_A &= 0x0FFF; register_B &= 0x0FFF;
      }
      /* opWAI_A_A (e5) */
      /* wait for a tick on the watchdog */
      CinemaClearScreen();
      bNewFrame = 1;
      register_PC = 0x006f;
      break;

    case 0x006f:

      /* Invariants: register_P = 0xdeadbeef register_A = 0x2c0 */;
      /* opWAI_A_A (e5) */
      register_PC = 0x0070;

    case 0x0070:

      /* Invariants: register_P = 0xdeadbeef register_A = 0x2c0 */;
      /* opADDimm_A_AA (21) */
      cmp_new = 0x1; cmp_old = acc_a0 = register_A; register_A = flag_C = 0x02c1;

      /* opAWDirg_A_AA (f7) */
      cmp_old = acc_a0 = register_A;
      register_A = (flag_C = (register_A + (cmp_new = ram[register_I]))) & 0xFFF;
      set_watchdog();
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opLDPimm_A_A (86) */
      register_P = 0x6; /* set page register */
      /* opCMPdir_A_AA (b9) */
      cmp_old = acc_a0 = register_A; /* backup old acc */
      flag_C = (((cmp_new = ram[register_I = 0x69]) ^ 0xFFF) + 1 + register_A);
      /* opLDJimm_A_A (4e) */
      register_J = 0x00be;
      /* opJNC_A_AA (5d) */
      if (!(flag_C & CARRYBIT)) {
        register_PC = 0x00be;
        break;
      };
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opADDimmX_A_AA (20) */
      cmp_old = register_A; cmp_new = 0x1e;
      register_A = flag_C = acc_a0 = 0x001e;
      /* opOUTbi_A_A (93) */
      /* opOUTsnd_A (93) */
      set_sound_data(0);
      /* opLSRe_A_AA (eb) */
      cmp_new = 0x0BEB; cmp_old = acc_a0 = register_A; flag_C = (0x0BEB + register_A);
      register_A >>= 1;
      /* opOUTbi_A_A (90) */
      /* opOUTsnd_A (90) */
      set_sound_addr_A(register_A & 1);
      /* opLSRe_A_AA (eb) */
      cmp_new = 0x0BEB; cmp_old = acc_a0 = register_A; flag_C = (0x0BEB + register_A);
      register_A >>= 1;
      /* opOUTbi_A_A (91) */
      /* opOUTsnd_A (91) */
      set_sound_addr_B(register_A & 1);
      /* opLSRe_A_AA (eb) */
      cmp_new = 0x0BEB; cmp_old = acc_a0 = register_A; flag_C = (0x0BEB + register_A);
      register_A >>= 1;
      /* opOUTbi_A_A (92) */
      /* opOUTsnd_A (92) */
      set_sound_addr_C(register_A & 1);
      /* opLSRe_A_AA (eb) */
      cmp_new = 0x0BEB; cmp_old = acc_a0 = register_A; flag_C = (0x0BEB + register_A);
      register_A >>= 1;
      /* opOUTbi_A_A (94) */
      /* opOUTsnd_A (94) */
      if (register_A & 1) strobe_sound_on(); else strobe_sound_off();
      /* opLSRe_A_AA (eb) */
      cmp_new = 0x0BEB; cmp_old = acc_a0 = register_A; flag_C = (0x0BEB + register_A);
      register_A >>= 1;
      /* opOUTbi_A_A (94) */
      /* opOUTsnd_A (94) */
      if (register_A & 1) strobe_sound_on(); else strobe_sound_off();
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opADDimmX_A_AA (20) */
      cmp_old = register_A; cmp_new = 0x1a;
      register_A = flag_C = acc_a0 = 0x001a;
      /* opOUTbi_A_A (93) */
      /* opOUTsnd_A (93) */
      set_sound_data(0);
      /* opLSRe_A_AA (eb) */
      cmp_new = 0x0BEB; cmp_old = acc_a0 = register_A; flag_C = (0x0BEB + register_A);
      register_A >>= 1;
      /* opOUTbi_A_A (90) */
      /* opOUTsnd_A (90) */
      set_sound_addr_A(register_A & 1);
      /* opLSRe_A_AA (eb) */
      cmp_new = 0x0BEB; cmp_old = acc_a0 = register_A; flag_C = (0x0BEB + register_A);
      register_A >>= 1;
      /* opOUTbi_A_A (91) */
      /* opOUTsnd_A (91) */
      set_sound_addr_B(register_A & 1);
      /* opLSRe_A_AA (eb) */
      cmp_new = 0x0BEB; cmp_old = acc_a0 = register_A; flag_C = (0x0BEB + register_A);
      register_A >>= 1;
      /* opOUTbi_A_A (92) */
      /* opOUTsnd_A (92) */
      set_sound_addr_C(register_A & 1);
      /* opLSRe_A_AA (eb) */
      cmp_new = 0x0BEB; cmp_old = acc_a0 = register_A; flag_C = (0x0BEB + register_A);
      register_A >>= 1;
      /* opOUTbi_A_A (94) */
      /* opOUTsnd_A (94) */
      if (register_A & 1) strobe_sound_on(); else strobe_sound_off();
      /* opLSRe_A_AA (eb) */
      cmp_new = 0x0BEB; cmp_old = acc_a0 = register_A; flag_C = (0x0BEB + register_A);
      register_A >>= 1;
      /* opOUTbi_A_A (94) */
      /* opOUTsnd_A (94) */
      if (register_A & 1) strobe_sound_on(); else strobe_sound_off();
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opADDimmX_A_AA (20) */
      cmp_old = register_A; cmp_new = 0x18;
      register_A = flag_C = acc_a0 = 0x0018;
      /* opOUTbi_A_A (93) */
      /* opOUTsnd_A (93) */
      set_sound_data(0);
      /* opLSRe_A_AA (eb) */
      cmp_new = 0x0BEB; cmp_old = acc_a0 = register_A; flag_C = (0x0BEB + register_A);
      register_A >>= 1;
      /* opOUTbi_A_A (90) */
      /* opOUTsnd_A (90) */
      set_sound_addr_A(register_A & 1);
      /* opLSRe_A_AA (eb) */
      cmp_new = 0x0BEB; cmp_old = acc_a0 = register_A; flag_C = (0x0BEB + register_A);
      register_A >>= 1;
      /* opOUTbi_A_A (91) */
      /* opOUTsnd_A (91) */
      set_sound_addr_B(register_A & 1);
      /* opLSRe_A_AA (eb) */
      cmp_new = 0x0BEB; cmp_old = acc_a0 = register_A; flag_C = (0x0BEB + register_A);
      register_A >>= 1;
      /* opOUTbi_A_A (92) */
      /* opOUTsnd_A (92) */
      set_sound_addr_C(register_A & 1);
      /* opLSRe_A_AA (eb) */
      cmp_new = 0x0BEB; cmp_old = acc_a0 = register_A; flag_C = (0x0BEB + register_A);
      register_A >>= 1;
      /* opOUTbi_A_A (94) */
      /* opOUTsnd_A (94) */
      if (register_A & 1) strobe_sound_on(); else strobe_sound_off();
      /* opLSRe_A_AA (eb) */
      cmp_new = 0x0BEB; cmp_old = acc_a0 = register_A; flag_C = (0x0BEB + register_A);
      register_A >>= 1;
      /* opOUTbi_A_A (94) */
      /* opOUTsnd_A (94) */
      if (register_A & 1) strobe_sound_on(); else strobe_sound_off();
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opADDimmX_A_AA (20) */
      cmp_old = register_A; cmp_new = 0x16;
      register_A = flag_C = acc_a0 = 0x0016;
      /* opOUTbi_A_A (93) */
      /* opOUTsnd_A (93) */
      set_sound_data(0);
      /* opLSRe_A_AA (eb) */
      cmp_new = 0x0BEB; cmp_old = acc_a0 = register_A; flag_C = (0x0BEB + register_A);
      register_A >>= 1;
      /* opOUTbi_A_A (90) */
      /* opOUTsnd_A (90) */
      set_sound_addr_A(register_A & 1);
      /* opLSRe_A_AA (eb) */
      cmp_new = 0x0BEB; cmp_old = acc_a0 = register_A; flag_C = (0x0BEB + register_A);
      register_A >>= 1;
      /* opOUTbi_A_A (91) */
      /* opOUTsnd_A (91) */
      set_sound_addr_B(register_A & 1);
      /* opLSRe_A_AA (eb) */
      cmp_new = 0x0BEB; cmp_old = acc_a0 = register_A; flag_C = (0x0BEB + register_A);
      register_A >>= 1;
      /* opOUTbi_A_A (92) */
      /* opOUTsnd_A (92) */
      set_sound_addr_C(register_A & 1);
      /* opLSRe_A_AA (eb) */
      cmp_new = 0x0BEB; cmp_old = acc_a0 = register_A; flag_C = (0x0BEB + register_A);
      register_A >>= 1;
      /* opOUTbi_A_A (94) */
      /* opOUTsnd_A (94) */
      if (register_A & 1) strobe_sound_on(); else strobe_sound_off();
      /* opLSRe_A_AA (eb) */
      cmp_new = 0x0BEB; cmp_old = acc_a0 = register_A; flag_C = (0x0BEB + register_A);
      register_A >>= 1;
      /* opOUTbi_A_A (94) */
      /* opOUTsnd_A (94) */
      if (register_A & 1) strobe_sound_on(); else strobe_sound_off();
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opADDimmX_A_AA (20) */
      cmp_old = register_A; cmp_new = 0x14;
      register_A = flag_C = acc_a0 = 0x0014;
      /* opOUTbi_A_A (93) */
      /* opOUTsnd_A (93) */
      set_sound_data(0);
      /* opLSRe_A_AA (eb) */
      cmp_new = 0x0BEB; cmp_old = acc_a0 = register_A; flag_C = (0x0BEB + register_A);
      register_A >>= 1;
      /* opOUTbi_A_A (90) */
      /* opOUTsnd_A (90) */
      set_sound_addr_A(register_A & 1);
      /* opLSRe_A_AA (eb) */
      cmp_new = 0x0BEB; cmp_old = acc_a0 = register_A; flag_C = (0x0BEB + register_A);
      register_A >>= 1;
      /* opOUTbi_A_A (91) */
      /* opOUTsnd_A (91) */
      set_sound_addr_B(register_A & 1);
      /* opLSRe_A_AA (eb) */
      cmp_new = 0x0BEB; cmp_old = acc_a0 = register_A; flag_C = (0x0BEB + register_A);
      register_A >>= 1;
      /* opOUTbi_A_A (92) */
      /* opOUTsnd_A (92) */
      set_sound_addr_C(register_A & 1);
      /* opLSRe_A_AA (eb) */
      cmp_new = 0x0BEB; cmp_old = acc_a0 = register_A; flag_C = (0x0BEB + register_A);
      register_A >>= 1;
      /* opOUTbi_A_A (94) */
      /* opOUTsnd_A (94) */
      if (register_A & 1) strobe_sound_on(); else strobe_sound_off();
      /* opLSRe_A_AA (eb) */
      cmp_new = 0x0BEB; cmp_old = acc_a0 = register_A; flag_C = (0x0BEB + register_A);
      register_A >>= 1;
      /* opOUTbi_A_A (94) */
      /* opOUTsnd_A (94) */
      if (register_A & 1) strobe_sound_on(); else strobe_sound_off();

    case 0x00be:

      /* Invariants: register_P = 0x6 register_I = 0x69 register_A = 0x00 */;
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opADDimmX_A_AA (20) */
      cmp_old = register_A; cmp_new = 0x17;
      register_A = flag_C = acc_a0 = 0x0017;
      /* opLDPimm_A_A (83) */
      register_P = 0x3; /* set page register */
      /* opCMPdir_A_AA (b7) */
      cmp_old = acc_a0 = register_A; /* backup old acc */
      flag_C = (((cmp_new = ram[register_I = 0x37]) ^ 0xFFF) + 1 + register_A);
      /* opLDJimm_A_A (4f) */
      register_J = 0x00df;
      /* opJNC_A_AA (5d) */
      if (!(flag_C & CARRYBIT)) {
        register_PC = 0x00df;
        break;
      };
      /* opLDAimm_A_AA (01) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0x0100;
      /* opADDimmX_A_AA (20) */
      cmp_old = register_A; cmp_new = 0x15;
      register_A = flag_C = acc_a0 = 0x0115;
      /* opCMPdir_A_AA (b3) */
      cmp_old = acc_a0 = register_A; /* backup old acc */
      flag_C = (((cmp_new = ram[register_I = 0x33]) ^ 0xFFF) + 1 + register_A);
      /* opLDJimm_A_A (42) */
      register_J = 0x00e2;
      /* opJNC_A_AA (5d) */
      if (!(flag_C & CARRYBIT)) {
        register_PC = 0x00e2;
        break;
      };
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opCMPdir_A_AA (b0) */
      cmp_old = acc_a0 = register_A; /* backup old acc */
      flag_C = (((cmp_new = ram[register_I = 0x30]) ^ 0xFFF) + 1 + register_A);
      /* opJEQ_A_AA (5c) */
      if (cmp_new == cmp_old) {
        register_PC = 0x00e2;
        break;
      };
      /* opLDPimm_A_A (86) */
      register_P = 0x6; /* set page register */
      /* opLDAdir_A_AA (a9) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = 0x69]; /* set I register */

      /* opLDJimm_A_A (4f) */
      register_J = 0x00df;
      /* opJEQ_A_AA (5c) */
      if (cmp_new == cmp_old) {
        register_PC = 0x00df;
        break;
      };
      /* opSUBimm_A_AA (31) */
      cmp_old = acc_a0 = register_A;
      register_A = (flag_C = (register_A + (((cmp_new = 0x1) ^ 0xFFF) + 1))) & 0xFFF; /* 1's-comp add */
      /* opSTAirg_A_A (e6) */
      ram[0x69] = register_A; /* store acc */
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opADDimm_A_AA (2f) */
      cmp_new = 0xf; cmp_old = acc_a0 = register_A; register_A = flag_C = 0x000f;

      /* opLDPimm_A_A (87) */
      register_P = 0x7; /* set page register */
      /* opSTAdir_A_A (d2) */
      ram[register_I = 0x72] = 0x00f; /* store acc to RAM */
      /* opLDPimm_A_A (87) */
      /* opLDAdir_A_AA (a3) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = 0x73]; /* set I register */

      /* opADDimm_A_AA (21) */
      register_A = (flag_C = ((cmp_old = acc_a0 = register_A) + (cmp_new = 0x1))) & 0xFFF; /* add values, save carry */

      /* opSTAirg_A_A (e6) */
      ram[0x73] = register_A; /* store acc */

    case 0x00df:

      /* Invariants: register_P = 0xdeadbeef register_I = 0x37 register_A = 0x17 */;
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opLDPimm_A_A (83) */
      register_P = 0x3; /* set page register */
      /* opSTAdir_A_A (d0) */
      ram[register_I = 0x30] = 0x000; /* store acc to RAM */

    case 0x00e2:

      /* Invariants: register_P = 0x3 */;
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opADDimmX_A_AA (20) */
      cmp_old = register_A; cmp_new = 0x17;
      register_A = flag_C = acc_a0 = 0x0017;
      /* opLDPimm_A_A (84) */
      register_P = 0x4; /* set page register */
      /* opCMPdir_A_AA (b7) */
      cmp_old = acc_a0 = register_A; /* backup old acc */
      flag_C = (((cmp_new = ram[register_I = 0x47]) ^ 0xFFF) + 1 + register_A);
      /* opLDJimm_A_A (43) */
      register_J = 0x0103;
      /* opJNC_A_AA (5d) */
      if (!(flag_C & CARRYBIT)) {
        register_PC = 0x0103;
        break;
      };
      /* opLDAimm_A_AA (01) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0x0100;
      /* opADDimmX_A_AA (20) */
      cmp_old = register_A; cmp_new = 0x15;
      register_A = flag_C = acc_a0 = 0x0115;
      /* opCMPdir_A_AA (b3) */
      cmp_old = acc_a0 = register_A; /* backup old acc */
      flag_C = (((cmp_new = ram[register_I = 0x43]) ^ 0xFFF) + 1 + register_A);
      /* opLDJimm_A_A (46) */
      register_J = 0x0106;
      /* opJNC_A_AA (5d) */
      if (!(flag_C & CARRYBIT)) {
        register_PC = 0x0106;
        break;
      };
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opCMPdir_A_AA (b0) */
      cmp_old = acc_a0 = register_A; /* backup old acc */
      flag_C = (((cmp_new = ram[register_I = 0x40]) ^ 0xFFF) + 1 + register_A);
      /* opJEQ_A_AA (5c) */
      if (cmp_new == cmp_old) {
        register_PC = 0x0106;
        break;
      };
      /* opLDPimm_A_A (86) */
      register_P = 0x6; /* set page register */
      /* opLDAdir_A_AA (a9) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = 0x69]; /* set I register */

      /* opLDJimm_A_A (43) */
      register_J = 0x0103;
      /* opJEQ_A_AA (5c) */
      if (cmp_new == cmp_old) {
        register_PC = 0x0103;
        break;
      };
      /* opSUBimm_A_AA (31) */
      cmp_old = acc_a0 = register_A;
      register_A = (flag_C = (register_A + (((cmp_new = 0x1) ^ 0xFFF) + 1))) & 0xFFF; /* 1's-comp add */
      /* opSTAirg_A_A (e6) */
      ram[0x69] = register_A; /* store acc */
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opADDimm_A_AA (2f) */
      cmp_new = 0xf; cmp_old = acc_a0 = register_A; register_A = flag_C = 0x000f;

      /* opLDPimm_A_A (87) */
      register_P = 0x7; /* set page register */
      /* opSTAdir_A_A (d2) */
      ram[register_I = 0x72] = 0x00f; /* store acc to RAM */
      /* opLDPimm_A_A (87) */
      /* opLDAdir_A_AA (a3) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = 0x73]; /* set I register */

      /* opADDimm_A_AA (21) */
      register_A = (flag_C = ((cmp_old = acc_a0 = register_A) + (cmp_new = 0x1))) & 0xFFF; /* add values, save carry */

      /* opSTAirg_A_A (e6) */
      ram[0x73] = register_A; /* store acc */

    case 0x0103:

      /* Invariants: register_P = 0xdeadbeef register_I = 0x47 register_A = 0x17 */;
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opLDPimm_A_A (84) */
      register_P = 0x4; /* set page register */
      /* opSTAdir_A_A (d0) */
      ram[register_I = 0x40] = 0x000; /* store acc to RAM */

    case 0x0106:

      /* Invariants: register_P = 0xdeadbeef */;
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opADDimmX_A_AA (20) */
      cmp_old = register_A; cmp_new = 0x17;
      register_A = flag_C = acc_a0 = 0x0017;
      /* opLDPimm_A_A (85) */
      register_P = 0x5; /* set page register */
      /* opCMPdir_A_AA (b7) */
      cmp_old = acc_a0 = register_A; /* backup old acc */
      flag_C = (((cmp_new = ram[register_I = 0x57]) ^ 0xFFF) + 1 + register_A);
      /* opLDJimm_A_A (47) */
      register_J = 0x0127;
      /* opJNC_A_AA (5d) */
      if (!(flag_C & CARRYBIT)) {
        register_PC = 0x0127;
        break;
      };
      /* opLDAimm_A_AA (01) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0x0100;
      /* opADDimmX_A_AA (20) */
      cmp_old = register_A; cmp_new = 0x15;
      register_A = flag_C = acc_a0 = 0x0115;
      /* opCMPdir_A_AA (b3) */
      cmp_old = acc_a0 = register_A; /* backup old acc */
      flag_C = (((cmp_new = ram[register_I = 0x53]) ^ 0xFFF) + 1 + register_A);
      /* opLDJimm_A_A (4a) */
      register_J = 0x012a;
      /* opJNC_A_AA (5d) */
      if (!(flag_C & CARRYBIT)) {
        register_PC = 0x012a;
        break;
      };
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opCMPdir_A_AA (b0) */
      cmp_old = acc_a0 = register_A; /* backup old acc */
      flag_C = (((cmp_new = ram[register_I = 0x50]) ^ 0xFFF) + 1 + register_A);
      /* opJEQ_A_AA (5c) */
      if (cmp_new == cmp_old) {
        register_PC = 0x012a;
        break;
      };
      /* opLDPimm_A_A (86) */
      register_P = 0x6; /* set page register */
      /* opLDAdir_A_AA (a9) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = 0x69]; /* set I register */

      /* opLDJimm_A_A (47) */
      register_J = 0x0127;
      /* opJEQ_A_AA (5c) */
      if (cmp_new == cmp_old) {
        register_PC = 0x0127;
        break;
      };
      /* opSUBimm_A_AA (31) */
      cmp_old = acc_a0 = register_A;
      register_A = (flag_C = (register_A + (((cmp_new = 0x1) ^ 0xFFF) + 1))) & 0xFFF; /* 1's-comp add */
      /* opSTAirg_A_A (e6) */
      ram[0x69] = register_A; /* store acc */
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opADDimm_A_AA (2f) */
      cmp_new = 0xf; cmp_old = acc_a0 = register_A; register_A = flag_C = 0x000f;

      /* opLDPimm_A_A (87) */
      register_P = 0x7; /* set page register */
      /* opSTAdir_A_A (d2) */
      ram[register_I = 0x72] = 0x00f; /* store acc to RAM */
      /* opLDPimm_A_A (87) */
      /* opLDAdir_A_AA (a3) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = 0x73]; /* set I register */

      /* opADDimm_A_AA (21) */
      register_A = (flag_C = ((cmp_old = acc_a0 = register_A) + (cmp_new = 0x1))) & 0xFFF; /* add values, save carry */

      /* opSTAirg_A_A (e6) */
      ram[0x73] = register_A; /* store acc */

    case 0x0127:

      /* Invariants: register_P = 0xdeadbeef register_I = 0x57 register_A = 0x17 */;
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opLDPimm_A_A (85) */
      register_P = 0x5; /* set page register */
      /* opSTAdir_A_A (d0) */
      ram[register_I = 0x50] = 0x000; /* store acc to RAM */

    case 0x012a:

      /* Invariants: register_P = 0x5 */;
      /* opLDPimm_A_A (87) */
      register_P = 0x7; /* set page register */
      /* opLDAdir_A_AA (a3) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = 0x73]; /* set I register */

      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opLDJimm_A_A (4e) */
      register_J = 0x013e;
      /* opJLT_A_A (5b) */
      if (cmp_new < cmp_old) {
        register_PC = 0x013e;
        break;
      };
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opADDimmX_A_AA (20) */
      cmp_old = register_A; cmp_new = 0x14;
      register_A = flag_C = acc_a0 = 0x0014;
      /* opOUTbi_A_A (93) */
      /* opOUTsnd_A (93) */
      set_sound_data(0);
      /* opLSRe_A_AA (eb) */
      cmp_new = 0x0BEB; cmp_old = acc_a0 = register_A; flag_C = (0x0BEB + register_A);
      register_A >>= 1;
      /* opOUTbi_A_A (90) */
      /* opOUTsnd_A (90) */
      set_sound_addr_A(register_A & 1);
      /* opLSRe_A_AA (eb) */
      cmp_new = 0x0BEB; cmp_old = acc_a0 = register_A; flag_C = (0x0BEB + register_A);
      register_A >>= 1;
      /* opOUTbi_A_A (91) */
      /* opOUTsnd_A (91) */
      set_sound_addr_B(register_A & 1);
      /* opLSRe_A_AA (eb) */
      cmp_new = 0x0BEB; cmp_old = acc_a0 = register_A; flag_C = (0x0BEB + register_A);
      register_A >>= 1;
      /* opOUTbi_A_A (92) */
      /* opOUTsnd_A (92) */
      set_sound_addr_C(register_A & 1);
      /* opLSRe_A_AA (eb) */
      cmp_new = 0x0BEB; cmp_old = acc_a0 = register_A; flag_C = (0x0BEB + register_A);
      register_A >>= 1;
      /* opOUTbi_A_A (94) */
      /* opOUTsnd_A (94) */
      if (register_A & 1) strobe_sound_on(); else strobe_sound_off();
      /* opLSRe_A_AA (eb) */
      cmp_new = 0x0BEB; cmp_old = acc_a0 = register_A; flag_C = (0x0BEB + register_A);
      register_A >>= 1;
      /* opOUTbi_A_A (94) */
      /* opOUTsnd_A (94) */
      if (register_A & 1) strobe_sound_on(); else strobe_sound_off();

    case 0x013e:

      /* Invariants: register_P = 0x7 register_I = 0x73 register_A = 0x00 */;
      /* opLDJimm_A_A (40) */
      register_J = 0x0140;
      /* opJDR_A_A (5a) */
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opOUTbi_A_A (96) */
      vgColour = 0x07;
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opADDimmX_A_AA (20) */
      cmp_old = register_A; cmp_new = 0xa0;
      register_A = flag_C = acc_a0 = 0x00a0;
      /* opLDPimm_A_A (8c) */
      register_P = 0xc; /* set page register */
      /* opSTAdir_A_A (d1) */
      ram[register_I = 0xc1] = 0x0a0; /* store acc to RAM */

    case 0x0148:

      /* Invariants: register_P = 0xc register_I = 0xc1 */;
      /* opLDAdir_A_AA (a1) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = 0xc1]; /* set I register */

      /* opADDimm_A_AA (21) */
      register_A = (flag_C = ((cmp_old = acc_a0 = register_A) + (cmp_new = 0x1))) & 0xFFF; /* add values, save carry */

      /* opSTAdir_A_A (d2) */
      ram[register_I = 0xc2] = register_A; /* store acc to RAM */

    case 0x014b:

      /* Invariants: register_P = 0xc */;
      /* opLDAimm_A_AA (04) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0x0400;
      /* opADDimm_A_AA (28) */
      cmp_new = 0x8; cmp_old = acc_a0 = register_A; register_A = flag_C = 0x0408;

      /* opLDIdir_A_A (c1) */
      register_I = ram[0xc1] & 0xff; /* set new register_I (8 bits) */
      /* opSUBirg_A_AA (e8) */
      register_A = (flag_C = ((cmp_old = acc_a0 = register_A) + ((cmp_new = ram[register_I]) ^ 0xFFF) + 1)) & 0xFFF; /* ones compliment */
      /* opASRe_A_AA (ed) */
      cmp_new = 0xDED; cmp_old = flag_C = acc_a0 = register_A;
      register_A = SEX(register_A); /* make signed */
      register_A = (((signed short int)(((signed short int)register_A) >> (signed short int)1)) /* SAR */) & 0xFFF;
      /* opASRe_A_AA (ed) */
      cmp_new = 0xDED; cmp_old = flag_C = acc_a0 = register_A;
      register_A = SEX(register_A); /* make signed */
      register_A = (((signed short int)(((signed short int)register_A) >> (signed short int)1)) /* SAR */) & 0xFFF;
      /* opASRe_A_AA (ed) */
      cmp_new = 0xDED; cmp_old = flag_C = acc_a0 = register_A;
      register_A = SEX(register_A); /* make signed */
      register_A = (((signed short int)(((signed short int)register_A) >> (signed short int)1)) /* SAR */) & 0xFFF;
      /* opASRe_A_AA (ed) */
      cmp_new = 0xDED; cmp_old = flag_C = acc_a0 = register_A;
      register_A = SEX(register_A); /* make signed */
      register_A = (((signed short int)(((signed short int)register_A) >> (signed short int)1)) /* SAR */) & 0xFFF;
      /* opASRe_A_AA (ed) */
      cmp_new = 0xDED; cmp_old = flag_C = acc_a0 = register_A;
      register_A = SEX(register_A); /* make signed */
      register_A = (((signed short int)(((signed short int)register_A) >> (signed short int)1)) /* SAR */) & 0xFFF;
      /* opSTAdir_A_A (d5) */
      ram[register_I = 0xc5] = register_A; /* store acc to RAM */
      /* opNOP_A_B (57) */
      /* opLDAimm_B_AA (03) */
      flag_C = acc_a0 = register_A;
      cmp_old = register_B; /* step back cmp flag */
      register_B = cmp_new = 0x0300;
      /* opASRDe_A_AA (ee) */
      cmp_new = 0x0EEE; cmp_old = acc_a0 = register_A; flag_C = (0x0EEE + register_A);
      register_A = (register_A >> 1) | ((register_B & 1) << 11);
      register_B = (register_B >> 1) | (register_B & 0x800);
      /* opLDAimm_A_AA (02) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0x0200;
      /* opLDJimm_A_A (4b) */
      register_J = 0x015b;
      /* opJDR_A_A (5a) */
      /* opVIN_A_A (f0) */

      FromX = register_A & 0xFFF; /* regA goes to x-coord */
      FromY = register_B & 0xFFF; /* regB goes to y-coord */

      /* opLDAimm_A_AA (08) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0x0800;
      /* opLDAdir_A_AA (a5) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = 0xc5]; /* set I register */

      /* opLDJimm_A_A (44) */
      register_J = 0x0164;
      /* opJLT_A_A (5b) */
      if (cmp_new < cmp_old) {
        register_PC = 0x0164;
        break;
      };
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opSUBirg_A_AA (e8) */
      register_A = (flag_C = ((cmp_old = acc_a0 = register_A) + ((cmp_new = ram[register_I]) ^ 0xFFF) + 1)) & 0xFFF; /* ones compliment */

    case 0x0164:

      /* Invariants: register_P = 0xc register_I = 0xc5 */;
      /* opSTAdir_A_A (d8) */
      ram[register_I = 0xc8] = register_A; /* store acc to RAM */
      /* opLSRe_A_AA (eb) */
      cmp_new = 0x0BEB; cmp_old = acc_a0 = register_A; flag_C = (0x0BEB + register_A);
      register_A >>= 1;
      /* opLSRe_A_AA (eb) */
      cmp_new = 0x0BEB; cmp_old = acc_a0 = register_A; flag_C = (0x0BEB + register_A);
      register_A >>= 1;
      /* opLDPimm_A_A (87) */
      register_P = 0x7; /* set page register */
      /* opSTAdir_A_A (d0) */
      ram[register_I = 0x70] = register_A; /* store acc to RAM */
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opADDimm_A_AA (24) */
      cmp_new = 0x4; cmp_old = acc_a0 = register_A; register_A = flag_C = 0x0004;

      /* opLDPimm_A_A (87) */
      /* opCMPdir_A_AA (b3) */
      cmp_old = acc_a0 = register_A; /* backup old acc */
      flag_C = (((cmp_new = ram[register_I = 0x73]) ^ 0xFFF) + 1 + register_A);
      /* opLDJimm_A_A (4b) */
      register_J = 0x018b;
      /* opJLT_A_A (5b) */
      if (cmp_new < cmp_old) {
        register_PC = 0x018b;
        break;
      };
      /* opNOP_A_B (57) */
      /* opLDAimm_B_AA (00) */
      flag_C = acc_a0 = register_A;
      cmp_old = register_B; /* step back cmp flag */
      register_B = cmp_new = 0;
      /* opNOP_A_B (57) */
      /* opSUBirg_B_AA (e8) */
      acc_a0 = register_A;
      register_B = (flag_C = ((cmp_old = register_B) + ((cmp_new = ram[register_I]) ^ 0xFFF) + 1)) & 0xFFF; /* ones compliment */
      /* opASRDe_A_AA (ee) */
      cmp_new = 0x0EEE; cmp_old = acc_a0 = register_A; flag_C = (0x0EEE + register_A);
      register_A = (register_A >> 1) | ((register_B & 1) << 11);
      register_B = (register_B >> 1) | (register_B & 0x800);
      /* opNOP_A_B (57) */
      /* opSUBimm_B_AA (31) */
      acc_a0 = register_A;
      register_B = (flag_C = ((cmp_old = register_B) + ((cmp_new = 0x1) ^ 0xFFF) + 1)) & 0xFFF; /* 1's-comp add */
      /* opLDPimm_A_A (8c) */
      register_P = 0xc; /* set page register */
      /* opLDAdir_A_AA (a5) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = 0xc5]; /* set I register */

      /* opADDimm_A_AA (22) */
      register_A = (flag_C = ((cmp_old = acc_a0 = register_A) + (cmp_new = 0x2))) & 0xFFF; /* add values, save carry */

      /* opASRe_A_AA (ed) */
      cmp_new = 0xDED; cmp_old = flag_C = acc_a0 = register_A;
      register_A = SEX(register_A); /* make signed */
      register_A = (((signed short int)(((signed short int)register_A) >> (signed short int)1)) /* SAR */) & 0xFFF;
      /* opASRe_A_AA (ed) */
      cmp_new = 0xDED; cmp_old = flag_C = acc_a0 = register_A;
      register_A = SEX(register_A); /* make signed */
      register_A = (((signed short int)(((signed short int)register_A) >> (signed short int)1)) /* SAR */) & 0xFFF;
      /* opLDPimm_A_A (87) */
      register_P = 0x7; /* set page register */
      /* opSTAdir_A_A (d4) */
      ram[register_I = 0x74] = register_A; /* store acc to RAM */
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opLDJimm_A_A (41) */
      register_J = 0x0181;

    case 0x0181:

      /* opAWDirg_A_AA (e7) */
      cmp_old = acc_a0 = register_A;
      register_A = (flag_C = (register_A + (cmp_new = ram[register_I]))) & 0xFFF;
      set_watchdog();
      /* opNOP_A_B (57) */
      /* opADDimm_B_AA (21) */
      acc_a0 = register_A; /* save old accA bit0 */
      cmp_old = register_B; /* store old acc for later */
      register_B = (flag_C = (register_B + (cmp_new = 0x01))) & 0xFFF; /* add values */
      /* opJNC_A_AA (5d) */
      if (!(flag_C & CARRYBIT)) {
        register_PC = 0x0181;
        break;
      };
      /* opLDPimm_A_A (8c) */
      register_P = 0xc; /* set page register */
      /* opADDdir_A_AA (65) */
      register_A = (flag_C = ((cmp_old = acc_a0 = register_A) + (cmp_new = ram[register_I = 0xc5]))) & 0xFFF; /* do acc operation */
      /* opSTAirg_A_A (e6) */
      ram[0xc5] = register_A; /* store acc */
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opADDimm_A_AA (21) */
      cmp_new = 0x1; cmp_old = acc_a0 = register_A; register_A = flag_C = 0x0001;

      /* opOUTbi_A_A (96) */
      vgColour = 0x0f;

    case 0x018b:

      /* Invariants: register_P = 0x7 register_I = 0x73 register_A = 0x04 */;
      /* opLDPimm_A_A (8c) */
      register_P = 0xc; /* set page register */
      /* opLDAdir_A_AA (a5) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = 0xc5]; /* set I register */

      /* opLDIdir_A_A (c1) */
      register_I = ram[0xc1] & 0xff; /* set new register_I (8 bits) */
      /* opAWDirg_A_AA (e7) */
      cmp_old = acc_a0 = register_A;
      register_A = (flag_C = (register_A + (cmp_new = ram[register_I]))) & 0xFFF;
      set_watchdog();
      /* opSTAirg_A_A (e6) */
      ram[register_I] = register_A; /* store acc */
      /* opLDAimm_A_AA (03) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0x0300;
      /* opADDimm_A_AA (28) */
      cmp_new = 0x8; cmp_old = acc_a0 = register_A; register_A = flag_C = 0x0308;

      /* opLDIdir_A_A (c2) */
      register_I = ram[0xc2] & 0xff; /* set new register_I (8 bits) */
      /* opSUBirg_A_AA (e8) */
      register_A = (flag_C = ((cmp_old = acc_a0 = register_A) + ((cmp_new = ram[register_I]) ^ 0xFFF) + 1)) & 0xFFF; /* ones compliment */
      /* opASRe_A_AA (ed) */
      cmp_new = 0xDED; cmp_old = flag_C = acc_a0 = register_A;
      register_A = SEX(register_A); /* make signed */
      register_A = (((signed short int)(((signed short int)register_A) >> (signed short int)1)) /* SAR */) & 0xFFF;
      /* opASRe_A_AA (ed) */
      cmp_new = 0xDED; cmp_old = flag_C = acc_a0 = register_A;
      register_A = SEX(register_A); /* make signed */
      register_A = (((signed short int)(((signed short int)register_A) >> (signed short int)1)) /* SAR */) & 0xFFF;
      /* opASRe_A_AA (ed) */
      cmp_new = 0xDED; cmp_old = flag_C = acc_a0 = register_A;
      register_A = SEX(register_A); /* make signed */
      register_A = (((signed short int)(((signed short int)register_A) >> (signed short int)1)) /* SAR */) & 0xFFF;
      /* opASRe_A_AA (ed) */
      cmp_new = 0xDED; cmp_old = flag_C = acc_a0 = register_A;
      register_A = SEX(register_A); /* make signed */
      register_A = (((signed short int)(((signed short int)register_A) >> (signed short int)1)) /* SAR */) & 0xFFF;
      /* opASRe_A_AA (ed) */
      cmp_new = 0xDED; cmp_old = flag_C = acc_a0 = register_A;
      register_A = SEX(register_A); /* make signed */
      register_A = (((signed short int)(((signed short int)register_A) >> (signed short int)1)) /* SAR */) & 0xFFF;
      /* opSTAdir_A_A (d6) */
      ram[register_I = 0xc6] = register_A; /* store acc to RAM */
      /* opLDAimm_A_AA (08) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0x0800;
      /* opLDAirg_A_AA (ea) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = ram[register_I];
      /* opLDJimm_A_A (41) */
      register_J = 0x01a1;
      /* opJLT_A_A (5b) */
      if (cmp_new < cmp_old) {
        register_PC = 0x01a1;
        break;
      };
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opSUBirg_A_AA (e8) */
      register_A = (flag_C = ((cmp_old = acc_a0 = register_A) + ((cmp_new = ram[register_I]) ^ 0xFFF) + 1)) & 0xFFF; /* ones compliment */

    case 0x01a1:

      /* Invariants: register_P = 0xc register_I = 0xc6 */;
      /* opSTAdir_A_A (d9) */
      ram[register_I = 0xc9] = register_A; /* store acc to RAM */
      /* opLSRe_A_AA (eb) */
      cmp_new = 0x0BEB; cmp_old = acc_a0 = register_A; flag_C = (0x0BEB + register_A);
      register_A >>= 1;
      /* opLSRe_A_AA (eb) */
      cmp_new = 0x0BEB; cmp_old = acc_a0 = register_A; flag_C = (0x0BEB + register_A);
      register_A >>= 1;
      /* opLDPimm_A_A (87) */
      register_P = 0x7; /* set page register */
      /* opADDdir_A_AA (60) */
      register_A = (flag_C = ((cmp_old = acc_a0 = register_A) + (cmp_new = ram[register_I = 0x70]))) & 0xFFF; /* do acc operation */
      /* opSTAirg_A_A (e6) */
      ram[0x70] = register_A; /* store acc */
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opADDimm_A_AA (24) */
      cmp_new = 0x4; cmp_old = acc_a0 = register_A; register_A = flag_C = 0x0004;

      /* opLDPimm_A_A (87) */
      /* opCMPdir_A_AA (b3) */
      cmp_old = acc_a0 = register_A; /* backup old acc */
      flag_C = (((cmp_new = ram[register_I = 0x73]) ^ 0xFFF) + 1 + register_A);
      /* opLDJimm_A_A (46) */
      register_J = 0x01c6;
      /* opJLT_A_A (5b) */
      if (cmp_new < cmp_old) {
        register_PC = 0x01c6;
        break;
      };
      /* opNOP_A_B (57) */
      /* opLDAimm_B_AA (00) */
      flag_C = acc_a0 = register_A;
      cmp_old = register_B; /* step back cmp flag */
      register_B = cmp_new = 0;
      /* opNOP_A_B (57) */
      /* opSUBirg_B_AA (e8) */
      acc_a0 = register_A;
      register_B = (flag_C = ((cmp_old = register_B) + ((cmp_new = ram[register_I]) ^ 0xFFF) + 1)) & 0xFFF; /* ones compliment */
      /* opASRDe_A_AA (ee) */
      cmp_new = 0x0EEE; cmp_old = acc_a0 = register_A; flag_C = (0x0EEE + register_A);
      register_A = (register_A >> 1) | ((register_B & 1) << 11);
      register_B = (register_B >> 1) | (register_B & 0x800);
      /* opNOP_A_B (57) */
      /* opSUBimm_B_AA (31) */
      acc_a0 = register_A;
      register_B = (flag_C = ((cmp_old = register_B) + ((cmp_new = 0x1) ^ 0xFFF) + 1)) & 0xFFF; /* 1's-comp add */
      /* opLDPimm_A_A (8c) */
      register_P = 0xc; /* set page register */
      /* opLDAdir_A_AA (a6) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = 0xc6]; /* set I register */

      /* opADDimm_A_AA (22) */
      register_A = (flag_C = ((cmp_old = acc_a0 = register_A) + (cmp_new = 0x2))) & 0xFFF; /* add values, save carry */

      /* opASRe_A_AA (ed) */
      cmp_new = 0xDED; cmp_old = flag_C = acc_a0 = register_A;
      register_A = SEX(register_A); /* make signed */
      register_A = (((signed short int)(((signed short int)register_A) >> (signed short int)1)) /* SAR */) & 0xFFF;
      /* opASRe_A_AA (ed) */
      cmp_new = 0xDED; cmp_old = flag_C = acc_a0 = register_A;
      register_A = SEX(register_A); /* make signed */
      register_A = (((signed short int)(((signed short int)register_A) >> (signed short int)1)) /* SAR */) & 0xFFF;
      /* opLDPimm_A_A (87) */
      register_P = 0x7; /* set page register */
      /* opSTAdir_A_A (d4) */
      ram[register_I = 0x74] = register_A; /* store acc to RAM */
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opLDJimm_A_A (4f) */
      register_J = 0x01bf;

    case 0x01bf:

      /* opAWDirg_A_AA (e7) */
      cmp_old = acc_a0 = register_A;
      register_A = (flag_C = (register_A + (cmp_new = ram[register_I]))) & 0xFFF;
      set_watchdog();
      /* opNOP_A_B (57) */
      /* opADDimm_B_AA (21) */
      acc_a0 = register_A; /* save old accA bit0 */
      cmp_old = register_B; /* store old acc for later */
      register_B = (flag_C = (register_B + (cmp_new = 0x01))) & 0xFFF; /* add values */
      /* opJNC_A_AA (5d) */
      if (!(flag_C & CARRYBIT)) {
        register_PC = 0x01bf;
        break;
      };
      /* opLDPimm_A_A (8c) */
      register_P = 0xc; /* set page register */
      /* opADDdir_A_AA (66) */
      register_A = (flag_C = ((cmp_old = acc_a0 = register_A) + (cmp_new = ram[register_I = 0xc6]))) & 0xFFF; /* do acc operation */
      /* opSTAirg_A_A (e6) */
      ram[0xc6] = register_A; /* store acc */

    case 0x01c6:

      /* Invariants: register_P = 0x7 register_I = 0x73 register_A = 0x04 */;
      /* opLDPimm_A_A (8c) */
      register_P = 0xc; /* set page register */
      /* opLDAdir_A_AA (a6) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = 0xc6]; /* set I register */

      /* opLDIdir_A_A (c2) */
      register_I = ram[0xc2] & 0xff; /* set new register_I (8 bits) */
      /* opAWDirg_A_AA (e7) */
      cmp_old = acc_a0 = register_A;
      register_A = (flag_C = (register_A + (cmp_new = ram[register_I]))) & 0xFFF;
      set_watchdog();
      /* opSTAirg_A_A (e6) */
      ram[register_I] = register_A; /* store acc */
      /* opLDAimm_A_AA (09) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0x0900;
      /* opLDIdir_A_A (c1) */
      register_I = ram[0xc1] & 0xff; /* set new register_I (8 bits) */
      /* opSUBirg_A_AA (e8) */
      register_A = (flag_C = ((cmp_old = acc_a0 = register_A) + ((cmp_new = ram[register_I]) ^ 0xFFF) + 1)) & 0xFFF; /* ones compliment */
      /* opLDJimm_A_A (4f) */
      register_J = 0x020f;
      /* opJNC_A_AA (5d) */
      if (!(flag_C & CARRYBIT)) {
        register_PC = 0x020f;
        break;
      };
      /* opLDAimm_A_AA (07) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0x0700;
      /* opLDIdir_A_A (c2) */
      register_I = ram[0xc2] & 0xff; /* set new register_I (8 bits) */
      /* opSUBirg_A_AA (e8) */
      register_A = (flag_C = ((cmp_old = acc_a0 = register_A) + ((cmp_new = ram[register_I]) ^ 0xFFF) + 1)) & 0xFFF; /* ones compliment */
      /* opJNC_A_AA (5d) */
      if (!(flag_C & CARRYBIT)) {
        register_PC = 0x020f;
        break;
      };
      /* opLDAdir_A_AA (a8) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = 0xc8]; /* set I register */

      /* opSUBimm_A_AA (34) */
      cmp_old = acc_a0 = register_A;
      register_A = (flag_C = (register_A + (((cmp_new = 0x4) ^ 0xFFF) + 1))) & 0xFFF; /* 1's-comp add */
      /* opLDJimm_A_A (4f) */
      register_J = 0x01df;
      /* opJLT_A_A (5b) */
      if (cmp_new < cmp_old) {
        register_PC = 0x01df;
        break;
      };
      /* opLDAdir_A_AA (a9) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = 0xc9]; /* set I register */

      /* opSUBimm_A_AA (35) */
      cmp_old = acc_a0 = register_A;
      register_A = (flag_C = (register_A + (((cmp_new = 0x5) ^ 0xFFF) + 1))) & 0xFFF; /* 1's-comp add */
      /* opLDJimm_A_A (4f) */
      register_J = 0x020f;
      /* opJNC_A_AA (5d) */
      if (!(flag_C & CARRYBIT)) {
        register_PC = 0x020f;
        break;
      };

    case 0x01df:

      /* Invariants: register_P = 0xc */;
      /* opLDIdir_A_A (c2) */
      register_I = ram[0xc2] & 0xff; /* set new register_I (8 bits) */
      /* opNOP_A_B (57) */
      /* opLDAirg_B_AA (ea) */
      flag_C = acc_a0 = register_A;
      cmp_old = register_B;
      register_B = cmp_new = ram[register_I];

      /* opASRDe_A_AA (ee) */
      cmp_new = 0x0EEE; cmp_old = acc_a0 = register_A; flag_C = (0x0EEE + register_A);
      register_A = (register_A >> 1) | ((register_B & 1) << 11);
      register_B = (register_B >> 1) | (register_B & 0x800);
      /* opLDIdir_A_A (c1) */
      register_I = ram[0xc1] & 0xff; /* set new register_I (8 bits) */
      /* opLDAirg_A_AA (ea) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = ram[register_I];
      /* opASRe_A_AA (ed) */
      cmp_new = 0xDED; cmp_old = flag_C = acc_a0 = register_A;
      register_A = SEX(register_A); /* make signed */
      register_A = (((signed short int)(((signed short int)register_A) >> (signed short int)1)) /* SAR */) & 0xFFF;
      /* opVIN_A_A (f0) */

      FromX = register_A & 0xFFF; /* regA goes to x-coord */
      FromY = register_B & 0xFFF; /* regB goes to y-coord */

      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opSUBimmX_A_AA (30) */
      cmp_old = acc_a0 = register_A; /* back up regA */
      register_A = (flag_C = (register_A + (((cmp_new = 0x4e) ^ 0xFFF) + 1))) & 0xFFF; /* add */

    case 0x01ea:

      /* Invariants: register_P = 0xc */;
      /* opADDimm_A_AA (21) */
      register_A = (flag_C = ((cmp_old = acc_a0 = register_A) + (cmp_new = 0x1))) & 0xFFF; /* add values, save carry */

      /* opLDJimm_A_A (4a) */
      register_J = 0x01ea;
      /* opJNC_A_AA (5d) */
      if (!(flag_C & CARRYBIT)) {
        register_PC = 0x01ea;
        break;
      };
      /* opLDAimm_A_AA (02) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0x0200;
      /* opADDimmX_A_AA (20) */
      cmp_old = register_A; cmp_new = 0x66;
      register_A = flag_C = acc_a0 = 0x0266;
      /* opLDPimm_A_A (87) */
      register_P = 0x7; /* set page register */
      /* opADDdir_A_AA (60) */
      register_A = (flag_C = ((cmp_old = acc_a0 = register_A) + (cmp_new = ram[register_I = 0x70]))) & 0xFFF; /* do acc operation */
      /* opXLT_A_AA (e2) */
      cmp_old = register_A; register_A = cmp_new = rom[register_A]; /* new acc value */
      /* opNOP_A_A (5f) */
      /* opLSLe_A_AA (ec) */
      cmp_new = 0x0CEC;
      cmp_old = acc_a0 = register_A;
      flag_C = (0x0CEC + register_A);
      register_A = (register_A << 1) & 0x0FFF;
      /* opNOP_A_B (57) */
      /* opLDAimm_B_AA (00) */
      flag_C = acc_a0 = register_A;
      cmp_old = register_B; /* step back cmp flag */
      register_B = cmp_new = 0;
      /* opLLT_A_AA (e4) */
      { CINEBYTE temp_byte = 0;
        for (;;) {
          if (   (((register_A >> 8) & 0x0A) && (((register_A >> 8) & 0x0A) ^ 0x0A))
                 ||   (((register_B >> 8) & 0x0A) && (((register_B >> 8) & 0x0A) ^ 0x0A))  ) break;
          register_A <<= 1; register_B <<= 1;
          if (!(++temp_byte)) break /* This may not be correct */;
        }
        vgShiftLength = temp_byte & 0xfff; register_A &= 0x0FFF; register_B &= 0x0FFF;
      }
      /* opLDPimm_A_A (8c) */
      register_P = 0xc; /* set page register */
      /* opLDIdir_A_A (c2) */
      register_I = ram[0xc2] & 0xff; /* set new register_I (8 bits) */
      /* opNOP_A_B (57) */
      /* opLDAirg_B_AA (ea) */
      flag_C = acc_a0 = register_A;
      cmp_old = register_B;
      register_B = cmp_new = ram[register_I];

      /* opASRDe_A_AA (ee) */
      cmp_new = 0x0EEE; cmp_old = acc_a0 = register_A; flag_C = (0x0EEE + register_A);
      register_A = (register_A >> 1) | ((register_B & 1) << 11);
      register_B = (register_B >> 1) | (register_B & 0x800);
      /* opLDIdir_A_A (c1) */
      register_I = ram[0xc1] & 0xff; /* set new register_I (8 bits) */
      /* opLDAirg_A_AA (ea) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = ram[register_I];
      /* opASRe_A_AA (ed) */
      cmp_new = 0xDED; cmp_old = flag_C = acc_a0 = register_A;
      register_A = SEX(register_A); /* make signed */
      register_A = (((signed short int)(((signed short int)register_A) >> (signed short int)1)) /* SAR */) & 0xFFF;
      /* opVDR_A_A (e0) */
      {
        /* set ending points and draw the vector, or buffer for a later draw. */
        int ToX = register_A & 0xFFF;
        int ToY = register_B & 0xFFF;

        /* Sign extend from 20 bit CCPU to 32bit target machine */
        FromX = SEX(FromX);
        ToX = SEX(ToX);
        FromY = SEX(FromY);
        ToY = SEX(ToY);

        /* figure out the vector */
        ToX -= FromX;
        ToX = ((signed short int)(((signed short int)ToX) >> (signed short int)vgShiftLength)) /* SAR */;
        ToX += FromX;

        ToY -= FromY;
        ToY = ((signed short int)(((signed short int)ToY) >> (signed short int)vgShiftLength)) /* SAR */;
        ToY += FromY;

        /* render the line */
#ifndef DUALCPU
        CinemaVectorData (FromX, FromY, ToX, ToY, vgColour);
#endif

      }
      /* opLDAdir_A_AA (a1) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = 0xc1]; /* set I register */

      /* opADDimm_A_AA (22) */
      register_A = (flag_C = ((cmp_old = acc_a0 = register_A) + (cmp_new = 0x2))) & 0xFFF; /* add values, save carry */

      /* opSTAirg_A_A (e6) */
      ram[0xc1] = register_A; /* store acc */
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opADDimmX_A_AA (20) */
      cmp_old = register_A; cmp_new = 0xc0;
      register_A = flag_C = acc_a0 = 0x00c0;
      /* opLDAirg_A_AA (ea) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = ram[register_I];
      /* opLDJimm_A_A (48) */
      register_J = 0x0148;
      /* opJLT_A_A (5b) */
      if (cmp_new < cmp_old) {
        register_PC = 0x0148;
        break;
      };
      /* opLDJimm_A_A (46) */
      register_J = 0x08b6;
      /* opJMP_A_A (58) */
      {
        register_PC = 0x08b6;
        break;
      };

    case 0x020f:

      /* Invariants: register_P = 0xc */;
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opADDimmX_A_AA (20) */
      cmp_old = register_A; cmp_new = 0x3f;
      register_A = flag_C = acc_a0 = 0x003f;
      /* opCMPdir_A_AA (b7) */
      cmp_old = acc_a0 = register_A; /* backup old acc */
      flag_C = (((cmp_new = ram[register_I = 0xc7]) ^ 0xFFF) + 1 + register_A);
      /* opANDirg_A_AA (e9) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A &= (cmp_new = ram[register_I]);
      /* opADDimm_A_AA (22) */
      register_A = (flag_C = ((cmp_old = acc_a0 = register_A) + (cmp_new = 0x2))) & 0xFFF; /* add values, save carry */

      /* opSTAirg_A_A (e6) */
      ram[0xc7] = register_A; /* store acc */
      /* opLDAimm_A_AA (02) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0x0200;
      /* opADDimmX_A_AA (20) */
      cmp_old = register_A; cmp_new = 0x74;
      register_A = flag_C = acc_a0 = 0x0274;
      /* opADDdir_A_AA (67) */
      register_A = (flag_C = ((cmp_old = acc_a0 = register_A) + (cmp_new = ram[register_I = 0xc7]))) & 0xFFF; /* do acc operation */
      /* opSTAdir_A_A (d5) */
      ram[register_I = 0xc5] = register_A; /* store acc to RAM */
      /* opADDimm_A_AA (21) */
      register_A = (flag_C = ((cmp_old = acc_a0 = register_A) + (cmp_new = 0x1))) & 0xFFF; /* add values, save carry */

      /* opXLT_A_AA (e2) */
      cmp_old = register_A; register_A = cmp_new = rom[register_A]; /* new acc value */
      /* opNOP_A_A (5f) */
      /* opSUBimmX_A_AA (30) */
      cmp_old = acc_a0 = register_A; /* back up regA */
      register_A = (flag_C = (register_A + (((cmp_new = 0x80) ^ 0xFFF) + 1))) & 0xFFF; /* add */
      /* opLSLe_A_AA (ec) */
      cmp_new = 0x0CEC;
      cmp_old = acc_a0 = register_A;
      flag_C = (0x0CEC + register_A);
      register_A = (register_A << 1) & 0x0FFF;
      /* opLSLe_A_AA (ec) */
      cmp_new = 0x0CEC;
      cmp_old = acc_a0 = register_A;
      flag_C = (0x0CEC + register_A);
      register_A = (register_A << 1) & 0x0FFF;
      /* opLSLe_A_AA (ec) */
      cmp_new = 0x0CEC;
      cmp_old = acc_a0 = register_A;
      flag_C = (0x0CEC + register_A);
      register_A = (register_A << 1) & 0x0FFF;
      /* opSTAdir_A_A (d6) */
      ram[register_I = (register_P << 4) + 0x6] = register_A; /* store acc to RAM */
      /* opLDAdir_A_AA (a5) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = (register_P << 4) + 0x05]; /* set I register */

      /* opXLT_A_AA (e2) */
      cmp_old = register_A; register_A = cmp_new = rom[register_A]; /* new acc value */
      /* opNOP_A_A (5f) */
      /* opSUBimmX_A_AA (30) */
      cmp_old = acc_a0 = register_A; /* back up regA */
      register_A = (flag_C = (register_A + (((cmp_new = 0x80) ^ 0xFFF) + 1))) & 0xFFF; /* add */
      /* opLSLe_A_AA (ec) */
      cmp_new = 0x0CEC;
      cmp_old = acc_a0 = register_A;
      flag_C = (0x0CEC + register_A);
      register_A = (register_A << 1) & 0x0FFF;
      /* opLSLe_A_AA (ec) */
      cmp_new = 0x0CEC;
      cmp_old = acc_a0 = register_A;
      flag_C = (0x0CEC + register_A);
      register_A = (register_A << 1) & 0x0FFF;
      /* opLSLe_A_AA (ec) */
      cmp_new = 0x0CEC;
      cmp_old = acc_a0 = register_A;
      flag_C = (0x0CEC + register_A);
      register_A = (register_A << 1) & 0x0FFF;
      /* opSTAirg_A_A (e6) */
      ram[register_I] = register_A; /* store acc */
      /* opLDAdir_A_AA (a0) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = (register_P << 4) + 0x00]; /* set I register */

      /* opADDimm_A_AA (21) */
      register_A = (flag_C = ((cmp_old = acc_a0 = register_A) + (cmp_new = 0x1))) & 0xFFF; /* add values, save carry */

      /* opSTAirg_A_A (e6) */
      ram[register_I] = register_A; /* store acc */
      /* opXLT_A_AA (e2) */
      cmp_old = register_A; register_A = cmp_new = rom[register_A]; /* new acc value */
      /* opNOP_A_A (5f) */
      /* opAWDirg_A_AA (e7) */
      cmp_old = acc_a0 = register_A;
      register_A = (flag_C = (register_A + (cmp_new = ram[register_I]))) & 0xFFF;
      set_watchdog();
      /* opXLT_A_AA (e2) */
      cmp_old = register_A; register_A = cmp_new = rom[register_A]; /* new acc value */
      /* opNOP_A_A (5f) */
      /* opAWDirg_A_AA (e7) */
      cmp_old = acc_a0 = register_A;
      register_A = (flag_C = (register_A + (cmp_new = ram[register_I]))) & 0xFFF;
      set_watchdog();
      /* opSTAdir_A_A (d3) */
      ram[register_I = (register_P << 4) + 0x3] = register_A; /* store acc to RAM */
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opADDimmX_A_AA (20) */
      cmp_old = register_A; cmp_new = 0x7f;
      register_A = flag_C = acc_a0 = 0x007f;
      /* opANDirg_A_AA (e9) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A &= (cmp_new = ram[register_I]);
      /* opSTAirg_A_A (e6) */
      ram[register_I] = register_A; /* store acc */
      /* opLSRe_A_AA (eb) */
      cmp_new = 0x0BEB; cmp_old = acc_a0 = register_A; flag_C = (0x0BEB + register_A);
      register_A >>= 1;
      /* opLDJimm_A_A (43) */
      register_J = 0x0243;
      /* opJA0_A_A (5e) */
      if (acc_a0 & 0x01) {
        register_PC = 0x0243;
        break;
      };

      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opSUBirg_A_AA (e8) */
      register_A = (flag_C = ((cmp_old = acc_a0 = register_A) + ((cmp_new = ram[register_I]) ^ 0xFFF) + 1)) & 0xFFF; /* ones compliment */
      /* opSTAirg_A_A (e6) */
      ram[register_I] = register_A; /* store acc */

    case 0x0243:

      /* Invariants: register_P = 0xc register_I = 0xc3 */;
      /* opLDAimm_A_AA (04) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0x0400;
      /* opAWDirg_A_AA (e7) */
      cmp_old = acc_a0 = register_A;
      register_A = (flag_C = (register_A + (cmp_new = ram[0xc3]))) & 0xFFF;
      set_watchdog();
      /* opADDdir_A_AA (65) */
      register_A = (flag_C = ((cmp_old = acc_a0 = register_A) + (cmp_new = ram[register_I = 0xc5]))) & 0xFFF; /* do acc operation */
      /* opLDIdir_A_A (c1) */
      register_I = ram[0xc1] & 0xff; /* set new register_I (8 bits) */
      /* opSTAirg_A_A (e6) */
      ram[register_I] = register_A; /* store acc */
      /* opLDAdir_A_AA (a0) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = 0xc0]; /* set I register */

      /* opADDimm_A_AA (21) */
      register_A = (flag_C = ((cmp_old = acc_a0 = register_A) + (cmp_new = 0x1))) & 0xFFF; /* add values, save carry */

      /* opSTAirg_A_A (e6) */
      ram[0xc0] = register_A; /* store acc */
      /* opXLT_A_AA (e2) */
      cmp_old = register_A; register_A = cmp_new = rom[register_A]; /* new acc value */
      /* opNOP_A_A (5f) */
      /* opAWDirg_A_AA (e7) */
      cmp_old = acc_a0 = register_A;
      register_A = (flag_C = (register_A + (cmp_new = ram[register_I]))) & 0xFFF;
      set_watchdog();
      /* opXLT_A_AA (e2) */
      cmp_old = register_A; register_A = cmp_new = rom[register_A]; /* new acc value */
      /* opNOP_A_A (5f) */
      /* opAWDirg_A_AA (e7) */
      cmp_old = acc_a0 = register_A;
      register_A = (flag_C = (register_A + (cmp_new = ram[register_I]))) & 0xFFF;
      set_watchdog();
      /* opSTAdir_A_A (d3) */
      ram[register_I = (register_P << 4) + 0x3] = register_A; /* store acc to RAM */
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opADDimmX_A_AA (20) */
      cmp_old = register_A; cmp_new = 0x7f;
      register_A = flag_C = acc_a0 = 0x007f;
      /* opANDirg_A_AA (e9) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A &= (cmp_new = ram[register_I]);
      /* opSTAirg_A_A (e6) */
      ram[register_I] = register_A; /* store acc */
      /* opLSRe_A_AA (eb) */
      cmp_new = 0x0BEB; cmp_old = acc_a0 = register_A; flag_C = (0x0BEB + register_A);
      register_A >>= 1;
      /* opLDJimm_A_A (4e) */
      register_J = 0x025e;
      /* opJA0_A_A (5e) */
      if (acc_a0 & 0x01) {
        register_PC = 0x025e;
        break;
      };

      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opSUBirg_A_AA (e8) */
      register_A = (flag_C = ((cmp_old = acc_a0 = register_A) + ((cmp_new = ram[register_I]) ^ 0xFFF) + 1)) & 0xFFF; /* ones compliment */
      /* opSTAirg_A_A (e6) */
      ram[register_I] = register_A; /* store acc */

    case 0x025e:

      /* Invariants: register_P = 0xc register_I = 0xc3 */;
      /* opLDAimm_A_AA (03) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0x0300;
      /* opAWDirg_A_AA (e7) */
      cmp_old = acc_a0 = register_A;
      register_A = (flag_C = (register_A + (cmp_new = ram[0xc3]))) & 0xFFF;
      set_watchdog();
      /* opADDdir_A_AA (66) */
      register_A = (flag_C = ((cmp_old = acc_a0 = register_A) + (cmp_new = ram[register_I = 0xc6]))) & 0xFFF; /* do acc operation */
      /* opLDIdir_A_A (c2) */
      register_I = ram[0xc2] & 0xff; /* set new register_I (8 bits) */
      /* opSTAirg_A_A (e6) */
      ram[register_I] = register_A; /* store acc */
      /* opLDJimm_A_A (4b) */
      register_J = 0x014b;
      /* opJMP_A_A (58) */
      {
        register_PC = 0x014b;
        break;
      };

    case 0x02b6:

      /* Invariants: register_P = 0x7 register_I = 0x7d register_A = 0x00 */;
      /* opLDAirg_A_AA (ea) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = ram[register_I];
      /* opSUBimmX_A_AA (30) */
      cmp_old = acc_a0 = register_A; /* back up regA */
      register_A = (flag_C = (register_A + (((cmp_new = 0x1f) ^ 0xFFF) + 1))) & 0xFFF; /* add */
      /* opLDJimm_A_A (4c) */
      register_J = 0x02dc;
      /* opJLT_A_A (5b) */
      if (cmp_new < cmp_old) {
        register_PC = 0x02dc;
        break;
      };
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opADDimm_A_AA (22) */
      cmp_new = 0x2; cmp_old = acc_a0 = register_A; register_A = flag_C = 0x0002;

      /* opCMPdir_A_AA (bd) */
      cmp_old = acc_a0 = register_A; /* backup old acc */
      flag_C = (((cmp_new = ram[register_I = 0x7d]) ^ 0xFFF) + 1 + register_A);
      /* opLDJimm_A_A (4d) */
      register_J = 0x08ed;
      /* opJLT_A_A (5b) */
      if (cmp_new < cmp_old) {
        register_PC = 0x08ed;
        break;
      };
      /* opADDimm_A_AA (25) */
      cmp_new = 0x5; cmp_old = acc_a0 = register_A; register_A = flag_C = 0x0007;

      /* opCMPdir_A_AA (bd) */
      cmp_old = acc_a0 = register_A; /* backup old acc */
      flag_C = (((cmp_new = ram[register_I = 0x7d]) ^ 0xFFF) + 1 + register_A);
      /* opJNC_A_AA (5d) */
      if (!(flag_C & CARRYBIT)) {
        register_PC = 0x08ed;
        break;
      };
      /* opINP_A_AA (1f) */
      cmp_old = flag_C = acc_a0 = register_A;
#ifdef RAWIO
      register_A = cmp_new = get_io_bit(0xf);
#else
      register_A = cmp_new = get_io_bit(0xf);
#endif
      /* opADDimm_A_AA (21) */
      register_A = (flag_C = ((cmp_old = acc_a0 = register_A) + (cmp_new = 0x1))) & 0xFFF; /* add values, save carry */

      /* opLDPimm_A_A (87) */
      /* opSTAdir_A_A (d5) */
      ram[register_I = 0x75] = register_A; /* store acc to RAM */
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opADDimm_A_AA (21) */
      cmp_new = 0x1; cmp_old = acc_a0 = register_A; register_A = flag_C = 0x0001;

      /* opANDirg_A_AA (e9) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A &= (cmp_new = ram[register_I]);
      /* opADDimmX_A_AA (20) */
      register_A = (flag_C = ((acc_a0 = cmp_old = register_A) + (cmp_new = 0x001e))) & 0xFFF; /* add values */
      /* opOUTbi_A_A (93) */
      /* opOUTsnd_A (93) */
      set_sound_data(register_A & 1);
      /* opLSRe_A_AA (eb) */
      cmp_new = 0x0BEB; cmp_old = acc_a0 = register_A; flag_C = (0x0BEB + register_A);
      register_A >>= 1;
      /* opOUTbi_A_A (90) */
      /* opOUTsnd_A (90) */
      set_sound_addr_A(register_A & 1);
      /* opLSRe_A_AA (eb) */
      cmp_new = 0x0BEB; cmp_old = acc_a0 = register_A; flag_C = (0x0BEB + register_A);
      register_A >>= 1;
      /* opOUTbi_A_A (91) */
      /* opOUTsnd_A (91) */
      set_sound_addr_B(register_A & 1);
      /* opLSRe_A_AA (eb) */
      cmp_new = 0x0BEB; cmp_old = acc_a0 = register_A; flag_C = (0x0BEB + register_A);
      register_A >>= 1;
      /* opOUTbi_A_A (92) */
      /* opOUTsnd_A (92) */
      set_sound_addr_C(register_A & 1);
      /* opLSRe_A_AA (eb) */
      cmp_new = 0x0BEB; cmp_old = acc_a0 = register_A; flag_C = (0x0BEB + register_A);
      register_A >>= 1;
      /* opOUTbi_A_A (94) */
      /* opOUTsnd_A (94) */
      if (register_A & 1) strobe_sound_on(); else strobe_sound_off();
      /* opLSRe_A_AA (eb) */
      cmp_new = 0x0BEB; cmp_old = acc_a0 = register_A; flag_C = (0x0BEB + register_A);
      register_A >>= 1;
      /* opOUTbi_A_A (94) */
      /* opOUTsnd_A (94) */
      if (register_A & 1) strobe_sound_on(); else strobe_sound_off();
      /* opLDJimm_A_A (4b) */
      register_J = 0x08fb;
      /* opJMP_A_A (58) */
      {
        register_PC = 0x08fb;
        break;
      };

    case 0x02dc:

      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opSTAirg_A_A (e6) */
      ram[register_I] = register_A; /* store acc */

    case 0x02de:

      /* Invariants: register_P = 0x6 register_I = 0x69 register_A = 0x00 */;
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opLDPimm_A_A (87) */
      register_P = 0x7; /* set page register */
      /* opCMPdir_A_AA (b3) */
      cmp_old = acc_a0 = register_A; /* backup old acc */
      flag_C = (((cmp_new = ram[register_I = 0x73]) ^ 0xFFF) + 1 + register_A);
      /* opLDJimm_A_A (4d) */
      register_J = 0x02ed;
      /* opJNC_A_AA (5d) */
      if (!(flag_C & CARRYBIT)) {
        register_PC = 0x02ed;
        break;
      };
      /* opLDAimm_A_AA (03) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0x0300;
      /* opADDimmX_A_AA (20) */
      cmp_old = register_A; cmp_new = 0x08;
      register_A = flag_C = acc_a0 = 0x0308;
      /* opLDPimm_A_A (81) */
      register_P = 0x1; /* set page register */
      /* opSTAdir_A_A (df) */
      ram[register_I = 0x1f] = 0x308; /* store acc to RAM */
      /* opLDJimm_A_A (40) */
      register_J = 0x0dc0;
      /* opLDPimm_A_A (82) */
      register_P = 0x2; /* set page register */
      /* opJPP8_A_B (50) */
      register_PC = 0x1dc0; /* Jump to other rom bank */
      break;

    case 0x02ed:

      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opLDPimm_A_A (86) */
      register_P = 0x6; /* set page register */
      /* opSTAdir_A_A (d2) */
      ram[register_I = 0x62] = 0x000; /* store acc to RAM */
      /* opSTAdir_A_A (d0) */
      ram[register_I = 0x60] = 0x000; /* store acc to RAM */
      /* opADDimm_A_AA (25) */
      cmp_new = 0x5; cmp_old = acc_a0 = register_A; register_A = flag_C = 0x0005;

      /* opLDPimm_A_A (87) */
      register_P = 0x7; /* set page register */
      /* opCMPdir_A_AA (b3) */
      cmp_old = acc_a0 = register_A; /* backup old acc */
      flag_C = (((cmp_new = ram[register_I = 0x73]) ^ 0xFFF) + 1 + register_A);
      /* opLDJimm_A_A (4a) */
      register_J = 0x004a;
      /* opJNC_A_AA (5d) */
      if (!(flag_C & CARRYBIT)) {
        register_PC = 0x004a;
        break;
      };
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opADDimmX_A_AA (20) */
      cmp_old = register_A; cmp_new = 0x15;
      register_A = flag_C = acc_a0 = 0x0015;
      /* opOUTbi_A_A (93) */
      /* opOUTsnd_A (93) */
      set_sound_data(1);
      /* opLSRe_A_AA (eb) */
      cmp_new = 0x0BEB; cmp_old = acc_a0 = register_A; flag_C = (0x0BEB + register_A);
      register_A >>= 1;
      /* opOUTbi_A_A (90) */
      /* opOUTsnd_A (90) */
      set_sound_addr_A(register_A & 1);
      /* opLSRe_A_AA (eb) */
      cmp_new = 0x0BEB; cmp_old = acc_a0 = register_A; flag_C = (0x0BEB + register_A);
      register_A >>= 1;
      /* opOUTbi_A_A (91) */
      /* opOUTsnd_A (91) */
      set_sound_addr_B(register_A & 1);
      /* opLSRe_A_AA (eb) */
      cmp_new = 0x0BEB; cmp_old = acc_a0 = register_A; flag_C = (0x0BEB + register_A);
      register_A >>= 1;
      /* opOUTbi_A_A (92) */
      /* opOUTsnd_A (92) */
      set_sound_addr_C(register_A & 1);
      /* opLSRe_A_AA (eb) */
      cmp_new = 0x0BEB; cmp_old = acc_a0 = register_A; flag_C = (0x0BEB + register_A);
      register_A >>= 1;
      /* opOUTbi_A_A (94) */
      /* opOUTsnd_A (94) */
      if (register_A & 1) strobe_sound_on(); else strobe_sound_off();
      /* opLSRe_A_AA (eb) */
      cmp_new = 0x0BEB; cmp_old = acc_a0 = register_A; flag_C = (0x0BEB + register_A);
      register_A >>= 1;
      /* opOUTbi_A_A (94) */
      /* opOUTsnd_A (94) */
      if (register_A & 1) strobe_sound_on(); else strobe_sound_off();
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opADDimm_A_AA (25) */
      cmp_new = 0x5; cmp_old = acc_a0 = register_A; register_A = flag_C = 0x0005;

      /* opJMP_A_A (58) */
      {
        register_PC = 0x004a;
        break;
      };

    case 0x0308:

      /* opNOP_A_A (5f) */
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opLDPimm_A_A (83) */
      register_P = 0x3; /* set page register */
      /* opCMPdir_A_AA (b7) */
      cmp_old = acc_a0 = register_A; /* backup old acc */
      flag_C = (((cmp_new = ram[register_I = 0x37]) ^ 0xFFF) + 1 + register_A);
      /* opLDJimm_A_A (49) */
      register_J = 0x0319;
      /* opJNC_A_AA (5d) */
      if (!(flag_C & CARRYBIT)) {
        register_PC = 0x0319;
        break;
      };
      /* opLDAdir_A_AA (ad) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = 0x3d]; /* set I register */

      /* opJEQ_A_AA (5c) */
      if (cmp_new == cmp_old) {
        register_PC = 0x0319;
        break;
      };
      /* opADDdir_A_AA (63) */
      register_A = (flag_C = ((cmp_old = acc_a0 = register_A) + (cmp_new = ram[register_I = 0x33]))) & 0xFFF; /* do acc operation */
      /* opSTAirg_A_A (e6) */
      ram[0x33] = register_A; /* store acc */
      /* opLDAimm_A_AA (0f) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0x0f00;
      /* opCMPdir_A_AA (b3) */
      cmp_old = acc_a0 = register_A; /* backup old acc */
      flag_C = (((cmp_new = ram[register_I = 0x33]) ^ 0xFFF) + 1 + register_A);
      /* opJLT_A_A (5b) */
      if (cmp_new < cmp_old) {
        register_PC = 0x0319;
        break;
      };
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opSTAdir_A_A (d0) */
      ram[register_I = 0x30] = 0x000; /* store acc to RAM */
      /* opSTAdir_A_A (dd) */
      ram[register_I = 0x3d] = 0x000; /* store acc to RAM */

    case 0x0319:

      /* Invariants: register_P = 0x3 register_A = 0x00 */;
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opLDPimm_A_A (84) */
      register_P = 0x4; /* set page register */
      /* opCMPdir_A_AA (b7) */
      cmp_old = acc_a0 = register_A; /* backup old acc */
      flag_C = (((cmp_new = ram[register_I = 0x47]) ^ 0xFFF) + 1 + register_A);
      /* opLDJimm_A_A (49) */
      register_J = 0x0329;
      /* opJNC_A_AA (5d) */
      if (!(flag_C & CARRYBIT)) {
        register_PC = 0x0329;
        break;
      };
      /* opLDAdir_A_AA (ad) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = 0x4d]; /* set I register */

      /* opJEQ_A_AA (5c) */
      if (cmp_new == cmp_old) {
        register_PC = 0x0329;
        break;
      };
      /* opADDdir_A_AA (63) */
      register_A = (flag_C = ((cmp_old = acc_a0 = register_A) + (cmp_new = ram[register_I = 0x43]))) & 0xFFF; /* do acc operation */
      /* opSTAirg_A_A (e6) */
      ram[0x43] = register_A; /* store acc */
      /* opLDAimm_A_AA (0f) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0x0f00;
      /* opCMPdir_A_AA (b3) */
      cmp_old = acc_a0 = register_A; /* backup old acc */
      flag_C = (((cmp_new = ram[register_I = 0x43]) ^ 0xFFF) + 1 + register_A);
      /* opJLT_A_A (5b) */
      if (cmp_new < cmp_old) {
        register_PC = 0x0329;
        break;
      };
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opSTAdir_A_A (d0) */
      ram[register_I = 0x40] = 0x000; /* store acc to RAM */
      /* opSTAdir_A_A (dd) */
      ram[register_I = 0x4d] = 0x000; /* store acc to RAM */

    case 0x0329:

      /* Invariants: register_P = 0x4 register_A = 0x00 */;
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opLDPimm_A_A (85) */
      register_P = 0x5; /* set page register */
      /* opCMPdir_A_AA (b7) */
      cmp_old = acc_a0 = register_A; /* backup old acc */
      flag_C = (((cmp_new = ram[register_I = 0x57]) ^ 0xFFF) + 1 + register_A);
      /* opLDJimm_A_A (49) */
      register_J = 0x0339;
      /* opJNC_A_AA (5d) */
      if (!(flag_C & CARRYBIT)) {
        register_PC = 0x0339;
        break;
      };
      /* opLDAdir_A_AA (ad) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = 0x5d]; /* set I register */

      /* opJEQ_A_AA (5c) */
      if (cmp_new == cmp_old) {
        register_PC = 0x0339;
        break;
      };
      /* opADDdir_A_AA (63) */
      register_A = (flag_C = ((cmp_old = acc_a0 = register_A) + (cmp_new = ram[register_I = 0x53]))) & 0xFFF; /* do acc operation */
      /* opSTAirg_A_A (e6) */
      ram[0x53] = register_A; /* store acc */
      /* opLDAimm_A_AA (0f) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0x0f00;
      /* opCMPdir_A_AA (b3) */
      cmp_old = acc_a0 = register_A; /* backup old acc */
      flag_C = (((cmp_new = ram[register_I = 0x53]) ^ 0xFFF) + 1 + register_A);
      /* opJLT_A_A (5b) */
      if (cmp_new < cmp_old) {
        register_PC = 0x0339;
        break;
      };
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opSTAdir_A_A (d0) */
      ram[register_I = 0x50] = 0x000; /* store acc to RAM */
      /* opSTAdir_A_A (dd) */
      ram[register_I = 0x5d] = 0x000; /* store acc to RAM */

    case 0x0339:

      /* Invariants: register_P = 0x5 register_A = 0x00 */;
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opLDPimm_A_A (86) */
      register_P = 0x6; /* set page register */
      /* opCMPdir_A_AA (b9) */
      cmp_old = acc_a0 = register_A; /* backup old acc */
      flag_C = (((cmp_new = ram[register_I = 0x69]) ^ 0xFFF) + 1 + register_A);
      /* opLDJimm_A_A (4f) */
      register_J = 0x086f;
      /* opJEQ_A_AA (5c) */
      if (cmp_new == cmp_old) {
        register_PC = 0x086f;
        break;
      };
      /* opINP_A_AA (16) */
      cmp_old = flag_C = acc_a0 = register_A;
#ifdef RAWIO
      register_A = cmp_new = get_io_bit(0x6);
#else
      register_A = cmp_new = get_io_shields();
#endif
      /* opADDimm_A_AA (21) */
      register_A = (flag_C = ((cmp_old = acc_a0 = register_A) + (cmp_new = 0x1))) & 0xFFF; /* add values, save carry */

      /* opLDJimm_A_A (4f) */
      register_J = 0x086f;
      /* opJEQ_A_AA (5c) */
      if (cmp_new == cmp_old) {
        register_PC = 0x086f;
        break;
      };
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opLDPimm_A_A (88) */
      register_P = 0x8; /* set page register */
      /* opCMPdir_A_AA (b1) */
      cmp_old = acc_a0 = register_A; /* backup old acc */
      flag_C = (((cmp_new = ram[register_I = 0x81]) ^ 0xFFF) + 1 + register_A);
      /* opJEQ_A_AA (5c) */
      if (cmp_new == cmp_old) {
        register_PC = 0x086f;
        break;
      };
      /* opLDAdir_A_AA (a0) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = 0x80]; /* set I register */

      /* opADDimm_A_AA (21) */
      register_A = (flag_C = ((cmp_old = acc_a0 = register_A) + (cmp_new = 0x1))) & 0xFFF; /* add values, save carry */

      /* opSTAirg_A_A (e6) */
      ram[0x80] = register_A; /* store acc */
      /* opSUBimm_A_AA (38) */
      cmp_old = acc_a0 = register_A;
      register_A = (flag_C = (register_A + (((cmp_new = 0x8) ^ 0xFFF) + 1))) & 0xFFF; /* 1's-comp add */
      /* opLDJimm_A_A (47) */
      register_J = 0x0377;
      /* opJNC_A_AA (5d) */
      if (!(flag_C & CARRYBIT)) {
        register_PC = 0x0377;
        break;
      };
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opSTAirg_A_A (e6) */
      ram[0x80] = 0x000; /* store acc */
      /* opLDAdir_A_AA (a1) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = 0x81]; /* set I register */

      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opLDJimm_A_A (43) */
      register_J = 0x0373;
      /* opJEQ_A_AA (5c) */
      if (cmp_new == cmp_old) {
        register_PC = 0x0373;
        break;
      };
      /* opADDimmX_A_AA (20) */
      cmp_old = register_A; cmp_new = 0xff;
      register_A = flag_C = acc_a0 = 0x00ff;
      /* opANDirg_A_AA (e9) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A &= (cmp_new = ram[register_I]);
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opLDJimm_A_A (4d) */
      register_J = 0x036d;
      /* opJEQ_A_AA (5c) */
      if (cmp_new == cmp_old) {
        register_PC = 0x036d;
        break;
      };
      /* opADDimm_A_AA (2f) */
      cmp_new = 0xf; cmp_old = acc_a0 = register_A; register_A = flag_C = 0x000f;

      /* opANDirg_A_AA (e9) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A &= (cmp_new = ram[register_I]);
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opLDJimm_A_A (48) */
      register_J = 0x0368;
      /* opJEQ_A_AA (5c) */
      if (cmp_new == cmp_old) {
        register_PC = 0x0368;
        break;
      };
      /* opLDAirg_A_AA (ea) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = ram[register_I];
      /* opSUBimm_A_AA (31) */
      cmp_old = acc_a0 = register_A;
      register_A = (flag_C = (register_A + (((cmp_new = 0x1) ^ 0xFFF) + 1))) & 0xFFF; /* 1's-comp add */
      /* opLDJimm_A_A (46) */
      register_J = 0x0376;
      /* opJMP_A_A (58) */
      {
        register_PC = 0x0376;
        break;
      };

    case 0x0368:

      /* Invariants: register_P = 0x8 register_I = 0x81 register_A = 0x00 */;
      /* opLDAirg_A_AA (ea) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = ram[register_I];
      /* opSUBimm_A_AA (37) */
      cmp_old = acc_a0 = register_A;
      register_A = (flag_C = (register_A + (((cmp_new = 0x7) ^ 0xFFF) + 1))) & 0xFFF; /* 1's-comp add */
      /* opLDJimm_A_A (46) */
      register_J = 0x0376;
      /* opJMP_A_A (58) */
      {
        register_PC = 0x0376;
        break;
      };

    case 0x036d:

      /* opLDAirg_A_AA (ea) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = ram[register_I];
      /* opSUBimmX_A_AA (30) */
      cmp_old = acc_a0 = register_A; /* back up regA */
      register_A = (flag_C = (register_A + (((cmp_new = 0x67) ^ 0xFFF) + 1))) & 0xFFF; /* add */
      /* opLDJimm_A_A (46) */
      register_J = 0x0376;
      /* opJMP_A_A (58) */
      {
        register_PC = 0x0376;
        break;
      };

    case 0x0373:

      /* opLDAimm_A_AA (09) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0x0900;
      /* opADDimmX_A_AA (20) */
      cmp_old = register_A; cmp_new = 0x99;
      register_A = flag_C = acc_a0 = 0x0999;

    case 0x0376:

      /* Invariants: register_P = 0x8 register_I = 0x81 */;
      /* opSTAirg_A_A (e6) */
      ram[0x81] = register_A; /* store acc */

    case 0x0377:

      /* Invariants: register_P = 0x8 */;
      /* opNOP_A_B (57) */
      /* opLDAimm_B_AA (03) */
      flag_C = acc_a0 = register_A;
      cmp_old = register_B; /* step back cmp flag */
      register_B = cmp_new = 0x0300;
      /* opASRDe_A_AA (ee) */
      cmp_new = 0x0EEE; cmp_old = acc_a0 = register_A; flag_C = (0x0EEE + register_A);
      register_A = (register_A >> 1) | ((register_B & 1) << 11);
      register_B = (register_B >> 1) | (register_B & 0x800);
      /* opLDPimm_A_A (81) */
      register_P = 0x1; /* set page register */
      /* opNOP_A_B (57) */
      /* opSTAdir_B_BB (d1) */
      ram[register_I = (register_P << 4) + 0x1] = register_B; /* set I register and store B to ram */
      /* opLDAimm_A_AA (02) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0x0200;
      /* opSTAdir_A_A (d0) */
      ram[register_I = 0x10] = 0x200; /* store acc to RAM */
      /* opLDJimm_A_A (41) */
      register_J = 0x0381;
      /* opJDR_A_A (5a) */
      /* opVIN_A_A (f0) */

      FromX = register_A & 0xFFF; /* regA goes to x-coord */
      FromY = register_B & 0xFFF; /* regB goes to y-coord */

      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opSUBimmX_A_AA (30) */
      cmp_old = acc_a0 = register_A; /* back up regA */
      register_A = (flag_C = (register_A + (((cmp_new = 0x43) ^ 0xFFF) + 1))) & 0xFFF; /* add */

    case 0x0386:

      /* Invariants: register_P = 0x1 register_I = 0x10 */;
      /* opADDimm_A_AA (21) */
      register_A = (flag_C = ((cmp_old = acc_a0 = register_A) + (cmp_new = 0x1))) & 0xFFF; /* add values, save carry */

      /* opLDJimm_A_A (46) */
      register_J = 0x0386;
      /* opJNC_A_AA (5d) */
      if (!(flag_C & CARRYBIT)) {
        register_PC = 0x0386;
        break;
      };
      /* opLDAimm_A_AA (03) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0x0300;
      /* opADDimmX_A_AA (20) */
      cmp_old = register_A; cmp_new = 0xcb;
      register_A = flag_C = acc_a0 = 0x03cb;
      /* opSTAdir_A_A (d6) */
      ram[register_I = 0x16] = 0x3cb; /* store acc to RAM */
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opADDimmX_A_AA (20) */
      cmp_old = register_A; cmp_new = 0xff;
      register_A = flag_C = acc_a0 = 0x00ff;
      /* opSTAdir_A_A (d8) */
      ram[register_I = 0x18] = 0x0ff; /* store acc to RAM */

    case 0x0392:

      /* Invariants: register_P = 0x1 */;
      /* opLDAdir_A_AA (a0) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = 0x10]; /* set I register */

      /* opNOP_A_B (57) */
      /* opLDAdir_B_AA (a1) */
      flag_C = acc_a0 = register_A;
      cmp_old = register_B; /* store old acc */
      register_B = cmp_new = ram[register_I = (register_P << 4) + 0x01]; /* new acc value */
      /* opVIN_A_A (f0) */

      FromX = register_A & 0xFFF; /* regA goes to x-coord */
      FromY = register_B & 0xFFF; /* regB goes to y-coord */

      /* opLDAdir_A_AA (a6) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = 0x16]; /* set I register */

      /* opADDimm_A_AA (22) */
      register_A = (flag_C = ((cmp_old = acc_a0 = register_A) + (cmp_new = 0x2))) & 0xFFF; /* add values, save carry */

      /* opSTAirg_A_A (e6) */
      ram[0x16] = register_A; /* store acc */
      /* opXLT_A_AA (e2) */
      cmp_old = register_A; register_A = cmp_new = rom[register_A]; /* new acc value */
      /* opNOP_A_A (5f) */
      /* opCMPdir_A_AA (b8) */
      cmp_old = acc_a0 = register_A; /* backup old acc */
      flag_C = (((cmp_new = ram[register_I = (register_P << 4) + 0x8]) ^ 0xFFF) + 1 + register_A);
      /* opLDJimm_A_A (46) */
      register_J = 0x03e6;
      /* opJEQ_A_AA (5c) */
      if (cmp_new == cmp_old) {
        register_PC = 0x03e6;
        break;
      };
      /* opLSLe_A_AA (ec) */
      cmp_new = 0x0CEC;
      cmp_old = acc_a0 = register_A;
      flag_C = (0x0CEC + register_A);
      register_A = (register_A << 1) & 0x0FFF;
      /* opLSLe_A_AA (ec) */
      cmp_new = 0x0CEC;
      cmp_old = acc_a0 = register_A;
      flag_C = (0x0CEC + register_A);
      register_A = (register_A << 1) & 0x0FFF;
      /* opSUBdir_A_AA (70) */
      cmp_old = acc_a0 = register_A;
      register_A = (flag_C = (register_A + ((cmp_new = ram[register_I = (register_P << 4) + 0x0]) ^ 0xFFF) + 1)) & 0xFFF; /* set regI addr */
      /* opSTAdir_A_A (d3) */
      ram[register_I = (register_P << 4) + 0x3] = register_A; /* store acc to RAM */
      /* opLDAdir_A_AA (a6) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = (register_P << 4) + 0x06]; /* set I register */

      /* opADDimm_A_AA (21) */
      register_A = (flag_C = ((cmp_old = acc_a0 = register_A) + (cmp_new = 0x1))) & 0xFFF; /* add values, save carry */

      /* opXLT_A_AA (e2) */
      cmp_old = register_A; register_A = cmp_new = rom[register_A]; /* new acc value */
      /* opNOP_A_A (5f) */
      /* opLSLe_A_AA (ec) */
      cmp_new = 0x0CEC;
      cmp_old = acc_a0 = register_A;
      flag_C = (0x0CEC + register_A);
      register_A = (register_A << 1) & 0x0FFF;
      /* opLSLe_A_AA (ec) */
      cmp_new = 0x0CEC;
      cmp_old = acc_a0 = register_A;
      flag_C = (0x0CEC + register_A);
      register_A = (register_A << 1) & 0x0FFF;
      /* opSUBdir_A_AA (71) */
      cmp_old = acc_a0 = register_A;
      register_A = (flag_C = (register_A + ((cmp_new = ram[register_I = (register_P << 4) + 0x1]) ^ 0xFFF) + 1)) & 0xFFF; /* set regI addr */
      /* opSTAdir_A_A (d4) */
      ram[register_I = (register_P << 4) + 0x4] = register_A; /* store acc to RAM */
      /* opLDAdir_A_AA (a3) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = (register_P << 4) + 0x03]; /* set I register */

      /* opNOP_A_B (57) */
      /* opLDAdir_B_AA (a4) */
      flag_C = acc_a0 = register_A;
      cmp_old = register_B; /* store old acc */
      register_B = cmp_new = ram[register_I = (register_P << 4) + 0x04]; /* new acc value */
      /* opLLT_A_AA (e4) */
      { CINEBYTE temp_byte = 0;
        for (;;) {
          if (   (((register_A >> 8) & 0x0A) && (((register_A >> 8) & 0x0A) ^ 0x0A))
                 ||   (((register_B >> 8) & 0x0A) && (((register_B >> 8) & 0x0A) ^ 0x0A))  ) break;
          register_A <<= 1; register_B <<= 1;
          if (!(++temp_byte)) break /* This may not be correct */;
        }
        vgShiftLength = temp_byte & 0xfff; register_A &= 0x0FFF; register_B &= 0x0FFF;
      }
      /* opADDdir_A_AA (60) */
      register_A = (flag_C = ((cmp_old = acc_a0 = register_A) + (cmp_new = ram[register_I = (register_P << 4) + 0x0]))) & 0xFFF; /* do acc operation */
      /* opNOP_A_B (57) */
      /* opADDdir_B_AA (61) */
      acc_a0 = register_A; /* store old acc value */
      register_B = (flag_C = ((cmp_old = register_B) + (cmp_new = ram[register_I = (register_P << 4) + 0x01]))) & 0xFFF; /* do acc operation */
      /* opVDR_A_A (e0) */
      {
        /* set ending points and draw the vector, or buffer for a later draw. */
        int ToX = register_A & 0xFFF;
        int ToY = register_B & 0xFFF;

        /* Sign extend from 20 bit CCPU to 32bit target machine */
        FromX = SEX(FromX);
        ToX = SEX(ToX);
        FromY = SEX(FromY);
        ToY = SEX(ToY);

        /* figure out the vector */
        ToX -= FromX;
        ToX = ((signed short int)(((signed short int)ToX) >> (signed short int)vgShiftLength)) /* SAR */;
        ToX += FromX;

        ToY -= FromY;
        ToY = ((signed short int)(((signed short int)ToY) >> (signed short int)vgShiftLength)) /* SAR */;
        ToY += FromY;

        /* render the line */
#ifndef DUALCPU
        CinemaVectorData (FromX, FromY, ToX, ToY, vgColour);
#endif

      }
      /* opLDAdir_A_AA (a3) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = (register_P << 4) + 0x03]; /* set I register */

      /* opADDdir_A_AA (60) */
      register_A = (flag_C = ((cmp_old = acc_a0 = register_A) + (cmp_new = ram[register_I = (register_P << 4) + 0x0]))) & 0xFFF; /* do acc operation */
      /* opSTAirg_A_A (e6) */
      ram[register_I] = register_A; /* store acc */
      /* opLDAdir_A_AA (a4) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = (register_P << 4) + 0x04]; /* set I register */

      /* opADDdir_A_AA (61) */
      register_A = (flag_C = ((cmp_old = acc_a0 = register_A) + (cmp_new = ram[register_I = (register_P << 4) + 0x1]))) & 0xFFF; /* do acc operation */
      /* opSTAirg_A_A (e6) */
      ram[register_I] = register_A; /* store acc */
      /* opLDPimm_A_A (88) */
      register_P = 0x8; /* set page register */
      /* opCMPdir_A_AA (b3) */
      cmp_old = acc_a0 = register_A; /* backup old acc */
      flag_C = (((cmp_new = ram[register_I = 0x83]) ^ 0xFFF) + 1 + register_A);
      /* opLDJimm_A_A (4d) */
      register_J = 0x03bd;
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opNOP_A_B (57) */
      /* opLDAirg_B_AA (ea) */
      flag_C = acc_a0 = register_A;
      cmp_old = register_B;
      register_B = cmp_new = ram[register_I];

      /* opASRDe_A_AA (ee) */
      cmp_new = 0x0EEE; cmp_old = acc_a0 = register_A; flag_C = (0x0EEE + register_A);
      register_A = (register_A >> 1) | ((register_B & 1) << 11);
      register_B = (register_B >> 1) | (register_B & 0x800);
      /* opASRDe_A_AA (ee) */
      cmp_new = 0x0EEE; cmp_old = acc_a0 = register_A; flag_C = (0x0EEE + register_A);
      register_A = (register_A >> 1) | ((register_B & 1) << 11);
      register_B = (register_B >> 1) | (register_B & 0x800);
      /* opAWDirg_A_AA (e7) */
      cmp_old = acc_a0 = register_A;
      register_A = (flag_C = (register_A + (cmp_new = ram[0x83]))) & 0xFFF;
      set_watchdog();
      /* opLSLe_A_AA (ec) */
      cmp_new = 0x0CEC;
      cmp_old = acc_a0 = register_A;
      flag_C = (0x0CEC + register_A);
      register_A = (register_A << 1) & 0x0FFF;
      /* opAWDirg_A_AA (e7) */
      cmp_old = acc_a0 = register_A;
      register_A = (flag_C = (register_A + (cmp_new = ram[0x83]))) & 0xFFF;
      set_watchdog();
      /* opLSRe_A_AA (eb) */
      cmp_new = 0x0BEB; cmp_old = acc_a0 = register_A; flag_C = (0x0BEB + register_A);
      register_A >>= 1;
      /* opSTAirg_A_A (e6) */
      ram[0x83] = register_A; /* store acc */
      /* opOUTbi_A_A (96) */
      vgColour = register_A & 0x01 ? 0x0f : 0x07;
      /* opJDR_A_A (5a) */
      /* opLDPimm_A_A (81) */
      register_P = 0x1; /* set page register */
      /* opLDJimm_A_A (42) */
      register_J = 0x0392;
      /* opJMP_A_A (58) */
      {
        register_PC = 0x0392;
        break;
      };

    case 0x03e6:

      /* Invariants: register_P = 0x1 register_I = 0x18 register_A = 0xff */;
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opADDimmX_A_AA (20) */
      cmp_old = register_A; cmp_new = 0x19;
      register_A = flag_C = acc_a0 = 0x0019;
      /* opOUTbi_A_A (93) */
      /* opOUTsnd_A (93) */
      set_sound_data(1);
      /* opLSRe_A_AA (eb) */
      cmp_new = 0x0BEB; cmp_old = acc_a0 = register_A; flag_C = (0x0BEB + register_A);
      register_A >>= 1;
      /* opOUTbi_A_A (90) */
      /* opOUTsnd_A (90) */
      set_sound_addr_A(register_A & 1);
      /* opLSRe_A_AA (eb) */
      cmp_new = 0x0BEB; cmp_old = acc_a0 = register_A; flag_C = (0x0BEB + register_A);
      register_A >>= 1;
      /* opOUTbi_A_A (91) */
      /* opOUTsnd_A (91) */
      set_sound_addr_B(register_A & 1);
      /* opLSRe_A_AA (eb) */
      cmp_new = 0x0BEB; cmp_old = acc_a0 = register_A; flag_C = (0x0BEB + register_A);
      register_A >>= 1;
      /* opOUTbi_A_A (92) */
      /* opOUTsnd_A (92) */
      set_sound_addr_C(register_A & 1);
      /* opLSRe_A_AA (eb) */
      cmp_new = 0x0BEB; cmp_old = acc_a0 = register_A; flag_C = (0x0BEB + register_A);
      register_A >>= 1;
      /* opOUTbi_A_A (94) */
      /* opOUTsnd_A (94) */
      if (register_A & 1) strobe_sound_on(); else strobe_sound_off();
      /* opLSRe_A_AA (eb) */
      cmp_new = 0x0BEB; cmp_old = acc_a0 = register_A; flag_C = (0x0BEB + register_A);
      register_A >>= 1;
      /* opOUTbi_A_A (94) */
      /* opOUTsnd_A (94) */
      if (register_A & 1) strobe_sound_on(); else strobe_sound_off();
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opADDimmX_A_AA (20) */
      cmp_old = register_A; cmp_new = 0x16;
      register_A = flag_C = acc_a0 = 0x0016;
      /* opOUTbi_A_A (93) */
      /* opOUTsnd_A (93) */
      set_sound_data(0);
      /* opLSRe_A_AA (eb) */
      cmp_new = 0x0BEB; cmp_old = acc_a0 = register_A; flag_C = (0x0BEB + register_A);
      register_A >>= 1;
      /* opOUTbi_A_A (90) */
      /* opOUTsnd_A (90) */
      set_sound_addr_A(register_A & 1);
      /* opLSRe_A_AA (eb) */
      cmp_new = 0x0BEB; cmp_old = acc_a0 = register_A; flag_C = (0x0BEB + register_A);
      register_A >>= 1;
      /* opOUTbi_A_A (91) */
      /* opOUTsnd_A (91) */
      set_sound_addr_B(register_A & 1);
      /* opLSRe_A_AA (eb) */
      cmp_new = 0x0BEB; cmp_old = acc_a0 = register_A; flag_C = (0x0BEB + register_A);
      register_A >>= 1;
      /* opOUTbi_A_A (92) */
      /* opOUTsnd_A (92) */
      set_sound_addr_C(register_A & 1);
      /* opLSRe_A_AA (eb) */
      cmp_new = 0x0BEB; cmp_old = acc_a0 = register_A; flag_C = (0x0BEB + register_A);
      register_A >>= 1;
      /* opOUTbi_A_A (94) */
      /* opOUTsnd_A (94) */
      if (register_A & 1) strobe_sound_on(); else strobe_sound_off();
      /* opLSRe_A_AA (eb) */
      cmp_new = 0x0BEB; cmp_old = acc_a0 = register_A; flag_C = (0x0BEB + register_A);
      register_A >>= 1;
      /* opOUTbi_A_A (94) */
      /* opOUTsnd_A (94) */
      if (register_A & 1) strobe_sound_on(); else strobe_sound_off();
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opLDPimm_A_A (86) */
      register_P = 0x6; /* set page register */
      /* opCMPdir_A_AA (b9) */
      cmp_old = acc_a0 = register_A; /* backup old acc */
      flag_C = (((cmp_new = ram[register_I = 0x69]) ^ 0xFFF) + 1 + register_A);
      /* opLDJimm_A_A (4e) */
      register_J = 0x045e;
      /* opJEQ_A_AA (5c) */
      if (cmp_new == cmp_old) {
        register_PC = 0x045e;
        break;
      };
      /* opLDPimm_A_A (83) */
      register_P = 0x3; /* set page register */
      /* opCMPdir_A_AA (b0) */
      cmp_old = acc_a0 = register_A; /* backup old acc */
      flag_C = (((cmp_new = ram[register_I = 0x30]) ^ 0xFFF) + 1 + register_A);
      /* opLDJimm_A_A (44) */
      register_J = 0x0424;
      /* opJEQ_A_AA (5c) */
      if (cmp_new == cmp_old) {
        register_PC = 0x0424;
        break;
      };
      /* opCMPdir_A_AA (bd) */
      cmp_old = acc_a0 = register_A; /* backup old acc */
      flag_C = (((cmp_new = ram[register_I = 0x3d]) ^ 0xFFF) + 1 + register_A);
      /* opJNC_A_AA (5d) */
      if (!(flag_C & CARRYBIT)) {
        register_PC = 0x0424;
        break;
      };
      /* opLDAimm_A_AA (02) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0x0200;
      /* opCMPdir_A_AA (b3) */
      cmp_old = acc_a0 = register_A; /* backup old acc */
      flag_C = (((cmp_new = ram[register_I = 0x33]) ^ 0xFFF) + 1 + register_A);
      /* opJNC_A_AA (5d) */
      if (!(flag_C & CARRYBIT)) {
        register_PC = 0x0424;
        break;
      };
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opADDimmX_A_AA (20) */
      cmp_old = register_A; cmp_new = 0x4d;
      register_A = flag_C = acc_a0 = 0x004d;
      /* opSTAdir_A_A (dd) */
      ram[register_I = 0x3d] = 0x04d; /* store acc to RAM */
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opADDimmX_A_AA (20) */
      cmp_old = register_A; cmp_new = 0x17;
      register_A = flag_C = acc_a0 = 0x0017;
      /* opOUTbi_A_A (93) */
      /* opOUTsnd_A (93) */
      set_sound_data(1);
      /* opLSRe_A_AA (eb) */
      cmp_new = 0x0BEB; cmp_old = acc_a0 = register_A; flag_C = (0x0BEB + register_A);
      register_A >>= 1;
      /* opOUTbi_A_A (90) */
      /* opOUTsnd_A (90) */
      set_sound_addr_A(register_A & 1);
      /* opLSRe_A_AA (eb) */
      cmp_new = 0x0BEB; cmp_old = acc_a0 = register_A; flag_C = (0x0BEB + register_A);
      register_A >>= 1;
      /* opOUTbi_A_A (91) */
      /* opOUTsnd_A (91) */
      set_sound_addr_B(register_A & 1);
      /* opLSRe_A_AA (eb) */
      cmp_new = 0x0BEB; cmp_old = acc_a0 = register_A; flag_C = (0x0BEB + register_A);
      register_A >>= 1;
      /* opOUTbi_A_A (92) */
      /* opOUTsnd_A (92) */
      set_sound_addr_C(register_A & 1);
      /* opLSRe_A_AA (eb) */
      cmp_new = 0x0BEB; cmp_old = acc_a0 = register_A; flag_C = (0x0BEB + register_A);
      register_A >>= 1;
      /* opOUTbi_A_A (94) */
      /* opOUTsnd_A (94) */
      if (register_A & 1) strobe_sound_on(); else strobe_sound_off();
      /* opLSRe_A_AA (eb) */
      cmp_new = 0x0BEB; cmp_old = acc_a0 = register_A; flag_C = (0x0BEB + register_A);
      register_A >>= 1;
      /* opOUTbi_A_A (94) */
      /* opOUTsnd_A (94) */
      if (register_A & 1) strobe_sound_on(); else strobe_sound_off();
      register_PC = 0x424; break;

    default:
      /* Jumping to any illegal address or end of eprom will come here */
      /* we ought to reinitialise or something */
      ;
  }
}

// ops0400.c

void cineExecute0400(void)
{
  switch (register_PC)
  {
    case 0x0424:

      /* Invariants: register_P = 0x3 */;
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opLDPimm_A_A (84) */
      register_P = 0x4; /* set page register */
      /* opCMPdir_A_AA (b0) */
      cmp_old = acc_a0 = register_A; /* backup old acc */
      flag_C = (((cmp_new = ram[register_I = 0x40]) ^ 0xFFF) + 1 + register_A);
      /* opLDJimm_A_A (41) */
      register_J = 0x0441;
      /* opJEQ_A_AA (5c) */
      if (cmp_new == cmp_old) {
        register_PC = 0x0441;
        break;
      };
      /* opCMPdir_A_AA (bd) */
      cmp_old = acc_a0 = register_A; /* backup old acc */
      flag_C = (((cmp_new = ram[register_I = 0x4d]) ^ 0xFFF) + 1 + register_A);
      /* opJNC_A_AA (5d) */
      if (!(flag_C & CARRYBIT)) {
        register_PC = 0x0441;
        break;
      };
      /* opLDAimm_A_AA (02) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0x0200;
      /* opCMPdir_A_AA (b3) */
      cmp_old = acc_a0 = register_A; /* backup old acc */
      flag_C = (((cmp_new = ram[register_I = 0x43]) ^ 0xFFF) + 1 + register_A);
      /* opJNC_A_AA (5d) */
      if (!(flag_C & CARRYBIT)) {
        register_PC = 0x0441;
        break;
      };
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opADDimmX_A_AA (20) */
      cmp_old = register_A; cmp_new = 0x4d;
      register_A = flag_C = acc_a0 = 0x004d;
      /* opSTAdir_A_A (dd) */
      ram[register_I = 0x4d] = 0x04d; /* store acc to RAM */
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opADDimmX_A_AA (20) */
      cmp_old = register_A; cmp_new = 0x17;
      register_A = flag_C = acc_a0 = 0x0017;
      /* opOUTbi_A_A (93) */
      /* opOUTsnd_A (93) */
      set_sound_data(1);
      /* opLSRe_A_AA (eb) */
      cmp_new = 0x0BEB; cmp_old = acc_a0 = register_A; flag_C = (0x0BEB + register_A);
      register_A >>= 1;
      /* opOUTbi_A_A (90) */
      /* opOUTsnd_A (90) */
      set_sound_addr_A(register_A & 1);
      /* opLSRe_A_AA (eb) */
      cmp_new = 0x0BEB; cmp_old = acc_a0 = register_A; flag_C = (0x0BEB + register_A);
      register_A >>= 1;
      /* opOUTbi_A_A (91) */
      /* opOUTsnd_A (91) */
      set_sound_addr_B(register_A & 1);
      /* opLSRe_A_AA (eb) */
      cmp_new = 0x0BEB; cmp_old = acc_a0 = register_A; flag_C = (0x0BEB + register_A);
      register_A >>= 1;
      /* opOUTbi_A_A (92) */
      /* opOUTsnd_A (92) */
      set_sound_addr_C(register_A & 1);
      /* opLSRe_A_AA (eb) */
      cmp_new = 0x0BEB; cmp_old = acc_a0 = register_A; flag_C = (0x0BEB + register_A);
      register_A >>= 1;
      /* opOUTbi_A_A (94) */
      /* opOUTsnd_A (94) */
      if (register_A & 1) strobe_sound_on(); else strobe_sound_off();
      /* opLSRe_A_AA (eb) */
      cmp_new = 0x0BEB; cmp_old = acc_a0 = register_A; flag_C = (0x0BEB + register_A);
      register_A >>= 1;
      /* opOUTbi_A_A (94) */
      /* opOUTsnd_A (94) */
      if (register_A & 1) strobe_sound_on(); else strobe_sound_off();

    case 0x0441:

      /* Invariants: register_P = 0x4 register_I = 0x43 register_A = 0x200 */;
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opLDPimm_A_A (85) */
      register_P = 0x5; /* set page register */
      /* opCMPdir_A_AA (b0) */
      cmp_old = acc_a0 = register_A; /* backup old acc */
      flag_C = (((cmp_new = ram[register_I = 0x50]) ^ 0xFFF) + 1 + register_A);
      /* opLDJimm_A_A (4e) */
      register_J = 0x045e;
      /* opJEQ_A_AA (5c) */
      if (cmp_new == cmp_old) {
        register_PC = 0x045e;
        break;
      };
      /* opCMPdir_A_AA (bd) */
      cmp_old = acc_a0 = register_A; /* backup old acc */
      flag_C = (((cmp_new = ram[register_I = 0x5d]) ^ 0xFFF) + 1 + register_A);
      /* opJNC_A_AA (5d) */
      if (!(flag_C & CARRYBIT)) {
        register_PC = 0x045e;
        break;
      };
      /* opLDAimm_A_AA (02) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0x0200;
      /* opCMPdir_A_AA (b3) */
      cmp_old = acc_a0 = register_A; /* backup old acc */
      flag_C = (((cmp_new = ram[register_I = 0x53]) ^ 0xFFF) + 1 + register_A);
      /* opJNC_A_AA (5d) */
      if (!(flag_C & CARRYBIT)) {
        register_PC = 0x045e;
        break;
      };
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opADDimmX_A_AA (20) */
      cmp_old = register_A; cmp_new = 0x4d;
      register_A = flag_C = acc_a0 = 0x004d;
      /* opSTAdir_A_A (dd) */
      ram[register_I = 0x5d] = 0x04d; /* store acc to RAM */
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opADDimmX_A_AA (20) */
      cmp_old = register_A; cmp_new = 0x17;
      register_A = flag_C = acc_a0 = 0x0017;
      /* opOUTbi_A_A (93) */
      /* opOUTsnd_A (93) */
      set_sound_data(1);
      /* opLSRe_A_AA (eb) */
      cmp_new = 0x0BEB; cmp_old = acc_a0 = register_A; flag_C = (0x0BEB + register_A);
      register_A >>= 1;
      /* opOUTbi_A_A (90) */
      /* opOUTsnd_A (90) */
      set_sound_addr_A(register_A & 1);
      /* opLSRe_A_AA (eb) */
      cmp_new = 0x0BEB; cmp_old = acc_a0 = register_A; flag_C = (0x0BEB + register_A);
      register_A >>= 1;
      /* opOUTbi_A_A (91) */
      /* opOUTsnd_A (91) */
      set_sound_addr_B(register_A & 1);
      /* opLSRe_A_AA (eb) */
      cmp_new = 0x0BEB; cmp_old = acc_a0 = register_A; flag_C = (0x0BEB + register_A);
      register_A >>= 1;
      /* opOUTbi_A_A (92) */
      /* opOUTsnd_A (92) */
      set_sound_addr_C(register_A & 1);
      /* opLSRe_A_AA (eb) */
      cmp_new = 0x0BEB; cmp_old = acc_a0 = register_A; flag_C = (0x0BEB + register_A);
      register_A >>= 1;
      /* opOUTbi_A_A (94) */
      /* opOUTsnd_A (94) */
      if (register_A & 1) strobe_sound_on(); else strobe_sound_off();
      /* opLSRe_A_AA (eb) */
      cmp_new = 0x0BEB; cmp_old = acc_a0 = register_A; flag_C = (0x0BEB + register_A);
      register_A >>= 1;
      /* opOUTbi_A_A (94) */
      /* opOUTsnd_A (94) */
      if (register_A & 1) strobe_sound_on(); else strobe_sound_off();

    case 0x045e:

      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opLDPimm_A_A (86) */
      register_P = 0x6; /* set page register */
      /* opCMPdir_A_AA (b9) */
      cmp_old = acc_a0 = register_A; /* backup old acc */
      flag_C = (((cmp_new = ram[register_I = 0x69]) ^ 0xFFF) + 1 + register_A);
      /* opLDJimm_A_A (46) */
      register_J = 0x0586;
      /* opJEQ_A_AA (5c) */
      if (cmp_new == cmp_old) {
        register_PC = 0x0586;
        break;
      };
      /* opINP_A_AA (1e) */
      cmp_old = flag_C = acc_a0 = register_A;
#ifdef RAWIO
      register_A = cmp_new = get_io_bit(0xe);
#else
      register_A = cmp_new = get_io_bit(0xe);
#endif
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opLDJimm_A_A (47) */
      register_J = 0x04d7;
      /* opJEQ_A_AA (5c) */
      if (cmp_new == cmp_old) {
        register_PC = 0x04d7;
        break;
      };
      /* opLDPimm_A_A (82) */
      register_P = 0x2; /* set page register */
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opOUTbi_A_A (97) */
      /* opOUTsnd_A (97) */
      put_io_bit(/*bitno*/0x7, /*set or clr*/0x0);
      /* opLDJimm_A_A (4e) */
      register_J = 0x046e;
      /* opJDR_A_A (5a) */
      /* opLDAdir_A_AA (aa) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = 0x2a]; /* set I register */

      /* opSTAdir_A_A (d8) */
      ram[register_I = 0x28] = register_A; /* store acc to RAM */
      /* opLDAimm_A_AA (04) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0x0400;
      /* opADDimmX_A_AA (20) */
      cmp_old = register_A; cmp_new = 0x78;
      register_A = flag_C = acc_a0 = 0x0478;
      /* opSTAdir_A_A (dd) */
      ram[register_I = 0x2d] = 0x478; /* store acc to RAM */
      /* opLDJimm_A_A (45) */
      register_J = 0x0495;
      /* opJMP_A_A (58) */
      {
        register_PC = 0x0495;
        break;
      };

    case 0x0495:

      /* opLDAimm_A_AA (08) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0x0800;
      /* opLDPimm_A_A (82) */
      register_P = 0x2; /* set page register */
      /* opSTAdir_A_A (df) */
      ram[register_I = 0x2f] = 0x800; /* store acc to RAM */
      /* opLSRe_A_AA (eb) */
      cmp_new = 0x0BEB; cmp_old = acc_a0 = register_A; flag_C = (0x0BEB + register_A);
      register_A >>= 1;
      /* opSTAdir_A_A (de) */
      ram[register_I = 0x2e] = register_A; /* store acc to RAM */
      /* opLDJimm_A_A (4c) */
      register_J = 0x049c;
    /* opJDR_A_A (5a) */

    case 0x049d:

      /* opLDAimm_A_AA (08) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0x0800;
      /* opADDdir_A_AA (6f) */
      register_A = (flag_C = ((cmp_old = acc_a0 = register_A) + (cmp_new = ram[register_I = (register_P << 4) + 0xf]))) & 0xFFF; /* do acc operation */
      /* opVIN_A_A (f0) */

      FromX = register_A & 0xFFF; /* regA goes to x-coord */
      FromY = register_B & 0xFFF; /* regB goes to y-coord */

      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opNOP_A_B (57) */
      /* opLDAimm_B_AA (01) */
      flag_C = acc_a0 = register_A;
      cmp_old = register_B; /* step back cmp flag */
      register_B = cmp_new = 0x0100;
      /* opLLT_A_AA (e4) */
      { CINEBYTE temp_byte = 0;
        for (;;) {
          if (   (((register_A >> 8) & 0x0A) && (((register_A >> 8) & 0x0A) ^ 0x0A))
                 ||   (((register_B >> 8) & 0x0A) && (((register_B >> 8) & 0x0A) ^ 0x0A))  ) break;
          register_A <<= 1; register_B <<= 1;
          if (!(++temp_byte)) break /* This may not be correct */;
        }
        vgShiftLength = temp_byte & 0xfff; register_A &= 0x0FFF; register_B &= 0x0FFF;
      }
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opSUBimmX_A_AA (30) */
      cmp_old = acc_a0 = register_A; /* back up regA */
      register_A = (flag_C = (register_A + (((cmp_new = 0x11) ^ 0xFFF) + 1))) & 0xFFF; /* add */

    case 0x04a7:

      /* opADDimm_A_AA (21) */
      register_A = (flag_C = ((cmp_old = acc_a0 = register_A) + (cmp_new = 0x1))) & 0xFFF; /* add values, save carry */

      /* opLDJimm_A_A (47) */
      register_J = 0x04a7;
      /* opJNC_A_AA (5d) */
      if (!(flag_C & CARRYBIT)) {
        register_PC = 0x04a7;
        break;
      };
      /* opLDAdir_A_AA (af) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = (register_P << 4) + 0x0f]; /* set I register */

      /* opLDJimm_A_A (43) */
      register_J = 0x04b3;
      /* opJEI_A_A (59) */
      /* opADDdir_A_AA (6e) */
      register_A = (flag_C = ((cmp_old = acc_a0 = register_A) + (cmp_new = ram[register_I = (register_P << 4) + 0xe]))) & 0xFFF; /* do acc operation */
      /* opLDJimm_A_A (44) */
      register_J = 0x04b4;
      /* opJMP_A_A (58) */
      {
        register_PC = 0x04b4;
        break;
      };

    case 0x04b4:

      /* opSTAdir_A_A (df) */
      ram[register_I = (register_P << 4) + 0xf] = register_A; /* store acc to RAM */
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opADDimm_A_AA (22) */
      cmp_new = 0x2; cmp_old = acc_a0 = register_A; register_A = flag_C = 0x0002;

      /* opCMPdir_A_AA (be) */
      cmp_old = acc_a0 = register_A; /* backup old acc */
      flag_C = (((cmp_new = ram[register_I = (register_P << 4) + 0xe]) ^ 0xFFF) + 1 + register_A);
      /* opLDJimm_A_A (41) */
      register_J = 0x04c1;
      /* opJEQ_A_AA (5c) */
      if (cmp_new == cmp_old) {
        register_PC = 0x04c1;
        break;
      };
      /* opLDAirg_A_AA (ea) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = ram[register_I];
      /* opLSRe_A_AA (eb) */
      cmp_new = 0x0BEB; cmp_old = acc_a0 = register_A; flag_C = (0x0BEB + register_A);
      register_A >>= 1;
      /* opSTAirg_A_A (e6) */
      ram[register_I] = register_A; /* store acc */
      /* opLDJimm_A_A (4d) */
      register_J = 0x049d;
      /* opJMP_A_A (58) */
      {
        register_PC = 0x049d;
        break;
      };

    case 0x04c1:

      /* opLDAdir_A_AA (af) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = (register_P << 4) + 0x0f]; /* set I register */

      /* opSUBdir_A_AA (78) */
      cmp_old = acc_a0 = register_A;
      register_A = (flag_C = (register_A + ((cmp_new = ram[register_I = (register_P << 4) + 0x8]) ^ 0xFFF) + 1)) & 0xFFF; /* set regI addr */
      /* opSTAdir_A_A (de) */
      ram[register_I = (register_P << 4) + 0xe] = register_A; /* store acc to RAM */
      /* opLDAimm_A_AA (08) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0x0800;
      /* opCMPdir_A_AA (be) */
      cmp_old = acc_a0 = register_A; /* backup old acc */
      flag_C = (((cmp_new = ram[register_I = (register_P << 4) + 0xe]) ^ 0xFFF) + 1 + register_A);
      /* opLDJimm_A_A (4c) */
      register_J = 0x04cc;
      /* opJLT_A_A (5b) */
      if (cmp_new < cmp_old) {
        register_PC = 0x04cc;
        break;
      };
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opSUBdir_A_AA (7e) */
      cmp_old = acc_a0 = register_A;
      register_A = (flag_C = (register_A + ((cmp_new = ram[register_I = (register_P << 4) + 0xe]) ^ 0xFFF) + 1)) & 0xFFF; /* set regI addr */
      /* opSTAirg_A_A (e6) */
      ram[register_I] = register_A; /* store acc */

    case 0x04cc:

      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opADDimm_A_AA (27) */
      cmp_new = 0x7; cmp_old = acc_a0 = register_A; register_A = flag_C = 0x0007;

      /* opCMPdir_A_AA (be) */
      cmp_old = acc_a0 = register_A; /* backup old acc */
      flag_C = (((cmp_new = ram[register_I = (register_P << 4) + 0xe]) ^ 0xFFF) + 1 + register_A);
      /* opLDJimm_A_A (44) */
      register_J = 0x04d4;
      /* opJLT_A_A (5b) */
      if (cmp_new < cmp_old) {
        register_PC = 0x04d4;
        break;
      };
      /* opLDAdir_A_AA (af) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = (register_P << 4) + 0x0f]; /* set I register */

      /* opSTAdir_A_A (d8) */
      ram[register_I = (register_P << 4) + 0x8] = register_A; /* store acc to RAM */

    case 0x04d4:

      /* opCMPdir_A_AA (bd) */
      cmp_old = acc_a0 = register_A; /* backup old acc */
      flag_C = (((cmp_new = ram[register_I = (register_P << 4) + 0xd]) ^ 0xFFF) + 1 + register_A);
      /* opLDJirg_A_A (e1) */
      /* load J reg from value at last dir addr */
      register_J = ram[register_I];
      /* opJMP_A_A (58) */
      {
        register_PC = register_J;
        break;
      };

    case 0x04d7:

      /* Invariants: register_P = 0x6 register_I = 0x69 */;
      /* opINP_A_AA (11) */
      cmp_old = flag_C = acc_a0 = register_A;
#ifdef RAWIO
      register_A = cmp_new = get_io_bit(0x1);
#else
      register_A = cmp_new = get_io_moveright();
#endif
      /* opLDPimm_A_A (82) */
      register_P = 0x2; /* set page register */
      /* opSTAdir_A_A (df) */
      ram[register_I = 0x2f] = register_A; /* store acc to RAM */
      /* opINP_A_AA (12) */
      cmp_old = flag_C = acc_a0 = register_A;
#ifdef RAWIO
      register_A = cmp_new = get_io_bit(0x2);
#else
      register_A = cmp_new = get_io_moveleft();
#endif
      /* opSUBirg_A_AA (e8) */
      register_A = (flag_C = ((cmp_old = acc_a0 = register_A) + ((cmp_new = ram[register_I]) ^ 0xFFF) + 1)) & 0xFFF; /* ones compliment */
      /* opLSLe_A_AA (ec) */
      cmp_new = 0x0CEC;
      cmp_old = acc_a0 = register_A;
      flag_C = (0x0CEC + register_A);
      register_A = (register_A << 1) & 0x0FFF;
      /* opLSLe_A_AA (ec) */
      cmp_new = 0x0CEC;
      cmp_old = acc_a0 = register_A;
      flag_C = (0x0CEC + register_A);
      register_A = (register_A << 1) & 0x0FFF;
      /* opLSLe_A_AA (ec) */
      cmp_new = 0x0CEC;
      cmp_old = acc_a0 = register_A;
      flag_C = (0x0CEC + register_A);
      register_A = (register_A << 1) & 0x0FFF;
      /* opLSLe_A_AA (ec) */
      cmp_new = 0x0CEC;
      cmp_old = acc_a0 = register_A;
      flag_C = (0x0CEC + register_A);
      register_A = (register_A << 1) & 0x0FFF;
      /* opADDdir_A_AA (6a) */
      register_A = (flag_C = ((cmp_old = acc_a0 = register_A) + (cmp_new = ram[register_I = 0x2a]))) & 0xFFF; /* do acc operation */
      /* opSTAirg_A_A (e6) */
      ram[0x2a] = register_A; /* store acc */
      /* opLDAimm_A_AA (02) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0x0200;
      /* opCMPdir_A_AA (ba) */
      cmp_old = acc_a0 = register_A; /* backup old acc */
      flag_C = (((cmp_new = ram[register_I = 0x2a]) ^ 0xFFF) + 1 + register_A);
      /* opLDJimm_A_A (46) */
      register_J = 0x04f6;
      /* opJLT_A_A (5b) */
      if (cmp_new < cmp_old) {
        register_PC = 0x04f6;
        break;
      };
      /* opLDAimm_A_AA (0e) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0x0e00;
      /* opCMPdir_A_AA (ba) */
      cmp_old = acc_a0 = register_A; /* backup old acc */
      flag_C = (((cmp_new = ram[register_I = 0x2a]) ^ 0xFFF) + 1 + register_A);
      /* opJNC_A_AA (5d) */
      if (!(flag_C & CARRYBIT)) {
        register_PC = 0x04f6;
        break;
      };
      /* opLDAimm_A_AA (08) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0x0800;
      /* opCMPdir_A_AA (ba) */
      cmp_old = acc_a0 = register_A; /* backup old acc */
      flag_C = (((cmp_new = ram[register_I = 0x2a]) ^ 0xFFF) + 1 + register_A);
      /* opLDJimm_A_A (44) */
      register_J = 0x04f4;
      /* opJLT_A_A (5b) */
      if (cmp_new < cmp_old) {
        register_PC = 0x04f4;
        break;
      };
      /* opLDAimm_A_AA (0e) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0x0e00;
      /* opSTAirg_A_A (e6) */
      ram[0x2a] = 0xe00; /* store acc */
      /* opLDJimm_A_A (46) */
      register_J = 0x04f6;
      /* opJMP_A_A (58) */
      {
        register_PC = 0x04f6;
        break;
      };

    case 0x04f4:

      /* opLDAimm_A_AA (02) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0x0200;
      /* opSTAirg_A_A (e6) */
      ram[register_I] = register_A; /* store acc */

    case 0x04f6:

      /* Invariants: register_P = 0x2 register_I = 0x2a */;
      /* opLDAirg_A_AA (ea) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = ram[register_I];
      /* opSTAdir_A_A (dc) */
      ram[register_I = 0x2c] = register_A; /* store acc to RAM */
      /* opINP_A_AA (13) */
      cmp_old = flag_C = acc_a0 = register_A;
#ifdef RAWIO
      register_A = cmp_new = get_io_bit(0x3);
#else
      register_A = cmp_new = get_io_moveup();
#endif
      /* opLDPimm_A_A (82) */
      /* opSTAdir_A_A (df) */
      ram[register_I = 0x2f] = register_A; /* store acc to RAM */
      /* opINP_A_AA (14) */
      cmp_old = flag_C = acc_a0 = register_A;
#ifdef RAWIO
      register_A = cmp_new = get_io_bit(0x4);
#else
      register_A = cmp_new = get_io_movedown();
#endif
      /* opSUBirg_A_AA (e8) */
      register_A = (flag_C = ((cmp_old = acc_a0 = register_A) + ((cmp_new = ram[register_I]) ^ 0xFFF) + 1)) & 0xFFF; /* ones compliment */
      /* opLSLe_A_AA (ec) */
      cmp_new = 0x0CEC;
      cmp_old = acc_a0 = register_A;
      flag_C = (0x0CEC + register_A);
      register_A = (register_A << 1) & 0x0FFF;
      /* opLSLe_A_AA (ec) */
      cmp_new = 0x0CEC;
      cmp_old = acc_a0 = register_A;
      flag_C = (0x0CEC + register_A);
      register_A = (register_A << 1) & 0x0FFF;
      /* opLSLe_A_AA (ec) */
      cmp_new = 0x0CEC;
      cmp_old = acc_a0 = register_A;
      flag_C = (0x0CEC + register_A);
      register_A = (register_A << 1) & 0x0FFF;
      /* opLSLe_A_AA (ec) */
      cmp_new = 0x0CEC;
      cmp_old = acc_a0 = register_A;
      flag_C = (0x0CEC + register_A);
      register_A = (register_A << 1) & 0x0FFF;
      /* opADDdir_A_AA (6b) */
      register_A = (flag_C = ((cmp_old = acc_a0 = register_A) + (cmp_new = ram[register_I = 0x2b]))) & 0xFFF; /* do acc operation */
      /* opSTAirg_A_A (e6) */
      ram[0x2b] = register_A; /* store acc */
      /* opLDAimm_A_AA (03) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0x0300;
      /* opLSRe_A_AA (eb) */
      cmp_new = 0x0BEB; cmp_old = acc_a0 = register_A; flag_C = (0x0BEB + register_A);
      register_A >>= 1;
      /* opCMPdir_A_AA (bb) */
      cmp_old = acc_a0 = register_A; /* backup old acc */
      flag_C = (((cmp_new = ram[register_I = 0x2b]) ^ 0xFFF) + 1 + register_A);
      /* opLDJimm_A_A (4b) */
      register_J = 0x051b;
      /* opJLT_A_A (5b) */
      if (cmp_new < cmp_old) {
        register_PC = 0x051b;
        break;
      };
      /* opLDAimm_A_AA (0d) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0x0d00;
      /* opASRe_A_AA (ed) */
      cmp_new = 0xDED; cmp_old = flag_C = acc_a0 = register_A;
      register_A = SEX(register_A); /* make signed */
      register_A = (((signed short int)(((signed short int)register_A) >> (signed short int)1)) /* SAR */) & 0xFFF;
      /* opCMPdir_A_AA (bb) */
      cmp_old = acc_a0 = register_A; /* backup old acc */
      flag_C = (((cmp_new = ram[register_I = 0x2b]) ^ 0xFFF) + 1 + register_A);
      /* opJNC_A_AA (5d) */
      if (!(flag_C & CARRYBIT)) {
        register_PC = 0x051b;
        break;
      };
      /* opLDAimm_A_AA (08) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0x0800;
      /* opCMPdir_A_AA (bb) */
      cmp_old = acc_a0 = register_A; /* backup old acc */
      flag_C = (((cmp_new = ram[register_I = 0x2b]) ^ 0xFFF) + 1 + register_A);
      /* opLDJimm_A_A (48) */
      register_J = 0x0518;
      /* opJLT_A_A (5b) */
      if (cmp_new < cmp_old) {
        register_PC = 0x0518;
        break;
      };
      /* opLDAimm_A_AA (0d) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0x0d00;
      /* opASRe_A_AA (ed) */
      cmp_new = 0xDED; cmp_old = flag_C = acc_a0 = register_A;
      register_A = SEX(register_A); /* make signed */
      register_A = (((signed short int)(((signed short int)register_A) >> (signed short int)1)) /* SAR */) & 0xFFF;
      /* opSTAirg_A_A (e6) */
      ram[0x2b] = register_A; /* store acc */
      /* opLDJimm_A_A (4b) */
      register_J = 0x051b;
      /* opJMP_A_A (58) */
      {
        register_PC = 0x051b;
        break;
      };

    case 0x0518:

      /* opLDAimm_A_AA (03) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0x0300;
      /* opLSRe_A_AA (eb) */
      cmp_new = 0x0BEB; cmp_old = acc_a0 = register_A; flag_C = (0x0BEB + register_A);
      register_A >>= 1;
      /* opSTAirg_A_A (e6) */
      ram[register_I] = register_A; /* store acc */

    case 0x051b:

      /* Invariants: register_P = 0x2 register_I = 0x2b */;
      /* opLDAirg_A_AA (ea) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = ram[register_I];
      /* opSTAdir_A_A (dd) */
      ram[register_I = 0x2d] = register_A; /* store acc to RAM */
      /* opLDAimm_A_AA (01) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0x0100;
      /* opADDimmX_A_AA (20) */
      cmp_old = register_A; cmp_new = 0xa4;
      register_A = flag_C = acc_a0 = 0x01a4;
      /* opCMPdir_A_AA (bc) */
      cmp_old = acc_a0 = register_A; /* backup old acc */
      flag_C = (((cmp_new = ram[register_I = 0x2c]) ^ 0xFFF) + 1 + register_A);
      /* opLDJimm_A_A (47) */
      register_J = 0x0537;
      /* opJLT_A_A (5b) */
      if (cmp_new < cmp_old) {
        register_PC = 0x0537;
        break;
      };
      /* opLDAimm_A_AA (08) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0x0800;
      /* opCMPdir_A_AA (bc) */
      cmp_old = acc_a0 = register_A; /* backup old acc */
      flag_C = (((cmp_new = ram[register_I = 0x2c]) ^ 0xFFF) + 1 + register_A);
      /* opLDJimm_A_A (43) */
      register_J = 0x0533;
      /* opJLT_A_A (5b) */
      if (cmp_new < cmp_old) {
        register_PC = 0x0533;
        break;
      };
      /* opLDAimm_A_AA (0e) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0x0e00;
      /* opADDimmX_A_AA (20) */
      cmp_old = register_A; cmp_new = 0x84;
      register_A = flag_C = acc_a0 = 0x0e84;
      /* opCMPdir_A_AA (bc) */
      cmp_old = acc_a0 = register_A; /* backup old acc */
      flag_C = (((cmp_new = ram[register_I = 0x2c]) ^ 0xFFF) + 1 + register_A);
      /* opLDJimm_A_A (46) */
      register_J = 0x0536;
      /* opJLT_A_A (5b) */
      if (cmp_new < cmp_old) {
        register_PC = 0x0536;
        break;
      };
      /* opLDJimm_A_A (47) */
      register_J = 0x0537;
      /* opJMP_A_A (58) */
      {
        register_PC = 0x0537;
        break;
      };

    case 0x0533:

      /* opLDAimm_A_AA (01) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0x0100;
      /* opADDimmX_A_AA (20) */
      cmp_old = register_A; cmp_new = 0xa4;
      register_A = flag_C = acc_a0 = 0x01a4;

    case 0x0536:

      /* opSTAdir_A_A (dc) */
      ram[register_I = (register_P << 4) + 0xc] = register_A; /* store acc to RAM */

    case 0x0537:

      /* Invariants: register_P = 0x2 register_I = 0x2c */;
      /* opLDAimm_A_AA (01) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0x0100;
      /* opADDimmX_A_AA (20) */
      cmp_old = register_A; cmp_new = 0x2c;
      register_A = flag_C = acc_a0 = 0x012c;
      /* opCMPdir_A_AA (bd) */
      cmp_old = acc_a0 = register_A; /* backup old acc */
      flag_C = (((cmp_new = ram[register_I = 0x2d]) ^ 0xFFF) + 1 + register_A);
      /* opLDJimm_A_A (41) */
      register_J = 0x0551;
      /* opJLT_A_A (5b) */
      if (cmp_new < cmp_old) {
        register_PC = 0x0551;
        break;
      };
      /* opLDAimm_A_AA (08) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0x0800;
      /* opCMPdir_A_AA (bd) */
      cmp_old = acc_a0 = register_A; /* backup old acc */
      flag_C = (((cmp_new = ram[register_I = 0x2d]) ^ 0xFFF) + 1 + register_A);
      /* opLDJimm_A_A (4d) */
      register_J = 0x054d;
      /* opJLT_A_A (5b) */
      if (cmp_new < cmp_old) {
        register_PC = 0x054d;
        break;
      };
      /* opLDAimm_A_AA (0e) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0x0e00;
      /* opADDimmX_A_AA (20) */
      cmp_old = register_A; cmp_new = 0xd4;
      register_A = flag_C = acc_a0 = 0x0ed4;
      /* opCMPdir_A_AA (bd) */
      cmp_old = acc_a0 = register_A; /* backup old acc */
      flag_C = (((cmp_new = ram[register_I = 0x2d]) ^ 0xFFF) + 1 + register_A);
      /* opLDJimm_A_A (40) */
      register_J = 0x0550;
      /* opJLT_A_A (5b) */
      if (cmp_new < cmp_old) {
        register_PC = 0x0550;
        break;
      };
      /* opLDJimm_A_A (41) */
      register_J = 0x0551;
      /* opJMP_A_A (58) */
      {
        register_PC = 0x0551;
        break;
      };

    case 0x054d:

      /* opLDAimm_A_AA (01) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0x0100;
      /* opADDimmX_A_AA (20) */
      cmp_old = register_A; cmp_new = 0x2c;
      register_A = flag_C = acc_a0 = 0x012c;

    case 0x0550:

      /* opSTAdir_A_A (dd) */
      ram[register_I = (register_P << 4) + 0xd] = register_A; /* store acc to RAM */

    case 0x0551:

      /* Invariants: register_P = 0x2 register_I = 0x2d */;
      /* opLDAimm_A_AA (03) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0x0300;
      /* opLSRe_A_AA (eb) */
      cmp_new = 0x0BEB; cmp_old = acc_a0 = register_A; flag_C = (0x0BEB + register_A);
      register_A >>= 1;
      /* opADDdir_A_AA (6d) */
      register_A = (flag_C = ((cmp_old = acc_a0 = register_A) + (cmp_new = ram[register_I = 0x2d]))) & 0xFFF; /* do acc operation */
      /* opLDPimm_A_A (86) */
      register_P = 0x6; /* set page register */
      /* opSTAdir_A_A (d4) */
      ram[register_I = 0x64] = register_A; /* store acc to RAM */
      /* opNOP_A_B (57) */
      /* opLDAirg_B_AA (ea) */
      flag_C = acc_a0 = register_A;
      cmp_old = register_B;
      register_B = cmp_new = ram[register_I];

      /* opLDAimm_A_AA (02) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0x0200;
      /* opLDPimm_A_A (82) */
      register_P = 0x2; /* set page register */
      /* opADDdir_A_AA (6c) */
      register_A = (flag_C = ((cmp_old = acc_a0 = register_A) + (cmp_new = ram[register_I = 0x2c]))) & 0xFFF; /* do acc operation */
      /* opLDPimm_A_A (86) */
      register_P = 0x6; /* set page register */
      /* opSTAdir_A_A (d3) */
      ram[register_I = 0x63] = register_A; /* store acc to RAM */
      /* opSUBimmX_A_AA (30) */
      cmp_old = acc_a0 = register_A; /* back up regA */
      register_A = (flag_C = (register_A + (((cmp_new = 0x1e) ^ 0xFFF) + 1))) & 0xFFF; /* add */
      /* opLDJimm_A_A (4f) */
      register_J = 0x055f;
      /* opJDR_A_A (5a) */
      /* opVIN_A_A (f0) */

      FromX = register_A & 0xFFF; /* regA goes to x-coord */
      FromY = register_B & 0xFFF; /* regB goes to y-coord */

      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opOUTbi_A_A (96) */
      vgColour = 0x07;
      /* opINP_A_AA (15) */
      cmp_old = flag_C = acc_a0 = register_A;
#ifdef RAWIO
      register_A = cmp_new = get_io_bit(0x5);
#else
      register_A = cmp_new = get_io_fire();
#endif
      /* opLDPimm_A_A (86) */
      /* opSTAdir_A_A (df) */
      ram[register_I = 0x6f] = register_A; /* store acc to RAM */
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opCMPdir_A_AA (b0) */
      cmp_old = acc_a0 = register_A; /* backup old acc */
      flag_C = (((cmp_new = ram[register_I = 0x60]) ^ 0xFFF) + 1 + register_A);
      /* opLDJimm_A_A (43) */
      register_J = 0x0573;
      /* opJEQ_A_AA (5c) */
      if (cmp_new == cmp_old) {
        register_PC = 0x0573;
        break;
      };
      /* opLDAirg_A_AA (ea) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = ram[register_I];
      /* opSUBimm_A_AA (31) */
      cmp_old = acc_a0 = register_A;
      register_A = (flag_C = (register_A + (((cmp_new = 0x1) ^ 0xFFF) + 1))) & 0xFFF; /* 1's-comp add */
      /* opSTAirg_A_A (e6) */
      ram[0x60] = register_A; /* store acc */
      /* opLDJimm_A_A (44) */
      register_J = 0x0584;
      /* opJMP_A_A (58) */
      {
        register_PC = 0x0584;
        break;
      };

    case 0x0573:

      /* Invariants: register_P = 0x6 register_I = 0x60 register_A = 0x00 */;
      /* opCMPdir_A_AA (bf) */
      cmp_old = acc_a0 = register_A; /* backup old acc */
      flag_C = (((cmp_new = ram[register_I = 0x6f]) ^ 0xFFF) + 1 + register_A);
      /* opLDJimm_A_A (44) */
      register_J = 0x0584;
      /* opJNC_A_AA (5d) */
      if (!(flag_C & CARRYBIT)) {
        register_PC = 0x0584;
        break;
      };
      /* opINP_A_AA (16) */
      cmp_old = flag_C = acc_a0 = register_A;
#ifdef RAWIO
      register_A = cmp_new = get_io_bit(0x6);
#else
      register_A = cmp_new = get_io_shields();
#endif
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opJEQ_A_AA (5c) */
      if (cmp_new == cmp_old) {
        register_PC = 0x0584;
        break;
      };
      /* opLDAdir_A_AA (af) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = 0x6f]; /* set I register */

      /* opCMPdir_A_AA (b1) */
      cmp_old = acc_a0 = register_A; /* backup old acc */
      flag_C = (((cmp_new = ram[register_I = 0x61]) ^ 0xFFF) + 1 + register_A);
      /* opLDJimm_A_A (44) */
      register_J = 0x0584;
      /* opJEQ_A_AA (5c) */
      if (cmp_new == cmp_old) {
        register_PC = 0x0584;
        break;
      };
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opADDimm_A_AA (29) */
      cmp_new = 0x9; cmp_old = acc_a0 = register_A; register_A = flag_C = 0x0009;

      /* opSTAdir_A_A (d2) */
      ram[register_I = 0x62] = 0x009; /* store acc to RAM */
      /* opADDimm_A_AA (23) */
      cmp_new = 0x3; cmp_old = acc_a0 = register_A; register_A = flag_C = 0x000c;

      /* opSTAdir_A_A (d0) */
      ram[register_I = 0x60] = 0x00c; /* store acc to RAM */

    case 0x0584:

      /* Invariants: register_P = 0x6 */;
      /* opLDAdir_A_AA (af) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = 0x6f]; /* set I register */

      /* opSTAdir_A_A (d1) */
      ram[register_I = 0x61] = register_A; /* store acc to RAM */

    case 0x0586:

      /* Invariants: register_P = 0x6 register_I = 0x61 */;
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opLDPimm_A_A (83) */
      register_P = 0x3; /* set page register */
      /* opCMPdir_A_AA (b0) */
      cmp_old = acc_a0 = register_A; /* backup old acc */
      flag_C = (((cmp_new = ram[register_I = 0x30]) ^ 0xFFF) + 1 + register_A);
      /* opLDJimm_A_A (4f) */
      register_J = 0x05af;
      /* opJEQ_A_AA (5c) */
      if (cmp_new == cmp_old) {
        register_PC = 0x05af;
        break;
      };
      /* opCMPdir_A_AA (b7) */
      cmp_old = acc_a0 = register_A; /* backup old acc */
      flag_C = (((cmp_new = ram[register_I = 0x37]) ^ 0xFFF) + 1 + register_A);
      /* opLDJimm_A_A (49) */
      register_J = 0x0599;
      /* opJNC_A_AA (5d) */
      if (!(flag_C & CARRYBIT)) {
        register_PC = 0x0599;
        break;
      };
      /* opCMPdir_A_AA (bd) */
      cmp_old = acc_a0 = register_A; /* backup old acc */
      flag_C = (((cmp_new = ram[register_I = 0x3d]) ^ 0xFFF) + 1 + register_A);
      /* opJEQ_A_AA (5c) */
      if (cmp_new == cmp_old) {
        register_PC = 0x0599;
        break;
      };
      /* opSTAdir_A_A (d6) */
      ram[register_I = 0x36] = 0x000; /* store acc to RAM */
      /* opLDAdir_A_AA (a4) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = 0x34]; /* set I register */

      /* opADDimm_A_AA (21) */
      register_A = (flag_C = ((cmp_old = acc_a0 = register_A) + (cmp_new = 0x1))) & 0xFFF; /* add values, save carry */

      /* opSTAirg_A_A (e6) */
      ram[0x34] = register_A; /* store acc */
      /* opLDAdir_A_AA (a5) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = 0x35]; /* set I register */

      /* opADDimm_A_AA (22) */
      register_A = (flag_C = ((cmp_old = acc_a0 = register_A) + (cmp_new = 0x2))) & 0xFFF; /* add values, save carry */

      /* opSTAirg_A_A (e6) */
      ram[0x35] = register_A; /* store acc */

    case 0x0599:

      /* Invariants: register_P = 0x3 register_A = 0x00 */;
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opADDimm_A_AA (22) */
      cmp_new = 0x2; cmp_old = acc_a0 = register_A; register_A = flag_C = 0x0002;

      /* opCMPdir_A_AA (b7) */
      cmp_old = acc_a0 = register_A; /* backup old acc */
      flag_C = (((cmp_new = ram[register_I = 0x37]) ^ 0xFFF) + 1 + register_A);
      /* opLDJimm_A_A (4c) */
      register_J = 0x05ac;
      /* opJNC_A_AA (5d) */
      if (!(flag_C & CARRYBIT)) {
        register_PC = 0x05ac;
        break;
      };
      /* opLDAimm_A_AA (05) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0x0500;
      /* opADDimmX_A_AA (20) */
      cmp_old = register_A; cmp_new = 0xb6;
      register_A = flag_C = acc_a0 = 0x05b6;
      /* opLDPimm_A_A (80) */
      register_P = 0x0; /* set page register */
      /* opSTAdir_A_A (d0) */
      ram[register_I = 0x00] = 0x5b6; /* store acc to RAM */
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opADDimmX_A_AA (20) */
      cmp_old = register_A; cmp_new = 0x31;
      register_A = flag_C = acc_a0 = 0x0031;
      /* opLDPimm_A_A (81) */
      register_P = 0x1; /* set page register */
      /* opSTAdir_A_A (d9) */
      ram[register_I = 0x19] = 0x031; /* store acc to RAM */
      /* opLDJimm_A_A (40) */
      register_J = 0x0c00;
      /* opJMP_A_A (58) */
      {
        register_PC = 0x0c00;
        break;
      };

    case 0x05ac:

      /* Invariants: register_P = 0x3 register_I = 0x37 register_A = 0x02 */;
      /* opLDAdir_A_AA (a7) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = 0x37]; /* set I register */

      /* opADDimm_A_AA (21) */
      register_A = (flag_C = ((cmp_old = acc_a0 = register_A) + (cmp_new = 0x1))) & 0xFFF; /* add values, save carry */

      /* opSTAirg_A_A (e6) */
      ram[0x37] = register_A; /* store acc */

    case 0x05af:

      /* Invariants: register_P = 0x3 */;
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opSUBimmX_A_AA (30) */
      cmp_old = acc_a0 = register_A; /* back up regA */
      register_A = (flag_C = (register_A + (((cmp_new = 0x64) ^ 0xFFF) + 1))) & 0xFFF; /* add */

    case 0x05b2:

      /* Invariants: register_P = 0x3 */;
      /* opADDimm_A_AA (21) */
      register_A = (flag_C = ((cmp_old = acc_a0 = register_A) + (cmp_new = 0x1))) & 0xFFF; /* add values, save carry */

      /* opLDJimm_A_A (42) */
      register_J = 0x05b2;
      /* opJNC_A_AA (5d) */
      if (!(flag_C & CARRYBIT)) {
        register_PC = 0x05b2;
        break;
      };

    case 0x05b6:

      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opLDPimm_A_A (86) */
      register_P = 0x6; /* set page register */
      /* opCMPdir_A_AA (b9) */
      cmp_old = acc_a0 = register_A; /* backup old acc */
      flag_C = (((cmp_new = ram[register_I = 0x69]) ^ 0xFFF) + 1 + register_A);
      /* opLDJimm_A_A (48) */
      register_J = 0x0648;
      /* opJEQ_A_AA (5c) */
      if (cmp_new == cmp_old) {
        register_PC = 0x0648;
        break;
      };
      /* opLDPimm_A_A (86) */
      /* opLDAdir_A_AA (a3) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = 0x63]; /* set I register */

      /* opLDPimm_A_A (81) */
      register_P = 0x1; /* set page register */
      /* opSTAdir_A_A (d0) */
      ram[register_I = 0x10] = register_A; /* store acc to RAM */
      /* opSUBimmX_A_AA (30) */
      cmp_old = acc_a0 = register_A; /* back up regA */
      register_A = (flag_C = (register_A + (((cmp_new = 0x1e) ^ 0xFFF) + 1))) & 0xFFF; /* add */
      /* opSTAdir_A_A (d3) */
      ram[register_I = 0x13] = register_A; /* store acc to RAM */
      /* opLDPimm_A_A (86) */
      register_P = 0x6; /* set page register */
      /* opLDAdir_A_AA (a4) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = 0x64]; /* set I register */

      /* opLDPimm_A_A (81) */
      register_P = 0x1; /* set page register */
      /* opSTAdir_A_A (d1) */
      ram[register_I = 0x11] = register_A; /* store acc to RAM */
      /* opSTAdir_A_A (d4) */
      ram[register_I = 0x14] = register_A; /* store acc to RAM */
      /* opLDAimm_A_AA (06) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0x0600;
      /* opADDimmX_A_AA (20) */
      cmp_old = register_A; cmp_new = 0x15;
      register_A = flag_C = acc_a0 = 0x0615;
      /* opSTAdir_A_A (d6) */
      ram[register_I = 0x16] = 0x615; /* store acc to RAM */
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opADDimmX_A_AA (20) */
      cmp_old = register_A; cmp_new = 0xff;
      register_A = flag_C = acc_a0 = 0x00ff;
      /* opSTAdir_A_A (d8) */
      ram[register_I = 0x18] = 0x0ff; /* store acc to RAM */

    case 0x05d0:

      /* Invariants: register_P = 0x1 */;
      /* opLDJimm_A_A (40) */
      register_J = 0x05d0;
      /* opJDR_A_A (5a) */
      /* opLDAdir_A_AA (a3) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = 0x13]; /* set I register */

      /* opNOP_A_B (57) */
      /* opLDAdir_B_AA (a4) */
      flag_C = acc_a0 = register_A;
      cmp_old = register_B; /* store old acc */
      register_B = cmp_new = ram[register_I = (register_P << 4) + 0x04]; /* new acc value */
      /* opVIN_A_A (f0) */

      FromX = register_A & 0xFFF; /* regA goes to x-coord */
      FromY = register_B & 0xFFF; /* regB goes to y-coord */

      /* opLDAdir_A_AA (a6) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = 0x16]; /* set I register */

      /* opADDimm_A_AA (22) */
      register_A = (flag_C = ((cmp_old = acc_a0 = register_A) + (cmp_new = 0x2))) & 0xFFF; /* add values, save carry */

      /* opSTAirg_A_A (e6) */
      ram[0x16] = register_A; /* store acc */
      /* opXLT_A_AA (e2) */
      cmp_old = register_A; register_A = cmp_new = rom[register_A]; /* new acc value */
      /* opNOP_A_A (5f) */
      /* opCMPdir_A_AA (b8) */
      cmp_old = acc_a0 = register_A; /* backup old acc */
      flag_C = (((cmp_new = ram[register_I = (register_P << 4) + 0x8]) ^ 0xFFF) + 1 + register_A);
      /* opLDJimm_A_A (4f) */
      register_J = 0x05ff;
      /* opJEQ_A_AA (5c) */
      if (cmp_new == cmp_old) {
        register_PC = 0x05ff;
        break;
      };
      /* opSUBimmX_A_AA (30) */
      cmp_old = acc_a0 = register_A; /* back up regA */
      register_A = (flag_C = (register_A + (((cmp_new = 0x80) ^ 0xFFF) + 1))) & 0xFFF; /* add */
      /* opSUBdir_A_AA (73) */
      cmp_old = acc_a0 = register_A;
      register_A = (flag_C = (register_A + ((cmp_new = ram[register_I = (register_P << 4) + 0x3]) ^ 0xFFF) + 1)) & 0xFFF; /* set regI addr */
      /* opADDdir_A_AA (60) */
      register_A = (flag_C = ((cmp_old = acc_a0 = register_A) + (cmp_new = ram[register_I = (register_P << 4) + 0x0]))) & 0xFFF; /* do acc operation */
      /* opSTAdir_A_A (dc) */
      ram[register_I = (register_P << 4) + 0xc] = register_A; /* store acc to RAM */
      /* opLDAdir_A_AA (a6) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = (register_P << 4) + 0x06]; /* set I register */

      /* opADDimm_A_AA (21) */
      register_A = (flag_C = ((cmp_old = acc_a0 = register_A) + (cmp_new = 0x1))) & 0xFFF; /* add values, save carry */

      /* opXLT_A_AA (e2) */
      cmp_old = register_A; register_A = cmp_new = rom[register_A]; /* new acc value */
      /* opNOP_A_A (5f) */
      /* opSUBimmX_A_AA (30) */
      cmp_old = acc_a0 = register_A; /* back up regA */
      register_A = (flag_C = (register_A + (((cmp_new = 0x80) ^ 0xFFF) + 1))) & 0xFFF; /* add */
      /* opSUBdir_A_AA (74) */
      cmp_old = acc_a0 = register_A;
      register_A = (flag_C = (register_A + ((cmp_new = ram[register_I = (register_P << 4) + 0x4]) ^ 0xFFF) + 1)) & 0xFFF; /* set regI addr */
      /* opADDdir_A_AA (61) */
      register_A = (flag_C = ((cmp_old = acc_a0 = register_A) + (cmp_new = ram[register_I = (register_P << 4) + 0x1]))) & 0xFFF; /* do acc operation */
      /* opSTAdir_A_A (dd) */
      ram[register_I = (register_P << 4) + 0xd] = register_A; /* store acc to RAM */
      /* opLDAdir_A_AA (ac) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = (register_P << 4) + 0x0c]; /* set I register */

      /* opNOP_A_B (57) */
      /* opLDAdir_B_AA (ad) */
      flag_C = acc_a0 = register_A;
      cmp_old = register_B; /* store old acc */
      register_B = cmp_new = ram[register_I = (register_P << 4) + 0x0d]; /* new acc value */
      /* opLLT_A_AA (e4) */
      { CINEBYTE temp_byte = 0;
        for (;;) {
          if (   (((register_A >> 8) & 0x0A) && (((register_A >> 8) & 0x0A) ^ 0x0A))
                 ||   (((register_B >> 8) & 0x0A) && (((register_B >> 8) & 0x0A) ^ 0x0A))  ) break;
          register_A <<= 1; register_B <<= 1;
          if (!(++temp_byte)) break /* This may not be correct */;
        }
        vgShiftLength = temp_byte & 0xfff; register_A &= 0x0FFF; register_B &= 0x0FFF;
      }
      /* opADDdir_A_AA (63) */
      register_A = (flag_C = ((cmp_old = acc_a0 = register_A) + (cmp_new = ram[register_I = (register_P << 4) + 0x3]))) & 0xFFF; /* do acc operation */
      /* opNOP_A_B (57) */
      /* opADDdir_B_AA (64) */
      acc_a0 = register_A; /* store old acc value */
      register_B = (flag_C = ((cmp_old = register_B) + (cmp_new = ram[register_I = (register_P << 4) + 0x04]))) & 0xFFF; /* do acc operation */
      /* opVDR_A_A (e0) */
      {
        /* set ending points and draw the vector, or buffer for a later draw. */
        int ToX = register_A & 0xFFF;
        int ToY = register_B & 0xFFF;

        /* Sign extend from 20 bit CCPU to 32bit target machine */
        FromX = SEX(FromX);
        ToX = SEX(ToX);
        FromY = SEX(FromY);
        ToY = SEX(ToY);

        /* figure out the vector */
        ToX -= FromX;
        ToX = ((signed short int)(((signed short int)ToX) >> (signed short int)vgShiftLength)) /* SAR */;
        ToX += FromX;

        ToY -= FromY;
        ToY = ((signed short int)(((signed short int)ToY) >> (signed short int)vgShiftLength)) /* SAR */;
        ToY += FromY;

        /* render the line */
#ifndef DUALCPU
        CinemaVectorData (FromX, FromY, ToX, ToY, vgColour);
#endif

      }
      /* opLDAdir_A_AA (ac) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = (register_P << 4) + 0x0c]; /* set I register */

      /* opADDdir_A_AA (63) */
      register_A = (flag_C = ((cmp_old = acc_a0 = register_A) + (cmp_new = ram[register_I = (register_P << 4) + 0x3]))) & 0xFFF; /* do acc operation */
      /* opSTAirg_A_A (e6) */
      ram[register_I] = register_A; /* store acc */
      /* opLDAdir_A_AA (ad) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = (register_P << 4) + 0x0d]; /* set I register */

      /* opADDdir_A_AA (64) */
      register_A = (flag_C = ((cmp_old = acc_a0 = register_A) + (cmp_new = ram[register_I = (register_P << 4) + 0x4]))) & 0xFFF; /* do acc operation */
      /* opSTAirg_A_A (e6) */
      ram[register_I] = register_A; /* store acc */
      /* opLDJimm_A_A (40) */
      register_J = 0x05d0;
      /* opJMP_A_A (58) */
      {
        register_PC = 0x05d0;
        break;
      };

    case 0x05ff:

      /* Invariants: register_P = 0x1 register_I = 0x18 register_A = 0xff */;
      /* opLDAdir_A_AA (a6) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = 0x16]; /* set I register */

      /* opADDimm_A_AA (21) */
      register_A = (flag_C = ((cmp_old = acc_a0 = register_A) + (cmp_new = 0x1))) & 0xFFF; /* add values, save carry */

      /* opSTAirg_A_A (e6) */
      ram[0x16] = register_A; /* store acc */
      /* opXLT_A_AA (e2) */
      cmp_old = register_A; register_A = cmp_new = rom[register_A]; /* new acc value */
      /* opNOP_A_A (5f) */
      /* opCMPdir_A_AA (b8) */
      cmp_old = acc_a0 = register_A; /* backup old acc */
      flag_C = (((cmp_new = ram[register_I = (register_P << 4) + 0x8]) ^ 0xFFF) + 1 + register_A);
      /* opLDJimm_A_A (48) */
      register_J = 0x0648;
      /* opJEQ_A_AA (5c) */
      if (cmp_new == cmp_old) {
        register_PC = 0x0648;
        break;
      };
      /* opSUBimmX_A_AA (30) */
      cmp_old = acc_a0 = register_A; /* back up regA */
      register_A = (flag_C = (register_A + (((cmp_new = 0x80) ^ 0xFFF) + 1))) & 0xFFF; /* add */
      /* opADDdir_A_AA (60) */
      register_A = (flag_C = ((cmp_old = acc_a0 = register_A) + (cmp_new = ram[register_I = (register_P << 4) + 0x0]))) & 0xFFF; /* do acc operation */
      /* opSTAdir_A_A (d3) */
      ram[register_I = (register_P << 4) + 0x3] = register_A; /* store acc to RAM */
      /* opLDAdir_A_AA (a6) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = (register_P << 4) + 0x06]; /* set I register */

      /* opADDimm_A_AA (21) */
      register_A = (flag_C = ((cmp_old = acc_a0 = register_A) + (cmp_new = 0x1))) & 0xFFF; /* add values, save carry */

      /* opXLT_A_AA (e2) */
      cmp_old = register_A; register_A = cmp_new = rom[register_A]; /* new acc value */
      /* opNOP_A_A (5f) */
      /* opSUBimmX_A_AA (30) */
      cmp_old = acc_a0 = register_A; /* back up regA */
      register_A = (flag_C = (register_A + (((cmp_new = 0x80) ^ 0xFFF) + 1))) & 0xFFF; /* add */
      /* opADDdir_A_AA (61) */
      register_A = (flag_C = ((cmp_old = acc_a0 = register_A) + (cmp_new = ram[register_I = (register_P << 4) + 0x1]))) & 0xFFF; /* do acc operation */
      /* opSTAdir_A_A (d4) */
      ram[register_I = (register_P << 4) + 0x4] = register_A; /* store acc to RAM */
      /* opLDJimm_A_A (40) */
      register_J = 0x05d0;
      /* opJMP_A_A (58) */
      {
        register_PC = 0x05d0;
        break;
      };

    case 0x0648:

      /* Invariants: register_P = 0x1 register_I = 0x18 register_A = 0xff */;
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opLDPimm_A_A (86) */
      register_P = 0x6; /* set page register */
      /* opCMPdir_A_AA (b2) */
      cmp_old = acc_a0 = register_A; /* backup old acc */
      flag_C = (((cmp_new = ram[register_I = 0x62]) ^ 0xFFF) + 1 + register_A);
      /* opLDJimm_A_A (49) */
      register_J = 0x06b9;
      /* opJEQ_A_AA (5c) */
      if (cmp_new == cmp_old) {
        register_PC = 0x06b9;
        break;
      };
      /* opINP_A_AA (18) */
      cmp_old = flag_C = acc_a0 = register_A;
#ifdef RAWIO
      register_A = cmp_new = get_io_bit(0x8);
#else
      register_A = cmp_new = get_io_bit(0x8);
#endif
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opLDJimm_A_A (4d) */
      register_J = 0x065d;
      /* opJEQ_A_AA (5c) */
      if (cmp_new == cmp_old) {
        register_PC = 0x065d;
        break;
      };
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opADDimm_A_AA (26) */
      cmp_new = 0x6; cmp_old = acc_a0 = register_A; register_A = flag_C = 0x0006;

      /* opLDAirg_A_AA (ea) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = ram[register_I];
      /* opJLT_A_A (5b) */
      if (cmp_new < cmp_old) {
        register_PC = 0x065d;
        break;
      };
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opADDimm_A_AA (21) */
      cmp_new = 0x1; cmp_old = acc_a0 = register_A; register_A = flag_C = 0x0001;

      /* opANDirg_A_AA (e9) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A &= (cmp_new = ram[register_I]);
      /* opLDJimm_A_A (4f) */
      register_J = 0x065f;
      /* opJMP_A_A (58) */
      {
        register_PC = 0x065f;
        break;
      };

    case 0x065d:

      /* Invariants: register_P = 0x6 register_I = 0x62 register_A = 0x00 */;
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opADDimm_A_AA (21) */
      cmp_new = 0x1; cmp_old = acc_a0 = register_A; register_A = flag_C = 0x0001;


    case 0x065f:

      /* Invariants: register_P = 0x6 register_I = 0x62 register_A = 0x01 */;
      /* opADDimmX_A_AA (20) */
      cmp_old = register_A; cmp_new = 0x1a;
      register_A = flag_C = acc_a0 = 0x001b;
      /* opOUTbi_A_A (93) */
      /* opOUTsnd_A (93) */
      set_sound_data(1);
      /* opLSRe_A_AA (eb) */
      cmp_new = 0x0BEB; cmp_old = acc_a0 = register_A; flag_C = (0x0BEB + register_A);
      register_A >>= 1;
      /* opOUTbi_A_A (90) */
      /* opOUTsnd_A (90) */
      set_sound_addr_A(register_A & 1);
      /* opLSRe_A_AA (eb) */
      cmp_new = 0x0BEB; cmp_old = acc_a0 = register_A; flag_C = (0x0BEB + register_A);
      register_A >>= 1;
      /* opOUTbi_A_A (91) */
      /* opOUTsnd_A (91) */
      set_sound_addr_B(register_A & 1);
      /* opLSRe_A_AA (eb) */
      cmp_new = 0x0BEB; cmp_old = acc_a0 = register_A; flag_C = (0x0BEB + register_A);
      register_A >>= 1;
      /* opOUTbi_A_A (92) */
      /* opOUTsnd_A (92) */
      set_sound_addr_C(register_A & 1);
      /* opLSRe_A_AA (eb) */
      cmp_new = 0x0BEB; cmp_old = acc_a0 = register_A; flag_C = (0x0BEB + register_A);
      register_A >>= 1;
      /* opOUTbi_A_A (94) */
      /* opOUTsnd_A (94) */
      if (register_A & 1) strobe_sound_on(); else strobe_sound_off();
      /* opLSRe_A_AA (eb) */
      cmp_new = 0x0BEB; cmp_old = acc_a0 = register_A; flag_C = (0x0BEB + register_A);
      register_A >>= 1;
      /* opOUTbi_A_A (94) */
      /* opOUTsnd_A (94) */
      if (register_A & 1) strobe_sound_on(); else strobe_sound_off();
      /* opLDAdir_A_AA (a2) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = 0x62]; /* set I register */

      /* opNOP_A_B (57) */
      /* opLDAimm_B_AA (00) */
      flag_C = acc_a0 = register_A;
      cmp_old = register_B; /* step back cmp flag */
      register_B = cmp_new = 0;
      /* opCMPdir_A_AA (b3) */
      cmp_old = acc_a0 = register_A; /* backup old acc */
      flag_C = (((cmp_new = ram[register_I = 0x63]) ^ 0xFFF) + 1 + register_A);
      /* opMULirg_A_AA (e3) */
      MUL();
      /* opMULirg_A_AA (e3) */
      MUL();
      /* opMULirg_A_AA (e3) */
      MUL();
      /* opMULirg_A_AA (e3) */
      MUL();
      /* opASRDe_A_AA (ee) */
      cmp_new = 0x0EEE; cmp_old = acc_a0 = register_A; flag_C = (0x0EEE + register_A);
      register_A = (register_A >> 1) | ((register_B & 1) << 11);
      register_B = (register_B >> 1) | (register_B & 0x800);
      /* opNOP_A_B (57) */
      /* opSTAdir_B_BB (d5) */
      ram[register_I = (register_P << 4) + 0x5] = register_B; /* set I register and store B to ram */
      /* opLDAdir_A_AA (a3) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = 0x63]; /* set I register */

      /* opSUBdir_A_AA (75) */
      cmp_old = acc_a0 = register_A;
      register_A = (flag_C = (register_A + ((cmp_new = ram[register_I = 0x65]) ^ 0xFFF) + 1)) & 0xFFF; /* set regI addr */
      /* opLDPimm_A_A (80) */
      register_P = 0x0; /* set page register */
      /* opSTAdir_A_A (da) */
      ram[register_I = 0x0a] = register_A; /* store acc to RAM */
      /* opNOP_A_B (57) */
      /* opLDAimm_B_AA (00) */
      flag_C = acc_a0 = register_A;
      cmp_old = register_B; /* step back cmp flag */
      register_B = cmp_new = 0;
      /* opLDPimm_A_A (86) */
      register_P = 0x6; /* set page register */
      /* opLDAdir_A_AA (a2) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = 0x62]; /* set I register */

      /* opLDPimm_A_A (86) */
      /* opCMPdir_A_AA (b4) */
      cmp_old = acc_a0 = register_A; /* backup old acc */
      flag_C = (((cmp_new = ram[register_I = 0x64]) ^ 0xFFF) + 1 + register_A);
      /* opMULirg_A_AA (e3) */
      MUL();
      /* opMULirg_A_AA (e3) */
      MUL();
      /* opMULirg_A_AA (e3) */
      MUL();
      /* opMULirg_A_AA (e3) */
      MUL();
      /* opASRDe_A_AA (ee) */
      cmp_new = 0x0EEE; cmp_old = acc_a0 = register_A; flag_C = (0x0EEE + register_A);
      register_A = (register_A >> 1) | ((register_B & 1) << 11);
      register_B = (register_B >> 1) | (register_B & 0x800);
      /* opNOP_A_B (57) */
      /* opSTAdir_B_BB (d6) */
      ram[register_I = (register_P << 4) + 0x6] = register_B; /* set I register and store B to ram */
      /* opLDAdir_A_AA (a4) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = 0x64]; /* set I register */

      /* opSUBdir_A_AA (76) */
      cmp_old = acc_a0 = register_A;
      register_A = (flag_C = (register_A + ((cmp_new = ram[register_I = 0x66]) ^ 0xFFF) + 1)) & 0xFFF; /* set regI addr */
      /* opLDPimm_A_A (80) */
      register_P = 0x0; /* set page register */
      /* opSTAdir_A_A (db) */
      ram[register_I = 0x0b] = register_A; /* store acc to RAM */
      /* opNOP_A_B (57) */
      /* opLDAirg_B_AA (ea) */
      flag_C = acc_a0 = register_A;
      cmp_old = register_B;
      register_B = cmp_new = ram[register_I];

      /* opLDAdir_A_AA (aa) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = 0x0a]; /* set I register */

      /* opLDJimm_A_A (41) */
      register_J = 0x0691;
      /* opJDR_A_A (5a) */
      /* opVIN_A_A (f0) */

      FromX = register_A & 0xFFF; /* regA goes to x-coord */
      FromY = register_B & 0xFFF; /* regB goes to y-coord */


    case 0x0693:

      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opLDPimm_A_A (84) */
      register_P = 0x4; /* set page register */
      /* opCMPdir_A_AA (b0) */
      cmp_old = acc_a0 = register_A; /* backup old acc */
      flag_C = (((cmp_new = ram[register_I = 0x40]) ^ 0xFFF) + 1 + register_A);
      /* opLDJimm_A_A (4d) */
      register_J = 0x06cd;
      /* opJEQ_A_AA (5c) */
      if (cmp_new == cmp_old) {
        register_PC = 0x06cd;
        break;
      };
      /* opCMPdir_A_AA (b7) */
      cmp_old = acc_a0 = register_A; /* backup old acc */
      flag_C = (((cmp_new = ram[register_I = 0x47]) ^ 0xFFF) + 1 + register_A);
      /* opLDJimm_A_A (46) */
      register_J = 0x06a6;
      /* opJNC_A_AA (5d) */
      if (!(flag_C & CARRYBIT)) {
        register_PC = 0x06a6;
        break;
      };
      /* opCMPdir_A_AA (bd) */
      cmp_old = acc_a0 = register_A; /* backup old acc */
      flag_C = (((cmp_new = ram[register_I = 0x4d]) ^ 0xFFF) + 1 + register_A);
      /* opJEQ_A_AA (5c) */
      if (cmp_new == cmp_old) {
        register_PC = 0x06a6;
        break;
      };
      /* opSTAdir_A_A (d6) */
      ram[register_I = 0x46] = 0x000; /* store acc to RAM */
      /* opLDAdir_A_AA (a4) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = 0x44]; /* set I register */

      /* opADDimm_A_AA (22) */
      register_A = (flag_C = ((cmp_old = acc_a0 = register_A) + (cmp_new = 0x2))) & 0xFFF; /* add values, save carry */

      /* opSTAirg_A_A (e6) */
      ram[0x44] = register_A; /* store acc */
      /* opLDAdir_A_AA (a5) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = 0x45]; /* set I register */

      /* opSUBimm_A_AA (31) */
      cmp_old = acc_a0 = register_A;
      register_A = (flag_C = (register_A + (((cmp_new = 0x1) ^ 0xFFF) + 1))) & 0xFFF; /* 1's-comp add */
      /* opSTAirg_A_A (e6) */
      ram[0x45] = register_A; /* store acc */

    case 0x06a6:

      /* Invariants: register_P = 0x4 register_A = 0x00 */;
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opADDimm_A_AA (22) */
      cmp_new = 0x2; cmp_old = acc_a0 = register_A; register_A = flag_C = 0x0002;

      /* opCMPdir_A_AA (b7) */
      cmp_old = acc_a0 = register_A; /* backup old acc */
      flag_C = (((cmp_new = ram[register_I = 0x47]) ^ 0xFFF) + 1 + register_A);
      /* opLDJimm_A_A (4a) */
      register_J = 0x06ca;
      /* opJNC_A_AA (5d) */
      if (!(flag_C & CARRYBIT)) {
        register_PC = 0x06ca;
        break;
      };
      /* opLDAimm_A_AA (06) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0x0600;
      /* opADDimmX_A_AA (20) */
      cmp_old = register_A; cmp_new = 0xd4;
      register_A = flag_C = acc_a0 = 0x06d4;
      /* opLDPimm_A_A (80) */
      register_P = 0x0; /* set page register */
      /* opSTAdir_A_A (d0) */
      ram[register_I = 0x00] = 0x6d4; /* store acc to RAM */
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opADDimmX_A_AA (20) */
      cmp_old = register_A; cmp_new = 0x41;
      register_A = flag_C = acc_a0 = 0x0041;
      /* opLDPimm_A_A (81) */
      register_P = 0x1; /* set page register */
      /* opSTAdir_A_A (d9) */
      ram[register_I = 0x19] = 0x041; /* store acc to RAM */
      /* opLDJimm_A_A (40) */
      register_J = 0x0c00;
      /* opJMP_A_A (58) */
      {
        register_PC = 0x0c00;
        break;
      };

    case 0x06b9:

      /* Invariants: register_P = 0x6 register_I = 0x62 register_A = 0x00 */;
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opADDimmX_A_AA (20) */
      cmp_old = register_A; cmp_new = 0x1a;
      register_A = flag_C = acc_a0 = 0x001a;
      /* opOUTbi_A_A (93) */
      /* opOUTsnd_A (93) */
      set_sound_data(0);
      /* opLSRe_A_AA (eb) */
      cmp_new = 0x0BEB; cmp_old = acc_a0 = register_A; flag_C = (0x0BEB + register_A);
      register_A >>= 1;
      /* opOUTbi_A_A (90) */
      /* opOUTsnd_A (90) */
      set_sound_addr_A(register_A & 1);
      /* opLSRe_A_AA (eb) */
      cmp_new = 0x0BEB; cmp_old = acc_a0 = register_A; flag_C = (0x0BEB + register_A);
      register_A >>= 1;
      /* opOUTbi_A_A (91) */
      /* opOUTsnd_A (91) */
      set_sound_addr_B(register_A & 1);
      /* opLSRe_A_AA (eb) */
      cmp_new = 0x0BEB; cmp_old = acc_a0 = register_A; flag_C = (0x0BEB + register_A);
      register_A >>= 1;
      /* opOUTbi_A_A (92) */
      /* opOUTsnd_A (92) */
      set_sound_addr_C(register_A & 1);
      /* opLSRe_A_AA (eb) */
      cmp_new = 0x0BEB; cmp_old = acc_a0 = register_A; flag_C = (0x0BEB + register_A);
      register_A >>= 1;
      /* opOUTbi_A_A (94) */
      /* opOUTsnd_A (94) */
      if (register_A & 1) strobe_sound_on(); else strobe_sound_off();
      /* opLSRe_A_AA (eb) */
      cmp_new = 0x0BEB; cmp_old = acc_a0 = register_A; flag_C = (0x0BEB + register_A);
      register_A >>= 1;
      /* opOUTbi_A_A (94) */
      /* opOUTsnd_A (94) */
      if (register_A & 1) strobe_sound_on(); else strobe_sound_off();
      /* opLDJimm_A_A (43) */
      register_J = 0x0693;
      /* opJMP_A_A (58) */
      {
        register_PC = 0x0693;
        break;
      };

    case 0x06ca:

      /* Invariants: register_P = 0x4 register_I = 0x47 register_A = 0x02 */;
      /* opLDAdir_A_AA (a7) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = 0x47]; /* set I register */

      /* opADDimm_A_AA (21) */
      register_A = (flag_C = ((cmp_old = acc_a0 = register_A) + (cmp_new = 0x1))) & 0xFFF; /* add values, save carry */

      /* opSTAirg_A_A (e6) */
      ram[0x47] = register_A; /* store acc */

    case 0x06cd:

      /* Invariants: register_P = 0x4 register_I = 0x47 */;
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opSUBimmX_A_AA (30) */
      cmp_old = acc_a0 = register_A; /* back up regA */
      register_A = (flag_C = (register_A + (((cmp_new = 0x64) ^ 0xFFF) + 1))) & 0xFFF; /* add */

    case 0x06d0:

      /* Invariants: register_P = 0x4 register_I = 0x47 */;
      /* opADDimm_A_AA (21) */
      register_A = (flag_C = ((cmp_old = acc_a0 = register_A) + (cmp_new = 0x1))) & 0xFFF; /* add values, save carry */

      /* opLDJimm_A_A (40) */
      register_J = 0x06d0;
      /* opJNC_A_AA (5d) */
      if (!(flag_C & CARRYBIT)) {
        register_PC = 0x06d0;
        break;
      };

    case 0x06d4:

      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opLDPimm_A_A (86) */
      register_P = 0x6; /* set page register */
      /* opCMPdir_A_AA (b2) */
      cmp_old = acc_a0 = register_A; /* backup old acc */
      flag_C = (((cmp_new = ram[register_I = 0x62]) ^ 0xFFF) + 1 + register_A);
      /* opLDJimm_A_A (4d) */
      register_J = 0x07bd;
      /* opJEQ_A_AA (5c) */
      if (cmp_new == cmp_old) {
        register_PC = 0x07bd;
        break;
      };
      /* opINP_A_AA (1d) */
      cmp_old = flag_C = acc_a0 = register_A;
#ifdef RAWIO
      register_A = cmp_new = get_io_bit(0xd);
#else
      register_A = cmp_new = get_io_bit(0xd);
#endif
      /* opADDimm_A_AA (21) */
      register_A = (flag_C = ((cmp_old = acc_a0 = register_A) + (cmp_new = 0x1))) & 0xFFF; /* add values, save carry */

      /* opLDJimm_A_A (44) */
      register_J = 0x0724;
      /* opJEQ_A_AA (5c) */
      if (cmp_new == cmp_old) {
        register_PC = 0x0724;
        break;
      };
      /* opOUTbi_A_A (96) */
      vgColour = register_A & 0x01 ? 0x0f : 0x07;
      /* opLDAimm_A_AA (0f) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0x0f00;
      /* opADDimmX_A_AA (20) */
      cmp_old = register_A; cmp_new = 0xc0;
      register_A = flag_C = acc_a0 = 0x0fc0;
      /* opLDPimm_A_A (80) */
      register_P = 0x0; /* set page register */
      /* opSTAdir_A_A (d5) */
      ram[register_I = 0x05] = 0xfc0; /* store acc to RAM */
      /* opNOP_A_B (57) */
      /* opLDAimm_B_AA (00) */
      flag_C = acc_a0 = register_A;
      cmp_old = register_B; /* step back cmp flag */
      register_B = cmp_new = 0;
      /* opLDPimm_A_A (86) */
      register_P = 0x6; /* set page register */
      /* opLDAdir_A_AA (a2) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = 0x62]; /* set I register */

      /* opLDPimm_A_A (80) */
      register_P = 0x0; /* set page register */
      /* opCMPdir_A_AA (b5) */
      cmp_old = acc_a0 = register_A; /* backup old acc */
      flag_C = (((cmp_new = ram[register_I = 0x05]) ^ 0xFFF) + 1 + register_A);
      /* opMULirg_A_AA (e3) */
      MUL();
      /* opMULirg_A_AA (e3) */
      MUL();
      /* opMULirg_A_AA (e3) */
      MUL();
      /* opMULirg_A_AA (e3) */
      MUL();
      /* opASRDe_A_AA (ee) */
      cmp_new = 0x0EEE; cmp_old = acc_a0 = register_A; flag_C = (0x0EEE + register_A);
      register_A = (register_A >> 1) | ((register_B & 1) << 11);
      register_B = (register_B >> 1) | (register_B & 0x800);
      /* opNOP_A_B (57) */
      /* opADDdir_B_AA (6a) */
      acc_a0 = register_A; /* store old acc value */
      register_B = (flag_C = ((cmp_old = register_B) + (cmp_new = ram[register_I = 0x0a]))) & 0xFFF; /* do acc operation */
      /* opNOP_A_B (57) */
      /* opSTAdir_B_BB (d5) */
      ram[register_I = (register_P << 4) + 0x5] = register_B; /* set I register and store B to ram */
      /* opLDAdir_A_AA (ab) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = 0x0b]; /* set I register */

      /* opSTAdir_A_A (d6) */
      ram[register_I = 0x06] = register_A; /* store acc to RAM */
      /* opNOP_A_B (57) */
      /* opLDAirg_B_AA (ea) */
      flag_C = acc_a0 = register_A;
      cmp_old = register_B;
      register_B = cmp_new = ram[register_I];

      /* opLDAdir_A_AA (a5) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = 0x05]; /* set I register */

      /* opVIN_A_A (f0) */

      FromX = register_A & 0xFFF; /* regA goes to x-coord */
      FromY = register_B & 0xFFF; /* regB goes to y-coord */

      /* opLDPimm_A_A (86) */
      register_P = 0x6; /* set page register */
      /* opSUBdir_A_AA (73) */
      cmp_old = acc_a0 = register_A;
      register_A = (flag_C = (register_A + ((cmp_new = ram[register_I = 0x63]) ^ 0xFFF) + 1)) & 0xFFF; /* set regI addr */
      /* opASRe_A_AA (ed) */
      cmp_new = 0xDED; cmp_old = flag_C = acc_a0 = register_A;
      register_A = SEX(register_A); /* make signed */
      register_A = (((signed short int)(((signed short int)register_A) >> (signed short int)1)) /* SAR */) & 0xFFF;
      /* opLDPimm_A_A (80) */
      register_P = 0x0; /* set page register */
      /* opSTAdir_A_A (d7) */
      ram[register_I = 0x07] = register_A; /* store acc to RAM */
      /* opLDAimm_A_AA (08) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0x0800;
      /* opLDAirg_A_AA (ea) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = ram[register_I];
      /* opLDJimm_A_A (46) */
      register_J = 0x0706;
      /* opJLT_A_A (5b) */
      if (cmp_new < cmp_old) {
        register_PC = 0x0706;
        break;
      };
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opSUBirg_A_AA (e8) */
      register_A = (flag_C = ((cmp_old = acc_a0 = register_A) + ((cmp_new = ram[register_I]) ^ 0xFFF) + 1)) & 0xFFF; /* ones compliment */

    case 0x0706:

      /* Invariants: register_P = 0x0 register_I = 0x07 */;
      /* opSTAdir_A_A (de) */
      ram[register_I = 0x0e] = register_A; /* store acc to RAM */
      /* opLDAdir_A_AA (a6) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = 0x06]; /* set I register */

      /* opLDPimm_A_A (86) */
      register_P = 0x6; /* set page register */
      /* opSUBdir_A_AA (74) */
      cmp_old = acc_a0 = register_A;
      register_A = (flag_C = (register_A + ((cmp_new = ram[register_I = 0x64]) ^ 0xFFF) + 1)) & 0xFFF; /* set regI addr */
      /* opASRe_A_AA (ed) */
      cmp_new = 0xDED; cmp_old = flag_C = acc_a0 = register_A;
      register_A = SEX(register_A); /* make signed */
      register_A = (((signed short int)(((signed short int)register_A) >> (signed short int)1)) /* SAR */) & 0xFFF;
      /* opLDPimm_A_A (80) */
      register_P = 0x0; /* set page register */
      /* opSTAdir_A_A (d8) */
      ram[register_I = 0x08] = register_A; /* store acc to RAM */
      /* opNOP_A_B (57) */
      /* opLDAirg_B_AA (ea) */
      flag_C = acc_a0 = register_A;
      cmp_old = register_B;
      register_B = cmp_new = ram[register_I];

      /* opLDAimm_A_AA (08) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0x0800;
      /* opLDAirg_A_AA (ea) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = ram[register_I];
      /* opLDJimm_A_A (46) */
      register_J = 0x0716;
      /* opJLT_A_A (5b) */
      if (cmp_new < cmp_old) {
        register_PC = 0x0716;
        break;
      };
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opSUBirg_A_AA (e8) */
      register_A = (flag_C = ((cmp_old = acc_a0 = register_A) + ((cmp_new = ram[register_I]) ^ 0xFFF) + 1)) & 0xFFF; /* ones compliment */

    case 0x0716:

      /* Invariants: register_P = 0x0 register_I = 0x08 */;
      /* opADDdir_A_AA (6e) */
      register_A = (flag_C = ((cmp_old = acc_a0 = register_A) + (cmp_new = ram[register_I = 0x0e]))) & 0xFFF; /* do acc operation */
      /* opSUBimm_A_AA (33) */
      cmp_old = acc_a0 = register_A;
      register_A = (flag_C = (register_A + (((cmp_new = 0x3) ^ 0xFFF) + 1))) & 0xFFF; /* 1's-comp add */
      /* opLDJimm_A_A (44) */
      register_J = 0x07a4;
      /* opJNC_A_AA (5d) */
      if (!(flag_C & CARRYBIT)) {
        register_PC = 0x07a4;
        break;
      };
      /* opLDAdir_A_AA (a7) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = 0x07]; /* set I register */

      /* opLLT_A_AA (e4) */
      { CINEBYTE temp_byte = 0;
        for (;;) {
          if (   (((register_A >> 8) & 0x0A) && (((register_A >> 8) & 0x0A) ^ 0x0A))
                 ||   (((register_B >> 8) & 0x0A) && (((register_B >> 8) & 0x0A) ^ 0x0A))  ) break;
          register_A <<= 1; register_B <<= 1;
          if (!(++temp_byte)) break /* This may not be correct */;
        }
        vgShiftLength = temp_byte & 0xfff; register_A &= 0x0FFF; register_B &= 0x0FFF;
      }
      /* opADDdir_A_AA (65) */
      register_A = (flag_C = ((cmp_old = acc_a0 = register_A) + (cmp_new = ram[register_I = 0x05]))) & 0xFFF; /* do acc operation */
      /* opNOP_A_B (57) */
      /* opADDdir_B_AA (66) */
      acc_a0 = register_A; /* store old acc value */
      register_B = (flag_C = ((cmp_old = register_B) + (cmp_new = ram[register_I = 0x06]))) & 0xFFF; /* do acc operation */
      /* opVDR_A_A (e0) */
      {
        /* set ending points and draw the vector, or buffer for a later draw. */
        int ToX = register_A & 0xFFF;
        int ToY = register_B & 0xFFF;

        /* Sign extend from 20 bit CCPU to 32bit target machine */
        FromX = SEX(FromX);
        ToX = SEX(ToX);
        FromY = SEX(FromY);
        ToY = SEX(ToY);

        /* figure out the vector */
        ToX -= FromX;
        ToX = ((signed short int)(((signed short int)ToX) >> (signed short int)vgShiftLength)) /* SAR */;
        ToX += FromX;

        ToY -= FromY;
        ToY = ((signed short int)(((signed short int)ToY) >> (signed short int)vgShiftLength)) /* SAR */;
        ToY += FromY;

        /* render the line */
#ifndef DUALCPU
        CinemaVectorData (FromX, FromY, ToX, ToY, vgColour);
#endif

      }
      /* opLDJimm_A_A (44) */
      register_J = 0x07a4;
      /* opJMP_A_A (58) */
      {
        register_PC = 0x07a4;
        break;
      };

    case 0x0724:

      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opADDimm_A_AA (21) */
      cmp_new = 0x1; cmp_old = acc_a0 = register_A; register_A = flag_C = 0x0001;

      /* opOUTbi_A_A (96) */
      vgColour = 0x0f;
      /* opLDPimm_A_A (80) */
      register_P = 0x0; /* set page register */
      /* opLDAdir_A_AA (aa) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = 0x0a]; /* set I register */

      /* opLDPimm_A_A (86) */
      register_P = 0x6; /* set page register */
      /* opSUBdir_A_AA (73) */
      cmp_old = acc_a0 = register_A;
      register_A = (flag_C = (register_A + ((cmp_new = ram[register_I = 0x63]) ^ 0xFFF) + 1)) & 0xFFF; /* set regI addr */
      /* opASRe_A_AA (ed) */
      cmp_new = 0xDED; cmp_old = flag_C = acc_a0 = register_A;
      register_A = SEX(register_A); /* make signed */
      register_A = (((signed short int)(((signed short int)register_A) >> (signed short int)1)) /* SAR */) & 0xFFF;
      /* opLDPimm_A_A (80) */
      register_P = 0x0; /* set page register */
      /* opSTAdir_A_A (d7) */
      ram[register_I = 0x07] = register_A; /* store acc to RAM */
      /* opLDAdir_A_AA (ab) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = 0x0b]; /* set I register */

      /* opLDPimm_A_A (86) */
      register_P = 0x6; /* set page register */
      /* opSUBdir_A_AA (74) */
      cmp_old = acc_a0 = register_A;
      register_A = (flag_C = (register_A + ((cmp_new = ram[register_I = 0x64]) ^ 0xFFF) + 1)) & 0xFFF; /* set regI addr */
      /* opASRe_A_AA (ed) */
      cmp_new = 0xDED; cmp_old = flag_C = acc_a0 = register_A;
      register_A = SEX(register_A); /* make signed */
      register_A = (((signed short int)(((signed short int)register_A) >> (signed short int)1)) /* SAR */) & 0xFFF;
      /* opLDPimm_A_A (80) */
      register_P = 0x0; /* set page register */
      /* opSTAdir_A_A (d8) */
      ram[register_I = 0x08] = register_A; /* store acc to RAM */
      /* opLDAdir_A_AA (a7) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = 0x07]; /* set I register */

      /* opASRe_A_AA (ed) */
      cmp_new = 0xDED; cmp_old = flag_C = acc_a0 = register_A;
      register_A = SEX(register_A); /* make signed */
      register_A = (((signed short int)(((signed short int)register_A) >> (signed short int)1)) /* SAR */) & 0xFFF;
      /* opASRe_A_AA (ed) */
      cmp_new = 0xDED; cmp_old = flag_C = acc_a0 = register_A;
      register_A = SEX(register_A); /* make signed */
      register_A = (((signed short int)(((signed short int)register_A) >> (signed short int)1)) /* SAR */) & 0xFFF;
      /* opSTAdir_A_A (de) */
      ram[register_I = 0x0e] = register_A; /* store acc to RAM */
      /* opLDAimm_A_AA (07) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0x0700;
      /* opADDimmX_A_AA (20) */
      cmp_old = register_A; cmp_new = 0xa4;
      register_A = flag_C = acc_a0 = 0x07a4;

    case 0x073b:

      /* opSTAdir_A_A (d1) */
      ram[register_I = (register_P << 4) + 0x1] = register_A; /* store acc to RAM */
      /* opLDAdir_A_AA (aa) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = (register_P << 4) + 0x0a]; /* set I register */

      /* opSTAdir_A_A (d2) */
      ram[register_I = (register_P << 4) + 0x2] = register_A; /* store acc to RAM */
      /* opLDAdir_A_AA (ab) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = (register_P << 4) + 0x0b]; /* set I register */

      /* opSTAdir_A_A (d3) */
      ram[register_I = (register_P << 4) + 0x3] = register_A; /* store acc to RAM */
      /* opADDdir_A_AA (68) */
      register_A = (flag_C = ((cmp_old = acc_a0 = register_A) + (cmp_new = ram[register_I = (register_P << 4) + 0x8]))) & 0xFFF; /* do acc operation */
      /* opSTAdir_A_A (d9) */
      ram[register_I = (register_P << 4) + 0x9] = register_A; /* store acc to RAM */
      /* opLDAdir_A_AA (aa) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = (register_P << 4) + 0x0a]; /* set I register */

      /* opADDdir_A_AA (67) */
      register_A = (flag_C = ((cmp_old = acc_a0 = register_A) + (cmp_new = ram[register_I = (register_P << 4) + 0x7]))) & 0xFFF; /* do acc operation */
      /* opADDdir_A_AA (6e) */
      register_A = (flag_C = ((cmp_old = acc_a0 = register_A) + (cmp_new = ram[register_I = (register_P << 4) + 0xe]))) & 0xFFF; /* do acc operation */
      /* opSTAdir_A_A (d4) */
      ram[register_I = (register_P << 4) + 0x4] = register_A; /* store acc to RAM */
      /* opLDAimm_A_AA (07) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0x0700;
      /* opADDimmX_A_AA (20) */
      cmp_old = register_A; cmp_new = 0x81;
      register_A = flag_C = acc_a0 = 0x0781;
      /* opSTAdir_A_A (dc) */
      ram[register_I = (register_P << 4) + 0xc] = 0x781; /* store acc to RAM */
      /* opLDAdir_A_AA (a2) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = (register_P << 4) + 0x02]; /* set I register */

      /* opNOP_A_B (57) */
      /* opLDAdir_B_AA (a3) */
      flag_C = acc_a0 = register_A;
      cmp_old = register_B; /* store old acc */
      register_B = cmp_new = ram[register_I = (register_P << 4) + 0x03]; /* new acc value */
      /* opLDJimm_A_A (4f) */
      register_J = 0x074f;
      /* opJDR_A_A (5a) */
      /* opVIN_A_A (f0) */

      FromX = register_A & 0xFFF; /* regA goes to x-coord */
      FromY = register_B & 0xFFF; /* regB goes to y-coord */

      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opSUBimmX_A_AA (30) */
      cmp_old = acc_a0 = register_A; /* back up regA */
      register_A = (flag_C = (register_A + (((cmp_new = 0x0b) ^ 0xFFF) + 1))) & 0xFFF; /* add */

    case 0x0754:

      /* opADDimm_A_AA (21) */
      register_A = (flag_C = ((cmp_old = acc_a0 = register_A) + (cmp_new = 0x1))) & 0xFFF; /* add values, save carry */

      /* opLDJimm_A_A (44) */
      register_J = 0x0754;
      /* opJNC_A_AA (5d) */
      if (!(flag_C & CARRYBIT)) {
        register_PC = 0x0754;
        break;
      };
      /* opLDAdir_A_AA (a9) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = (register_P << 4) + 0x09]; /* set I register */

      /* opSUBdir_A_AA (73) */
      cmp_old = acc_a0 = register_A;
      register_A = (flag_C = (register_A + ((cmp_new = ram[register_I = (register_P << 4) + 0x3]) ^ 0xFFF) + 1)) & 0xFFF; /* set regI addr */
      /* opSTAdir_A_A (dd) */
      ram[register_I = (register_P << 4) + 0xd] = register_A; /* store acc to RAM */
      /* opNOP_A_B (57) */
      /* opLDAirg_B_AA (ea) */
      flag_C = acc_a0 = register_A;
      cmp_old = register_B;
      register_B = cmp_new = ram[register_I];

      /* opLDAimm_A_AA (08) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0x0800;
      /* opLDAirg_A_AA (ea) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = ram[register_I];
      /* opLDJimm_A_A (44) */
      register_J = 0x0764;
      /* opJLT_A_A (5b) */
      if (cmp_new < cmp_old) {
        register_PC = 0x0764;
        break;
      };
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opSUBirg_A_AA (e8) */
      register_A = (flag_C = ((cmp_old = acc_a0 = register_A) + ((cmp_new = ram[register_I]) ^ 0xFFF) + 1)) & 0xFFF; /* ones compliment */

    case 0x0764:

      /* opSTAirg_A_A (e6) */
      ram[register_I] = register_A; /* store acc */
      /* opLDAdir_A_AA (a4) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = (register_P << 4) + 0x04]; /* set I register */

      /* opSUBdir_A_AA (72) */
      cmp_old = acc_a0 = register_A;
      register_A = (flag_C = (register_A + ((cmp_new = ram[register_I = (register_P << 4) + 0x2]) ^ 0xFFF) + 1)) & 0xFFF; /* set regI addr */
      /* opSTAdir_A_A (df) */
      ram[register_I = (register_P << 4) + 0xf] = register_A; /* store acc to RAM */
      /* opLDAimm_A_AA (08) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0x0800;
      /* opLDAirg_A_AA (ea) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = ram[register_I];
      /* opLDJimm_A_A (4f) */
      register_J = 0x076f;
      /* opJLT_A_A (5b) */
      if (cmp_new < cmp_old) {
        register_PC = 0x076f;
        break;
      };
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opSUBirg_A_AA (e8) */
      register_A = (flag_C = ((cmp_old = acc_a0 = register_A) + ((cmp_new = ram[register_I]) ^ 0xFFF) + 1)) & 0xFFF; /* ones compliment */

    case 0x076f:

      /* opADDdir_A_AA (6d) */
      register_A = (flag_C = ((cmp_old = acc_a0 = register_A) + (cmp_new = ram[register_I = (register_P << 4) + 0xd]))) & 0xFFF; /* do acc operation */
      /* opSUBimm_A_AA (33) */
      cmp_old = acc_a0 = register_A;
      register_A = (flag_C = (register_A + (((cmp_new = 0x3) ^ 0xFFF) + 1))) & 0xFFF; /* 1's-comp add */
      /* opLDJimm_A_A (4a) */
      register_J = 0x077a;
      /* opJNC_A_AA (5d) */
      if (!(flag_C & CARRYBIT)) {
        register_PC = 0x077a;
        break;
      };
      /* opLDAdir_A_AA (af) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = (register_P << 4) + 0x0f]; /* set I register */

      /* opLLT_A_AA (e4) */
      { CINEBYTE temp_byte = 0;
        for (;;) {
          if (   (((register_A >> 8) & 0x0A) && (((register_A >> 8) & 0x0A) ^ 0x0A))
                 ||   (((register_B >> 8) & 0x0A) && (((register_B >> 8) & 0x0A) ^ 0x0A))  ) break;
          register_A <<= 1; register_B <<= 1;
          if (!(++temp_byte)) break /* This may not be correct */;
        }
        vgShiftLength = temp_byte & 0xfff; register_A &= 0x0FFF; register_B &= 0x0FFF;
      }
      /* opADDdir_A_AA (62) */
      register_A = (flag_C = ((cmp_old = acc_a0 = register_A) + (cmp_new = ram[register_I = (register_P << 4) + 0x2]))) & 0xFFF; /* do acc operation */
      /* opNOP_A_B (57) */
      /* opADDdir_B_AA (63) */
      acc_a0 = register_A; /* store old acc value */
      register_B = (flag_C = ((cmp_old = register_B) + (cmp_new = ram[register_I = (register_P << 4) + 0x03]))) & 0xFFF; /* do acc operation */
      /* opVDR_A_A (e0) */
      {
        /* set ending points and draw the vector, or buffer for a later draw. */
        int ToX = register_A & 0xFFF;
        int ToY = register_B & 0xFFF;

        /* Sign extend from 20 bit CCPU to 32bit target machine */
        FromX = SEX(FromX);
        ToX = SEX(ToX);
        FromY = SEX(FromY);
        ToY = SEX(ToY);

        /* figure out the vector */
        ToX -= FromX;
        ToX = ((signed short int)(((signed short int)ToX) >> (signed short int)vgShiftLength)) /* SAR */;
        ToX += FromX;

        ToY -= FromY;
        ToY = ((signed short int)(((signed short int)ToY) >> (signed short int)vgShiftLength)) /* SAR */;
        ToY += FromY;

        /* render the line */
#ifndef DUALCPU
        CinemaVectorData (FromX, FromY, ToX, ToY, vgColour);
#endif

      }

    case 0x077a:

      /* opLDAdir_A_AA (a4) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = (register_P << 4) + 0x04]; /* set I register */

      /* opSTAdir_A_A (d2) */
      ram[register_I = (register_P << 4) + 0x2] = register_A; /* store acc to RAM */
      /* opLDAdir_A_AA (a9) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = (register_P << 4) + 0x09]; /* set I register */

      /* opSTAdir_A_A (d3) */
      ram[register_I = (register_P << 4) + 0x3] = register_A; /* store acc to RAM */
      /* opCMPdir_A_AA (bc) */
      cmp_old = acc_a0 = register_A; /* backup old acc */
      flag_C = (((cmp_new = ram[register_I = (register_P << 4) + 0xc]) ^ 0xFFF) + 1 + register_A);
      /* opLDJirg_A_A (e1) */
      /* load J reg from value at last dir addr */
      register_J = ram[register_I];
      /* opJMP_A_A (58) */
      {
        register_PC = register_J;
        break;
      };

    case 0x07a4:

      /* Invariants: register_P = 0x0 register_I = 0x06 */;
      /* opLDAimm_A_AA (04) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0x0400;
      /* opLDPimm_A_A (86) */
      register_P = 0x6; /* set page register */
      /* opSUBdir_A_AA (73) */
      cmp_old = acc_a0 = register_A;
      register_A = (flag_C = (register_A + ((cmp_new = ram[register_I = 0x63]) ^ 0xFFF) + 1)) & 0xFFF; /* set regI addr */
      /* opSTAdir_A_A (d5) */
      ram[register_I = 0x65] = register_A; /* store acc to RAM */
      /* opNOP_A_B (57) */
      /* opLDAimm_B_AA (00) */
      flag_C = acc_a0 = register_A;
      cmp_old = register_B; /* step back cmp flag */
      register_B = cmp_new = 0;
      /* opLDAdir_A_AA (a2) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = 0x62]; /* set I register */

      /* opCMPdir_A_AA (b5) */
      cmp_old = acc_a0 = register_A; /* backup old acc */
      flag_C = (((cmp_new = ram[register_I = 0x65]) ^ 0xFFF) + 1 + register_A);
      /* opMULirg_A_AA (e3) */
      MUL();
      /* opMULirg_A_AA (e3) */
      MUL();
      /* opMULirg_A_AA (e3) */
      MUL();
      /* opMULirg_A_AA (e3) */
      MUL();
      /* opASRDe_A_AA (ee) */
      cmp_new = 0x0EEE; cmp_old = acc_a0 = register_A; flag_C = (0x0EEE + register_A);
      register_A = (register_A >> 1) | ((register_B & 1) << 11);
      register_B = (register_B >> 1) | (register_B & 0x800);
      /* opNOP_A_B (57) */
      /* opSTAdir_B_BB (d5) */
      ram[register_I = (register_P << 4) + 0x5] = register_B; /* set I register and store B to ram */
      /* opLDAirg_A_AA (ea) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = ram[register_I];
      /* opADDdir_A_AA (63) */
      register_A = (flag_C = ((cmp_old = acc_a0 = register_A) + (cmp_new = ram[register_I = 0x63]))) & 0xFFF; /* do acc operation */
      /* opLDPimm_A_A (80) */
      register_P = 0x0; /* set page register */
      /* opSTAdir_A_A (da) */
      ram[register_I = 0x0a] = register_A; /* store acc to RAM */
      /* opNOP_A_B (57) */
      /* opLDAdir_B_AA (ab) */
      flag_C = acc_a0 = register_A;
      cmp_old = register_B; /* store old acc */
      register_B = cmp_new = ram[register_I = (register_P << 4) + 0x0b]; /* new acc value */
      /* opLDJimm_A_A (4b) */
      register_J = 0x07bb;
      /* opJDR_A_A (5a) */
      /* opVIN_A_A (f0) */

      FromX = register_A & 0xFFF; /* regA goes to x-coord */
      FromY = register_B & 0xFFF; /* regB goes to y-coord */


    case 0x07bd:

      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opLDPimm_A_A (85) */
      register_P = 0x5; /* set page register */
      /* opCMPdir_A_AA (b0) */
      cmp_old = acc_a0 = register_A; /* backup old acc */
      flag_C = (((cmp_new = ram[register_I = 0x50]) ^ 0xFFF) + 1 + register_A);
      /* opLDJimm_A_A (46) */
      register_J = 0x07e6;
      /* opJEQ_A_AA (5c) */
      if (cmp_new == cmp_old) {
        register_PC = 0x07e6;
        break;
      };
      /* opCMPdir_A_AA (b7) */
      cmp_old = acc_a0 = register_A; /* backup old acc */
      flag_C = (((cmp_new = ram[register_I = 0x57]) ^ 0xFFF) + 1 + register_A);
      /* opLDJimm_A_A (40) */
      register_J = 0x07d0;
      /* opJNC_A_AA (5d) */
      if (!(flag_C & CARRYBIT)) {
        register_PC = 0x07d0;
        break;
      };
      /* opCMPdir_A_AA (bd) */
      cmp_old = acc_a0 = register_A; /* backup old acc */
      flag_C = (((cmp_new = ram[register_I = 0x5d]) ^ 0xFFF) + 1 + register_A);
      /* opJEQ_A_AA (5c) */
      if (cmp_new == cmp_old) {
        register_PC = 0x07d0;
        break;
      };
      /* opSTAdir_A_A (d6) */
      ram[register_I = 0x56] = 0x000; /* store acc to RAM */
      /* opLDAdir_A_AA (a4) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = 0x54]; /* set I register */

      /* opSUBimm_A_AA (32) */
      cmp_old = acc_a0 = register_A;
      register_A = (flag_C = (register_A + (((cmp_new = 0x2) ^ 0xFFF) + 1))) & 0xFFF; /* 1's-comp add */
      /* opSTAirg_A_A (e6) */
      ram[0x54] = register_A; /* store acc */
      /* opLDAdir_A_AA (a5) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = 0x55]; /* set I register */

      /* opADDimm_A_AA (21) */
      register_A = (flag_C = ((cmp_old = acc_a0 = register_A) + (cmp_new = 0x1))) & 0xFFF; /* add values, save carry */

      /* opSTAirg_A_A (e6) */
      ram[0x55] = register_A; /* store acc */

    case 0x07d0:

      /* Invariants: register_P = 0x5 register_A = 0x00 */;
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opADDimm_A_AA (22) */
      cmp_new = 0x2; cmp_old = acc_a0 = register_A; register_A = flag_C = 0x0002;

      /* opCMPdir_A_AA (b7) */
      cmp_old = acc_a0 = register_A; /* backup old acc */
      flag_C = (((cmp_new = ram[register_I = 0x57]) ^ 0xFFF) + 1 + register_A);
      /* opLDJimm_A_A (43) */
      register_J = 0x07e3;
      /* opJNC_A_AA (5d) */
      if (!(flag_C & CARRYBIT)) {
        register_PC = 0x07e3;
        break;
      };
      /* opLDAimm_A_AA (07) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0x0700;
      /* opADDimmX_A_AA (20) */
      cmp_old = register_A; cmp_new = 0xed;
      register_A = flag_C = acc_a0 = 0x07ed;
      /* opLDPimm_A_A (80) */
      register_P = 0x0; /* set page register */
      /* opSTAdir_A_A (d0) */
      ram[register_I = 0x00] = 0x7ed; /* store acc to RAM */
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opADDimmX_A_AA (20) */
      cmp_old = register_A; cmp_new = 0x51;
      register_A = flag_C = acc_a0 = 0x0051;
      /* opLDPimm_A_A (81) */
      register_P = 0x1; /* set page register */
      /* opSTAdir_A_A (d9) */
      ram[register_I = 0x19] = 0x051; /* store acc to RAM */
      /* opLDJimm_A_A (40) */
      register_J = 0x0c00;
      /* opJMP_A_A (58) */
      {
        register_PC = 0x0c00;
        break;
      };

    case 0x07e3:

      /* Invariants: register_P = 0x5 register_I = 0x57 register_A = 0x02 */;
      /* opLDAdir_A_AA (a7) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = 0x57]; /* set I register */

      /* opADDimm_A_AA (21) */
      register_A = (flag_C = ((cmp_old = acc_a0 = register_A) + (cmp_new = 0x1))) & 0xFFF; /* add values, save carry */

      /* opSTAirg_A_A (e6) */
      ram[0x57] = register_A; /* store acc */

    case 0x07e6:

      /* Invariants: register_P = 0x5 */;
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opSUBimmX_A_AA (30) */
      cmp_old = acc_a0 = register_A; /* back up regA */
      register_A = (flag_C = (register_A + (((cmp_new = 0x64) ^ 0xFFF) + 1))) & 0xFFF; /* add */

    case 0x07e9:

      /* Invariants: register_P = 0x5 */;
      /* opADDimm_A_AA (21) */
      register_A = (flag_C = ((cmp_old = acc_a0 = register_A) + (cmp_new = 0x1))) & 0xFFF; /* add values, save carry */

      /* opLDJimm_A_A (49) */
      register_J = 0x07e9;
      /* opJNC_A_AA (5d) */
      if (!(flag_C & CARRYBIT)) {
        register_PC = 0x07e9;
        break;
      };

    case 0x07ed:

      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opLDPimm_A_A (86) */
      register_P = 0x6; /* set page register */
      /* opCMPdir_A_AA (b2) */
      cmp_old = acc_a0 = register_A; /* backup old acc */
      flag_C = (((cmp_new = ram[register_I = 0x62]) ^ 0xFFF) + 1 + register_A);
      /* opLDJimm_A_A (45) */
      register_J = 0x0855;
      /* opJEQ_A_AA (5c) */
      if (cmp_new == cmp_old) {
        register_PC = 0x0855;
        break;
      };
      /* opINP_A_AA (1d) */
      cmp_old = flag_C = acc_a0 = register_A;
#ifdef RAWIO
      register_A = cmp_new = get_io_bit(0xd);
#else
      register_A = cmp_new = get_io_bit(0xd);
#endif
      /* opADDimm_A_AA (21) */
      register_A = (flag_C = ((cmp_old = acc_a0 = register_A) + (cmp_new = 0x1))) & 0xFFF; /* add values, save carry */

      /* opLDJimm_A_A (4e) */
      register_J = 0x083e;
      /* opJEQ_A_AA (5c) */
      if (cmp_new == cmp_old) {
        register_PC = 0x083e;
        break;
      };
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opADDimmX_A_AA (20) */
      cmp_old = register_A; cmp_new = 0x40;
      register_A = flag_C = acc_a0 = 0x0040;
      /* opLDPimm_A_A (80) */
      register_P = 0x0; /* set page register */
      /* opSTAdir_A_A (d5) */
      ram[register_I = 0x05] = 0x040; /* store acc to RAM */
      /* opNOP_A_B (57) */
      /* opLDAimm_B_AA (00) */
      flag_C = acc_a0 = register_A;
      cmp_old = register_B; /* step back cmp flag */
      register_B = cmp_new = 0;
      /* opLDPimm_A_A (86) */
      register_P = 0x6; /* set page register */
      /* opLDAdir_A_AA (a2) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = 0x62]; /* set I register */

      /* opLDPimm_A_A (80) */
      register_P = 0x0; /* set page register */
      /* opCMPdir_A_AA (b5) */
      cmp_old = acc_a0 = register_A; /* backup old acc */
      flag_C = (((cmp_new = ram[register_I = 0x05]) ^ 0xFFF) + 1 + register_A);
      /* opMULirg_A_AA (e3) */
      MUL();
      /* opMULirg_A_AA (e3) */
      MUL();
      /* opMULirg_A_AA (e3) */
      MUL();
      /* opMULirg_A_AA (e3) */
      MUL();
      /* opASRDe_A_AA (ee) */
      cmp_new = 0x0EEE; cmp_old = acc_a0 = register_A; flag_C = (0x0EEE + register_A);
      register_A = (register_A >> 1) | ((register_B & 1) << 11);
      register_B = (register_B >> 1) | (register_B & 0x800);
      /* opNOP_A_B (57) */
      /* opADDdir_B_AA (6a) */
      acc_a0 = register_A; /* store old acc value */
      register_B = (flag_C = ((cmp_old = register_B) + (cmp_new = ram[register_I = 0x0a]))) & 0xFFF; /* do acc operation */
      /* opNOP_A_B (57) */
      /* opSTAdir_B_BB (d5) */
      ram[register_I = (register_P << 4) + 0x5] = register_B; /* set I register and store B to ram */
      /* opLDAdir_A_AA (ab) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = 0x0b]; /* set I register */

      /* opSTAdir_A_A (d6) */
      ram[register_I = 0x06] = register_A; /* store acc to RAM */
      /* opNOP_A_B (57) */
      /* opLDAirg_B_AA (ea) */
      flag_C = acc_a0 = register_A;
      cmp_old = register_B;
      register_B = cmp_new = ram[register_I];

      /* opLDAdir_A_AA (a5) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = 0x05]; /* set I register */

      /* opVIN_A_A (f0) */

      FromX = register_A & 0xFFF; /* regA goes to x-coord */
      FromY = register_B & 0xFFF; /* regB goes to y-coord */

      /* opLDPimm_A_A (80) */
      /* opLDAdir_A_AA (a5) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = 0x05]; /* set I register */

      /* opLDPimm_A_A (86) */
      register_P = 0x6; /* set page register */
      /* opSUBdir_A_AA (73) */
      cmp_old = acc_a0 = register_A;
      register_A = (flag_C = (register_A + ((cmp_new = ram[register_I = 0x63]) ^ 0xFFF) + 1)) & 0xFFF; /* set regI addr */
      /* opASRe_A_AA (ed) */
      cmp_new = 0xDED; cmp_old = flag_C = acc_a0 = register_A;
      register_A = SEX(register_A); /* make signed */
      register_A = (((signed short int)(((signed short int)register_A) >> (signed short int)1)) /* SAR */) & 0xFFF;
      /* opLDPimm_A_A (80) */
      register_P = 0x0; /* set page register */
      /* opSTAdir_A_A (d7) */
      ram[register_I = 0x07] = register_A; /* store acc to RAM */
      /* opLDAimm_A_AA (08) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0x0800;
      /* opLDAirg_A_AA (ea) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = ram[register_I];
      /* opLDJimm_A_A (40) */
      register_J = 0x0820;
      /* opJLT_A_A (5b) */
      if (cmp_new < cmp_old) {
        register_PC = 0x0820;
        break;
      };
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opSUBirg_A_AA (e8) */
      register_A = (flag_C = ((cmp_old = acc_a0 = register_A) + ((cmp_new = ram[register_I]) ^ 0xFFF) + 1)) & 0xFFF; /* ones compliment */
      register_PC = 0x0820;
      break;

    /*********************************************************/
    default:
      /* Jumping to any illegal address or end of eprom will come here */
      /* we ought to reinitialise or something */
      ;
  }
}

// ops1000.c

void cineExecute1000(void)
{
  switch (register_PC)
  {
    case 0x1000:

      state = state_A; /* Even if it's not! :-) */
      /* opNOP_A_A (5f) */
      /* opNOP_A_A (5f) */
      /* opLDJimm_A_A (40) */
      register_J = 0x0d70;
      /* opJMP_A_A (58) */
      {
        register_PC = 0x1d70;
        break;
      };

    case 0x1005:

      /* opWAI_A_A (e5) */
      /* wait for a tick on the watchdog */
      CinemaClearScreen();
      bNewFrame = 1;
      register_PC = 0x1006;
      break;

    case 0x1006:

      /* opWAI_A_A (e5) */
      /* wait for a tick on the watchdog */

      register_PC = 0x1007;

    case 0x1007:

      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opOUTbi_A_A (96) */
      vgColour = 0x07;
      /* opLDPimm_A_A (81) */
      register_P = 0x1; /* set page register */
      /* opSTAdir_A_A (d0) */
      ram[register_I = 0x10] = 0x000; /* store acc to RAM */
      /* opSTAdir_A_A (d1) */
      ram[register_I = 0x11] = 0x000; /* store acc to RAM */
      /* opAWDirg_A_AA (f7) */
      cmp_old = acc_a0 = register_A;
      register_A = (flag_C = (register_A + (cmp_new = ram[0x11]))) & 0xFFF;
      set_watchdog();
      /* opNOP_A_B (57) */
      /* opLDAimm_B_AA (03) */
      flag_C = acc_a0 = register_A;
      cmp_old = register_B; /* step back cmp flag */
      register_B = cmp_new = 0x0300;
      /* opASRDe_A_AA (ee) */
      cmp_new = 0x0EEE; cmp_old = acc_a0 = register_A; flag_C = (0x0EEE + register_A);
      register_A = (register_A >> 1) | ((register_B & 1) << 11);
      register_B = (register_B >> 1) | (register_B & 0x800);
      /* opLDAimm_A_AA (02) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0x0200;
      /* opVIN_A_A (f0) */

      FromX = register_A & 0xFFF; /* regA goes to x-coord */
      FromY = register_B & 0xFFF; /* regB goes to y-coord */

      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opSUBimmX_A_AA (30) */
      cmp_old = acc_a0 = register_A; /* back up regA */
      register_A = (flag_C = (register_A + (((cmp_new = 0x38) ^ 0xFFF) + 1))) & 0xFFF; /* add */

    case 0x1015:

      /* opADDimm_A_AA (21) */
      register_A = (flag_C = ((cmp_old = acc_a0 = register_A) + (cmp_new = 0x1))) & 0xFFF; /* add values, save carry */

      /* opLDJimm_A_A (45) */
      register_J = 0x0015;
      /* opJNC_A_AA (5d) */
      if (!(flag_C & CARRYBIT)) {
        register_PC = 0x1015;
        break;
      };
      /* opLDAimm_A_AA (01) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0x0100;
      /* opSTAdir_A_A (d2) */
      ram[register_I = (register_P << 4) + 0x2] = 0x100; /* store acc to RAM */
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opSTAdir_A_A (dc) */
      ram[register_I = (register_P << 4) + 0xc] = 0x000; /* store acc to RAM */
      /* opSTAdir_A_A (da) */
      ram[register_I = (register_P << 4) + 0xa] = 0x000; /* store acc to RAM */
      /* opADDimmX_A_AA (20) */
      cmp_old = register_A; cmp_new = 0xff;
      register_A = flag_C = acc_a0 = 0x00ff;
      /* opSTAdir_A_A (dd) */
      ram[register_I = (register_P << 4) + 0xd] = 0x0ff; /* store acc to RAM */
      /* opSTAdir_A_A (db) */
      ram[register_I = (register_P << 4) + 0xb] = 0x0ff; /* store acc to RAM */
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opADDimmX_A_AA (20) */
      cmp_old = register_A; cmp_new = 0x31;
      register_A = flag_C = acc_a0 = 0x0031;
      /* opSTAdir_A_A (d6) */
      ram[register_I = (register_P << 4) + 0x6] = 0x031; /* store acc to RAM */
      /* opADDimmX_A_AA (20) */
      cmp_old = register_A; cmp_new = 0x2e;
      register_A = flag_C = acc_a0 = 0x005f;
      /* opSTAdir_A_A (d7) */
      ram[register_I = (register_P << 4) + 0x7] = 0x05f; /* store acc to RAM */
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opADDimmX_A_AA (20) */
      cmp_old = register_A; cmp_new = 0x05;
      register_A = flag_C = acc_a0 = 0x0005;
      /* opLDPimm_A_A (82) */
      register_P = 0x2; /* set page register */
      /* opSTAdir_A_A (df) */
      ram[register_I = 0x2f] = 0x005; /* store acc to RAM */
      /* opLDJimm_A_A (48) */
      register_J = 0x0128;
      /* opJMP_A_A (58) */
      {
        register_PC = 0x1128;
        break;
      };

    case 0x1072:

      /* Invariants: register_P = 0x1 register_I = 0x19 */;
      /* opLDAirg_A_AA (ea) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = ram[register_I];
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opOUTbi_A_A (96) */
      vgColour = 0x07;
      /* opLDJimm_A_A (48) */
      register_J = 0x0128;
      /* opJEQ_A_AA (5c) */
      if (cmp_new == cmp_old) {
        register_PC = 0x1128;
        break;
      };
      /* opADDimm_A_AA (21) */
      cmp_new = 0x1; cmp_old = acc_a0 = register_A; register_A = flag_C = 0x0001;

      /* opOUTbi_A_A (96) */
      vgColour = 0x0f;
      /* opJMP_A_A (58) */
      {
        register_PC = 0x1128;
        break;
      };

    case 0x1080:

      /* Invariants: register_P = 0x2 register_I = 0x00 register_A = 0x600 */;
      /* opNOP_A_A (5f) */
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opLDPimm_A_A (83) */
      register_P = 0x3; /* set page register */
      /* opCMPdir_A_AA (b0) */
      cmp_old = acc_a0 = register_A; /* backup old acc */
      flag_C = (((cmp_new = ram[register_I = 0x30]) ^ 0xFFF) + 1 + register_A);
      /* opLDJimm_A_A (44) */
      register_J = 0x0094;
      /* opJEQ_A_AA (5c) */
      if (cmp_new == cmp_old) {
        register_PC = 0x1094;
        break;
      };
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opADDimmX_A_AA (20) */
      cmp_old = register_A; cmp_new = 0x3b;
      register_A = flag_C = acc_a0 = 0x003b;
      /* opLDPimm_A_A (81) */
      register_P = 0x1; /* set page register */
      /* opSTAdir_A_A (d9) */
      ram[register_I = 0x19] = 0x03b; /* store acc to RAM */
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opADDimmX_A_AA (20) */
      cmp_old = register_A; cmp_new = 0x94;
      register_A = flag_C = acc_a0 = 0x0094;
      /* opLDPimm_A_A (82) */
      register_P = 0x2; /* set page register */
      /* opSTAdir_A_A (df) */
      ram[register_I = 0x2f] = 0x094; /* store acc to RAM */
      /* opLDJimm_A_A (47) */
      register_J = 0x00b7;
      /* opJMP_A_A (58) */
      {
        register_PC = 0x10b7;
        break;
      };

    case 0x1094:

      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opLDPimm_A_A (84) */
      register_P = 0x4; /* set page register */
      /* opCMPdir_A_AA (b0) */
      cmp_old = acc_a0 = register_A; /* backup old acc */
      flag_C = (((cmp_new = ram[register_I = 0x40]) ^ 0xFFF) + 1 + register_A);
      /* opLDJimm_A_A (47) */
      register_J = 0x00a7;
      /* opJEQ_A_AA (5c) */
      if (cmp_new == cmp_old) {
        register_PC = 0x10a7;
        break;
      };
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opADDimmX_A_AA (20) */
      cmp_old = register_A; cmp_new = 0x4b;
      register_A = flag_C = acc_a0 = 0x004b;
      /* opLDPimm_A_A (81) */
      register_P = 0x1; /* set page register */
      /* opSTAdir_A_A (d9) */
      ram[register_I = 0x19] = 0x04b; /* store acc to RAM */
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opADDimmX_A_AA (20) */
      cmp_old = register_A; cmp_new = 0xa7;
      register_A = flag_C = acc_a0 = 0x00a7;
      /* opLDPimm_A_A (82) */
      register_P = 0x2; /* set page register */
      /* opSTAdir_A_A (df) */
      ram[register_I = 0x2f] = 0x0a7; /* store acc to RAM */
      /* opLDJimm_A_A (47) */
      register_J = 0x00b7;
      /* opJMP_A_A (58) */
      {
        register_PC = 0x10b7;
        break;
      };

    case 0x10a7:

      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opLDPimm_A_A (85) */
      register_P = 0x5; /* set page register */
      /* opCMPdir_A_AA (b0) */
      cmp_old = acc_a0 = register_A; /* backup old acc */
      flag_C = (((cmp_new = ram[register_I = 0x50]) ^ 0xFFF) + 1 + register_A);
      /* opLDJimm_A_A (4d) */
      register_J = 0x047d;
      /* opJEQ_A_AA (5c) */
      if (cmp_new == cmp_old) {
        register_PC = 0x147d;
        break;
      };
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opADDimmX_A_AA (20) */
      cmp_old = register_A; cmp_new = 0x5b;
      register_A = flag_C = acc_a0 = 0x005b;
      /* opLDPimm_A_A (81) */
      register_P = 0x1; /* set page register */
      /* opSTAdir_A_A (d9) */
      ram[register_I = 0x19] = 0x05b; /* store acc to RAM */
      /* opLDAimm_A_AA (04) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0x0400;
      /* opADDimmX_A_AA (20) */
      cmp_old = register_A; cmp_new = 0x7d;
      register_A = flag_C = acc_a0 = 0x047d;
      /* opLDPimm_A_A (82) */
      register_P = 0x2; /* set page register */
      /* opSTAdir_A_A (df) */
      ram[register_I = 0x2f] = 0x47d; /* store acc to RAM */

    case 0x10b7:

      /* Invariants: register_P = 0x2 register_I = 0x2f */;
      /* opLDPimm_A_A (81) */
      register_P = 0x1; /* set page register */
      /* opLDIdir_A_A (c9) */
      register_I = ram[0x19] & 0xff; /* set new register_I (8 bits) */
      /* opLDAirg_A_AA (ea) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = ram[register_I];
      /* opLDPimm_A_A (82) */
      register_P = 0x2; /* set page register */
      /* opSTAdir_A_A (d6) */
      ram[register_I = 0x26] = register_A; /* store acc to RAM */
      /* opLDPimm_A_A (81) */
      register_P = 0x1; /* set page register */
      /* opSTAdir_A_A (d3) */
      ram[register_I = 0x13] = register_A; /* store acc to RAM */
      /* opLDAimm_A_AA (04) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0x0400;
      /* opCMPdir_A_AA (b3) */
      cmp_old = acc_a0 = register_A; /* backup old acc */
      flag_C = (((cmp_new = ram[register_I = 0x13]) ^ 0xFFF) + 1 + register_A);
      /* opLDJimm_A_A (49) */
      register_J = 0x0479;
      /* opJNC_A_AA (5d) */
      if (!(flag_C & CARRYBIT)) {
        register_PC = 0x1479;
        break;
      };
      /* opLDAdir_A_AA (a9) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = 0x19]; /* set I register */

      /* opADDimm_A_AA (21) */
      register_A = (flag_C = ((cmp_old = acc_a0 = register_A) + (cmp_new = 0x1))) & 0xFFF; /* add values, save carry */

      /* opSTAirg_A_A (e6) */
      ram[0x19] = register_A; /* store acc */
      /* opLDIdir_A_A (c9) */
      register_I = ram[0x19] & 0xff; /* set new register_I (8 bits) */
      /* opLDAirg_A_AA (ea) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = ram[register_I];
      /* opLDPimm_A_A (82) */
      register_P = 0x2; /* set page register */
      /* opSTAdir_A_A (d7) */
      ram[register_I = 0x27] = register_A; /* store acc to RAM */
      /* opNOP_A_B (57) */
      /* opLDAirg_B_AA (ea) */
      flag_C = acc_a0 = register_A;
      cmp_old = register_B;
      register_B = cmp_new = ram[register_I];

      /* opLDAimm_A_AA (03) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0x0300;
      /* opCMPdir_A_AA (b7) */
      cmp_old = acc_a0 = register_A; /* backup old acc */
      flag_C = (((cmp_new = ram[register_I = 0x27]) ^ 0xFFF) + 1 + register_A);
      /* opJNC_A_AA (5d) */
      if (!(flag_C & CARRYBIT)) {
        register_PC = 0x1479;
        break;
      };
      /* opLDPimm_A_A (81) */
      register_P = 0x1; /* set page register */
      /* opLDAdir_A_AA (a3) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = 0x13]; /* set I register */

      /* opLDJimm_A_A (43) */
      register_J = 0x00d3;
      /* opJDR_A_A (5a) */
      /* opVIN_A_A (f0) */

      FromX = register_A & 0xFFF; /* regA goes to x-coord */
      FromY = register_B & 0xFFF; /* regB goes to y-coord */

      /* opLDAdir_A_AA (a9) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = 0x19]; /* set I register */

      /* opSUBimm_A_AA (3c) */
      cmp_old = acc_a0 = register_A;
      register_A = (flag_C = (register_A + (((cmp_new = 0xc) ^ 0xFFF) + 1))) & 0xFFF; /* 1's-comp add */
      /* opSTAirg_A_A (e6) */
      ram[0x19] = register_A; /* store acc */
      /* opLDIdir_A_A (c9) */
      register_I = ram[0x19] & 0xff; /* set new register_I (8 bits) */
      /* opLDAirg_A_AA (ea) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = ram[register_I];
      /* opSTAdir_A_A (d7) */
      ram[register_I = 0x17] = register_A; /* store acc to RAM */
      /* opADDimm_A_AA (22) */
      register_A = (flag_C = ((cmp_old = acc_a0 = register_A) + (cmp_new = 0x2))) & 0xFFF; /* add values, save carry */

      /* opSTAdir_A_A (d6) */
      ram[register_I = 0x16] = register_A; /* store acc to RAM */
      /* opSUBimm_A_AA (31) */
      cmp_old = acc_a0 = register_A;
      register_A = (flag_C = (register_A + (((cmp_new = 0x1) ^ 0xFFF) + 1))) & 0xFFF; /* 1's-comp add */
      /* opXLT_A_AA (e2) */
      cmp_old = register_A; register_A = cmp_new = rom[0x1000 | register_A]; /* new acc value */
      /* opNOP_A_A (5f) */
      /* opSTAdir_A_A (df) */
      ram[register_I = (register_P << 4) + 0xf] = register_A; /* store acc to RAM */
      /* opNOP_A_B (57) */
      /* opLDAirg_B_AA (ea) */
      flag_C = acc_a0 = register_A;
      cmp_old = register_B;
      register_B = cmp_new = ram[register_I];

      /* opLDAdir_A_AA (a7) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = (register_P << 4) + 0x07]; /* set I register */

      /* opXLT_A_AA (e2) */
      cmp_old = register_A; register_A = cmp_new = rom[0x1000 | register_A]; /* new acc value */
      /* opNOP_A_A (5f) */
      /* opSTAirg_A_A (e6) */
      ram[register_I] = register_A; /* store acc */
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opASRDe_A_AA (ee) */
      cmp_new = 0x0EEE; cmp_old = acc_a0 = register_A; flag_C = (0x0EEE + register_A);
      register_A = (register_A >> 1) | ((register_B & 1) << 11);
      register_B = (register_B >> 1) | (register_B & 0x800);
      /* opASRDe_A_AA (ee) */
      cmp_new = 0x0EEE; cmp_old = acc_a0 = register_A; flag_C = (0x0EEE + register_A);
      register_A = (register_A >> 1) | ((register_B & 1) << 11);
      register_B = (register_B >> 1) | (register_B & 0x800);
      /* opASRDe_A_AA (ee) */
      cmp_new = 0x0EEE; cmp_old = acc_a0 = register_A; flag_C = (0x0EEE + register_A);
      register_A = (register_A >> 1) | ((register_B & 1) << 11);
      register_B = (register_B >> 1) | (register_B & 0x800);
      /* opASRDe_A_AA (ee) */
      cmp_new = 0x0EEE; cmp_old = acc_a0 = register_A; flag_C = (0x0EEE + register_A);
      register_A = (register_A >> 1) | ((register_B & 1) << 11);
      register_B = (register_B >> 1) | (register_B & 0x800);
      /* opAWDirg_A_AA (e7) */
      cmp_old = acc_a0 = register_A;
      register_A = (flag_C = (register_A + (cmp_new = ram[register_I]))) & 0xFFF;
      set_watchdog();
      /* opSTAirg_A_A (e6) */
      ram[register_I] = register_A; /* store acc */
      /* opLDAdir_A_AA (a9) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = (register_P << 4) + 0x09]; /* set I register */

      /* opADDimm_A_AA (21) */
      register_A = (flag_C = ((cmp_old = acc_a0 = register_A) + (cmp_new = 0x1))) & 0xFFF; /* add values, save carry */

      /* opSTAirg_A_A (e6) */
      ram[register_I] = register_A; /* store acc */
      /* opLDIdir_A_A (c9) */
      register_I = ram[(register_P << 4) + 0x09] & 0xff; /* set/mask new register_I */
      /* opLDAirg_A_AA (ea) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = ram[register_I];
      /* opSTAdir_A_A (d0) */
      ram[register_I = (register_P << 4) + 0x0] = register_A; /* store acc to RAM */
      /* opLDAdir_A_AA (a9) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = (register_P << 4) + 0x09]; /* set I register */

      /* opADDimm_A_AA (21) */
      register_A = (flag_C = ((cmp_old = acc_a0 = register_A) + (cmp_new = 0x1))) & 0xFFF; /* add values, save carry */

      /* opSTAirg_A_A (e6) */
      ram[register_I] = register_A; /* store acc */
      /* opLDIdir_A_A (c9) */
      register_I = ram[(register_P << 4) + 0x09] & 0xff; /* set/mask new register_I */
      /* opLDAirg_A_AA (ea) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = ram[register_I];
      /* opSTAdir_A_A (d1) */
      ram[register_I = (register_P << 4) + 0x1] = register_A; /* store acc to RAM */
      /* opLDAdir_A_AA (a9) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = (register_P << 4) + 0x09]; /* set I register */

      /* opADDimm_A_AA (21) */
      register_A = (flag_C = ((cmp_old = acc_a0 = register_A) + (cmp_new = 0x1))) & 0xFFF; /* add values, save carry */

      /* opSTAirg_A_A (e6) */
      ram[register_I] = register_A; /* store acc */
      /* opLDIdir_A_A (c9) */
      register_I = ram[(register_P << 4) + 0x09] & 0xff; /* set/mask new register_I */
      /* opLDAirg_A_AA (ea) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = ram[register_I];
      /* opSTAdir_A_A (d2) */
      ram[register_I = (register_P << 4) + 0x2] = register_A; /* store acc to RAM */
      /* opLDAdir_A_AA (a9) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = (register_P << 4) + 0x09]; /* set I register */

      /* opADDimm_A_AA (21) */
      register_A = (flag_C = ((cmp_old = acc_a0 = register_A) + (cmp_new = 0x1))) & 0xFFF; /* add values, save carry */

      /* opSTAirg_A_A (e6) */
      ram[register_I] = register_A; /* store acc */
      /* opLDIdir_A_A (c9) */
      register_I = ram[(register_P << 4) + 0x09] & 0xff; /* set/mask new register_I */
      /* opLDAirg_A_AA (ea) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = ram[register_I];
      /* opSTAdir_A_A (de) */
      ram[register_I = (register_P << 4) + 0xe] = register_A; /* store acc to RAM */
      /* opLDAimm_A_AA (01) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0x0100;
      /* opADDimmX_A_AA (20) */
      cmp_old = register_A; cmp_new = 0x0d;
      register_A = flag_C = acc_a0 = 0x010d;
      /* opSTAdir_A_A (df) */
      ram[register_I = (register_P << 4) + 0xf] = 0x10d; /* store acc to RAM */
      /* opLDJimm_A_A (45) */
      register_J = 0x0575;
      /* opJMP_A_A (58) */
      {
        register_PC = 0x1575;
        break;
      };

    case 0x110d:

      /* Invariants: register_P = 0x1 register_I = 0x1f */;
      /* opLDAdir_A_AA (aa) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = 0x1a]; /* set I register */

      /* opSTAdir_A_A (dc) */
      ram[register_I = 0x1c] = register_A; /* store acc to RAM */
      /* opLDAdir_A_AA (ab) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = 0x1b]; /* set I register */

      /* opSTAdir_A_A (dd) */
      ram[register_I = 0x1d] = register_A; /* store acc to RAM */
      /* opLDAdir_A_AA (a9) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = 0x19]; /* set I register */

      /* opADDimm_A_AA (21) */
      register_A = (flag_C = ((cmp_old = acc_a0 = register_A) + (cmp_new = 0x1))) & 0xFFF; /* add values, save carry */

      /* opSTAirg_A_A (e6) */
      ram[0x19] = register_A; /* store acc */
      /* opLDIdir_A_A (c9) */
      register_I = ram[0x19] & 0xff; /* set new register_I (8 bits) */
      /* opLDAirg_A_AA (ea) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = ram[register_I];
      /* opSTAdir_A_A (de) */
      ram[register_I = 0x1e] = register_A; /* store acc to RAM */
      /* opLDAimm_A_AA (01) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0x0100;
      /* opADDimmX_A_AA (20) */
      cmp_old = register_A; cmp_new = 0x1e;
      register_A = flag_C = acc_a0 = 0x011e;
      /* opSTAdir_A_A (df) */
      ram[register_I = 0x1f] = 0x11e; /* store acc to RAM */
      /* opLDJimm_A_A (45) */
      register_J = 0x0575;
      /* opJMP_A_A (58) */
      {
        register_PC = 0x1575;
        break;
      };

    case 0x111e:

      /* Invariants: register_P = 0x1 register_I = 0x1f */;
      /* opLDAdir_A_AA (a9) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = 0x19]; /* set I register */

      /* opADDimm_A_AA (22) */
      register_A = (flag_C = ((cmp_old = acc_a0 = register_A) + (cmp_new = 0x2))) & 0xFFF; /* add values, save carry */

      /* opSTAirg_A_A (e6) */
      ram[0x19] = register_A; /* store acc */
      /* opLDIdir_A_A (c9) */
      register_I = ram[0x19] & 0xff; /* set new register_I (8 bits) */
      /* opLDAirg_A_AA (ea) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = ram[register_I];
      /* opSTAdir_A_A (d9) */
      ram[register_I = 0x19] = register_A; /* store acc to RAM */
      /* opLSRe_A_AA (eb) */
      cmp_new = 0x0BEB; cmp_old = acc_a0 = register_A; flag_C = (0x0BEB + register_A);
      register_A >>= 1;
      /* opLDJimm_A_A (42) */
      register_J = 0x0072;
      /* opJMP_A_A (58) */
      {
        register_PC = 0x1072;
        break;
      };

    case 0x1128:

      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opADDimmX_A_AA (20) */
      cmp_old = register_A; cmp_new = 0xff;
      register_A = flag_C = acc_a0 = 0x00ff;
      /* opLDPimm_A_A (81) */
      register_P = 0x1; /* set page register */
      /* opSTAdir_A_A (d8) */
      ram[register_I = 0x18] = 0x0ff; /* store acc to RAM */
      /* opLDAdir_A_AA (a6) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = 0x16]; /* set I register */

      /* opSUBimm_A_AA (33) */
      cmp_old = acc_a0 = register_A;
      register_A = (flag_C = (register_A + (((cmp_new = 0x3) ^ 0xFFF) + 1))) & 0xFFF; /* 1's-comp add */
      /* opSTAirg_A_A (e6) */
      ram[0x16] = register_A; /* store acc */
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opSUBimm_A_AA (32) */
      cmp_old = acc_a0 = register_A;
      register_A = (flag_C = (register_A + (((cmp_new = 0x2) ^ 0xFFF) + 1))) & 0xFFF; /* 1's-comp add */
      /* opLDPimm_A_A (80) */
      register_P = 0x0; /* set page register */
      /* opSTAdir_A_A (d3) */
      ram[register_I = 0x03] = register_A; /* store acc to RAM */

    case 0x1134:

      /* Invariants: register_P = 0x0 register_I = 0x03 */;
      /* opLDPimm_A_A (80) */
      /* opLDAdir_A_AA (a3) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = 0x03]; /* set I register */

      /* opLSRe_A_AA (eb) */
      cmp_new = 0x0BEB; cmp_old = acc_a0 = register_A; flag_C = (0x0BEB + register_A);
      register_A >>= 1;
      /* opLDJimm_A_A (4c) */
      register_J = 0x039c;
      /* opJA0_A_A (5e) */
      if (acc_a0 & 0x01) {
        register_PC = 0x139c;
        break;
      };

      /* opLDPimm_A_A (81) */
      register_P = 0x1; /* set page register */
      /* opLDAdir_A_AA (a6) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = 0x16]; /* set I register */

      /* opADDimm_A_AA (23) */
      register_A = (flag_C = ((cmp_old = acc_a0 = register_A) + (cmp_new = 0x3))) & 0xFFF; /* add values, save carry */

      /* opSTAirg_A_A (e6) */
      ram[0x16] = register_A; /* store acc */
      /* opXLT_A_AA (e2) */
      cmp_old = register_A; register_A = cmp_new = rom[0x1000 | register_A]; /* new acc value */
      /* opNOP_A_A (5f) */
      /* opCMPdir_A_AA (b8) */
      cmp_old = acc_a0 = register_A; /* backup old acc */
      flag_C = (((cmp_new = ram[register_I = (register_P << 4) + 0x8]) ^ 0xFFF) + 1 + register_A);
      /* opLDJimm_A_A (41) */
      register_J = 0x0471;
      /* opJEQ_A_AA (5c) */
      if (cmp_new == cmp_old) {
        register_PC = 0x1471;
        break;
      };
      /* opSUBimmX_A_AA (30) */
      cmp_old = acc_a0 = register_A; /* back up regA */
      register_A = (flag_C = (register_A + (((cmp_new = 0x80) ^ 0xFFF) + 1))) & 0xFFF; /* add */
      /* opLSLe_A_AA (ec) */
      cmp_new = 0x0CEC;
      cmp_old = acc_a0 = register_A;
      flag_C = (0x0CEC + register_A);
      register_A = (register_A << 1) & 0x0FFF;
      /* opLSLe_A_AA (ec) */
      cmp_new = 0x0CEC;
      cmp_old = acc_a0 = register_A;
      flag_C = (0x0CEC + register_A);
      register_A = (register_A << 1) & 0x0FFF;
      /* opSTAdir_A_A (d3) */
      ram[register_I = (register_P << 4) + 0x3] = register_A; /* store acc to RAM */
      /* opLDAdir_A_AA (a6) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = (register_P << 4) + 0x06]; /* set I register */

      /* opADDimm_A_AA (21) */
      register_A = (flag_C = ((cmp_old = acc_a0 = register_A) + (cmp_new = 0x1))) & 0xFFF; /* add values, save carry */

      /* opXLT_A_AA (e2) */
      cmp_old = register_A; register_A = cmp_new = rom[0x1000 | register_A]; /* new acc value */
      /* opNOP_A_A (5f) */
      /* opSUBimmX_A_AA (30) */
      cmp_old = acc_a0 = register_A; /* back up regA */
      register_A = (flag_C = (register_A + (((cmp_new = 0x80) ^ 0xFFF) + 1))) & 0xFFF; /* add */
      /* opLSLe_A_AA (ec) */
      cmp_new = 0x0CEC;
      cmp_old = acc_a0 = register_A;
      flag_C = (0x0CEC + register_A);
      register_A = (register_A << 1) & 0x0FFF;
      /* opLSLe_A_AA (ec) */
      cmp_new = 0x0CEC;
      cmp_old = acc_a0 = register_A;
      flag_C = (0x0CEC + register_A);
      register_A = (register_A << 1) & 0x0FFF;
      /* opSTAdir_A_A (d4) */
      ram[register_I = (register_P << 4) + 0x4] = register_A; /* store acc to RAM */
      /* opLDAdir_A_AA (a6) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = (register_P << 4) + 0x06]; /* set I register */

      /* opADDimm_A_AA (22) */
      register_A = (flag_C = ((cmp_old = acc_a0 = register_A) + (cmp_new = 0x2))) & 0xFFF; /* add values, save carry */

      /* opXLT_A_AA (e2) */
      cmp_old = register_A; register_A = cmp_new = rom[0x1000 | register_A]; /* new acc value */
      /* opNOP_A_A (5f) */
      /* opSUBimmX_A_AA (30) */
      cmp_old = acc_a0 = register_A; /* back up regA */
      register_A = (flag_C = (register_A + (((cmp_new = 0x80) ^ 0xFFF) + 1))) & 0xFFF; /* add */
      /* opLSLe_A_AA (ec) */
      cmp_new = 0x0CEC;
      cmp_old = acc_a0 = register_A;
      flag_C = (0x0CEC + register_A);
      register_A = (register_A << 1) & 0x0FFF;
      /* opLSLe_A_AA (ec) */
      cmp_new = 0x0CEC;
      cmp_old = acc_a0 = register_A;
      flag_C = (0x0CEC + register_A);
      register_A = (register_A << 1) & 0x0FFF;
      /* opSTAdir_A_A (d5) */
      ram[register_I = (register_P << 4) + 0x5] = register_A; /* store acc to RAM */
      /* opLDPimm_A_A (80) */
      register_P = 0x0; /* set page register */
      /* opLDAdir_A_AA (a3) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = 0x03]; /* set I register */

      /* opADDimm_A_AA (22) */
      register_A = (flag_C = ((cmp_old = acc_a0 = register_A) + (cmp_new = 0x2))) & 0xFFF; /* add values, save carry */

      /* opSTAirg_A_A (e6) */
      ram[0x03] = register_A; /* store acc */
      /* opLDPimm_A_A (81) */
      register_P = 0x1; /* set page register */
      /* opLDAdir_A_AA (a3) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = 0x13]; /* set I register */

      /* opASRe_A_AA (ed) */
      cmp_new = 0xDED; cmp_old = flag_C = acc_a0 = register_A;
      register_A = SEX(register_A); /* make signed */
      register_A = (((signed short int)(((signed short int)register_A) >> (signed short int)1)) /* SAR */) & 0xFFF;
      /* opSTAirg_A_A (e6) */
      ram[0x13] = register_A; /* store acc */
      /* opLDAdir_A_AA (a4) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = 0x14]; /* set I register */

      /* opASRe_A_AA (ed) */
      cmp_new = 0xDED; cmp_old = flag_C = acc_a0 = register_A;
      register_A = SEX(register_A); /* make signed */
      register_A = (((signed short int)(((signed short int)register_A) >> (signed short int)1)) /* SAR */) & 0xFFF;
      /* opSTAirg_A_A (e6) */
      ram[0x14] = register_A; /* store acc */
      /* opLDAdir_A_AA (ac) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = 0x1c]; /* set I register */

      /* opADDdir_A_AA (6d) */
      register_A = (flag_C = ((cmp_old = acc_a0 = register_A) + (cmp_new = ram[register_I = 0x1d]))) & 0xFFF; /* do acc operation */
      /* opSTAdir_A_A (df) */
      ram[register_I = 0x1f] = register_A; /* store acc to RAM */
      /* opNOP_A_B (57) */
      /* opLDAimm_B_AA (00) */
      flag_C = acc_a0 = register_A;
      cmp_old = register_B; /* step back cmp flag */
      register_B = cmp_new = 0;
      /* opLDAimm_A_AA (08) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0x0800;
      /* opLDAdir_A_AA (af) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = 0x1f]; /* set I register */

      /* opLDJimm_A_A (42) */
      register_J = 0x0172;
      /* opJLT_A_A (5b) */
      if (cmp_new < cmp_old) {
        register_PC = 0x1172;
        break;
      };
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opSUBirg_A_AA (e8) */
      register_A = (flag_C = ((cmp_old = acc_a0 = register_A) + ((cmp_new = ram[register_I]) ^ 0xFFF) + 1)) & 0xFFF; /* ones compliment */

    case 0x1172:

      /* Invariants: register_P = 0x1 register_I = 0x1f */;
      /* opCMPdir_A_AA (b3) */
      cmp_old = acc_a0 = register_A; /* backup old acc */
      flag_C = (((cmp_new = ram[register_I = 0x13]) ^ 0xFFF) + 1 + register_A);
      /* opMULirg_A_AA (e3) */
      MUL();
      /* opMULirg_A_AA (e3) */
      MUL();
      /* opMULirg_A_AA (e3) */
      MUL();
      /* opMULirg_A_AA (e3) */
      MUL();
      /* opMULirg_A_AA (e3) */
      MUL();
      /* opMULirg_A_AA (e3) */
      MUL();
      /* opMULirg_A_AA (e3) */
      MUL();
      /* opMULirg_A_AA (e3) */
      MUL();
      /* opMULirg_A_AA (e3) */
      MUL();
      /* opLDAimm_A_AA (08) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0x0800;
      /* opCMPdir_A_AA (bf) */
      cmp_old = acc_a0 = register_A; /* backup old acc */
      flag_C = (((cmp_new = ram[register_I = 0x1f]) ^ 0xFFF) + 1 + register_A);
      /* opNOP_A_B (57) */
      /* opSTAdir_B_BB (df) */
      ram[register_I = (register_P << 4) + 0xf] = register_B; /* set I register and store B to ram */
      /* opLDJimm_A_A (46) */
      register_J = 0x0186;
      /* opJLT_A_A (5b) */
      if (cmp_new < cmp_old) {
        register_PC = 0x1186;
        break;
      };
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opSUBdir_A_AA (7f) */
      cmp_old = acc_a0 = register_A;
      register_A = (flag_C = (register_A + ((cmp_new = ram[register_I = 0x1f]) ^ 0xFFF) + 1)) & 0xFFF; /* set regI addr */
      /* opSTAirg_A_A (e6) */
      ram[0x1f] = register_A; /* store acc */

    case 0x1186:

      /* Invariants: register_P = 0x1 register_I = 0x1f */;
      /* opLDAdir_A_AA (a4) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = 0x14]; /* set I register */

      /* opADDdir_A_AA (63) */
      register_A = (flag_C = ((cmp_old = acc_a0 = register_A) + (cmp_new = ram[register_I = 0x13]))) & 0xFFF; /* do acc operation */
      /* opSTAirg_A_A (e6) */
      ram[0x13] = register_A; /* store acc */
      /* opSUBdir_A_AA (74) */
      cmp_old = acc_a0 = register_A;
      register_A = (flag_C = (register_A + ((cmp_new = ram[register_I = 0x14]) ^ 0xFFF) + 1)) & 0xFFF; /* set regI addr */
      /* opSUBirg_A_AA (e8) */
      register_A = (flag_C = ((cmp_old = acc_a0 = register_A) + ((cmp_new = ram[register_I]) ^ 0xFFF) + 1)) & 0xFFF; /* ones compliment */
      /* opSTAirg_A_A (e6) */
      ram[0x14] = register_A; /* store acc */
      /* opNOP_A_B (57) */
      /* opLDAimm_B_AA (00) */
      flag_C = acc_a0 = register_A;
      cmp_old = register_B; /* step back cmp flag */
      register_B = cmp_new = 0;
      /* opLDAimm_A_AA (08) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0x0800;
      /* opLDAdir_A_AA (ac) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = 0x1c]; /* set I register */

      /* opLDJimm_A_A (45) */
      register_J = 0x0195;
      /* opJLT_A_A (5b) */
      if (cmp_new < cmp_old) {
        register_PC = 0x1195;
        break;
      };
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opSUBirg_A_AA (e8) */
      register_A = (flag_C = ((cmp_old = acc_a0 = register_A) + ((cmp_new = ram[register_I]) ^ 0xFFF) + 1)) & 0xFFF; /* ones compliment */

    case 0x1195:

      /* Invariants: register_P = 0x1 register_I = 0x1c */;
      /* opCMPdir_A_AA (b3) */
      cmp_old = acc_a0 = register_A; /* backup old acc */
      flag_C = (((cmp_new = ram[register_I = 0x13]) ^ 0xFFF) + 1 + register_A);
      /* opMULirg_A_AA (e3) */
      MUL();
      /* opMULirg_A_AA (e3) */
      MUL();
      /* opMULirg_A_AA (e3) */
      MUL();
      /* opMULirg_A_AA (e3) */
      MUL();
      /* opMULirg_A_AA (e3) */
      MUL();
      /* opMULirg_A_AA (e3) */
      MUL();
      /* opMULirg_A_AA (e3) */
      MUL();
      /* opMULirg_A_AA (e3) */
      MUL();
      /* opMULirg_A_AA (e3) */
      MUL();
      /* opLDAimm_A_AA (08) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0x0800;
      /* opCMPdir_A_AA (bc) */
      cmp_old = acc_a0 = register_A; /* backup old acc */
      flag_C = (((cmp_new = ram[register_I = 0x1c]) ^ 0xFFF) + 1 + register_A);
      /* opNOP_A_B (57) */
      /* opSTAdir_B_BB (d3) */
      ram[register_I = (register_P << 4) + 0x3] = register_B; /* set I register and store B to ram */
      /* opLDJimm_A_A (49) */
      register_J = 0x01a9;
      /* opJLT_A_A (5b) */
      if (cmp_new < cmp_old) {
        register_PC = 0x11a9;
        break;
      };
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opSUBdir_A_AA (73) */
      cmp_old = acc_a0 = register_A;
      register_A = (flag_C = (register_A + ((cmp_new = ram[register_I = 0x13]) ^ 0xFFF) + 1)) & 0xFFF; /* set regI addr */
      /* opSTAirg_A_A (e6) */
      ram[0x13] = register_A; /* store acc */

    case 0x11a9:

      /* Invariants: register_P = 0x1 register_I = 0x13 */;
      /* opNOP_A_B (57) */
      /* opLDAimm_B_AA (00) */
      flag_C = acc_a0 = register_A;
      cmp_old = register_B; /* step back cmp flag */
      register_B = cmp_new = 0;
      /* opLDAimm_A_AA (08) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0x0800;
      /* opLDAdir_A_AA (ad) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = 0x1d]; /* set I register */

      /* opLDJimm_A_A (42) */
      register_J = 0x01b2;
      /* opJLT_A_A (5b) */
      if (cmp_new < cmp_old) {
        register_PC = 0x11b2;
        break;
      };
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opSUBirg_A_AA (e8) */
      register_A = (flag_C = ((cmp_old = acc_a0 = register_A) + ((cmp_new = ram[register_I]) ^ 0xFFF) + 1)) & 0xFFF; /* ones compliment */

    case 0x11b2:

      /* Invariants: register_P = 0x1 register_I = 0x1d */;
      /* opCMPdir_A_AA (b4) */
      cmp_old = acc_a0 = register_A; /* backup old acc */
      flag_C = (((cmp_new = ram[register_I = 0x14]) ^ 0xFFF) + 1 + register_A);
      /* opMULirg_A_AA (e3) */
      MUL();
      /* opMULirg_A_AA (e3) */
      MUL();
      /* opMULirg_A_AA (e3) */
      MUL();
      /* opMULirg_A_AA (e3) */
      MUL();
      /* opMULirg_A_AA (e3) */
      MUL();
      /* opMULirg_A_AA (e3) */
      MUL();
      /* opMULirg_A_AA (e3) */
      MUL();
      /* opMULirg_A_AA (e3) */
      MUL();
      /* opMULirg_A_AA (e3) */
      MUL();
      /* opLDAimm_A_AA (08) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0x0800;
      /* opCMPdir_A_AA (bd) */
      cmp_old = acc_a0 = register_A; /* backup old acc */
      flag_C = (((cmp_new = ram[register_I = 0x1d]) ^ 0xFFF) + 1 + register_A);
      /* opNOP_A_B (57) */
      /* opSTAdir_B_BB (d4) */
      ram[register_I = (register_P << 4) + 0x4] = register_B; /* set I register and store B to ram */
      /* opLDJimm_A_A (46) */
      register_J = 0x01c6;
      /* opJLT_A_A (5b) */
      if (cmp_new < cmp_old) {
        register_PC = 0x11c6;
        break;
      };
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opSUBdir_A_AA (74) */
      cmp_old = acc_a0 = register_A;
      register_A = (flag_C = (register_A + ((cmp_new = ram[register_I = 0x14]) ^ 0xFFF) + 1)) & 0xFFF; /* set regI addr */
      /* opSTAirg_A_A (e6) */
      ram[0x14] = register_A; /* store acc */

    case 0x11c6:

      /* Invariants: register_P = 0x1 register_I = 0x14 register_A = 0x800 */;
      /* opLDAdir_A_AA (af) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = 0x1f]; /* set I register */

      /* opSUBdir_A_AA (73) */
      cmp_old = acc_a0 = register_A;
      register_A = (flag_C = (register_A + ((cmp_new = ram[register_I = 0x13]) ^ 0xFFF) + 1)) & 0xFFF; /* set regI addr */
      /* opLSLe_A_AA (ec) */
      cmp_new = 0x0CEC;
      cmp_old = acc_a0 = register_A;
      flag_C = (0x0CEC + register_A);
      register_A = (register_A << 1) & 0x0FFF;
      /* opSTAirg_A_A (e6) */
      ram[0x13] = register_A; /* store acc */
      /* opLDAdir_A_AA (af) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = 0x1f]; /* set I register */

      /* opSUBdir_A_AA (74) */
      cmp_old = acc_a0 = register_A;
      register_A = (flag_C = (register_A + ((cmp_new = ram[register_I = 0x14]) ^ 0xFFF) + 1)) & 0xFFF; /* set regI addr */
      /* opLSLe_A_AA (ec) */
      cmp_new = 0x0CEC;
      cmp_old = acc_a0 = register_A;
      flag_C = (0x0CEC + register_A);
      register_A = (register_A << 1) & 0x0FFF;
      /* opSTAirg_A_A (e6) */
      ram[0x14] = register_A; /* store acc */
      /* opLDAdir_A_AA (a4) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = 0x14]; /* set I register */

      /* opASRe_A_AA (ed) */
      cmp_new = 0xDED; cmp_old = flag_C = acc_a0 = register_A;
      register_A = SEX(register_A); /* make signed */
      register_A = (((signed short int)(((signed short int)register_A) >> (signed short int)1)) /* SAR */) & 0xFFF;
      /* opSTAirg_A_A (e6) */
      ram[0x14] = register_A; /* store acc */
      /* opLDAdir_A_AA (a5) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = 0x15]; /* set I register */

      /* opASRe_A_AA (ed) */
      cmp_new = 0xDED; cmp_old = flag_C = acc_a0 = register_A;
      register_A = SEX(register_A); /* make signed */
      register_A = (((signed short int)(((signed short int)register_A) >> (signed short int)1)) /* SAR */) & 0xFFF;
      /* opSTAirg_A_A (e6) */
      ram[0x15] = register_A; /* store acc */
      /* opLDAdir_A_AA (aa) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = 0x1a]; /* set I register */

      /* opADDdir_A_AA (6b) */
      register_A = (flag_C = ((cmp_old = acc_a0 = register_A) + (cmp_new = ram[register_I = 0x1b]))) & 0xFFF; /* do acc operation */
      /* opSTAdir_A_A (df) */
      ram[register_I = 0x1f] = register_A; /* store acc to RAM */
      /* opNOP_A_B (57) */
      /* opLDAimm_B_AA (00) */
      flag_C = acc_a0 = register_A;
      cmp_old = register_B; /* step back cmp flag */
      register_B = cmp_new = 0;
      /* opLDAimm_A_AA (08) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0x0800;
      /* opLDAdir_A_AA (af) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = 0x1f]; /* set I register */

      /* opLDJimm_A_A (40) */
      register_J = 0x01e0;
      /* opJLT_A_A (5b) */
      if (cmp_new < cmp_old) {
        register_PC = 0x11e0;
        break;
      };
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opSUBirg_A_AA (e8) */
      register_A = (flag_C = ((cmp_old = acc_a0 = register_A) + ((cmp_new = ram[register_I]) ^ 0xFFF) + 1)) & 0xFFF; /* ones compliment */

    case 0x11e0:

      /* Invariants: register_P = 0x1 register_I = 0x1f */;
      /* opCMPdir_A_AA (b4) */
      cmp_old = acc_a0 = register_A; /* backup old acc */
      flag_C = (((cmp_new = ram[register_I = 0x14]) ^ 0xFFF) + 1 + register_A);
      /* opMULirg_A_AA (e3) */
      MUL();
      /* opMULirg_A_AA (e3) */
      MUL();
      /* opMULirg_A_AA (e3) */
      MUL();
      /* opMULirg_A_AA (e3) */
      MUL();
      /* opMULirg_A_AA (e3) */
      MUL();
      /* opMULirg_A_AA (e3) */
      MUL();
      /* opMULirg_A_AA (e3) */
      MUL();
      /* opMULirg_A_AA (e3) */
      MUL();
      /* opMULirg_A_AA (e3) */
      MUL();
      /* opLDAimm_A_AA (08) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0x0800;
      /* opCMPdir_A_AA (bf) */
      cmp_old = acc_a0 = register_A; /* backup old acc */
      flag_C = (((cmp_new = ram[register_I = 0x1f]) ^ 0xFFF) + 1 + register_A);
      /* opNOP_A_B (57) */
      /* opSTAdir_B_BB (df) */
      ram[register_I = (register_P << 4) + 0xf] = register_B; /* set I register and store B to ram */
      /* opLDJimm_A_A (44) */
      register_J = 0x01f4;
      /* opJLT_A_A (5b) */
      if (cmp_new < cmp_old) {
        register_PC = 0x11f4;
        break;
      };
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opSUBdir_A_AA (7f) */
      cmp_old = acc_a0 = register_A;
      register_A = (flag_C = (register_A + ((cmp_new = ram[register_I = 0x1f]) ^ 0xFFF) + 1)) & 0xFFF; /* set regI addr */
      /* opSTAirg_A_A (e6) */
      ram[0x1f] = register_A; /* store acc */

    case 0x11f4:

      /* Invariants: register_P = 0x1 register_I = 0x1f */;
      /* opLDAdir_A_AA (a5) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = 0x15]; /* set I register */

      /* opADDdir_A_AA (64) */
      register_A = (flag_C = ((cmp_old = acc_a0 = register_A) + (cmp_new = ram[register_I = 0x14]))) & 0xFFF; /* do acc operation */
      /* opSTAirg_A_A (e6) */
      ram[0x14] = register_A; /* store acc */
      /* opSUBdir_A_AA (75) */
      cmp_old = acc_a0 = register_A;
      register_A = (flag_C = (register_A + ((cmp_new = ram[register_I = 0x15]) ^ 0xFFF) + 1)) & 0xFFF; /* set regI addr */
      /* opSUBirg_A_AA (e8) */
      register_A = (flag_C = ((cmp_old = acc_a0 = register_A) + ((cmp_new = ram[register_I]) ^ 0xFFF) + 1)) & 0xFFF; /* ones compliment */
      /* opSTAirg_A_A (e6) */
      ram[0x15] = register_A; /* store acc */
      /* opNOP_A_B (57) */
      /* opLDAimm_B_AA (00) */
      flag_C = acc_a0 = register_A;
      cmp_old = register_B; /* step back cmp flag */
      register_B = cmp_new = 0;
      /* opLDAimm_A_AA (08) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0x0800;
      /* opLDAdir_A_AA (aa) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = 0x1a]; /* set I register */

      /* opLDJimm_A_A (43) */
      register_J = 0x0203;
      /* opJLT_A_A (5b) */
      if (cmp_new < cmp_old) {
        register_PC = 0x1203;
        break;
      };
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opSUBirg_A_AA (e8) */
      register_A = (flag_C = ((cmp_old = acc_a0 = register_A) + ((cmp_new = ram[register_I]) ^ 0xFFF) + 1)) & 0xFFF; /* ones compliment */

    case 0x1203:

      /* Invariants: register_P = 0x1 register_I = 0x1a */;
      /* opCMPdir_A_AA (b4) */
      cmp_old = acc_a0 = register_A; /* backup old acc */
      flag_C = (((cmp_new = ram[register_I = 0x14]) ^ 0xFFF) + 1 + register_A);
      /* opMULirg_A_AA (e3) */
      MUL();
      /* opMULirg_A_AA (e3) */
      MUL();
      /* opMULirg_A_AA (e3) */
      MUL();
      /* opMULirg_A_AA (e3) */
      MUL();
      /* opMULirg_A_AA (e3) */
      MUL();
      /* opMULirg_A_AA (e3) */
      MUL();
      /* opMULirg_A_AA (e3) */
      MUL();
      /* opMULirg_A_AA (e3) */
      MUL();
      /* opMULirg_A_AA (e3) */
      MUL();
      /* opLDAimm_A_AA (08) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0x0800;
      /* opCMPdir_A_AA (ba) */
      cmp_old = acc_a0 = register_A; /* backup old acc */
      flag_C = (((cmp_new = ram[register_I = 0x1a]) ^ 0xFFF) + 1 + register_A);
      /* opNOP_A_B (57) */
      /* opSTAdir_B_BB (d4) */
      ram[register_I = (register_P << 4) + 0x4] = register_B; /* set I register and store B to ram */
      /* opLDJimm_A_A (47) */
      register_J = 0x0217;
      /* opJLT_A_A (5b) */
      if (cmp_new < cmp_old) {
        register_PC = 0x1217;
        break;
      };
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opSUBdir_A_AA (74) */
      cmp_old = acc_a0 = register_A;
      register_A = (flag_C = (register_A + ((cmp_new = ram[register_I = 0x14]) ^ 0xFFF) + 1)) & 0xFFF; /* set regI addr */
      /* opSTAirg_A_A (e6) */
      ram[0x14] = register_A; /* store acc */

    case 0x1217:

      /* Invariants: register_P = 0x1 register_I = 0x14 */;
      /* opNOP_A_B (57) */
      /* opLDAimm_B_AA (00) */
      flag_C = acc_a0 = register_A;
      cmp_old = register_B; /* step back cmp flag */
      register_B = cmp_new = 0;
      /* opLDAimm_A_AA (08) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0x0800;
      /* opLDAdir_A_AA (ab) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = 0x1b]; /* set I register */

      /* opLDJimm_A_A (40) */
      register_J = 0x0220;
      /* opJLT_A_A (5b) */
      if (cmp_new < cmp_old) {
        register_PC = 0x1220;
        break;
      };
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opSUBirg_A_AA (e8) */
      register_A = (flag_C = ((cmp_old = acc_a0 = register_A) + ((cmp_new = ram[register_I]) ^ 0xFFF) + 1)) & 0xFFF; /* ones compliment */

    case 0x1220:

      /* Invariants: register_P = 0x1 register_I = 0x1b */;
      /* opCMPdir_A_AA (b5) */
      cmp_old = acc_a0 = register_A; /* backup old acc */
      flag_C = (((cmp_new = ram[register_I = 0x15]) ^ 0xFFF) + 1 + register_A);
      /* opMULirg_A_AA (e3) */
      MUL();
      /* opMULirg_A_AA (e3) */
      MUL();
      /* opMULirg_A_AA (e3) */
      MUL();
      /* opMULirg_A_AA (e3) */
      MUL();
      /* opMULirg_A_AA (e3) */
      MUL();
      /* opMULirg_A_AA (e3) */
      MUL();
      /* opMULirg_A_AA (e3) */
      MUL();
      /* opMULirg_A_AA (e3) */
      MUL();
      /* opMULirg_A_AA (e3) */
      MUL();
      /* opLDAimm_A_AA (08) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0x0800;
      /* opCMPdir_A_AA (bb) */
      cmp_old = acc_a0 = register_A; /* backup old acc */
      flag_C = (((cmp_new = ram[register_I = 0x1b]) ^ 0xFFF) + 1 + register_A);
      /* opNOP_A_B (57) */
      /* opSTAdir_B_BB (d5) */
      ram[register_I = (register_P << 4) + 0x5] = register_B; /* set I register and store B to ram */
      /* opLDJimm_A_A (44) */
      register_J = 0x0234;
      /* opJLT_A_A (5b) */
      if (cmp_new < cmp_old) {
        register_PC = 0x1234;
        break;
      };
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opSUBdir_A_AA (75) */
      cmp_old = acc_a0 = register_A;
      register_A = (flag_C = (register_A + ((cmp_new = ram[register_I = 0x15]) ^ 0xFFF) + 1)) & 0xFFF; /* set regI addr */
      /* opSTAirg_A_A (e6) */
      ram[0x15] = register_A; /* store acc */

    case 0x1234:

      /* Invariants: register_P = 0x1 register_I = 0x15 */;
      /* opLDAdir_A_AA (af) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = 0x1f]; /* set I register */

      /* opSUBdir_A_AA (74) */
      cmp_old = acc_a0 = register_A;
      register_A = (flag_C = (register_A + ((cmp_new = ram[register_I = 0x14]) ^ 0xFFF) + 1)) & 0xFFF; /* set regI addr */
      /* opLSLe_A_AA (ec) */
      cmp_new = 0x0CEC;
      cmp_old = acc_a0 = register_A;
      flag_C = (0x0CEC + register_A);
      register_A = (register_A << 1) & 0x0FFF;
      /* opSTAirg_A_A (e6) */
      ram[0x14] = register_A; /* store acc */
      /* opLDAdir_A_AA (af) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = 0x1f]; /* set I register */

      /* opSUBdir_A_AA (75) */
      cmp_old = acc_a0 = register_A;
      register_A = (flag_C = (register_A + ((cmp_new = ram[register_I = 0x15]) ^ 0xFFF) + 1)) & 0xFFF; /* set regI addr */
      /* opLSLe_A_AA (ec) */
      cmp_new = 0x0CEC;
      cmp_old = acc_a0 = register_A;
      flag_C = (0x0CEC + register_A);
      register_A = (register_A << 1) & 0x0FFF;
      /* opSTAirg_A_A (e6) */
      ram[0x15] = register_A; /* store acc */
      /* opLDAdir_A_AA (a3) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = 0x13]; /* set I register */

      /* opASRe_A_AA (ed) */
      cmp_new = 0xDED; cmp_old = flag_C = acc_a0 = register_A;
      register_A = SEX(register_A); /* make signed */
      register_A = (((signed short int)(((signed short int)register_A) >> (signed short int)1)) /* SAR */) & 0xFFF;
      /* opSTAirg_A_A (e6) */
      ram[0x13] = register_A; /* store acc */
      /* opLDAdir_A_AA (a5) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = 0x15]; /* set I register */

      /* opASRe_A_AA (ed) */
      cmp_new = 0xDED; cmp_old = flag_C = acc_a0 = register_A;
      register_A = SEX(register_A); /* make signed */
      register_A = (((signed short int)(((signed short int)register_A) >> (signed short int)1)) /* SAR */) & 0xFFF;
      /* opSTAirg_A_A (e6) */
      ram[0x15] = register_A; /* store acc */
      /* opLDAdir_A_AA (ac) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = 0x1c]; /* set I register */

      /* opADDdir_A_AA (6d) */
      register_A = (flag_C = ((cmp_old = acc_a0 = register_A) + (cmp_new = ram[register_I = 0x1d]))) & 0xFFF; /* do acc operation */
      /* opSTAdir_A_A (df) */
      ram[register_I = 0x1f] = register_A; /* store acc to RAM */
      /* opNOP_A_B (57) */
      /* opLDAimm_B_AA (00) */
      flag_C = acc_a0 = register_A;
      cmp_old = register_B; /* step back cmp flag */
      register_B = cmp_new = 0;
      /* opLDAimm_A_AA (08) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0x0800;
      /* opLDAdir_A_AA (af) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = 0x1f]; /* set I register */

      /* opLDJimm_A_A (4e) */
      register_J = 0x024e;
      /* opJLT_A_A (5b) */
      if (cmp_new < cmp_old) {
        register_PC = 0x124e;
        break;
      };
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opSUBirg_A_AA (e8) */
      register_A = (flag_C = ((cmp_old = acc_a0 = register_A) + ((cmp_new = ram[register_I]) ^ 0xFFF) + 1)) & 0xFFF; /* ones compliment */

    case 0x124e:

      /* Invariants: register_P = 0x1 register_I = 0x1f */;
      /* opCMPdir_A_AA (b3) */
      cmp_old = acc_a0 = register_A; /* backup old acc */
      flag_C = (((cmp_new = ram[register_I = 0x13]) ^ 0xFFF) + 1 + register_A);
      /* opMULirg_A_AA (e3) */
      MUL();
      /* opMULirg_A_AA (e3) */
      MUL();
      /* opMULirg_A_AA (e3) */
      MUL();
      /* opMULirg_A_AA (e3) */
      MUL();
      /* opMULirg_A_AA (e3) */
      MUL();
      /* opMULirg_A_AA (e3) */
      MUL();
      /* opMULirg_A_AA (e3) */
      MUL();
      /* opMULirg_A_AA (e3) */
      MUL();
      /* opMULirg_A_AA (e3) */
      MUL();
      /* opLDAimm_A_AA (08) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0x0800;
      /* opCMPdir_A_AA (bf) */
      cmp_old = acc_a0 = register_A; /* backup old acc */
      flag_C = (((cmp_new = ram[register_I = 0x1f]) ^ 0xFFF) + 1 + register_A);
      /* opNOP_A_B (57) */
      /* opSTAdir_B_BB (df) */
      ram[register_I = (register_P << 4) + 0xf] = register_B; /* set I register and store B to ram */
      /* opLDJimm_A_A (42) */
      register_J = 0x0262;
      /* opJLT_A_A (5b) */
      if (cmp_new < cmp_old) {
        register_PC = 0x1262;
        break;
      };
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opSUBdir_A_AA (7f) */
      cmp_old = acc_a0 = register_A;
      register_A = (flag_C = (register_A + ((cmp_new = ram[register_I = 0x1f]) ^ 0xFFF) + 1)) & 0xFFF; /* set regI addr */
      /* opSTAirg_A_A (e6) */
      ram[0x1f] = register_A; /* store acc */

    case 0x1262:

      /* Invariants: register_P = 0x1 register_I = 0x1f */;
      /* opLDAdir_A_AA (a5) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = 0x15]; /* set I register */

      /* opADDdir_A_AA (63) */
      register_A = (flag_C = ((cmp_old = acc_a0 = register_A) + (cmp_new = ram[register_I = 0x13]))) & 0xFFF; /* do acc operation */
      /* opSTAirg_A_A (e6) */
      ram[0x13] = register_A; /* store acc */
      /* opSUBdir_A_AA (75) */
      cmp_old = acc_a0 = register_A;
      register_A = (flag_C = (register_A + ((cmp_new = ram[register_I = 0x15]) ^ 0xFFF) + 1)) & 0xFFF; /* set regI addr */
      /* opSUBirg_A_AA (e8) */
      register_A = (flag_C = ((cmp_old = acc_a0 = register_A) + ((cmp_new = ram[register_I]) ^ 0xFFF) + 1)) & 0xFFF; /* ones compliment */
      /* opSTAirg_A_A (e6) */
      ram[0x15] = register_A; /* store acc */
      /* opNOP_A_B (57) */
      /* opLDAimm_B_AA (00) */
      flag_C = acc_a0 = register_A;
      cmp_old = register_B; /* step back cmp flag */
      register_B = cmp_new = 0;
      /* opLDAimm_A_AA (08) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0x0800;
      /* opLDAdir_A_AA (ac) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = 0x1c]; /* set I register */

      /* opLDJimm_A_A (41) */
      register_J = 0x0271;
      /* opJLT_A_A (5b) */
      if (cmp_new < cmp_old) {
        register_PC = 0x1271;
        break;
      };
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opSUBirg_A_AA (e8) */
      register_A = (flag_C = ((cmp_old = acc_a0 = register_A) + ((cmp_new = ram[register_I]) ^ 0xFFF) + 1)) & 0xFFF; /* ones compliment */

    case 0x1271:

      /* Invariants: register_P = 0x1 register_I = 0x1c */;
      /* opCMPdir_A_AA (b3) */
      cmp_old = acc_a0 = register_A; /* backup old acc */
      flag_C = (((cmp_new = ram[register_I = 0x13]) ^ 0xFFF) + 1 + register_A);
      /* opMULirg_A_AA (e3) */
      MUL();
      /* opMULirg_A_AA (e3) */
      MUL();
      /* opMULirg_A_AA (e3) */
      MUL();
      /* opMULirg_A_AA (e3) */
      MUL();
      /* opMULirg_A_AA (e3) */
      MUL();
      /* opMULirg_A_AA (e3) */
      MUL();
      /* opMULirg_A_AA (e3) */
      MUL();
      /* opMULirg_A_AA (e3) */
      MUL();
      /* opMULirg_A_AA (e3) */
      MUL();
      /* opLDAimm_A_AA (08) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0x0800;
      /* opCMPdir_A_AA (bc) */
      cmp_old = acc_a0 = register_A; /* backup old acc */
      flag_C = (((cmp_new = ram[register_I = 0x1c]) ^ 0xFFF) + 1 + register_A);
      /* opNOP_A_B (57) */
      /* opSTAdir_B_BB (d3) */
      ram[register_I = (register_P << 4) + 0x3] = register_B; /* set I register and store B to ram */
      /* opLDJimm_A_A (45) */
      register_J = 0x0285;
      /* opJLT_A_A (5b) */
      if (cmp_new < cmp_old) {
        register_PC = 0x1285;
        break;
      };
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opSUBdir_A_AA (73) */
      cmp_old = acc_a0 = register_A;
      register_A = (flag_C = (register_A + ((cmp_new = ram[register_I = 0x13]) ^ 0xFFF) + 1)) & 0xFFF; /* set regI addr */
      /* opSTAirg_A_A (e6) */
      ram[0x13] = register_A; /* store acc */

    case 0x1285:

      /* Invariants: register_P = 0x1 register_I = 0x13 */;
      /* opNOP_A_B (57) */
      /* opLDAimm_B_AA (00) */
      flag_C = acc_a0 = register_A;
      cmp_old = register_B; /* step back cmp flag */
      register_B = cmp_new = 0;
      /* opLDAimm_A_AA (08) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0x0800;
      /* opLDAdir_A_AA (ad) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = 0x1d]; /* set I register */

      /* opLDJimm_A_A (4e) */
      register_J = 0x028e;
      /* opJLT_A_A (5b) */
      if (cmp_new < cmp_old) {
        register_PC = 0x128e;
        break;
      };
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opSUBirg_A_AA (e8) */
      register_A = (flag_C = ((cmp_old = acc_a0 = register_A) + ((cmp_new = ram[register_I]) ^ 0xFFF) + 1)) & 0xFFF; /* ones compliment */

    case 0x128e:

      /* Invariants: register_P = 0x1 register_I = 0x1d */;
      /* opCMPdir_A_AA (b5) */
      cmp_old = acc_a0 = register_A; /* backup old acc */
      flag_C = (((cmp_new = ram[register_I = 0x15]) ^ 0xFFF) + 1 + register_A);
      /* opMULirg_A_AA (e3) */
      MUL();
      /* opMULirg_A_AA (e3) */
      MUL();
      /* opMULirg_A_AA (e3) */
      MUL();
      /* opMULirg_A_AA (e3) */
      MUL();
      /* opMULirg_A_AA (e3) */
      MUL();
      /* opMULirg_A_AA (e3) */
      MUL();
      /* opMULirg_A_AA (e3) */
      MUL();
      /* opMULirg_A_AA (e3) */
      MUL();
      /* opMULirg_A_AA (e3) */
      MUL();
      /* opLDAimm_A_AA (08) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0x0800;
      /* opCMPdir_A_AA (bd) */
      cmp_old = acc_a0 = register_A; /* backup old acc */
      flag_C = (((cmp_new = ram[register_I = 0x1d]) ^ 0xFFF) + 1 + register_A);
      /* opNOP_A_B (57) */
      /* opSTAdir_B_BB (d5) */
      ram[register_I = (register_P << 4) + 0x5] = register_B; /* set I register and store B to ram */
      /* opLDJimm_A_A (42) */
      register_J = 0x02a2;
      /* opJLT_A_A (5b) */
      if (cmp_new < cmp_old) {
        register_PC = 0x12a2;
        break;
      };
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opSUBdir_A_AA (75) */
      cmp_old = acc_a0 = register_A;
      register_A = (flag_C = (register_A + ((cmp_new = ram[register_I = 0x15]) ^ 0xFFF) + 1)) & 0xFFF; /* set regI addr */
      /* opSTAirg_A_A (e6) */
      ram[0x15] = register_A; /* store acc */

    case 0x12a2:

      /* Invariants: register_P = 0x1 register_I = 0x15 register_A = 0x800 */;
      /* opLDAdir_A_AA (af) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = 0x1f]; /* set I register */

      /* opSUBdir_A_AA (73) */
      cmp_old = acc_a0 = register_A;
      register_A = (flag_C = (register_A + ((cmp_new = ram[register_I = 0x13]) ^ 0xFFF) + 1)) & 0xFFF; /* set regI addr */
      /* opLSLe_A_AA (ec) */
      cmp_new = 0x0CEC;
      cmp_old = acc_a0 = register_A;
      flag_C = (0x0CEC + register_A);
      register_A = (register_A << 1) & 0x0FFF;
      /* opSTAirg_A_A (e6) */
      ram[0x13] = register_A; /* store acc */
      /* opLDAdir_A_AA (af) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = 0x1f]; /* set I register */

      /* opSUBdir_A_AA (75) */
      cmp_old = acc_a0 = register_A;
      register_A = (flag_C = (register_A + ((cmp_new = ram[register_I = 0x15]) ^ 0xFFF) + 1)) & 0xFFF; /* set regI addr */
      /* opLSLe_A_AA (ec) */
      cmp_new = 0x0CEC;
      cmp_old = acc_a0 = register_A;
      flag_C = (0x0CEC + register_A);
      register_A = (register_A << 1) & 0x0FFF;
      /* opSTAirg_A_A (e6) */
      ram[0x15] = register_A; /* store acc */
      /* opLDAdir_A_AA (a3) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = 0x13]; /* set I register */

      /* opASRe_A_AA (ed) */
      cmp_new = 0xDED; cmp_old = flag_C = acc_a0 = register_A;
      register_A = SEX(register_A); /* make signed */
      register_A = (((signed short int)(((signed short int)register_A) >> (signed short int)1)) /* SAR */) & 0xFFF;
      /* opASRe_A_AA (ed) */
      cmp_new = 0xDED; cmp_old = flag_C = acc_a0 = register_A;
      register_A = SEX(register_A); /* make signed */
      register_A = (((signed short int)(((signed short int)register_A) >> (signed short int)1)) /* SAR */) & 0xFFF;
      /* opADDdir_A_AA (60) */
      register_A = (flag_C = ((cmp_old = acc_a0 = register_A) + (cmp_new = ram[register_I = 0x10]))) & 0xFFF; /* do acc operation */
      /* opLDPimm_A_A (80) */
      register_P = 0x0; /* set page register */
      /* opSTAdir_A_A (d5) */
      ram[register_I = 0x05] = register_A; /* store acc to RAM */
      /* opLDPimm_A_A (81) */
      register_P = 0x1; /* set page register */
      /* opLDAdir_A_AA (a4) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = 0x14]; /* set I register */

      /* opASRe_A_AA (ed) */
      cmp_new = 0xDED; cmp_old = flag_C = acc_a0 = register_A;
      register_A = SEX(register_A); /* make signed */
      register_A = (((signed short int)(((signed short int)register_A) >> (signed short int)1)) /* SAR */) & 0xFFF;
      /* opASRe_A_AA (ed) */
      cmp_new = 0xDED; cmp_old = flag_C = acc_a0 = register_A;
      register_A = SEX(register_A); /* make signed */
      register_A = (((signed short int)(((signed short int)register_A) >> (signed short int)1)) /* SAR */) & 0xFFF;
      /* opADDdir_A_AA (61) */
      register_A = (flag_C = ((cmp_old = acc_a0 = register_A) + (cmp_new = ram[register_I = 0x11]))) & 0xFFF; /* do acc operation */
      /* opLDPimm_A_A (80) */
      register_P = 0x0; /* set page register */
      /* opSTAdir_A_A (d6) */
      ram[register_I = 0x06] = register_A; /* store acc to RAM */
      /* opLDPimm_A_A (81) */
      register_P = 0x1; /* set page register */
      /* opLDAdir_A_AA (a5) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = 0x15]; /* set I register */

      /* opASRe_A_AA (ed) */
      cmp_new = 0xDED; cmp_old = flag_C = acc_a0 = register_A;
      register_A = SEX(register_A); /* make signed */
      register_A = (((signed short int)(((signed short int)register_A) >> (signed short int)1)) /* SAR */) & 0xFFF;
      /* opASRe_A_AA (ed) */
      cmp_new = 0xDED; cmp_old = flag_C = acc_a0 = register_A;
      register_A = SEX(register_A); /* make signed */
      register_A = (((signed short int)(((signed short int)register_A) >> (signed short int)1)) /* SAR */) & 0xFFF;
      /* opADDdir_A_AA (62) */
      register_A = (flag_C = ((cmp_old = acc_a0 = register_A) + (cmp_new = ram[register_I = 0x12]))) & 0xFFF; /* do acc operation */
      /* opLSRe_A_AA (eb) */
      cmp_new = 0x0BEB; cmp_old = acc_a0 = register_A; flag_C = (0x0BEB + register_A);
      register_A >>= 1;
      /* opLDPimm_A_A (80) */
      register_P = 0x0; /* set page register */
      /* opSTAdir_A_A (d9) */
      ram[register_I = 0x09] = register_A; /* store acc to RAM */
      /* opLDAimm_A_AA (02) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0x0200;
      /* opSTAdir_A_A (dd) */
      ram[register_I = 0x0d] = 0x200; /* store acc to RAM */
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opSTAdir_A_A (d4) */
      ram[register_I = 0x04] = 0x000; /* store acc to RAM */

    case 0x12c3:

      /* Invariants: register_P = 0x0 register_I = 0x04 register_A = 0x00 */;
      /* opLDAdir_A_AA (ad) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = 0x0d]; /* set I register */

      /* opCMPdir_A_AA (b9) */
      cmp_old = acc_a0 = register_A; /* backup old acc */
      flag_C = (((cmp_new = ram[register_I = 0x09]) ^ 0xFFF) + 1 + register_A);
      /* opLDJimm_A_A (43) */
      register_J = 0x0333;
      /* opJLT_A_A (5b) */
      if (cmp_new < cmp_old) {
        register_PC = 0x1333;
        break;
      };
      /* opNOP_A_B (57) */
      /* opLDAimm_B_AA (00) */
      flag_C = acc_a0 = register_A;
      cmp_old = register_B; /* step back cmp flag */
      register_B = cmp_new = 0;
      /* opLSLDe_A_AA (ef) */
      cmp_new = 0x0FEF; cmp_old = acc_a0 = register_A; flag_C = (0x0FEF + register_A);
      register_A = (register_A << 1) & 0xFFF; register_B = (register_B << 1) & 0xFFF;
      /* opNOP_A_B (57) */
      /* opADDimm_B_AA (21) */
      acc_a0 = register_A; /* save old accA bit0 */
      cmp_old = register_B; /* store old acc for later */
      register_B = (flag_C = (register_B + (cmp_new = 0x01))) & 0xFFF; /* add values */
      /* opSUBirg_A_AA (e8) */
      register_A = (flag_C = ((cmp_old = acc_a0 = register_A) + ((cmp_new = ram[register_I]) ^ 0xFFF) + 1)) & 0xFFF; /* ones compliment */
      /* opLDJimm_A_A (44) */
      register_J = 0x02d4;
      /* opJLT_A_A (5b) */
      if (cmp_new < cmp_old) {
        register_PC = 0x12d4;
        break;
      };
      /* opAWDirg_A_AA (e7) */
      cmp_old = acc_a0 = register_A;
      register_A = (flag_C = (register_A + (cmp_new = ram[0x09]))) & 0xFFF;
      set_watchdog();
      /* opNOP_A_B (57) */
      /* opSUBimm_B_AA (31) */
      acc_a0 = register_A;
      register_B = (flag_C = ((cmp_old = register_B) + ((cmp_new = 0x1) ^ 0xFFF) + 1)) & 0xFFF; /* 1's-comp add */

    case 0x12d4:

      /* Invariants: register_P = 0x0 register_I = 0x09 register_A = 0x400 */;
      /* opLSLDe_A_AA (ef) */
      cmp_new = 0x0FEF; cmp_old = acc_a0 = register_A; flag_C = (0x0FEF + register_A);
      register_A = (register_A << 1) & 0xFFF; register_B = (register_B << 1) & 0xFFF;
      /* opNOP_A_B (57) */
      /* opADDimm_B_AA (21) */
      acc_a0 = register_A; /* save old accA bit0 */
      cmp_old = register_B; /* store old acc for later */
      register_B = (flag_C = (register_B + (cmp_new = 0x01))) & 0xFFF; /* add values */
      /* opSUBirg_A_AA (e8) */
      register_A = (flag_C = ((cmp_old = acc_a0 = register_A) + ((cmp_new = ram[register_I]) ^ 0xFFF) + 1)) & 0xFFF; /* ones compliment */
      /* opLDJimm_A_A (4e) */
      register_J = 0x02de;
      /* opJLT_A_A (5b) */
      if (cmp_new < cmp_old) {
        register_PC = 0x12de;
        break;
      };
      /* opAWDirg_A_AA (e7) */
      cmp_old = acc_a0 = register_A;
      register_A = (flag_C = (register_A + (cmp_new = ram[0x09]))) & 0xFFF;
      set_watchdog();
      /* opNOP_A_B (57) */
      /* opSUBimm_B_AA (31) */
      acc_a0 = register_A;
      register_B = (flag_C = ((cmp_old = register_B) + ((cmp_new = 0x1) ^ 0xFFF) + 1)) & 0xFFF; /* 1's-comp add */

    case 0x12de:

      /* Invariants: register_P = 0x0 register_I = 0x09 */;
      /* opLSLDe_A_AA (ef) */
      cmp_new = 0x0FEF; cmp_old = acc_a0 = register_A; flag_C = (0x0FEF + register_A);
      register_A = (register_A << 1) & 0xFFF; register_B = (register_B << 1) & 0xFFF;
      /* opNOP_A_B (57) */
      /* opADDimm_B_AA (21) */
      acc_a0 = register_A; /* save old accA bit0 */
      cmp_old = register_B; /* store old acc for later */
      register_B = (flag_C = (register_B + (cmp_new = 0x01))) & 0xFFF; /* add values */
      /* opSUBirg_A_AA (e8) */
      register_A = (flag_C = ((cmp_old = acc_a0 = register_A) + ((cmp_new = ram[register_I]) ^ 0xFFF) + 1)) & 0xFFF; /* ones compliment */
      /* opLDJimm_A_A (48) */
      register_J = 0x02e8;
      /* opJLT_A_A (5b) */
      if (cmp_new < cmp_old) {
        register_PC = 0x12e8;
        break;
      };
      /* opAWDirg_A_AA (e7) */
      cmp_old = acc_a0 = register_A;
      register_A = (flag_C = (register_A + (cmp_new = ram[0x09]))) & 0xFFF;
      set_watchdog();
      /* opNOP_A_B (57) */
      /* opSUBimm_B_AA (31) */
      acc_a0 = register_A;
      register_B = (flag_C = ((cmp_old = register_B) + ((cmp_new = 0x1) ^ 0xFFF) + 1)) & 0xFFF; /* 1's-comp add */

    case 0x12e8:

      /* Invariants: register_P = 0x0 register_I = 0x09 */;
      /* opLSLDe_A_AA (ef) */
      cmp_new = 0x0FEF; cmp_old = acc_a0 = register_A; flag_C = (0x0FEF + register_A);
      register_A = (register_A << 1) & 0xFFF; register_B = (register_B << 1) & 0xFFF;
      /* opNOP_A_B (57) */
      /* opADDimm_B_AA (21) */
      acc_a0 = register_A; /* save old accA bit0 */
      cmp_old = register_B; /* store old acc for later */
      register_B = (flag_C = (register_B + (cmp_new = 0x01))) & 0xFFF; /* add values */
      /* opSUBirg_A_AA (e8) */
      register_A = (flag_C = ((cmp_old = acc_a0 = register_A) + ((cmp_new = ram[register_I]) ^ 0xFFF) + 1)) & 0xFFF; /* ones compliment */
      /* opLDJimm_A_A (42) */
      register_J = 0x02f2;
      /* opJLT_A_A (5b) */
      if (cmp_new < cmp_old) {
        register_PC = 0x12f2;
        break;
      };
      /* opAWDirg_A_AA (e7) */
      cmp_old = acc_a0 = register_A;
      register_A = (flag_C = (register_A + (cmp_new = ram[0x09]))) & 0xFFF;
      set_watchdog();
      /* opNOP_A_B (57) */
      /* opSUBimm_B_AA (31) */
      acc_a0 = register_A;
      register_B = (flag_C = ((cmp_old = register_B) + ((cmp_new = 0x1) ^ 0xFFF) + 1)) & 0xFFF; /* 1's-comp add */

    case 0x12f2:

      /* Invariants: register_P = 0x0 register_I = 0x09 */;
      /* opLSLDe_A_AA (ef) */
      cmp_new = 0x0FEF; cmp_old = acc_a0 = register_A; flag_C = (0x0FEF + register_A);
      register_A = (register_A << 1) & 0xFFF; register_B = (register_B << 1) & 0xFFF;
      /* opNOP_A_B (57) */
      /* opADDimm_B_AA (21) */
      acc_a0 = register_A; /* save old accA bit0 */
      cmp_old = register_B; /* store old acc for later */
      register_B = (flag_C = (register_B + (cmp_new = 0x01))) & 0xFFF; /* add values */
      /* opSUBirg_A_AA (e8) */
      register_A = (flag_C = ((cmp_old = acc_a0 = register_A) + ((cmp_new = ram[register_I]) ^ 0xFFF) + 1)) & 0xFFF; /* ones compliment */
      /* opLDJimm_A_A (4c) */
      register_J = 0x02fc;
      /* opJLT_A_A (5b) */
      if (cmp_new < cmp_old) {
        register_PC = 0x12fc;
        break;
      };
      /* opAWDirg_A_AA (e7) */
      cmp_old = acc_a0 = register_A;
      register_A = (flag_C = (register_A + (cmp_new = ram[0x09]))) & 0xFFF;
      set_watchdog();
      /* opNOP_A_B (57) */
      /* opSUBimm_B_AA (31) */
      acc_a0 = register_A;
      register_B = (flag_C = ((cmp_old = register_B) + ((cmp_new = 0x1) ^ 0xFFF) + 1)) & 0xFFF; /* 1's-comp add */

    case 0x12fc:

      /* Invariants: register_P = 0x0 register_I = 0x09 */;
      /* opLSLDe_A_AA (ef) */
      cmp_new = 0x0FEF; cmp_old = acc_a0 = register_A; flag_C = (0x0FEF + register_A);
      register_A = (register_A << 1) & 0xFFF; register_B = (register_B << 1) & 0xFFF;
      /* opNOP_A_B (57) */
      /* opADDimm_B_AA (21) */
      acc_a0 = register_A; /* save old accA bit0 */
      cmp_old = register_B; /* store old acc for later */
      register_B = (flag_C = (register_B + (cmp_new = 0x01))) & 0xFFF; /* add values */
      /* opSUBirg_A_AA (e8) */
      register_A = (flag_C = ((cmp_old = acc_a0 = register_A) + ((cmp_new = ram[register_I]) ^ 0xFFF) + 1)) & 0xFFF; /* ones compliment */
      /* opLDJimm_A_A (46) */
      register_J = 0x0306;
      /* opJLT_A_A (5b) */
      if (cmp_new < cmp_old) {
        register_PC = 0x1306;
        break;
      };
      /* opAWDirg_A_AA (e7) */
      cmp_old = acc_a0 = register_A;
      register_A = (flag_C = (register_A + (cmp_new = ram[0x09]))) & 0xFFF;
      set_watchdog();
      /* opNOP_A_B (57) */
      /* opSUBimm_B_AA (31) */
      acc_a0 = register_A;
      register_B = (flag_C = ((cmp_old = register_B) + ((cmp_new = 0x1) ^ 0xFFF) + 1)) & 0xFFF; /* 1's-comp add */

    case 0x1306:

      /* Invariants: register_P = 0x0 register_I = 0x09 */;
      /* opLSLDe_A_AA (ef) */
      cmp_new = 0x0FEF; cmp_old = acc_a0 = register_A; flag_C = (0x0FEF + register_A);
      register_A = (register_A << 1) & 0xFFF; register_B = (register_B << 1) & 0xFFF;
      /* opNOP_A_B (57) */
      /* opADDimm_B_AA (21) */
      acc_a0 = register_A; /* save old accA bit0 */
      cmp_old = register_B; /* store old acc for later */
      register_B = (flag_C = (register_B + (cmp_new = 0x01))) & 0xFFF; /* add values */
      /* opSUBirg_A_AA (e8) */
      register_A = (flag_C = ((cmp_old = acc_a0 = register_A) + ((cmp_new = ram[register_I]) ^ 0xFFF) + 1)) & 0xFFF; /* ones compliment */
      /* opLDJimm_A_A (40) */
      register_J = 0x0310;
      /* opJLT_A_A (5b) */
      if (cmp_new < cmp_old) {
        register_PC = 0x1310;
        break;
      };
      /* opAWDirg_A_AA (e7) */
      cmp_old = acc_a0 = register_A;
      register_A = (flag_C = (register_A + (cmp_new = ram[0x09]))) & 0xFFF;
      set_watchdog();
      /* opNOP_A_B (57) */
      /* opSUBimm_B_AA (31) */
      acc_a0 = register_A;
      register_B = (flag_C = ((cmp_old = register_B) + ((cmp_new = 0x1) ^ 0xFFF) + 1)) & 0xFFF; /* 1's-comp add */

    case 0x1310:

      /* Invariants: register_P = 0x0 register_I = 0x09 */;
      /* opLSLDe_A_AA (ef) */
      cmp_new = 0x0FEF; cmp_old = acc_a0 = register_A; flag_C = (0x0FEF + register_A);
      register_A = (register_A << 1) & 0xFFF; register_B = (register_B << 1) & 0xFFF;
      /* opNOP_A_B (57) */
      /* opADDimm_B_AA (21) */
      acc_a0 = register_A; /* save old accA bit0 */
      cmp_old = register_B; /* store old acc for later */
      register_B = (flag_C = (register_B + (cmp_new = 0x01))) & 0xFFF; /* add values */
      /* opSUBirg_A_AA (e8) */
      register_A = (flag_C = ((cmp_old = acc_a0 = register_A) + ((cmp_new = ram[register_I]) ^ 0xFFF) + 1)) & 0xFFF; /* ones compliment */
      /* opLDJimm_A_A (4a) */
      register_J = 0x031a;
      /* opJLT_A_A (5b) */
      if (cmp_new < cmp_old) {
        register_PC = 0x131a;
        break;
      };
      /* opAWDirg_A_AA (e7) */
      cmp_old = acc_a0 = register_A;
      register_A = (flag_C = (register_A + (cmp_new = ram[0x09]))) & 0xFFF;
      set_watchdog();
      /* opNOP_A_B (57) */
      /* opSUBimm_B_AA (31) */
      acc_a0 = register_A;
      register_B = (flag_C = ((cmp_old = register_B) + ((cmp_new = 0x1) ^ 0xFFF) + 1)) & 0xFFF; /* 1's-comp add */

    case 0x131a:

      /* Invariants: register_P = 0x0 register_I = 0x09 */;
      /* opLSLDe_A_AA (ef) */
      cmp_new = 0x0FEF; cmp_old = acc_a0 = register_A; flag_C = (0x0FEF + register_A);
      register_A = (register_A << 1) & 0xFFF; register_B = (register_B << 1) & 0xFFF;
      /* opNOP_A_B (57) */
      /* opADDimm_B_AA (21) */
      acc_a0 = register_A; /* save old accA bit0 */
      cmp_old = register_B; /* store old acc for later */
      register_B = (flag_C = (register_B + (cmp_new = 0x01))) & 0xFFF; /* add values */
      /* opSUBirg_A_AA (e8) */
      register_A = (flag_C = ((cmp_old = acc_a0 = register_A) + ((cmp_new = ram[register_I]) ^ 0xFFF) + 1)) & 0xFFF; /* ones compliment */
      /* opLDJimm_A_A (44) */
      register_J = 0x0324;
      /* opJLT_A_A (5b) */
      if (cmp_new < cmp_old) {
        register_PC = 0x1324;
        break;
      };
      /* opAWDirg_A_AA (e7) */
      cmp_old = acc_a0 = register_A;
      register_A = (flag_C = (register_A + (cmp_new = ram[0x09]))) & 0xFFF;
      set_watchdog();
      /* opNOP_A_B (57) */
      /* opSUBimm_B_AA (31) */
      acc_a0 = register_A;
      register_B = (flag_C = ((cmp_old = register_B) + ((cmp_new = 0x1) ^ 0xFFF) + 1)) & 0xFFF; /* 1's-comp add */

    case 0x1324:

      /* Invariants: register_P = 0x0 register_I = 0x09 */;
      /* opLSLDe_A_AA (ef) */
      cmp_new = 0x0FEF; cmp_old = acc_a0 = register_A; flag_C = (0x0FEF + register_A);
      register_A = (register_A << 1) & 0xFFF; register_B = (register_B << 1) & 0xFFF;
      /* opNOP_A_B (57) */
      /* opADDimm_B_AA (21) */
      acc_a0 = register_A; /* save old accA bit0 */
      cmp_old = register_B; /* store old acc for later */
      register_B = (flag_C = (register_B + (cmp_new = 0x01))) & 0xFFF; /* add values */
      /* opSUBirg_A_AA (e8) */
      register_A = (flag_C = ((cmp_old = acc_a0 = register_A) + ((cmp_new = ram[register_I]) ^ 0xFFF) + 1)) & 0xFFF; /* ones compliment */
      /* opLDJimm_A_A (4e) */
      register_J = 0x032e;
      /* opJLT_A_A (5b) */
      if (cmp_new < cmp_old) {
        register_PC = 0x132e;
        break;
      };
      /* opAWDirg_A_AA (e7) */
      cmp_old = acc_a0 = register_A;
      register_A = (flag_C = (register_A + (cmp_new = ram[0x09]))) & 0xFFF;
      set_watchdog();
      /* opNOP_A_B (57) */
      /* opSUBimm_B_AA (31) */
      acc_a0 = register_A;
      register_B = (flag_C = ((cmp_old = register_B) + ((cmp_new = 0x1) ^ 0xFFF) + 1)) & 0xFFF; /* 1's-comp add */

    case 0x132e:

      /* Invariants: register_P = 0x0 register_I = 0x09 */;
      /* opNOP_A_B (57) */
      /* opSTAdir_B_BB (d9) */
      ram[register_I = (register_P << 4) + 0x9] = register_B; /* set I register and store B to ram */
      /* opLDJimm_A_A (4b) */
      register_J = 0x033b;
      /* opJMP_A_A (58) */
      {
        register_PC = 0x133b;
        break;
      };

    case 0x1333:

      /* opSUBirg_A_AA (e8) */
      register_A = (flag_C = ((cmp_old = acc_a0 = register_A) + ((cmp_new = ram[register_I]) ^ 0xFFF) + 1)) & 0xFFF; /* ones compliment */
      /* opSTAdir_A_A (dd) */
      ram[register_I = (register_P << 4) + 0xd] = register_A; /* store acc to RAM */
      /* opLDAdir_A_AA (a4) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = (register_P << 4) + 0x04]; /* set I register */

      /* opADDimm_A_AA (21) */
      register_A = (flag_C = ((cmp_old = acc_a0 = register_A) + (cmp_new = 0x1))) & 0xFFF; /* add values, save carry */

      /* opSTAirg_A_A (e6) */
      ram[register_I] = register_A; /* store acc */
      /* opLDJimm_A_A (43) */
      register_J = 0x02c3;
      /* opJMP_A_A (58) */
      {
        register_PC = 0x12c3;
        break;
      };

    case 0x133b:

      /* Invariants: register_P = 0x0 register_I = 0x09 */;
      /* opNOP_A_B (57) */
      /* opLDAimm_B_AA (00) */
      flag_C = acc_a0 = register_A;
      cmp_old = register_B; /* step back cmp flag */
      register_B = cmp_new = 0;
      /* opLDAimm_A_AA (08) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0x0800;
      /* opLDAdir_A_AA (a5) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = 0x05]; /* set I register */

      /* opLDJimm_A_A (44) */
      register_J = 0x0344;
      /* opJLT_A_A (5b) */
      if (cmp_new < cmp_old) {
        register_PC = 0x1344;
        break;
      };
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opSUBirg_A_AA (e8) */
      register_A = (flag_C = ((cmp_old = acc_a0 = register_A) + ((cmp_new = ram[register_I]) ^ 0xFFF) + 1)) & 0xFFF; /* ones compliment */

    case 0x1344:

      /* Invariants: register_P = 0x0 register_I = 0x05 */;
      /* opCMPdir_A_AA (b9) */
      cmp_old = acc_a0 = register_A; /* backup old acc */
      flag_C = (((cmp_new = ram[register_I = 0x09]) ^ 0xFFF) + 1 + register_A);
      /* opMULirg_A_AA (e3) */
      MUL();
      /* opMULirg_A_AA (e3) */
      MUL();
      /* opMULirg_A_AA (e3) */
      MUL();
      /* opMULirg_A_AA (e3) */
      MUL();
      /* opMULirg_A_AA (e3) */
      MUL();
      /* opMULirg_A_AA (e3) */
      MUL();
      /* opMULirg_A_AA (e3) */
      MUL();
      /* opMULirg_A_AA (e3) */
      MUL();
      /* opMULirg_A_AA (e3) */
      MUL();
      /* opMULirg_A_AA (e3) */
      MUL();
      /* opMULirg_A_AA (e3) */
      MUL();
      /* opNOP_A_B (57) */
      /* opSTAdir_B_BB (d7) */
      ram[register_I = (register_P << 4) + 0x7] = register_B; /* set I register and store B to ram */
      /* opLDAimm_A_AA (08) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0x0800;
      /* opCMPdir_A_AA (b5) */
      cmp_old = acc_a0 = register_A; /* backup old acc */
      flag_C = (((cmp_new = ram[register_I = 0x05]) ^ 0xFFF) + 1 + register_A);
      /* opLDJimm_A_A (4a) */
      register_J = 0x035a;
      /* opJLT_A_A (5b) */
      if (cmp_new < cmp_old) {
        register_PC = 0x135a;
        break;
      };
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opSUBdir_A_AA (77) */
      cmp_old = acc_a0 = register_A;
      register_A = (flag_C = (register_A + ((cmp_new = ram[register_I = 0x07]) ^ 0xFFF) + 1)) & 0xFFF; /* set regI addr */
      /* opSTAirg_A_A (e6) */
      ram[0x07] = register_A; /* store acc */

    case 0x135a:

      /* Invariants: register_P = 0x0 */;
      /* opNOP_A_B (57) */
      /* opLDAimm_B_AA (00) */
      flag_C = acc_a0 = register_A;
      cmp_old = register_B; /* step back cmp flag */
      register_B = cmp_new = 0;
      /* opLDAimm_A_AA (08) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0x0800;
      /* opLDAdir_A_AA (a6) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = 0x06]; /* set I register */

      /* opLDJimm_A_A (43) */
      register_J = 0x0363;
      /* opJLT_A_A (5b) */
      if (cmp_new < cmp_old) {
        register_PC = 0x1363;
        break;
      };
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opSUBirg_A_AA (e8) */
      register_A = (flag_C = ((cmp_old = acc_a0 = register_A) + ((cmp_new = ram[register_I]) ^ 0xFFF) + 1)) & 0xFFF; /* ones compliment */

    case 0x1363:

      /* Invariants: register_P = 0x0 register_I = 0x06 */;
      /* opCMPdir_A_AA (b9) */
      cmp_old = acc_a0 = register_A; /* backup old acc */
      flag_C = (((cmp_new = ram[register_I = 0x09]) ^ 0xFFF) + 1 + register_A);
      /* opMULirg_A_AA (e3) */
      MUL();
      /* opMULirg_A_AA (e3) */
      MUL();
      /* opMULirg_A_AA (e3) */
      MUL();
      /* opMULirg_A_AA (e3) */
      MUL();
      /* opMULirg_A_AA (e3) */
      MUL();
      /* opMULirg_A_AA (e3) */
      MUL();
      /* opMULirg_A_AA (e3) */
      MUL();
      /* opMULirg_A_AA (e3) */
      MUL();
      /* opMULirg_A_AA (e3) */
      MUL();
      /* opMULirg_A_AA (e3) */
      MUL();
      /* opMULirg_A_AA (e3) */
      MUL();
      /* opNOP_A_B (57) */
      /* opSTAdir_B_BB (d8) */
      ram[register_I = (register_P << 4) + 0x8] = register_B; /* set I register and store B to ram */
      /* opLDAimm_A_AA (08) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0x0800;
      /* opCMPdir_A_AA (b6) */
      cmp_old = acc_a0 = register_A; /* backup old acc */
      flag_C = (((cmp_new = ram[register_I = 0x06]) ^ 0xFFF) + 1 + register_A);
      /* opLDJimm_A_A (49) */
      register_J = 0x0379;
      /* opJLT_A_A (5b) */
      if (cmp_new < cmp_old) {
        register_PC = 0x1379;
        break;
      };
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opSUBdir_A_AA (78) */
      cmp_old = acc_a0 = register_A;
      register_A = (flag_C = (register_A + ((cmp_new = ram[register_I = 0x08]) ^ 0xFFF) + 1)) & 0xFFF; /* set regI addr */
      /* opSTAirg_A_A (e6) */
      ram[0x08] = register_A; /* store acc */

    case 0x1379:

      /* Invariants: register_P = 0x0 */;
      /* opNOP_A_B (57) */
      /* opLDAimm_B_AA (00) */
      flag_C = acc_a0 = register_A;
      cmp_old = register_B; /* step back cmp flag */
      register_B = cmp_new = 0;
      /* opNOP_A_B (57) */
      /* opSUBdir_B_AA (74) */
      acc_a0 = register_A;
      register_B = (flag_C = ((cmp_old = register_B) + ((cmp_new = ram[register_I = (register_P << 4) + 0x04]) ^ 0xFFF) + 1)) & 0xFFF; /* ones compliment */
      /* opLDJimm_A_A (4b) */
      register_J = 0x038b;
      /* opJEQ_A_AA (5c) */
      if (cmp_new == cmp_old) {
        register_PC = 0x138b;
        break;
      };
      /* opLDJimm_A_A (42) */
      register_J = 0x0382;

    case 0x1382:

      /* opLDAdir_A_AA (a5) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = (register_P << 4) + 0x05]; /* set I register */

      /* opADDdir_A_AA (67) */
      register_A = (flag_C = ((cmp_old = acc_a0 = register_A) + (cmp_new = ram[register_I = (register_P << 4) + 0x7]))) & 0xFFF; /* do acc operation */
      /* opSTAirg_A_A (e6) */
      ram[register_I] = register_A; /* store acc */
      /* opLDAdir_A_AA (a6) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = (register_P << 4) + 0x06]; /* set I register */

      /* opADDdir_A_AA (68) */
      register_A = (flag_C = ((cmp_old = acc_a0 = register_A) + (cmp_new = ram[register_I = (register_P << 4) + 0x8]))) & 0xFFF; /* do acc operation */
      /* opSTAirg_A_A (e6) */
      ram[register_I] = register_A; /* store acc */
      /* opNOP_A_B (57) */
      /* opADDimm_B_AA (21) */
      acc_a0 = register_A; /* save old accA bit0 */
      cmp_old = register_B; /* store old acc for later */
      register_B = (flag_C = (register_B + (cmp_new = 0x01))) & 0xFFF; /* add values */
      /* opJNC_A_AA (5d) */
      if (!(flag_C & CARRYBIT)) {
        register_PC = 0x1382;
        break;
      };

    case 0x138b:

      /* Invariants: register_P = 0x0 register_I = 0x04 */;
      /* opLDAdir_A_AA (a3) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = 0x03]; /* set I register */

      /* opADDimmX_A_AA (20) */
      register_A = (flag_C = ((acc_a0 = cmp_old = register_A) + (cmp_new = 0x00e0))) & 0xFFF; /* add values */
      /* opSTAdir_A_A (de) */
      ram[register_I = 0x0e] = register_A; /* store acc to RAM */
      /* opADDimm_A_AA (21) */
      register_A = (flag_C = ((cmp_old = acc_a0 = register_A) + (cmp_new = 0x1))) & 0xFFF; /* add values, save carry */

      /* opSTAdir_A_A (df) */
      ram[register_I = 0x0f] = register_A; /* store acc to RAM */
      /* opLDAimm_A_AA (02) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0x0200;
      /* opADDdir_A_AA (67) */
      register_A = (flag_C = ((cmp_old = acc_a0 = register_A) + (cmp_new = ram[register_I = 0x07]))) & 0xFFF; /* do acc operation */
      /* opSTAirg_A_A (e6) */
      ram[0x07] = register_A; /* store acc */
      /* opLDIdir_A_A (ce) */
      register_I = ram[0x0e] & 0xff; /* set new register_I (8 bits) */
      /* opSTAirg_A_A (e6) */
      ram[register_I] = register_A; /* store acc */
      /* opLDAimm_A_AA (03) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0x0300;
      /* opLSRe_A_AA (eb) */
      cmp_new = 0x0BEB; cmp_old = acc_a0 = register_A; flag_C = (0x0BEB + register_A);
      register_A >>= 1;
      /* opADDdir_A_AA (68) */
      register_A = (flag_C = ((cmp_old = acc_a0 = register_A) + (cmp_new = ram[register_I = 0x08]))) & 0xFFF; /* do acc operation */
      /* opSTAirg_A_A (e6) */
      ram[0x08] = register_A; /* store acc */
      /* opLDIdir_A_A (cf) */
      register_I = ram[0x0f] & 0xff; /* set new register_I (8 bits) */
      /* opSTAirg_A_A (e6) */
      ram[register_I] = register_A; /* store acc */

    case 0x139c:

      /* opLDPimm_A_A (81) */
      register_P = 0x1; /* set page register */
      /* opLDAdir_A_AA (a7) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = 0x17]; /* set I register */

      /* opXLT_A_AA (e2) */
      cmp_old = register_A; register_A = cmp_new = rom[0x1000 | register_A]; /* new acc value */
      /* opNOP_A_A (5f) */
      /* opCMPdir_A_AA (b8) */
      cmp_old = acc_a0 = register_A; /* backup old acc */
      flag_C = (((cmp_new = ram[register_I = (register_P << 4) + 0x8]) ^ 0xFFF) + 1 + register_A);
      /* opLDJimm_A_A (49) */
      register_J = 0x0479;
      /* opJEQ_A_AA (5c) */
      if (cmp_new == cmp_old) {
        register_PC = 0x1479;
        break;
      };
      /* opLDJimm_A_A (44) */
      register_J = 0x0134;
      /* opJDR_A_A (5a) */
      /* opLDPimm_A_A (80) */
      register_P = 0x0; /* set page register */
      /* opSTAdir_A_A (db) */
      ram[register_I = 0x0b] = register_A; /* store acc to RAM */
      /* opLSRe_A_AA (eb) */
      cmp_new = 0x0BEB; cmp_old = acc_a0 = register_A; flag_C = (0x0BEB + register_A);
      register_A >>= 1;
      /* opLSRe_A_AA (eb) */
      cmp_new = 0x0BEB; cmp_old = acc_a0 = register_A; flag_C = (0x0BEB + register_A);
      register_A >>= 1;
      /* opLSRe_A_AA (eb) */
      cmp_new = 0x0BEB; cmp_old = acc_a0 = register_A; flag_C = (0x0BEB + register_A);
      register_A >>= 1;
      /* opLSRe_A_AA (eb) */
      cmp_new = 0x0BEB; cmp_old = acc_a0 = register_A; flag_C = (0x0BEB + register_A);
      register_A >>= 1;
      /* opLSLe_A_AA (ec) */
      cmp_new = 0x0CEC;
      cmp_old = acc_a0 = register_A;
      flag_C = (0x0CEC + register_A);
      register_A = (register_A << 1) & 0x0FFF;
      /* opCMPdir_A_AA (b3) */
      cmp_old = acc_a0 = register_A; /* backup old acc */
      flag_C = (((cmp_new = ram[register_I = 0x03]) ^ 0xFFF) + 1 + register_A);
      /* opJLT_A_A (5b) */
      if (cmp_new < cmp_old) {
        register_PC = 0x1134;
        break;
      };
      /* opSTAdir_A_A (da) */
      ram[register_I = 0x0a] = register_A; /* store acc to RAM */
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opADDimm_A_AA (2f) */
      cmp_new = 0xf; cmp_old = acc_a0 = register_A; register_A = flag_C = 0x000f;

      /* opCMPdir_A_AA (bb) */
      cmp_old = acc_a0 = register_A; /* backup old acc */
      flag_C = (((cmp_new = ram[register_I = 0x0b]) ^ 0xFFF) + 1 + register_A);
      /* opANDirg_A_AA (e9) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A &= (cmp_new = ram[register_I]);
      /* opLSLe_A_AA (ec) */
      cmp_new = 0x0CEC;
      cmp_old = acc_a0 = register_A;
      flag_C = (0x0CEC + register_A);
      register_A = (register_A << 1) & 0x0FFF;
      /* opSTAirg_A_A (e6) */
      ram[0x0b] = register_A; /* store acc */
      /* opCMPdir_A_AA (b3) */
      cmp_old = acc_a0 = register_A; /* backup old acc */
      flag_C = (((cmp_new = ram[register_I = 0x03]) ^ 0xFFF) + 1 + register_A);
      /* opJLT_A_A (5b) */
      if (cmp_new < cmp_old) {
        register_PC = 0x1134;
        break;
      };
      /* opLDAdir_A_AA (aa) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = 0x0a]; /* set I register */

      /* opADDimmX_A_AA (20) */
      register_A = (flag_C = ((acc_a0 = cmp_old = register_A) + (cmp_new = 0x00e0))) & 0xFFF; /* add values */
      /* opSTAdir_A_A (de) */
      ram[register_I = 0x0e] = register_A; /* store acc to RAM */
      /* opADDimm_A_AA (21) */
      register_A = (flag_C = ((cmp_old = acc_a0 = register_A) + (cmp_new = 0x1))) & 0xFFF; /* add values, save carry */

      /* opSTAdir_A_A (df) */
      ram[register_I = 0x0f] = register_A; /* store acc to RAM */
      /* opLDIdir_A_A (cf) */
      register_I = ram[0x0f] & 0xff; /* set new register_I (8 bits) */
      /* opLDAirg_A_AA (ea) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = ram[register_I];
      /* opSTAdir_A_A (d6) */
      ram[register_I = 0x06] = register_A; /* store acc to RAM */
      /* opLDAimm_A_AA (03) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0x0300;
      /* opCMPdir_A_AA (b6) */
      cmp_old = acc_a0 = register_A; /* backup old acc */
      flag_C = (((cmp_new = ram[register_I = 0x06]) ^ 0xFFF) + 1 + register_A);
      /* opLDJimm_A_A (44) */
      register_J = 0x0454;
      /* opJNC_A_AA (5d) */
      if (!(flag_C & CARRYBIT)) {
        register_PC = 0x1454;
        break;
      };
      /* opLDIdir_A_A (ce) */
      register_I = ram[0x0e] & 0xff; /* set new register_I (8 bits) */
      /* opLDAirg_A_AA (ea) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = ram[register_I];
      /* opSTAdir_A_A (d5) */
      ram[register_I = 0x05] = register_A; /* store acc to RAM */
      /* opLDAimm_A_AA (04) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0x0400;
      /* opCMPdir_A_AA (b5) */
      cmp_old = acc_a0 = register_A; /* backup old acc */
      flag_C = (((cmp_new = ram[register_I = 0x05]) ^ 0xFFF) + 1 + register_A);
      /* opJNC_A_AA (5d) */
      if (!(flag_C & CARRYBIT)) {
        register_PC = 0x1454;
        break;
      };
      /* opLDAdir_A_AA (ab) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = 0x0b]; /* set I register */

      /* opADDimmX_A_AA (20) */
      register_A = (flag_C = ((acc_a0 = cmp_old = register_A) + (cmp_new = 0x00e0))) & 0xFFF; /* add values */
      /* opSTAdir_A_A (de) */
      ram[register_I = 0x0e] = register_A; /* store acc to RAM */
      /* opADDimm_A_AA (21) */
      register_A = (flag_C = ((cmp_old = acc_a0 = register_A) + (cmp_new = 0x1))) & 0xFFF; /* add values, save carry */

      /* opSTAdir_A_A (df) */
      ram[register_I = 0x0f] = register_A; /* store acc to RAM */
      /* opLDIdir_A_A (cf) */
      register_I = ram[0x0f] & 0xff; /* set new register_I (8 bits) */
      /* opLDAirg_A_AA (ea) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = ram[register_I];
      /* opSTAdir_A_A (d8) */
      ram[register_I = 0x08] = register_A; /* store acc to RAM */
      /* opLDAimm_A_AA (03) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0x0300;
      /* opCMPdir_A_AA (b8) */
      cmp_old = acc_a0 = register_A; /* backup old acc */
      flag_C = (((cmp_new = ram[register_I = 0x08]) ^ 0xFFF) + 1 + register_A);
      /* opJNC_A_AA (5d) */
      if (!(flag_C & CARRYBIT)) {
        register_PC = 0x1454;
        break;
      };
      /* opLDIdir_A_A (ce) */
      register_I = ram[0x0e] & 0xff; /* set new register_I (8 bits) */
      /* opLDAirg_A_AA (ea) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = ram[register_I];
      /* opSTAdir_A_A (d7) */
      ram[register_I = 0x07] = register_A; /* store acc to RAM */
      /* opLDAimm_A_AA (04) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0x0400;
      /* opCMPdir_A_AA (b7) */
      cmp_old = acc_a0 = register_A; /* backup old acc */
      flag_C = (((cmp_new = ram[register_I = 0x07]) ^ 0xFFF) + 1 + register_A);
      /* opJNC_A_AA (5d) */
      if (!(flag_C & CARRYBIT)) {
        register_PC = 0x1454;
        break;
      };
      /* opLDAdir_A_AA (a8) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = 0x08]; /* set I register */

      /* opSUBdir_A_AA (76) */
      cmp_old = acc_a0 = register_A;
      register_A = (flag_C = (register_A + ((cmp_new = ram[register_I = 0x06]) ^ 0xFFF) + 1)) & 0xFFF; /* set regI addr */
      /* opSTAdir_A_A (d8) */
      ram[register_I = 0x08] = register_A; /* store acc to RAM */
      /* opLDAdir_A_AA (a7) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = 0x07]; /* set I register */

      /* opSUBdir_A_AA (75) */
      cmp_old = acc_a0 = register_A;
      register_A = (flag_C = (register_A + ((cmp_new = ram[register_I = 0x05]) ^ 0xFFF) + 1)) & 0xFFF; /* set regI addr */
      /* opSTAdir_A_A (d7) */
      ram[register_I = 0x07] = register_A; /* store acc to RAM */
      /* opLDAimm_A_AA (00) */
      cmp_old = flag_C = acc_a0 = register_A;
      register_A = cmp_new = 0;
      /* opADDimm_A_AA (22) */
      cmp_new = 0x2; cmp_old = acc_a0 = register_A; register_A = flag_C = 0x0002;

      /* opLDPimm_A_A (81) */
      register_P = 0x1; /* set page register */
      /* opCMPdir_A_AA (b9) */
      cmp_old = acc_a0 = register_A; /* backup old acc */
      flag_C = (((cmp_new = ram[register_I = 0x19]) ^ 0xFFF) + 1 + register_A);
      /* opLDJimm_A_A (4e) */
      register_J = 0x041e;
      /* opJLT_A_A (5b) */
      if (cmp_new < cmp_old) {
        register_PC = 0x141e;
        break;
      };
      /* opLDPimm_A_A (80) */
      register_P = 0x0; /* set page register */
      /* opLDAdir_A_AA (a5) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = 0x05]; /* set I register */

      /* opLDPimm_A_A (82) */
      register_P = 0x2; /* set page register */
      /* opSUBdir_A_AA (76) */
      cmp_old = acc_a0 = register_A;
      register_A = (flag_C = (register_A + ((cmp_new = ram[register_I = 0x26]) ^ 0xFFF) + 1)) & 0xFFF; /* set regI addr */
      /* opLSLe_A_AA (ec) */
      cmp_new = 0x0CEC;
      cmp_old = acc_a0 = register_A;
      flag_C = (0x0CEC + register_A);
      register_A = (register_A << 1) & 0x0FFF;
      /* opLSLe_A_AA (ec) */
      cmp_new = 0x0CEC;
      cmp_old = acc_a0 = register_A;
      flag_C = (0x0CEC + register_A);
      register_A = (register_A << 1) & 0x0FFF;
      /* opLDPimm_A_A (80) */
      register_P = 0x0; /* set page register */
      /* opSTAdir_A_A (dc) */
      ram[register_I = 0x0c] = register_A; /* store acc to RAM */
      /* opNOP_A_B (57) */
      /* opLDAimm_B_AA (00) */
      flag_C = acc_a0 = register_A;
      cmp_old = register_B; /* step back cmp flag */
      register_B = cmp_new = 0;
      /* opLDPimm_A_A (81) */
      register_P = 0x1; /* set page register */
      /* opLDAdir_A_AA (a9) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = 0x19]; /* set I register */

      /* opLDPimm_A_A (80) */
      register_P = 0x0; /* set page register */
      /* opCMPdir_A_AA (bc) */
      cmp_old = acc_a0 = register_A; /* backup old acc */
      flag_C = (((cmp_new = ram[register_I = 0x0c]) ^ 0xFFF) + 1 + register_A);
      /* opMULirg_A_AA (e3) */
      MUL();
      /* opMULirg_A_AA (e3) */
      MUL();
      /* opMULirg_A_AA (e3) */
      MUL();
      /* opMULirg_A_AA (e3) */
      MUL();
      /* opMULirg_A_AA (e3) */
      MUL();
      /* opLDPimm_A_A (82) */
      register_P = 0x2; /* set page register */
      /* opNOP_A_B (57) */
      /* opADDdir_B_AA (66) */
      acc_a0 = register_A; /* store old acc value */
      register_B = (flag_C = ((cmp_old = register_B) + (cmp_new = ram[register_I = 0x26]))) & 0xFFF; /* do acc operation */
      /* opLDPimm_A_A (80) */
      register_P = 0x0; /* set page register */
      /* opNOP_A_B (57) */
      /* opSTAdir_B_BB (d5) */
      ram[register_I = (register_P << 4) + 0x5] = register_B; /* set I register and store B to ram */
      /* opLDPimm_A_A (80) */
      /* opLDAdir_A_AA (a6) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = 0x06]; /* set I register */

      /* opLDPimm_A_A (82) */
      register_P = 0x2; /* set page register */
      /* opSUBdir_A_AA (77) */
      cmp_old = acc_a0 = register_A;
      register_A = (flag_C = (register_A + ((cmp_new = ram[register_I = 0x27]) ^ 0xFFF) + 1)) & 0xFFF; /* set regI addr */
      /* opLSLe_A_AA (ec) */
      cmp_new = 0x0CEC;
      cmp_old = acc_a0 = register_A;
      flag_C = (0x0CEC + register_A);
      register_A = (register_A << 1) & 0x0FFF;
      /* opLSLe_A_AA (ec) */
      cmp_new = 0x0CEC;
      cmp_old = acc_a0 = register_A;
      flag_C = (0x0CEC + register_A);
      register_A = (register_A << 1) & 0x0FFF;
      /* opLDPimm_A_A (80) */
      register_P = 0x0; /* set page register */
      /* opSTAdir_A_A (dc) */
      ram[register_I = 0x0c] = register_A; /* store acc to RAM */
      /* opNOP_A_B (57) */
      /* opLDAimm_B_AA (00) */
      flag_C = acc_a0 = register_A;
      cmp_old = register_B; /* step back cmp flag */
      register_B = cmp_new = 0;
      /* opLDPimm_A_A (81) */
      register_P = 0x1; /* set page register */
      /* opLDAdir_A_AA (a9) */
      cmp_old = flag_C = acc_a0 = register_A; /* store old acc */
      register_A = cmp_new = ram[register_I = 0x19]; /* set I register */

      /* opLDPimm_A_A (80) */
      register_P = 0x0; /* set page register */
      /* opCMPdir_A_AA (bc) */
      cmp_old = acc_a0 = register_A; /* backup old acc */
      flag_C = (((cmp_new = ram[register_I = 0x0c]) ^ 0xFFF) + 1 + register_A);
      /* opMULirg_A_AA (e3) */
      MUL();
      /* opMULirg_A_AA (e3) */
      MUL();
      /* opMULirg_A_AA (e3) */
      MUL();
      /* opMULirg_A_AA (e3) */
      MUL();
      /* opMULirg_A_AA (e3) */
      MUL();
      /* opLDPimm_A_A (82) */
      register_P = 0x2; /* set page register */
      /* opNOP_A_B (57) */
      /* opADDdir_B_AA (67) */
      acc_a0 = register_A; /* store old acc value */
      register_B = (flag_C = ((cmp_old = register_B) + (cmp_new = ram[register_I = 0x27]))) & 0xFFF; /* do acc operation */
      /* opLDPimm_A_A (80) */
      register_P = 0x0; /* set page register */
      /* opNOP_A_B (57) */
      /* opSTAdir_B_BB (d6) */
      ram[register_I = (register_P << 4) + 0x6] = register_B; /* set I register and store B to ram */
      register_PC = 0x141e; break;

    default:
      /* Jumping to any illegal address or end of eprom will come here */
      /* we ought to reinitialise or something */
      ;
  }
}

void ERROR (char *fmt, ...)
{
}

void reset_coin_counter (int RCstate)
{
  coinflag = IO_COIN;
  ioSwitches |= IO_COIN;
}

int get_coin_state (void)
{
  return ((coinflag >> 7) & 1);
}

int get_quarters_per_game (void)
{
  return (quarterflag & 1);
}

void set_watchdog (void)
{
}

int get_shield_bit0 (void)
{
  return ((SW_SHIELDS >> 5) & 1);
}

int get_shield_bit1 (void)
{
  return ((SW_SHIELDS >> 4) & 1);
}

int get_shield_bit2 (void)
{
  return ((SW_SHIELDS >> 1) & 1);
}

int get_switch_bit (int n)
{
  return (((int) ioSwitches >> n) & 1);
}

int get_io_bit (int n)
{
  return (((startflag | shieldsflag | fireflag | mousecode /* | inputs */ ) >>
           n) & 1);
}

int get_io_moveright (void)
{
  return (((startflag | shieldsflag | fireflag | mousecode /* | inputs */ ) >>
           1) & 1);
}

int get_io_moveleft (void)
{
  return (((startflag | shieldsflag | fireflag | mousecode /* | inputs */ ) >>
           2) & 1);
}

int get_io_moveup (void)
{
  return (((startflag | shieldsflag | fireflag | mousecode /* | inputs */ ) >>
           3) & 1);
}

int get_io_movedown (void)
{
  return (((startflag | shieldsflag | fireflag | mousecode /* | inputs */ ) >>
           4) & 1);
}

int get_io_fire (void)
{
  return (((startflag | shieldsflag | fireflag | mousecode /* | inputs */ ) >>
           5) & 1);
}

int get_io_shields(void)
{
  /* | inputs */
  return (((startflag | shieldsflag | fireflag | mousecode) >> 6) & 1);
}

int get_io_startbutton()
{
  /* | inputs */
  return (((startflag | shieldsflag | fireflag | mousecode) >> 7) & 1);
  // try to force the game to start without buttons
  //return 0x80;
}

void put_io_bit (int pos, int how)
{
  if (how == 1)
    /* Set a bit */
    ioInputs = ioInputs | (1 << pos);
  else
    /* Clear a bit */
    ioInputs = ioInputs & (~(1 << pos));
}

void set_sound_data (int bit)
{
  sound_data = bit;
}

void set_sound_addr_A (int bit)
{
  sound_addr_A = bit;
}

void set_sound_addr_B (int bit)
{
  sound_addr_B = bit;
}

void set_sound_addr_C (int bit)
{
  sound_addr_C = bit;
}

/* These don't yet do anything obvious or useful.  Debugging needed... */
void strobe_sound_on()
{
  int sound_addr_tmp = (sound_addr_C << 2) | (sound_addr_B << 1) | sound_addr_A;
  sound_addr = sound_addr_tmp;
}

void strobe_sound_off()
{
  int sound_addr_tmp = (sound_addr_C << 2) | (sound_addr_B << 1) | sound_addr_A;
  sound_addr = sound_addr_tmp;
}

void init_graph (void)
{
}

void end_graph (void)
{
}

void save_config (void)
{
}

int load_config()
{
  return true;
}

void cineReset (void)
{
}

/* It's interesting to run "gprof" on this code.  It spends something
   like 20% of its time in MUL.  Really *must* tweak the code generator
   to use the C multiply operation. */

void MUL (void)
{
  /* opMULirg_A_AA (e3) */
  cmp_new = ram[register_I];
  register_B <<= 4;   /* get sign bit 15 */
  register_B |= (register_A >> 8);  /* bring in A high nibble */
  register_A = ((register_A & 0xFF) << 8) | (0xe3); /* pick up opcode */
  if (register_A & 0x100) /* 1bit shifted out? */
  {
    acc_a0 = register_A = (register_A >> 8) | ((register_B & 0xFF) << 8);
    register_A >>= 1;
    register_A &= 0xFFF;
    register_B =
      ((unsigned short int) (((signed short int) register_B) >>
                             (signed short int) 4)) /* SAR */ ;
    cmp_old = register_B & 0x0F;
    register_B =
      ((unsigned short int) (((signed short int) register_B) >>
                             (signed short int) 1)) /* SAR */ ;
    register_B &= 0xFFF;
    flag_C = (register_B += cmp_new);
    register_B &= 0xFFF;
  }
  else
  {
    register_A =
      (register_A >> 8) | /* Bhigh | Alow */ ((register_B & 0xFF) << 8);
    cmp_old = acc_a0 = register_A & 0xFFF;
    flag_C = (cmp_old + cmp_new);
    register_A >>= 1;
    register_A &= 0xFFF;
    register_B =
      ((unsigned short int) (((signed short int) register_B) >>
                             (signed short int) 5)) /* SAR */ ;
    register_B &= 0xFFF;
  }
}

/* main interpreter body */

void cineExecuteFrame()
{
  bNewFrame = 0;
  for (;;)
  {
    switch ((register_PC >> 10))
    {
      case 0:
        cineExecute0000 ();
        break;
      case 1:
        cineExecute0400 ();
        break;
      case 2:
        cineExecute0800 ();
        break;
      case 3:
        cineExecute0c00 ();
        break;
      case 4:
        cineExecute1000 ();
        break;
      case 5:
        cineExecute1400 ();
        break;
      case 6:
        cineExecute1800 ();
        break;
      case 7:
        cineExecute1c00 ();
        break;
    }

    if (bNewFrame == 1)
      return;
  }
}
