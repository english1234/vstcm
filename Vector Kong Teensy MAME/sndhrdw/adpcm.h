#ifndef ADPCM_H
#define ADPCM_H

#define MAX_ADPCM 8

/* NOTE: Actual sample data is specified in the sound_prom parameter of the game driver, but
   since the MAME code expects this to be an array of char *'s, we do a small kludge here */

struct ADPCMsample
{
	int num;       /* trigger number (-1 to mark the end) */
	int offset;    /* offset in that region */
	int length;    /* length of the sample */
};

#define ADPCM_SAMPLES_START(name) struct ADPCMsample name[] = {
#define ADPCM_SAMPLE(n,o,l) { (n), (o), (l) },
#define ADPCM_SAMPLES_END { 0, 0, 0, } };


/* a generic ADPCM interface, for unknown chips */

struct ADPCMinterface
{
	int num;			       /* total number of ADPCM decoders in the machine */
	int frequency;             /* playback frequency */
	int region;                /* memory region where the samples come from */
	void (*init)(struct ADPCMinterface *, struct ADPCMsample *, int max); /* initialization function */
	int volume[MAX_ADPCM];     /* master volume */
};

int ADPCM_sh_start (struct ADPCMinterface *interface);
void ADPCM_sh_stop (void);
void ADPCM_sh_update (void);

void ADPCM_trigger (int num, int which);
void ADPCM_play (int num, int offset, int length);
void ADPCM_setvol (int num, int vol);
void ADPCM_stop (int num);
int ADPCM_playing (int num);


/* an interface for the OKIM6295 and similar chips */

#define MAX_OKIM6295 2

struct OKIM6295interface
{
	int num;                  /* total number of chips */
	int frequency;            /* playback frequency */
	int region;               /* memory region where the sample ROM lives */
	int volume[MAX_OKIM6295]; /* master volume */
};

int OKIM6295_sh_start (struct OKIM6295interface *intf);
void OKIM6295_sh_stop (void);
void OKIM6295_sh_update (void);

int OKIM6295_status_r (int num);
void OKIM6295_data_w (int num, int data);


/* an interface for the MSM5205 and similar chips */

#define MAX_MSM5205 8

struct MSM5205interface
{
	int num;                  /* total number of chips */
	int frequency;            /* playback frequency */
	void (*interrupt)(int);   /* interrupt function (called when chip is active) */
	int volume[MAX_OKIM6295]; /* master volume */
};

int MSM5205_sh_start (struct MSM5205interface *intf);
void MSM5205_sh_stop (void);
void MSM5205_sh_update (void);

void MSM5205_reset_w (int num, int reset);
void MSM5205_data_w (int num, int data);


#endif
