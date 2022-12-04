#include "namco.h"
#include "8910intf.h"
#include "dac.h"
#include "samples.h"
#include "streams.h"
#include "sn76496.h"
#include "adpcm.h"
#include "nesintf.h"

/*
#include "2203intf.h"
#include "2151intf.h"
#include "2610intf.h"
#include "3812intf.h"
#include "2413intf.h"
#include "pokey.h"
#include "5220intf.h"
#include "vlm5030.h"
#include "astrocde.h"
*/


void soundlatch_w(int offset,int data);
int soundlatch_r(int offset);
void soundlatch_clear_w(int offset,int data);
void soundlatch2_w(int offset,int data);
int soundlatch2_r(int offset);
void soundlatch2_clear_w(int offset,int data);
void soundlatch3_w(int offset,int data);
int soundlatch3_r(int offset);
void soundlatch3_clear_w(int offset,int data);
void soundlatch4_w(int offset,int data);
int soundlatch4_r(int offset);
void soundlatch4_clear_w(int offset,int data);

/* If you're going to use soundlatchX_clear_w, and the cleared value is
   something other than 0x00, use this function from machine_init. Note
   that this one call effects all 4 latches */
void soundlatch_setclearedvalue(int value);

int get_play_channels(int request);
void reset_play_channels(void);

int sound_start(void);
void sound_stop(void);
void sound_update(void);


/* structure for SOUND_CUSTOM sound drivers */
struct CustomSound_interface
{
	int (*sh_start)(void);
	void (*sh_stop)(void);
	void (*sh_update)(void);
};
