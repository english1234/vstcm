/*********************************************************************

  common.h

  Generic functions, mostly ROM and graphics related.

*********************************************************************/

#ifndef COMMON_H
#define COMMON_H

#include "osdepend.h"

struct RomModule
{
	const char *name;			/* name of the file to load */
	unsigned int offset;		/* offset to load it to */
	unsigned int length;		/* length of the file */
	unsigned int crc;			/* standard CRC-32 checksum */
};

/* there are some special cases for the above. name, offset and size all set to 0 */
/* mark the end of the aray. If name is 0 and the others aren't, that means "continue */
/* reading the previous rom from this address". If length is 0 and offset is not 0, */
/* that marks the start of a new memory region. Confused? Well, don't worry, just use */
/* the macros below. */

#define ROMFLAG_MASK          0xf0000000           /* 4 bits worth of flags in the high nibble */
#define ROMFLAG_ALTERNATE     0x80000000           /* Alternate bytes, either even or odd */
#define ROMFLAG_DISPOSE       0x80000000           /* Dispose of this region when done */
#define ROMFLAG_IGNORE        0x40000000           /* BM: Ignored - drivers must load this region themselves */
#define ROMFLAG_WIDE          0x40000000           /* 16-bit ROM; may need byte swapping */
#define ROMFLAG_SWAP          0x20000000           /* 16-bit ROM with bytes in wrong order */

/* start of table */
#define ROM_START(name) static struct RomModule name[] = {
/* start of memory region */
#define ROM_REGION(length) { 0, length, 0, 0 },
/* start of disposable memory region */
#define ROM_REGION_DISPOSE(length) { 0, length | ROMFLAG_DISPOSE, 0, 0 },

/* Optional */
#define ROM_REGION_OPTIONAL(length) { 0, length | ROMFLAG_IGNORE, 0, 0 },

/* ROM to load */
#define ROM_LOAD(name,offset,length,crc) { name, offset, length, crc },

/* continue loading the previous ROM to a new address */
#define ROM_CONTINUE(offset,length) { 0, offset, length, 0 },
/* restart loading the previous ROM to a new address */
#define ROM_RELOAD(offset,length) { (char *)-1, offset, length, 0 },

/* The following ones are for code ONLY - don't use for graphics data!!! */
/* load the ROM at even/odd addresses. Useful with 16 bit games */
#define ROM_LOAD_EVEN(name,offset,length,crc) { name, offset & ~1, length | ROMFLAG_ALTERNATE, crc },
#define ROM_LOAD_ODD(name,offset,length,crc)  { name, offset |  1, length | ROMFLAG_ALTERNATE, crc },
/* load the ROM at even/odd addresses. Useful with 16 bit games */
#define ROM_LOAD_WIDE(name,offset,length,crc) { name, offset, length | ROMFLAG_WIDE, crc },
#define ROM_LOAD_WIDE_SWAP(name,offset,length,crc) { name, offset, length | ROMFLAG_WIDE | ROMFLAG_SWAP, crc },

/* Use THESE ones for graphics data */
#ifdef LSB_FIRST
#define ROM_LOAD_GFX_EVEN  ROM_LOAD_ODD
#define ROM_LOAD_GFX_ODD   ROM_LOAD_EVEN
#else
#define ROM_LOAD_GFX_EVEN  ROM_LOAD_EVEN
#define ROM_LOAD_GFX_ODD   ROM_LOAD_ODD
#endif

/* end of table */
#define ROM_END { 0, 0, 0, 0 } };



struct GameSample
{
	int length;
	int smpfreq;
	unsigned char resolution;
	unsigned char volume;
	signed char data[1];	/* extendable */
};

struct GameSamples
{
	int total;	/* total number of samples */
	struct GameSample *sample[1];	/* extendable */
};



struct GfxLayout
{
	unsigned short width,height; /* width and height of chars/sprites */
	unsigned int total; /* total numer of chars/sprites in the rom */
	unsigned short planes; /* number of bitplanes */
	int planeoffset[8]; /* start of every bitplane */
	int xoffset[32]; /* coordinates of the bit corresponding to the pixel */
	int yoffset[32]; /* of the given coordinates */
	short charincrement; /* distance between two consecutive characters/sprites */
};

struct GfxElement
{
	int width,height;

	struct osd_bitmap *gfxdata;	/* graphic data */
	unsigned int total_elements;	/* total number of characters/sprites */

	int color_granularity;	/* number of colors for each color code */
							/* (for example, 4 for 2 bitplanes gfx) */
	unsigned short *colortable;	/* map color codes to screen pens */
								/* if this is 0, the function does a verbatim copy */
	int total_colors;
	unsigned int *pen_usage;	/* an array of total_elements ints. */
								/* It is a table of the pens each character uses */
								/* (bit 0 = pen 0, and so on). This is used by */
								/* drawgfgx() to do optimizations like skipping */
								/* drawing of a totally transparent characters */
};

struct GfxDecodeInfo
{
	int memory_region;	/* memory region where the data resides (usually 1) */
						/* -1 marks the end of the array */
	int start;	/* beginning of data to decode */
	struct GfxLayout *gfxlayout;
	int color_codes_start;	/* offset in the color lookup table where color codes start */
	int total_color_codes;	/* total number of color codes */
};


struct rectangle
{
	int min_x,max_x;
	int min_y,max_y;
};



#define TRANSPARENCY_NONE 0
#define TRANSPARENCY_PEN 1
#define TRANSPARENCY_PENS 4
#define TRANSPARENCY_COLOR 2
#define TRANSPARENCY_THROUGH 3

void showdisclaimer(void);

/* LBO 042898 - added coin counters */
#define COIN_COUNTERS	4	/* total # of coin counters */
void coin_counter_w (int offset, int data);
void coin_lockout_w (int offset, int data);
void coin_lockout_global_w (int offset, int data);  /* Locks out all coin inputs */

int readroms(void);
void printromlist(const struct RomModule *romp,const char *basename);
struct GameSamples *readsamples(const char **samplenames,const char *basename);
void freesamples(struct GameSamples *samples);
void decodechar(struct GfxElement *gfx,int num,const unsigned char *src,const struct GfxLayout *gl);
struct GfxElement *decodegfx(const unsigned char *src,const struct GfxLayout *gl);
void freegfx(struct GfxElement *gfx);
void drawgfx(struct osd_bitmap *dest,const struct GfxElement *gfx,
		unsigned int code,unsigned int color,int flipx,int flipy,int sx,int sy,
		const struct rectangle *clip,int transparency,int transparent_color);
void copybitmap(struct osd_bitmap *dest,struct osd_bitmap *src,int flipx,int flipy,int sx,int sy,
		const struct rectangle *clip,int transparency,int transparent_color);
void copybitmapzoom(struct osd_bitmap *dest,struct osd_bitmap *src,int flipx,int flipy,int sx,int sy,
		const struct rectangle *clip,int transparency,int transparent_color,int scalex,int scaley);
void copyscrollbitmap(struct osd_bitmap *dest,struct osd_bitmap *src,
		int rows,const int *rowscroll,int cols,const int *colscroll,
		const struct rectangle *clip,int transparency,int transparent_color);
void fillbitmap(struct osd_bitmap *dest,int pen,const struct rectangle *clip);
void drawgfxzoom( struct osd_bitmap *dest_bmp,const struct GfxElement *gfx,
		unsigned int code,unsigned int color,int flipx,int flipy,int sx,int sy,
		const struct rectangle *clip,int transparency,int transparent_color,int scalex, int scaley );

#endif
