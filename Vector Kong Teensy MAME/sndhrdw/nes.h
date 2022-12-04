#ifndef NESPSG_H
#define NESPSG_H


#define MAX_NESPSG 2


/*
** Initialize NES PSG emulator(s).
**
** 'num'      is the number of virtual PSG's to allocate
** 'clock'    is master clock rate (Hz)
** 'rate'     is sampling rate and 'bufsiz' is the size of the
** buffer that should be updated at each interval
*/
int NESInit(int num, int clk, int rate, int bitsize, int bufsiz, void **buffer );

/*
** shutdown the NESPSG emulators .. make sure that no sound system stuff
** is touching our audio buffers ...
*/
void NESShutdown(void);

/*
** reset all chip registers for NESPSG number 'num'
*/
void NESResetChip(int num);

/*
** called to update all chips; should be called about 50 - 70 times per
** second ... (depends on sample rate and buffer size)
*/
void NESUpdate(void);

void NESUpdateOne(int chip , int endp );

/*
** write 'v' to register 'r' on NESPSG chip number 'n'
*/
void NESWriteReg(int n, int r, int v);

/*
** read register 'r' on NESPSG chip number 'n'
*/
unsigned char NESReadReg(int n, int r);


/*
** set clockrate for one
*/
void NESSetClock(int n,int clk,int rate);

/*
** set output gain
**
** The gain is expressed in 0.2dB increments, e.g. a gain of 10 is an increase
** of 2dB. Note that the gain only affects sounds not playing at full volume,
** since the ones at full volume are already played at the maximum intensity
** allowed by the sound card.
** 0x00 is the default.
** 0xff is the maximum allowed value.
*/
void NESSetGain(int n,int gain);

/*
** You have to provide this function
*/
unsigned char NESPortHandler(int num,int port, int iswrite, unsigned char val);

#endif
