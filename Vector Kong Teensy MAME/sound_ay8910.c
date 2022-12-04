/***************************************************************************

  ay8910.c


  Emulation of the AY-3-8910 / YM2149 sound chip.

  Based on various code snippets by Ville Hallik, Michael Cuddy,
  Tatsuyuki Satoh, Fabrice Frances, Nicola Salmoria.

***************************************************************************/

#include "driver.h"
#include "sndhrdw/ay8910.h"


#ifdef SIGNED_SAMPLES
	#define MAX_OUTPUT 0x7fff
#else
	#define MAX_OUTPUT 0xffff
#endif

#define STEP 0x8000


struct AY8910
{
	int Channel;
	int SampleRate;
	int (*PortAread)(int offset);
	int (*PortBread)(int offset);
	void (*PortAwrite)(int offset,int data);
	void (*PortBwrite)(int offset,int data);
	int register_latch;
	unsigned char Regs[16];
	unsigned int UpdateStep;
	int PeriodA,PeriodB,PeriodC,PeriodN,PeriodE;
	int CountA,CountB,CountC,CountN,CountE;
	unsigned int VolA,VolB,VolC,VolE;
	unsigned char EnvelopeA,EnvelopeB,EnvelopeC;
	unsigned char OutputA,OutputB,OutputC,OutputN;
	signed char CountEnv;
	unsigned char Hold,Alternate,Attack,Holding;
	int RNG;
	unsigned int VolTable[32];
};

/* register id's */
#define AY_AFINE	(0)
#define AY_ACOARSE	(1)
#define AY_BFINE	(2)
#define AY_BCOARSE	(3)
#define AY_CFINE	(4)
#define AY_CCOARSE	(5)
#define AY_NOISEPER	(6)
#define AY_ENABLE	(7)
#define AY_AVOL		(8)
#define AY_BVOL		(9)
#define AY_CVOL		(10)
#define AY_EFINE	(11)
#define AY_ECOARSE	(12)
#define AY_ESHAPE	(13)

#define AY_PORTA	(14)
#define AY_PORTB	(15)


static struct AY8910 AYPSG[MAX_8910];		/* array of PSG's */



void _AYWriteReg(int n, int r, int v)
{
	struct AY8910 *PSG = &AYPSG[n];
	int old;


	PSG->Regs[r] = v;

	/* A note about the period of tones, noise and envelope: for speed reasons,*/
	/* we count down from the period to 0, but careful studies of the chip     */
	/* output prove that it instead counts up from 0 until the counter becomes */
	/* greater or equal to the period. This is an important difference when the*/
	/* program is rapidly changing the period to modulate the sound.           */
	/* To compensate for the difference, when the period is changed we adjust  */
	/* our internal counter.                                                   */
	/* Also, note that period = 0 is the same as period = 1. This is mentioned */
	/* in the YM2203 data sheets. However, this does NOT apply to the Envelope */
	/* period. In that case, period = 0 is half as period = 1. */
	switch( r )
	{
	case AY_AFINE:
	case AY_ACOARSE:
		PSG->Regs[AY_ACOARSE] &= 0x0f;
		old = PSG->PeriodA;
		PSG->PeriodA = (PSG->Regs[AY_AFINE] + 256 * PSG->Regs[AY_ACOARSE]) * PSG->UpdateStep;
		if (PSG->PeriodA == 0) PSG->PeriodA = PSG->UpdateStep;
		PSG->CountA += PSG->PeriodA - old;
		if (PSG->CountA <= 0) PSG->CountA = 1;
		break;
	case AY_BFINE:
	case AY_BCOARSE:
		PSG->Regs[AY_BCOARSE] &= 0x0f;
		old = PSG->PeriodB;
		PSG->PeriodB = (PSG->Regs[AY_BFINE] + 256 * PSG->Regs[AY_BCOARSE]) * PSG->UpdateStep;
		if (PSG->PeriodB == 0) PSG->PeriodB = PSG->UpdateStep;
		PSG->CountB += PSG->PeriodB - old;
		if (PSG->CountB <= 0) PSG->CountB = 1;
		break;
	case AY_CFINE:
	case AY_CCOARSE:
		PSG->Regs[AY_CCOARSE] &= 0x0f;
		old = PSG->PeriodC;
		PSG->PeriodC = (PSG->Regs[AY_CFINE] + 256 * PSG->Regs[AY_CCOARSE]) * PSG->UpdateStep;
		if (PSG->PeriodC == 0) PSG->PeriodC = PSG->UpdateStep;
		PSG->CountC += PSG->PeriodC - old;
		if (PSG->CountC <= 0) PSG->CountC = 1;
		break;
	case AY_NOISEPER:
		PSG->Regs[AY_NOISEPER] &= 0x1f;
		old = PSG->PeriodN;
		PSG->PeriodN = PSG->Regs[AY_NOISEPER] * PSG->UpdateStep;
		if (PSG->PeriodN == 0) PSG->PeriodN = PSG->UpdateStep;
		PSG->CountN += PSG->PeriodN - old;
		if (PSG->CountN <= 0) PSG->CountN = 1;
		break;
	case AY_AVOL:
		PSG->Regs[AY_AVOL] &= 0x1f;
		PSG->EnvelopeA = PSG->Regs[AY_AVOL] & 0x10;
		PSG->VolA = PSG->EnvelopeA ? PSG->VolE : PSG->VolTable[PSG->Regs[AY_AVOL] ? PSG->Regs[AY_AVOL]*2+1 : 0];
		break;
	case AY_BVOL:
		PSG->Regs[AY_BVOL] &= 0x1f;
		PSG->EnvelopeB = PSG->Regs[AY_BVOL] & 0x10;
		PSG->VolB = PSG->EnvelopeB ? PSG->VolE : PSG->VolTable[PSG->Regs[AY_BVOL] ? PSG->Regs[AY_BVOL]*2+1 : 0];
		break;
	case AY_CVOL:
		PSG->Regs[AY_CVOL] &= 0x1f;
		PSG->EnvelopeC = PSG->Regs[AY_CVOL] & 0x10;
		PSG->VolC = PSG->EnvelopeC ? PSG->VolE : PSG->VolTable[PSG->Regs[AY_CVOL] ? PSG->Regs[AY_CVOL]*2+1 : 0];
		break;
	case AY_EFINE:
	case AY_ECOARSE:
		old = PSG->PeriodE;
		PSG->PeriodE = ((PSG->Regs[AY_EFINE] + 256 * PSG->Regs[AY_ECOARSE])) * PSG->UpdateStep;
		if (PSG->PeriodE == 0) PSG->PeriodE = PSG->UpdateStep / 2;
		PSG->CountE += PSG->PeriodE - old;
		if (PSG->CountE <= 0) PSG->CountE = 1;
		break;
	case AY_ESHAPE:
		/* envelope shapes:
		C AtAlH
		0 0 x x  \___

		0 1 x x  /___

		1 0 0 0  \\\\

		1 0 0 1  \___

		1 0 1 0  \/\/
		          ___
		1 0 1 1  \

		1 1 0 0  ////
		          ___
		1 1 0 1  /

		1 1 1 0  /\/\

		1 1 1 1  /___

		The envelope counter on the AY-3-8910 has 16 steps. On the YM2149 it
		has twice the steps, happening twice as fast. Since the end result is
		just a smoother curve, we always use the YM2149 behaviour.
		*/
		PSG->Regs[AY_ESHAPE] &= 0x0f;
		PSG->Attack = (PSG->Regs[AY_ESHAPE] & 0x04) ? 0x1f : 0x00;
		if ((PSG->Regs[AY_ESHAPE] & 0x08) == 0)
		{
			/* if Continue = 0, map the shape to the equivalent one which has Continue = 1 */
			PSG->Hold = 1;
			PSG->Alternate = PSG->Attack;
		}
		else
		{
			PSG->Hold = PSG->Regs[AY_ESHAPE] & 0x01;
			PSG->Alternate = PSG->Regs[AY_ESHAPE] & 0x02;
		}
		PSG->CountE = PSG->PeriodE;
		PSG->CountEnv = 0x1f;
		PSG->Holding = 0;
		PSG->VolE = PSG->VolTable[PSG->CountEnv ^ PSG->Attack];
		if (PSG->EnvelopeA) PSG->VolA = PSG->VolE;
		if (PSG->EnvelopeB) PSG->VolB = PSG->VolE;
		if (PSG->EnvelopeC) PSG->VolC = PSG->VolE;
		break;
	case AY_PORTA:
		if ((PSG->Regs[AY_ENABLE] & 0x40) == 0)
if (errorlog) fprintf(errorlog,"warning: write to 8910 #%d Port A set as input\n",n);
if (PSG->PortAwrite) (*PSG->PortAwrite)(0,v);
else if (errorlog) fprintf(errorlog,"PC %04x: warning - write %02x to 8910 #%d Port A\n",cpu_getpc(),v,n);
		break;
	case AY_PORTB:
		if ((PSG->Regs[AY_ENABLE] & 0x80) == 0)
if (errorlog) fprintf(errorlog,"warning: write to 8910 #%d Port B set as input\n",n);
if (PSG->PortBwrite) (*PSG->PortBwrite)(0,v);
else if (errorlog) fprintf(errorlog,"PC %04x: warning - write %02x to 8910 #%d Port B\n",cpu_getpc(),v,n);
		break;
	}
}


/* write a register on AY8910 chip number 'n' */
void AYWriteReg(int chip, int r, int v)
{
	struct AY8910 *PSG = &AYPSG[chip];


	if (r > 15) return;
	if (r < 14)
	{
		if (r == AY_ESHAPE || PSG->Regs[r] != v)
		{
			/* update the output buffer before changing the register */
			stream_update(PSG->Channel,0);
		}
	}

	_AYWriteReg(chip,r,v);
}



unsigned char AYReadReg(int n, int r)
{
	struct AY8910 *PSG = &AYPSG[n];


	if (r > 15) return 0;

	switch (r)
	{
	case AY_PORTA:
		if ((PSG->Regs[AY_ENABLE] & 0x40) != 0)
if (errorlog) fprintf(errorlog,"warning: read from 8910 #%d Port A set as output\n",n);
if (PSG->PortAread) PSG->Regs[AY_PORTA] = (*PSG->PortAread)(0);
else if (errorlog) fprintf(errorlog,"PC %04x: warning - read 8910 #%d Port A\n",cpu_getpc(),n);
		break;
	case AY_PORTB:
		if ((PSG->Regs[AY_ENABLE] & 0x80) != 0)
if (errorlog) fprintf(errorlog,"warning: read from 8910 #%d Port B set as output\n",n);
if (PSG->PortBread) PSG->Regs[AY_PORTB] = (*PSG->PortBread)(0);
else if (errorlog) fprintf(errorlog,"PC %04x: warning - read 8910 #%d Port B\n",cpu_getpc(),n);
		break;
	}
	return PSG->Regs[r];
}


void AY8910Write(int chip,int a,int data)
{
	struct AY8910 *PSG = &AYPSG[chip];

	if (a & 1)
	{	/* Data port */
		AYWriteReg(chip,PSG->register_latch,data);
	}
	else
	{	/* Register port */
		PSG->register_latch = data & 0x0f;
	}
}

int AY8910Read(int chip)
{
	struct AY8910 *PSG = &AYPSG[chip];

	return AYReadReg(chip,PSG->register_latch);
}


/* AY8910 interface */
int AY8910_read_port_0_r(int offset) { return AY8910Read(0); }
int AY8910_read_port_1_r(int offset) { return AY8910Read(1); }
int AY8910_read_port_2_r(int offset) { return AY8910Read(2); }
int AY8910_read_port_3_r(int offset) { return AY8910Read(3); }
int AY8910_read_port_4_r(int offset) { return AY8910Read(4); }

void AY8910_control_port_0_w(int offset,int data) { AY8910Write(0,0,data); }
void AY8910_control_port_1_w(int offset,int data) { AY8910Write(1,0,data); }
void AY8910_control_port_2_w(int offset,int data) { AY8910Write(2,0,data); }
void AY8910_control_port_3_w(int offset,int data) { AY8910Write(3,0,data); }
void AY8910_control_port_4_w(int offset,int data) { AY8910Write(4,0,data); }

void AY8910_write_port_0_w(int offset,int data) { AY8910Write(0,1,data); }
void AY8910_write_port_1_w(int offset,int data) { AY8910Write(1,1,data); }
void AY8910_write_port_2_w(int offset,int data) { AY8910Write(2,1,data); }
void AY8910_write_port_3_w(int offset,int data) { AY8910Write(3,1,data); }
void AY8910_write_port_4_w(int offset,int data) { AY8910Write(4,1,data); }



static void AY8910Update_8(int chip,void **buffer,int length)
{
#define DATATYPE unsigned char
#define DATACONV(A) ((A) / (STEP * 256))
#include "sndhrdw/ay8910u.h"
#undef DATATYPE
#undef DATACONV
}

static void AY8910Update_16(int chip,void **buffer,int length)
{
#define DATATYPE unsigned short
#define DATACONV(A) ((A) / STEP)
#include "sndhrdw/ay8910u.h"
#undef DATATYPE
#undef DATACONV
}


void AY8910_set_clock(int chip,int clock)
{
	struct AY8910 *PSG = &AYPSG[chip];

	/* the step clock for the tone and noise generators is the chip clock    */
	/* divided by 8; for the envelope generator of the AY-3-8910, it is half */
	/* that much (clock/16), but the envelope of the YM2149 goes twice as    */
	/* fast, therefore again clock/8.                                        */
	/* Here we calculate the number of steps which happen during one sample  */
	/* at the given sample rate. No. of events = sample rate / (clock/8).    */
	/* STEP is a multiplier used to turn the fraction into a fixed point     */
	/* number.                                                               */
	PSG->UpdateStep = ((double)STEP * PSG->SampleRate * 8) / clock;
}



/***************************************************************************

  set output volume and gain

  The gain is expressed in 0.2dB increments, e.g. a gain of 10 is an increase
  of 2dB. Note that the gain a�only affects sounds not playing at full volume,
  since the ones at full volume are already played at the maximum intensity
  allowed by the sound card.
  0x00 is the default.
  0xff is the maximum allowed value.

***************************************************************************/
void AY8910_set_volume(int chip,int volume,int gain)
{
	struct AY8910 *PSG = &AYPSG[chip];
	int i;
	double out;


	stream_set_volume(PSG->Channel,volume);
	stream_set_volume(PSG->Channel+1,volume);
	stream_set_volume(PSG->Channel+2,volume);

	gain &= 0xff;

	/* increase max output basing on gain (0.2 dB per step) */
	out = MAX_OUTPUT;
	while (gain-- > 0)
		out *= 1.023292992;	/* = (10 ^ (0.2/20)) */

	/* calculate the volume->voltage conversion table */
	/* The AY-3-8910 has 16 levels, in a logarithmic scale (3dB per step) */
	/* The YM2149 still has 16 levels for the tone generators, but 32 for */
	/* the envelope generator (1.5dB per step). */
	for (i = 31;i > 0;i--)
	{
		/* limit volume to avoid clipping */
		if (out > MAX_OUTPUT) PSG->VolTable[i] = MAX_OUTPUT;
		else PSG->VolTable[i] = out;

		out /= 1.188502227;	/* = 10 ^ (1.5/20) = 1.5dB */
	}
	PSG->VolTable[0] = 0;
}



void AY8910_reset(int chip)
{
	int i;
	struct AY8910 *PSG = &AYPSG[chip];


	PSG->register_latch = 0;
	PSG->RNG = 1;
	PSG->OutputA = 0;
	PSG->OutputB = 0;
	PSG->OutputC = 0;
	PSG->OutputN = 0xff;
	for (i = 0;i < AY_PORTA;i++)
		_AYWriteReg(chip,i,0);	/* AYWriteReg() uses the timer system; we cannot */
								/* call it at this time because the timer system */
								/* has not been initialized. */
}

static int AY8910_init(int chip,const char *chipname,int clock,int sample_rate,int sample_bits,
		int (*portAread)(int offset),int (*portBread)(int offset),
		void (*portAwrite)(int offset,int data),void (*portBwrite)(int offset,int data))
{
	int i;
	struct AY8910 *PSG = &AYPSG[chip];
	char buf[3][40];
	const char *name[3];


	memset(PSG,0,sizeof(struct AY8910));
	PSG->SampleRate = sample_rate;
	PSG->PortAread = portAread;
	PSG->PortBread = portBread;
	PSG->PortAwrite = portAwrite;
	PSG->PortBwrite = portBwrite;
	for (i = 0;i < 3;i++)
	{
		name[i] = buf[i];
		sprintf(buf[i],"%s #%d Ch %c",chipname,chip,'A'+i);
	}
	PSG->Channel = stream_init_multi(3,
			name,sample_rate,sample_bits,
			chip,(sample_bits == 16) ? AY8910Update_16 : AY8910Update_8);

	if (PSG->Channel == -1)
		return 1;

	AY8910_set_clock(chip,clock);
	AY8910_set_volume(chip,255,0);
	AY8910_reset(chip);

	return 0;
}



int AY8910_sh_start(struct AY8910interface *interface,const char *chipname)
{
	int chip;


	for (chip = 0;chip < interface->num;chip++)
	{
		if (AY8910_init(chip,chipname,interface->baseclock,Machine->sample_rate,Machine->sample_bits,
				interface->portAread[chip],interface->portBread[chip],
				interface->portAwrite[chip],interface->portBwrite[chip]) != 0)
			return 1;
		AY8910_set_volume(chip,interface->volume[chip] & 0xff,(interface->volume[chip] >> 8) & 0xff);
	}
	return 0;
}

void AY8910_sh_stop(void)
{
}

void AY8910_sh_update(void)
{
}
