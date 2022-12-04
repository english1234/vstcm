#ifndef namco_h
#define namco_h

struct namco_interface
{
	int samplerate;	/* sample rate */
	int voices;		/* number of voices */
	int gain;		/* 16 * gain adjustment */
	int volume;		/* playback volume */
	int region;		/* memory region */
};

int namco_sh_start(struct namco_interface *intf);
void namco_sh_stop(void);
void namco_sh_update(void);

void pengo_sound_enable_w(int offset,int data);
void pengo_sound_w(int offset,int data);

void mappy_sound_enable_w(int offset,int data);
void mappy_sound_w(int offset,int data);

#endif

