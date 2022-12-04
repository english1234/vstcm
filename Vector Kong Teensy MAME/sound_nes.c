/***************************************************************************

  nes.c


  Emulation of the sound chip used on the NES and in some Nintendo arcade
  games (Donkey Kong 3, Punch Out).

  Hardware info by Jeremy Chadwick and Hedley Rainne.

  Based on psg.c.

  As of this moment, everything in here is totally wrong and largely
  incomplete, but it is enough to hear some recognizable tunes.

***************************************************************************/

/***************************************************************************

NES sound hardware info by Jeremy Chadwick and Hedley Rainne



  For the Square Wave and Triangle Wave channels, a formulae can be used
to provide accurate playback of the NES's sound:

    P = 111860.78 / (CHx + 1)

  Where "P" is the actual played data, and CHx is the channel played.
The channel formulaes are the following:

    CH1 = $4002 + ($4003 & 7) * 256   (Square Wave #1)
    CH2 = $4006 + ($4007 & 7) * 256   (Square Wave #2)
    CH3 = $400A + ($400B & 7) * 256   (Triangle Wave)

  Where the $400x values are actual values written to that register.

  For the PCM channel, there are two methods of implementation: via DMA,
and via the PCM Volume Port ($4011).

  Samples are sent byte-by-byte into $4011, the result being quite
audible. However, only a few games seem to use this method, while most
use the DMA transfer approach.

� $4000   � RW    � CChessss � Square Wave Control Register #1             �
�         �       �          �                                             �
�         �       �          �   C = Duty Cycle (Positive vs. Negative)    �
�         �       �          �        00 = 87.5%                           �
�         �       �          �        01 = 75.0%                           �
�         �       �          �        10 = 58.0%                           �
�         �       �          �        11 = 25.0%                           �
�         �       �          �   h = Hold Note                             �
�         �       �          �         0 = Don't hold note                 �
�         �       �          �         1 = Hold note                       �
�         �       �          �   e = Envelope Select                       �
�         �       �          �         0 = Envelope Vary                   �
�         �       �          �         1 = Envelope Fixed                  �
�         �       �          �   s = Playback Rate                         �
����������������������������������������������������������������������������
� $4001   � RW    � fsssHrrr � Square Wave Control Register #2             �
�         �       �          �                                             �
�         �       �          �   f = Frequency Fixed/Variable Select       �
�         �       �          �         0 = Fixed  (bits 0-6 disabled)      �
�         �       �          �         1 = Variable (bits 0-6 enabled)     �
�         �       �          �   s = Frequency Change Speed                �
�         �       �          �   H = Low/High Frequency Select             �
�         �       �          �         0 = Low -> High                     �
�         �       �          �         1 = High -> Low                     �
�         �       �          �   r = Frequency Range (0=Min, 7=Max)        �
����������������������������������������������������������������������������
� $4002   � RW    � dddddddd � Square Wave Frequency Value Register #1     �
�         �       �          �                                             �
�         �       �          �   d = Frequency Value Data (lower 8-bits)   �
����������������������������������������������������������������������������
� $4003   � RW    � tttttddd � Square Wave Frequency Value Register #2     �
�         �       �          �                                             �
�         �       �          �   d = Frequency Value Data (upper 3-bits)   �
�         �       �          �   t = Active Time Length                    �
�         �       �          �                                             �
�         �       �          � NOTE: The Frequency Value is a full 11-bits �
�         �       �          �       in size; be aware you will need to    �
�         �       �          �       write the upper 3-bits to $4003.      �
����������������������������������������������������������������������������
� $4004   � RW    � CChessss � Square Wave Control Register #1             �
�         �       �          �                                             �
�         �       �          �   C = Duty Cycle (Positive vs. Negative)    �
�         �       �          �        00 = 87.5%                           �
�         �       �          �        01 = 75.0%                           �
�         �       �          �        10 = 58.0%                           �
�         �       �          �        11 = 25.0%                           �
�         �       �          �   h = Hold Note                             �
�         �       �          �         0 = Don't hold note                 �
�         �       �          �         1 = Hold note                       �
�         �       �          �   e = Envelope Select                       �
�         �       �          �         0 = Envelope Vary                   �
�         �       �          �         1 = Envelope Fixed                  �
�         �       �          �   s = Playback Rate                         �
����������������������������������������������������������������������������
� $4005   � RW    � fsssHrrr � Square Wave Control Register #2             �
�         �       �          �                                             �
�         �       �          �   f = Frequency Fixed/Variable Select       �
�         �       �          �         0 = Fixed  (bits 0-6 disabled)      �
�         �       �          �         1 = Variable (bits 0-6 enabled)     �
�         �       �          �   s = Frequency Change Speed                �
�         �       �          �   H = Low/High Frequency Select             �
�         �       �          �         0 = Low -> High                     �
�         �       �          �         1 = High -> Low                     �
�         �       �          �   r = Frequency Range (0=Min, 7=Max)        �
����������������������������������������������������������������������������
� $4006   � RW    � dddddddd � Square Wave Frequency Value Register #1     �
�         �       �          �                                             �
�         �       �          �   d = Frequency Value Data (lower 8-bits)   �
����������������������������������������������������������������������������
� $4007   � RW    � tttttddd � Square Wave Frequency Value Register #2     �
�         �       �          �                                             �
�         �       �          �   d = Frequency Value Data (upper 3-bits)   �
�         �       �          �   t = Active Time Length                    �
�         �       �          �                                             �
�         �       �          � NOTE: The Frequency Value is a full 11-bits �
�         �       �          �       in size; be aware you will need to    �
�         �       �          �       write the upper 3-bits to $4007.      �
����������������������������������������������������������������������������
� $4008   � RW    � CChessss � Triangle Wave Control Register #1           �
�         �       �          �                                             �
�         �       �          �   C = Duty Cycle (Positive vs. Negative)    �
�         �       �          �        00 = 87.5%                           �
�         �       �          �        01 = 75.0%                           �
�         �       �          �        10 = 58.0%                           �
�         �       �          �        11 = 25.0%                           �
�         �       �          �   h = Hold Note                             �
�         �       �          �         0 = Don't hold note                 �
�         �       �          �         1 = Hold note                       �
�         �       �          �   e = Envelope Select                       �
�         �       �          �         0 = Envelope Vary                   �
�         �       �          �         1 = Envelope Fixed                  �
�         �       �          �   s = Playback Rate                         �
����������������������������������������������������������������������������
� $4009   � RW    � fsssHrrr � Triangle Wave Control Register #2           �
�         �       �          �                                             �
�         �       �          �   f = Frequency Fixed/Variable Select       �
�         �       �          �         0 = Fixed  (bits 0-6 disabled)      �
�         �       �          �         1 = Variable (bits 0-6 enabled)     �
�         �       �          �   s = Frequency Change Speed                �
�         �       �          �   H = Low/High Frequency Select             �
�         �       �          �         0 = Low -> High                     �
�         �       �          �         1 = High -> Low                     �
�         �       �          �   r = Frequency Range (0=Min, 7=Max)        �
����������������������������������������������������������������������������
� $400A   � RW    � dddddddd � Triangle Wave Frequency Value Register #1   �
�         �       �          �                                             �
�         �       �          �   d = Frequency Value Data (lower 8-bits)   �
����������������������������������������������������������������������������
� $400B   � RW    � tttttddd � Triangle Wave Frequency Value Register #2   �
�         �       �          �                                             �
�         �       �          �   d = Frequency Value Data (upper 3-bits)   �
�         �       �          �   t = Active Time Length                    �
�         �       �          �                                             �
�         �       �          � NOTE: The Frequency Value is a full 11-bits �
�         �       �          �       in size; be aware you will need to    �
�         �       �          �       write the upper 3-bits to $400B.      �
����������������������������������������������������������������������������
� $400C   � RW    � CChessss � Noise Control Register #1                   �
�         �       �          �                                             �
�         �       �          �   C = Duty Cycle (Positive vs. Negative)    �
�         �       �          �        00 = 87.5%                           �
�         �       �          �        01 = 75.0%                           �
�         �       �          �        10 = 58.0%                           �
�         �       �          �        11 = 25.0%                           �
�         �       �          �   h = Hold Note                             �
�         �       �          �         0 = Don't hold note                 �
�         �       �          �         1 = Hold note                       �
�         �       �          �   e = Envelope Select                       �
�         �       �          �         0 = Envelope Vary                   �
�         �       �          �         1 = Envelope Fixed                  �
�         �       �          �   s = Playback Rate                         �
����������������������������������������������������������������������������
� $400D   � RW    � fsssHrrr � Noise Control Register #2                   �
�         �       �          �                                             �
�         �       �          �   f = Frequency Fixed/Variable Select       �
�         �       �          �         0 = Fixed  (bits 0-6 disabled)      �
�         �       �          �         1 = Variable (bits 0-6 enabled)     �
�         �       �          �   s = Frequency Change Speed                �
�         �       �          �   H = Low/High Frequency Select             �
�         �       �          �         0 = Low -> High                     �
�         �       �          �         1 = High -> Low                     �
�         �       �          �   r = Frequency Range (0=Min, 7=Max)        �
����������������������������������������������������������������������������
� $400E   � RW    � dddddddd � Frequency Value Register #1                 �
�         �       �          �                                             �
�         �       �          �   d = Frequency Value Data (lower 8-bits)   �
����������������������������������������������������������������������������
� $400F   � RW    � tttttddd � Frequency Value Register #2                 �
�         �       �          �                                             �
�         �       �          �   d = Frequency Value Data (upper 3-bits)   �
�         �       �          �   t = Active Time Length                    �
�         �       �          �                                             �
�         �       �          � NOTE: The Frequency Value is a full 11-bits �
�         �       �          �       in size; be aware you will need to    �
�         �       �          �       write the upper 3-bits to $400F.      �

����������������������������������������������������������������������������
� $4010   � RW    � CChessss � PCM Control Register #1                     �
�         �       �          �                                             �
�         �       �          �   C = Duty Cycle (Positive vs. Negative)    �
�         �       �          �        00 = 87.5%                           �
�         �       �          �        01 = 75.0%                           �
�         �       �          �        10 = 58.0%                           �
�         �       �          �        11 = 25.0%                           �
�         �       �          �   h = Hold Note                             �
�         �       �          �         0 = Don't hold note                 �
�         �       �          �         1 = Hold note                       �
�         �       �          �   e = Envelope Select                       �
�         �       �          �         0 = Envelope Vary                   �
�         �       �          �         1 = Envelope Fixed                  �
�         �       �          �   s = Playback Rate                         �
����������������������������������������������������������������������������
� $4011   � RW    � vvvvvvvv � PCM Volume Control Register                 �
�         �       �          �                                             �
�         �       �          �   v = Volume                                �
����������������������������������������������������������������������������
� $4012   � RW    � aaaaaaaa � PCM Address Register                        �
�         �       �          �                                             �
�         �       �          �   a = Address                               �
����������������������������������������������������������������������������
� $4013   � RW    � LLLLLLLL � PCM Data Length Register                    �
�         �       �          �                                             �
�         �       �          �   L = Data Size/Length                      �
����������������������������������������������������������������������������
� $4014   �  W    �          � Sprite DMA                         [SPRDMA] �
�         �       �          �                                             �
�         �       �          �  Transfers 256 bytes of memory at address   �
�         �       �          �  $100*N, where N is the value written to    �
�         �       �          �  this register, into Sprite RAM.            �
����������������������������������������������������������������������������
� $4015   � RW    � ---abcde � Sound Control Register             [SNDCNT] �
�         �       �          �                                             �
�         �       �          �   e = Channel 1  (0=Disable, 1=Enable)      �
�         �       �          �   d = Channel 2  (0=Disable, 1=Enable)      �
�         �       �          �   c = Channel 3  (0=Disable, 1=Enable)      �
�         �       �          �   b = Channel 4  (0=Disable, 1=Enable)      �
�         �       �          �   a = Channel 5  (0=Disable, 1=Enable)      �


***************************************************************************/


#include "driver.h"
#include "sndhrdw/nes.h"



#ifdef SIGNED_SAMPLES
	#define MAX_OUTPUT 0x7fff
#else
	#define MAX_OUTPUT 0xffff
#endif

#define STEP 0x10000

struct NESPSG
{
	unsigned char Regs[24];
	void *Buf;			/* sound buffer */
	int bufp;				/* update buffer point */
	unsigned int UpdateStep;
	int PeriodA,PeriodB,PeriodC,PeriodN,PeriodE;
	int ActiveTimeA,ActiveTimeB,ActiveTimeC,ActiveTimeN;
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
#define NES_ACONTROL1	(0)
#define NES_ACONTROL2	(1)
#define NES_AFINE		(2)
#define NES_ACOARSE		(3)
#define NES_BCONTROL1	(4)
#define NES_BCONTROL2	(5)
#define NES_BFINE		(6)
#define NES_BCOARSE		(7)
#define NES_CCONTROL1	(8)
#define NES_CCONTROL2	(9)
#define NES_CFINE		(10)
#define NES_CCOARSE		(11)
#define NES_NCONTROL1	(12)
#define NES_NCONTROL2	(13)
#define NES_NFINE		(14)
#define NES_NCOARSE		(15)
#define NES_PCMCONTROL	(16)
#define NES_PCMVOLUME	(17)
#define NES_PCMDMAADDR	(18)
#define NES_PCMDMALEN	(19)
#define NES_SPRITEDMA	(20)
#define NES_ENABLE		(21)
#define NES_IN0			(22)
#define NES_IN1			(23)



/*
** some globals ...
*/
static int NESBufSize;		/* size of sound buffer, in samples */
static int NESNumChips;		/* total # of PSG's emulated */

static struct NESPSG NESPSG[MAX_NESPSG];		/* array of PSG's */

static int sample_16bit;


/*
** Initialize NES PSG emulator(s).
**
** 'num'      is the number of virtual PSG's to allocate
** 'clock'    is master clock rate (Hz)
** 'rate'     is sampling rate and 'bufsiz' is the size of the
** buffer that should be updated at each interval
*/
int NESInit(int num, int clk, int rate, int bitsize, int bufsiz, void **buffer )
{
	int i;


	if (num > MAX_NESPSG) return -1;

	NESNumChips = num;
	NESBufSize = bufsiz;
	if( bitsize == 16 ) sample_16bit = 1;
	else                sample_16bit = 0;

	/* init chip state */
	for ( i = 0 ; i < NESNumChips; i++ )
	{
		memset(&NESPSG[i],0,sizeof(struct NESPSG));
		NESSetClock(i,clk,rate);
		NESPSG[i].Buf = buffer[i];
		NESSetGain(i,0x00);
		NESResetChip(i);
	}

	return 0;
}

void NESShutdown()
{
	NESBufSize = 0;
}

/*
** reset all chip registers.
*/
void NESResetChip(int num)
{
	int i;
	struct NESPSG *PSG = &NESPSG[num];


	PSG->RNG = 1;
	PSG->OutputA = 0;
	PSG->OutputB = 0;
	PSG->OutputC = 0;
	PSG->OutputN = 0xff;

	PSG->bufp = 0;
	for (i = 0;i < NES_IN0;i++)
		NESWriteReg(num,i,0);
}

/* write a register on NESPSG chip number 'n' */
void NESWriteReg(int n, int r, int v)
{
	struct NESPSG *PSG = &NESPSG[n];


	if (n >= NESNumChips)
	{
if (errorlog) fprintf(errorlog,"error: write to NES PSG #%d, allocated only %d\n",n,NESNumChips);
		return;
	}

	if (r > NES_IN1)
	{
if (errorlog) fprintf(errorlog,"error: write to NES PSG #%d register #%d\n",n,r);
		return;
	}

if (errorlog) fprintf(errorlog,"write %02x to NES PSG #%d register %d\n",v,n,r);

	PSG->Regs[r] = v;

	switch( r )
	{
	case NES_AFINE:
	case NES_ACOARSE:
		PSG->PeriodA = (1 + PSG->Regs[NES_AFINE] + 256 * (PSG->Regs[NES_ACOARSE] & 0x07)) * PSG->UpdateStep;
		PSG->ActiveTimeA = (PSG->Regs[NES_ACOARSE] >> 3) * PSG->UpdateStep / 4;
		break;
	case NES_BFINE:
	case NES_BCOARSE:
		PSG->PeriodB = (1 + PSG->Regs[NES_BFINE] + 256 * (PSG->Regs[NES_BCOARSE] & 0x07)) * PSG->UpdateStep;
		PSG->ActiveTimeB = (PSG->Regs[NES_BCOARSE] >> 3) * PSG->UpdateStep / 4;
		break;
	case NES_CFINE:
	case NES_CCOARSE:
		PSG->PeriodC = (1 + PSG->Regs[NES_CFINE] + 256 * (PSG->Regs[NES_CCOARSE] & 0x07)) * PSG->UpdateStep;
		PSG->ActiveTimeC = (PSG->Regs[NES_CCOARSE] >> 3) * PSG->UpdateStep / 4;
		break;
	case NES_NFINE:
	case NES_NCOARSE:
		PSG->PeriodN = (1 + PSG->Regs[NES_NFINE] + 256 * (PSG->Regs[NES_NCOARSE] & 0x07)) * PSG->UpdateStep;
		PSG->ActiveTimeN = (PSG->Regs[NES_NCOARSE] >> 3) * PSG->UpdateStep / 4;
		break;
	}
}



unsigned char NESReadReg(int n, int r)
{
	struct NESPSG *PSG = &NESPSG[n];


	if (n >= NESNumChips)
	{
if (errorlog) fprintf(errorlog,"error: read from NES PSG #%d, allocated only %d\n",n,NESNumChips);
		return 0;
	}

	if (r > NES_IN1)
	{
if (errorlog) fprintf(errorlog,"error: read from NES PSG #%d register #%d\n",n,r);
		return 0;
	}


	return PSG->Regs[r];
}



void NESUpdateOne(int chip,int endp)
{
	struct NESPSG *PSG = &NESPSG[chip];
	unsigned char  *buffer_8;
	unsigned short *buffer_16;
	int length;

	buffer_8  = &((unsigned char  *)PSG->Buf)[PSG->bufp];
	buffer_16 = &((unsigned short *)PSG->Buf)[PSG->bufp];

	if( endp > NESBufSize ) endp = NESBufSize;
	length = endp - PSG->bufp;

	/* buffering loop */
	while (length)
	{
		int vola,volb,volc;
		int output;
		int left;


		/* vola, volb and volc keep track of how long each square wave stays */
		/* in the 1 position during the sample period. */
		vola = volb = volc = 0;

		left = STEP;
		do
		{
			int nextevent;


			nextevent = left;

			if (PSG->OutputA) vola += PSG->CountA;
			PSG->CountA -= nextevent;
			/* PeriodA is the half period of the square wave. Here, in each */
			/* loop I add PeriodA twice, so that at the end of the loop the */
			/* square wave is in the same status (0 or 1) it was at the start. */
			/* vola is also incremented by PeriodA, since the wave has been 1 */
			/* exactly half of the time, regardless of the initial position. */
			/* If we exit the loop in the middle, OutputA has to be inverted */
			/* and vola incremented only if the exit status of the square */
			/* wave is 1. */
			while (PSG->CountA <= 0)
			{
				PSG->CountA += PSG->PeriodA;
				if (PSG->CountA > 0)
				{
					PSG->OutputA ^= 1;
					if (PSG->OutputA) vola += PSG->PeriodA;
					break;
				}
				PSG->CountA += PSG->PeriodA;
				vola += PSG->PeriodA;
			}
			if (PSG->OutputA) vola -= PSG->CountA;

			if (PSG->OutputB) volb += PSG->CountB;
			PSG->CountB -= nextevent;
			while (PSG->CountB <= 0)
			{
				PSG->CountB += PSG->PeriodB;
				if (PSG->CountB > 0)
				{
					PSG->OutputB ^= 1;
					if (PSG->OutputB) volb += PSG->PeriodB;
					break;
				}
				PSG->CountB += PSG->PeriodB;
				volb += PSG->PeriodB;
			}
			if (PSG->OutputB) volb -= PSG->CountB;

			if (PSG->OutputC) volc += PSG->CountC;
			PSG->CountC -= nextevent;
			while (PSG->CountC <= 0)
			{
				PSG->CountC += PSG->PeriodC;
				if (PSG->CountC > 0)
				{
					PSG->OutputC ^= 1;
					if (PSG->OutputC) volc += PSG->PeriodC;
					break;
				}
				PSG->CountC += PSG->PeriodC;
				volc += PSG->PeriodC;
			}
			if (PSG->OutputC) volc -= PSG->CountC;

			left -= nextevent;
		} while (left > 0);

/*		output = vola*PSG->VolA + volb*PSG->VolB + volc*PSG->VolC;*/
		output = 0;
		if (PSG->ActiveTimeA > 0)
		{
			PSG->ActiveTimeA--;
			output += vola*0x2aaa;
		}
		if (PSG->ActiveTimeB > 0)
		{
			PSG->ActiveTimeB--;
			output += volb*0x2aaa;
		}
		if (PSG->ActiveTimeC > 0)
		{
			PSG->ActiveTimeC--;
			output += volc*0x2aaa;
		}
		if( sample_16bit ) *buffer_16++ = output / STEP;
		else               *buffer_8++  = output / (STEP*256);

		length--;
	}
	PSG->bufp  = endp;
}



/*
** called to update all chips
*/
void NESUpdate(void)
{
	int i;


	for (i = 0;i < NESNumChips;i++)
	{
		NESUpdateOne( i , NESBufSize );
		NESPSG[i].bufp = 0;
	}
}



void NESSetClock(int n,int clk,int rate)
{
	NESPSG[n].UpdateStep = ((double)STEP * rate * 128) / clk;
}



/*
** set output gain
**
** The gain is expressed in 0.2dB increments, e.g. a gain of 10 is an increase
** of 2dB. Note that the gain a�only affects sounds not playing at full volume,
** since the ones at full volume are already played at the maximum intensity
** allowed by the sound card.
** 0x00 is the default.
** 0xff is the maximum allowed value.
*/
void NESSetGain(int n,int gain)
{
	struct NESPSG *PSG = &NESPSG[n];
	int i;
	double out;


	gain &= 0xff;

	/* increase max output basing on gain (0.2 dB per step) */
	out = MAX_OUTPUT/3;
	while (gain-- > 0)
		out *= 1.023292992;	/* = (10 ^ (0.2/20)) */

	/* calculate the volume->voltage conversion table */
	/* The AY-3-8910 has 16 levels, in a logarithmic scale (3dB per step) */
	/* The YM2203 still has 16 levels for the tone generators, but 32 for */
	/* the envelope generator (1.5dB per step). */
	for (i = 31;i > 0;i--)
	{
		/* limit volume to avoid clipping */
		if (out > MAX_OUTPUT/3) PSG->VolTable[i] = MAX_OUTPUT/3;
		else PSG->VolTable[i] = out;

		out /= 1.188502227;	/* = 10 ^ (1.5/20) */
	}
	PSG->VolTable[0] = 0;
}
