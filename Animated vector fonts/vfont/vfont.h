//
// A Vector font library based upon the Hershey font set
// 
// Michael McElligott
// okio@users.sourceforge.net
// https://youtu.be/T0WgGcm7ujM
//
//  Copyright (c) 2005-2017  Michael McElligott
// 
//  This program is free software; you can redistribute it and/or
//  modify it under the terms of the GNU LIBRARY GENERAL PUBLIC LICENSE
//  as published by the Free Software Foundation; either version 2
//  of the License, or (at your option) any later version.
//
//  This program is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//  GNU LIBRARY GENERAL PUBLIC LICENSE for more details.
//
//	You should have received a copy of the GNU Library General Public
//	License along with this library; if not, write to the Free
//	Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

#ifndef _VFONT_H_
#define _VFONT_H_

#include "hfont.h"

#define VWIDTH		(480)
#define VHEIGHT		(272)

extern uint8_t renderBuffer[VWIDTH*VHEIGHT];	// our render distination. defined in primitives.cpp

//#define LINE_STD			1		// slowest: standard Bresenham algorithm
//#define LINE_FAST			1		// fast: similar to Bresenham but forgoes accuracy for performance
//#define LINE_FASTEST16	1		// faster: render direct to a 16bit buffer. about 3x faster than LINE_STD
#define LINE_FASTEST8		1		// fastest: render direct to 8bit buffer, with a palette.

#if LINE_FASTEST8

#define drawPixel			drawPixel8		// 16Bpp via an 8bit lookup table (palette)
#else
#define drawPixel			drawPixel16		// 16bit 565
#endif

enum _brush{
	BRUSH_POINT,			// .
	BRUSH_CIRCLE,			// O
	BRUSH_CIRCLE_FILLED,
	BRUSH_SQUARE,		
	BRUSH_SQUARE_FILLED,	
	BRUSH_TRIANGLE,			// north facing
	BRUSH_TRIANGLE_FILLED,	
	BRUSH_STROKE_1,			// slope up		/	
	BRUSH_STROKE_2,			// slope down	\ symbol
	BRUSH_STROKE_3,			// horizontal	-	
	BRUSH_STROKE_4,			// vertical		|	
	BRUSH_STROKE_5,			// 
	BRUSH_STROKE_6,			// 
	BRUSH_STAR,				// *
	BRUSH_X,				// X
	BRUSH_CARET,			// ^
	BRUSH_PLUS,				// +
	BRUSH_BITMAP,

	BRUSH_TOTAL,
	BRUSH_DISK = BRUSH_CIRCLE_FILLED		
};

#define RENDEROP_NONE			0x00
#define RENDEROP_SHEAR_X		0x01
#define RENDEROP_SHEAR_Y		0x02
#define RENDEROP_SHEAR_XYX		0x04
#define RENDEROP_ROTATE_STRING	0x08
#define RENDEROP_ROTATE_GLYPHS	0x10

#ifndef DEG2RAD
#define DEG2RAD(a)				((a)*((M_PI / 180.0f)))
#endif

#define CALC_PITCH_1(w)			(((w)>>3)+(((w)&0x07)!=0))	// 1bit packed, calculate number of storage bytes per row given width (of glyph)
#define CALC_PITCH_16(w)		((w)*sizeof(uint16_t))		// 16bit, 8 bits per byte

/*#define COLOUR_RED				RGB_16_RED
#define COLOUR_GREEN			RGB_16_GREEN
#define COLOUR_BLUE				RGB_16_BLUE
#define COLOUR_WHITE			RGB_16_WHITE
#define COLOUR_BLACK			RGB_16_BLACK
#define COLOUR_MAGENTA			RGB_16_MAGENTA
#define COLOUR_YELLOW			RGB_16_YELLOW
#define COLOUR_CYAN				RGB_16_CYAN*/
#define COLOUR_CREAM			(((0xEE&0xF8)<<8) | ((0xE7&0xFC)<<3) | 0xD0>>3)
#define COLOUR_24TO16(c)		((((c>>16)&0xF8)<<8) | (((c>>8)&0xFC)<<3) | ((c&0xF8)>>3))

typedef struct{
	float x1;
	float y1;
	float x2;
	float y2;
}box_t;

typedef struct {
	float angle;
	float cos;
	float sin;
}rotate_t;

typedef struct {
	const uint8_t *pixels;
	uint8_t width;
	uint8_t height;
	uint8_t stubUnused;
}image_t;
		
typedef struct {
	const hfont_t *font;
	int x;				// initial rendering position
	int y;
	
	struct {			// current rendering position
		float x;
		float y;
	}pos;

	float xpad;			// add horizontal padding. can be minus. eg; -0.5f

	struct {
		float glyph;		// vector scale/glyph size (2.0 = float size glyph)
		float horizontal;
		float vertical;
	}scale;

	struct {
		rotate_t string;
		rotate_t glyph;
	}rotate;
	
	struct {
		float angleX;
		float angleY;
		float cos;
		float sin;
		float tan;
	}shear;

	struct {
		float size;
		float step;
		float advanceMult;
		uint16_t type;
		uint16_t colour;

		image_t image;
	}brush;
	
	uint16_t renderOp;
}vfont_t;

#include "fonts.h"
#include "brushes.h"

void vfontInitialise (vfont_t *ctx);
void setAspect (vfont_t *ctx, const float hori, const float vert);
void setShearAngle (vfont_t *ctx, const float shrX, const float shrY);
void setFont (vfont_t *ctx, const hfont_t *font);
int setBrushBitmap (vfont_t *ctx, const void *bitmap, const uint8_t width, const uint8_t height);
const hfont_t *getFont (vfont_t *ctx);
int setBrush (vfont_t *ctx, const int brush);
float setBrushSize (vfont_t *ctx, const float size);
void setBrushStep (vfont_t *ctx, const float step);
float getBrushStep (vfont_t *ctx);
void setGlyphScale (vfont_t *ctx, const float scale);
float getGlyphScale (vfont_t *ctx);
void setGlyphPadding (vfont_t *ctx, const float pad);
float getGlyphPadding (vfont_t *ctx);
uint16_t setBrushColour (vfont_t *ctx, const uint16_t colour);
void setRenderFilter (vfont_t *ctx, const uint32_t op);
uint32_t getRenderFilter (vfont_t *ctx);

void setRotationAngle (vfont_t *ctx, const float rotGlyph, const float rotString);
void setShearAngle (vfont_t *ctx, const float shrX, const float shrY);

float getCharMetrics (vfont_t *ctx, const hfont_t *font, const uint16_t c, float *adv, box_t *box);
void getGlyphMetrics (vfont_t *ctx, const uint16_t c, int *w, int *h);
void getStringMetrics (vfont_t *ctx, const char *text, box_t *box);

void drawString (vfont_t *ctx, const char *text, const int x, const int y);

// primitives, one should never be compelled to use these
/*void drawBitmap (image_t *img, int x, int y, const uint16_t colour);
void drawRectangle (int x1, int y1, int x2, int y2, const uint16_t colour);
void drawRectangleFilled (int x1, int y1, int x2, int y2, const uint16_t colour);
void drawTriangle (const int x1, const int y1, const int x2, const int y2, const int x3, const int y3, const uint16_t colour);
void drawTriangleFilled (const int x0, const int y0, const int x1, const int y1, const int x2, const int y2, const uint16_t colour);
void drawCircle (const int xc, const int yc, const int radius, const uint16_t colour);
void drawCircleFilled (const int x0, const int y0, const float radius, const uint16_t colour);
void drawLine (const int x1, const int y1, const int x2, const int y2, const uint16_t colour);
*/
static const inline void drawPixel8 (const int x, const int y, const uint8_t colourIdx)
{
	uint8_t *pixels = (uint8_t*)renderBuffer;	
	pixels[(y*VWIDTH)+x] = colourIdx;
	//*(pixels+(y*VWIDTH)+x) = colourIdx;
}

#endif
