#ifndef _FONTS_H_
#define _FONTS_H_

#undef	DECLARE_FONT
#define DECLARE_FONT(name) extern const hfont_t (name);

DECLARE_FONT(romans)
DECLARE_FONT(romand)
DECLARE_FONT(romant)
DECLARE_FONT(timesg)
DECLARE_FONT(timesi)
DECLARE_FONT(timesib)
DECLARE_FONT(timesrb)
DECLARE_FONT(timesr)
DECLARE_FONT(futural)
DECLARE_FONT(futuram)
DECLARE_FONT(astrology)
DECLARE_FONT(meteorology)
DECLARE_FONT(mathupp)
DECLARE_FONT(cursive)
DECLARE_FONT(cyrillic1)
DECLARE_FONT(cyrillic2)
DECLARE_FONT(gothgbt)
DECLARE_FONT(gothgrt)
DECLARE_FONT(gothiceng)
DECLARE_FONT(gothicger)
DECLARE_FONT(gothicita)
DECLARE_FONT(gothitt)
DECLARE_FONT(greek)
DECLARE_FONT(greekc)
DECLARE_FONT(greeks)
DECLARE_FONT(japanesea)
DECLARE_FONT(japaneseb)
DECLARE_FONT(markers)
DECLARE_FONT(mathlow)
DECLARE_FONT(music)
DECLARE_FONT(scriptc)
DECLARE_FONT(scripts)
DECLARE_FONT(symbolic)


/*

 The Hershey Fonts:
  -  each point is described in eight bytes as "xxx yyy:", where xxx and yyy are
  -  the coordinate values as ASCII numbers.
  - are a set of more than 2000 glyph (symbol) descriptions in vector
    ( <x,y>; point-to-point ) format
  - can be grouped as almost 20 'occidental' (english, greek,
    cyrillic) fonts, 3 or more 'oriental' (Kanji, Hiragana,
    and Katakana) fonts, and a few hundred miscellaneous
    symbols (mathematical, musical, cartographic, etc etc)
  - are suitable for typographic quality output on a vector device
    (such as a plotter) when used at an appropriate scale.



The structure is bascially as follows: each character consists of a number 1->4000 (not all used) in column 0:4,
the number of vertices in columns 5:7, the left hand position in column 8, the right hand position in column 9,
and finally the vertices in single character pairs.
All coordinates are given relative to the ascii value of 'R'.
If the coordinate value is " " that indicates a pen up operation.

As an example consider the 8th symbol

    MWOMOV UMUV OQUQ

The left position is 'M' - 'R' = -5
The right position is 'W' - 'R' = 5
The first coordinate is "OM" = (-3,-5)
The second coordinate is "OV" = (-3,4)
Raise the pen " " (new vector)
Move to "UM" = (3,-5)
Draw to "UV" = (3,4)
Raise the pen " " (new vector)
Move to "OQ" = (-3,-1)
Draw to "UQ" = (3,-1)
Drawing this out on a piece of paper will reveal it represents an 'H'. 

*/

#if 0
#include "fonts/astrology.h"
#include "fonts/cursive.h"
#include "fonts/cyrilc_1.h"
#include "fonts/cyrillic.h"
#include "fonts/futural.h"
#include "fonts/futuram.h"
#include "fonts/gothgbt.h"
#include "fonts/gothgrt.h"
#include "fonts/gothiceng.h"
#include "fonts/gothicger.h"
#include "fonts/gothicita.h"
#include "fonts/gothitt.h"
#include "fonts/greek.h"
#include "fonts/greekc.h"
#include "fonts/greeks.h"
#include "fonts/markers.h"
#include "fonts/mathlow.h"
#include "fonts/mathupp.h"
#include "fonts/meteorology.h"
#include "fonts/music.h"
#include "fonts/rowmand.h"
#include "fonts/rowmans.h"
#include "fonts/rowmant.h"
#include "fonts/scriptc.h"
#include "fonts/scripts.h"
#include "fonts/symbolic.h"
#include "fonts/timesg.h"
#include "fonts/timesi.h"
#include "fonts/timesib.h"
#include "fonts/timesr.h"
#include "fonts/timesrb.h"
#endif

#if 0
#include "fonts/japanesea.h"
#include "fonts/japaneseb.h"
#endif

#endif
