#ifndef SAMPLES_H
#define SAMPLES_H

struct Samplesinterface
{
	int channels;	/* number of discrete audio channels needed */
};


/* Start one of the samples loaded from disk. Note: channel must be in the range */
/* 0 .. Samplesinterface->channels-1. It is NOT the discrete channel to pass to */
/* osd_play_sample() */
void sample_start(int channel,int samplenum,int loop);
void sample_adjust(int channel,int freq,int volume);
void sample_stop(int channel);
int sample_playing(int channel);


int samples_sh_start(struct Samplesinterface *interface);
void samples_sh_stop(void);
void samples_sh_update(void);

#endif
