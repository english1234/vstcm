/***************************************************************************

  vidhrdw/generic.c

  Some general purpose functions used by many video drivers.

***************************************************************************/

#include "driver.h"
#include "vidhrdw/generic.h"



unsigned char *videoram;
int videoram_size;
unsigned char *colorram;
unsigned char *spriteram;	/* not used in this module... */
unsigned char *spriteram_2;	/* ... */
unsigned char *spriteram_3;	/* ... */
int spriteram_size;	/* ... here just for convenience */
int spriteram_2_size;	/* ... here just for convenience */
int spriteram_3_size;	/* ... here just for convenience */
unsigned char *flip_screen;	/* ... */
unsigned char *flip_screen_x;	/* ... */
unsigned char *flip_screen_y;	/* ... */
unsigned char *dirtybuffer;
struct osd_bitmap *tmpbitmap;



/***************************************************************************

  Start the video hardware emulation.

***************************************************************************/
int generic_vh_start(void)
{
	int i;


	dirtybuffer = 0;
	tmpbitmap = 0;

	if (videoram_size == 0)
	{
if (errorlog) fprintf(errorlog,"Error: generic_vh_start() called but videoram_size not initialized\n");
		return 1;
	}

	if ((dirtybuffer = malloc(videoram_size)) == 0)
		return 1;
	memset(dirtybuffer,1,videoram_size);

	if ((tmpbitmap = osd_new_bitmap(Machine->drv->screen_width,Machine->drv->screen_height,Machine->scrbitmap->depth)) == 0)
	{
		free(dirtybuffer);
		return 1;
	}

	return 0;
}



/***************************************************************************

  Stop the video hardware emulation.

***************************************************************************/
void generic_vh_stop(void)
{
	int i;


	free(dirtybuffer);
	osd_free_bitmap(tmpbitmap);

	dirtybuffer = 0;
	tmpbitmap = 0;
}



int videoram_r(int offset)
{
	return videoram[offset];
}

int colorram_r(int offset)
{
	return colorram[offset];
}

void videoram_w(int offset,int data)
{
	if (videoram[offset] != data)
	{
		dirtybuffer[offset] = 1;

		videoram[offset] = data;
	}
}

void colorram_w(int offset,int data)
{
	if (colorram[offset] != data)
	{
		dirtybuffer[offset] = 1;

		colorram[offset] = data;
	}
}



int spriteram_r(int offset)
{
	return spriteram[offset];
}

void spriteram_w(int offset,int data)
{
	spriteram[offset] = data;
}
