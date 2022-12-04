#ifndef NESINTF_H
#define NESINTF_H

#include "nes.h"


struct NESinterface
{
	int num;	/* total number of chips in the machine */
	int baseclock;
	int volume[MAX_NESPSG];
};

int NESPSG_0_r(int offset);
int NESPSG_1_r(int offset);
void NESPSG_0_w(int offset,int data);
void NESPSG_1_w(int offset,int data);

int NESPSG_sh_start(struct NESinterface *interface);
void NESPSG_sh_stop(void);
void NESPSG_sh_update(void);

#endif
