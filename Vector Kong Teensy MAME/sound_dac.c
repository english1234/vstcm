#include "driver.h"


#define MAX_OUTPUT 0x7fff

static int channel[MAX_DAC];
static unsigned char latch[MAX_DAC];
static unsigned char VolTable[MAX_DAC][256];
static unsigned char SignedVolTable[MAX_DAC][256];


static void DAC_update(int num, void *buffer, int length) {
  memset(buffer, latch[num], length);
}


void DAC_data_w(int num, int data) {
  /* update the output buffer before changing the registers */
  if (latch[num] != data)
    stream_update(channel[num], 0);

  latch[num] = VolTable[num][data];
}


void DAC_signed_data_w(int num, int data) {
  /* update the output buffer before changing the registers */
  if (latch[num] != data)
    stream_update(channel[num], 0);

  latch[num] = SignedVolTable[num][data];
}


void DAC_set_volume(int num, int volume, int gain) {
  int i;
  int out;


  stream_set_volume(channel[num], volume);

  gain &= 0xff;

  /* build volume table (linear) */
  for (i = 255; i > 0; i--) {
    out = (i * (1.0 + (gain / 16))) * MAX_OUTPUT / 255;
    /* limit volume to avoid clipping */
    if (out > MAX_OUTPUT) VolTable[num][i] = MAX_OUTPUT / 256;
    else VolTable[num][i] = out / 256;
  }
  VolTable[num][0] = 0;

  for (i = 0; i < 128; i++) {
    SignedVolTable[num][0x80 + i] = VolTable[num][2 * i];
    SignedVolTable[num][0x80 - i] = -VolTable[num][2 * i];
  }
  SignedVolTable[num][0] = -VolTable[num][255];
}


int DAC_sh_start(struct DACinterface *interface) {
  int i;

  emu_printf("DAC_sh_start");
  
  for (i = 0; i < interface->num; i++) {
    char name[40];


    sprintf(name, "DAC #%d", i);

    // VSTCM test
    emu_printf(name);

    channel[i] = stream_init(
      name, Machine->sample_rate, 8,
      i, DAC_update);

    if (channel[i] == -1)
      return 1;

    DAC_set_volume(i, interface->volume[i] & 0xff, (interface->volume[i] >> 8) & 0xff);

    latch[i] = 0;
  }
  return 0;
}

void DAC_sh_stop(void) {
}

void DAC_sh_update(void) {
}
