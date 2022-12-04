#ifndef EMUAPI_H
#define EMUAPI_H

//#define INVX        1
//#define INVY        1
#define HAS_SND     1
//#define CUSTOM_SND  1
//#define HAS_I2CKBD  1
//#define TIMER_REND  1

#define EXTRA_HEAP  0x16000

// Title:     <                                        >
#define TITLE "             MAME Emulator              "
#define ROMSDIR "/roms"

//#define emu_Init(ROM) {mam_Init(); mam_Start(ROM);}
//#define emu_Step(x) {mam_Step();}
#define emu_Input(x) {mam_Input(x);}

#define PALETTE_SIZE         256
#define VID_FRAME_SKIP       0x0
//#define TFT_VBUFFER_YCROP    0
#define SINGLELINE_RENDERING 1

#define R32(rgb) ((rgb>>16)&0xff) 
#define G32(rgb) ((rgb>>8)&0xff) 
#define B32(rgb) (rgb & 0xff) 

#define ACTION_NONE          0
#define ACTION_MAXKBDVAL     16
#define ACTION_EXITKBD       128
#define ACTION_RUNTFT        129
#define ACTION_RUNVGA        130

#ifdef KEYMAP_PRESENT

#define TAREA_W_DEF          32
#define TAREA_H_DEF          32
#define TAREA_END            255
#define TAREA_NEW_ROW        254
#define TAREA_NEW_COL        253
#define TAREA_XY             252
#define TAREA_WH             251

#define KEYBOARD_X           16
#define KEYBOARD_Y           32
#define KEYBOARD_KEY_H       30
#define KEYBOARD_KEY_W       28
#define KEYBOARD_HIT_COLOR   RGBVAL16(0xff,0x00,0x00)

const unsigned short keysw[]=
{ TAREA_NEW_ROW,18,18,18,18,18,18,18,18,18,18,  
  TAREA_NEW_ROW,18,18,18,18,18,18,18,18,18,18, 
  TAREA_NEW_ROW,18,18,18,18,18,18,18,18,18,18, 
  TAREA_NEW_ROW,18,18,18,18,18,18,18,18,18,18, 
  //TAREA_NEW_ROW,18,18,18,18,18,18,18,18,18,18, 
  TAREA_END}; 

const unsigned short keys[]={

59, 60, 61, 62, 63, 64, 65, 66, 67, 68,
109,110,111,112,106,107,108,17,18,19,
20,21,22,23,24,25,26,27,28,29,
30,31,32,33,34,35,36,37,38,57   };




#ifdef HAS_I2CKBD
const unsigned short i2ckeys[] = {
     0X0080,0X0008,0X0180,0X0108,0X0280,0X0208,0X0380,0X0308,0X0480,0X0408,
     0X0040,0X0004,0X0140,0X0104,0X0240,0X0204,0X0340,0X0304,0X0440,0X0404,
     0X0020,0X0002,0X0120,0X0102,0X0220,0X0202,0X0320,0X0302,0X0420,0X0402,
     0X0010,0X0001,0X0110,0X0101,0X0210,0X0201,0X0310,0X0301,0X0410,0X0401 };
     //0X0580,0X0540,0X0520,0X0510,0X0508,0X0504,0X0502,0X0501,0X0680,0X0640 };
#endif
   
#endif

// Modified to use VSTCM PCB buttons as game controls

#define MASK_JOY2_RIGHT 0x0001
#define MASK_JOY2_LEFT  0x0002
#define MASK_JOY2_UP    0x0004
#define MASK_JOY2_DOWN  0x0008
#define MASK_JOY2_BTN   0x0010
#define MASK_KEY_USER1  0x0020
#define MASK_KEY_USER2  0x0040
#define MASK_KEY_USER3  0x0080

#define MASK_KEY_USER4  0x0100
#define MASK_KEY_USER5  0x0200

//#define MASK_JOY1_RIGHT 0x0100
//#define MASK_JOY1_LEFT  0x0200
#define MASK_JOY1_UP    0x0400
#define MASK_JOY1_DOWN  0x0800
#define MASK_JOY1_BTN   0x1000

#ifdef __cplusplus  
extern "C" {
#endif
extern void emu_init(void);
extern void emu_printf(char * text);
extern void emu_printi(int val);
extern void * emu_Malloc(int size);
extern void emu_Free(void * pt);

 
extern int emu_FileOpen(char * filename);
extern int emu_FileRead(char * buf, int size);
extern unsigned char emu_FileGetc(void);
extern int emu_FileTell(void);
extern int emu_FileSeek(int seek);
extern void emu_FileClose(void);
extern int emu_FileSize(char * filename);
extern int emu_LoadFile(char * filename, char * buf, int size);
extern int emu_LoadFileSeek(char * filename, char * buf, int size, int seek);
extern void emu_SetPaletteEntry(unsigned char r, unsigned char g, unsigned char b, int index);
extern void emu_DrawScreen(unsigned char * VBuf, int width, int height, int stride);
extern void emu_DrawLine(unsigned char * VBuf, int width, int height, int line);
extern void emu_DrawLine16(unsigned short * VBuf, int width, int height, int line);
extern void emu_DrawVsync(void);
extern int emu_FrameSkip(void);
extern void * emu_LineBuffer(int line);

extern void emu_InitJoysticks(void);
extern int emu_SwapJoysticks(int statusOnly);
extern unsigned short emu_DebounceLocalKeys(void);
extern int emu_ReadKeys(void);
extern int emu_GetPad(void);
extern int emu_ReadAnalogJoyX(int min, int max);
extern int emu_ReadAnalogJoyY(int min, int max);
extern int emu_ReadI2CKeyboard(void);
extern void emu_sndPlaySound(int chan, int volume, int freq);
extern void emu_sndPlayStop(void);    // Added for VSTCM
extern void emu_sndPlayBuzz(int size, int val);
extern void emu_sndInit();
extern void emu_resetus(void);
extern int emu_us(void);

extern int emu_setKeymap(int index);

#ifdef __cplusplus
}
#endif

#endif




