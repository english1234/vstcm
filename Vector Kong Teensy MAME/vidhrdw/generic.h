#include "driver.h"


extern unsigned char *videoram;
extern int videoram_size;
extern unsigned char *colorram;
extern unsigned char *spriteram;
extern int spriteram_size;
extern unsigned char *spriteram_2;
extern int spriteram_2_size;
extern unsigned char *spriteram_3;
extern int spriteram_3_size;
extern unsigned char *flip_screen;
extern unsigned char *flip_screen_x;
extern unsigned char *flip_screen_y;
extern unsigned char *dirtybuffer;
extern struct osd_bitmap *tmpbitmap;

int generic_vh_start(void);
void generic_vh_stop(void);
int videoram_r(int offset);
int colorram_r(int offset);
void videoram_w(int offset,int data);
void colorram_w(int offset,int data);
int spriteram_r(int offset);
void spriteram_w(int offset,int data);
