#include <string.h>
#include "emuapi.h"

static int prev_key = 0;
static int prev_j = 0;
static int hk = 0;
static int prev_hk = 0;
static int k = 0;

extern void mam_Input(int click) {
  //hk = emu_ReadI2CKeyboard();
  k = emu_ReadKeys();
}

#include "osdepend.h"
#include "driver.h"

static struct GameOptions options;
static int vector_game;
static int bitmap_safety = 8;
static struct osd_bitmap *scrbitmap;
int display_updated = 0;
unsigned char No_FM = 1;
int nocheat = 1;

#define USE_CLUT 1

#ifdef USE_CLUT
#define SCREEN_DEPTH 8
#else
#define SCREEN_DEPTH 16
#endif

/**********************************************************
 Patches for std
***********************************************************/
#include <stdarg.h>

int myport_Printf(const char *f, ...) {
  int r = 0;
  va_list args;
  char *buf[256];
  //    printf("printing using format: %s", f);
  //   va_start(args, f);
  //   sprintf(buf, f, args);
  //   emu_printf(buf);

  //    log = fopen("log.txt","ab");
  ////    r = vprintf(f, args);
  //    r = vfprintf(log, f, args);
  //   va_end(args);
  //    fclose(log);

  return r;
}

#define Option_VariadicMacro(f, ...) \
  printf("printing using format: %s", f); \
  printf(f, __VA_ARGS__)

/*
void * myport_malloc(size_t size, char * file, int line) {
  void * retval =  malloc(size);
  myport_Printf("allocating %d at %u from %s line %d\n", size, retval, file, line);
  return retval;
}

void myport_Free(void * pt)
{
  if (pt != NULL) {
    myport_Printf("freing %u\n", (unsigned int)pt);
    free(pt);
  }
}
*/

#define malloc(x) emu_Malloc(x)
#define free(x) emu_Free(x)

/**********************************************************
 Patches for files
***********************************************************/
int myport_FileOpen(const char *filename, const char *mode) {
  return (emu_FileOpen(filename));
  //  int retval=0;
  //  retval = (int)fopen(filename,mode);
  //  return retval;
}

int myport_FileSeek(int handle, int offset, int seek) {
  return (emu_FileSeek(offset));
  //  int retval=0;
  //  retval = fseek((FILE*)handle,offset,seek);
  //  return retval;
}

int myport_FileRead(void *buf, int sizet, int size, int handle) {
  return (emu_FileRead(buf, sizet * size));
  //  int retval=0;
  //  retval = fread(buf,sizet,size,(FILE*)handle);
  //  return retval;
}

int myport_FileClose(int handle) {
  emu_FileClose();
  return (1);
  //  int retval=0;
  //  retval = fclose((FILE*)handle);
  //  return retval;
}

int myport_FileTell(int handle) {
  return (emu_FileTell());
  //  int retval=0;
  //  retval = ftell((FILE*)handle);
  //  return retval;
}

int myport_FilePrintf(int handle, const char *format, va_list ap) {
  return (emu_FilePrintf(format, ap));
  //  int retval=0;
  //  retval = fprintf((FILE*)handle, format, ap);
  //  return retval;
}


/**********************************************************
 Patches for init
***********************************************************/
int osd_init(void) {
  myport_Printf("osd_init\n");
  return 0;
}

void osd_exit(void) {
  myport_Printf("osd_exit\n");
}

/**********************************************************
 Patches for display
***********************************************************/
void osd_mark_dirty(int x1, int _y1, int x2, int y2, int ui) {
}

static struct osd_bitmap *new_bitmap(int width, int height, int depth) {
  struct osd_bitmap *bitmap;

  myport_Printf("new_bitmap. depth=> %i\n", depth);

  if (Machine->orientation & ORIENTATION_SWAP_XY) {
    int temp;

    temp = width;
    width = height;
    height = temp;
  }
  if ((bitmap = malloc(sizeof(struct osd_bitmap))) != 0) {
    int i, rowlen, rdwidth;
    unsigned char *bm;
    int safety;

    myport_Printf("osd_new_bitmap. New bitmap allocated\n");

    if (width > 32) safety = bitmap_safety;
    else safety = 0; /* don't create the safety area for GfxElement bitmaps */
    bitmap->depth = depth;
    bitmap->width = width;
    bitmap->height = height;
    bitmap->line = NULL;
    bitmap->_private = NULL;
  } else {
    myport_Printf("Cannot allocate the bitmap.\n");
  }
  return bitmap;
}

struct osd_bitmap *osd_new_bitmap(int width, int height, int depth) /* ASG 980209 */
{
  struct osd_bitmap *bitmap;

  myport_Printf("osd_new_bitmap. depth=> %i\n", depth);

  if (Machine->orientation & ORIENTATION_SWAP_XY) {
    int temp;

    temp = width;
    width = height;
    height = temp;
  }

  if ((bitmap = malloc(sizeof(struct osd_bitmap))) != 0) {
    int i, rowlen, rdwidth;
    unsigned char *bm;
    int safety;


    if (width > 32) safety = 8;
    else safety = 0; /* don't create the safety area for GfxElement bitmaps */

    //    if (depth != 8 && depth != 16) depth = 8;
    depth = SCREEN_DEPTH;

    bitmap->depth = depth;
    bitmap->width = width;
    bitmap->height = height;
    bitmap->line = NULL;
    bitmap->_private = NULL;

    rdwidth = (width + 7) & ~7; /* round width to a quadword */
    if (depth == 16)
      rowlen = 2 * (rdwidth + 2 * safety) * sizeof(unsigned char);
    else
      rowlen = (rdwidth + 2 * safety) * sizeof(unsigned char);

    if ((bm = malloc((height + 2 * safety) * rowlen)) == 0) {
      free(bitmap);
      return 0;
    }

    if ((bitmap->line = malloc(height * sizeof(unsigned char *))) == 0) {
      free(bm);
      free(bitmap);
      return 0;
    }

    for (i = 0; i < height; i++)
      bitmap->line[i] = &bm[(i + safety) * rowlen + safety];

    bitmap->_private = bm;

    osd_clearbitmap(bitmap);
  }

  return bitmap;
}

/* set the bitmap to black */
void osd_clearbitmap(struct osd_bitmap *bitmap) {
  emu_printf("osd_clearbitmap");
  int i;

  if (bitmap != scrbitmap) {
    for (i = 0; i < bitmap->height; i++) {
      if (bitmap->depth == 16)
        memset(bitmap->line[i], 0, 2 * bitmap->width);
      else
        memset(bitmap->line[i], 0, bitmap->width);
    }
  }
}

void osd_free_bitmap(struct osd_bitmap *bitmap) {
  if (bitmap) {
    if (bitmap->line) free(bitmap->line);
    if (bitmap->_private) free(bitmap->_private);
    free(bitmap);
  }
}

struct osd_bitmap *osd_create_display(int width, int height, int attributes) {
#ifdef USE_CLUT
  unsigned char *pixels;
#else
  unsigned short *pixels;
#endif

  emu_printi(width);
  emu_printi(height);

  int stride = 0;
  int i = 0;

  /* Look if this is a vector game */
  if (Machine->drv->video_attributes & VIDEO_TYPE_VECTOR)
    vector_game = 1;
  else
    vector_game = 0;


  scrbitmap = new_bitmap(width, height, SCREEN_DEPTH);
  if (!scrbitmap) return 0;

  if ((scrbitmap->line = malloc(scrbitmap->height * sizeof(unsigned char *))) == 0) {
    myport_Printf("osd_new_bitmap. Could not allocate bitmap->line\n");
    free(scrbitmap);
    return 0;
  }

  pixels = malloc(scrbitmap->height * scrbitmap->width * SCREEN_DEPTH / 8);
  if (!pixels) {
    myport_Printf("osd_new_bitmap. Could not allocate bitmap buffer\n");
    free(scrbitmap);
    return 0;
  }
  memset(pixels, 0, scrbitmap->height * scrbitmap->width * SCREEN_DEPTH / 8);
  //scrbitmap->_private = pixels;

  stride = scrbitmap->width;
  for (i = 0; i < scrbitmap->height; i++) {
    scrbitmap->line[i] = &pixels[(i * stride)];
  }

  return scrbitmap;
}

/* Close the display. */
void osd_close_display(void) {
  myport_Printf("osd_close_display\n");
  if (scrbitmap) {
    osd_free_bitmap(scrbitmap);
    scrbitmap = NULL;
  }
}

#define MIN(a, b) ((a < b) ? a : b)
#define TFT_HEIGHT 240

/* Update the display. */
void osd_update_display(void) {
  // emu_printf("osd_update_display");
  /*
  int i;
  for(i=0;i<MIN(scrbitmap->height,TFT_HEIGHT); i++)
  {
#ifdef USE_CLUT
    emu_DrawLine((unsigned char *)scrbitmap->line[i], MIN(scrbitmap->width,TFT_WIDTH) , scrbitmap->height, i); 
#else
    emu_DrawLine16((unsigned short *)scrbitmap->line[i], MIN(scrbitmap->width,TFT_WIDTH) , scrbitmap->height, i); 
#endif    
  }
*/

  int delta = 0;
  if (scrbitmap->height > TFT_HEIGHT) {
    delta = scrbitmap->height / (scrbitmap->height - TFT_HEIGHT) - 1;
  }
  int i;
  int line = 0;
  int pos = delta;
  for (i = 0; i < MIN(TFT_HEIGHT, scrbitmap->height); i++) {
    pos--;
    if ((pos == 0) && (delta)) {
      line++;
      pos = delta;
    }
#ifdef USE_CLUT
    emu_DrawLine((unsigned char *)scrbitmap->line[line], scrbitmap->width, TFT_HEIGHT, i);
#else
    emu_DrawLine16((unsigned short *)scrbitmap->line[line], scrbitmap->width, WIN_H, i);
#endif
    line++;
  }

  display_updated = 1;
  emu_DrawVsync();
}

/*
 * palette is an array of 'totalcolors' R,G,B triplets. The function returns
 * in *pens the pen values corresponding to the requested colors.
 * If 'totalcolors' is 32768, 'palette' is ignored and the *pens array is filled
 * with pen values corresponding to a 5-5-5 15-bit palette
 */
void osd_allocate_colors(unsigned int totalcolors, const unsigned char *palette, unsigned short *pens) {
  if (totalcolors == 32768) {
    int r, g, b;
    for (r = 0; r < 32; r++)
      for (g = 0; g < 32; g++)
        for (b = 0; b < 32; b++) {}
    //        *pens++ = RGBVAL16((r << 3) | (r >> 2),(g << 3) | (g >> 2),(b << 3) | (b >> 2));
  } else {
    int i;

    /* fill the palette starting from the end, so we mess up badly written */
    /* drivers which don't go through Machine->pens[] */
    for (i = 0; i < totalcolors; i++)
      pens[i] = 255 - i;

    for (i = 0; i < totalcolors; i++) {
      //   pens[i] = RGBVAL16(palette[3*i], palette[3*i+1], palette[3*i+2]);
    }
  }

  int i;
  myport_Printf("Filling the pens for %i colors.\n", totalcolors);
#ifdef USE_CLUT
  for (i = 0; i < totalcolors; i++) {
    emu_SetPaletteEntry(palette[3 * i], palette[3 * i + 1], palette[3 * i + 2], i);
    pens[i] = i;
  }
#endif
}

/*
 * Reset the given pen to the specified rgb values.
 */
void osd_modify_pen(int pen, unsigned char red, unsigned char green, unsigned char blue) {
  emu_SetPaletteEntry(red, green, blue, pen);
}

void osd_get_pen(int pen, unsigned char *red, unsigned char *green, unsigned char *blue) {
  if (scrbitmap->depth == 16) {
    *red = ((pen >> 11) & 0x1f) << 3;
    *green = ((pen >> 5) & 0x2f) << 2;
    *blue = (pen & 0x1f) << 3;
  } else {
    *red = 0;    //current_palette[pen][0];
    *green = 0;  //= current_palette[pen][1];
    *blue = 0;   //current_palette[pen][2];
  }
}

/**********************************************************
 Patches for audio
***********************************************************/
void osd_update_audio(void) {
}

//play a sample in 8 bit
void osd_play_sample(int channel, signed char *data, int len, int freq, int volume, int loop) {
  //myport_Printf("osd_play_sample\n");
}
//play a sample in 16 bit
void osd_play_sample_16(int channel, signed short *data, int len, int freq, int volume, int loop) {
  //myport_Printf("osd_play_sample_16\n");
}
//play a streamed sample in 8 bit
void osd_play_streamed_sample(int channel, signed char *data, int len, int freq, int volume, int pan) {
}
//play a streamed sample in 16 bit
void osd_play_streamed_sample_16(int channel, signed short *data, int len, int freq, int volume, int pan) {
  //myport_Printf("osd_play_streamed_sample_16\n");
}
void osd_adjust_sample(int channel, int freq, int volume) {
  //myport_Printf("osd_adjust_sample\n");
}
void osd_stop_sample(int channel) {
  //myport_Printf("osd_stop_sample\n");
}
void osd_restart_sample(int channel) {
  //myport_Printf("osd_restart_sample\n");
}
int osd_get_sample_status(int channel) {
  //myport_Printf("osd_get_sample_status\n");
}
void osd_ym2203_write(int n, int r, int v) {
  //myport_Printf("osd_ym2203_write\n");
}
void osd_ym2203_update(void) {
  //myport_Printf("osd_ym2203_update\n");
}
void osd_ym3812_control(int reg) {
  //myport_Printf("osd_ym3812_control\n");
}
void osd_ym3812_write(int data) {
  //myport_Printf("osd_ym3812_write\n");
}

// Modified for VSTCM
int osd_key_pressed(int keycode) {
  int retval = 0;

  switch (keycode) {
    case OSD_KEY_1:
      if (k & MASK_KEY_USER1) retval = 1;
      break;
    case OSD_KEY_2:
      if (k & MASK_KEY_USER2) retval = 1;
      break;
    case OSD_KEY_3:
      if (k & MASK_KEY_USER3) retval = 1;
      break;
    case OSD_KEY_4:
      if (k & MASK_KEY_USER4) retval = 1;
      break;
    case OSD_KEY_5:
      if (k & MASK_KEY_USER5) retval = 1;
      break;
      /*  case OSD_KEY_UP:
      if ((k & MASK_JOY1_UP) || (k & MASK_JOY2_UP)) retval = 1;
      break;
    case OSD_KEY_DOWN:
      if ((k & MASK_JOY1_DOWN) || (k & MASK_JOY2_DOWN)) retval = 1;
      break;
    case OSD_KEY_LEFT:
      if ((k & MASK_JOY1_LEFT) || (k & MASK_JOY2_LEFT)) retval = 1;
      break;
    case OSD_KEY_RIGHT:
      if ((k & MASK_JOY1_RIGHT) || (k & MASK_JOY2_RIGHT)) retval = 1;
      break;
    case OSD_KEY_LCONTROL:
      if (k & MASK_JOY2_BTN) retval = 1;
      break;
    case OSD_KEY_ALT:
      if (k & MASK_KEY_USER3) retval = 1;
      break;*/
  }
  return (retval);
}


// original
/**********************************************************
 Patches for keys/joy
***********************************************************/
/*int osd_key_pressed(int keycode) {
  int retval = 0;

  switch (keycode) {
    case OSD_KEY_3:
      if (k & MASK_KEY_USER1) retval = 1;
      break;
    case OSD_KEY_1:
      if (k & MASK_KEY_USER2) retval = 1;
      break;
    case OSD_KEY_UP:
      if ((k & MASK_JOY1_UP) || (k & MASK_JOY2_UP)) retval = 1;
      break;
    case OSD_KEY_DOWN:
      if ((k & MASK_JOY1_DOWN) || (k & MASK_JOY2_DOWN)) retval = 1;
      break;
    case OSD_KEY_LEFT:
      if ((k & MASK_JOY1_LEFT) || (k & MASK_JOY2_LEFT)) retval = 1;
      break;
    case OSD_KEY_RIGHT:
      if ((k & MASK_JOY1_RIGHT) || (k & MASK_JOY2_RIGHT)) retval = 1;
      break;
    case OSD_KEY_LCONTROL:
      if (k & MASK_JOY2_BTN) retval = 1;
      break;
    case OSD_KEY_ALT:
      if (k & MASK_KEY_USER3) retval = 1;
      break;
  }
  return (retval);
}

*/
void osd_poll_joystick(void) {
  //myport_Printf("osd_poll_joystick\n");
}

int osd_joy_pressed(int joycode) {
  //myport_Printf("osd_joy_pressed %d\n", joycode);
  int retval = 0;

  return (retval);
}

void osd_trak_read(int *deltax, int *deltay) {
  //myport_Printf("osd_trak_read\n");
}

/* return values in the range -128 .. 128 (yes, 128, not 127) */
void osd_analogjoy_read(int *analog_x, int *analog_y) {
  //myport_Printf("osd_analogjoy_read\n");
}



void osd_led_w(int led, int on) {
  //myport_Printf("osd_led_w\n");
}



/**********************************************************
 Patches for file io
***********************************************************/
typedef enum {
  kPlainFile,
  kRAMFile,
  kZippedFile
} eFileType;

typedef struct
{
  FILE *file;
  unsigned char *data;
  unsigned int offset;
  unsigned int length;
  eFileType type;
  unsigned int crc;
} FakeFileHandle;

//#include "unzip.h"


int osd_faccess(const char *newfilename, int filetype) {
  return 1;
}

static FakeFileHandle fh;

void *osd_fopen(const char *game, const char *filename, int filetype, int _write) {
  char name[100];
  char *gamename;

  int indx;
  FakeFileHandle *f = (FakeFileHandle *)&fh;  //malloc(sizeof(FakeFileHandle));
  if (f == 0) return f;
  f->type = kPlainFile;
  f->file = 0;

  gamename = (char *)game;
  int found = 0;

  /* Support "-romdir" yuck. */
  switch (filetype) {
    case OSD_FILETYPE_ROM:
    case OSD_FILETYPE_SAMPLE:
      if (!found) {
        sprintf(name, "%s/%s", gamename, filename);
        myport_Printf("loading file %s\n", name);
        f->file = myport_FileOpen(name, _write ? "wb" : "rb");
        if (f->file == 0) {
          myport_Printf("loaded file %s failed\n", name);
        } else {
          found = 1;
        }
      }
      if (!found) {
        /* try with a .zip extension */
        /*
        sprintf(name,"%s/%s.zip", dirname, gamename);
        myport_Printf("loading ZIP file %s\n",name);              
        if (load_zipped_file(name, filename, &f->data, &f->length)==0) {
          f->type = kZippedFile;
          f->offset = 0;
          f->crc = crc32 (0L, f->data, f->length);
          found = 1;
          myport_Printf("loaded ZIP file, crc is %u\n",f->crc); 
        }
        else {
          myport_Printf("loaded ZIP file %s failed\n",name);              
        }
        */
      }
      break;
    case OSD_FILETYPE_HIGHSCORE:
    case OSD_FILETYPE_CONFIG:
    case OSD_FILETYPE_INPUTLOG:
    default:
      f->file = 0;
      break;
  }

  if (!found) {
    //  free(f);
    return 0;
  }

  return f;
}

/* JB 980920 */
int osd_fsize(void *file) {
  FakeFileHandle *f = (FakeFileHandle *)file;

  if (f->type == kRAMFile || f->type == kZippedFile)
    return f->length;

  return 0;
}

unsigned int osd_fcrc(void *file) {
  FakeFileHandle *f = (FakeFileHandle *)file;
  return f->crc;
}



int osd_fread(void *file, void *buffer, int length) {
  FakeFileHandle *f = (FakeFileHandle *)file;

  switch (f->type) {
    case kPlainFile:
      return myport_FileRead(buffer, 1, length, f->file);
      break;
    case kZippedFile:
      /* reading from the uncompressed image of a zipped file */
      if (f->data) {
        if (length + f->offset > f->length)
          length = f->length - f->offset;
        memcpy(buffer, f->offset + f->data, length);
        f->offset += length;
        return length;
      }
      break;
  }

  return 0;
}


int osd_fwrite(void *file, const void *buffer, int length) {
  return 0;
}


int osd_fseek(void *file, int offset, int whence) {
  FakeFileHandle *f = (FakeFileHandle *)file;
  int err = 0;

  switch (f->type) {
    case kPlainFile:
      return myport_FileSeek(((FakeFileHandle *)file)->file, offset, whence);
      break;
    case kZippedFile:
      /* seeking within the uncompressed image of a zipped file */
      switch (whence) {
        case SEEK_SET:
          f->offset = offset;
          break;
        case SEEK_CUR:
          f->offset += offset;
          break;
        case SEEK_END:
          f->offset = f->length + offset;
          break;
      }
      break;
  }

  return err;
}

void osd_fclose(void *file) {
  FakeFileHandle *f = (FakeFileHandle *)file;

  switch (f->type) {
    case kPlainFile:
      myport_FileClose(f->file);
      break;
    case kZippedFile:
      if (f->data)
        free(f->data);
      break;
  }
  //free(f);
}


/**********************************************************
 Patches for profiler
***********************************************************/
void osd_profiler(int type) {
}



/**********************************************************
 Patches for sound
***********************************************************/

int stream_init(const char *name, int sample_rate, int sample_bits,
                int param, void (*callback)(int param, void *buffer, int length)) {
  return 1;
}
int stream_init_multi(int channels, const char **name, int sample_rate, int sample_bits,
                      int param, void (*callback)(int param, void **buffer, int length)) {
  return 1;
}

void stream_set_volume(int channel, int volume) {
}
void stream_set_pan(int channel, int pan) {
}

void set_RC_filter(int channel, int R1, int R2, int R3, int C) {
}
void stream_update(int channel, int min_interval) {
}
// VSTCM test data to play some kind of sound
static int notedelay = 0;
static int notes[] = {
  440,
  466,
  494,
  523,
  554,
  587,
  622,
  659,
  698,
  740,
  784,
  831,
  880
};
static int note_pos;


typedef struct {
  int length;
  int smpfreq;
  unsigned char resolution;
  unsigned char volume;
  signed char data[100]; /* extendable was originally 1 */
} GameSampleX;

typedef struct
{
  int total;              /* total number of samples */
  GameSampleX *sample[4]; /* extendable was originally 1 */
} GameSamplesX;

void sample_start(int channel, int samplenum, int loop) {
  emu_printf("sample_start (mameport.c) being called to play a sound sample");

  notedelay = 1;

  // notedelay += 1;
   notedelay &= 0x07;

  //int note = notes[note_pos];
  // int note = notes[samplenum];

 /*GameSamplesX *gs;

  gs = Machine->samples;

  emu_printf("playing sample of length");
  emu_printi(gs->sample[samplenum]->length);
  int mynote;

//  for (int i = 0; i < gs->sample[samplenum]->length; i++) {
    mynote = gs->sample[samplenum]->data[note_pos];
    note_pos ++;
    if (note_pos >= gs->sample[samplenum]->length)
      emu_sndPlayStop();

    //  emu_printi(mynote);
   // emu_sndPlaySound(channel, notedelay << 4, mynote);  // channel should be 1 probably
    emu_sndPlaySound(channel, notedelay , mynote);  // channel should be 1 probably
 // }*/
}


void sample_stop(int channel) {
  // Added for VSTCM
  emu_sndPlayStop();
}

void sample_adjust(int channel, int freq, int volume) {
}

int sample_playing(int channel) {
  return 0;
}



/**********************************************************
 Main emu methods
***********************************************************/
void mam_Init(void) {
}

void mam_Start(char *game) {
  int i, game_index;

  emu_printf("mam_Start ");
  emu_printf(game);

  game_index = -1;
  char *p = game;
  for (; *p; ++p) *p = tolower(*p);
  emu_printf(game);
  for (i = 0; (drivers[i] && (game_index == -1)); i++) {
    if (strcmp(game, drivers[i]->name) == 0) {
      game_index = i;
      emu_printf("found game");
      //myport_Printf("Game \"%s\" found at position \"%i\".\n", game, game_index);
      break;
    }
  }

  if (game_index == -1) {
    emu_printf("NOT found game");
    //myport_Printf("Game \"%s\" not supported\n", game);
    return;
  }

  options.errorlog = 0;
  options.frameskip = 1;
  options.ror = 0;
  options.rol = 0;
  options.norotate = 0;
  options.flipx = 0;
  options.flipy = 0;
  options.samplerate = 0;  //11025;
  options.samplebits = 8;
  options.cheat = 0;
  options.mame_debug = 0;

  if (!start_game(game_index, &options)) {
    emu_printf("Mame initialized");
  }
}

void mam_Step(void) {
  step_game();
}

int mam_RomValid(char *filename) {
  int found = 0;
  int i;

  char *p = filename;
  for (; *p; ++p) *p = tolower(*p);
  for (i = 0; (drivers[i] && (found == 0)); i++) {
    if (strcmp(filename, drivers[i]->name) == 0) {
      found = 1;
      break;
    }
  }
  return found;
}


void SND_Process(void *stream, int len) {
  audio_play_sample(stream, 0, len);
}