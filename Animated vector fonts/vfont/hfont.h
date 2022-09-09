#ifndef _HFONT_H
#define _HFONT_H

//#pragma GCC optimize ("-O2")

#include <inttypes.h>

typedef struct {
	uint32_t glyphCount;
	char *glyphs[];
}hfont_t;

#endif
