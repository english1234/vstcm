#include "iopins.h"
#include "emuapi.h"
#include "keyboard_osd.h"

extern "C" {
#include "mameport.h"
}

#ifdef HAS_SND

#include <Audio.h>
#include "AudioPlaySystem.h"
#include "sn76496.h"
#include "fm.h"

AudioPlaySystem mymixer;
//#if defined(__IMXRT1052__) || defined(__IMXRT1062__)
//AudioOutputMQS mqs;
//AudioConnection patchCord9(mymixer, 0, mqs, 1);
/*#else
AudioOutputAnalog dac1;
AudioConnection patchCord1(mymixer, dac1);
#endif */
typedef struct
{
  int SampleRate;
  unsigned int UpdateStep;
  int VolTable[16];
  int Register[8];
  int LastRegister;
  int Volume[4];
  unsigned int RNG;
  int NoiseFB;
  int Period[4];
  int Count[4];
  int Output[4];
} SN76496;


//extern struct SN76496 sn[MAX_76496];
extern SN76496 sn[MAX_76496];
#define MAX_OUTPUT 0x7fff
#define STEP 0x10000
#define FB_WNOISE 0x12000
#define FB_PNOISE 0x08000
#define NG_PRESET 0x0F35


void SN76496_set_gain(int chip, int gain) {
  //struct SN76496 *R = &sn[chip];
  SN76496 *R = &sn[chip];
  int i;
  double out;


  gain &= 0xff;

  /* increase max output basing on gain (0.2 dB per step) */
  out = MAX_OUTPUT / 3;
  while (gain-- > 0)
    out *= 1.023292992; /* = (10 ^ (0.2/20)) */

  /* build volume table (2dB per step) */
  for (i = 0; i < 15; i++) {
    /* limit volume to avoid clipping */
    if (out > MAX_OUTPUT / 3) R->VolTable[i] = MAX_OUTPUT / 3;
    else R->VolTable[i] = out;

    out /= 1.258925412; /* = 10 ^ (2/20) = 2dB */
  }
  R->VolTable[15] = 0;
}

void SN76496_set_clock(int chip, int clock) {
  //	struct SN76496 *R = &sn[chip];
  SN76496 *R = &sn[chip];

  /* the base clock for the tone generators is the chip clock divided by 16; */
  /* for the noise generator, it is clock / 256. */
  /* Here we calculate the number of steps which happen during one sample */
  /* at the given sample rate. No. of events = sample rate / (clock/16). */
  /* STEP is a multiplier used to turn the fraction into a fixed point */
  /* number. */
  R->UpdateStep = ((double)STEP * R->SampleRate * 16) / clock;
}

int SN76496_init(int chip, int clock, int volume, int sample_rate) {
  int i;
  //	struct SN76496 *R = &sn[chip];
  SN76496 *R = &sn[chip];

  R->SampleRate = sample_rate;
  SN76496_set_clock(chip, clock);

  for (i = 0; i < 4; i++) R->Volume[i] = 0;

  R->LastRegister = 0;
  for (i = 0; i < 8; i += 2) {
    R->Register[i] = 0;
    R->Register[i + 1] = 0x0f; /* volume = 0 */
  }

  for (i = 0; i < 4; i++) {
    R->Output[i] = 0;
    R->Period[i] = R->Count[i] = R->UpdateStep;
  }
  R->RNG = NG_PRESET;
  R->Output[3] = R->RNG & 1;

  return 0;
}

int SN76496_sh_start(int clock, int volume, int rate) {
  SN76496_init(0, clock, volume & 0xff, rate);
  SN76496_set_gain(0, (volume >> 8) & 0xff);
  return 0;
}

void emu_sndInit() {
  Serial.println("sound init");
  //AudioMemory(16);
  mymixer.begin_audio(512, mymixer.snd_Mixer);
  mymixer.start();
}

void emu_sndPlaySound(int chan, int volume, int freq) {
  if (chan < 6) {
    mymixer.sound(chan, freq, volume);
  }

  Serial.print(chan);
  Serial.print(":");
  Serial.print(volume);
  Serial.print(":");
  Serial.println(freq);
}

void emu_sndPlayBuzz(int size, int val) {
  mymixer.buzz(size, val);
  //Serial.print((val==1)?1:0);
  //Serial.print(":");
  //Serial.println(size);
}

// Added for VSTCM
void emu_sndPlayStop() {
  mymixer.stop();
 
}

#endif

#include "settings.h"  // VSTCM specific

extern "C" void init_gamma();
extern "C" void draw_girder_stage();
extern "C" void UpdateDK();
extern "C" void update_frame_count();

bool vgaMode = false;

//static unsigned char palette8[PALETTE_SIZE];
//static unsigned short palette16[PALETTE_SIZE];
static IntervalTimer myTimer;
volatile boolean vbl = true;
static int skip = 0;
static elapsedMicros tius;

static void vblCount() {
  if (vbl) {
    vbl = false;
  } else {
    vbl = true;
  }
}

void emu_SetPaletteEntry(unsigned char r, unsigned char g, unsigned char b, int index) {
  // if (index < PALETTE_SIZE) {
  //Serial.println("%d: %d %d %d\n", index, r,g,b);
  //   palette8[index]  = RGBVAL8(r,g,b);
  //  palette16[index] = RGBVAL16(r,g,b);
  //  }
}

void emu_DrawVsync(void) {
  /*  volatile boolean vb = vbl;
  skip += 1;
  skip &= VID_FRAME_SKIP;
  if (!vgaMode) {
    while (vbl == vb) {};
  }
#ifdef HAS_VGA
  else {
    while (vbl == vb) {};
  }
#endif */
}

void emu_DrawLine(unsigned char *VBuf, int width, int height, int line) {
  /*  if (!vgaMode) {
    // tft.writeLine(width,1,line, VBuf, palette16);
  }
#ifdef HAS_VGA
  else {
    int fb_width = UVGA_XRES, fb_height = UVGA_YRES;
    fb_width += UVGA_XRES_EXTRA;
    int offx = (fb_width - width) / 2;
    int offy = (fb_height - height) / 2 + line;
    uint8_t *dst = VGA_frame_buffer + (fb_width * offy) + offx;
    for (int i = 0; i < width; i++) {
      uint8_t pixel = palette8[*VBuf++];
      *dst++ = pixel;
    }
  }
#endif */
}

void emu_DrawLine16(unsigned short *VBuf, int width, int height, int line) {
  /* if (!vgaMode) {
    if (skip == 0) {
      // tft.writeLine(width,height,line, VBuf);
    }
  }
#ifdef HAS_VGA
#endif */
}

void emu_DrawScreen(unsigned char *VBuf, int width, int height, int stride) {
  /* if (!vgaMode) {
    if (skip == 0) {
      //  tft.writeScreen(width,height-TFT_VBUFFER_YCROP,stride, VBuf+(TFT_VBUFFER_YCROP/2)*stride, palette16);
    }
  }
#ifdef HAS_VGA
  else {
    int fb_width = UVGA_XRES, fb_height = UVGA_YRES;
    //uvga.get_frame_buffer_size(&fb_width, &fb_height);
    fb_width += UVGA_XRES_EXTRA;
    int offx = (fb_width - width) / 2;
    int offy = (fb_height - height) / 2;
    uint8_t *buf = VGA_frame_buffer + (fb_width * offy) + offx;
    for (int y = 0; y < height; y++) {
      uint8_t *dest = buf;
      for (int x = 0; x < width; x++) {
        uint8_t pixel = palette8[*VBuf++];
        *dest++ = pixel;
      }
      buf += fb_width;
      VBuf += (stride - width);
    }
  }
#endif */
}

int emu_FrameSkip(void) {
  return skip;
}

extern "C" void read_vstcm_config();
extern "C" void SPI_init();
extern "C" void draw_moveto(int, int);
extern "C" void Update_DK();
extern "C" void Setup_DK();

extern float line_draw_speed;
extern params_t v_setting[NB_SETTINGS];
typedef struct
{
  int sample_rate;    /* Sample rate (8000-44100) */
  int enabled;        /* 1= sound emulation is enabled */
  int buffer_size;    /* Size of sound buffer (in bytes) */
  int16_t *buffer[2]; /* Signed 16-bit stereo sound data */
  struct {
    int curStage;
    int lastStage;
    int16_t *buffer[2];
  } fm;
  struct {
    int curStage;
    int lastStage;
    int16_t *buffer;
  } psg;
} t_snd;

extern t_snd snd;
#define SND_SIZE (snd.buffer_size * sizeof(int16_t))
static int sound_tbl[262];

extern "C" int YM2612Init(int num, int clock, int rate, FM_TIMERHANDLER TimerHandler,FM_IRQHANDLER IRQHandler);


int audio_init(int rate) {
  int i;

  /* 68000 and YM2612 clock */
  float vclk = 53693175.0 / 7;

  /* Z80 and SN76489 clock */
  float zclk = 3579545.0;

  /* Clear the sound data context */
  memset(&snd, 0, sizeof(snd));

  /* Make sure the requested sample rate is valid */
  if (!rate || ((rate < 8000) | (rate > 44100))) {
    return (0);
  }

  /* Calculate the sound buffer size */
  snd.buffer_size = (rate / 60);
  snd.sample_rate = rate;

  /* Allocate sound buffers */
  snd.fm.buffer[0] = emu_Malloc(SND_SIZE);
  snd.fm.buffer[1] = emu_Malloc(SND_SIZE);
  snd.psg.buffer = emu_Malloc(SND_SIZE);

  /* Make sure we could allocate everything */
  if (!snd.fm.buffer[0] || !snd.fm.buffer[1] || !snd.psg.buffer) {
    return (0);
  }

  /* Initialize sound chip emulation */
  SN76496_sh_start(zclk, 100, rate);


  // VSTCM COMMENTED OUT NOT ENOUGH FREE MEMORY
 //  YM2612Init(1, vclk, rate, NULL, NULL);

  /* Set audio enable flag */
  snd.enabled = 1;

  /* Make sound table */
  for (i = 0; i < 262; i++) {
    float p = snd.buffer_size * i;
    p = p / 262;
    sound_tbl[i] = p;
  }

  return (0);
}


void setup() {
  init_gamma();
  read_vstcm_config();  // Read saved settings from Teensy SD card
                        // IR_remote_setup();    // Configure the infra red remote control, if present
                        // buttons_setup();      // Configure buttons on vstcm for input using built in pullup resistors
  SPI_init();           // Set up pins on Teensy as well as SPI registers

  // line_draw_speed = (float)v_setting[5].pval / NORMAL_SHIFT_SCALING;
  line_draw_speed = (float)v_setting[5].pval / NORMAL_SHIFT;

  emu_init();
  myTimer.begin(vblCount, 20000);  //to run every 20ms
  mam_Init();
  char playgame[] = "dkong";
  mam_Start(playgame);
#ifdef HAS_SND
  audio_init(22050);
  emu_sndInit();
#endif
  Setup_DK();
}

extern int chunk_count;

typedef struct
{
  int running;
  int enable;
  int count;
  int base;
  int index;
} t_timer;
/* YM2612 data */
int fm_timera_tab[0x400]; /* Precalculated timer A values */
int fm_timerb_tab[0x100]; /* Precalculated timer B values */
uint8_t fm_reg[2][0x100]; /* Register arrays (2x256) */
uint8_t fm_latch[2];      /* Register latches */
uint8_t fm_status;        /* Read-only status flags */
t_timer timer[2];         /* Timers A and B */

/* Initialize the YM2612 and SN76489 emulation */
void sound_init(void) {
  /* Timers run at half the YM2612 input clock */
  float clock = ((53.693175 / 7) / 2);
  int i;

  /* Make Timer A table */
  for (i = 0; i < 1024; i += 1) {
    /* Formula is "time(us) = 72 * (1024 - A) / clock" */
    fm_timera_tab[i] = ((int)(float)(72 * (1024 - i)) / (clock));
  }

  /* Make Timer B table */
  for (i = 0; i < 256; i += 1) {
    /* Formula is "time(us) = 1152 * (256 - B) / clock" */
    fm_timerb_tab[i] = (int)(float)(1152 * (256 - i)) / clock;
  }
}

void sound_reset(void) {
  if (snd.enabled) {
    YM2612ResetChip(0);
  }
}

void fm_write(int address, int data) {
  int a0 = (address & 1);
  int a1 = (address >> 1) & 1;

  if (a0) {
    /* Register data */
    fm_reg[a1][fm_latch[a1]] = data;

    /* Timer control only in set A */
    if (a1 == 0)
      switch (fm_latch[a1]) {
        case 0x24: /* Timer A (LSB) */
          timer[0].index = (timer[0].index & 0x0003) | (data << 2);
          timer[0].index &= 0x03FF;
          timer[0].base = fm_timera_tab[timer[0].index];
          break;

        case 0x25: /* Timer A (MSB) */
          timer[0].index = (timer[0].index & 0x03FC) | (data & 3);
          timer[0].index &= 0x03FF;
          timer[0].base = fm_timera_tab[timer[0].index];
          break;

        case 0x26: /* Timer B */
          timer[1].index = data;
          timer[1].base = timer[1].count = fm_timerb_tab[timer[1].index];
          break;

        case 0x27: /* Timer Control */

          /* LOAD */
          timer[0].running = (data >> 0) & 1;
          if (timer[0].running) timer[0].count = 0;
          timer[1].running = (data >> 1) & 1;
          if (timer[1].running) timer[1].count = 0;

          /* ENABLE */
          timer[0].enable = (data >> 2) & 1;
          timer[1].enable = (data >> 3) & 1;

          /* RESET */
          if (data & 0x10) fm_status &= ~1;
          if (data & 0x20) fm_status &= ~2;
          break;
      }
  } else {
    /* Register latch */
    fm_latch[a1] = data;
  }

  if (snd.enabled) {
    if (snd.fm.curStage - snd.fm.lastStage > 1) {
      int16_t *tempBuffer[2];
      tempBuffer[0] = snd.fm.buffer[0] + snd.fm.lastStage;
      tempBuffer[1] = snd.fm.buffer[1] + snd.fm.lastStage;
      YM2612UpdateOne(0, (int16_t **)tempBuffer, snd.fm.curStage - snd.fm.lastStage);
      snd.fm.lastStage = snd.fm.curStage;
    }

    YM2612Write(0, address & 3, data);
  }
}


int fm_read(int address) {
  return (fm_status);
}


void fm_update_timers(void) {
  int i;

  /* Process YM2612 timers */
  for (i = 0; i < 2; i += 1) {
    /* Is the timer running? */
    if (timer[i].running) {
      /* Each scanline takes up roughly 64 microseconds */
      timer[i].count += 64;

      /* Check if the counter overflowed */
      if (timer[i].count > timer[i].base) {
        /* Reload counter */
        timer[i].count = 0;

        /* Disable timer */
        timer[i].running = 0;

        /* Set overflow flag (if flag setting is enabled) */
        if (timer[i].enable) {
          fm_status |= (1 << i);
        }
      }
    }
  }
}

void psg_write(int data) {
  if (snd.enabled) {
    if (snd.psg.curStage - snd.psg.lastStage > 1) {
      int16_t *tempBuffer;
      tempBuffer = snd.psg.buffer + snd.psg.lastStage;
      SN76496Update(0, tempBuffer, snd.psg.curStage - snd.psg.lastStage);
      snd.psg.lastStage = snd.psg.curStage;
    }

    SN76496Write(0, data);
  }
}

static uint8_t notedelay = 0;
static int notes[] = {
  440,
  466,
  494,
  523,
  554,
  587,
  622,
  659,
  698,
  740,
  784,
  831,
  880
};
static int note_pos;


void loop(void) {
  mam_Step();

  /* Point to start of sound buffer */
  snd.fm.lastStage = snd.fm.curStage = 0;
  snd.psg.lastStage = snd.psg.curStage = 0;

  uint16_t bClick = emu_DebounceLocalKeys();
  emu_Input(bClick);
  Update_DK();
  update_frame_count();

  fm_update_timers();

  for (int line = 0; line < 262; line += 1) {
    snd.fm.curStage = sound_tbl[line];  // line counts from 0 to 262 during each loop : use frame count instead?
    snd.psg.curStage = sound_tbl[line];
  }


 /*notedelay += 1;
  notedelay &= 0x07;
  int note = notes[note_pos];
  emu_sndPlaySound(1, notedelay << 4, note);
  if (!notedelay) {
    note_pos += 1;
    if (note_pos >= sizeof(notes) / sizeof(int)) {
      note_pos = 0;
    }
  }*/
}
