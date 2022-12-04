#ifndef DAC_H
#define DAC_H

#define MAX_DAC 4

struct DACinterface
{
	int num;	/* total number of DACs */
	int volume[MAX_DAC];
};

int DAC_sh_start(struct DACinterface *interface);
void DAC_sh_stop(void);
void DAC_sh_update(void);
void DAC_data_w(int num,int data);
void DAC_signed_data_w(int num,int data);
void DAC_set_volume(int num,int volume,int gain);
#endif
