#ifndef _vecsim_h_
#define _vecsim_h_
/*
 * vecsim.h: Atari Vector game simulator
 *
 * Copyright 1991, 1992, 1993, 1996 Hedley Rainnie and Eric Smith
 *
 *    This program is free software; you can redistribute it and/or modify
 *    it under the terms of the GNU General Public License as published by
 *    the Free Software Foundation; either version 2 of the License, or
 *    (at your option) any later version.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU General Public License for more details.
 *
 *    You should have received a copy of the GNU General Public License
 *    along with this program; if not, write to the Free Software
 *    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

#define INTBREAK 0x01
#define BREAKPT 0x02
#define BREAK 0x03
#define STEP 0x04
#define ERRORBRK 0x05
#define INSTTAG 0x06 /* instruction fetch from other than normal memory */

#ifdef VG_DEBUG
extern int trace_vgo;
extern int vg_step; /* single step the vector generator */
extern int vg_print;
extern unsigned long last_vgo_cyc;
extern unsigned long vgo_count;
#endif /* VG_DEBUG */

/* The following are B&W with the DVG: */
#define LUNAR_LANDER 1
#define ASTEROIDS 2
#define ASTEROIDS_DX 3
/* B&W with AVG: */
#define RED_BARON 4
#define BATTLEZONE 5 /* 2901 math box */
/* All the rest are color with AVG: */
#define TEMPEST 6 /* 2901 math box */
#define SPACE_DUEL 7
#define GRAVITAR 8
#define BLACK_WIDOW 9
#define MAJOR_HAVOC 10 /* extra 6502 for sound, quad POKEY */
/* The following use two 6809s, a math box, and four POKEYs: */
#define STAR_WARS 11
#define EMPIRE 12
/* The following uses a 68000: */
#define QUANTUM 13

#define FIRST_GAME LUNAR_LANDER
#define LAST_GAME MAJOR_HAVOC /* we don't do 6809 or 68000 (yet) */


/***********************************************************************
*
* cycle and byte count macros
*
***********************************************************************/


/* instruction byte count macro, for translated code only */
#ifdef KEEP_ACCURATE_PC
#define B(val) do { PC += val; } while (0)
#else
#define B(val)
#endif

/* instruction cycle count macro */
#ifdef NO_CYCLE_COUNT
#define C(val)
#else
#define C(val) do { totcycles += val; } while (0)
#endif


/***********************************************************************
*
* flag utilities
*
***********************************************************************/

#define DECLARE_CC \
    register int CC=0; do {SET_I;} while (0)

#define setflags(val) \
    do { \
      CC &= ~ (Z_BIT | N_BIT); \
      if ((val) == 0) \
        CC |= Z_BIT; \
      if ((val) & 0x80) \
        CC |= N_BIT; \
    } while (0)

#define flags_to_byte      (CC)  /* NOTE:  CC is not an argument to the macro! */

#define byte_to_flags(b) do { CC = (b); } while (0)

#define STO_N(val) do { if (val) CC |= N_BIT; else CC &= ~ N_BIT; } while (0)
#define SET_N      do { CC |= N_BIT; } while (0)
#define CLR_N      do { CC &= ~ N_BIT; } while (0)
#define TST_N      ((CC & N_BIT) != 0)

#define STO_V(val) do { if (val) CC |= V_BIT; else CC &= ~ V_BIT; } while (0)
#define SET_V      do { CC |= V_BIT; } while (0)
#define CLR_V      do { CC &= ~ V_BIT; } while (0)
#define TST_V      ((CC & V_BIT) != 0)

#define STO_D(val) do { if (val) CC |= D_BIT; else CC &= ~ D_BIT; } while (0)
#define SET_D      do { CC |= D_BIT; } while (0)
#define CLR_D      do { CC &= ~ D_BIT; } while (0)
#define TST_D      ((CC & D_BIT) != 0)

#define STO_I(val) do { if (val) CC |= I_BIT; else CC &= ~ I_BIT; } while (0)
#define SET_I      do { CC |= I_BIT; } while (0)
#define CLR_I      do { CC &= ~ I_BIT; } while (0)
#define TST_I      ((CC & I_BIT) != 0)

#define STO_Z(val) do { if (val) CC |= Z_BIT; else CC &= ~ Z_BIT; } while (0)
#define SET_Z      do { CC |= Z_BIT; } while (0)
#define CLR_Z      do { CC &= ~ Z_BIT; } while (0)
#define TST_Z      ((CC & Z_BIT) != 0)

#define STO_C(val) do { if (val) CC |= C_BIT; else CC &= ~ C_BIT; } while (0)
#define SET_C      do { CC |= C_BIT; } while (0)
#define CLR_C      do { CC &= ~ C_BIT; } while (0)
#define TST_C      ((CC & C_BIT) != 0)


/***********************************************************************
*
* effective address calculation for simulated code
*
***********************************************************************/
/*
// inline helpers
    static inline bool page_changing(uint16_t base, int delta) { return ((base + delta) ^ base) & 0xff00; }
    static inline uint16_t set_l(uint16_t base, uint8_t val) { return (base & 0xff00) | val; }
    static inline uint16_t set_h(uint16_t base, uint8_t val) { return (base & 0x00ff) | (val << 8); }

IND
    TMP2 = read_pc();
    TMP = read(TMP2);
    TMP = set_h(TMP, read((TMP2+1) & 0xff));
    if(page_changing(TMP, Y)) {
        read(set_l(TMP, TMP+Y));
    }
    do_adc(read(TMP+Y));
    prefetch();

ABS
    TMP = read_pc();
    TMP = set_h(TMP, read_pc());
    if(page_changing(TMP, X)) {
        read(set_l(TMP, TMP+X));
    }
    TMP += X;
    TMP = read(TMP);
    do_adc(TMP);
    prefetch();
*/



#define PAGE_CHECK(p1, p2) do {if (((p1)&0xff00) != ((p2)&0xff00)) C(1);} while (0)
//#define PAGE_CHECK(p1, p2) do {;} while(0)

#define EA_IMM   do { addr = PC++; } while (0)
#define EA_ABS   do { addr = memrdwd (PC,PC,totcycles);     PC += 2; } while (0)
#define EA_ABS_X do { addr = memrdwd (PC,PC,totcycles) + X; PC += 2; } while (0)
#define EA_ABS_Y do { addr = memrdwd (PC,PC,totcycles) + Y; PC += 2; } while (0)
#define EA_ZP    do { addr = memrd (PC,PC,totcycles);       PC++; } while (0)
#define EA_ZP_X  do { addr = (memrd (PC,PC,totcycles) + X) & 0xff; PC++; } while (0)
#define EA_ZP_Y  do { addr = (memrd (PC,PC,totcycles) + Y) & 0xff; PC++; } while (0)

#define EA_ABS_X_C do { addr = memrdwd (PC,PC,totcycles) + X; PAGE_CHECK(addr-X, addr); PC += 2; } while (0)
#define EA_ABS_Y_C do { addr = memrdwd (PC,PC,totcycles) + Y; PAGE_CHECK(addr-Y, addr); PC += 2; } while (0)


#define EA_IND_X do { addr = (memrd (PC,PC,totcycles) + X) & 0xff; addr = memrdwd (addr,PC,totcycles); PC++; } while (0)
/* Note that indirect indexed will do the wrong thing if the zero page address
   plus X is $FF, because the 6502 doesn't generate a carry */

#define EA_IND_Y   do { addr = memrd (PC,PC,totcycles); addr = memrdwd (addr,PC,totcycles) + Y; PC++; } while (0)
#define EA_IND_Y_C do { addr = memrd (PC,PC,totcycles); addr = memrdwd (addr,PC,totcycles) + Y; PAGE_CHECK(addr-Y, addr); PC++; } while (0)
/* Note that indexed indirect will do the wrong thing if the zero page address
   is $FF, because the 6502 doesn't generate a carry */

#define EA_IND   do { addr = memrdwd (PC,PC,totcycles); addr = memrdwd (addr,PC,totcycles); PC += 2; } while (0)
/* Note that this doesn't handle the NMOS 6502 indirect bug, where the low
   byte of the indirect address is $FF */


/***********************************************************************
*
* effective address calculation for translated code
*
***********************************************************************/

/*
 * The translator doesn't use an EA macro for immediate mode, as there are
 * specific instruction macros for immediate mode (DO_LDAI, DO_ADCI, etc.).
 *
 * A useful optimiztion would be to avoid using memrd if the translator
 * knows that the operand is in RAM or ROM.
 */

#define TR_ABS(arg)   do { addr = (arg); } while (0)
#define TR_ABS_X(arg) do { addr = (arg) + X; } while (0)
#define TR_ABS_Y(arg) do { addr = (arg) + Y; } while (0)
#define TR_ZP(arg)    do { addr = (arg); } while (0)
#define TR_ZP_X(arg)  do { addr = ((arg) + X) & 0xff; } while (0)
#define TR_ZP_Y(arg)  do { addr = ((arg) + Y) & 0xff; } while (0)

#define TR_IND_X(arg) do { addr = memrdwd (((arg) + X) & 0xff, PC, totcycles); } while (0)
/* Note that indirect indexed will do the wrong thing if the zero page address
   plus X is $FF, because the 6502 doesn't generate a carry */
/* The translator can't trivially check for this, because it doesn't know
   what is in the X register */

#define TR_IND_Y(arg) do { addr = memrdwd ((arg), PC, totcycles) + Y; } while (0)
/* Note that indexed indirect will do the wrong thing if the zero page address
   is $FF, because the 6502 doesn't generate a carry */
/* The translator should check for this */

#define TR_IND(arg)   do { addr = memrdwd (addr, PC, totcycles); } while (0)   /* IS THIS A BUG?  addr vs arg? */
/* Note that this doesn't handle the NMOS 6502 indirect bug, where the low
   byte of the indirect address is $FF */
/* The translator should check for this */


/***********************************************************************
*
* loads and stores
*
***********************************************************************/

#define DO_LDA  do { A = memrd (addr, PC, totcycles); setflags (A); } while (0)
#define DO_LDX  do { X = memrd (addr, PC, totcycles); setflags (X); } while (0)
#define DO_LDY  do { Y = memrd (addr, PC, totcycles); setflags (Y); } while (0)

#define DO_LDAI(data) do { A = data; } while (0)
#define DO_LDXI(data) do { X = data; } while (0)
#define DO_LDYI(data) do { Y = data; } while (0)

#define DO_STA  memwr (addr, A, PC, totcycles)
#define DO_STX  memwr (addr, X, PC, totcycles)
#define DO_STY  memwr (addr, Y, PC, totcycles)

/***********************************************************************
*
* register transfers
*
***********************************************************************/

#define DO_TAX do { X = A;  setflags (X); } while (0)
#define DO_TAY do { Y = A;  setflags (Y); } while (0)
#define DO_TSX do { X = SP; setflags (X); } while (0)
#define DO_TXA do { A = X;  setflags (A); } while (0)
#define DO_TXS do { SP = X;               } while (0)
#define DO_TYA do { A = Y;  setflags (A); } while (0)

/***********************************************************************
*
* index arithmetic
*
***********************************************************************/

#define DO_DEX do { X = (X - 1) & 0xff; setflags (X); } while (0)
#define DO_DEY do { Y = (Y - 1) & 0xff; setflags (Y); } while (0)
#define DO_INX do { X = (X + 1) & 0xff; setflags (X); } while (0)
#define DO_INY do { Y = (Y + 1) & 0xff; setflags (Y); } while (0)

/***********************************************************************
*
* stack operations
*
***********************************************************************/

#define DO_PHA dopush (A, PC)
#define DO_PHP dopush (flags_to_byte, PC)
#define DO_PLA do { A = dopop(PC); setflags (A); } while (0)
#define DO_PLP do { byte f; f = dopop(PC); byte_to_flags (f); } while (0)

/***********************************************************************
*
* logical instructions
*
***********************************************************************/

#define DO_AND  do { A &= memrd (addr, PC, totcycles); setflags (A); } while (0)
#define DO_ORA  do { A |= memrd (addr, PC, totcycles); setflags (A); } while (0)
#define DO_EOR  do { A ^= memrd (addr, PC, totcycles); setflags (A); } while (0)

#define DO_ANDI(data) do { A &= data; setflags (A); } while (0)
#define DO_ORAI(data) do { A |= data; setflags (A); } while (0)
#define DO_EORI(data) do { A ^= data; setflags (A); } while (0)

#define DO_BIT \
  do { \
    byte bval; \
    bval = memrd(addr, PC, totcycles); \
    STO_N (bval & 0x80); \
    STO_V (bval & 0x40); \
    STO_Z ((A & bval) == 0); \
  } while (0)

/***********************************************************************
*
* arithmetic instructions
*
***********************************************************************/

#define DO_ADCI(data) \
  do { \
    uint16_t wtemp; \
    if(TST_D) \
      { \
    uint16_t nib1, nib2; \
    uint16_t result1, result2; \
    uint16_t result3, result4; \
    wtemp = A; \
    nib1 = (data) & 0xf;			\
    nib2 = wtemp & 0xf; \
    result1 = nib1+nib2+TST_C; /* Add carry */ \
    if(result1 >= 10) \
      { \
        result1 = result1 - 10; \
        result2 = 1; \
      } \
    else \
      result2 = 0; \
    nib1 = ((data) & 0xf0) >> 4;		\
    nib2 = (wtemp & 0xf0) >> 4; \
    result3 = nib1+nib2+result2; \
    if(result3 >= 10) \
      { \
        result3 = result3 - 10; \
        result4 = 1; \
      } \
    else \
      result4 = 0; \
    STO_C (result4); \
    CLR_V; \
    wtemp = (result3 << 4) | (result1); \
    A = wtemp & 0xff; \
    setflags (A); \
      } \
    else \
      { \
    wtemp = A; \
    wtemp += TST_C;    /* add carry */ \
    wtemp += (data);		       \
    STO_C (wtemp & 0x100); \
    STO_V ((((A ^ (data)) & 0x80) == 0) && (((A ^ wtemp) & 0x80) != 0)); \
    A = wtemp & 0xff; \
    setflags (A); \
      } \
  } while (0)

#define DO_SBCI(data) \
  do { \
    uint16_t wtemp; \
    if (TST_D) \
      { \
    int nib1, nib2; \
    int result1, result2; \
    int result3, result4; \
    wtemp = A; \
    nib1 = (data) & 0xf;			\
    nib2 = wtemp & 0xf; \
    result1 = nib2-nib1-!TST_C; /* Sub borrow */ \
    if(result1 < 0) \
      { \
        result1 += 10; \
        result2 = 1; \
      } \
    else \
      result2 = 0; \
    nib1 = ((data) & 0xf0) >> 4;		\
    nib2 = (wtemp & 0xf0) >> 4; \
    result3 = nib2-nib1-result2; \
    if(result3 < 0) \
      { \
        result3 += 10; \
        result4 = 1; \
      } \
    else \
      result4 = 0; \
    STO_C (!result4); \
    CLR_V; \
    wtemp = (result3 << 4) | (result1); \
    A = wtemp & 0xff; \
    setflags (A); \
      } \
    else \
      { \
    wtemp = A; \
    wtemp += TST_C; \
    wtemp += ((data) ^ 0xff);			\
    STO_C (wtemp & 0x100); \
    STO_V ((((A ^ (data)) & 0x80) == 0) && (((A ^ wtemp) & 0x80) != 0)); \
    A = wtemp & 0xff; \
    setflags (A); \
      } \
  } while (0)



#define DO_SBCI_MAME(val) \
  do { \
	if (TST_D) \
	{ \
	  unsigned char c = TST_C ? 0 : 1; \
	  CC &= ~(N_BIT|V_BIT|Z_BIT|C_BIT); \
	  unsigned short int diff = A - (val) - c;	\
	  unsigned char al = (A & 15) - ((val) & 15) - c;	\
	  if(((signed char)(al)) < 0) \
		  al -= 6; \
	  unsigned char ah = (A >> 4) - ((val) >> 4) - (((signed char)(al)) < 0); \
	  if(!(unsigned char)(diff)) \
		  CC |= Z_BIT; \
	  else if(diff & 0x80) \
		  CC |= N_BIT; \
	  if((A^(val)) & (A^diff) & 0x80)	\
		  CC |= V_BIT; \
	  if(!(diff & 0xff00)) \
		  CC |= C_BIT; \
	  if(((signed char)(ah)) < 0) \
		  ah -= 6; \
	  A = ((ah << 4) | (al & 15))&0xff; \
	} \
	else \
	{ \
	  unsigned short int diff = A - (val) - (CC & C_BIT ? 0 : 1);	\
	  CC &= ~(N_BIT|V_BIT|Z_BIT|C_BIT); \
	  if(!(unsigned char)(diff)) \
		  CC |= Z_BIT; \
	  else if(((signed char)(diff)) < 0) \
		  CC |= N_BIT; \
	  if((A^(val)) & (A^diff) & 0x80)	\
		  CC |= V_BIT; \
	  if(!(diff & 0xff00)) \
		  CC |= C_BIT; \
	  A = diff&0xff; \
	} \
  } while (0)


#define DO_ADC   do { byte bval; bval = memrd (addr, PC, totcycles); DO_ADCI (bval); } while (0)
#define DO_SBC   do { byte bval; bval = memrd (addr, PC, totcycles); DO_SBCI_MAME (bval); } while (0)

#define docompare(bval,reg) \
  do { \
    STO_C ((reg) >= (bval));			\
    STO_Z ((reg) == (bval));			\
    STO_N (((reg) - (bval)) & 0x80);		\
  } while (0)

#define DO_CMP   do { byte bval; bval = memrd (addr, PC, totcycles); docompare (bval, A); } while (0)
#define DO_CPX   do { byte bval; bval = memrd (addr, PC, totcycles); docompare (bval, X); } while (0)
#define DO_CPY   do { byte bval; bval = memrd (addr, PC, totcycles); docompare (bval, Y); } while (0)

#define DO_CMPI(data)  docompare (data, A)
#define DO_CPXI(data)  docompare (data, X)
#define DO_CPYI(data)  docompare (data, Y)

/***********************************************************************
*
* read/modify/write instructions (INC, DEC, shifts and rotates)
*
***********************************************************************/

#define DO_INC \
  do { \
    byte bval; \
    bval = memrd(addr, PC, totcycles) + 1; \
    setflags (bval); \
    memwr(addr,bval, PC, totcycles); \
  } while (0)

#define DO_DEC \
  do { \
    byte bval; \
    bval = memrd(addr, PC, totcycles) - 1; \
    setflags (bval); \
    memwr(addr,bval, PC, totcycles); \
  } while (0)

#define DO_ROR_int(val) \
  do { \
    byte oldC = TST_C; \
    STO_C ((val) & 0x01);			\
    val >>= 1; \
    if (oldC) \
      val |= 0x80; \
    setflags (val); \
  } while (0)

#define DO_RORA   DO_ROR_int(A)

#define DO_ROR \
  do { \
    byte bval; \
    bval = memrd (addr, PC, totcycles); \
    DO_ROR_int (bval); \
    memwr (addr, bval, PC, totcycles); \
  } while (0)

#define DO_ROL_int(val) \
  do { \
    byte oldC = TST_C; \
    STO_C ((val) & 0x80);    \
    val = ((val) << 1) & 0xff;			\
    val |= oldC; \
    setflags (val); \
  } while (0)

#define DO_ROLA   DO_ROL_int(A)

#define DO_ROL \
  do { \
    byte bval; \
    bval = memrd (addr, PC, totcycles); \
    DO_ROL_int (bval); \
    memwr (addr, bval, PC, totcycles); \
  } while (0)

#define DO_ASL_int(val) \
  do { \
    STO_C ((val) & 0x80);    \
    val = ((val) << 1) & 0xff;			\
    setflags (val); \
  } while (0)

#define DO_ASLA   DO_ASL_int(A)

#define DO_ASL \
  do { \
    byte bval; \
    bval = memrd (addr, PC, totcycles); \
    DO_ASL_int (bval); \
    memwr (addr, bval, PC, totcycles); \
  } while (0)

#define DO_LSR_int(val) \
  do { \
    STO_C ((val) & 0x01);			\
    val >>= 1; \
    setflags (val); \
  } while (0)

#define DO_LSRA   DO_LSR_int(A)

#define DO_LSR \
  do { \
    byte bval; \
    bval = memrd (addr, PC, totcycles); \
    DO_LSR_int (bval); \
    memwr (addr, bval, PC, totcycles); \
  } while (0)

/***********************************************************************
*
* flag manipulation
*
***********************************************************************/

#define DO_CLC   CLR_C
#define DO_CLD   CLR_D
#define DO_CLI   CLR_I
#define DO_CLV   CLR_V

#define DO_SEC   SET_C
#define DO_SED   SET_D
#define DO_SEI   SET_I

/***********************************************************************
*
* instruction flow:  branches, jumps, calls, returns, SWI
*
***********************************************************************/

#define DO_JMP   do { PC = addr; } while (0)

#define TR_JMP   do { PC = addr; continue; } while (0)

#define DO_JSR \
  do { \
    PC--; \
    dopush(PC >> 8, PC); \
    dopush(PC & 0xff, PC); \
    PC = addr; \
  } while (0)

/*
 * Note that the argument to TR_JSR is the address of the JSR instruction,
 * _not_ the address of the target.  The address of the target has to be
 * set up beforehand by the TR_EA_ABS macro or the like.
 */
#define TR_JSR(arg) \
  do { \
    dopush (((arg) + 2) >> 8, PC); \
    dopush (((arg) + 2) & 0xff, PC);		\
    PC = addr; \
    continue; \
  } while (0)

#define DO_RTI \
  do { \
    byte f; \
    f = dopop(PC); \
    byte_to_flags (f); \
    PC = dopop(PC); /* & 0xff */ \
    PC |= dopop(PC) << 8; \
  } while (0)

#define TR_RTI \
  do { \
    byte f; \
    f = dopop(PC); \
    byte_to_flags (f); \
    PC = dopop(PC); /* & 0xff */ \
    PC |= dopop(PC) << 8; \
    continue; \
  } while (0)

#define DO_RTS \
  do { \
    PC = dopop(PC); /* & 0xff */ \
    PC |= dopop(PC) << 8; \
    PC++; \
  } while (0)

#define TR_RTS \
  do { \
    PC = dopop(PC); /* & 0xff */ \
    PC |= dopop(PC) << 8; \
    PC++; \
    continue; \
  } while (0)

#if 0
#define DO_BRK \
  do { \
    ; \
  } while (0)

#define TR_BRK(arg) \
  do { \
    ; \
  } while (0)
#endif

// Malban add one cycle if page is changed
#define dobranch(bit,sign) \
  do { \
    int offset; \
    if ((bit) == (sign))			\
      { \
      C(1); \
    offset = memrd (PC, PC, totcycles); \
    int pageStart=(PC+1)&0xff00; \
    if (offset & 0x80) \
          offset |= 0xff00; \
    PC = (PC + 1 + offset) & 0xffff; \
    int pageEnd=PC&0xff00;  \
    if (pageStart != pageEnd) C(1); \
      } \
    else \
      PC++; \
  } while (0)

#define trbranch(bit,sign,dest) \
  do { \
    if ((bit) == (sign))			\
      { \
        PC = (dest);				\
        continue; \
      } \
  } while (0)

#define DO_BCC dobranch (TST_C, 0)
#define DO_BCS dobranch (TST_C, 1)
#define DO_BEQ dobranch (TST_Z, 1)
#define DO_BMI dobranch (TST_N, 1)
#define DO_BNE dobranch (TST_Z, 0)
#define DO_BPL dobranch (TST_N, 0)
#define DO_BVC dobranch (TST_V, 0)
#define DO_BVS dobranch (TST_V, 1)

#define TR_BCC(addr) trbranch (TST_C, 0, addr)
#define TR_BCS(addr) trbranch (TST_C, 1, addr)
#define TR_BEQ(addr) trbranch (TST_Z, 1, addr)
#define TR_BMI(addr) trbranch (TST_N, 1, addr)
#define TR_BNE(addr) trbranch (TST_Z, 0, addr)
#define TR_BPL(addr) trbranch (TST_N, 0, addr)
#define TR_BVC(addr) trbranch (TST_V, 0, addr)
#define TR_BVS(addr) trbranch (TST_V, 1, addr)

/***********************************************************************
*
* misc.
*
***********************************************************************/

#define DO_NOP
/*
 * mathbox.h: math box simulation (Battlezone/Red Baron/Tempest)
 */

/* types of access */
#define RD 1
#define WR 2

/* tags */
#define MEMORY 0  /* memory, no special processing */
#define ROMWRT 1  /* ROM write, just print a message */
#define IGNWRT 2  /* spurious write that we don't care about */
#define UNKNOWN 3 /* don't know what it is */

#define COININ 4  /* coin, slam, self test, diag, VG halt, 3KHz inputs */
#define COINOUT 5 /* coin counter and invert X & Y outputs */
#define WDCLR 6
#define INTACK 7

#define VGRST 8
#define VGO 9     /* VGO */
#define DMACNT 10 /* DVG only */

#define VECRAM MEMORY /* Vector RAM */
/*
 * VECRAM used to be 11 so we could do some special debugging stuff on
 * writes, but that is no longer necessary.
 */

#define COLORRAM 12

#define POKEY1 13 /* Pokey */
#define POKEY2 14 /* Pokey */
#define POKEY3 15 /* Pokey */
#define POKEY4 16 /* Pokey */

#define OPTSW1 17
#define OPTSW2 18
#define OPT1_2BIT 19

/* 20 and 21 no longer used */

#define EAROMCON 22
#define EAROMWR 23
#define EAROMRD 24

#define MBLO 25
#define MBHI 26
#define MBSTART 27
#define MBSTAT 28

#define GRAVITAR_IN1 29
#define GRAVITAR_IN2 30

#define SD_INPUTS 31

#define TEMP_OUTPUTS 32

#define BZ_SOUND 33
#define BZ_INPUTS 34

#define LUNAR_MEM 35
#define LUNAR_SW1 36
#define LUNAR_SW2 37
#define LUNAR_POT 38
#define LUNAR_OUT 39
#define LUNAR_SND 40
#define LUNAR_SND_RST 41

#define ASTEROIDS_SW1 42
#define ASTEROIDS_SW2 43
#define ASTEROIDS_OUT 44
#define ASTEROIDS_EXP 45
#define ASTEROIDS_THUMP 46
#define ASTEROIDS_SND 47
#define ASTEROIDS_SND_RST 48

#define AST_DEL_OUT 49

#define RB_SW 50
#define RB_SND 51
#define RB_SND_RST 52
#define RB_JOY 53

#define TEMPEST_PROTECTTION_0 54

#define BREAKTAG 0x80


#define MAX_OPT_REG 3

typedef struct {
  int32_t left;
  int32_t right;
  int32_t fire;
  int32_t thrust;
  int32_t xhyper;  // calling it hyper causes a conflict with the hyper function when including windows.h
  int32_t shield;
  int32_t abort;
} switch_rec;

#define leftfwd left
#define leftrev thrust
#define rightfwd right
#define rightrev xhyper

extern switch_rec switches[2];

typedef struct {
  uint8_t x;
  uint8_t y;
} joystick_rec;

extern joystick_rec joystick;

typedef struct {
  const char *name;
  uint32_t addr;
  uint32_t len;
  uint32_t offset;
} rom_info;

typedef struct {
  uint16_t addr;
  uint32_t len;
  uint32_t dir;
  uint32_t tag;
} tag_info;

#define MAX_POKEY 4 /* POKEYs are numbered 0 .. MAX_POKEY - 1 */

uint8_t pokey_read(int pokeynum, int reg, int PC, unsigned long cyc);
void pokey_write(int pokeynum, int reg, uint8_t val, int PC, unsigned long cyc);

#ifdef FIFO
struct _fifo {
  unsigned PC;
  unsigned SP;
  uint8_t A;
  uint8_t X;
  uint8_t Y;
  uint8_t flags;
};
extern struct _fifo fifo[0x10000];
extern unsigned short pcpos;
#endif

#ifdef INST_COUNT
extern unsigned long icount;
#endif

#define WRAP_CYC_COUNT 1000000000

#ifdef COUNT_INTERRUPTS
extern unsigned long int_count;
extern unsigned long int_quit;
#endif

void sim_6502(void);

#define N_BIT 0x80
#define V_BIT 0x40
#define B_BIT 0x10
#define D_BIT 0x08
#define I_BIT 0x04
#define Z_BIT 0x02
#define C_BIT 0x01

#endif