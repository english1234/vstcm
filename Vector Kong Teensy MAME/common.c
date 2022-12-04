#include "WProgram.h"
/*********************************************************************
  common.c

  Generic functions, mostly ROM and graphics related.
  NO LONGER TRUE! Filled up with VSTCM and Vector Donkey Kong specific 
  routines adapted and/or written by Robin Champion / November 2022
*********************************************************************/

#include <arduino.h>
#include <math.h>
#include "driver.h"
#include "myport.h"

/* These globals are only kept on a machine basis - LBO 042898 */
extern unsigned int dispensed_tickets;
unsigned int coins[COIN_COUNTERS];
unsigned int lastcoin[COIN_COUNTERS];
unsigned int coinlockedout[COIN_COUNTERS];

/* LBO */
#ifdef LSB_FIRST
#define intelLong(x) (x)
#define BL0 0
#define BL1 1
#define BL2 2
#define BL3 3
#define WL0 0
#define WL1 1
#else
#define intelLong(x) (((x << 24) | (((unsigned long)x) >> 24) | ((x & 0x0000ff00) << 8) | ((x & 0x00ff0000) >> 8)))
#define BL0 3
#define BL1 2
#define BL2 1
#define BL3 0
#define WL0 1
#define WL1 0
#endif

#define TA

#define read_dword(address) *(int *)address
#define write_dword(address, data) *(int *)address = data

// VSTCM Vector Kong specific routines

extern void draw_to_xy(int, int);
extern int cpu_readmem16(int address);
extern void cpu_writemem29_dword(int address, int data);
extern void cpu_writemem16(int address, int data);

// Local function prototypes
uint8_t read_u8(int);
uint32_t read_direct_u32(int);

// Basic vector drawing functions
// 12 for X, 15 for Y work well
// add 700 for X, 0 for Y

#define X_SCALE_FACTOR 12
#define Y_SCALE_FACTOR 15
#define X_ADD 700
#define Y_ADD 0

#define GW 9  // girder width, to make it easy to increase if gaps appear on screen between tiles
#define GH 9  // girder width

// Defining region codes
#define LEFT 0x1
#define RIGHT 0x2
#define BOTTOM 0x4
#define TOP 0x8

#define BLK 0xF00
#define WHT 0xF01
#define YEL 0xF02
#define RED 0xF03
#define BLU 0xF04
#define MBR 0xF05
#define BRN 0xF06
#define MAG 0xF07
#define PNK 0xF08
#define LBR 0xF09
#define CYN 0xF0A
#define GRY 0xF0B
#define BRIGHT_BLUE 0xF0C
#define LAST_POINT 0xF0D
#define BR 0xF0E   // instruction to break in a vector chain
#define GRN 0xF0F  // Green (not normally needed)

#define MODE 0x600a
#define STAGE 0x6227
#define LEVEL 0x6229
#define VRAM_BL 0x77bf
#define VRAM_TR 0x7440

typedef struct Chunk {
  struct Chunk *nextChunk;
  uint16_t x0;  // We'll just use 12 bits of X & Y for a 4096 point resolution
  uint16_t y0;
  uint16_t x1;  // We'll just use 12 bits of X & Y for a 4096 point resolution
  uint16_t y1;
  uint8_t r;  // Max value of each colour is 255
  uint8_t g;
  uint8_t b;
} DataChunk_t;

#define MAX_PTS 4000  // Peaks around 2750

static int vector_color;
static int game_mode, last_mode;
static int _growling;
static int frame_count;
static uint16_t s_in_vec_cnt;
static uint16_t s_tmp_vec_cnt;
static float s_in_vec_last_x;
static float s_in_vec_last_y;
static uint32_t s_out_vec_cnt;
static DataChunk_t *s_in_vec_list;
static DataChunk_t *s_out_vec_list;
static DataChunk_t *s_tmp_vec_list;
static int32_t s_clipx_min = 1;
static int32_t s_clipx_max = 4095;
static int32_t s_clipy_min = 1;
static int32_t s_clipy_max = 4095;

typedef struct {
  const uint16_t points[270];  // maximum 270 points per vector
} dk_sprite_t;

// Vector library
dk_sprite_t vector_library[] = {
  [0x00] = { 0, 2, 0, 4, 2, 6, 4, 6, 6, 4, 6, 2, 4, 0, 2, 0, 0, 2, LAST_POINT },                            // 0                                                                                                                                                                                                                                                                              // 0
  [0x01] = { 0, 0, 0, 6, 0, 3, 6, 3, 5, 1, LAST_POINT },                                                    // 1 optimised without breaks                                                                                                                                                                                                                                                                                                   // 1                                                                                                                                                                                                                                                                              // 1
  [0x02] = { 0, 6, 0, 0, 5, 6, 6, 3, 5, 0, LAST_POINT },                                                    // 2                                                                                                                                                                                                                                                                              // 2
  [0x03] = { 1, 0, 0, 1, 0, 5, 1, 6, 2, 6, 3, 5, 3, 2, 6, 6, 6, 1, LAST_POINT },                            // 3                                                                                                                                                                                                                                                                              // 3
  [0x04] = { 0, 5, 6, 5, 2, 0, 2, 7, LAST_POINT },                                                          // 4                                                                                                                                                                                                                                                                              // 4
  [0x05] = { 1, 0, 0, 1, 0, 5, 2, 6, 4, 5, 4, 0, 6, 0, 6, 5, LAST_POINT },                                  // 5                                                                                                                                                                                                                                                                              // 5
  [0x06] = { 3, 0, 1, 0, 0, 1, 0, 5, 1, 6, 2, 6, 3, 5, 3, 0, 6, 2, 6, 5, LAST_POINT },                      // 6                                                                                                                                                                                                                                                                              // 6
  [0x07] = { 6, 0, 6, 6, 0, 2, LAST_POINT },                                                                // 7                                                                                                                                                                                                                                                                              // 7
  [0x08] = { 2, 0, 0, 1, 0, 5, 2, 6, 5, 0, 6, 1, 6, 4, 5, 5, 2, 0, LAST_POINT },                            // 8                                                                                                                                                                                                                                                                              // 8
  [0x09] = { 0, 1, 0, 4, 2, 6, 5, 6, 6, 5, 6, 1, 5, 0, 4, 0, 3, 1, 3, 6, LAST_POINT },                      // 9                                                                                                                                                                                                                                                                              // 9
  [0x10] = { LAST_POINT },                                                                                  // space                                                                                                                                                                                                                                                                                                                                   // space
  [0x11] = { 0, 0, 4, 0, 6, 3, 4, 6, 0, 6, 2, 6, 2, 0, LAST_POINT },                                        // A                                                                                                                                                                                                                                                                                    // A
  [0x12] = { 0, 0, 6, 0, 6, 5, 5, 6, 4, 6, 3, 5, 2, 6, 1, 6, 0, 5, 0, 0, BR, BR, 3, 0, 3, 5, LAST_POINT },  // B
  [0x13] = { 1, 6, 0, 5, 0, 2, 2, 0, 4, 0, 6, 2, 6, 5, 5, 6, LAST_POINT },                                  // C
  [0x14] = { 0, 0, 6, 0, 6, 4, 4, 6, 2, 6, 0, 4, 0, 0, LAST_POINT },                                        // D
  [0x15] = { 0, 5, 0, 0, 3, 0, 3, 4, 3, 0, 6, 0, 6, 5, LAST_POINT },                                        // E                                                                                                                                                                                                                                                                                     // E optimised with no breaks                                                                                                                                                                                                                                                                                      // E
  [0x16] = { 0, 0, 3, 0, 3, 5, 3, 0, 6, 0, 6, 6, LAST_POINT },                                              // F optimised without breaks
  [0x17] = { 3, 4, 3, 6, 0, 6, 0, 2, 2, 0, 4, 0, 6, 2, 6, 6, LAST_POINT },                                  // G
  [0x18] = { 0, 0, 6, 0, 3, 0, 3, 6, 0, 6, 6, 6, LAST_POINT },                                              // H optimised without breaks                                                                                                                                                                                                                                                                                     // H
  [0x19] = { 0, 0, 0, 6, 0, 3, 6, 3, 6, 0, 6, 6, LAST_POINT },                                              // I optimised to remove break                                                                                                                                                                                                                                                                                // I
  [0x1a] = { 1, 0, 0, 1, 0, 5, 1, 6, 6, 6, LAST_POINT },                                                    // J
  [0x1b] = { 0, 6, 3, 0, 6, 6, 3, 0, 0, 0, 6, 0, LAST_POINT },                                              // K optimised without breaks                                                                                                                                                                                                                                                                           // K
  [0x1c] = { 6, 0, 0, 0, 0, 5, LAST_POINT },                                                                // L
  [0x1d] = { 0, 0, 6, 0, 2, 3, 6, 6, 0, 6, LAST_POINT },                                                    // M
  [0x1e] = { 0, 0, 6, 0, 0, 6, 6, 6, LAST_POINT },                                                          // N
  [0x1f] = { 1, 0, 5, 0, 6, 1, 6, 5, 5, 6, 1, 6, 0, 5, 0, 1, 1, 0, LAST_POINT },                            // O
  [0x20] = { 0, 0, 6, 0, 6, 5, 5, 6, 3, 6, 2, 5, 2, 0, LAST_POINT },                                        // P
  [0x21] = { 1, 0, 5, 0, 6, 1, 6, 5, 5, 6, 2, 6, 0, 4, 0, 1, 1, 0, BR, BR, 0, 6, 2, 3, LAST_POINT },        // Q
  [0x22] = { 0, 0, 6, 0, 6, 5, 5, 6, 4, 6, 2, 3, 2, 0, 2, 3, 0, 6, LAST_POINT },                            // R
  [0x23] = { 1, 0, 0, 1, 0, 5, 1, 6, 2, 6, 4, 0, 5, 0, 6, 1, 6, 4, 5, 5, LAST_POINT },                      // S
  [0x24] = { 6, 0, 6, 6, 6, 3, 0, 3, LAST_POINT },                                                          // T optimised to remove break                                                                                                                                                                                                                                                                                                           // T
  [0x25] = { 6, 0, 1, 0, 0, 1, 0, 5, 1, 6, 6, 6, LAST_POINT },                                              // U
  [0x26] = { 6, 0, 3, 0, 0, 3, 3, 6, 6, 6, LAST_POINT },                                                    // V
  [0x27] = { 6, 0, 2, 0, 0, 1, 4, 3, 0, 5, 2, 6, 6, 6, LAST_POINT },                                        // W
  [0x28] = { 0, 0, 6, 6, 3, 3, 6, 0, 0, 6, LAST_POINT },                                                    // X
  [0x29] = { 6, 0, 3, 3, 6, 6, 3, 3, 0, 3, LAST_POINT },                                                    // Y optimised to remove break
  [0x2a] = { 6, 0, 6, 6, 0, 0, 0, 6, LAST_POINT },                                                          // Z
  [0x2b] = { 0, 0, 1, 0, 1, 1, 0, 1, 0, 0, LAST_POINT },                                                    // dot
  [0x2c] = { 3, 0, 3, 5, LAST_POINT },                                                                      // dash
  [0x2d] = { 5, 0, 5, 6, LAST_POINT },                                                                      // underscore
  [0x2e] = { 4, 3, 4, 3, BR, BR, 2, 3, 2, 3, LAST_POINT },                                                  // colon
  [0x2f] = { 5, 0, 5, 6, LAST_POINT },                                                                      // Alt underscore
  [0x30] = { 0, 2, 2, 0, 4, 0, 6, 2, LAST_POINT },                                                          // Left bracket
  [0x31] = { 0, 2, 2, 4, 4, 4, 6, 2, LAST_POINT },                                                          // Right bracket
  [0x32] = { 0, 0, 0, 4, BR, BR, 6, 0, 6, 4, BR, BR, 0, 2, 6, 2, LAST_POINT },                              // I
  [0x33] = { 0, 0, 0, 6, BR, BR, 6, 0, 6, 6, BR, BR, 0, 1, 6, 1, BR, BR, 0, 4, 6, 4, LAST_POINT },          // II
  [0x34] = { 2, 0, 2, 5, BR, BR, 4, 0, 4, 5, LAST_POINT },                                                  // equals
  [0x35] = { 3, 0, 3, 5, LAST_POINT },                                                                      // dash
  [0x36] = { 0, 2, 2, 2, BR, BR, 4, 2, 6, 2, BR, BR, 0, 5, 2, 5, BR, BR, 4, 5, 6, 5 },                      // !!
  [0x37] = { 0, 2, 2, 2, BR, BR, 4, 2, 6, 2, BR, BR, 0, 5, 2, 5, BR, BR, 4, 5, 6, 5 },                      // !! different style
  [0x38] = { 0, 3, 2, 3, BR, BR, 4, 3, 6, 3 },                                                              // !
  [0x39] = { 0, 3, 2, 3, BR, BR, 4, 3, 6, 3 },                                                              // ! different style
  [0x3A] = { 3, 0, 6, 0, LAST_POINT },                                                                      // '
  [0x3B] = { 3, 2, 6, 2, BR, BR, 4, 4, 6, 4, LAST_POINT },                                                  // "
  [0x3C] = { 3, 2, 6, 2, BR, BR, 4, 4, 6, 4, LAST_POINT },                                                  // "
  [0x3D] = { 3, 2, 6, 2, BR, BR, 4, 4, 6, 4, LAST_POINT },                                                  // " (should be skinny quote marks)
  [0x3E] = { 0, 0, 7, 0, 7, 7, LAST_POINT },                                                                // L shape (right, bottom)
  [0x3F] = { 7, 0, 7, 7, 0, 7, LAST_POINT },                                                                // L shape, (right, top)
  [0x40] = { 7, 0, 0, 0, 0, 7, LAST_POINT },                                                                // L shape
  [0x41] = { 0, 0, 7, 0, 7, 7, LAST_POINT },                                                                // L shape, (left, top)
  [0x42] = { 2, 2, LAST_POINT },                                                                            // 42 is dot
  [0x43] = { 2, 2, 0, 0, LAST_POINT },                                                                      // 43 is comma
  [0x44] = { 0, 5, 4, 5, 4, 7, 2, 7, 0, 8, BR, BR, 2, 5, 2, 7, BR, BR, 4, 10, 1, 10, 0, 11, 0, 12, 1, 13, 4, 13, BR,
             BR, 0, 15, 4, 15, 4, 17, 2, 17, 2, 18, 0, 18, 0, 15, BR, BR, 2, 15, 2, 17, BR, BR, 0, 23,
             0, 21, 4, 21, 4, 23, BR, BR, 2, 21, 2, 22, BR, BR, 0, 25, 4, 25, 0, 28, 4, 28, BR, BR, 0,
             30, 4, 30, 4, 32, 3, 33, 1, 33, 0, 32, 0, 30, LAST_POINT },  // 44 - 48 RUB END graphic
  [0x45] = { LAST_POINT },
  [0x46] = { LAST_POINT },
  [0x47] = { LAST_POINT },
  [0x48] = { LAST_POINT },
  [0x49] = { 0, 4, 2, 2, 5, 2, 7, 4, 7, 8, 5, 10, 2, 10, 0, 8, 0, 4, BR, BR, 2, 7, 2, 5, 5, 5, 5, 7, LAST_POINT },
  [0x4a] = { LAST_POINT },  // 4A copyright logo
  [0x4b] = { LAST_POINT },  // 4B, 4C = some logo?
  [0x4c] = { LAST_POINT },
  // 4D, - 4F = solid blocks of various colors
  [0x4D] = { BR, RED, 0, 0, 0, 7, 7, 7, 7, 0, 0, 0, LAST_POINT },  // attempt at a solid block (empty square for vector)
  [0x4E] = { BR, GRN, 0, 0, 0, 7, 7, 7, 7, 0, 0, 0, LAST_POINT },  // attempt at a solid block (empty square for vector)
  [0x4F] = { BR, BLU, 0, 0, 0, 7, 7, 7, 7, 0, 0, 0, LAST_POINT },  // attempt at a solid block (empty square for vector)
                                                                   // 50 = 67 = kong graphics
                                                                   //		["dk-front"]
  [0x50] = { 31, 20, 31, 17, 27, 13, 25, 13, 25, 15, 28, 15, 29, 16, 30, 18, 30, 19, 29, 20, BR, BR, 25, 20,
             25, 18, 24, 18, 24, 20, BR, BR, 21, 15, 22, 16, 22, 20, BR, BR, 26, 18, 27, 18, 27, 19, 26, 19,
             26, 18, BR, BR, 6, 20, 6, 16, 2, 12, BR, BR, 2, 4, 4, 4, 5, 3, 7, 3, 11, 7, 13, 7, 13, 4, 16, 1,
             19, 1, 23, 6, 24, 8, 24, 10, BR, BR, 7, 15, 8, 14, 10, 14, BR, BR, 19, 6, 17, 10, 16, 13, BR, BR,
             10, 13, 11, 10, BR, MBR, 27, 13, 27, 11, 26, 10, 25, 10, 21, 11, 20, 14, 19, 14, 18, 16, 18, 20,
             BR, BR, 2, 12, 0, 11, 0, 0, 2, 2, 2, 4, BR, BR, 16, 13, 16, 15, 15, 18, 14, 19, 12, 19, 10, 17,
             10, 13, BR, BR, 6, 17, 7, 17, 8, 16, BR, BR, 6, 19, 7, 19, 8, 18, BR, BR, 1, 10, 2, 11, BR, BR,
             1, 5, 2, 6, BR, BR, 28, 17, 28, 19, 26, 19, 26, 17, 28, 17, BR, LBR, 26, 18, 27, 18, 27, 19, 26, 19, 26, 18, LAST_POINT },

  /*  [0x50] = { BR, YEL, 5, 0, 7, 2, 7, 0, 5, 0, BR, RED, 2, 0, 2, 1, 1, 2, 0, 2, 0, 0, 2, 0, LAST_POINT },
  [0x51] = { BR, YEL, 0, 3, 2, 6, 7, 6, LAST_POINT },
  [0x52] = { BR, YEL, 0, 6, 7, 3, LAST_POINT },
  [0x53] = { BR, YEL, 0, 2, 1, 2, 3, 0, LAST_POINT },
  [0x54] = { 0, 6, 3, 0, 6, 6, 3, 0, 0, 0, 6, 0, LAST_POINT },  // letter K
  [0x55] = { 0, 6, 3, 0, 6, 6, 3, 0, 0, 0, 6, 0, LAST_POINT },  // letter K
  [0x56] = { 0, 6, 3, 0, 6, 6, 3, 0, 0, 0, 6, 0, LAST_POINT },  // letter K
  [0x57] = { 0, 6, 3, 0, 6, 6, 3, 0, 0, 0, 6, 0, LAST_POINT },  // letter K
  [0x58] = { 0, 6, 3, 0, 6, 6, 3, 0, 0, 0, 6, 0, LAST_POINT },  // letter K
  [0x59] = { 0, 6, 3, 0, 6, 6, 3, 0, 0, 0, 6, 0, LAST_POINT },  // letter K
  [0x5A] = { 0, 6, 3, 0, 6, 6, 3, 0, 0, 0, 6, 0, LAST_POINT },  // letter K
  [0x5B] = { 0, 6, 3, 0, 6, 6, 3, 0, 0, 0, 6, 0, LAST_POINT },  // letter K
  [0x5C] = { 0, 6, 3, 0, 6, 6, 3, 0, 0, 0, 6, 0, LAST_POINT },  // letter K
  [0x5D] = { 0, 6, 3, 0, 6, 6, 3, 0, 0, 0, 6, 0, LAST_POINT },  // letter K
  [0x5E] = { 0, 6, 3, 0, 6, 6, 3, 0, 0, 0, 6, 0, LAST_POINT },  // letter K
  [0x5F] = { 0, 6, 3, 0, 6, 6, 3, 0, 0, 0, 6, 0, LAST_POINT },  // letter K
  [0x60] = { 0, 6, 3, 0, 6, 6, 3, 0, 0, 0, 6, 0, LAST_POINT },  // letter K
  [0x61] = { 0, 6, 3, 0, 6, 6, 3, 0, 0, 0, 6, 0, LAST_POINT },  // letter K
  [0x62] = { 0, 6, 3, 0, 6, 6, 3, 0, 0, 0, 6, 0, LAST_POINT },  // letter K
  [0x63] = { 0, 6, 3, 0, 6, 6, 3, 0, 0, 0, 6, 0, LAST_POINT },  // letter K
  [0x64] = { 0, 6, 3, 0, 6, 6, 3, 0, 0, 0, 6, 0, LAST_POINT },  // letter K
  [0x65] = { BR, YEL, 0, 4, 4, 1, 7, 1, LAST_POINT },
  [0x66] = { BR, YEL, 0, 1, 7, 5, LAST_POINT },
  [0x67] = { BR, YEL, 0, 6, 3, 7, LAST_POINT },*/
  [0x68] = { LAST_POINT },
  [0x69] = { LAST_POINT },
  [0x6a] = { LAST_POINT },
  [0x6b] = { LAST_POINT },
  // 6C - 6F = BONUS graphic
  [0x6c] = { 2, 0, 2, 4, 3, 5, 4, 4, 5, 5, 6, 4, 6, 0, 2, 0, BR, BR, 4, 4, 4, 0, BR, BR, 3, 7, 2, 8, 2, 11, 3, 12, 5, 12, 6, 11, 6, 8, 5, 7, 3, 7, BR, BR, 2, 14, 6, 14, 2, 19, 6, 19, BR, BR, 6, 21, 3, 21, 2, 22, 2, 25, 3, 26, 6, 26, BR, BR, 2, 28, 2, 31, 4, 31, 4, 28, 5, 28, 6, 29, 6, 31, LAST_POINT },
  [0x6d] = { LAST_POINT },
  [0x6e] = { LAST_POINT },
  [0x6e] = { LAST_POINT },
  [0x6f] = { LAST_POINT },
  // 70-79 = 0 - 9 (larger, used in score and timer coloured red)
  [0x70] = { BR, RED, 0, 2, 0, 4, 2, 6, 4, 6, 6, 4, 6, 2, 4, 0, 2, 0, 0, 2, LAST_POINT },
  [0x71] = { BR, RED, 0, 0, 0, 6, BR, BR, 0, 3, 6, 3, 5, 1, LAST_POINT },
  [0x72] = { BR, RED, 0, 6, 0, 0, 5, 6, 6, 3, 5, 0, LAST_POINT },
  [0x73] = { BR, RED, 1, 0, 0, 1, 0, 5, 1, 6, 2, 6, 3, 5, 3, 2, 6, 6, 6, 1, LAST_POINT },
  [0x74] = { BR, RED, 0, 5, 6, 5, 2, 0, 2, 7, LAST_POINT },
  [0x75] = { BR, RED, 1, 0, 0, 1, 0, 5, 2, 6, 4, 5, 4, 0, 6, 0, 6, 5, LAST_POINT },
  [0x76] = { BR, RED, 3, 0, 1, 0, 0, 1, 0, 5, 1, 6, 2, 6, 3, 5, 3, 0, 6, 2, 6, 5, LAST_POINT },
  [0x77] = { BR, RED, 6, 0, 6, 6, 0, 2, LAST_POINT },
  [0x78] = { BR, RED, 2, 0, 0, 1, 0, 5, 2, 6, 5, 0, 6, 1, 6, 4, 5, 5, 2, 0, LAST_POINT },
  [0x79] = { BR, RED, 0, 1, 0, 4, 2, 6, 5, 6, 6, 5, 6, 1, 5, 0, 4, 0, 3, 1, 3, 6, LAST_POINT },
  // Lines for bonus box?
  [0x7a] = { LAST_POINT },
  [0x7b] = { LAST_POINT },

  /* [0x7c] = { 7, 0, 7, 8, BR, BR, 5, 8, 5, 0 },
  [0x7d] = { 0, 3, 7, 3, BR, BR, 7, 5, 0, 5 },
  [0x7e] = { LAST_POINT },
  [0x7f] = { 0, 3, 7, 3, BR, BR, 7, 5, 0, 5 }, */

  [0x7c] = { LAST_POINT },
  [0x7d] = { LAST_POINT },
  [0x7e] = { LAST_POINT },
  [0x7f] = { LAST_POINT },

  // 0-9
  [0x80] = { 0, 2, 0, 4, 2, 6, 4, 6, 6, 4, 6, 2, 4, 0, 2, 0, 0, 2, LAST_POINT },  // Alternative 0-9
  [0x81] = { 0, 0, 0, 6, BR, BR, 0, 3, 6, 3, 5, 1, LAST_POINT },
  [0x82] = { 0, 6, 0, 0, 5, 6, 6, 3, 5, 0, LAST_POINT },
  [0x83] = { 1, 0, 0, 1, 0, 5, 1, 6, 2, 6, 3, 5, 3, 2, 6, 6, 6, 1, LAST_POINT },
  [0x84] = { 0, 5, 6, 5, 2, 0, 2, 7, LAST_POINT },
  [0x85] = { 1, 0, 0, 1, 0, 5, 2, 6, 4, 5, 4, 0, 6, 0, 6, 5, LAST_POINT },
  [0x86] = { 3, 0, 1, 0, 0, 1, 0, 5, 1, 6, 2, 6, 3, 5, 3, 0, 6, 2, 6, 5, LAST_POINT },
  [0x87] = { 6, 0, 6, 6, 0, 2, LAST_POINT },
  [0x88] = { 2, 0, 0, 1, 0, 5, 2, 6, 5, 0, 6, 1, 6, 4, 5, 5, 2, 0, LAST_POINT },
  [0x89] = { 0, 1, 0, 4, 2, 6, 5, 6, 6, 5, 6, 1, 5, 0, 4, 0, 3, 1, 3, 6, LAST_POINT },
  [0x8a] = { 0, 0, 6, 0, 2, 3, 6, 6, 0, 6, LAST_POINT },  // Alternative capital M
  [0x8b] = { 0, 0, 6, 0, 2, 3, 6, 6, 0, 6, LAST_POINT },  // Alternative lower case m
                                                          // 8F-8C = some graphic
                                                          /* [0x8c] = { 0, 3, 2, 3, BR, BR, 4, 3, 6, 3 },            // !
  [0x8d] = { 0, 3, 2, 3, BR, BR, 4, 3, 6, 3 },            // !
  [0x8e] = { 0, 3, 2, 3, BR, BR, 4, 3, 6, 3 },            // !
  [0x8f] = { 0, 3, 2, 3, BR, BR, 4, 3, 6, 3 },            // !
  */
  [0x8c] = { LAST_POINT },
  [0x8d] = { LAST_POINT },
  [0x8e] = { LAST_POINT },
  [0x8f] = { LAST_POINT },

  [0x90] = { LAST_POINT },
  [0x91] = { LAST_POINT },
  [0x92] = { LAST_POINT },
  [0x93] = { LAST_POINT },
  [0x94] = { LAST_POINT },
  [0x95] = { LAST_POINT },
  [0x96] = { LAST_POINT },
  [0x97] = { LAST_POINT },
  [0x98] = { LAST_POINT },
  [0x99] = { LAST_POINT },
  [0x9a] = { LAST_POINT },
  [0x9b] = { LAST_POINT },
  [0x9c] = { LAST_POINT },
  [0x9D] = { LAST_POINT },
  [0x9E] = { LAST_POINT },                                                                                                                                                                       // should be right half of TM symbol
  [0x9f] = { BR, RED, 0, 13, 0, 2, 2, 0, 5, 0, 7, 2, 7, 13, 5, 15, 2, 15, 0, 13, BR, BR, 2, 12, 5, 12, 2, 10, 5, 8, 2, 8, BR, BR, 5, 6, 5, 2, 5, 4, 2, 4, BR, BLK, 15, 0, 15, 15, LAST_POINT },  // should be left half of TM symbol
  // A0 - AF are blank
  [0xb0] = { 0, 0, 7, 0, 7, GW, 0, GW, 0, 0, LAST_POINT },                                      // Supposed to be a girder with hole in center used in rivets screen (just a box for now)
  [0xb1] = { BR, RED, 0, 0, 7, 0, BR, YEL, 7, GW, BR, RED, 0, GW, BR, YEL, 0, 0, LAST_POINT },  // Red square with Yellow lines top and bottom
  [0xb2] = { LAST_POINT },
  [0xb3] = { LAST_POINT },
  [0xb4] = { LAST_POINT },
  [0xb5] = { LAST_POINT },
  [0xb6] = { BR, MAG, GH, 0, GH, GW, LAST_POINT },                                              // white line on top
  [0xb7] = { 0, 1, GH, 1, GH, 2, GH - 2, 2, GH - 2, 5, GH, 5, GH, 6, 0, 6, 0, 1, LAST_POINT },  // paint can?
  [0xb8] = { BR, MAG, 0, 0, 0, GW, LAST_POINT },                                                // red line on bottom
  // B9 - BF are blank
  // C0 - C7 = girder with ladder on bottom going up
  [0xc0] = { 0, 0, GH, 0, 6, 0, 6, GW, GH, GW, 0, GW, 2, GW, 2, 0, LAST_POINT },                 // ladder with 2 rungs
  [0xc1] = { 6, GW, 0, GW, 2, GW, 2, 0, 0, 0, 6, 0, 6, GW, BR, BR, GH, 0, GH, GW, LAST_POINT },  // ladder with 2 rungs with line at top
  [0xc2] = { 5, 0, 0, 0, 2, 0, 2, GW, 0, GW, 5, GW, BR, BR, 6, GW, 6, 0, LAST_POINT },
  [0xc3] = { 4, 0, 0, 0, 2, 0, 2, GW, 0, GW, 4, GW, BR, BR, 5, GW, 5, 0, LAST_POINT },
  [0xc4] = { 3, 0, 0, 0, 2, 0, 2, GW, 0, GW, 3, GW, BR, BR, 4, GW, 4, 0, LAST_POINT },
  [0xc5] = { 2, 0, 0, 0, 2, 0, 2, GW, 0, GW, 2, GW, BR, BR, 3, GW, 3, 0, LAST_POINT },
  [0xc6] = { 1, 0, 0, 0, BR, BR, 1, GW, 0, GW, BR, BR, 2, GW, 2, 0, LAST_POINT },
  [0xc7] = { GH, 0, GH, GW, BR, BR, 1, GW, 1, 0, LAST_POINT },
  // C8 - CF are blank
  // D0 - D7 = ladder graphic with girder under going up and out
  [0xd0] = { GH, 0, GH, GW, BR, BR, 0, GW, 0, 0, LAST_POINT },  // horizontal girder simplified for faster drawing
  [0xd1] = { 6, 0, 6, GW, LAST_POINT },
  [0xd2] = { 5, 0, 5, GW, BR, BR, GH, GW, 6, GW, 6, 0, GH, 0, LAST_POINT },
  [0xd3] = { 4, 0, 4, GW, BR, BR, GH, GW, 5, GW, 6, GW, 6, 0, 5, 0, 7, 0, LAST_POINT },
  [0xd4] = { 3, 0, 3, GW, BR, BR, GH, GW, 4, GW, 6, GW, 6, 0, 4, 0, 7, 0, LAST_POINT },
  [0xd5] = { 2, 0, 2, GW, BR, BR, GH, GW, 3, GW, 6, GW, 6, 0, 3, 0, 7, 0, LAST_POINT },
  [0xd6] = { 1, 0, 1, GW, BR, BR, GH, GW, 2, GW, 6, GW, 6, 0, 2, 0, 7, 0, LAST_POINT },
  [0xd7] = { 0, 0, 0, GW, BR, BR, GH, GW, 1, GW, 6, GW, 6, 0, 1, 0, 7, 0, LAST_POINT },
  // D8 - DC are blank
  // help HE
  [0xdd] = { 0, 0, 7, 0, BR, BR, 4, 0, 4, 4, BR, BR, 1, 4, 7, 4, BR, BR, 2, 9, 1, 6, 7, 6, 7, 9, BR,
             BR, 5, 6, 5, 9, BR, BR, 7, 11, 2, 11, 3, 14, BR, BR, 3, 16, 7, 16, 7, 18, 6, 19, 5, 18,
             5, 16, BR, BR, 7, 22, 5, 21, BR, BR, 3, 21, 3, 21, LAST_POINT },  // Help (big H)
  [0xde] = { LAST_POINT },                                                     // de help EL
  [0xdf] = { LAST_POINT },                                                     // df help P!
  // E1 - E7 = girder graphic going up and out
  [0xe0] = { LAST_POINT },
  [0xe1] = { GH, 0, GH, GW, LAST_POINT },
  [0xe2] = { 6, 0, 6, GW, LAST_POINT },
  [0xe3] = { 5, 0, 5, GW, LAST_POINT },
  [0xe4] = { 4, 0, 4, GW, LAST_POINT },
  [0xe5] = { 3, 0, 3, GW, LAST_POINT },
  [0xe6] = { 2, 0, 2, GW, LAST_POINT },
  [0xe7] = { GH, 0, GH, GW, BR, BR, 1, GW, 1, 0, LAST_POINT },  // same as c7 (missed ladder out on c7)
  // E8 - EC are blank
  [0xed] = { 7, 1, 5, 1, BR, BR, 6, 1, 6, 5, BR, BR, 7, 5, 4, 5, BR, BR, 7, 10, 7, 7, 4, 7, 3,
             10, BR, BR, 5, 7, 5, 10, BR, BR, 7, 12, 3, 12, 2, 15, BR, BR, 1, 17, 7, 17, 7, 20,
             3, 20, 3, 17, BR, BR, 7, 23, 2, 22, BR, BR, 0, 21, 0, 22, LAST_POINT },  // Help (little H)
  [0xee] = { LAST_POINT },                                                            // ee help EL
  [0xef] = { LAST_POINT },                                                            // ef help P!
  // F6 - F0 = girder graphic in several vertical phases coming up from bottom
  [0xf0] = { GH, 0, GH, GW, BR, BR, 0, GW, 0, 0, LAST_POINT },
  [0xf1] = { 6, 0, 6, GW, LAST_POINT },
  [0xf2] = { 5, 0, 5, GW, LAST_POINT },
  [0xf3] = { 4, 0, 4, GW, LAST_POINT },
  [0xf4] = { 3, 0, 3, GW, LAST_POINT },
  [0xf5] = { 2, 0, 2, GW, LAST_POINT },
  [0xf6] = { 1, 0, 1, GW, LAST_POINT },
  [0xf7] = { 0, 0, 0, GW, LAST_POINT },
  // F8, F9, FA are blank
  [0xf8] = { LAST_POINT },
  [0xf9] = { LAST_POINT },
  [0xfa] = { LAST_POINT },
  [0xfb] = { 5, 1, 6, 2, 6, 5, 5, 6, 4, 6, 2, 3, BR, BR, 0, 3, 1, 4, BR, BR, 1, 3, 0, 4, LAST_POINT },                                 // ? (actually a question mark)                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                       // question mark
  [0xfc] = { GH, GW, 0, GW, LAST_POINT },                                                                                              // right red edge
  [0xfd] = { 0, 0, GH, 0, LAST_POINT },                                                                                                // left edge red vertical line
  [0xfe] = { 0, 0, 7, 0, 7, 7, 0, 7, 0, 0, LAST_POINT },                                                                               // X graphic                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                               // cross
  [0xff] = { 5, 2, 7, 2, 7, 4, 5, 4, 5, 2, BR, BR, 5, 3, 2, 3, 0, 1, BR, BR, 2, 3, 0, 5, BR, BR, 4, 0, 3, 1, 3, 5, 4, 6, LAST_POINT }  // Extra Mario Icon                                                                                                                                                                                                                                                                                                                                                                                                                                                                                   // jumpman / stick man
};

dk_sprite_t vector_library2[] = {
  // Love heart
  [0x00] = { 0, 8, 5, 2, 7, 1, 10, 1, 12, 3, 12, 6, 10, 8, 12, 10, 12, 13, 10, 15, 7, 15, 5, 14, 0, 8, LAST_POINT },
  // broken heart
  [0x01] = { 0, 7, 5, 1, 7, 0, 10, 0, 12, 5, 11, 6, 10, 5, 8, 7, 5, 4, 2, 7, 0, 7, BR, BR, 1, 9, 2, 9, 5, 7, 8, 10,
             10, 8, 12, 10, 12, 13, 10, 15, 7, 15, 5, 14, 1, 9, LAST_POINT },
  //	oilcan
  [0x02] = { 0, 0, 0, 1, 15, 1, 15, 0, 15, 15, 15, 14, 0, 14, 0, 15, 0, 0, BR, CYN, 5, 0, 5, 15, BR, BR, 7, 13, 7, 11, 10,
             11, BR, BR, 7, 9, 10, 9, BR, BR, 7, 6, 7, 4, 10, 4, 10, 7, 7, 7, BR, BR, 11, 0, 11, 15, LAST_POINT },
  // flames
  [0x03] = { 0, 4, 2, 2, 3, 3, 8, 0, 4, 5, 5, 6, 9, 4, 5, 8, 4, 7, 2, 10, 2, 11, 4, 12, 9, 10, 4, 14, 0, 12, LAST_POINT },
  // Barrels
  // down
  [0x04] = { 2, 0, 7, 0, 9, 3, 9, 12, 7, 15, 2, 15, 0, 12, 0, 3, 2, 0, BR, BR, 1, 1, 8, 1, BR, BR, 1, 14, 8, 14, LAST_POINT },  // barrel going down ladder or crazy barrel
  // down-1
  [0x05] = { 3, 3, 3, 12, BR, BR, 6, 3, 6, 12, LAST_POINT },
  // down-2
  [0x06] = { 2, 3, 2, 12, BR, BR, 7, 3, 7, 12, LAST_POINT },
  // Hammer
  // hammer-up (0x01e)
  [0x07] = { 5, 0, 7, 0, 8, 1, 8, 8, 7, 9, 5, 9, 4, 8, 4, 1, 5, 0, BR, BR, 4, 4, 0, 4, 0, 5, 4, 5, BR, BR, 8, 4, 9, 4, 9, 5, 8, 5, LAST_POINT },
  // hammer-down (0x01f)
  [0x08] = { 8, 0, 9, 0, 9, 2, 8, 3, 1, 3, 0, 2, 0, 0, 1, 0, 8, 0, BR, BR, 5, 0, 5, 0, 4, 0, 4, 0, BR, BR, 5, 3, 5, 9, 4, 9, 4, 3, BR, GRY, 0, 3, 0, 5, BR, BR, 0, 0, 0, 0, LAST_POINT },
  // Horizontal barrels
  // rolling
  [0x09] = { 3, 0, 6, 0, 8, 2, 9, 4, 9, 7, 8, 9, 6, 11, 3, 11, 1, 9, 0, 7, 0, 4, 1, 2, 3, 0, LAST_POINT },  // barrel outline
  // roll-1
  [0x0a] = { 2, 3, 3, 4, BR, BR, 3, 3, 2, 4, BR, BR, 6, 5, 3, 8, LAST_POINT },  // regular barrel
  // roll-2
  [0x0b] = { 2, 7, 3, 8, BR, BR, 3, 7, 2, 8, BR, BR, 3, 3, 6, 6, LAST_POINT },
  // roll-3
  [0x0c] = { 6, 7, 7, 8, BR, BR, 7, 7, 6, 8, BR, BR, 6, 3, 3, 6, LAST_POINT },
  // roll-4
  [0x0d] = { 6, 3, 7, 4, BR, BR, 7, 3, 6, 4, BR, BR, 3, 5, 6, 8, LAST_POINT },
  // skull-1
  [0x0e] = { 3, 3, 5, 3, 6, 4, 6, 7, 7, 8, 6, 9, 5, 8, 3, 8, 2, 7, 2, 4, 3, 3, BR, BR, 5, 4, 3, 6, LAST_POINT },  // skull/blue barrel
  // skull-2
  [0x0f] = { 5, 8, 3, 8, 2, 7, 2, 4, 3, 3, 5, 3, 6, 2, 7, 3, 6, 4, 6, 7, 5, 8, BR, BR, 3, 5, 5, 7, LAST_POINT },
  // skull-3
  [0x10] = { 7, 4, 7, 7, 6, 8, 4, 8, 3, 7, 3, 4, 2, 3, 3, 2, 4, 3, 6, 3, 7, 4, BR, BR, 6, 5, 4, 7, LAST_POINT },
  // skull-4
  [0x11] = { 4, 3, 6, 3, 7, 4, 7, 7, 6, 8, 4, 8, 3, 9, 2, 8, 3, 7, 3, 4, 4, 3, BR, BR, 6, 6, 4, 4, LAST_POINT },
  // stacked
  [0x12] = { 3, 0, 12, 0, 15, 2, 15, 7, 12, 9, 3, 9, 0, 7, 0, 7, 0, 2, 3, 0, BR, BR, 1, 2, 1, 7, BR, BR, 14, 2, 14, 7, BR, MBR, 2, 3, 13, 3, BR, BR, 2, 6, 13, 6, LAST_POINT },
  // Fireballs
  // fireball
  [0x13] = { 12, 2, 5, 0, 3, 0, 1, 1, 0, 3, 0, 8, 1, 10, 3, 11, 6, 12, 11, 13, 9, 10, 13, 12, 10, 9, 15,
             11, 10, 7, 13, 8, 10, 5, 14, 7, 9, 3, 12, 2, BR, RED, 6, 3, 7, 4, 6, 5, 5, 4, 6, 3, BR, BR,
             6, 6, 7, 7, 6, 8, 5, 7, 6, 6, LAST_POINT },
  // fb-flame
  [0x14] = { 12, 2, 5, 0, BR, BR, 6, 12, 11, 13, 9, 10, 13, 12, 10, 9, 15, 11, 10, 7, 13, 8, 10, 5, 14, 7, 9, 3, 12, 2, LAST_POINT },
  // Jumpman
  //["jm-120"] was 0xD0
  [0x15] = { 14, 4, 10, 3, 10, 4, 14, 6, BR, BR, 14, 11, 10, 12, 14, 9, BR, BR, 11, 5, 11, 6, 10, 7, 10, 8, 11, 9, 11, 11, 10, 10, 10, 9, 9, 8, 9, 7, 10, 6, 10, 5, 11, 5, BR, BR, 9, 5, 8, 4, 9, 2, BR, BR, 9, 10, 8, 11, 9, 13, BR, BR, 6, 4, 6, 3, 8, 1, BR, BR, 6, 11, 6, 12, 8, 14, BR, BR, 6, 5, 6, 6, 5, 6, 5, 5, 6, 5, BR, BR, 6, 9, 6, 10, 5, 10, 5, 9, 6, 9, BR, BR, 4, 3, 2, 5, 1, 4, 2, 1, 3, 1, 4, 3, BR, BR, 2, 10, 4, 12, 3, 13, 3, 14, 2, 14, 1, 11, 2, 10, BR, RED, 14, 4, 15, 5, 16, 7, 16, 8, 15, 10, 14, 11, 14, 4, BR, BR, 4, 3, 6, 4, 7, 5, 8, 5, 8, 6, 7, 6, 6, 7, 6, 8, 7, 9, 8, 9, 8, 10, 7, 10, 6, 11, 4, 12, BR, BR, 2, 5, 3, 7, 3, 8, 2, 10, BR, GRY, 13, 6, 13, 7, 12, 7, 12, 6, 13, 6, BR, BR, 13, 8, 13, 9, 12, 9, 12, 8, 13, 8, BR, BR, 10, 4, 8, 6, BR, BR, 10, 11, 8, 9, BR, BR, 8, 1, 9, 0, 10, 0, 10, 1, 9, 2, BR, BR, 9, 13, 10, 14, 10, 15, 9, 15, 8, 14, BR, BR, 10, 4, 12, 4, BR, BR, 10, 11, 12, 11, LAST_POINT },
  //["jm-121"] was 0xD1
  [0x16] = { 10, 2, 11, 1, 14, 2, 14, 3, 13, 3, 12, 4, 10, 2, BR, BR, 5, 2, 3, 4, 2, 3, 1, 3, 1, 2, 4, 1, 5, 2, BR, BR, 10, 5, 10, 6, 9, 6, 9, 5, 10, 5, BR, BR, 6, 5, 6, 6, 5, 6, 5, 5, 6, 5, BR, BR, 11, 6, 12, 6, 14, 8, BR, BR, 10, 9, 11, 8, 13, 9, BR, BR, 4, 6, 3, 6, 1, 8, BR, BR, 5, 9, 4, 8, 2, 9, BR, BR, 9, 14, 11, 10, 12, 10, 11, 14, BR, BR, 6, 14, 4, 10, 3, 10, 4, 14, BR, BR, 10, 11, 9, 11, 8, 10, 7, 10, 6, 11, 4, 11, 5, 10, 6, 10, 7, 9, 8, 9, 9, 10, 10, 10, 10, 11, BR, RED, 11, 14, 10, 15, 8, 16, 7, 16, 5, 15, 4, 14, 11, 14, BR, BR, 12, 4, 11, 6, 10, 7, 10, 8, 9, 8, 9, 7, 8, 6, 7, 6, 6, 7, 6, 8, 5, 8, 5, 7, 4, 6, 3, 4, BR, BR, 10, 2, 8, 3, 7, 3, 5, 2, BR, GRY, 14, 8, 15, 9, 15, 10, 14, 10, 13, 9, BR, BR, 2, 9, 1, 10, 0, 10, 0, 9, 1, 8, BR, BR, 9, 8, 11, 10, BR, BR, 6, 8, 4, 10, BR, BR, 9, 12, 9, 13, 8, 13, 8, 12, 9, 12, BR, BR, 7, 12, 7, 13, 6, 13, 6, 12, 7, 12, LAST_POINT },
  //["jm-122"] was 0xD2
  [0x17] = { 10, 2, 12, 3, 11, 7, 10, 7, 10, 6, 9, 5, 10, 2, BR, BR, 6, 5, 6, 6, 5, 6, 5, 5, 6, 5, BR, BR, 2, 2, 2, 5, 4, 7, 3, 8, 2, 8, 0, 6, 0, 0, 1, 0, 2, 2, BR, BR, 8, 10, 8, 11, 5, 11, 5, 10, 8, 10, BR, BR, 8, 10, 5, 11, BR, BR, 8, 11, 5, 10, BR, BR, 0, 14, 0, 9, 1, 8, 1, 11, 2, 11, 1, 13, 2, 14, BR, RED, 0, 14, 8, 14, BR, BR, 0, 14, 1, 16, 3, 16, 6, 14, BR, BR, 10, 2, 9, 1, 6, 0, 4, 0, 2, 1, BR, BR, 9, 5, 8, 4, 7, 4, 6, 7, 5, 8, BR, GRY, 2, 2, 2, 1, 1, 0, 0, 0, 0, 2, BR, BR, 5, 8, 5, 9, 6, 10, BR, BR, 8, 11, 9, 11, 8, 12, 6, 13, 5, 14, BR, BR, 5, 12, 5, 13, 4, 13, 4, 12, 5, 12, LAST_POINT },
  //["jm-0"] was 0xC0
  [0x18] = { 6, 6, 6, 7, 5, 7, 5, 6, 6, 6, BR, BR, 8, 6, 7, 5, BR, BR, 2, 5, 1, 5, 1, 4, 0, 4, 0, 7, 2, 7, 2, 5, BR, BR, 2, 10, 1, 10, 1, 9, 0, 9, 0, 12, 2, 12, 2, 10, BR, BR, 10, 3, 11, 3, 11, 5, 10, 5, 10, 3, BR, BR, 11, 3, 10, 5, BR, BR, 10, 3, 11, 5, BR, BR, 14, 8, 13, 9, 11, 8, 11, 9, 12, 10, 12, 11, 10, 11, 10, 13, 14, 11, BR, BR, 8, 10, 8, 11, 7, 12, 5, 12, 3, 10, BR, BR, 7, 9, 6, 10, 5, 9, BR, RED, 14, 3, 14, 11, 16, 10, 16, 8, 14, 5, BR, BR, 6, 9, 7, 8, 7, 5, 5, 4, 3, 4, 2, 5, BR, BR, 2, 7, 3, 8, 3, 9, 2, 10, BR, BR, 4, 11, 2, 12, BR, GRY, 14, 6, 13, 5, 12, 3, 11, 2, 11, 3, 10, 4, 9, 4, 8, 6, BR, BR, 8, 10, 10, 11, BR, BR, 5, 9, 4, 8, 3, 9, 3, 10, BR, BR, 13, 6, 13, 7, 12, 7, 12, 6, 13, 6, LAST_POINT },
  //["jm-1"]
  [0x19] = { 10, 3, 11, 3, 11, 5, 10, 5, 10, 3, BR, BR, 11, 3, 10, 5, BR, BR, 10, 3, 11, 5, BR, BR, 14, 8, 13, 9, 11, 8, 11, 9, 12, 10, 12, 11, 10, 11, 10, 13, 14, 11, BR, BR, 8, 2, 8, 6, BR, BR, 6, 3, 6, 5, BR, BR, 8, 10, 8, 12, BR, BR, 6, 11, 6, 12, 5, 14, BR, BR, 6, 7, 6, 8, 5, 8, 5, 7, 6, 7, BR, BR, 5, 2, 2, 2, 2, 4, 4, 4, 4, 3, 5, 3, 5, 2, BR, BR, 3, 13, 2, 14, 0, 13, 0, 12, 1, 12, 2, 11, 3, 13, BR, RED, 14, 3, 14, 11, 16, 10, 16, 8, 14, 5, BR, BR, 4, 4, 7, 6, 7, 9, 6, 11, 3, 13, BR, BR, 2, 4, 3, 7, 3, 8, 2, 11, BR, GRY, 14, 6, 13, 5, 12, 3, 11, 2, 11, 3, 10, 4, 9, 4, 8, 6, BR, BR, 8, 10, 10, 11, BR, BR, 13, 6, 13, 7, 12, 7, 12, 6, 13, 6, BR, BR, 8, 2, 8, 1, 7, 1, 6, 2, 6, 3, BR, BR, 7, 14, 7, 15, 5, 15, 5, 14, LAST_POINT },
  //["jm-2"]
  [0x1a] = { 9, 2, 10, 2, 10, 4, 9, 4, 9, 2, BR, BR, 10, 2, 9, 4, BR, BR, 9, 2, 10, 4, BR, BR, 13, 7, 12, 8, 10, 7, 10, 8, 11, 9, 11, 10, 9, 10, 9, 12, 13, 10, BR, BR, 6, 4, 7, 9, 7, 11, 6, 12, 5, 11, 4, 4, BR, BR, 2, 6, 2, 8, 0, 8, 0, 5, 1, 5, 1, 6, 2, 6, BR, BR, 4, 13, 4, 15, 1, 15, 1, 14, 2, 14, 2, 13, 4, 13, BR, RED, 13, 2, 13, 10, 15, 9, 15, 7, 13, 4, BR, BR, 4, 4, 2, 6, BR, BR, 2, 8, 3, 8, 3, 9, 2, 13, BR, BR, 6, 12, 4, 13, BR, GRY, 13, 5, 12, 4, 11, 2, 10, 1, 10, 2, 9, 3, 8, 3, 7, 5, BR, BR, 7, 9, 9, 10, BR, BR, 12, 5, 12, 6, 11, 6, 11, 5, 12, 5, BR, BR, 6, 4, 6, 2, 5, 2, 4, 4, LAST_POINT },
  //["jm-3"]
  [0x1b] = { 14, 2, 11, 1, 8, 3, 7, 3, BR, BR, 14, 4, 11, 3, 9, 5, BR, BR, 9, 10, 10, 12, 10, 13, 9, 14, 7, 12, BR, BR, 4, 3, 4, 6, 3, 6, 3, 2, 4, 3, BR, BR, 2, 7, 2, 10, 1, 11, 1, 7, 2, 7, BR, BR, 14, 5, 13, 4, 10, 4, 9, 6, 9, 9, 10, 11, 13, 11, 14, 10, BR, RED, 15, 5, 15, 10, 14, 10, 13, 9, 13, 6, 14, 5, 15, 5, BR, BR, 4, 3, 5, 2, 6, 2, 7, 3, 8, 5, 9, 5, 9, 6, 8, 6, 8, 7, 7, 8, 8, 9, 9, 9, 9, 10, 8, 10, 7, 12, 4, 12, 2, 10, BR, GRY, 14, 2, 15, 3, 15, 4, 14, 4, LAST_POINT },
  //["jm-4"]
  [0x1c] = { 15, 6, 13, 6, 14, 5, 12, 2, 11, 1, 10, 1, 10, 2, 11, 4, BR, BR, 15, 11, 13, 11, 14, 12, 13, 14, 10, 14, BR, BR, 6, 4, 6, 7, 5, 7, 5, 3, 6, 4, BR, BR, 3, 7, 3, 10, 2, 11, 2, 7, 3, 7, BR, RED, 15, 6, 15, 11, 14, 10, 14, 7, 15, 6, BR, BR, 6, 4, 8, 2, 9, 2, 11, 4, 12, 6, 13, 6, 13, 7, 12, 7, 11, 8, 11, 9, 12, 10, 13, 10, 13, 11, 12, 11, 11, 13, 10, 14, 7, 14, 3, 10, LAST_POINT },
  //["jm-5"]
  [0x1d] = { 7, 1, 7, 5, 6, 5, 6, 1, 7, 1, BR, BR, 3, 7, 3, 10, 2, 11, 2, 7, 3, 7, BR, BR, 11, 12, 9, 14, 7, 15, 6, 15, BR, RED, 7, 1, 8, 0, 9, 0, 11, 2, 12, 4, 12, 6, 11, 7, 11, 8, 12, 9, 12, 10, 11, 12, 9, 13, 5, 13, 3, 10, BR, BR, 3, 7, 5, 9, 6, 9, 7, 7, 7, 5, BR, GRY, 6, 13, 5, 14, 5, 15, 6, 15, LAST_POINT },
  //["jm-6"]
  [0x1e] = { 14, 5, 13, 4, 10, 4, 9, 6, 9, 9, 10, 11, 13, 11, 14, 10, BR, BR, 5, 1, 7, 1, 9, 3, 9, 5, BR, BR, 9, 10, 9, 12, 7, 14, 4, 14, BR, BR, 0, 3, 0, 7, 1, 7, 1, 4, 0, 3, BR, BR, 1, 8, 0, 8, 0, 12, 1, 11, 1, 8, BR, RED, 15, 5, 15, 10, 14, 10, 13, 9, 13, 6, 14, 5, 15, 5, BR, BR, 1, 7, 2, 6, 3, 7, 3, 8, 2, 9, 1, 8, BR, BR, 1, 4, 2, 3, 6, 3, 8, 5, 9, 5, 9, 6, 7, 6, 6, 7, 6, 8, 7, 9, 9, 9, 9, 10, 8, 10, 6, 12, 2, 12, 1, 11, BR, GRY, 5, 1, 4, 1, 4, 2, 5, 3, BR, BR, 5, 12, 4, 13, 4, 14, 5, 14, LAST_POINT },
  //["jm-8"]
  [0x1f] = { 11, 3, 11, 5, 10, 5, 10, 3, 11, 3, BR, BR, 10, 3, 11, 5, BR, BR, 11, 3, 10, 5, BR, BR, 14, 11, 10, 13, 10, 10, 12, 11, 12, 9, BR, BR, 14, 7, 9, 7, 7, 9, 6, 11, 7, 12, 8, 11, 9, 9, 14, 9, BR, BR, 8, 6, 7, 5, BR, RED, 16, 9, 16, 10, 14, 11, 14, 9, BR, BR, 14, 3, 14, 7, BR, BR, 14, 5, 16, 8, BR, GRY, 14, 7, 16, 8, 16, 9, 14, 9, BR, BR, 13, 6, 13, 7, 12, 7, 12, 6, 13, 6, BR, BR, 14, 6, 13, 5, 12, 3, 11, 2, BR, BR, 10, 4, 9, 4, 8, 6, BR, BR, 10, 10, 9, 10, BR, RED, 7, 9, 7, 5, 5, 4, 3, 4, 2, 5, BR, BR, 7, 12, 2, 12, BR, BR, 2, 7, 3, 8, 3, 9, 2, 10, BR, BLU, 6, 6, 6, 7, 5, 7, 5, 6, 6, 6, BR, BR, 2, 5, 2, 7, 0, 7, 0, 4, 1, 4, 1, 5, 2, 5, BR, BR, 2, 10, 2, 12, 0, 12, 0, 9, 1, 9, 1, 10, 2, 10, LAST_POINT },
  //["jm-9"]
  [0x20] = { 11, 3, 11, 5, 10, 5, 10, 3, 11, 3, BR, BR, 11, 3, 10, 5, BR, BR, 10, 3, 11, 5, BR, BR, 14, 8, 13, 9, 11, 8, 11, 9, 12, 10, 12, 11, 10, 11, 10, 13, 14, 11, BR, BR, 8, 2, 8, 10, 7, 11, 6, 10, 6, 2, BR, RED, 14, 3, 14, 11, 16, 10, 16, 8, 14, 5, BR, GRY, 13, 6, 13, 7, 12, 7, 12, 6, 13, 6, BR, BR, 14, 6, 13, 5, 12, 3, 11, 2, 11, 3, BR, BR, 10, 4, 9, 4, 8, 5, BR, BR, 8, 10, 10, 10, BR, BR, 8, 2, 8, 0, 7, 0, 6, 2, BR, RED, 6, 4, 3, 4, 2, 5, BR, BR, 2, 7, 3, 8, 3, 9, 2, 10, BR, BR, 2, 12, 5, 11, 7, 11, BR, BLU, 6, 6, 5, 6, 5, 7, 6, 7, BR, BR, 2, 5, 2, 7, 0, 7, 0, 4, 1, 4, 1, 5, 2, 5, BR, BR, 2, 10, 2, 12, 0, 12, 0, 9, 1, 9, 1, 10, 2, 10, LAST_POINT },
  //["jm-10"]
  [0x21] = { 11, 3, 11, 5, 10, 5, 10, 3, 11, 3, BR, BR, 10, 3, 11, 5, BR, BR, 11, 3, 10, 5, BR, BR, 14, 11, 10, 13, 10, 10, 12, 11, 12, 9, BR, BR, 14, 7, 9, 7, 7, 9, 6, 11, 7, 12, 8, 11, 9, 9, 14, 9, BR, BR, 8, 6, 7, 5, BR, RED, 16, 9, 16, 10, 14, 11, 14, 9, BR, BR, 14, 3, 14, 7, BR, BR, 14, 5, 16, 8, BR, GRY, 14, 7, 16, 8, 16, 9, 14, 9, BR, BR, 13, 6, 13, 7, 12, 7, 12, 6, 13, 6, BR, BR, 14, 6, 13, 5, 12, 3, 11, 2, BR, BR, 10, 4, 9, 4, 8, 6, BR, BR, 10, 10, 9, 10, BR, RED, 7, 9, 7, 5, 4, 4, BR, BR, 2, 4, 3, 7, 3, 8, 2, 11, BR, BR, 7, 12, 5, 11, 3, 13, BR, BLU, 4, 4, 4, 3, 5, 3, 5, 2, 2, 2, 2, 4, 4, 4, BR, BR, 2, 11, 3, 13, 2, 14, 0, 13, 0, 12, 1, 12, 2, 11, BR, BR, 6, 6, 6, 7, 5, 7, 5, 6, 6, 6, LAST_POINT },
  //["jm-11"]
  [0x22] = { 11, 3, 11, 5, 10, 5, 10, 3, 11, 3, BR, BR, 11, 3, 10, 5, BR, BR, 10, 3, 11, 5, BR, BR, 14, 8, 13, 9, 11, 8, 11, 9, 12, 10, 12, 11, 10, 11, 10, 13, 14, 11, BR, BR, 8, 2, 8, 10, 7, 11, 6, 10, 6, 2, BR, RED, 14, 3, 14, 11, 16, 10, 16, 8, 14, 5, BR, GRY, 13, 6, 13, 7, 12, 7, 12, 6, 13, 6, BR, BR, 14, 6, 13, 5, 12, 3, 11, 2, 11, 3, BR, BR, 10, 4, 9, 4, 8, 5, BR, BR, 8, 10, 10, 10, BR, BR, 8, 2, 8, 0, 7, 0, 6, 2, BR, RED, 6, 5, 4, 4, BR, BR, 2, 4, 3, 7, 3, 8, 2, 11, BR, BR, 7, 11, 5, 11, 3, 13, BR, BLU, 6, 6, 5, 6, 5, 7, 6, 7, BR, BR, 5, 2, 5, 3, 4, 3, 4, 4, 2, 4, 2, 2, 5, 2, BR, BR, 2, 11, 3, 13, 2, 14, 0, 13, 0, 12, 1, 12, 2, 11, LAST_POINT },
  //["jm-12"]
  [0x23] = { 11, 2, 11, 4, 10, 4, 10, 2, 11, 2, BR, BR, 10, 2, 11, 4, BR, BR, 11, 2, 10, 4, BR, BR, 14, 10, 10, 12, 10, 9, 12, 10, 12, 8, BR, BR, 14, 6, 9, 6, 7, 8, 6, 10, 7, 11, 8, 10, 9, 8, 14, 8, BR, BR, 8, 5, 7, 4, BR, RED, 16, 8, 16, 9, 14, 10, 14, 8, BR, BR, 14, 2, 14, 6, BR, BR, 14, 4, 16, 7, BR, GRY, 14, 6, 16, 7, 16, 8, 14, 8, BR, BR, 13, 5, 13, 6, 12, 6, 12, 5, 13, 5, BR, BR, 14, 5, 13, 4, 12, 2, 11, 1, BR, BR, 10, 3, 9, 3, 8, 6, BR, BR, 10, 9, 9, 9, BR, RED, 7, 8, 7, 4, 4, 4, 2, 6, BR, BR, 2, 8, 3, 8, 3, 9, 2, 13, BR, BR, 7, 11, 6, 11, 4, 13, BR, BLU, 6, 6, 6, 7, 5, 7, 5, 6, 6, 6, BR, BR, 0, 5, 0, 8, 2, 8, 2, 6, 1, 6, 1, 5, 0, 5, BR, BR, 4, 13, 4, 15, 1, 15, 1, 14, 2, 14, 2, 13, 4, 13, LAST_POINT },
  //["jm-13"]
  [0x24] = { 11, 2, 11, 4, 10, 4, 10, 2, 11, 2, BR, BR, 11, 2, 10, 4, BR, BR, 10, 2, 11, 4, BR, BR, 14, 7, 13, 8, 11, 7, 11, 8, 12, 9, 12, 10, 10, 10, 10, 12, 14, 10, BR, BR, 8, 2, 8, 10, 7, 11, 6, 10, 6, 2, BR, RED, 14, 2, 14, 10, 16, 9, 16, 7, 14, 4, BR, GRY, 13, 5, 13, 6, 12, 6, 12, 5, 13, 5, BR, BR, 14, 5, 13, 4, 12, 2, 11, 1, 11, 2, BR, BR, 10, 3, 9, 3, 8, 4, BR, BR, 8, 10, 10, 10, BR, BR, 8, 2, 8, 0, 7, 0, 6, 2, BR, RED, 6, 4, 4, 4, 2, 6, BR, BR, 2, 8, 3, 8, 3, 9, 2, 13, BR, BR, 4, 13, 6, 11, 7, 11, BR, BLU, 6, 6, 5, 6, 5, 7, 6, 7, BR, BR, 2, 6, 1, 6, 1, 5, 0, 5, 0, 8, 2, 8, 2, 6, BR, BR, 4, 13, 4, 15, 1, 15, 1, 14, 2, 14, 2, 13, 4, 13, LAST_POINT },
  //["jm-14"]
  [0x25] = { 9, 3, 10, 3, 10, 5, 9, 5, 9, 3, BR, BR, 10, 3, 9, 5, BR, BR, 9, 3, 10, 5, BR, BR, 13, 8, 12, 9, 10, 8, 10, 9, 11, 10, 11, 11, 9, 11, 9, 13, 13, 11, BR, BR, 1, 1, 4, 1, 4, 2, 3, 2, 3, 3, 1, 3, 1, 1, BR, BR, 3, 12, 5, 13, 3, 15, 2, 15, 3, 14, 2, 13, 3, 12, BR, BR, 7, 3, 7, 6, BR, BR, 5, 2, 5, 5, BR, BR, 7, 10, 8, 13, BR, BR, 5, 11, 6, 14, BR, BR, 5, 7, 5, 8, 4, 8, 4, 7, 5, 7, BR, RED, 13, 3, 13, 11, 15, 10, 15, 8, 13, 5, BR, BR, 3, 3, 6, 6, 6, 9, 5, 11, 3, 11, 3, 12, BR, BR, 1, 3, 2, 7, 2, 8, 0, 10, 0, 11, 2, 13, BR, GRY, 13, 6, 12, 5, 11, 3, 10, 2, 10, 3, 9, 4, 8, 4, 7, 6, BR, BR, 7, 10, 9, 11, BR, BR, 12, 6, 12, 7, 11, 7, 11, 6, 12, 6, BR, BR, 7, 3, 7, 1, 6, 1, 5, 2, BR, BR, 8, 13, 8, 15, 7, 15, 6, 14, LAST_POINT },
  //["jm-15"]
  [0x26] = { 10, 4, 11, 4, 11, 6, 10, 6, 10, 4, BR, BR, 11, 4, 10, 6, BR, BR, 10, 4, 11, 6, BR, BR, 14, 9, 13, 10, 11, 9, 11, 10, 12, 11, 12, 12, 10, 12, 10, 14, 14, 12, BR, BR, 9, 11, 9, 13, 7, 16, 6, 16, BR, BR, 6, 12, 7, 13, 6, 14, BR, BR, 7, 3, 8, 4, 9, 6, 9, 8, BR, BR, 5, 4, 6, 5, 7, 7, 7, 8, BR, BR, 2, 1, 3, 2, 2, 3, 3, 4, 1, 5, 0, 3, 2, 1, BR, BR, 2, 6, 3, 7, 2, 8, 3, 9, 1, 10, 0, 8, 2, 6, BR, RED, 14, 4, 14, 12, 16, 11, 16, 9, 14, 6, BR, BR, 3, 4, 4, 5, 6, 6, 7, 8, 7, 10, 6, 12, 3, 13, 1, 10, BR, BR, 1, 5, 2, 6, BR, BR, 3, 9, 4, 10, BR, GRY, 14, 7, 13, 6, 12, 4, 11, 3, 11, 4, 10, 5, 9, 5, 9, 6, BR, BR, 8, 11, 10, 12, BR, BR, 13, 7, 13, 8, 12, 8, 12, 7, 13, 7, BR, BR, 7, 3, 6, 2, 5, 2, 4, 3, 5, 4, BR, BR, 6, 14, 5, 14, 4, 15, 4, 16, 6, 16, BR, BR, 0, 12, 1, 13, 2, 15, BR, BR, 0, 14, 1, 16, LAST_POINT },
  // Kong
  //	["dk-side"] was 0xA0
  [0x27] = { 7, 1, 7, 5, 9, 7, 11, 7, 17, 13, 23, 15, 26, 18, 28, 23, 28, 26, 30, 28, 31, 30, 31, 35, 30, 36, BR, BR, 2, 6, 3,
             7, 3, 13, 5, 15, 5, 23, 4, 23, 2, 22, BR, BR, 2, 30, 5, 31, 10, 28, BR, BR, 3, 35, 10, 28, 18, 21, 23, 21, 24, 22,
             BR, BR, 7, 39, 13, 35, 17, 32, BR, BR, 19, 35, 21, 37, 21, 41, BR, BR, 26, 38, 26, 40, 25, 40, 25, 38, 26, 38, BR,
             BR, 6, 16, 7, 17, 10, 23, 10, 25, BR, BR, 6, 22, 8, 24, 9, 26, BR, BR, 30, 36, 30, 35, 27, 31, 24, 34, 22, 34, 21,
             33, 21, 32, BR, MBR, 7, 1, 1, 1, 0, 2, 0, 8, 2, 6, BR, BR, 5, 2, 5, 3, BR, BR, 1, 2, 2, 3, BR, BR, 2, 22, 0, 22, 0,
             34, 2, 32, 2, 30, BR, BR, 1, 24, 2, 24, BR, BR, 1, 29, 2, 28, BR, BR, 3, 35, 0, 39, 0, 41, 1, 42, 2, 42, 4, 40, 5,
             40, 6, 42, 7, 42, 7, 39, BR, BR, 17, 32, 17, 36, 18, 39, 21, 42, 22, 42, 24, 41, 25, 40, BR, BR, 26, 38, 30, 36,
             BR, BR, 21, 32, 23, 30, 25, 30, 26, 29, 26, 28, 25, 27, 24, 27, 20, 31, 17, 32, BR, BR, 28, 36, 28, 34, 26, 34,
             26, 36, 28, 36, BR, LBR, 27, 36, 27, 35, 26, 35, 26, 36, 27, 36, LAST_POINT },
  //		["dk-front"] was 0xA1
  [0x28] = { 31, 20, 31, 17, 27, 13, 25, 13, 25, 15, 28, 15, 29, 16, 30, 18, 30, 19, 29, 20, BR, BR, 25, 20, 25, 18, 24, 18,
             24, 20, BR, BR, 21, 15, 22, 16, 22, 20, BR, BR, 26, 18, 27, 18, 27, 19, 26, 19, 26, 18, BR, BR, 6, 20, 6, 16, 2,
             12, BR, BR, 2, 4, 4, 4, 5, 3, 7, 3, 11, 7, 13, 7, 13, 4, 16, 1, 19, 1, 23, 6, 24, 8, 24, 10, BR, BR, 7, 15, 8, 14,
             10, 14, BR, BR, 19, 6, 17, 10, 16, 13, BR, BR, 10, 13, 11, 10, BR, MBR, 27, 13, 27, 11, 26, 10, 25, 10, 21, 11, 20,
             14, 19, 14, 18, 16, 18, 20, BR, BR, 2, 12, 0, 11, 0, 0, 2, 2, 2, 4, BR, BR, 16, 13, 16, 15, 15, 18, 14, 19, 12, 19,
             10, 17, 10, 13, BR, BR, 6, 17, 7, 17, 8, 16, BR, BR, 6, 19, 7, 19, 8, 18, BR, BR, 1, 10, 2, 11, BR, BR, 1, 5, 2, 6,
             BR, BR, 28, 17, 28, 19, 26, 19, 26, 17, 28, 17, BR, LBR, 26, 18, 27, 18, 27, 19, 26, 19, 26, 18, LAST_POINT },
  // ["dk-growl"] was 0xA2
  [0x29] = { 22, 20, 22, 16, 23, 15, 23, 14, 22, 13, 20, 14, 19, 17, 19, 20, BR, LBR, 21, 15, 21, 17, BR, BR, 22, 16, 20, 16, BR, BR, 21, 19, 21, 20, BR, BR, 22, 20, 20, 20, LAST_POINT },
  //		["dk-hold"] was 0xA3
  [0x2a] = { 31, 20, 31, 17, 27, 13, 25, 13, 25, 15, 28, 15, 29, 16, 30, 18, 30, 19, 29, 20, BR, BR, 25, 20, 25, 18, 24, 18, 24, 20, BR, BR, 21, 15, 22, 16, 22, 20, BR, BR, 26, 18, 27, 18, 27, 19, 26, 19, 26, 18, BR, BR, 2, 4, 4, 4, 5, 3, 7, 3, 11, 1, 14, 0, 17, 0, 21, 1, 26, 6, 26, 8, 25, 10, BR, BR, 7, 3, 4, 6, BR, BR, 15, 11, 17, 10, 15, 8, 11, 8, BR, BR, 11, 12, 11, 16, 13, 18, 15, 18, 16, 17, 16, 12, 15, 11, 12, 11, 11, 12, BR, MBR, BR, BR, 13, 14, 14, 15, BR, BR, 14, 14, 13, 15, BR, BR, 27, 13, 27, 11, 26, 10, 25, 10, 21, 11, 20, 14, 19, 14, 18, 16, 18, 20, BR, BR, 2, 12, 0, 11, 0, 0, 2, 2, 2, 4, BR, BR, 1, 10, 2, 11, BR, BR, 1, 5, 2, 6, BR, BR, 28, 17, 28, 19, 26, 19, 26, 17, 28, 17, BR, BR, 4, 6, 3, 9, 3, 11, 4, 11, 5, 10, 8, 10, 9, 12, 10, 12, 10, 11, 11, 8, BR, LBR, 26, 18, 27, 18, 27, 19, 26, 19, 26, 18, LAST_POINT },
  //		["pauline"]
  [0x2b] = { 14, 11, 1, 12, 4, 0, 10, 7, 15, 6, 15, 7, 13, 9, 14, 11, BR, BLU, 10, 7, 9, 12, BR, PNK, 20, 14, 21, 13, 21, 8,
             15, 1, 15, 6, 15, 7, 20, 10, 20, 14, 18, 14, 16, 12, 16, 10, 14, 10, BR, BR, 19, 12, 19, 13, BR, BR, 2, 5, 0, 6,
             1, 2, 3, 3, 2, 5, BR, BR, 13, 6, 12, 2, 11, 2, 11, 7, BR, BR, 10, 12, 9, 15, 10, 15, 12, 11, BR, BR, 1, 12, 0, 13, 0, 9, 2, 9, 1, 12, LAST_POINT },
  // Bonus points
  [0x2c] = { 5, 0, 6, 1, 0, 1, BR, BR, 0, 0, 0, 2, BR, BR, 0, 4, 0, 8, 6, 8, 6, 4, 0, 4, BR, BR, 0, 10, 0, 14,
             6, 14, 6, 10, 0, 10, LAST_POINT },  // 100 Points
  [0x2d] = { 0, 0, 0, 4, 2, 4, 3, 1, 6, 4, 6, 0, BR, BR, 0, 6, 0, 9, 6, 9, 6, 6, 0, 6, BR, BR, 0, 11, 0, 14, 6,
             14, 6, 11, 0, 11, LAST_POINT },  // 300 Points
  [0x2e] = { 1, 0, 0, 1, 0, 3, 1, 4, 3, 4, 4, 0, 6, 0, 6, 4, BR, BR, 0, 6, 0, 9, 6, 9, 6, 6, 0, 6, BR, BR, 0, 11,
             0, 14, 6, 14, 6, 11, 0, 11, LAST_POINT },  // 500 Points
  [0x2f] = { 1, 0, 2, 0, 4, 4, 5, 4, 6, 3, 6, 1, 5, 0, 4, 0, 2, 4, 1, 4, 0, 3, 0, 1, 1, 0, BR, BR, 0, 6, 0, 9, 6,
             9, 6, 6, 0, 6, BR, BR, 0, 11, 0, 14, 6, 14, 6, 11, 0, 11, LAST_POINT },  // 800 Points

  // Item was smashed

  /*  [0x50] = { 6, 1, 9, 1, 13, 5, 13, 10, 9, 14, 6, 14, 2, 10, 2, 5, 6, 1, BR, YEL, 6, 2, 9, 2, 12, 5, 12, 10, 9, 13, 6, 13, 3, 10, 3, 5, 6, 2 },
  [0x51] = { 7, 3, 4, 6, 4, 9, 7, 12, 8, 12, 11, 9, 11, 6, 8, 3, 7, 3, BR, YEL, 7, 4, 5, 6, 5, 9, 7, 11, 8, 11, 10,
             9, 10, 6, 8, 4, 7, 4, BR, RED, 7, 5, 6, 6, 6, 9, 7, 10, 8, 10, 9, 9, 9, 6, 8, 5, 7, 5 },
  [0x52] = { 6, 1, 9, 1, 13, 5, 13, 10, 9, 14, 6, 14, 2, 10, 2, 5, 6, 1, BR, YEL, 6, 2, 9, 2, 12, 5, 12, 10, 9, 13, 6, 13, 3, 10, 3, 5, 6, 2 },  // simplify smash animation
  [0x53] = { 7, 3, 4, 6, 4, 9, 7, 12, 8, 12, 11, 9, 11, 6, 8, 3, 7, 3, BR, YEL, 7, 4, 5, 6, 5, 9, 7, 11, 8, 11, 10, 9,
             10, 6, 8, 4, 7, 4, BR, RED, 7, 5, 6, 6, 6, 9, 7, 10, 8, 10, 9, 9, 9, 6, 8, 5, 7, 5 },  // simplify smash animation
  [0x54] = { 7, 5, 5, 7, 5, 8, 7, 10, 8, 10, 10, 8, 10, 7, 8, 5, 7, 5, BR, YEL, 7, 6, 6, 7, 6, 8, 7, 9, 8, 9, 9, 8, 9, 7, 8, 6, 7, 6, BR, RED, 7, 7, 7, 8, 8, 8, 8, 7, 7, 7, LAST_POINT },
  [0x55] = { BR, YEL, 8, 3, 8, 0, BR, BR, 5, 4, 1, 0, BR, BR, 4, 7, 1, 7, BR, BR, 5, 11, 1, 15, BR, BR, 8, 12, 8, 15, BR, BR, 10, 11, 14, 15,
             BR, BR, 11, 7, 14, 7, BR, BR, 10, 4, 14, 0, BR, RED, 8, 6, 8, 8, BR, BR, 7, 7, 9, 7, LAST_POINT },

*/


  // Non-character objects:

  //	["select"]
  /* [0x57] = { 0, 0, 16, 0, 16, 16, 0, 16, 0, 0, LAST_POINT },  // selection box
                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                              // Bonus points
  //  [0xf7b] = { 5, 0, 6, 1, 0, 1, BR, BR, 0, 0, 0, 2, BR, BR, 0, 4, 0, 8, 6, 8, 6, 4, 0, 4, BR, BR, 0, 10, 0, 14, 6, 14, 6, 10, 0, 10, LAST_POINT },      // 100 Points
  //  [0xf7d] = { 0, 0, 0, 4, 2, 4, 3, 1, 6, 4, 6, 0, BR, BR, 0, 6, 0, 9, 6, 9, 6, 6, 0, 6, BR, BR, 0, 11, 0, 14, 6, 14, 6, 11, 0, 11, LAST_POINT },         // 300 Points
  //   [0xf7e] = { 1, 0, 0, 1, 0, 3, 1, 4, 3, 4, 4, 0, 6, 0, 6, 4, BR, BR, 0, 6, 0, 9, 6, 9, 6, 6, 0, 6, BR, BR, 0, 11, 0, 14, 6, 14, 6, 11, 0, 11, LAST_POINT },       // 500 Points
  //   [0xf7f] = { 1, 0, 2, 0, 4, 4, 5, 4, 6, 3, 6, 1, 5, 0, 4, 0, 2, 4, 1, 4, 0, 3, 0, 1, 1, 0, BR, BR, 0, 6, 0, 9, 6, 9, 6, 6, 0, 6, BR, BR, 0, 11, 0, 14, 6, 14, 6, 11, 0, 11, LAST_POINT },  // 800 Points
  // Love heart
  // [0xf76] = { 0, 8, 5, 2, 7, 1, 10, 1, 12, 3, 12, 6, 10, 8, 12, 10, 12, 13, 10, 15, 7, 15, 5, 14, 0, 8, LAST_POINT },                                                                  // full heart
  //  [0xf77] = { 0, 7, 5, 1, 7, 0, 10, 0, 12, 5, 11, 6, 10, 5, 8, 7, 5, 4, 2, 7, 0, 7, BR, BR, 1, 9, 2, 9, 5, 7, 8, 10, 10, 8, 12, 10, 12, 13, 10, 15, 7, 15, 5, 14, 1, 9, LAST_POINT },  // broken heart
  // Item was smashed
  //  [0xf60] = { 6, 1, 9, 1, 13, 5, 13, 10, 9, 14, 6, 14, 2, 10, 2, 5, 6, 1, BR, YEL, 6, 2, 9, 2, 12, 5, 12, 10, 9, 13, 6, 13, 3, 10, 3, 5, 6, 2 },
  //  [0xf61] = { 7, 3, 4, 6, 4, 9, 7, 12, 8, 12, 11, 9, 11, 6, 8, 3, 7, 3, BR, YEL, 7, 4, 5, 6, 5, 9, 7, 11, 8, 11, 10, 9, 10, 6, 8, 4, 7, 4, BR, RED, 7, 5, 6, 6, 6, 9, 7, 10, 8, 10, 9, 9, 9, 6, 8, 5, 7, 5 },
  //	[0xf62] = [0xf60],  // simplify smash animation
  //	[0xf63] = [0xf61],  // simplify smash animation
  //[0xf62] = {7,5,5,7,5,8,7,10,8,10,10,8,10,7,8,5,7,5,BR,YEL,7,6,6,7,6,8,7,9,8,9,9,8,9,7,8,6,7,6,BR,RED,7,7,7,8,8,8,8,7,7,7, LAST_POINT},
  //[0xf63] = {BR,YEL,8,3,8,0,BR,BR,5,4,1,0,BR,BR,4,7,1,7,BR,BR,5,11,1,15,BR,BR,8,12,8,15,BR,BR,10,11,14,15,BR,BR,11,7,14,7,BR,BR,10,4,14,0,BR,RED,8,6,8,8,BR,BR,7,7,9,7, LAST_POINT},
  */
};

//
// Return RGB values and update colour DACs if necessary
//
DataChunk_t change_colour(DataChunk_t *localChunk, int color) {
  // DataChunk_t localChunk;

  localChunk->r = 0xB4;  // White by default
  localChunk->g = 0xB4;
  localChunk->b = 0xB4;

  if (color == BLK) {
    localChunk->r = 0;
    localChunk->g = 0;
    localChunk->b = 0;
  } else if (color == WHT) {
    localChunk->r = 0xB4;
    localChunk->g = 0xB4;
    localChunk->b = 0xB4;
  } else if (color == YEL) {
    localChunk->r = 0xB4;
    localChunk->g = 0xB4;
    localChunk->b = 0x50;
  } else if (color == RED) {
    localChunk->r = 0xB4;
    localChunk->g = 0;
    localChunk->b = 0;
  } else if (color == BLU) {
    localChunk->r = 0;
    localChunk->g = 0;
    localChunk->b = 0xB4;
  } else if (color == MBR) {
    localChunk->r = 0xB4;
    localChunk->g = 0x77;
    localChunk->b = 0x13;
  } else if (color == BRN) {
    localChunk->r = 0xB4;
    localChunk->g = 0x06;
    localChunk->b = 0x09;
  } else if (color == MAG) {
    localChunk->r = 0xFF;
    localChunk->g = 0x00;
    localChunk->b = 0xFF;
  } else if (color == PNK) {
    localChunk->r = 0xff;
    localChunk->g = 0xd1;
    localChunk->b = 0xdc;
  } else if (color == LBR) {
    localChunk->r = 0xB4;
    localChunk->g = 0xb9;
    localChunk->b = 0x8b;
  } else if (color == CYN) {
    localChunk->r = 0x14;
    localChunk->g = 0xf3;
    localChunk->b = 0xff;
  } else if (color == GRY) {
    localChunk->r = 0x96;
    localChunk->g = 0x96;
    localChunk->b = 0x96;
  } else if (color == BRIGHT_BLUE) {
    localChunk->r = 0x1f;
    localChunk->g = 0x1f;
    localChunk->b = 0xff;
  } else if (color == GRN) {
    localChunk->r = 0;
    localChunk->g = 0xff;
    localChunk->b = 0;
  }

  return *localChunk;
}
//
// draw multiple chained lines from a table of y,x points.  Optional offset for start y,x.
//
void draw_object(dk_sprite_t *vec_lib, uint16_t vec_id, int offset_x, int offset_y, int color, int flip_x, int flip_y) {
  int _savey = -1, _savex = -1;
  int _i;
  int _datay, _datax;

  // REPETITIOUS CODE TO OPTIMISE BELOW

  DataChunk_t localChunk;

  for (_i = 0; _i < 270; _i += 2) {
    _datay = vec_lib[vec_id].points[_i];
    _datax = vec_lib[vec_id].points[_i + 1];

    if (_datay == LAST_POINT) {  // Stop reading if we've reached the last point of the vector
      if (_savex != -1 && _savey != -1) {
        if (flip_x > 0) {
          add_chunk(flip_x - _savex + offset_x, _savey + offset_y, 0, 0, 0);
        } else if (flip_y > 0) {
          add_chunk(_savex + offset_x, flip_y - _savey + offset_y, 0, 0, 0);
        } else {
          add_chunk(_savex + offset_x, _savey + offset_y, 0, 0, 0);
        }
      }
      break;
    } else if (_datay == BR) {  // We've reached a break in the vector
      if (_datax == BR) {       // it's a definite break, not just a colour change
        if (flip_x > 0)
          add_chunk(flip_x - _savex + offset_x, _savey + offset_y, 0, 0, 0);
        else if (flip_y > 0)
          add_chunk(_savex + offset_x, flip_y - _savey + offset_y, 0, 0, 0);
        else
          add_chunk(_savex + offset_x, _savey + offset_y, 0, 0, 0);
      } else {
        localChunk = change_colour(&localChunk, _datax);  // Keep note of the next colours to use
      }
    } else {          // otherwise either move the gun to the start of a vector, or else draw the next line
      if (_i == 0) {  // if it's the first pair of points, then move to the spot with gun off
        add_chunk(_datax + offset_x, _datay + offset_y, 0, 0, 0);
        localChunk = change_colour(&localChunk, color);  // Keep note of the next colours to use
      } else {
        if (flip_x > 0)
          add_chunk(flip_x - _datax + offset_x, _datay + offset_y, localChunk.r, localChunk.g, localChunk.b);
        else if (flip_y > 0)
          add_chunk(_datax + offset_x, flip_y - _datay + offset_y, localChunk.r, localChunk.g, localChunk.b);
        else
          add_chunk(_datax + offset_x, _datay + offset_y, localChunk.r, localChunk.g, localChunk.b);
      }
      _savey = _datay;  // Keep track of where the gun landed last time
      _savex = _datax;
    }
  }
}
//
// Add vectors (and blank vectors) to the output vector list.
//
void reconnect_vectors() {
  int last_x = 0;
  int last_y = 0;
  int x0, y0, x1, y1;

  s_out_vec_cnt = 0;

  int i;
  for (i = 0; i < s_in_vec_cnt; i++) {
    x0 = s_in_vec_list[i].x0;
    y0 = s_in_vec_list[i].y0;
    x1 = s_in_vec_list[i].x1;
    y1 = s_in_vec_list[i].y1;

    if (last_x != x0 || last_y != y0) {
      // Disconnect detected. Insert a blank vector.
      s_out_vec_list[s_out_vec_cnt].x0 = last_x;
      s_out_vec_list[s_out_vec_cnt].y0 = last_y;
      s_out_vec_list[s_out_vec_cnt].x1 = x0;
      s_out_vec_list[s_out_vec_cnt].y1 = y0;
      s_out_vec_list[s_out_vec_cnt].r = 0;
      s_out_vec_list[s_out_vec_cnt].g = 0;
      s_out_vec_list[s_out_vec_cnt].b = 0;
      s_out_vec_cnt++;
    }
    s_out_vec_list[s_out_vec_cnt].x0 = last_x;
    s_out_vec_list[s_out_vec_cnt].y0 = last_y;
    s_out_vec_list[s_out_vec_cnt].x1 = x1;
    s_out_vec_list[s_out_vec_cnt].y1 = y1;
    s_out_vec_list[s_out_vec_cnt].r = s_in_vec_list[i].r;
    s_out_vec_list[s_out_vec_cnt].g = s_in_vec_list[i].g;
    s_out_vec_list[s_out_vec_cnt].b = s_in_vec_list[i].b;
    s_out_vec_cnt++;
    last_x = x1;
    last_y = y1;
  }
}
//
// Draw the pre-calculated vectors stored in a buffer
//
void draw_chunks() {
  reconnect_vectors();  // optimise the vectors before drawing

  for (int i = 0; i < s_out_vec_cnt; i++) {
    brightness(s_out_vec_list[i].r, s_out_vec_list[i].g, s_out_vec_list[i].b);
    draw_to_xy(s_out_vec_list[i].x1, s_out_vec_list[i].y1);
  }
}
//
// Draw moving sprites later (move the fixed ones above to a buffer)
//
void draw_girder_stage() {

  if (game_mode == 0x16 && read(0x6388, 0, 0) >= 4)
    _growling = 1;

  draw_stacked_barrels();
  draw_kong(172, 24, _growling);
  draw_pauline(90);
  draw_loveheart();
  draw_bonusbox();
  draw_hammers(168, 56, 17, 148);
  draw_jumpman();
  draw_barrels();
  draw_fireballs();
  draw_points();
  draw_oilcan_and_flames(8, 16);
}
//
// Initialise some variables and game registers before game starts
//
void Setup_DK() {
  s_in_vec_cnt = 0;  // vector counter
  s_in_vec_last_x = 0.0;
  s_in_vec_last_y = 0.0;

  s_in_vec_list = (DataChunk_t *)emu_Malloc(MAX_PTS * sizeof(DataChunk_t));
  s_out_vec_list = (DataChunk_t *)emu_Malloc(MAX_PTS * sizeof(DataChunk_t));
  s_tmp_vec_list = (DataChunk_t *)emu_Malloc(MAX_PTS * sizeof(DataChunk_t));

  frame_count = 0;
}
//
// Update vectors during game play
//
void Update_DK() {
  s_in_vec_cnt = 0;  // reset vector counter to draw a new frame
  vector_color = WHT;
  game_mode = read(MODE, 0, 0);

  draw_vector_characters();  // Draw various game objects stored in tile RAM

  if (read(VRAM_BL, 0xf0, 0))
    draw_girder_stage();  // Draw the other vectors of the girder stage which can be animated

  // if (read(VRAM_BL, 0xb0))
  //  draw_rivet_stage();

  mode_specific_changes();  // Handles various game states
  draw_chunks();            // Draw buffer of pre-calculated vectors
  update_frame_count();
  brightness(0, 0, 0);  // Turn the colour guns off before the next frame
  last_mode = game_mode;
}
//
// Keep count of drawn frames
//
void update_frame_count() {
  frame_count++;
  if (frame_count > 10)
    frame_count = 0;
}
//
// draw 100, 300, 500 or 800 when points awarded
//
void draw_points() {
  if (read(0x6a30, 0, 0) > 0) {
    int _y = 254 - read(0x6a33, 0, 0);
    int _x = read(0x6a30, 0, 0) - 22;
    draw_object(vector_library2, read(0x6a31) + 0x2B, _x, _y + 3, WHT, 0, 0);  // move points up a little so they don't overlap as much
  }
}

void draw_kong(int y, int x, int growl) {
  int obj = 0x0a;
  int colour1 = LBR;
  int colour2 = MBR;

  int _state = read(0x691d, 0, 0);  // state of kong - is he deploying a barrel?
                                    // if (read(0x6a20, 0, 0) &&   // COMMENTED THIS OUT OTHERWISE KONG NEVER GETS DRAWN
  if (_state == 173 || _state == 45 || _state == 42) {
    if (read(0x6382, 0x80, 0x81)) {
      obj = 0x0e;
      colour1 = CYN;
      colour2 = BLU;
    }

    // Kong deploying a barrel
    if (_state == 173) {
      // Releasing barrel to right
      draw_object(vector_library2, 0x27, x + 1, y, BRN, 0, 0);
      draw_object(vector_library2, 0x09, x + 44, y, colour1, 0, 0);
      draw_object(vector_library2, obj, x + 44, y, colour2, 0, 0);
    } else if (_state == 45) {
      // Grabbing barrel from left (mirrored)
      draw_object(vector_library2, 0x27, x - 3, y, BRN, 42, 0);     // dk-side
      draw_object(vector_library2, 0x09, x - 3, y, colour1, 0, 0);  // rolling
      draw_object(vector_library2, obj, x - 15, y, colour2, 0, 0);
    } else if (_state == 42) {
      // Holding barrel in front
      draw_object(vector_library2, 0x2a, x, y, BRN, 0, 0);        // left side
      draw_object(vector_library2, 0x2a, x + 20, y, BRN, 20, 0);  // mirrored right side
      draw_object(vector_library2, 0x04, x + 12.4, y + 2, colour1, 0, 0);
      draw_object(vector_library2, 0x05, x + 12.4, y + 2, colour2, 0, 0);
    }
  } else {
    // Default front facing Kong
    draw_object(vector_library2, 0x28, x, y, BRN, 0, 0);        // left side
    draw_object(vector_library2, 0x28, x + 20, y, BRN, 20, 0);  // mirrored right side

    if (growl) {
      draw_object(vector_library2, 0x29, x, y, MBR, 0, 0);        // left side
      draw_object(vector_library2, 0x29, x + 20, y, MBR, 20, 0);  // mirrored right side
    }
  }
}

void draw_stacked_barrels() {
  draw_object(vector_library2, 0x12, 0, 173, LBR, 0, 0);
  draw_object(vector_library2, 0x12, 10, 173, LBR, 0, 0);
  draw_object(vector_library2, 0x12, 0, 189, LBR, 0, 0);
  draw_object(vector_library2, 0x12, 10, 189, LBR, 0, 0);
}

void draw_hammers(int hammer1_x, int hammer1_y, int hammer2_x, int hammer2_y) {
  if (read(0x6a1c, 0, 0) > 0 && read(0x6691, 0, 0))
    draw_object(vector_library2, 0x07, hammer1_x, hammer1_y, MBR, 0, 0);

  if (read(0x6a18, 0, 0) > 0 && read(0x6681, 0, 0))
    draw_object(vector_library2, 0x07, hammer2_x, hammer2_y, MBR, 0, 0);
}

long rndnum(min, max) {
  return random() * (max - min + 1) + min;
}

void draw_oilcan_and_flames(int x, int y) {
  draw_object(vector_library2, 0x02, y, x, BLU, 0, 0);                           // Oil can is Blue in original game
  if (read(0x6a29, 0x40, 0x43)) {                                                // oilcan is on fire
    draw_object(vector_library2, 0x03, x + 8, y + 8, YEL, 0, 0);                 // draw base of flames
    draw_object(vector_library2, 0x03, x + 8, y + 8 + rndnum(0, 3), RED, 0, 0);  // draw flames extending upwards
  }
}

void draw_barrels() {
  int _y, _x, _type, _state, _addr, barrel1_colour, barrel2_colour, dec_state, barrel_sprite;
  const uint16_t BARRELS[] = { 0x6700, 0x6720, 0x6740, 0x6760, 0x6780, 0x67a0, 0x67c0, 0x67e0, 0x6800, 0x6820 };
  uint16_t barrel_state[] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };  // Up to 10 barrels can be active

  for (_addr = 0; _addr <= 9; _addr++) {
    if (read(BARRELS[_addr], 0, 0) == 1 && read(0x6200, 1, 0) && read(BARRELS[_addr] + 3, 0, 0) > 0) {  // barrel active and Jumpman alive
      _y = 251 - read(BARRELS[_addr] + 5, 0, 0);
      _x = read(BARRELS[_addr] + 3, 0, 0) - 20;
      _type = read(BARRELS[_addr] + 0x15, 0, 0) + 1;  // type of barrel: 1 is normal, 2 is blue/skull

      if (_type == 1) {
        barrel1_colour = LBR;
        barrel2_colour = MBR;
        barrel_sprite = 0x09;
      } else if (_type == 2) {
        barrel1_colour = CYN;
        barrel2_colour = BLU;
        barrel_sprite = 0x0d;
      }

      if (read(BARRELS[_addr] + 1, 1, 0) || (read(BARRELS[_addr] + 2, 0, 0) & 1) == 1) {  // barrel is crazy or going down a ladder
        _state = read(BARRELS[_addr] + 0xf, 0, 0);

        dec_state = (_state % 2) + 1;  // dec_state is either 1 or 2
        draw_object(vector_library2, 0x04, _x - 2, _y, barrel1_colour, 0, 0);
        draw_object(vector_library2, 0x04 + dec_state, _x - 2, _y, barrel2_colour, 0, 0);
      } else {                         // barrel is rolling
        _state = barrel_state[_addr];  // not sure if the original "or 0" actually changes anything
        if (frame_count == 10) {       // Check every 10 frames if the barrel has changed direction
          if (read(BARRELS[_addr] + 2, 2, 0))
            _state -= 1;
          else
            _state += 1;  // roll left or right?

          barrel_state[_addr] = _state;
        }

        // Rolling barrel sprites are 0x09 (bigger), a, b, c, d
        dec_state = (_state % 4) + 1;  // dec_state is 1, 2, 3 or 4
        draw_object(vector_library2, 0x09, _x, _y, barrel1_colour, 0, 0);

        // 0x09+ (rolling barrel) or 0x0d+ (skull)
        draw_object(vector_library2, barrel_sprite + dec_state, _x, _y, barrel2_colour, 0, 0);
      }
    }
  }
}

void draw_fireballs() {
  int _y, _x, _flip = 0;
  int fireball_color = YEL;
  int flame_color = RED;
  uint16_t _addr;

  if (read(0x6217, 0, 0) == 1) {
    fireball_color = BLU;
    flame_color = CYN;
  }

  const uint16_t FIREBALLS[] = { 0x6400, 0x6420, 0x6440, 0x6460, 0x6480 };

  for (_addr = 0; _addr < 5; _addr++) {
    if (read(FIREBALLS[_addr], 1, 0)) {  // fireball is active
      _y = 247 - read(FIREBALLS[_addr] + 5, 0, 0);
      _x = read(FIREBALLS[_addr] + 3, 0, 0) - 22;
      if (read(FIREBALLS[_addr] + 0xd, 1, 0))
        _flip = 13;                                                                      // fireball moving right so flip the vectors
      draw_object(vector_library2, 0x13, _x, _y, fireball_color, _flip, 0);              // draw body
      draw_object(vector_library2, 0x14, _x, _y + rndnum(0, 3), flame_color, _flip, 0);  // draw flames extending upwards
    }
  }
}

void draw_loveheart() {
  int _x = read(0x6a20, 0, 0) - 23;

  if (_x > 0) {
    int _y = 250 - read(0x6a23, 0, 0);
    // love heart was originally mapped at 0x76 and 0x77, remapped to 0x00 and 0x01
    // 6a21 can be 0x76 or 0x77
    draw_object(vector_library2, read(0x6a21, 0, 0) - 0x76, _x, _y, MAG, 0, 0);  // THIS MIGHT CRASH THE GAME, NEED TO TEST WHAT 6A21 READS WHEN LEVEL IS WON
  }
}

void circle(int x, int y, int radius, int color) {
  // draw a segmented circle at given position with radius
  int _save_segy, _save_segx;
  DataChunk_t localChunk;

  change_colour(&localChunk, color);

  for (int _segment = 0; _segment > 360; _segment += 24) {
    int _angle = _segment * (3.14 / 180.0);
    int _segy = y + radius * sin(_angle);
    int _segx = x + radius * cos(_angle);

    if (_save_segy)
      _vector(_save_segx, _save_segy, _segx, _segy);

    _save_segy = _segy;
    _save_segx = _segx;
  }
}

void draw_jumpman() {
  int _y = 255 - read(0x6205, 0, 0);
  int _x = read(0x6203, 0, 0) - 15;
  int _sprite = read(0x694d, 0, 0);
  int _sprite_mod = _sprite % 128;
  int _smash_offset = 9;
  int _grab;
  int jman, flipx, flipy;

  if (_y < 255) {
    if ((_sprite_mod >= 0 && _sprite_mod <= 15) || _sprite_mod == 120 || _sprite_mod == 121 || _sprite_mod == 122) {
      // right facing sprites are mirrored.
      // < 128 are left facing sprites
      // (0,1,2) = walking, (3,4,5,6) = climbing, (8,9,10,11,12,13) = hammer smashing,
      // (14,15) = jumping, (120,121,122) = dead

      _grab = read(0x6218, 1, 0);
      if (_grab) {  // grabbing hammer
        if (_sprite < 128)
          _sprite = 10;
        else _sprite = 138;  // change default sprite to a hammer grab
                             //        if (_sprite < 128)
                             //      _sprite = 0x21;
                             //     else _sprite = 128 + 0x21;  // change default sprite to a hammer grab
      }

      if (_sprite < 128) {  // running left
        if (_sprite <= 15) jman = 0x18 + _sprite;
        else if (_sprite == 120) jman = 0x15;
        else if (_sprite == 121) jman = 0x16;
        else if (_sprite == 122) jman = 0x17;
        flipx = 0;
        flipy = 0;
      } else if (_sprite == 248) {  // flip y
        flipx = 0;
        flipy = 8;
        jman = 0x15;
      } else {  // flip x: running right
        if (_sprite - 128 <= 15) jman = 0x18 + _sprite - 128;
        else if (_sprite - 128 == 120) jman = 0x15;
        else if (_sprite - 128 == 121) jman = 0x16;
        else if (_sprite - 128 == 122) jman = 0x17;
        flipx = 8;
        flipy = 0;
      }

      draw_object(vector_library2, jman, _x - 7, _y - 7, BRIGHT_BLUE, flipx, flipy);

      // Add the hammer?
      if (_grab)  // grabbing hammer
        draw_object(vector_library2, 0x07, _x - 3, _y + 9, MBR, 0, 0);
      else if (_sprite_mod >= 8 && _sprite_mod <= 13) {  // using hammer
        if (_sprite == 8 || _sprite == 10)
          draw_object(vector_library2, 0x07, _x - 3, _y + 9, MBR, 0, 0);
        else if (_sprite == 12 || _sprite == 140)
          draw_object(vector_library2, 0x07, _x - 4, _y + 9, MBR, 0, 0);
        else if (_sprite == 136 || _sprite == 138)
          draw_object(vector_library2, 0x07, _x - 5, _y + 9, MBR, 0, 0);
        else if (_sprite == 9 || _sprite == 11 || _sprite == 13) {
          draw_object(vector_library2, 0x08, _x - 16, _y - 4, MBR, 0, 0);
          _smash_offset = -4;
        } else if (_sprite == 137 || _sprite == 139 || _sprite == 141) {
          draw_object(vector_library2, 0x08, _x + 13, _y - 4, MBR, 4, 0);
          _smash_offset = -4;
        }
      }

      // Add smashed item?
      _sprite = read(0x6a2d, 0, 0);
      if (read(0x6a2c, 0, 0) > 0 && _sprite >= 0x60 && _sprite <= 0x63) {
        // v0.14 - prefer growing circle effect to the sprites
        //  circle(_y + _smash_offset + 4, read(0x6a2c, 0, 0) - 13, (_sprite - 95) * 5, 0xff444444 + random(0xbbbbbb));
        circle(read(0x6a2c, 0, 0) - 13, _y + _smash_offset + 4, (_sprite - 95) * 5, RED);
        // Originally mapped at 0x60, remapped to 0x50
        // v0.14 - prefer growing circle effect to the sprites
        //  VOLUNTARILY COMMENTED OUT draw_object(_sprite - 0x10, read(0x6a2c, 0, 0) - 17, _y + _smash_offset, BLU, 0, 0);
      }
    }
  }
}

void mode_specific_changes() {
  int _y, _x;

  if (game_mode == 0x06 && last_mode == 0x06) {
    // display a growling Kong on the title screen
    draw_kong(48, 92, 0);  // 0 = true in the instance

    // show "vectorised by 10yard" in the footer
    const uint8_t letter[] = { 0x26, 0x15, 0x13, 0x24, 0x1f, 0x22, 0x19, 0x23, 0x15, 0x14, 0x10, 0x12, 0x29, 0x10, 0x01, 0x00, 0x29, 0x11, 0x22, 0x14 };

    for (int i = 0; i < 20; i++)
      draw_object(vector_library, letter[i], i * 8 + 25, 8, GRY, 0, 0);
  } else if (game_mode == 0x15) {  // Record name at end of game
    // highlight selected character during name registration
    _y = floor(read(0x6035, 0, 0) / 10) * -16 + 156;
    _x = read(0x6035, 0, 0) % 10 * 16 + 36;
    draw_object(vector_library2, 0x57, _x, _y, CYN, 0, 0);
  } else if (game_mode == 0xa) {
    // display multiple kongs on the how high can you get screen
    _y = 24;
    _x = 92;
    int j = read_u8(0x622e);
    for (int i = 1; i < j; i++) {
      draw_kong(_y, _x, 0);
      _y = _y + 32;
    }
  }
}
//
// Function to compute region code for a point(x, y)
//
uint32_t compute_code(int32_t x, int32_t y) {
  // initialized as being inside
  uint32_t code = 0;

  if (x < s_clipx_min)  // to the left of rectangle
    code |= LEFT;
  else if (x > s_clipx_max)  // to the right of rectangle
    code |= RIGHT;
  if (y < s_clipy_min)  // below the rectangle
    code |= BOTTOM;
  else if (y > s_clipy_max)  // above the rectangle
    code |= TOP;

  return code;
}
//
// Cohen-Sutherland line-clipping algorithm.  Some games (such as starwars)
// generate coordinates outside the view window, so we need to clip them here.
//
uint32_t line_clip(int32_t *pX1, int32_t *pY1, int32_t *pX2, int32_t *pY2) {
  int32_t x = 0, y = 0, x1, y1, x2, y2;
  uint32_t accept, code1, code2, code_out;

  x1 = *pX1;
  y1 = *pY1;
  x2 = *pX2;
  y2 = *pY2;

  accept = 0;
  // Compute region codes for P1, P2
  code1 = compute_code(x1, y1);
  code2 = compute_code(x2, y2);

  while (1) {
    if ((code1 == 0) && (code2 == 0)) {
      // If both endpoints lie within rectangle
      accept = 1;
      break;
    } else if (code1 & code2) {
      // If both endpoints are outside rectangle,
      // in same region
      break;
    } else {
      // Some segment of line lies within the
      // rectangle
      // At least one endpoint is outside the
      // rectangle, pick it.
      if (code1 != 0) {
        code_out = code1;
      } else {
        code_out = code2;
      }

      // Find intersection point;
      // using formulas y = y1 + slope * (x - x1),
      // x = x1 + (1 / slope) * (y - y1)
      if (code_out & TOP) {
        // point is above the clip rectangle
        x = x1 + (x2 - x1) * (s_clipy_max - y1) / (y2 - y1);
        y = s_clipy_max;
      } else if (code_out & BOTTOM) {
        // point is below the rectangle
        x = x1 + (x2 - x1) * (s_clipy_min - y1) / (y2 - y1);
        y = s_clipy_min;
      } else if (code_out & RIGHT) {
        // point is to the right of rectangle
        y = y1 + (y2 - y1) * (s_clipx_max - x1) / (x2 - x1);
        x = s_clipx_max;
      } else if (code_out & LEFT) {
        // point is to the left of rectangle
        y = y1 + (y2 - y1) * (s_clipx_min - x1) / (x2 - x1);
        x = s_clipx_min;
      }

      // Now intersection point x, y is found
      // We replace point outside rectangle
      // by intersection point
      if (code_out == code1) {
        x1 = x;
        y1 = y;
        code1 = compute_code(x1, y1);
      } else {
        x2 = x;
        y2 = y;
        code2 = compute_code(x2, y2);
      }
    }
  }
  *pX1 = x1;
  *pY1 = y1;
  *pX2 = x2;
  *pY2 = y2;
  return accept;
}
//
// Add chunk of data with X, Y and RGB info to buffer
//
void add_chunk(int32_t x, int32_t y, int r, int g, int b) {
  int32_t x0, y0, x1, y1;
  //float x0, y0, x1, y1;
  uint32_t add;

  x0 = s_in_vec_last_x;
  y0 = s_in_vec_last_y;
  x1 = x;
  y1 = y;

  y1 *= Y_SCALE_FACTOR;
  y1 += Y_ADD;
  x1 *= X_SCALE_FACTOR;
  x1 += X_ADD;

  uint32_t blank = (r == 0) && (g == 0) && (b == 0);
  // Don't include blank vectors.  We will add them again (see reconnect_vectors()) before sending.
  if (!blank) {
    if (s_in_vec_cnt < MAX_PTS) {
      add = line_clip(&x0, &y0, &x1, &y1);
      if (add) {
        if (s_in_vec_cnt) {
          s_in_vec_list[s_in_vec_cnt - 1].nextChunk = &s_in_vec_list[s_in_vec_cnt];
        }
        s_in_vec_list[s_in_vec_cnt].x0 = x0;
        s_in_vec_list[s_in_vec_cnt].y0 = y0;
        s_in_vec_list[s_in_vec_cnt].x1 = x1;
        s_in_vec_list[s_in_vec_cnt].y1 = y1;
        s_in_vec_list[s_in_vec_cnt].r = r;
        s_in_vec_list[s_in_vec_cnt].g = g;
        s_in_vec_list[s_in_vec_cnt].b = b;
        s_in_vec_cnt++;
      }
    } else {
      draw_chunks();     // draw buffer of pre-calculated vectors
      s_in_vec_cnt = 0;  // and start again at the beginning of the buffer to draw more vectors
    }
  }

  s_in_vec_last_x = x1;
  s_in_vec_last_y = y1;
}
//
// add a single vector to a buffer to draw later
//
void _vector(int x1, int y1, int x2, int y2) {

  DataChunk_t localChunk;

  add_chunk(x1, y1, 0, 0, 0);
  localChunk = change_colour(&localChunk, vector_color);
  add_chunk(x2, y2, localChunk.r, localChunk.g, localChunk.b);
}
//
// draw a single ladder at given y, x position of given height in pixels
//
/*
void draw_ladder(int y, int x, int h) {
  vector_color = CYN;

  int hy = h + y;
  int x1 = x + 8;
  int y1;

  _vector(x, y, x, hy);
  _vector(x1, y, x1, hy);

  y += 2;

  for (int i = 0; i < (h - 2); i += 4) {  // draw rung every 4th pixel (skipping 2 pixels at bottom)
    y1 = y + i;
    _vector(x, y1, x1, y1);
  }
}*/

void draw_bonusbox() {
  // Cyan
  int x, y, w, h;
  x = 170;
  y = 202;
  w = 42;
  h = 20;
  add_chunk(x + 4, y + h, 0, 0, 0);
  add_chunk(x, y + h, 0x14, 0xf3, 0xff);
  add_chunk(x, y, 0x14, 0xf3, 0xff);
  add_chunk(x + w, y, 0x14, 0xf3, 0xff);
  add_chunk(x + w, y + h, 0x14, 0xf3, 0xff);
  add_chunk(x + w - 4, y + h, 0x14, 0xf3, 0xff);
  // Magenta
  x = 173;
  y = 206;
  w = 35;
  h = 12;
  add_chunk(x, y + h, 0, 0, 0);
  add_chunk(x, y, 0xFF, 0x00, 0xff);
  add_chunk(x + w, y, 0xFF, 0x00, 0xff);
  add_chunk(x + w, y + h, 0xFF, 0x00, 0xff);
  add_chunk(x, y + h, 0xFF, 0x00, 0xff);
}

void draw_pauline(int _x) {
  int _y = 235 - read(0x6903, 0, 0);
  if (read(0x6905, 0, 0) != 17 && read(0x6a20, 0, 0))
    _y += 3;  // Pauline jumps when heart not showing
  draw_object(vector_library2, 0x2b, _x, _y, MAG, 0, 0);
}
//
// Some girder tiles are a bit lower than others, so reduce height a bit if necessary
//
int adjust_tile_height(uint8_t j) {
  int adjustment = 0;

  if (j == 0xd1 || j == 0xf1 || j == 0xc2 || j == 0xe2) {
    adjustment = -1;
  } else if (j == 0xd2 || j == 0xf2 || j == 0xc3 || j == 0xe3) {
    adjustment = -2;
  } else if (j == 0xd3 || j == 0xf3 || j == 0xc4 || j == 0xe4) {
    adjustment = -3;
  } else if (j == 0xd4 || j == 0xf4 || j == 0xc5 || j == 0xe5) {
    adjustment = -4;
  } else if (j == 0xd5 || j == 0xf5 || j == 0xc6 || j == 0xe6) {
    adjustment = -5;
  } else if (j == 0xd6 || j == 0xf6 || j == 0xc7 || j == 0xe7) {
    adjustment = -6;
  } else if (j == 0xd7 || j == 0xf7 || j == 0xb8) {
    adjustment = -7;
  }

  return adjustment;
}
//
// Output vector characters based on contents of video ram ($7400-77ff)
//
void draw_vector_characters() {
  int _addr = VRAM_TR;
  int _x, _y;
  int xtile, ytile;  // Coordinates of tile
  uint8_t j;
  int i;                        // General counter
  int tiles[28][32];            // Array of XY coordinates containing positions of girders and ladders
  static int oldtiles[28][32];  // Previous array of tiles used to avoid needless redraws
  int GirEndY;
  bool LadStart = false;   // Flag is true when we're currently identify start and end points of a ladder
  int LadStartY, LadEndY;  // Start and end coordinates of a ladder
  int LadStartX;
  bool different = false;


  // PERHAPS THESE 3 FOR LOOPS (OR AT LEAST 2) COULD BE COMBINED INTO ONE AT THE END WHEN EVERYTHING IS WORKING


  // Transform the tile RAM space into a 2D XY coordinate matrix
  // and check if anything needs redrawing since last execution
  for (_x = 223, xtile = 27; _x >= 0; _x -= 8, xtile--) {
    for (_y = 255, ytile = 31; _y >= 0; _y -= 8, ytile--) {
      j = read_u8(_addr);
      tiles[xtile][ytile] = j;
      if (j != oldtiles[xtile][ytile])  // A change has been made to the screen since last execution
        different = true;
      _addr++;
    }
  }
  // Need to keep a copy of these vectors and reinject them into the buffer if no changes made
  // Count the vectors in order to allocate just enough memory for this
  if (different == false) {                                                      // If none of the tiles have changed there's no need to go any further
    memcpy(s_in_vec_list, s_tmp_vec_list, s_tmp_vec_cnt * sizeof(DataChunk_t));  // Copy the old vectors back into the buffer
    s_in_vec_cnt = s_tmp_vec_cnt;
    return;
  }

  // Examine each tile of video ram, and identify girders and ladders along the way to be drawn separately
  // Girders are only drawn horizontally, so we can go down the screen line by line to identify the start and end points
  for (_y = 255, ytile = 31; _y >= 0; _y -= 8, ytile--) {
    for (_x = 0, xtile = 0; _x < 224; _x += 8, xtile++) {
      j = tiles[xtile][ytile];
      oldtiles[xtile][ytile] = j;  // keep a copy of what has just been drawn to avoid pointless redraws next time

      if ((j >= 0xd0 && j <= 0xd7) || (j >= 0xf0 && j <= 0xf7) || (j >= 0xb0 && j <= 0xb8) || (j >= 0xc2 && j <= 0xc7) || (j >= 0xe1 && j <= 0xe7)) {  // We've found a bit of girder
        GirEndY = _y + adjust_tile_height(j);                                                                                                          // Some girder tiles are a bit lower than others, so reduce height a bit
        add_chunk(_x, GirEndY, 0, 0, 0);                                                                                                               // Move to start point of girder
        add_chunk(_x + GW, GirEndY, 0xa0, 0, 0x60);                                                                                                    // Draw to end point of girder chunk in magenta
      }
      // Draw everything except girders, ladders and bonus points box
      // These will be drawn elsewhere
      if (!(j >= 0xc0 & j <= 0xc5) && !(j >= 0xd0 & j <= 0xd7) && !(j >= 0xf0 & j <= 0xf7) && !(j >= 0xc2 & j <= 0xc7) && !(j >= 0xe1 & j <= 0xe7) && !(j >= 0xb0 & j <= 0xb8) && !(j >= 0x7a & j <= 0x7f) && !(j >= 0x8c & j <= 0x8f))
        draw_object(vector_library, j, _x, _y, vector_color, 0, 0);
    }
  }

  for (_x = 0, xtile = 0; _x < 224; _x += 8, xtile++) {
    for (_y = 255, ytile = 31; _y >= 0; _y -= 8, ytile--) {
      j = tiles[xtile][ytile];
      int height = _y - 5;
      if (j == 0xc0 || j == 0xc1 || j == 0xc2 || j == 0xc3 || j == 0xc4 || j == 0xc5 || j == 0xd4) {  // We've found a bit of ladder
        add_chunk(_x, height, 0, 0, 0);                                                               // Move to start point of ladder
        add_chunk(_x, height + GH, 0x14, 0xf3, 0xff);                                                 // Draw to end point of ladder chunk in cyan
        add_chunk(_x, height + GH - 4, 0x14, 0xf3, 0xff);
        add_chunk(_x + GW, height + GH - 4, 0x14, 0xf3, 0xff);
        add_chunk(_x + GW, height + GH, 0x14, 0xf3, 0xff);
        add_chunk(_x + GW, height, 0x14, 0xf3, 0xff);
      }
    }
  }

  // Keep backup copy of vectors for comparison later to avoid pointless redraws
  memcpy(s_tmp_vec_list, s_in_vec_list, s_in_vec_cnt * sizeof(DataChunk_t));
  s_tmp_vec_cnt = s_in_vec_cnt;

  // emu_printf("draw_vector_characters, after drawing ladders");
  // emu_printi(s_in_vec_cnt);
}
//
// General functions
//
uint8_t read_u8(int address) {
  return cpu_readmem16(address);
}

uint32_t read_direct_u32(int address) {
  uint32_t ret, ret1, ret2, ret3, result;
  ret = cpu_readmem16(address);  // there is definitely a better way of doing this!
  ret1 = cpu_readmem16(address + 1);
  ret2 = cpu_readmem16(address + 2);
  ret3 = cpu_readmem16(address + 3);

  result = ret + (ret1 * 256) + (ret2 * 256 * 256) + (ret3 * 256 * 256 * 256);

  return result;
}

void write_direct_u32(int address, int value) {
  cpu_writemem29_dword(address, value);  // this only seems to work for 24 or 29 bits, so may need specially written routine, or else use 2 16 bit words
}

void write_u8(int address, int value) {
  cpu_writemem16(address, value);  // this should only be for 8 bits not 16 so could be causing bugs in game}
}

int read(int address, int equal_from, int to) {
  // return data from memory address, or boolean when equal or to & from values are provided
  int _d = read_u8(address);
  // this is probably more longwinded than it needs to be
  if (to != 0 && equal_from != 0) {
    if (_d >= equal_from && _d <= to)
      return 1;
    else
      return 0;
  } else if (equal_from != 0) {
    if (_d == equal_from)
      return 1;
    else
      return 0;
  } else
    return _d;
}

void write(int address, int value) {
  write_u8(address, value);
}

// END OF VSTCM SPECIFIC ROUTINES

void showdisclaimer(void) /* MAURY_BEGIN: dichiarazione */
{
  printf("MAME is an emulator: it reproduces, more or less faithfully, the behaviour of\n"
         "several arcade machines. But hardware is useless without software, so an image\n"
         "of the ROMs which run on that hardware is required. Such ROMs, like any other\n"
         "commercial software, are copyrighted material and it is therefore illegal to\n"
         "use them if you don't own the original arcade machine. Needless to say, ROMs\n"
         "are not distributed together with MAME. Distribution of MAME together with ROM\n"
         "images is a violation of copyright law and should be promptly reported to the\n"
         "authors so that appropriate legal action can be taken.\n\n");
} /* MAURY_END: dichiarazione */


/***************************************************************************

  Read ROMs into memory.

  Arguments:
  const struct RomModule *romp - pointer to an array of Rommodule structures,
                                 as defined in common.h.

***************************************************************************/
int readroms(void) {
  int region;
  const struct RomModule *romp;
  int checksumwarning = 0;
  int lengthwarning = 0;


  romp = Machine->gamedrv->rom;

  for (region = 0; region < MAX_MEMORY_REGIONS; region++)
    Machine->memory_region[region] = 0;

  region = 0;

  while (romp->name || romp->offset || romp->length) {
    unsigned int region_size;
    const char *name;

    /* Mish:  An 'optional' rom region, only loaded if sound emulation is turned on */
    if (Machine->sample_rate == 0 && (romp->offset & ROMFLAG_IGNORE)) {
      if (errorlog) fprintf(errorlog, "readroms():  Ignoring rom region %d\n", region);
      region++;

      romp++;
      while (romp->name || romp->length)
        romp++;

      continue;
    }

    if (romp->name || romp->length) {
      printf("Error in RomModule definition: expecting ROM_REGION\n");
      goto getout;
    }

    region_size = romp->offset & ~ROMFLAG_MASK;
    if ((Machine->memory_region[region] = malloc(region_size)) == 0) {
      printf("readroms():  Unable to allocate %d bytes of RAM\n", region_size);
      goto getout;
    }
    Machine->memory_region_length[region] = region_size;

    /* some games (i.e. Pleiades) want the memory clear on startup */
    memset(Machine->memory_region[region], 0, region_size);

    romp++;

    while (romp->length) {
      void *f;
      int expchecksum = romp->crc;
      int explength = 0;


      if (romp->name == 0) {
        printf("Error in RomModule definition: ROM_CONTINUE not preceded by ROM_LOAD\n");
        goto getout;
      } else if (romp->name == (char *)-1) {
        printf("Error in RomModule definition: ROM_RELOAD not preceded by ROM_LOAD\n");
        goto getout;
      }

      name = romp->name;
      f = osd_fopen(Machine->gamedrv->name, name, OSD_FILETYPE_ROM, 0);
      if (f == 0 && Machine->gamedrv->clone_of) {
        /* if the game is a clone, try loading the ROM from the main version */
        f = osd_fopen(Machine->gamedrv->clone_of->name, name, OSD_FILETYPE_ROM, 0);
      }
      if (f == 0) {
        /* NS981003: support for "load by CRC" */
        char crc[9];

        sprintf(crc, "%08x", romp->crc);
        f = osd_fopen(Machine->gamedrv->name, crc, OSD_FILETYPE_ROM, 0);
        if (f == 0 && Machine->gamedrv->clone_of) {
          /* if the game is a clone, try loading the ROM from the main version */
          f = osd_fopen(Machine->gamedrv->clone_of->name, crc, OSD_FILETYPE_ROM, 0);
        }
      }
      if (f == 0) {
        fprintf(stderr, "Unable to open ROM %s\n", name);
        goto printromlist;
      }

      do {
        unsigned char *c;
        unsigned int i;
        int length = romp->length & ~ROMFLAG_MASK;


        if (romp->name == (char *)-1)
          osd_fseek(f, 0, SEEK_SET); /* ROM_RELOAD */
        else
          explength += length;

        if (romp->offset + length > region_size) {
          printf("Error in RomModule definition: %s out of memory region space\n", name);
          osd_fclose(f);
          goto getout;
        }

        if (romp->length & ROMFLAG_ALTERNATE) {
          /* ROM_LOAD_EVEN and ROM_LOAD_ODD */
          unsigned char *temp;


          temp = malloc(length);

          if (!temp) {
            printf("Out of memory reading ROM %s\n", name);
            osd_fclose(f);
            goto getout;
          }

          if (osd_fread(f, temp, length) != length) {
            printf("Unable to read ROM %s\n", name);
            free(temp);
            osd_fclose(f);
            goto printromlist;
          }

          /* copy the ROM data */
#ifdef LSB_FIRST
          c = Machine->memory_region[region] + (romp->offset ^ 1);
#else
          c = Machine->memory_region[region] + romp->offset;
#endif

          for (i = 0; i < length; i += 2) {
            c[i * 2] = temp[i];
            c[i * 2 + 2] = temp[i + 1];
          }

          free(temp);
        } else {
          int wide = romp->length & ROMFLAG_WIDE;
#ifdef LSB_FIRST
          int swap = (romp->length & ROMFLAG_SWAP) ^ ROMFLAG_SWAP;
#else
          int swap = romp->length & ROMFLAG_SWAP;
#endif

          osd_fread(f, Machine->memory_region[region] + romp->offset, length);

          /* apply swappage */
          c = Machine->memory_region[region] + romp->offset;
          if (wide && swap) {
            for (i = 0; i < length; i += 2) {
              int temp = c[i];
              c[i] = c[i + 1];
              c[i + 1] = temp;
            }
          }
        }

        romp++;
      } while (romp->length && (romp->name == 0 || romp->name == (char *)-1));

      if (explength != osd_fsize(f)) {
        printf("Length mismatch on ROM '%s'. (Expected: %08x  Found: %08x)\n",
               name, explength, osd_fsize(f));
        lengthwarning++;
      }



      //if (expchecksum != osd_fcrc (f))
      //{
      //	if (checksumwarning == 0)
      //		printf ("The checksum of some ROMs does not match that of the ones MAME was tested with.\n"
      //				"WARNING: the game might not run correctly.\n"
      //				"Name         Expected  Found\n");
      //	checksumwarning++;
      //	if (expchecksum)
      //		printf("%-12s %08x %08x\n", name, expchecksum, osd_fcrc (f));
      //	else
      //		printf("%-12s NO GOOD DUMP EXISTS\n",name);
      //}

      osd_fclose(f);
    }

    region++;
  }


  return 0;


printromlist:

  printromlist(Machine->gamedrv->rom, Machine->gamedrv->name);
  exit(0);

getout:
  for (region = 0; region < MAX_MEMORY_REGIONS; region++) {
    free(Machine->memory_region[region]);
    Machine->memory_region[region] = 0;
  }
  return 1;
}


void printromlist(const struct RomModule *romp, const char *basename) {
  printf("This is the list of the ROMs required for driver \"%s\".\n"
         "Name              Size       Checksum\n",
         basename);
  while (romp->name || romp->offset || romp->length) {
    romp++; /* skip memory region definition */

    while (romp->length) {
      const char *name;
      int length, expchecksum;


      name = romp->name;
      expchecksum = romp->crc;

      length = 0;

      do {
        /* ROM_RELOAD */
        if (romp->name == (char *)-1)
          length = 0; /* restart */

        length += romp->length & ~ROMFLAG_MASK;

        romp++;
      } while (romp->length && (romp->name == 0 || romp->name == (char *)-1));

      printf("%-12s  %7d bytes  %08x\n", name, length, expchecksum);
    }
  }
}


/***************************************************************************

  Read samples into memory.
  This function is different from readroms() because it doesn't fail if
  it doesn't find a file: it will load as many samples as it can find.

***************************************************************************/
struct GameSamples *readsamples(const char **samplenames, const char *basename)
/* V.V - avoids samples duplication */
/* if first samplename is *dir, looks for samples into "basename" first, then "dir" */
{
  int i;
  struct GameSamples *samples;
  int skipfirst = 0;

  if (samplenames == 0 || samplenames[0] == 0) return 0;

  if (samplenames[0][0] == '*')
    skipfirst = 1;

  i = 0;
  while (samplenames[i + skipfirst] != 0) i++;

  if ((samples = emu_Malloc(sizeof(struct GameSamples) + (i - 1) * sizeof(struct GameSample))) == 0) {
    emu_printf("sound memory allocation failed");
    return 0;
  }

  samples->total = i;
  for (i = 0; i < samples->total; i++)
    samples->sample[i] = 0;

  for (i = 0; i < samples->total; i++) {
    void *f;
    char buf[100];


    if (samplenames[i + skipfirst][0]) {
      if ((f = osd_fopen(basename, samplenames[i + skipfirst], OSD_FILETYPE_SAMPLE, 0)) == 0)
        if (skipfirst)
          f = osd_fopen(samplenames[0] + 1, samplenames[i + skipfirst], OSD_FILETYPE_SAMPLE, 0);
      if (f != 0) {
        if (osd_fseek(f, 0, SEEK_END) == 0) {
          int dummy;
          unsigned char smpvol = 0, smpres = 0;
          unsigned smplen = 0, smpfrq = 0;

          osd_fseek(f, 0, SEEK_SET);
          osd_fread(f, buf, 4);
          if (memcmp(buf, "MAME", 4) == 0) {
            osd_fread(f, &smplen, 4); /* all datas are LITTLE ENDIAN */
            osd_fread(f, &smpfrq, 4);
            smplen = intelLong(smplen); /* so convert them in the right endian-ness */
            smpfrq = intelLong(smpfrq);
            osd_fread(f, &smpres, 1);
            osd_fread(f, &smpvol, 1);
            osd_fread(f, &dummy, 2);
            if ((smplen != 0) && (samples->sample[i] = malloc(sizeof(struct GameSample) + (smplen) * sizeof(char))) != 0) {
              samples->sample[i]->length = smplen;
              emu_printf("sample length");
              emu_printi(smplen);
              samples->sample[i]->volume = smpvol;
              samples->sample[i]->smpfreq = smpfrq;
              samples->sample[i]->resolution = smpres;
              osd_fread(f, samples->sample[i]->data, smplen);
            }
          }
        }

        osd_fclose(f);
      }
    }
  }

  return samples;
}


void freesamples(struct GameSamples *samples) {
  int i;


  if (samples == 0) return;

  for (i = 0; i < samples->total; i++)
    free(samples->sample[i]);

  free(samples);
}


/* LBO 042898 - added coin counters */
void coin_counter_w(int offset, int data) {
  if (offset >= COIN_COUNTERS) return;
  /* Count it only if the data has changed from 0 to non-zero */
  if (data && (lastcoin[offset] == 0)) {
    coins[offset]++;
  }
  lastcoin[offset] = data;
}

void coin_lockout_w(int offset, int data) {
  if (offset >= COIN_COUNTERS) return;

  coinlockedout[offset] = data;
}

/* Locks out all the coin inputs */
void coin_lockout_global_w(int offset, int data) {
  int i;

  for (i = 0; i < COIN_COUNTERS; i++) {
    coin_lockout_w(i, data);
  }
}

/***************************************************************************

  Function to convert the information stored in the graphic roms into a
  more usable format.

  Back in the early '80s, arcade machines didn't have the memory or the
  speed to handle bitmaps like we do today. They used "character maps",
  instead: they had one or more sets of characters (usually 8x8 pixels),
  which would be placed on the screen in order to form a picture. This was
  very fast: updating a character mapped display is, rougly speaking, 64
  times faster than updating an equivalent bitmap display, since you only
  modify groups of 8x8 pixels and not the single pixels. However, it was
  also much less versatile than a bitmap screen, since with only 256
  characters you had to do all of your graphics. To overcome this
  limitation, some special hardware graphics were used: "sprites". A sprite
  is essentially a bitmap, usually larger than a character, which can be
  placed anywhere on the screen (not limited to character boundaries) by
  just telling the hardware the coordinates. Moreover, sprites can be
  flipped along the major axis just by setting the appropriate bit (some
  machines can flip characters as well). This saves precious memory, since
  you need only one copy of the image instead of four.

  What about colors? Well, the early machines had a limited palette (let's
  say 16-32 colors) and each character or sprite could not use all of them
  at the same time. Characters and sprites data would use a limited amount
  of bits per pixel (typically 2, which allowed them to address only four
  different colors). You then needed some way to tell to the hardware which,
  among the available colors, were the four colors. This was done using a
  "color attribute", which typically was an index for a lookup table.

  OK, after this brief and incomplete introduction, let's come to the
  purpose of this section: how to interpret the data which is stored in
  the graphic roms. Unfortunately, there is no easy answer: it depends on
  the hardware. The easiest way to find how data is encoded, is to start by
  making a bit by bit dump of the rom. You will usually be able to
  immediately recognize some pattern: if you are lucky, you will see
  letters and numbers right away, otherwise you will see something which
  looks like letters and numbers, but with halves switched, dilated, or
  something like that. You'll then have to find a way to put it all
  together to obtain our standard one byte per pixel representation. Two
  things to remember:
  - keep in mind that every pixel has typically two (or more) bits
    associated with it, and they are not necessarily near to each other.
  - characters might be rotated 90 degrees. That's because many games used a
    tube rotated 90 degrees. Think how your monitor would look like if you
	put it on its side ;-)

  After you have successfully decoded the characters, you have to decode
  the sprites. This is usually more difficult, because sprites are larger,
  maybe have more colors, and are more difficult to recognize when they are
  messed up, since they are pure graphics without letters and numbers.
  However, with some work you'll hopefully be able to work them out as
  well. As a rule of thumb, the sprites should be encoded in a way not too
  dissimilar from the characters.

***************************************************************************/
static int readbit(const unsigned char *src, int bitnum) {
  return (src[bitnum / 8] >> (7 - bitnum % 8)) & 1;
}


void decodechar(struct GfxElement *gfx, int num, const unsigned char *src, const struct GfxLayout *gl) {
  int plane, x, y;
  unsigned char *dp;



  for (plane = 0; plane < gl->planes; plane++) {
    int offs;


    offs = num * gl->charincrement + gl->planeoffset[plane];

    for (y = 0; y < gfx->height; y++) {
      dp = gfx->gfxdata->line[num * gfx->height + y];

      for (x = 0; x < gfx->width; x++) {
        int xoffs, yoffs;


        if (plane == 0) dp[x] = 0;
        else dp[x] <<= 1;

        xoffs = x;
        yoffs = y;

        if (Machine->orientation & ORIENTATION_FLIP_X)
          xoffs = gfx->width - 1 - xoffs;

        if (Machine->orientation & ORIENTATION_FLIP_Y)
          yoffs = gfx->height - 1 - yoffs;

        if (Machine->orientation & ORIENTATION_SWAP_XY) {
          int temp;


          temp = xoffs;
          xoffs = yoffs;
          yoffs = temp;
        }

        dp[x] += readbit(src, offs + gl->yoffset[yoffs] + gl->xoffset[xoffs]);
      }
    }
  }


  if (gfx->pen_usage) {
    /* fill the pen_usage array with info on the used pens */
    gfx->pen_usage[num] = 0;

    for (y = 0; y < gfx->height; y++) {
      dp = gfx->gfxdata->line[num * gfx->height + y];

      for (x = 0; x < gfx->width; x++) {
        gfx->pen_usage[num] |= 1 << dp[x];
      }
    }
  }
}


struct GfxElement *decodegfx(const unsigned char *src, const struct GfxLayout *gl) {
  int c;
  struct osd_bitmap *bm;
  struct GfxElement *gfx;


  if ((gfx = malloc(sizeof(struct GfxElement))) == 0)
    return 0;

  if (Machine->orientation & ORIENTATION_SWAP_XY) {
    gfx->width = gl->height;
    gfx->height = gl->width;

    if ((bm = osd_create_bitmap(gl->total * gfx->height, gfx->width)) == 0) {
      free(gfx);
      return 0;
    }
  } else {
    gfx->width = gl->width;
    gfx->height = gl->height;

    if ((bm = osd_create_bitmap(gfx->width, gl->total * gfx->height)) == 0) {
      free(gfx);
      return 0;
    }
  }

  gfx->total_elements = gl->total;
  gfx->color_granularity = 1 << gl->planes;
  gfx->gfxdata = bm;

  gfx->pen_usage = 0;               /* need to make sure this is NULL if the next test fails) */
  if (gfx->color_granularity <= 32) /* can't handle more than 32 pens */
    gfx->pen_usage = malloc(gfx->total_elements * sizeof(int));
  /* no need to check for failure, the code can work without pen_usage */

  for (c = 0; c < gl->total; c++)
    decodechar(gfx, c, src, gl);

  return gfx;
}


void freegfx(struct GfxElement *gfx) {
  if (gfx) {
    free(gfx->pen_usage);
    osd_free_bitmap(gfx->gfxdata);
    free(gfx);
  }
}


/***************************************************************************

  Draw graphic elements in the specified bitmap.

  transparency == TRANSPARENCY_NONE - no transparency.
  transparency == TRANSPARENCY_PEN - bits whose _original_ value is == transparent_color
                                     are transparent. This is the most common kind of
									 transparency.
  transparency == TRANSPARENCY_PENS - as above, but transparent_color is a mask of
  									 transparent pens.
  transparency == TRANSPARENCY_COLOR - bits whose _remapped_ value is == Machine->pens[transparent_color]
                                     are transparent. This is used by e.g. Pac Man.
  transparency == TRANSPARENCY_THROUGH - if the _destination_ pixel is == transparent_color,
                                     the source pixel is drawn over it. This is used by
									 e.g. Jr. Pac Man to draw the sprites when the background
									 has priority over them.

***************************************************************************/
/* ASG 971011 -- moved this into a "core" function */
/* ASG 980209 -- changed to drawgfx_core8 */
static void drawgfx_core8(struct osd_bitmap *dest, const struct GfxElement *gfx,
                          unsigned int code, unsigned int color, int flipx, int flipy, int sx, int sy,
                          const struct rectangle *clip, int transparency, int transparent_color, int dirty) {
  // emu_printf("drawgfx_core8");
  int ox, oy, ex, ey, y, start, dy;
  const unsigned char *sd;
  unsigned char *bm, *bme;
  int col;
  int *sd4;
  int trans4, col4;
  struct rectangle myclip;


  if (!gfx) return;

  code %= gfx->total_elements;
  color %= gfx->total_colors;

  /* if necessary, remap the transparent color */
  if (transparency == TRANSPARENCY_COLOR)
    transparent_color = Machine->pens[transparent_color];

  if (gfx->pen_usage) {
    int transmask;


    transmask = 0;

    if (transparency == TRANSPARENCY_PEN) {
      transmask = 1 << transparent_color;
    } else if (transparency == TRANSPARENCY_PENS) {
      transmask = transparent_color;
    } else if (transparency == TRANSPARENCY_COLOR && gfx->colortable) {
      int i;
      const unsigned short *paldata;


      paldata = &gfx->colortable[gfx->color_granularity * color];

      for (i = gfx->color_granularity - 1; i >= 0; i--) {
        if (paldata[i] == transparent_color)
          transmask |= 1 << i;
      }
    }

    if ((gfx->pen_usage[code] & ~transmask) == 0)
      /* character is totally transparent, no need to draw */
      return;
    else if ((gfx->pen_usage[code] & transmask) == 0 && transparency != TRANSPARENCY_THROUGH)
      /* character is totally opaque, can disable transparency */
      transparency = TRANSPARENCY_NONE;
  }

  if (Machine->orientation & ORIENTATION_SWAP_XY) {
    int temp;

    temp = sx;
    sx = sy;
    sy = temp;

    temp = flipx;
    flipx = flipy;
    flipy = temp;

    if (clip) {
      /* clip and myclip might be the same, so we need a temporary storage */
      temp = clip->min_x;
      myclip.min_x = clip->min_y;
      myclip.min_y = temp;
      temp = clip->max_x;
      myclip.max_x = clip->max_y;
      myclip.max_y = temp;
      clip = &myclip;
    }
  }
  if (Machine->orientation & ORIENTATION_FLIP_X) {
    sx = dest->width - gfx->width - sx;
    if (clip) {
      int temp;


      /* clip and myclip might be the same, so we need a temporary storage */
      temp = clip->min_x;
      myclip.min_x = dest->width - 1 - clip->max_x;
      myclip.max_x = dest->width - 1 - temp;
      myclip.min_y = clip->min_y;
      myclip.max_y = clip->max_y;
      clip = &myclip;
    }
  }
  if (Machine->orientation & ORIENTATION_FLIP_Y) {
    sy = dest->height - gfx->height - sy;
    if (clip) {
      int temp;


      myclip.min_x = clip->min_x;
      myclip.max_x = clip->max_x;
      /* clip and myclip might be the same, so we need a temporary storage */
      temp = clip->min_y;
      myclip.min_y = dest->height - 1 - clip->max_y;
      myclip.max_y = dest->height - 1 - temp;
      clip = &myclip;
    }
  }


  /* check bounds */
  ox = sx;
  oy = sy;
  ex = sx + gfx->width - 1;
  if (sx < 0) sx = 0;
  if (clip && sx < clip->min_x) sx = clip->min_x;
  if (ex >= dest->width) ex = dest->width - 1;
  if (clip && ex > clip->max_x) ex = clip->max_x;
  if (sx > ex) return;
  ey = sy + gfx->height - 1;
  if (sy < 0) sy = 0;
  if (clip && sy < clip->min_y) sy = clip->min_y;
  if (ey >= dest->height) ey = dest->height - 1;
  if (clip && ey > clip->max_y) ey = clip->max_y;
  if (sy > ey) return;

  if (dirty)
    osd_mark_dirty(sx, sy, ex, ey, 0); /* ASG 971011 */

  /* start = code * gfx->height; */
  if (flipy) /* Y flop */
  {
    start = code * gfx->height + gfx->height - 1 - (sy - oy);
    dy = -1;
  } else /* normal */
  {
    start = code * gfx->height + (sy - oy);
    dy = 1;
  }

  /*emu_printf("sx");
  emu_printi(sx);
  emu_printf("sy");
  emu_printi(sy);
*/
  /*   int zx, zy;
    zx = sx * X_SCALE_FACTOR;
    zy = sy * Y_SCALE_FACTOR;
    zx += X_ADD;
    zy += Y_ADD;*/

  // VSTCM test, draw a small box
  /*    draw_moveto(zx, zy);
  draw_to_xy(zx + 20, zy);
  draw_to_xy(zx + 20, zy + 50);
  draw_to_xy(zx, zy + 20);
  draw_to_xy(zx, zy);
*/
  /*  vector_color = GRN;

  _vector(zx, zy, zx + 20, zy);
  _vector(zx + 20, zy + 50, zx, zy + 20);
  _vector(zx, zy + 20, zx, zy);
*/
  if (gfx->colortable) /* remap colors */
  {
    const unsigned short *paldata; /* ASG 980209 */

    paldata = &gfx->colortable[gfx->color_granularity * color];

    switch (transparency) {
      case TRANSPARENCY_NONE:
        if (flipx) /* X flip */
        {
          for (y = sy; y <= ey; y++) {
            bm = dest->line[y];
            bme = bm + ex;
            sd = gfx->gfxdata->line[start] + gfx->width - 1 - (sx - ox);
            for (bm += sx; bm <= bme - 7; bm += 8) {
              sd -= 8;
              bm[0] = paldata[sd[8]];
              bm[1] = paldata[sd[7]];
              bm[2] = paldata[sd[6]];
              bm[3] = paldata[sd[5]];
              bm[4] = paldata[sd[4]];
              bm[5] = paldata[sd[3]];
              bm[6] = paldata[sd[2]];
              bm[7] = paldata[sd[1]];
            }
            for (; bm <= bme; bm++)
              *bm = paldata[*(sd--)];
            start += dy;
          }
        } else /* normal */
        {
          for (y = sy; y <= ey; y++) {
            bm = dest->line[y];
            bme = bm + ex;
            sd = gfx->gfxdata->line[start] + (sx - ox);
            for (bm += sx; bm <= bme - 7; bm += 8) {
              bm[0] = paldata[sd[0]];
              bm[1] = paldata[sd[1]];
              bm[2] = paldata[sd[2]];
              bm[3] = paldata[sd[3]];
              bm[4] = paldata[sd[4]];
              bm[5] = paldata[sd[5]];
              bm[6] = paldata[sd[6]];
              bm[7] = paldata[sd[7]];
              sd += 8;
            }
            for (; bm <= bme; bm++)
              *bm = paldata[*(sd++)];
            start += dy;
          }
        }
        break;

      case TRANSPARENCY_PEN:
        trans4 = transparent_color * 0x01010101;
        if (flipx) /* X flip */
        {
          for (y = sy; y <= ey; y++) {
            bm = dest->line[y];
            bme = bm + ex;
            sd4 = (int *)(gfx->gfxdata->line[start] + gfx->width - 1 - (sx - ox) - 3);
            for (bm += sx; bm <= bme - 3; bm += 4) {
              if ((col4 = read_dword(sd4)) != trans4) {
                col = (col4 >> 24) & 0xff;
                if (col != transparent_color) bm[BL0] = paldata[col];
                col = (col4 >> 16) & 0xff;
                if (col != transparent_color) bm[BL1] = paldata[col];
                col = (col4 >> 8) & 0xff;
                if (col != transparent_color) bm[BL2] = paldata[col];
                col = col4 & 0xff;
                if (col != transparent_color) bm[BL3] = paldata[col];
              }
              sd4--;
            }
            sd = (unsigned char *)sd4 + 3;
            for (; bm <= bme; bm++) {
              col = *(sd--);
              if (col != transparent_color) *bm = paldata[col];
            }
            start += dy;
          }
        } else /* normal */
        {
          for (y = sy; y <= ey; y++) {
            bm = dest->line[y];
            bme = bm + ex;
            sd4 = (int *)(gfx->gfxdata->line[start] + (sx - ox));
            for (bm += sx; bm <= bme - 3; bm += 4) {
              if ((col4 = read_dword(sd4)) != trans4) {
                col = col4 & 0xff;
                if (col != transparent_color) bm[BL0] = paldata[col];
                col = (col4 >> 8) & 0xff;
                if (col != transparent_color) bm[BL1] = paldata[col];
                col = (col4 >> 16) & 0xff;
                if (col != transparent_color) bm[BL2] = paldata[col];
                col = (col4 >> 24) & 0xff;
                if (col != transparent_color) bm[BL3] = paldata[col];
              }
              sd4++;
            }
            sd = (unsigned char *)sd4;
            for (; bm <= bme; bm++) {
              col = *(sd++);
              if (col != transparent_color) *bm = paldata[col];
            }
            start += dy;
          }
        }
        break;

      case TRANSPARENCY_PENS:
#define PEN_IS_OPAQUE ((1 << col) & transparent_color) == 0

        if (flipx) /* X flip */
        {
          for (y = sy; y <= ey; y++) {
            bm = dest->line[y];
            bme = bm + ex;
            sd4 = (int *)(gfx->gfxdata->line[start] + gfx->width - 1 - (sx - ox) - 3);
            for (bm += sx; bm <= bme - 3; bm += 4) {
              col4 = read_dword(sd4);
              col = (col4 >> 24) & 0xff;
              if (PEN_IS_OPAQUE) bm[BL0] = paldata[col];
              col = (col4 >> 16) & 0xff;
              if (PEN_IS_OPAQUE) bm[BL1] = paldata[col];
              col = (col4 >> 8) & 0xff;
              if (PEN_IS_OPAQUE) bm[BL2] = paldata[col];
              col = col4 & 0xff;
              if (PEN_IS_OPAQUE) bm[BL3] = paldata[col];
              sd4--;
            }
            sd = (unsigned char *)sd4 + 3;
            for (; bm <= bme; bm++) {
              col = *(sd--);
              if (PEN_IS_OPAQUE) *bm = paldata[col];
            }
            start += dy;
          }
        } else /* normal */
        {
          for (y = sy; y <= ey; y++) {
            bm = dest->line[y];
            bme = bm + ex;
            sd4 = (int *)(gfx->gfxdata->line[start] + (sx - ox));
            for (bm += sx; bm <= bme - 3; bm += 4) {
              col4 = read_dword(sd4);
              col = col4 & 0xff;
              if (PEN_IS_OPAQUE) bm[BL0] = paldata[col];
              col = (col4 >> 8) & 0xff;
              if (PEN_IS_OPAQUE) bm[BL1] = paldata[col];
              col = (col4 >> 16) & 0xff;
              if (PEN_IS_OPAQUE) bm[BL2] = paldata[col];
              col = (col4 >> 24) & 0xff;
              if (PEN_IS_OPAQUE) bm[BL3] = paldata[col];
              sd4++;
            }
            sd = (unsigned char *)sd4;
            for (; bm <= bme; bm++) {
              col = *(sd++);
              if (PEN_IS_OPAQUE) *bm = paldata[col];
            }
            start += dy;
          }
        }
        break;

      case TRANSPARENCY_COLOR:
        if (flipx) /* X flip */
        {
          for (y = sy; y <= ey; y++) {
            bm = dest->line[y];
            bme = bm + ex;
            sd = gfx->gfxdata->line[start] + gfx->width - 1 - (sx - ox);
            for (bm += sx; bm <= bme; bm++) {
              col = paldata[*(sd--)];
              if (col != transparent_color) *bm = col;
            }
            start += dy;
          }
        } else /* normal */
        {
          for (y = sy; y <= ey; y++) {
            bm = dest->line[y];
            bme = bm + ex;
            sd = gfx->gfxdata->line[start] + (sx - ox);
            for (bm += sx; bm <= bme; bm++) {
              col = paldata[*(sd++)];
              if (col != transparent_color) *bm = col;
            }
            start += dy;
          }
        }
        break;

      case TRANSPARENCY_THROUGH:
        if (flipx) /* X flip */
        {
          for (y = sy; y <= ey; y++) {
            bm = dest->line[y];
            bme = bm + ex;
            sd = gfx->gfxdata->line[start] + gfx->width - 1 - (sx - ox);
            for (bm += sx; bm <= bme; bm++) {
              if (*bm == transparent_color)
                *bm = paldata[*sd];
              sd--;
            }
            start += dy;
          }
        } else /* normal */
        {
          for (y = sy; y <= ey; y++) {
            bm = dest->line[y];
            bme = bm + ex;
            sd = gfx->gfxdata->line[start] + (sx - ox);
            for (bm += sx; bm <= bme; bm++) {
              if (*bm == transparent_color)
                *bm = paldata[*sd];
              sd++;
            }
            start += dy;
          }
        }
        break;
    }
  } else {
    switch (transparency) {
      case TRANSPARENCY_NONE: /* do a verbatim copy (faster) */
        if (flipx)            /* X flip */
        {
          for (y = sy; y <= ey; y++) {
            bm = dest->line[y];
            bme = bm + ex;
            sd = gfx->gfxdata->line[start] + gfx->width - 1 - (sx - ox);
            for (bm += sx; bm <= bme - 7; bm += 8) {
              sd -= 8;
              bm[0] = sd[8];
              bm[1] = sd[7];
              bm[2] = sd[6];
              bm[3] = sd[5];
              bm[4] = sd[4];
              bm[5] = sd[3];
              bm[6] = sd[2];
              bm[7] = sd[1];
            }
            for (; bm <= bme; bm++)
              *bm = *(sd--);
            start += dy;
          }
        } else /* normal */
        {
          for (y = sy; y <= ey; y++) {
            bm = dest->line[y] + sx;
            sd = gfx->gfxdata->line[start] + (sx - ox);
            memcpy(bm, sd, ex - sx + 1);
            start += dy;
          }
        }
        break;

      case TRANSPARENCY_PEN:
      case TRANSPARENCY_COLOR:
        trans4 = transparent_color * 0x01010101;

        if (flipx) /* X flip */
        {
          for (y = sy; y <= ey; y++) {
            bm = dest->line[y];
            bme = bm + ex;
            sd4 = (int *)(gfx->gfxdata->line[start] + gfx->width - 1 - (sx - ox) - 3);
            for (bm += sx; bm <= bme - 3; bm += 4) {
              if ((col4 = read_dword(sd4)) != trans4) {
                col = (col4 >> 24) & 0xff;
                if (col != transparent_color) bm[BL0] = col;
                col = (col4 >> 16) & 0xff;
                if (col != transparent_color) bm[BL1] = col;
                col = (col4 >> 8) & 0xff;
                if (col != transparent_color) bm[BL2] = col;
                col = col4 & 0xff;
                if (col != transparent_color) bm[BL3] = col;
              }
              sd4--;
            }
            sd = (unsigned char *)sd4 + 3;
            for (; bm <= bme; bm++) {
              col = *(sd--);
              if (col != transparent_color) *bm = col;
            }
            start += dy;
          }
        } else /* normal */
        {
          int xod4;

          for (y = sy; y <= ey; y++) {
            bm = dest->line[y];
            bme = bm + ex;
            sd4 = (int *)(gfx->gfxdata->line[start] + (sx - ox));
            bm += sx;
            while (bm <= bme - 3) {
              /* bypass loop */
              while (bm <= bme - 3 && read_dword(sd4) == trans4) {
                sd4++;
                bm += 4;
              }
              /* drawing loop */
              while (bm <= bme - 3 && (col4 = read_dword(sd4)) != trans4) {
                xod4 = col4 ^ trans4;
                if ((xod4 & 0x000000ff) && (xod4 & 0x0000ff00) && (xod4 & 0x00ff0000) && (xod4 & 0xff000000)) {
                  write_dword((int *)bm, col4);
                } else {
                  if (xod4 & 0x000000ff) bm[BL0] = col4;
                  if (xod4 & 0x0000ff00) bm[BL1] = col4 >> 8;
                  if (xod4 & 0x00ff0000) bm[BL2] = col4 >> 16;
                  if (xod4 & 0xff000000) bm[BL3] = col4 >> 24;
                }
                sd4++;
                bm += 4;
              }
            }
            sd = (unsigned char *)sd4;
            for (; bm <= bme; bm++) {
              col = *(sd++);
              if (col != transparent_color) *bm = col;
            }
            start += dy;
          }
        }
        break;

      case TRANSPARENCY_THROUGH:
        if (flipx) /* X flip */
        {
          for (y = sy; y <= ey; y++) {
            bm = dest->line[y];
            bme = bm + ex;
            sd = gfx->gfxdata->line[start] + gfx->width - 1 - (sx - ox);
            for (bm = bm + sx; bm <= bme; bm++) {
              if (*bm == transparent_color)
                *bm = *sd;
              sd--;
            }
            start += dy;
          }
        } else /* normal */
        {
          for (y = sy; y <= ey; y++) {
            bm = dest->line[y];
            bme = bm + ex;
            sd = gfx->gfxdata->line[start] + (sx - ox);
            for (bm = bm + sx; bm <= bme; bm++) {
              if (*bm == transparent_color)
                *bm = *sd;
              sd++;
            }
            start += dy;
          }
        }
        break;
    }
  }
}

static void drawgfx_core16(struct osd_bitmap *dest, const struct GfxElement *gfx,
                           unsigned int code, unsigned int color, int flipx, int flipy, int sx, int sy,
                           const struct rectangle *clip, int transparency, int transparent_color, int dirty) {
  //emu_printf("drawgfx_core16");
  int ox, oy, ex, ey, y, start, dy;
  unsigned short *bm, *bme;
  int col;
  struct rectangle myclip;


  if (!gfx) return;

  code %= gfx->total_elements;
  color %= gfx->total_colors;

  /* if necessary, remap the transparent color */
  if (transparency == TRANSPARENCY_COLOR)
    transparent_color = Machine->pens[transparent_color];

  if (gfx->pen_usage) {
    int transmask;


    transmask = 0;

    if (transparency == TRANSPARENCY_PEN) {
      transmask = 1 << transparent_color;
    } else if (transparency == TRANSPARENCY_PENS) {
      transmask = transparent_color;
    } else if (transparency == TRANSPARENCY_COLOR && gfx->colortable) {
      int i;
      const unsigned short *paldata;


      paldata = &gfx->colortable[gfx->color_granularity * color];

      for (i = gfx->color_granularity - 1; i >= 0; i--) {
        if (paldata[i] == transparent_color)
          transmask |= 1 << i;
      }
    }

    if ((gfx->pen_usage[code] & ~transmask) == 0)
      /* character is totally transparent, no need to draw */
      return;
    else if ((gfx->pen_usage[code] & transmask) == 0 && transparency != TRANSPARENCY_THROUGH)
      /* character is totally opaque, can disable transparency */
      transparency = TRANSPARENCY_NONE;
  }

  if (Machine->orientation & ORIENTATION_SWAP_XY) {
    int temp;

    temp = sx;
    sx = sy;
    sy = temp;

    temp = flipx;
    flipx = flipy;
    flipy = temp;

    if (clip) {
      /* clip and myclip might be the same, so we need a temporary storage */
      temp = clip->min_x;
      myclip.min_x = clip->min_y;
      myclip.min_y = temp;
      temp = clip->max_x;
      myclip.max_x = clip->max_y;
      myclip.max_y = temp;
      clip = &myclip;
    }
  }
  if (Machine->orientation & ORIENTATION_FLIP_X) {
    sx = dest->width - gfx->width - sx;
    if (clip) {
      int temp;


      /* clip and myclip might be the same, so we need a temporary storage */
      temp = clip->min_x;
      myclip.min_x = dest->width - 1 - clip->max_x;
      myclip.max_x = dest->width - 1 - temp;
      myclip.min_y = clip->min_y;
      myclip.max_y = clip->max_y;
      clip = &myclip;
    }
  }
  if (Machine->orientation & ORIENTATION_FLIP_Y) {
    sy = dest->height - gfx->height - sy;
    if (clip) {
      int temp;


      myclip.min_x = clip->min_x;
      myclip.max_x = clip->max_x;
      /* clip and myclip might be the same, so we need a temporary storage */
      temp = clip->min_y;
      myclip.min_y = dest->height - 1 - clip->max_y;
      myclip.max_y = dest->height - 1 - temp;
      clip = &myclip;
    }
  }


  /* check bounds */
  ox = sx;
  oy = sy;
  ex = sx + gfx->width - 1;
  if (sx < 0) sx = 0;
  if (clip && sx < clip->min_x) sx = clip->min_x;
  if (ex >= dest->width) ex = dest->width - 1;
  if (clip && ex > clip->max_x) ex = clip->max_x;
  if (sx > ex) return;
  ey = sy + gfx->height - 1;
  if (sy < 0) sy = 0;
  if (clip && sy < clip->min_y) sy = clip->min_y;
  if (ey >= dest->height) ey = dest->height - 1;
  if (clip && ey > clip->max_y) ey = clip->max_y;
  if (sy > ey) return;

  if (dirty)
    osd_mark_dirty(sx, sy, ex, ey, 0); /* ASG 971011 */

  /* start = code * gfx->height; */
  if (flipy) /* Y flop */
  {
    start = code * gfx->height + gfx->height - 1 - (sy - oy);
    dy = -1;
  } else /* normal */
  {
    start = code * gfx->height + (sy - oy);
    dy = 1;
  }


  if (gfx->colortable) /* remap colors -- assumes 8-bit source */
  {
    int *sd4;
    int trans4, col4;
    const unsigned char *sd;
    const unsigned short *paldata;

    paldata = &gfx->colortable[gfx->color_granularity * color];

    switch (transparency) {
      case TRANSPARENCY_NONE:
        if (flipx) /* X flip */
        {
          for (y = sy; y <= ey; y++) {
            bm = (unsigned short *)dest->line[y];
            bme = bm + ex;
            sd = gfx->gfxdata->line[start] + gfx->width - 1 - (sx - ox);
            for (bm += sx; bm <= bme - 7; bm += 8) {
              sd -= 8;
              bm[0] = paldata[sd[8]];
              bm[1] = paldata[sd[7]];
              bm[2] = paldata[sd[6]];
              bm[3] = paldata[sd[5]];
              bm[4] = paldata[sd[4]];
              bm[5] = paldata[sd[3]];
              bm[6] = paldata[sd[2]];
              bm[7] = paldata[sd[1]];
            }
            for (; bm <= bme; bm++)
              *bm = paldata[*(sd--)];
            start += dy;
          }
        } else /* normal */
        {
          for (y = sy; y <= ey; y++) {
            bm = (unsigned short *)dest->line[y];
            bme = bm + ex;
            sd = gfx->gfxdata->line[start] + (sx - ox);
            for (bm += sx; bm <= bme - 7; bm += 8) {
              bm[0] = paldata[sd[0]];
              bm[1] = paldata[sd[1]];
              bm[2] = paldata[sd[2]];
              bm[3] = paldata[sd[3]];
              bm[4] = paldata[sd[4]];
              bm[5] = paldata[sd[5]];
              bm[6] = paldata[sd[6]];
              bm[7] = paldata[sd[7]];
              sd += 8;
            }
            for (; bm <= bme; bm++)
              *bm = paldata[*(sd++)];
            start += dy;
          }
        }
        break;

      case TRANSPARENCY_PEN:
        trans4 = transparent_color * 0x01010101;
        if (flipx) /* X flip */
        {
          for (y = sy; y <= ey; y++) {
            bm = (unsigned short *)dest->line[y];
            bme = bm + ex;
            sd4 = (int *)(gfx->gfxdata->line[start] + gfx->width - 1 - (sx - ox) - 3);
            for (bm += sx; bm <= bme - 3; bm += 4) {
              if ((col4 = read_dword(sd4)) != trans4) {
                col = (col4 >> 24) & 0xff;
                if (col != transparent_color) bm[BL0] = paldata[col];
                col = (col4 >> 16) & 0xff;
                if (col != transparent_color) bm[BL1] = paldata[col];
                col = (col4 >> 8) & 0xff;
                if (col != transparent_color) bm[BL2] = paldata[col];
                col = col4 & 0xff;
                if (col != transparent_color) bm[BL3] = paldata[col];
              }
              sd4--;
            }
            sd = (unsigned char *)sd4 + 3;
            for (; bm <= bme; bm++) {
              col = *(sd--);
              if (col != transparent_color) *bm = paldata[col];
            }
            start += dy;
          }
        } else /* normal */
        {
          for (y = sy; y <= ey; y++) {
            bm = (unsigned short *)dest->line[y];
            bme = bm + ex;
            sd4 = (int *)(gfx->gfxdata->line[start] + (sx - ox));
            for (bm += sx; bm <= bme - 3; bm += 4) {
              if ((col4 = read_dword(sd4)) != trans4) {
                col = col4 & 0xff;
                if (col != transparent_color) bm[BL0] = paldata[col];
                col = (col4 >> 8) & 0xff;
                if (col != transparent_color) bm[BL1] = paldata[col];
                col = (col4 >> 16) & 0xff;
                if (col != transparent_color) bm[BL2] = paldata[col];
                col = (col4 >> 24) & 0xff;
                if (col != transparent_color) bm[BL3] = paldata[col];
              }
              sd4++;
            }
            sd = (unsigned char *)sd4;
            for (; bm <= bme; bm++) {
              col = *(sd++);
              if (col != transparent_color) *bm = paldata[col];
            }
            start += dy;
          }
        }
        break;

      case TRANSPARENCY_PENS:
#define PEN_IS_OPAQUE ((1 << col) & transparent_color) == 0

        if (flipx) /* X flip */
        {
          for (y = sy; y <= ey; y++) {
            bm = (unsigned short *)dest->line[y];
            bme = bm + ex;
            sd4 = (int *)(gfx->gfxdata->line[start] + gfx->width - 1 - (sx - ox) - 3);
            for (bm += sx; bm <= bme - 3; bm += 4) {
              col4 = read_dword(sd4);
              col = (col4 >> 24) & 0xff;
              if (PEN_IS_OPAQUE) bm[BL0] = paldata[col];
              col = (col4 >> 16) & 0xff;
              if (PEN_IS_OPAQUE) bm[BL1] = paldata[col];
              col = (col4 >> 8) & 0xff;
              if (PEN_IS_OPAQUE) bm[BL2] = paldata[col];
              col = col4 & 0xff;
              if (PEN_IS_OPAQUE) bm[BL3] = paldata[col];
              sd4--;
            }
            sd = (unsigned char *)sd4 + 3;
            for (; bm <= bme; bm++) {
              col = *(sd--);
              if (PEN_IS_OPAQUE) *bm = paldata[col];
            }
            start += dy;
          }
        } else /* normal */
        {
          for (y = sy; y <= ey; y++) {
            bm = (unsigned short *)dest->line[y];
            bme = bm + ex;
            sd4 = (int *)(gfx->gfxdata->line[start] + (sx - ox));
            for (bm += sx; bm <= bme - 3; bm += 4) {
              col4 = read_dword(sd4);
              col = col4 & 0xff;
              if (PEN_IS_OPAQUE) bm[BL0] = paldata[col];
              col = (col4 >> 8) & 0xff;
              if (PEN_IS_OPAQUE) bm[BL1] = paldata[col];
              col = (col4 >> 16) & 0xff;
              if (PEN_IS_OPAQUE) bm[BL2] = paldata[col];
              col = (col4 >> 24) & 0xff;
              if (PEN_IS_OPAQUE) bm[BL3] = paldata[col];
              sd4++;
            }
            sd = (unsigned char *)sd4;
            for (; bm <= bme; bm++) {
              col = *(sd++);
              if (PEN_IS_OPAQUE) *bm = paldata[col];
            }
            start += dy;
          }
        }
        break;

      case TRANSPARENCY_COLOR:
        if (flipx) /* X flip */
        {
          for (y = sy; y <= ey; y++) {
            bm = (unsigned short *)dest->line[y];
            bme = bm + ex;
            sd = gfx->gfxdata->line[start] + gfx->width - 1 - (sx - ox);
            for (bm += sx; bm <= bme; bm++) {
              col = paldata[*(sd--)];
              if (col != transparent_color) *bm = col;
            }
            start += dy;
          }
        } else /* normal */
        {
          for (y = sy; y <= ey; y++) {
            bm = (unsigned short *)dest->line[y];
            bme = bm + ex;
            sd = gfx->gfxdata->line[start] + (sx - ox);
            for (bm += sx; bm <= bme; bm++) {
              col = paldata[*(sd++)];
              if (col != transparent_color) *bm = col;
            }
            start += dy;
          }
        }
        break;

      case TRANSPARENCY_THROUGH:
        if (flipx) /* X flip */
        {
          for (y = sy; y <= ey; y++) {
            bm = (unsigned short *)dest->line[y];
            bme = bm + ex;
            sd = gfx->gfxdata->line[start] + gfx->width - 1 - (sx - ox);
            for (bm += sx; bm <= bme; bm++) {
              if (*bm == transparent_color)
                *bm = paldata[*sd];
              sd--;
            }
            start += dy;
          }
        } else /* normal */
        {
          for (y = sy; y <= ey; y++) {
            bm = (unsigned short *)dest->line[y];
            bme = bm + ex;
            sd = gfx->gfxdata->line[start] + (sx - ox);
            for (bm += sx; bm <= bme; bm++) {
              if (*bm == transparent_color)
                *bm = paldata[*sd];
              sd++;
            }
            start += dy;
          }
        }
        break;
    }
  } else /* not palette mapped -- assumes 16-bit to 16-bit */
  {
    int *sd2;
    int trans2, col2;
    const unsigned short *sd;

    switch (transparency) {
      case TRANSPARENCY_NONE: /* do a verbatim copy (faster) */
        if (flipx)            /* X flip */
        {
          for (y = sy; y <= ey; y++) {
            bm = (unsigned short *)dest->line[y];
            bme = bm + ex;
            sd = (unsigned short *)gfx->gfxdata->line[start] + gfx->width - 1 - (sx - ox);
            for (bm += sx; bm <= bme - 7; bm += 8) {
              sd -= 8;
              bm[0] = sd[8];
              bm[1] = sd[7];
              bm[2] = sd[6];
              bm[3] = sd[5];
              bm[4] = sd[4];
              bm[5] = sd[3];
              bm[6] = sd[2];
              bm[7] = sd[1];
            }
            for (; bm <= bme; bm++)
              *bm = *(sd--);
            start += dy;
          }
        } else /* normal */
        {
          for (y = sy; y <= ey; y++) {
            bm = (unsigned short *)dest->line[y] + sx;
            sd = (unsigned short *)gfx->gfxdata->line[start] + (sx - ox);
            memcpy(bm, sd, (ex - sx + 1) * 2);
            start += dy;
          }
        }
        break;

      case TRANSPARENCY_PEN:
      case TRANSPARENCY_COLOR:
        trans2 = transparent_color * 0x00010001;

        if (flipx) /* X flip */
        {
          for (y = sy; y <= ey; y++) {
            bm = (unsigned short *)dest->line[y];
            bme = bm + ex;
            sd2 = (int *)((unsigned short *)gfx->gfxdata->line[start] + gfx->width - 1 - (sx - ox) - 3);
            for (bm += sx; bm <= bme - 1; bm += 2) {
              if ((col2 = read_dword(sd2)) != trans2) {
                col = (col2 >> 16) & 0xffff;
                if (col != transparent_color) bm[WL0] = col;
                col = col2 & 0xffff;
                if (col != transparent_color) bm[WL1] = col;
              }
              sd2--;
            }
            sd = (unsigned short *)sd2 + 1;
            for (; bm <= bme; bm++) {
              col = *(sd--);
              if (col != transparent_color) *bm = col;
            }
            start += dy;
          }
        } else /* normal */
        {
          int xod2;

          for (y = sy; y <= ey; y++) {
            bm = (unsigned short *)dest->line[y];
            bme = bm + ex;
            sd2 = (int *)((unsigned short *)gfx->gfxdata->line[start] + (sx - ox));
            bm += sx;
            while (bm <= bme - 1) {
              /* bypass loop */
              while (bm <= bme - 1 && read_dword(sd2) == trans2) {
                sd2++;
                bm += 2;
              }
              /* drawing loop */
              while (bm <= bme - 1 && (col2 = read_dword(sd2)) != trans2) {
                xod2 = col2 ^ trans2;
                if ((xod2 & 0x0000ffff) && (xod2 & 0xffff0000)) {
                  write_dword((int *)bm, col2);
                } else {
                  if (xod2 & 0x0000ffff) bm[WL0] = col2;
                  if (xod2 & 0xffff0000) bm[WL1] = col2 >> 16;
                }
                sd2++;
                bm += 2;
              }
            }
            sd = (unsigned short *)sd2;
            for (; bm <= bme; bm++) {
              col = *(sd++);
              if (col != transparent_color) *bm = col;
            }
            start += dy;
          }
        }
        break;

      case TRANSPARENCY_THROUGH:
        if (flipx) /* X flip */
        {
          for (y = sy; y <= ey; y++) {
            bm = (unsigned short *)dest->line[y];
            bme = bm + ex;
            sd = (unsigned short *)gfx->gfxdata->line[start] + gfx->width - 1 - (sx - ox);
            for (bm = bm + sx; bm <= bme; bm++) {
              if (*bm == transparent_color)
                *bm = *sd;
              sd--;
            }
            start += dy;
          }
        } else /* normal */
        {
          for (y = sy; y <= ey; y++) {
            bm = (unsigned short *)dest->line[y];
            bme = bm + ex;
            sd = (unsigned short *)gfx->gfxdata->line[start] + (sx - ox);
            for (bm = bm + sx; bm <= bme; bm++) {
              if (*bm == transparent_color)
                *bm = *sd;
              sd++;
            }
            start += dy;
          }
        }
        break;
    }
  }
}


/* ASG 971011 - this is the real draw gfx now */
void drawgfx(struct osd_bitmap *dest, const struct GfxElement *gfx,
             unsigned int code, unsigned int color, int flipx, int flipy, int sx, int sy,
             const struct rectangle *clip, int transparency, int transparent_color) {
  /* ASG 980209 -- separate 8-bit from 16-bit here */
  if (dest->depth != 16)
    drawgfx_core8(dest, gfx, code, color, flipx, flipy, sx, sy, clip, transparency, transparent_color, 1);
  else
    drawgfx_core16(dest, gfx, code, color, flipx, flipy, sx, sy, clip, transparency, transparent_color, 1);
}


/***************************************************************************

  Use drawgfx() to copy a bitmap onto another at the given position.
  This function will very likely change in the future.

***************************************************************************/
void copybitmap(struct osd_bitmap *dest, struct osd_bitmap *src, int flipx, int flipy, int sx, int sy,
                const struct rectangle *clip, int transparency, int transparent_color) {
  // emu_printf("copybitmap");
  static struct GfxElement mygfx = {
    0, 0, 0, /* filled in later */
    1, 1, 0, 1
  };

  mygfx.width = src->width;
  mygfx.height = src->height;
  mygfx.gfxdata = src;

  /* ASG 980209 -- separate 8-bit from 16-bit here */
  if (dest->depth != 16)
    drawgfx_core8(dest, &mygfx, 0, 0, flipx, flipy, sx, sy, clip, transparency, transparent_color, 0); /* ASG 971011 */
  else
    drawgfx_core16(dest, &mygfx, 0, 0, flipx, flipy, sx, sy, clip, transparency, transparent_color, 0); /* ASG 971011 */
}


void copybitmapzoom(struct osd_bitmap *dest, struct osd_bitmap *src, int flipx, int flipy, int sx, int sy,
                    const struct rectangle *clip, int transparency, int transparent_color, int scalex, int scaley) {
  static struct GfxElement mygfx = {
    0, 0, 0, /* filled in later */
    1, 1, 0, 1
  };
  unsigned short hacktable[256];
  int i;

  mygfx.width = src->width;
  mygfx.height = src->height;
  mygfx.gfxdata = src;
  mygfx.colortable = hacktable;
  for (i = 0; i < 256; i++) hacktable[i] = i;
  drawgfxzoom(dest, &mygfx, 0, 0, flipx, flipy, sx, sy, clip, transparency, transparent_color, scalex, scaley); /* ASG 971011 */
}


/***************************************************************************

  Copy a bitmap onto another with scroll and wraparound.
  This function supports multiple independently scrolling rows/columns.
  "rows" is the number of indepentently scrolling rows. "rowscroll" is an
  array of integers telling how much to scroll each row. Same thing for
  "cols" and "colscroll".
  If the bitmap cannot scroll in one direction, set rows or columns to 0.
  If the bitmap scrolls as a whole, set rows and/or cols to 1.
  Bidirectional scrolling is, of course, supported only if the bitmap
  scrolls as a whole in at least one direction.

***************************************************************************/
void copyscrollbitmap(struct osd_bitmap *dest, struct osd_bitmap *src,
                      int rows, const int *rowscroll, int cols, const int *colscroll,
                      const struct rectangle *clip, int transparency, int transparent_color) {
  int srcwidth, srcheight;


  if (Machine->orientation & ORIENTATION_SWAP_XY) {
    srcwidth = src->height;
    srcheight = src->width;
  } else {
    srcwidth = src->width;
    srcheight = src->height;
  }

  if (rows == 0) {
    /* scrolling columns */
    int col, colwidth;
    struct rectangle myclip;


    colwidth = srcwidth / cols;

    myclip.min_y = clip->min_y;
    myclip.max_y = clip->max_y;

    col = 0;
    while (col < cols) {
      int cons, scroll;


      /* count consecutive columns scrolled by the same amount */
      scroll = colscroll[col];
      cons = 1;
      while (col + cons < cols && colscroll[col + cons] == scroll)
        cons++;

      if (scroll < 0) scroll = srcheight - (-scroll) % srcheight;
      else scroll %= srcheight;

      myclip.min_x = col * colwidth;
      if (myclip.min_x < clip->min_x) myclip.min_x = clip->min_x;
      myclip.max_x = (col + cons) * colwidth - 1;
      if (myclip.max_x > clip->max_x) myclip.max_x = clip->max_x;

      copybitmap(dest, src, 0, 0, 0, scroll, &myclip, transparency, transparent_color);
      copybitmap(dest, src, 0, 0, 0, scroll - srcheight, &myclip, transparency, transparent_color);

      col += cons;
    }
  } else if (cols == 0) {
    /* scrolling rows */
    int row, rowheight;
    struct rectangle myclip;


    rowheight = srcheight / rows;

    myclip.min_x = clip->min_x;
    myclip.max_x = clip->max_x;

    row = 0;
    while (row < rows) {
      int cons, scroll;


      /* count consecutive rows scrolled by the same amount */
      scroll = rowscroll[row];
      cons = 1;
      while (row + cons < rows && rowscroll[row + cons] == scroll)
        cons++;

      if (scroll < 0) scroll = srcwidth - (-scroll) % srcwidth;
      else scroll %= srcwidth;

      myclip.min_y = row * rowheight;
      if (myclip.min_y < clip->min_y) myclip.min_y = clip->min_y;
      myclip.max_y = (row + cons) * rowheight - 1;
      if (myclip.max_y > clip->max_y) myclip.max_y = clip->max_y;

      copybitmap(dest, src, 0, 0, scroll, 0, &myclip, transparency, transparent_color);
      copybitmap(dest, src, 0, 0, scroll - srcwidth, 0, &myclip, transparency, transparent_color);

      row += cons;
    }
  } else if (rows == 1 && cols == 1) {
    /* XY scrolling playfield */
    int scrollx, scrolly;


    if (rowscroll[0] < 0) scrollx = srcwidth - (-rowscroll[0]) % srcwidth;
    else scrollx = rowscroll[0] % srcwidth;

    if (colscroll[0] < 0) scrolly = srcheight - (-colscroll[0]) % srcheight;
    else scrolly = colscroll[0] % srcheight;

    copybitmap(dest, src, 0, 0, scrollx, scrolly, clip, transparency, transparent_color);
    copybitmap(dest, src, 0, 0, scrollx, scrolly - srcheight, clip, transparency, transparent_color);
    copybitmap(dest, src, 0, 0, scrollx - srcwidth, scrolly, clip, transparency, transparent_color);
    copybitmap(dest, src, 0, 0, scrollx - srcwidth, scrolly - srcheight, clip, transparency, transparent_color);
  } else if (rows == 1) {
    /* scrolling columns + horizontal scroll */
    int col, colwidth;
    int scrollx;
    struct rectangle myclip;


    if (rowscroll[0] < 0) scrollx = srcwidth - (-rowscroll[0]) % srcwidth;
    else scrollx = rowscroll[0] % srcwidth;

    colwidth = srcwidth / cols;

    myclip.min_y = clip->min_y;
    myclip.max_y = clip->max_y;

    col = 0;
    while (col < cols) {
      int cons, scroll;


      /* count consecutive columns scrolled by the same amount */
      scroll = colscroll[col];
      cons = 1;
      while (col + cons < cols && colscroll[col + cons] == scroll)
        cons++;

      if (scroll < 0) scroll = srcheight - (-scroll) % srcheight;
      else scroll %= srcheight;

      myclip.min_x = col * colwidth + scrollx;
      if (myclip.min_x < clip->min_x) myclip.min_x = clip->min_x;
      myclip.max_x = (col + cons) * colwidth - 1 + scrollx;
      if (myclip.max_x > clip->max_x) myclip.max_x = clip->max_x;

      copybitmap(dest, src, 0, 0, scrollx, scroll, &myclip, transparency, transparent_color);
      copybitmap(dest, src, 0, 0, scrollx, scroll - srcheight, &myclip, transparency, transparent_color);

      myclip.min_x = col * colwidth + scrollx - srcwidth;
      if (myclip.min_x < clip->min_x) myclip.min_x = clip->min_x;
      myclip.max_x = (col + cons) * colwidth - 1 + scrollx - srcwidth;
      if (myclip.max_x > clip->max_x) myclip.max_x = clip->max_x;

      copybitmap(dest, src, 0, 0, scrollx - srcwidth, scroll, &myclip, transparency, transparent_color);
      copybitmap(dest, src, 0, 0, scrollx - srcwidth, scroll - srcheight, &myclip, transparency, transparent_color);

      col += cons;
    }
  } else if (cols == 1) {
    /* scrolling rows + vertical scroll */
    int row, rowheight;
    int scrolly;
    struct rectangle myclip;


    if (colscroll[0] < 0) scrolly = srcheight - (-colscroll[0]) % srcheight;
    else scrolly = colscroll[0] % srcheight;

    rowheight = srcheight / rows;

    myclip.min_x = clip->min_x;
    myclip.max_x = clip->max_x;

    row = 0;
    while (row < rows) {
      int cons, scroll;


      /* count consecutive rows scrolled by the same amount */
      scroll = rowscroll[row];
      cons = 1;
      while (row + cons < rows && rowscroll[row + cons] == scroll)
        cons++;

      if (scroll < 0) scroll = srcwidth - (-scroll) % srcwidth;
      else scroll %= srcwidth;

      myclip.min_y = row * rowheight + scrolly;
      if (myclip.min_y < clip->min_y) myclip.min_y = clip->min_y;
      myclip.max_y = (row + cons) * rowheight - 1 + scrolly;
      if (myclip.max_y > clip->max_y) myclip.max_y = clip->max_y;

      copybitmap(dest, src, 0, 0, scroll, scrolly, &myclip, transparency, transparent_color);
      copybitmap(dest, src, 0, 0, scroll - srcwidth, scrolly, &myclip, transparency, transparent_color);

      myclip.min_y = row * rowheight + scrolly - srcheight;
      if (myclip.min_y < clip->min_y) myclip.min_y = clip->min_y;
      myclip.max_y = (row + cons) * rowheight - 1 + scrolly - srcheight;
      if (myclip.max_y > clip->max_y) myclip.max_y = clip->max_y;

      copybitmap(dest, src, 0, 0, scroll, scrolly - srcheight, &myclip, transparency, transparent_color);
      copybitmap(dest, src, 0, 0, scroll - srcwidth, scrolly - srcheight, &myclip, transparency, transparent_color);

      row += cons;
    }
  }
}


/* fill a bitmap using the specified pen */
void fillbitmap(struct osd_bitmap *dest, int pen, const struct rectangle *clip) {
  int sx, sy, ex, ey, y;
  struct rectangle myclip;


  if (Machine->orientation & ORIENTATION_SWAP_XY) {
    if (clip) {
      myclip.min_x = clip->min_y;
      myclip.max_x = clip->max_y;
      myclip.min_y = clip->min_x;
      myclip.max_y = clip->max_x;
      clip = &myclip;
    }
  }
  if (Machine->orientation & ORIENTATION_FLIP_X) {
    if (clip) {
      int temp;


      temp = clip->min_x;
      myclip.min_x = dest->width - 1 - clip->max_x;
      myclip.max_x = dest->width - 1 - temp;
      myclip.min_y = clip->min_y;
      myclip.max_y = clip->max_y;
      clip = &myclip;
    }
  }
  if (Machine->orientation & ORIENTATION_FLIP_Y) {
    if (clip) {
      int temp;


      myclip.min_x = clip->min_x;
      myclip.max_x = clip->max_x;
      temp = clip->min_y;
      myclip.min_y = dest->height - 1 - clip->max_y;
      myclip.max_y = dest->height - 1 - temp;
      clip = &myclip;
    }
  }


  sx = 0;
  ex = dest->width - 1;
  sy = 0;
  ey = dest->height - 1;

  if (clip && sx < clip->min_x) sx = clip->min_x;
  if (clip && ex > clip->max_x) ex = clip->max_x;
  if (sx > ex) return;
  if (clip && sy < clip->min_y) sy = clip->min_y;
  if (clip && ey > clip->max_y) ey = clip->max_y;
  if (sy > ey) return;

  osd_mark_dirty(sx, sy, ex, ey, 0); /* ASG 971011 */

  /* ASG 980211 */
  if (dest->depth == 16) {
    if ((pen >> 8) == (pen & 0xff)) {
      for (y = sy; y <= ey; y++)
        memset(&dest->line[y][sx * 2], pen & 0xff, (ex - sx + 1) * 2);
    } else {
      for (y = sy; y <= ey; y++) {
        unsigned short *p = (unsigned short *)&dest->line[y][sx * 2];
        int x;
        for (x = sx; x <= ex; x++)
          *p++ = pen;
      }
    }
  } else {
    for (y = sy; y <= ey; y++)
      memset(&dest->line[y][sx], pen, ex - sx + 1);
  }
}


void drawgfxzoom(struct osd_bitmap *dest_bmp, const struct GfxElement *gfx,
                 unsigned int code, unsigned int color, int flipx, int flipy, int sx, int sy,
                 const struct rectangle *clip, int transparency, int transparent_color, int scalex, int scaley) {
  struct rectangle myclip;


  /* only support TRANSPARENCY_PEN and TRANSPARENCY_COLOR */
  if (transparency != TRANSPARENCY_PEN && transparency != TRANSPARENCY_COLOR)
    return;

  if (transparency == TRANSPARENCY_COLOR)
    transparent_color = Machine->pens[transparent_color];


  /*
	scalex and scaley are 16.16 fixed point numbers
	1<<15 : shrink to 50%
	1<<16 : uniform scale
	1<<17 : double to 200%
	*/


  if (Machine->orientation & ORIENTATION_SWAP_XY) {
    int temp;

    temp = sx;
    sx = sy;
    sy = temp;

    temp = flipx;
    flipx = flipy;
    flipy = temp;

    temp = scalex;
    scalex = scaley;
    scaley = temp;

    if (clip) {
      /* clip and myclip might be the same, so we need a temporary storage */
      temp = clip->min_x;
      myclip.min_x = clip->min_y;
      myclip.min_y = temp;
      temp = clip->max_x;
      myclip.max_x = clip->max_y;
      myclip.max_y = temp;
      clip = &myclip;
    }
  }
  if (Machine->orientation & ORIENTATION_FLIP_X) {
    sx = dest_bmp->width - ((gfx->width * scalex) >> 16) - sx;
    if (clip) {
      int temp;


      /* clip and myclip might be the same, so we need a temporary storage */
      temp = clip->min_x;
      myclip.min_x = dest_bmp->width - 1 - clip->max_x;
      myclip.max_x = dest_bmp->width - 1 - temp;
      myclip.min_y = clip->min_y;
      myclip.max_y = clip->max_y;
      clip = &myclip;
    }
  }
  if (Machine->orientation & ORIENTATION_FLIP_Y) {
    sy = dest_bmp->height - ((gfx->height * scaley) >> 16) - sy;
    if (clip) {
      int temp;


      myclip.min_x = clip->min_x;
      myclip.max_x = clip->max_x;
      /* clip and myclip might be the same, so we need a temporary storage */
      temp = clip->min_y;
      myclip.min_y = dest_bmp->height - 1 - clip->max_y;
      myclip.max_y = dest_bmp->height - 1 - temp;
      clip = &myclip;
    }
  }


  /* ASG 980209 -- added 16-bit version */
  if (dest_bmp->depth != 16) {
    if (gfx && gfx->colortable) {
      const unsigned short *pal = &gfx->colortable[gfx->color_granularity * (color % gfx->total_colors)]; /* ASG 980209 */
      struct osd_bitmap *source_bmp = gfx->gfxdata;
      int source_base = (code % gfx->total_elements) * gfx->height;

      int sprite_screen_height = (scaley * gfx->height) >> 16;
      int sprite_screen_width = (scalex * gfx->width) >> 16;

      /* compute sprite increment per screen pixel */
      int dx = (gfx->width << 16) / sprite_screen_width;
      int dy = (gfx->height << 16) / sprite_screen_height;

      int ex = sx + sprite_screen_width;
      int ey = sy + sprite_screen_height;

      int x_index_base;
      int y_index;

      if (flipx) {
        x_index_base = (sprite_screen_width - 1) * dx;
        dx = -dx;
      } else {
        x_index_base = 0;
      }

      if (flipy) {
        y_index = (sprite_screen_height - 1) * dy;
        dy = -dy;
      } else {
        y_index = 0;
      }

      if (clip) {
        if (sx < clip->min_x) { /* clip left */
          int pixels = clip->min_x - sx;
          sx += pixels;
          x_index_base += pixels * dx;
        }
        if (sy < clip->min_y) { /* clip top */
          int pixels = clip->min_y - sy;
          sy += pixels;
          y_index += pixels * dy;
        }
        /* NS 980211 - fixed incorrect clipping */
        if (ex > clip->max_x + 1) { /* clip right */
          int pixels = ex - clip->max_x - 1;
          ex -= pixels;
        }
        if (ey > clip->max_y + 1) { /* clip bottom */
          int pixels = ey - clip->max_y - 1;
          ey -= pixels;
        }
      }

      if (ex > sx) { /* skip if inner loop doesn't draw anything */
        int y;

        /* case 1: TRANSPARENCY_PEN */
        if (transparency == TRANSPARENCY_PEN) {
          for (y = sy; y < ey; y++) {
            unsigned char *source = source_bmp->line[source_base + (y_index >> 16)];
            unsigned char *dest = dest_bmp->line[y];

            int x, x_index = x_index_base;
            for (x = sx; x < ex; x++) {
              int c = source[x_index >> 16];
              if (c != transparent_color) dest[x] = pal[c];
              x_index += dx;
            }

            y_index += dy;
          }
        }

        /* case 2: TRANSPARENCY_COLOR */
        else if (transparency == TRANSPARENCY_COLOR) {
          for (y = sy; y < ey; y++) {
            unsigned char *source = source_bmp->line[source_base + (y_index >> 16)];
            unsigned char *dest = dest_bmp->line[y];

            int x, x_index = x_index_base;
            for (x = sx; x < ex; x++) {
              int c = pal[source[x_index >> 16]];
              if (c != transparent_color) dest[x] = c;
              x_index += dx;
            }

            y_index += dy;
          }
        }
      }
    }
  }

  /* ASG 980209 -- new 16-bit part */
  else {
    if (gfx && gfx->colortable) {
      const unsigned short *pal = &gfx->colortable[gfx->color_granularity * (color % gfx->total_colors)]; /* ASG 980209 */
      struct osd_bitmap *source_bmp = gfx->gfxdata;
      int source_base = (code % gfx->total_elements) * gfx->height;

      int sprite_screen_height = (scaley * gfx->height) >> 16;
      int sprite_screen_width = (scalex * gfx->width) >> 16;

      /* compute sprite increment per screen pixel */
      int dx = (gfx->width << 16) / sprite_screen_width;
      int dy = (gfx->height << 16) / sprite_screen_height;

      int ex = sx + sprite_screen_width;
      int ey = sy + sprite_screen_height;

      int x_index_base;
      int y_index;

      if (flipx) {
        x_index_base = (sprite_screen_width - 1) * dx;
        dx = -dx;
      } else {
        x_index_base = 0;
      }

      if (flipy) {
        y_index = (sprite_screen_height - 1) * dy;
        dy = -dy;
      } else {
        y_index = 0;
      }

      if (clip) {
        if (sx < clip->min_x) { /* clip left */
          int pixels = clip->min_x - sx;
          sx += pixels;
          x_index_base += pixels * dx;
        }
        if (sy < clip->min_y) { /* clip top */
          int pixels = clip->min_y - sy;
          sy += pixels;
          y_index += pixels * dy;
        }
        /* NS 980211 - fixed incorrect clipping */
        if (ex > clip->max_x + 1) { /* clip right */
          int pixels = ex - clip->max_x - 1;
          ex -= pixels;
        }
        if (ey > clip->max_y + 1) { /* clip bottom */
          int pixels = ey - clip->max_y - 1;
          ey -= pixels;
        }
      }

      if (ex > sx) { /* skip if inner loop doesn't draw anything */
        int y;

        /* case 1: TRANSPARENCY_PEN */
        if (transparency == TRANSPARENCY_PEN) {
          for (y = sy; y < ey; y++) {
            unsigned char *source = source_bmp->line[source_base + (y_index >> 16)];
            unsigned short *dest = (unsigned short *)dest_bmp->line[y];

            int x, x_index = x_index_base;
            for (x = sx; x < ex; x++) {
              int c = source[x_index >> 16];
              if (c != transparent_color) dest[x] = pal[c];
              x_index += dx;
            }

            y_index += dy;
          }
        }

        /* case 2: TRANSPARENCY_COLOR */
        else if (transparency == TRANSPARENCY_COLOR) {
          for (y = sy; y < ey; y++) {
            unsigned char *source = source_bmp->line[source_base + (y_index >> 16)];
            unsigned short *dest = (unsigned short *)dest_bmp->line[y];

            int x, x_index = x_index_base;
            for (x = sx; x < ex; x++) {
              int c = pal[source[x_index >> 16]];
              if (c != transparent_color) dest[x] = c;
              x_index += dx;
            }

            y_index += dy;
          }
        }
      }
    }
  }
}