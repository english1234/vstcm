#ifndef SN76496_H
#define SN76496_H

#define MAX_76496 4

struct SN76496interface
{
	int num;	/* total number of 76496 in the machine */
	int baseclock;
	int volume[MAX_76496];
};

int SN76496_sh_start(struct SN76496interface *interface);
void SN76496_sh_stop(void);
void SN76496_0_w(int offset,int data);
void SN76496_1_w(int offset,int data);
void SN76496_2_w(int offset,int data);
void SN76496_3_w(int offset,int data);
void SN76496_sh_update(void);

void SN76496_set_clock(int chip,int _clock);

#endif
