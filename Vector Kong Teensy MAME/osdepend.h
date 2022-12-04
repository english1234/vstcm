#ifndef OSDEPEND_H
#define OSDEPEND_H

/** suggested by  Scott Trent */
#ifdef aix
#include <sys/time.h>
#endif

#ifdef linux_alpha
#define FPTR long   /* 64bit: sizeof(void *) is sizeof(long)  */
#else
#define FPTR int
#endif

struct osd_bitmap
{
	int width,height;       /* width and height of the bitmap */
	int depth;		/* bits per pixel - ASG 980209 */
	void *_private; /* don't touch! - reserved for osdepend use */
	unsigned char **line; /* pointers to the start of each line */
};


#define OSD_KEY_NONE		0
#define OSD_KEY_ESC         1        /* keyboard scan codes */
#define OSD_KEY_1           2        /* (courtesy of allegro.h) */
#define OSD_KEY_2           3
#define OSD_KEY_3           4
#define OSD_KEY_4           5
#define OSD_KEY_5           6
#define OSD_KEY_6           7
#define OSD_KEY_7           8
#define OSD_KEY_8           9
#define OSD_KEY_9           10
#define OSD_KEY_0           11
#define OSD_KEY_MINUS       12
#define OSD_KEY_EQUALS      13
#define OSD_KEY_BACKSPACE   14
#define OSD_KEY_TAB         15
#define OSD_KEY_Q           16
#define OSD_KEY_W           17
#define OSD_KEY_E           18
#define OSD_KEY_R           19
#define OSD_KEY_T           20
#define OSD_KEY_Y           21
#define OSD_KEY_U           22
#define OSD_KEY_I           23
#define OSD_KEY_O           24
#define OSD_KEY_P           25
#define OSD_KEY_OPENBRACE   26
#define OSD_KEY_CLOSEBRACE  27
#define OSD_KEY_ENTER       28
#define OSD_KEY_LCONTROL    29
#define OSD_KEY_A           30
#define OSD_KEY_S           31
#define OSD_KEY_D           32
#define OSD_KEY_F           33
#define OSD_KEY_G           34
#define OSD_KEY_H           35
#define OSD_KEY_J           36
#define OSD_KEY_K           37
#define OSD_KEY_L           38
#define OSD_KEY_COLON       39
#define OSD_KEY_QUOTE       40
#define OSD_KEY_TILDE       41
#define OSD_KEY_LSHIFT      42
/* 43 */
#define OSD_KEY_Z           44
#define OSD_KEY_X           45
#define OSD_KEY_C           46
#define OSD_KEY_V           47
#define OSD_KEY_B           48
#define OSD_KEY_N           49
#define OSD_KEY_M           50
#define OSD_KEY_COMMA       51
#define OSD_KEY_STOP        52
#define OSD_KEY_SLASH       53
#define OSD_KEY_RSHIFT      54
#define OSD_KEY_ASTERISK    55
#define OSD_KEY_ALT         56
#define OSD_KEY_SPACE       57
#define OSD_KEY_CAPSLOCK    58
#define OSD_KEY_F1          59
#define OSD_KEY_F2          60
#define OSD_KEY_F3          61
#define OSD_KEY_F4          62
#define OSD_KEY_F5          63
#define OSD_KEY_F6          64
#define OSD_KEY_F7          65
#define OSD_KEY_F8          66
#define OSD_KEY_F9          67
#define OSD_KEY_F10         68
#define OSD_KEY_NUMLOCK     69
#define OSD_KEY_SCRLOCK     70
#define OSD_KEY_HOME        71
#define OSD_KEY_UP          72
#define OSD_KEY_PGUP        73
#define OSD_KEY_MINUS_PAD   74
#define OSD_KEY_LEFT        75
#define OSD_KEY_5_PAD       76
#define OSD_KEY_RIGHT       77
#define OSD_KEY_PLUS_PAD    78
#define OSD_KEY_END         79
#define OSD_KEY_DOWN        80
#define OSD_KEY_PGDN        81
#define OSD_KEY_INSERT      82
#define OSD_KEY_DEL         83
#define OSD_KEY_RCONTROL    84  /* different from Allegro */
#define OSD_KEY_ALTGR       85  /* different from Allegro */
/* 86 */
#define OSD_KEY_F11         87
#define OSD_KEY_F12         88
#define OSD_KEY_COMMAND     89
#define OSD_KEY_OPTION      90
/* 91 - 100 */
/* The following are all undefined in Allegro */
#define OSD_KEY_1_PAD		101
#define OSD_KEY_2_PAD		102
#define OSD_KEY_3_PAD		103
#define OSD_KEY_4_PAD		104
/* 105 */
#define OSD_KEY_6_PAD		106
#define OSD_KEY_7_PAD		107
#define OSD_KEY_8_PAD		108
#define OSD_KEY_9_PAD		109
#define OSD_KEY_0_PAD		110
#define OSD_KEY_STOP_PAD	111
#define OSD_KEY_EQUALS_PAD	112
#define OSD_KEY_SLASH_PAD	113
#define OSD_KEY_ASTER_PAD	114
#define OSD_KEY_ENTER_PAD	115

#define OSD_MAX_KEY         115

/* 116 - 119 */

/* The following are defined in Allegro */
/* 120 KEY_RCONTROL */
/* 121 KEY_ALTGR */
/* 122 KEY_SLASH2 */
/* 123 KEY_PAUSE */

/*
 * ASG 980730: these are pseudo-keys that the os-dependent code can
 * map to whatever they see fit
 * HJB 980812: added some more names and used higher values because
 * there were some clashes with Allegro's scancodes (see above)
 */
#define OSD_KEY_FAST_EXIT			128
#define OSD_KEY_CANCEL				129
#define OSD_KEY_RESET_MACHINE		130
#define OSD_KEY_PAUSE				131
#define OSD_KEY_UNPAUSE 			132
#define OSD_KEY_CONFIGURE			133
#define OSD_KEY_ON_SCREEN_DISPLAY	134
#define OSD_KEY_SHOW_GFX			135
#define OSD_KEY_JOY_CALIBRATE		136
#define OSD_KEY_FRAMESKIP			137
#define OSD_KEY_THROTTLE			138
#define OSD_KEY_SHOW_FPS			139
#define OSD_KEY_SHOW_PROFILE		140
#define OSD_KEY_SNAPSHOT			141
#define OSD_KEY_CHEAT_TOGGLE		142
#define OSD_KEY_DEBUGGER			143

#define OSD_MAX_PSEUDO				143

#define OSD_JOY_LEFT    1
#define OSD_JOY_RIGHT   2
#define OSD_JOY_UP      3
#define OSD_JOY_DOWN    4
#define OSD_JOY_FIRE1   5
#define OSD_JOY_FIRE2   6
#define OSD_JOY_FIRE3   7
#define OSD_JOY_FIRE4   8
#define OSD_JOY_FIRE5   9
#define OSD_JOY_FIRE6   10
#define OSD_JOY_FIRE7   11
#define OSD_JOY_FIRE8   12
#define OSD_JOY_FIRE9   13
#define OSD_JOY_FIRE10  14
#define OSD_JOY_FIRE    15      /* any of the first joystick fire buttons */
#define OSD_JOY2_LEFT   16
#define OSD_JOY2_RIGHT  17
#define OSD_JOY2_UP     18
#define OSD_JOY2_DOWN   19
#define OSD_JOY2_FIRE1  20
#define OSD_JOY2_FIRE2  21
#define OSD_JOY2_FIRE3  22
#define OSD_JOY2_FIRE4  23
#define OSD_JOY2_FIRE5  24
#define OSD_JOY2_FIRE6  25
#define OSD_JOY2_FIRE7  26
#define OSD_JOY2_FIRE8  27
#define OSD_JOY2_FIRE9  28
#define OSD_JOY2_FIRE10 29
#define OSD_JOY2_FIRE   30      /* any of the second joystick fire buttons */
#define OSD_JOY3_LEFT   31
#define OSD_JOY3_RIGHT  32
#define OSD_JOY3_UP     33
#define OSD_JOY3_DOWN   34
#define OSD_JOY3_FIRE1  35
#define OSD_JOY3_FIRE2  36
#define OSD_JOY3_FIRE3  37
#define OSD_JOY3_FIRE4  38
#define OSD_JOY3_FIRE5  39
#define OSD_JOY3_FIRE6  40
#define OSD_JOY3_FIRE7  41
#define OSD_JOY3_FIRE8  42
#define OSD_JOY3_FIRE9  43
#define OSD_JOY3_FIRE10 44
#define OSD_JOY3_FIRE   45      /* any of the third joystick fire buttons */
#define OSD_JOY4_LEFT   46
#define OSD_JOY4_RIGHT  47
#define OSD_JOY4_UP     48
#define OSD_JOY4_DOWN   49
#define OSD_JOY4_FIRE1  50
#define OSD_JOY4_FIRE2  51
#define OSD_JOY4_FIRE3  52
#define OSD_JOY4_FIRE4  53
#define OSD_JOY4_FIRE5  54
#define OSD_JOY4_FIRE6  55
#define OSD_JOY4_FIRE7  56
#define OSD_JOY4_FIRE8  57
#define OSD_JOY4_FIRE9  58
#define OSD_JOY4_FIRE10 59
#define OSD_JOY4_FIRE   60      /* any of the fourth joystick fire buttons */
#define OSD_MAX_JOY     60

#define X_AXIS          1
#define Y_AXIS          2

extern int video_sync;


int osd_init(void);
void osd_exit(void);
/* VERY IMPORTANT: the function must allocate also a "safety area" 8 pixels wide all */
/* around the bitmap. This is required because, for performance reasons, some graphic */
/* routines don't clip at boundaries of the bitmap. */
struct osd_bitmap *osd_new_bitmap(int width,int height,int depth);	/* ASG 980209 */
#define osd_create_bitmap(w,h) osd_new_bitmap((w),(h),8)		/* ASG 980209 */
void osd_clearbitmap(struct osd_bitmap *bitmap);
void osd_free_bitmap(struct osd_bitmap *bitmap);
/* Create a display screen, or window, large enough to accomodate a bitmap */
/* of the given dimensions. Attributes are the ones defined in driver.h. */
/* Return a osd_bitmap pointer or 0 in case of error. */
struct osd_bitmap *osd_create_display(int width,int height,int attributes);
int osd_set_display(int width,int height,int attributes);
void osd_close_display(void);
/* palette is an array of 'totalcolors' R,G,B triplets. The function returns */
/* in *pens the pen values corresponding to the requested colors. */
/* If 'totalcolors' is 32768, 'palette' is ignored and the *pens array is filled */
/* with pen values corresponding to a 5-5-5 15-bit palette */
void osd_allocate_colors(unsigned int totalcolors,const unsigned char *palette,unsigned short *pens);
void osd_modify_pen(int pen,unsigned char red, unsigned char green, unsigned char blue);
void osd_get_pen(int pen,unsigned char *red, unsigned char *green, unsigned char *blue);
void osd_mark_dirty(int xmin, int ymin, int xmax, int ymax, int ui);    /* ASG 971011 */
int osd_skip_this_frame(int recommend);
void osd_update_display(void);
void osd_set_gamma(float _gamma);
float osd_get_gamma(void);
void osd_set_brightness(int brightness);
int osd_get_brightness(void);
void osd_save_snapshot(void);

#define OSD_PAN_CENTER 0
#define OSD_PAN_LEFT   1
#define OSD_PAN_RIGHT  2
void osd_update_audio(void);
void osd_play_sample(int channel,signed char *data,int len,int freq,int volume,int loop);
void osd_play_sample_16(int channel,signed short *data,int len,int freq,int volume,int loop);
void osd_play_streamed_sample(int channel,signed char *data,int len,int freq,int volume,int pan);
void osd_play_streamed_sample_16(int channel,signed short *data,int len,int freq,int volume,int pan);
void osd_adjust_sample(int channel,int freq,int volume);
void osd_stop_sample(int channel);
void osd_restart_sample(int channel);
int osd_get_sample_status(int channel);
void osd_ym2203_write(int n, int r, int v);
void osd_ym2203_update(void);
void osd_ym3812_control(int reg);
void osd_ym3812_write(int data);
void osd_set_mastervolume(int attenuation);
int osd_get_mastervolume(void);
void osd_sound_enable(int enable);

int osd_key_pressed(int keycode);
int osd_key_pressed_memory(int keycode);
int osd_key_pressed_memory_repeat(int keycode,int speed);
int osd_read_key_immediate(void);
/* the following two should return pseudo key codes if translate != 0 */
int osd_read_key(int translate);
int osd_read_keyrepeat(int translate);
int key_to_pseudo_code(int k);
const char *osd_joy_name(int joycode);
const char *osd_key_name(int keycode);
void osd_poll_joystick(void);
int osd_joy_pressed(int joycode);

/* Joystick calibration routines BW 19981216 */
/* Do we need to calibrate the joystick at all? */
int osd_joystick_needs_calibration (void);
/* Preprocessing for joystick calibration. Returns 0 on success */
void osd_joystick_start_calibration (void);
/* Prepare the next calibration step. Return a description of this step. */
/* (e.g. "move to upper left") */
char *osd_joystick_calibrate_next (void);
/* Get the actual joystick calibration data for the current position */
void osd_joystick_calibrate (void);
/* Postprocessing (e.g. saving joystick data to config) */
void osd_joystick_end_calibration (void);


void osd_trak_read(int *deltax,int *deltay);

/* return values in the range -128 .. 128 (yes, 128, not 127) */
void osd_analogjoy_read(int *analog_x, int *analog_y);

/* file handling routines */
#define OSD_FILETYPE_ROM 1
#define OSD_FILETYPE_SAMPLE 2
#define OSD_FILETYPE_HIGHSCORE 3
#define OSD_FILETYPE_CONFIG 4
#define OSD_FILETYPE_INPUTLOG 5
#define OSD_FILETYPE_STATE 6
#define OSD_FILETYPE_ARTWORK 7

/* gamename holds the driver name, filename is only used for ROMs and    */
/* samples. If 'write' is not 0, the file is opened for write. Otherwise */
/* it is opened for read. */

int osd_faccess(const char *filename, int filetype);
void *osd_fopen(const char *gamename,const char *filename,int filetype,int write);
int osd_fread(void *file,void *buffer,int length);
int osd_fwrite(void *file,const void *buffer,int length);
int osd_fread_swap(void *file,void *buffer,int length);
int osd_fwrite_swap(void *file,const void *buffer,int length);
#if LSB_FIRST
#define osd_fread_msbfirst osd_fread_swap
#define osd_fwrite_msbfirst osd_fwrite_swap
#define osd_fread_lsbfirst osd_fread
#define osd_fwrite_lsbfirst osd_fwrite
#else
#define osd_fread_msbfirst osd_fread
#define osd_fwrite_msbfirst osd_fwrite
#define osd_fread_lsbfirst osd_fread_swap
#define osd_fwrite_lsbfirst osd_fwrite_swap
#endif
int osd_fseek(void *file,int offset,int whence);
void osd_fclose(void *file);
int osd_fchecksum(const char *gamename, const char *filename, unsigned int *length, unsigned int *sum);
int osd_fsize(void *file);
unsigned int osd_fcrc(void *file);

/* control keyboard leds or other indicators */
void osd_led_w(int led,int on);

/* config */
void osd_set_config(int def_samplerate, int def_samplebits);
void osd_save_config(int frameskip, int samplerate, int samplebits);
int osd_get_config_samplerate(int def_samplerate);
int osd_get_config_samplebits(int def_samplebits);
int osd_get_config_frameskip(int def_frameskip);

/* profiling */
enum {
	OSD_PROFILE_END = -1,
	OSD_PROFILE_CPU1 = 0,
	OSD_PROFILE_CPU2,
	OSD_PROFILE_CPU3,
	OSD_PROFILE_CPU4,
	OSD_PROFILE_VIDEO,
	OSD_PROFILE_BLIT,
	OSD_PROFILE_SOUND,
	OSD_PROFILE_TIMER_CALLBACK,
	OSD_PROFILE_EXTRA,
	/* the USER types are available to driver writes to profile */
	/* custom sections of the code */
	OSD_PROFILE_USER1,
	OSD_PROFILE_USER2,
	OSD_PROFILE_USER3,
	OSD_PROFILE_USER4,
	OSD_PROFILE_PROFILER,
	OSD_PROFILE_IDLE,
	OSD_TOTAL_PROFILES
};

/*
To start profiling a certain section, e.g. video:
osd_profiler(OSD_PROFILE_VIDEO);
to end profiling of the current section:
osd_profiler(OSD_PROFILE_END);
*/

void osd_profiler(int type);

#ifdef MAME_NET
void osd_net_sync(void);
#endif /* MAME_NET */

#endif
