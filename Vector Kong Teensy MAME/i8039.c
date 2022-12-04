/**************************************************************************
 *                      Intel 8039 Portable Emulator                      *
 *                                                                        *
 *                   Copyright (C) 1997 by Mirko Buffoni                  *
 *  Based on the original work (C) 1997 by Dan Boris, an 8048 emulator    *
 *     You are not allowed to distribute this software commercially       *
 *        Please, notify me, if you make any changes to this file         *
 **************************************************************************/

#include <stdio.h>
#include <string.h>
#include "stdlib.h"
#include "i8039.h"
#include "driver.h"


#define M_RDMEM(A)      I8039_RDMEM(A)
#define M_RDOP(A)       I8039_RDOP(A)
#define M_RDOP_ARG(A)   I8039_RDOP_ARG(A)
#define M_IN(A)		I8039_In(A)
#define M_OUT(A,V)	I8039_Out(A,V)

#if OLDPORTHANDLING
        #define port_r(A)	I8039_port_r(A)
        #define port_w(A,V)	I8039_port_w(A,V)
        #define test_r(A)	I8039_test_r(A)
        #define test_w(A,V)	I8039_test_w(A,V)
        #define bus_r		I8039_bus_r
        #define bus_w(V)	I8039_bus_w(V)
#else
        #define port_r(A)	I8039_In(I8039_p0 + A)
        #define port_w(A,V)	I8039_Out(I8039_p0 + A,V)
        #define test_r(A)	I8039_In(I8039_t0 + A)
        #define test_w(A,V)	I8039_Out(I8039_t0 + A,V)
        #define bus_r()		I8039_In(I8039_bus)
        #define bus_w(V)	I8039_Out(I8039_bus,V)
#endif

#define C_FLAG          0x80
#define A_FLAG          0x40
#define F_FLAG          0x20
#define B_FLAG          0x10

static I8039_Regs R;
int    I8039_ICount;
static byte Old_T1;

typedef void (*opcode_fn) (void);

#define POSITIVE_EDGE_T1  (((T1-Old_T1) > 0) ? 1 : 0)
#define NEGATIVE_EDGE_T1  (((Old_T1-T1) > 0) ? 1 : 0)

#define M_Cy    ((R.PSW & C_FLAG) >> 7)
#define M_Cn    (!M_Cy)
#define M_Ay    ((R.PSW & A_FLAG))
#define M_An    (!M_Ay)
#define M_F0y   ((R.PSW & F_FLAG))
#define M_F0n   (!M_F0y)
#define M_By    ((R.PSW & B_FLAG))
#define M_Bn    (!M_By)

#define intRAM	R.RAM
#define regPTR	R.regPtr

#define REG0	intRAM[regPTR  ]
#define REG1	intRAM[regPTR+1]
#define REG2	intRAM[regPTR+2]
#define REG3	intRAM[regPTR+3]
#define REG4	intRAM[regPTR+4]
#define REG5	intRAM[regPTR+5]
#define REG6	intRAM[regPTR+6]
#define REG7	intRAM[regPTR+7]


INLINE void CLR (byte flag) { R.PSW &= ~flag; }
INLINE void SET (byte flag) { R.PSW |= flag;  }


/* Get next opcode argument and increment program counter */
INLINE unsigned M_RDMEM_OPCODE (void)
{
        unsigned retval;
        retval=M_RDOP_ARG(R.PC.W.w1);
        R.PC.W.w1++;
        return retval;
}

INLINE void push(byte d)
{
	intRAM[8+R.SP++] = d;
        R.SP  = R.SP & 0x0f;
        R.PSW = R.PSW & 0xf8;
        R.PSW = R.PSW | (R.SP >> 1);
}

INLINE byte pull(void) {
	R.SP  = (R.SP + 15) & 0x0f;		/*  if (--R.SP < 0) R.SP = 15;  */
        R.PSW = R.PSW & 0xF8;
        R.PSW = R.PSW | (R.SP >> 1);
	/* regPTR = ((M_By) ? 24 : 0);  regPTR should not change */
	return intRAM[8+R.SP];
}

INLINE void daa_a(void)
{
	byte dat;
	if (((R.A & 0x0f) > 9) || (A_FLAG)) R.A += 6;
	dat = R.A >> 4;
	if ((dat > 9) || (C_FLAG)) { dat += 6; SET(C_FLAG); }
	else CLR(C_FLAG);
	R.A = (R.A & 0x0f) | (dat << 4);
}

INLINE void M_ADD(byte dat)
{
	word temp;

	CLR(C_FLAG | A_FLAG);
	if ((R.A & 0xf) + (dat & 0xf) > 0xf) SET(A_FLAG);
	temp = R.A + dat;
	if (temp > 0xff) SET(C_FLAG);
	R.A  = temp & 0xff;
}

INLINE void M_ADDC(byte dat)
{
	word temp;

	CLR(A_FLAG);
	if ((R.A & 0xf) + (dat & 0xf) + M_Cy > 0xf) SET(A_FLAG);
	temp = R.A + dat + M_Cy;
	CLR(C_FLAG);
	if (temp > 0xff) SET(C_FLAG);
	R.A  = temp & 0xff;
}

INLINE void M_CALL(word addr)
{
	push(R.PC.B.l);
	push((R.PC.B.h & 0x0f) | (R.PSW & 0xf0));
	R.PC.W.w1 = addr;
	/*change_pc(addr);*/
}

INLINE void M_XCHD(byte addr)
{
	byte dat = R.A & 0x0f;
	R.A &= 0xf0;
	R.A |= intRAM[addr] & 0x0f;
	intRAM[addr] &= 0xf0;
	intRAM[addr] |= dat;
}


INLINE void M_ILLEGAL(void)
{
}

INLINE void M_UNDEFINED(void)
{
}


#if OLDPORTHANDLING
        byte I8039_port_r(byte port)     { return R.p[port & 7]; }
        void I8039_port_w(byte port, byte data) { R.p[port & 7] = data; }

        byte I8039_test_r(byte port)     { return R.t[port & 1]; }
        void I8039_test_w(byte port, byte data) { R.t[port & 1] = data; }

        byte I8039_bus_r(void)      { return R.bus; }
        void I8039_bus_w(byte data) { R.bus = data; }
#endif

static void illegal(void)    { M_ILLEGAL(); }

static void add_a_n(void)    { M_ADD(M_RDMEM_OPCODE()); }
static void add_a_r0(void)   { M_ADD(REG0); }
static void add_a_r1(void)   { M_ADD(REG1); }
static void add_a_r2(void)   { M_ADD(REG2); }
static void add_a_r3(void)   { M_ADD(REG3); }
static void add_a_r4(void)   { M_ADD(REG4); }
static void add_a_r5(void)   { M_ADD(REG5); }
static void add_a_r6(void)   { M_ADD(REG6); }
static void add_a_r7(void)   { M_ADD(REG7); }
static void add_a_xr0(void)  { M_ADD(intRAM[REG0 & 0x7f]); }
static void add_a_xr1(void)  { M_ADD(intRAM[REG1 & 0x7f]); }
static void adc_a_n(void)    { M_ADDC(M_RDMEM_OPCODE()); }
static void adc_a_r0(void)   { M_ADDC(REG0); }
static void adc_a_r1(void)   { M_ADDC(REG1); }
static void adc_a_r2(void)   { M_ADDC(REG2); }
static void adc_a_r3(void)   { M_ADDC(REG3); }
static void adc_a_r4(void)   { M_ADDC(REG4); }
static void adc_a_r5(void)   { M_ADDC(REG5); }
static void adc_a_r6(void)   { M_ADDC(REG6); }
static void adc_a_r7(void)   { M_ADDC(REG7); }
static void adc_a_xr0(void)  { M_ADDC(intRAM[REG0 & 0x7f]); }
static void adc_a_xr1(void)  { M_ADDC(intRAM[REG1 & 0x7f]); }
static void anl_a_n(void)    { R.A &= M_RDMEM_OPCODE(); }
static void anl_a_r0(void)   { R.A &= REG0; }
static void anl_a_r1(void)   { R.A &= REG1; }
static void anl_a_r2(void)   { R.A &= REG2; }
static void anl_a_r3(void)   { R.A &= REG3; }
static void anl_a_r4(void)   { R.A &= REG4; }
static void anl_a_r5(void)   { R.A &= REG5; }
static void anl_a_r6(void)   { R.A &= REG6; }
static void anl_a_r7(void)   { R.A &= REG7; }
static void anl_a_xr0(void)  { R.A &= intRAM[REG0 & 0x7f]; }
static void anl_a_xr1(void)  { R.A &= intRAM[REG1 & 0x7f]; }
static void anl_bus_n(void)  { bus_w( bus_r() & M_RDMEM_OPCODE() ); }
static void anl_p1_n(void)   { port_w( 1, port_r(1) & M_RDMEM_OPCODE() ); }
static void anl_p2_n(void)   { port_w( 2, port_r(2) & M_RDMEM_OPCODE() ); }
static void anld_p4_a(void)  { port_w( 4, port_r(4) & M_RDMEM_OPCODE() ); }
static void anld_p5_a(void)  { port_w( 5, port_r(5) & M_RDMEM_OPCODE() ); }
static void anld_p6_a(void)  { port_w( 6, port_r(6) & M_RDMEM_OPCODE() ); }
static void anld_p7_a(void)  { port_w( 7, port_r(7) & M_RDMEM_OPCODE() ); }
static void call(void)       { byte i=M_RDMEM_OPCODE(); M_CALL(i | R.A11); }
static void call_1(void)     { byte i=M_RDMEM_OPCODE(); M_CALL(i | 0x100 | R.A11); }
static void call_2(void)     { byte i=M_RDMEM_OPCODE(); M_CALL(i | 0x200 | R.A11); }
static void call_3(void)     { byte i=M_RDMEM_OPCODE(); M_CALL(i | 0x300 | R.A11); }
static void call_4(void)     { byte i=M_RDMEM_OPCODE(); M_CALL(i | 0x400 | R.A11); }
static void call_5(void)     { byte i=M_RDMEM_OPCODE(); M_CALL(i | 0x500 | R.A11); }
static void call_6(void)     { byte i=M_RDMEM_OPCODE(); M_CALL(i | 0x600 | R.A11); }
static void call_7(void)     { byte i=M_RDMEM_OPCODE(); M_CALL(i | 0x700 | R.A11); }
static void clr_a(void)      { R.A=0; }
static void clr_c(void)      { CLR(C_FLAG); }
static void clr_f0(void)     { CLR(F_FLAG); }
static void clr_f1(void)     { R.f1 = 0; }
static void cpl_a(void)      { R.A ^= 0xff; }
static void cpl_c(void)      { R.PSW ^= C_FLAG; }
static void cpl_f0(void)     { R.PSW ^= F_FLAG; }
static void cpl_f1(void)     { R.f1 ^= 1; }
static void dec_a(void)      { R.A--; }
static void dec_r0(void)     { REG0--; }
static void dec_r1(void)     { REG1--; }
static void dec_r2(void)     { REG2--; }
static void dec_r3(void)     { REG3--; }
static void dec_r4(void)     { REG4--; }
static void dec_r5(void)     { REG5--; }
static void dec_r6(void)     { REG6--; }
static void dec_r7(void)     { REG7--; }
static void dis_i(void)      { R.xirq_en = 0; }
static void dis_tcnti(void)  { R.tirq_en = 0; }
static void djnz_r0(void)    { byte i=M_RDMEM_OPCODE(); REG0--; if (REG0 != 0) { R.PC.W.w1 = (R.PC.W.w1 & 0xf00) | i; /*change_pc(R.PC.W.w1);*/ } }
static void djnz_r1(void)    { byte i=M_RDMEM_OPCODE(); REG1--; if (REG1 != 0) { R.PC.W.w1 = (R.PC.W.w1 & 0xf00) | i; /*change_pc(R.PC.W.w1);*/ } }
static void djnz_r2(void)    { byte i=M_RDMEM_OPCODE(); REG2--; if (REG2 != 0) { R.PC.W.w1 = (R.PC.W.w1 & 0xf00) | i; /*change_pc(R.PC.W.w1);*/ } }
static void djnz_r3(void)    { byte i=M_RDMEM_OPCODE(); REG3--; if (REG3 != 0) { R.PC.W.w1 = (R.PC.W.w1 & 0xf00) | i; /*change_pc(R.PC.W.w1);*/ } }
static void djnz_r4(void)    { byte i=M_RDMEM_OPCODE(); REG4--; if (REG4 != 0) { R.PC.W.w1 = (R.PC.W.w1 & 0xf00) | i; /*change_pc(R.PC.W.w1);*/ } }
static void djnz_r5(void)    { byte i=M_RDMEM_OPCODE(); REG5--; if (REG5 != 0) { R.PC.W.w1 = (R.PC.W.w1 & 0xf00) | i; /*change_pc(R.PC.W.w1);*/ } }
static void djnz_r6(void)    { byte i=M_RDMEM_OPCODE(); REG6--; if (REG6 != 0) { R.PC.W.w1 = (R.PC.W.w1 & 0xf00) | i; /*change_pc(R.PC.W.w1);*/ } }
static void djnz_r7(void)    { byte i=M_RDMEM_OPCODE(); REG7--; if (REG7 != 0) { R.PC.W.w1 = (R.PC.W.w1 & 0xf00) | i; /*change_pc(R.PC.W.w1);*/ } }
static void en_i(void)       { R.xirq_en = 1; }
static void en_tcnti(void)   { R.tirq_en = 1; }
static void ento_clk(void)   { M_UNDEFINED(); }
static void in_a_p1(void)    { R.A = port_r(1); }
static void in_a_p2(void)    { R.A = port_r(2); }
static void ins_a_bus(void)  { R.A = bus_r(); }
static void inc_a(void)      { R.A++; }
static void inc_r0(void)     { REG0++; }
static void inc_r1(void)     { REG1++; }
static void inc_r2(void)     { REG2++; }
static void inc_r3(void)     { REG3++; }
static void inc_r4(void)     { REG4++; }
static void inc_r5(void)     { REG5++; }
static void inc_r6(void)     { REG6++; }
static void inc_r7(void)     { REG7++; }
static void inc_xr0(void)    { intRAM[REG0 & 0x7F]++; }
static void inc_xr1(void)    { intRAM[REG1 & 0x7F]++; }

/* static void jmp(void)        { byte i=M_RDOP(R.PC.W.w1); R.PC.W.w1 = i | R.A11; }
 */

static void jmp(void)
{
  byte i=M_RDOP(R.PC.W.w1);
  word oldpc,newpc;

  oldpc = R.PC.W.w1-1;
 R.PC.W.w1 = i | R.A11;         /*change_pc(R.PC.W.w1);*/
  newpc = R.PC.W.w1;
  if (newpc == oldpc) { if (I8039_ICount > 0) I8039_ICount = 0; } /* speed up busy loop */
  else if (newpc == oldpc-1 && M_RDOP(newpc) == 0x00)	/* NOP - Gyruss */
	  { if (I8039_ICount > 0) I8039_ICount = 0; }
}
static void jmp_1(void)      { byte i=M_RDOP(R.PC.W.w1); R.PC.W.w1 = i | 0x100 | R.A11; /*change_pc(R.PC.W.w1);*/ }
static void jmp_2(void)      { byte i=M_RDOP(R.PC.W.w1); R.PC.W.w1 = i | 0x200 | R.A11; /*change_pc(R.PC.W.w1);*/ }
static void jmp_3(void)      { byte i=M_RDOP(R.PC.W.w1); R.PC.W.w1 = i | 0x300 | R.A11; /*change_pc(R.PC.W.w1);*/ }
static void jmp_4(void)      { byte i=M_RDOP(R.PC.W.w1); R.PC.W.w1 = i | 0x400 | R.A11; /*change_pc(R.PC.W.w1);*/ }
static void jmp_5(void)      { byte i=M_RDOP(R.PC.W.w1); R.PC.W.w1 = i | 0x500 | R.A11; /*change_pc(R.PC.W.w1);*/ }
static void jmp_6(void)      { byte i=M_RDOP(R.PC.W.w1); R.PC.W.w1 = i | 0x600 | R.A11; /*change_pc(R.PC.W.w1);*/ }
static void jmp_7(void)      { byte i=M_RDOP(R.PC.W.w1); R.PC.W.w1 = i | 0x700 | R.A11; /*change_pc(R.PC.W.w1);*/ }
static void jmpp_xa(void)    { word addr = (R.PC.W.w1 & 0xf00) | R.A; R.PC.W.w1 = (R.PC.W.w1 & 0xf00) | M_RDMEM(addr); /*change_pc(R.PC.W.w1);*/ }
static void jb_0(void)       { byte i=M_RDMEM_OPCODE(); if (R.A & 0x01) { R.PC.W.w1 = (R.PC.W.w1 & 0xf00) | i; /*change_pc(R.PC.W.w1);*/ } }
static void jb_1(void)       { byte i=M_RDMEM_OPCODE(); if (R.A & 0x02) { R.PC.W.w1 = (R.PC.W.w1 & 0xf00) | i; /*change_pc(R.PC.W.w1);*/ } }
static void jb_2(void)       { byte i=M_RDMEM_OPCODE(); if (R.A & 0x04) { R.PC.W.w1 = (R.PC.W.w1 & 0xf00) | i; /*change_pc(R.PC.W.w1);*/ } }
static void jb_3(void)       { byte i=M_RDMEM_OPCODE(); if (R.A & 0x08) { R.PC.W.w1 = (R.PC.W.w1 & 0xf00) | i; /*change_pc(R.PC.W.w1);*/ } }
static void jb_4(void)       { byte i=M_RDMEM_OPCODE(); if (R.A & 0x10) { R.PC.W.w1 = (R.PC.W.w1 & 0xf00) | i; /*change_pc(R.PC.W.w1);*/ } }
static void jb_5(void)       { byte i=M_RDMEM_OPCODE(); if (R.A & 0x20) { R.PC.W.w1 = (R.PC.W.w1 & 0xf00) | i; /*change_pc(R.PC.W.w1);*/ } }
static void jb_6(void)       { byte i=M_RDMEM_OPCODE(); if (R.A & 0x40) { R.PC.W.w1 = (R.PC.W.w1 & 0xf00) | i; /*change_pc(R.PC.W.w1);*/ } }
static void jb_7(void)       { byte i=M_RDMEM_OPCODE(); if (R.A & 0x80) { R.PC.W.w1 = (R.PC.W.w1 & 0xf00) | i; /*change_pc(R.PC.W.w1);*/ } }
static void jf0(void)        { byte i=M_RDMEM_OPCODE(); if (M_F0y) { R.PC.W.w1 = (R.PC.W.w1 & 0xf00) | i; /*change_pc(R.PC.W.w1);*/ } }
static void jf_1(void)       { byte i=M_RDMEM_OPCODE(); if (R.f1)  { R.PC.W.w1 = (R.PC.W.w1 & 0xf00) | i; /*change_pc(R.PC.W.w1);*/ } }
static void jnc(void)        { byte i=M_RDMEM_OPCODE(); if (M_Cn)  { R.PC.W.w1 = (R.PC.W.w1 & 0xf00) | i; /*change_pc(R.PC.W.w1);*/ } }
static void jc(void)         { byte i=M_RDMEM_OPCODE(); if (M_Cy)  { R.PC.W.w1 = (R.PC.W.w1 & 0xf00) | i; /*change_pc(R.PC.W.w1);*/ } }
static void jni(void)        { byte i=M_RDMEM_OPCODE(); if (R.pending_irq == I8039_EXT_INT) { R.PC.W.w1 = (R.PC.W.w1 & 0xf00) | i; /*change_pc(R.PC.W.w1);*/ } }
static void jnt_0(void)      { byte i=M_RDMEM_OPCODE(); if (!test_r(0))	{ R.PC.W.w1 = (R.PC.W.w1 & 0xf00) | i; /*change_pc(R.PC.W.w1);*/ } }
static void jt_0(void)       { byte i=M_RDMEM_OPCODE(); if (test_r(0))	{ R.PC.W.w1 = (R.PC.W.w1 & 0xf00) | i; /*change_pc(R.PC.W.w1);*/ } }
static void jnt_1(void)      { byte i=M_RDMEM_OPCODE(); if (!test_r(1))	{ R.PC.W.w1 = (R.PC.W.w1 & 0xf00) | i; /*change_pc(R.PC.W.w1);*/ } }
static void jt_1(void)       { byte i=M_RDMEM_OPCODE(); if (test_r(1))	{ R.PC.W.w1 = (R.PC.W.w1 & 0xf00) | i; /*change_pc(R.PC.W.w1);*/ } }
static void jnz(void)        { byte i=M_RDMEM_OPCODE(); if (R.A != 0)	{ R.PC.W.w1 = (R.PC.W.w1 & 0xf00) | i; /*change_pc(R.PC.W.w1);*/ } }
static void jz(void)         { byte i=M_RDMEM_OPCODE(); if (R.A == 0)	{ R.PC.W.w1 = (R.PC.W.w1 & 0xf00) | i; /*change_pc(R.PC.W.w1);*/ } }
static void jtf(void)        { byte i=M_RDMEM_OPCODE(); if (R.t_flag)	{ R.PC.W.w1 = (R.PC.W.w1 & 0xf00) | i; /*change_pc(R.PC.W.w1);*/ R.t_flag = 0; } }
static void mov_a_n(void)    { R.A = M_RDMEM_OPCODE(); }
static void mov_a_r0(void)   { R.A = REG0; }
static void mov_a_r1(void)   { R.A = REG1; }
static void mov_a_r2(void)   { R.A = REG2; }
static void mov_a_r3(void)   { R.A = REG3; }
static void mov_a_r4(void)   { R.A = REG4; }
static void mov_a_r5(void)   { R.A = REG5; }
static void mov_a_r6(void)   { R.A = REG6; }
static void mov_a_r7(void)   { R.A = REG7; }
static void mov_a_psw(void)  { R.A = R.PSW; }
static void mov_a_xr0(void)  { R.A = intRAM[REG0 & 0x7f]; }
static void mov_a_xr1(void)  { R.A = intRAM[REG1 & 0x7f]; }
static void mov_r0_a(void)   { REG0 = R.A; }
static void mov_r1_a(void)   { REG1 = R.A; }
static void mov_r2_a(void)   { REG2 = R.A; }
static void mov_r3_a(void)   { REG3 = R.A; }
static void mov_r4_a(void)   { REG4 = R.A; }
static void mov_r5_a(void)   { REG5 = R.A; }
static void mov_r6_a(void)   { REG6 = R.A; }
static void mov_r7_a(void)   { REG7 = R.A; }
static void mov_psw_a(void)  { R.PSW = R.A; regPTR = ((M_By) ? 24 : 0); R.SP = (R.PSW & 7) << 1; }
static void mov_r0_n(void)   { REG0 = M_RDMEM_OPCODE(); }
static void mov_r1_n(void)   { REG1 = M_RDMEM_OPCODE(); }
static void mov_r2_n(void)   { REG2 = M_RDMEM_OPCODE(); }
static void mov_r3_n(void)   { REG3 = M_RDMEM_OPCODE(); }
static void mov_r4_n(void)   { REG4 = M_RDMEM_OPCODE(); }
static void mov_r5_n(void)   { REG5 = M_RDMEM_OPCODE(); }
static void mov_r6_n(void)   { REG6 = M_RDMEM_OPCODE(); }
static void mov_r7_n(void)   { REG7 = M_RDMEM_OPCODE(); }
static void mov_a_t(void)    { R.A = R.timer; }
static void mov_t_a(void)    { R.timer = R.A; }
static void mov_xr0_a(void)  { intRAM[REG0 & 0x7f] = R.A; }
static void mov_xr1_a(void)  { intRAM[REG1 & 0x7f] = R.A; }
static void mov_xr0_n(void)  { intRAM[REG0 & 0x7f] = M_RDMEM_OPCODE(); }
static void mov_xr1_n(void)  { intRAM[REG1 & 0x7f] = M_RDMEM_OPCODE(); }
static void movd_a_p4(void)  { R.A = port_r(4); }
static void movd_a_p5(void)  { R.A = port_r(5); }
static void movd_a_p6(void)  { R.A = port_r(6); }
static void movd_a_p7(void)  { R.A = port_r(7); }
static void movd_p4_a(void)  { port_w(4, R.A); }
static void movd_p5_a(void)  { port_w(5, R.A); }
static void movd_p6_a(void)  { port_w(6, R.A); }
static void movd_p7_a(void)  { port_w(7, R.A); }
static void movp_a_xa(void)  { R.A = M_RDMEM((R.PC.W.w1 & 0x0f00) | R.A); }
static void movp3_a_xa(void) { R.A = M_RDMEM(0x300 | R.A); }
static void movx_a_xr0(void) { R.A = M_IN(REG0); }
static void movx_a_xr1(void) { R.A = M_IN(REG1); }
static void movx_xr0_a(void) { M_OUT(REG0, R.A); }
static void movx_xr1_a(void) { M_OUT(REG1, R.A); }
static void nop(void) { }
static void orl_a_n(void)    { R.A |= M_RDMEM_OPCODE(); }
static void orl_a_r0(void)   { R.A |= REG0; }
static void orl_a_r1(void)   { R.A |= REG1; }
static void orl_a_r2(void)   { R.A |= REG2; }
static void orl_a_r3(void)   { R.A |= REG3; }
static void orl_a_r4(void)   { R.A |= REG4; }
static void orl_a_r5(void)   { R.A |= REG5; }
static void orl_a_r6(void)   { R.A |= REG6; }
static void orl_a_r7(void)   { R.A |= REG7; }
static void orl_a_xr0(void)  { R.A |= intRAM[REG0 & 0x7f]; }
static void orl_a_xr1(void)  { R.A |= intRAM[REG1 & 0x7f]; }
static void orl_bus_n(void)  { bus_w( bus_r() | M_RDMEM_OPCODE() ); }
static void orl_p1_n(void)   { port_w(1, port_r(1) | M_RDMEM_OPCODE() ); }
static void orl_p2_n(void)   { port_w(2, port_r(2) | M_RDMEM_OPCODE() ); }
static void orld_p4_a(void)  { port_w(4, port_r(4) | R.A ); }
static void orld_p5_a(void)  { port_w(5, port_r(5) | R.A ); }
static void orld_p6_a(void)  { port_w(6, port_r(6) | R.A ); }
static void orld_p7_a(void)  { port_w(7, port_r(7) | R.A ); }
static void outl_bus_a(void) { bus_w(R.A); }
static void outl_p1_a(void)  { port_w(1, R.A ); }
static void outl_p2_a(void)  { port_w(2, R.A ); }
static void ret(void)        { R.PC.W.w1 = ((pull() & 0x0f) << 8); R.PC.W.w1 |= pull(); /*change_pc(R.PC.W.w1);*/ }
static void retr(void)
{
	byte i=pull();
	R.PC.W.w1 = ((i & 0x0f) << 8) | pull(); /*change_pc(R.PC.W.w1);*/
        R.irq_executing = I8039_IGNORE_INT;
	R.A11 = R.A11ff;
        R.PSW = (R.PSW & 0x0f) | (i & 0xf0);   /* Stack is already changed by pull */
	regPTR = ((M_By) ? 24 : 0);
}
static void rl_a(void)       { byte i=R.A & 0x80; R.A <<= 1; if (i) R.A |= 0x01; else R.A &= 0xfe; }
static void rlc_a(void)      { byte i=M_Cy; if (R.A & 0x80) SET(C_FLAG); R.A <<= 1; if (i) R.A |= 0x01; else R.A &= 0xfe; }
static void rr_a(void)       { byte i=R.A & 1; R.A >>= 1; if (i) R.A |= 0x80; else R.A &= 0x7f; }
static void rrc_a(void)      { byte i=M_Cy; if (R.A & 1) SET(C_FLAG); R.A >>= 1; if (i) R.A |= 0x80; else R.A &= 0x7f; }
static void sel_mb0(void)    { R.A11 = 0; R.A11ff = 0; }
static void sel_mb1(void)    { R.A11ff = 0x800; if (R.irq_executing == I8039_IGNORE_INT) R.A11 = 0x800; }
static void sel_rb0(void)    { CLR(B_FLAG); regPTR = 0;  }
static void sel_rb1(void)    { SET(B_FLAG); regPTR = 24; }
static void stop_tcnt(void)  { R.timerON = R.countON = 0; }
static void strt_cnt(void)   { R.countON = 1; }
static void strt_t(void)     { R.timerON = 1; }
static void swap_a(void)     { byte i=R.A >> 4; R.A <<= 4; R.A |= i; }
static void xch_a_r0(void)   { byte i=R.A; R.A=REG0; REG0=i; }
static void xch_a_r1(void)   { byte i=R.A; R.A=REG1; REG1=i; }
static void xch_a_r2(void)   { byte i=R.A; R.A=REG2; REG2=i; }
static void xch_a_r3(void)   { byte i=R.A; R.A=REG3; REG3=i; }
static void xch_a_r4(void)   { byte i=R.A; R.A=REG4; REG4=i; }
static void xch_a_r5(void)   { byte i=R.A; R.A=REG5; REG5=i; }
static void xch_a_r6(void)   { byte i=R.A; R.A=REG6; REG6=i; }
static void xch_a_r7(void)   { byte i=R.A; R.A=REG7; REG7=i; }
static void xch_a_xr0(void)  { byte i=R.A; R.A=intRAM[REG0 & 0x7F]; intRAM[REG0 & 0x7F]=i; }
static void xch_a_xr1(void)  { byte i=R.A; R.A=intRAM[REG1 & 0x7F]; intRAM[REG1 & 0x7F]=i; }
static void xchd_a_xr0(void) { M_XCHD(REG0 & 0x7f); }
static void xchd_a_xr1(void) { M_XCHD(REG1 & 0x7f); }
static void xrl_a_n(void)    { R.A ^= M_RDMEM_OPCODE(); }
static void xrl_a_r0(void)   { R.A ^= REG0; }
static void xrl_a_r1(void)   { R.A ^= REG1; }
static void xrl_a_r2(void)   { R.A ^= REG2; }
static void xrl_a_r3(void)   { R.A ^= REG3; }
static void xrl_a_r4(void)   { R.A ^= REG4; }
static void xrl_a_r5(void)   { R.A ^= REG5; }
static void xrl_a_r6(void)   { R.A ^= REG6; }
static void xrl_a_r7(void)   { R.A ^= REG7; }
static void xrl_a_xr0(void)  { R.A ^= intRAM[REG0 & 0x7f]; }
static void xrl_a_xr1(void)  { R.A ^= intRAM[REG1 & 0x7f]; }

static unsigned cycles_main[256]=
{
	1, 1, 2, 2, 2, 1, 1, 1, 2, 2, 2, 1, 2, 2, 2, 2,
	1, 1, 2, 2, 2, 1, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1,
	1, 1, 0, 2, 2, 1, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1,
	1, 1, 2, 1, 2, 1, 2, 1, 1, 2, 2, 1, 2, 2, 2, 2,
	1, 1, 1, 2, 2, 1, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1,
	1, 1, 2, 2, 2, 1, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1,
	1, 1, 1, 1, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
	1, 1, 2, 1, 2, 1, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1,
	2, 2, 1, 0, 2, 1, 2, 1, 2, 2, 2, 1, 2, 2, 2, 2,
	2, 2, 2, 2, 2, 1, 2, 1, 2, 2, 2, 1, 2, 2, 2, 2,
	1, 1, 1, 2, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
	2, 2, 2, 2, 2, 1, 2, 1, 2, 2, 2, 2, 2, 2, 2, 2,
	1, 1, 1, 1, 2, 1, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1,
	1, 1, 2, 2, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
	1, 1, 1, 2, 2, 1, 2, 1, 2, 2, 2, 2, 2, 2, 2, 2,
	1, 1, 2, 1, 2, 1, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1
};

static opcode_fn opcode_main[256]=
{
        nop        ,illegal    ,outl_bus_a ,add_a_n   ,jmp       ,en_i      ,illegal   ,dec_a    ,
        ins_a_bus  ,in_a_p1    ,in_a_p2    ,illegal   ,movd_a_p4 ,movd_a_p5 ,movd_a_p6 ,movd_a_p7,
        inc_xr0    ,inc_xr1    ,jb_0       ,adc_a_n   ,call      ,dis_i     ,jtf       ,inc_a    ,
        inc_r0     ,inc_r1     ,inc_r2     ,inc_r3    ,inc_r4    ,inc_r5    ,inc_r6    ,inc_r7   ,
        xch_a_xr0  ,xch_a_xr1  ,illegal    ,mov_a_n   ,jmp_1     ,en_tcnti  ,jnt_0     ,clr_a    ,
        xch_a_r0   ,xch_a_r1   ,xch_a_r2   ,xch_a_r3  ,xch_a_r4  ,xch_a_r5  ,xch_a_r6  ,xch_a_r7 ,
        xchd_a_xr0 ,xchd_a_xr1 ,jb_1       ,illegal   ,call_1    ,dis_tcnti ,jt_0      ,cpl_a    ,
        illegal    ,outl_p1_a  ,outl_p2_a  ,illegal   ,movd_p4_a ,movd_p5_a ,movd_p6_a ,movd_p7_a,
        orl_a_xr0  ,orl_a_xr1  ,mov_a_t    ,orl_a_n   ,jmp_2     ,strt_cnt  ,jnt_1     ,swap_a   ,
        orl_a_r0   ,orl_a_r1   ,orl_a_r2   ,orl_a_r3  ,orl_a_r4  ,orl_a_r5  ,orl_a_r6  ,orl_a_r7 ,
        anl_a_xr0  ,anl_a_xr1  ,jb_2       ,anl_a_n   ,call_2    ,strt_t    ,jt_1      ,daa_a    ,
        anl_a_r0   ,anl_a_r1   ,anl_a_r2   ,anl_a_r3  ,anl_a_r4  ,anl_a_r5  ,anl_a_r6  ,anl_a_r7 ,
        add_a_xr0  ,add_a_xr1  ,mov_t_a    ,illegal   ,jmp_3     ,stop_tcnt ,illegal   ,rrc_a    ,
        add_a_r0   ,add_a_r1   ,add_a_r2   ,add_a_r3  ,add_a_r4  ,add_a_r5  ,add_a_r6  ,add_a_r7 ,
        adc_a_xr0  ,adc_a_xr1  ,jb_3       ,illegal   ,call_3    ,ento_clk  ,jf_1      ,rr_a     ,
        adc_a_r0   ,adc_a_r1   ,adc_a_r2   ,adc_a_r3  ,adc_a_r4  ,adc_a_r5  ,adc_a_r6  ,adc_a_r7 ,
        movx_a_xr0 ,movx_a_xr1 ,illegal    ,ret       ,jmp_4     ,clr_f0    ,jni       ,illegal  ,
        orl_bus_n  ,orl_p1_n   ,orl_p2_n   ,illegal   ,orld_p4_a ,orld_p5_a ,orld_p6_a ,orld_p7_a,
        movx_xr0_a ,movx_xr1_a ,jb_4       ,retr      ,call_4    ,cpl_f0    ,jnz       ,clr_c    ,
        anl_bus_n  ,anl_p1_n   ,anl_p2_n   ,illegal   ,anld_p4_a ,anld_p5_a ,anld_p6_a ,anld_p7_a,
        mov_xr0_a  ,mov_xr1_a  ,illegal    ,movp_a_xa ,jmp_5     ,clr_f1    ,illegal   ,cpl_c    ,
        mov_r0_a   ,mov_r1_a   ,mov_r2_a   ,mov_r3_a  ,mov_r4_a  ,mov_r5_a  ,mov_r6_a  ,mov_r7_a ,
        mov_xr0_n  ,mov_xr1_n  ,jb_5       ,jmpp_xa   ,call_5    ,cpl_f1    ,jf0       ,illegal  ,
        mov_r0_n   ,mov_r1_n   ,mov_r2_n   ,mov_r3_n  ,mov_r4_n  ,mov_r5_n  ,mov_r6_n  ,mov_r7_n ,
        illegal    ,illegal    ,illegal    ,illegal   ,jmp_6     ,sel_rb0   ,jz        ,mov_a_psw,
        dec_r0     ,dec_r1     ,dec_r2     ,dec_r3    ,dec_r4    ,dec_r5    ,dec_r6    ,dec_r7   ,
        xrl_a_xr0  ,xrl_a_xr1  ,jb_6       ,xrl_a_n   ,call_6    ,sel_rb1   ,illegal   ,mov_psw_a,
        xrl_a_r0   ,xrl_a_r1   ,xrl_a_r2   ,xrl_a_r3  ,xrl_a_r4  ,xrl_a_r5  ,xrl_a_r6  ,xrl_a_r7 ,
        illegal    ,illegal    ,illegal    ,movp3_a_xa,jmp_7     ,sel_mb0   ,jnc       ,rl_a     ,
        djnz_r0    ,djnz_r1    ,djnz_r2    ,djnz_r3   ,djnz_r4   ,djnz_r5   ,djnz_r6   ,djnz_r7  ,
        mov_a_xr0  ,mov_a_xr1  ,jb_7       ,illegal   ,call_7    ,sel_mb1   ,jc        ,rlc_a    ,
        mov_a_r0   ,mov_a_r1   ,mov_a_r2   ,mov_a_r3  ,mov_a_r4  ,mov_a_r5  ,mov_a_r6  ,mov_a_r7
};





/****************************************************************************/
/* Set all registers to given values                                        */
/****************************************************************************/
void I8039_SetRegs (I8039_Regs *Regs)
{
	R=*Regs;
	regPTR = ((M_By) ? 24 : 0);
	R.SP = (R.PSW << 1) & 0x0f;
	/*change_pc(R.PC.W.w1);*/
}

/****************************************************************************/
/* Get all registers in given buffer                                        */
/****************************************************************************/
void I8039_GetRegs (I8039_Regs *Regs)
{
	*Regs=R;
}


/****************************************************************************/
/* Reset registers to their initial values                                  */
/****************************************************************************/
void I8039_Reset (void)
{
	R.PC.W.w1  = 0;
	R.SP  = 0;
	R.A   = 0;
	R.PSW = 0x08;		/* Start with Carry SET, Bit 4 is always SET */
	memset(R.RAM, 0x0, 128);
	R.bus = 0;
        R.irq_executing = I8039_IGNORE_INT;
        R.pending_irq   = I8039_IGNORE_INT;

	R.A11ff   = R.A11     = 0;
	R.timerON = R.countON = 0;
	R.tirq_en = R.xirq_en = 0;
	R.xirq_en=1;
	R.timerON = 1;	/* Mario Bros. doesn't work without this */
	R.masterClock = 0;
}


/****************************************************************************/
/* Return program counter                                                   */
/****************************************************************************/
unsigned I8039_GetPC (void)
{
	return R.PC.W.w1;
}


/****************************************************************************/
/* Issue an interrupt if necessary                                          */
/****************************************************************************/
static int Timer_IRQ(void)
{
	if (R.tirq_en && !R.irq_executing)
	{
                R.irq_executing = I8039_TIMER_INT;
		push(R.PC.B.l);
		push((R.PC.B.h & 0x0f) | (R.PSW & 0xf0));
		R.PC.W.w1 = 0x07;
		/*change_pc(0x07);*/
		R.A11ff = R.A11;
		R.A11   = 0;
		return 2;		/* 2 clock cycles used */
	}
	return 0;
}

static int Ext_IRQ(void)
{
	if (R.xirq_en)
	{
		R.irq_executing = I8039_EXT_INT;
		push(R.PC.B.l);
		push((R.PC.B.h & 0x0f) | (R.PSW & 0xf0));
		R.PC.W.w1 = 0x03;
		R.A11ff = R.A11;
		R.A11   = 0;
		return 2;		/* 2 clock cycles used */
	}
	return 0;
}




/****************************************************************************/
/* Execute IPeriod T-States. Return 0 if emulation should be stopped        */
/****************************************************************************/
int I8039_Execute(int cycles)
{
   unsigned opcode, T1;

   I8039_ICount=cycles;

   do
   {
	switch (R.pending_irq)
	{
		case I8039_COUNT_INT:
		case I8039_TIMER_INT:   I8039_ICount -= Timer_IRQ();
                                        R.t_flag = 1;
					break;
		case I8039_EXT_INT:	I8039_ICount -= Ext_IRQ();
					break;
	}
        R.pending_irq = I8039_IGNORE_INT;

	opcode=M_RDOP(R.PC.W.w1);


	{	/* NS 971024 */
		extern int previouspc;
		previouspc = R.PC.W.w1;
	}

	R.PC.W.w1++;
	I8039_ICount-=cycles_main[opcode];
	(*(opcode_main[opcode]))();

        T1 = test_r(1);
	if (R.countON && POSITIVE_EDGE_T1)	/* Handle COUNTER IRQs */
	{
		R.timer++;
		if (R.timer == 0) I8039_Cause_Interrupt(I8039_COUNT_INT);
	}

	if (R.timerON)	                        /* Handle TIMER IRQs */
	{
		R.masterClock += cycles_main[opcode];
		if (R.masterClock > 31)
		{
			R.masterClock -= 31;
			R.timer++;
                        if (R.timer == 0) I8039_Cause_Interrupt(I8039_TIMER_INT);
		}
	}

        Old_T1 = T1;
   }
   while (I8039_ICount>0);

   return cycles - I8039_ICount;
}


void I8039_Cause_Interrupt(int type)
{
	if (type != I8039_IGNORE_INT)
		R.pending_irq = type;
}

void I8039_Clear_Pending_Interrupts(void)
{
	R.pending_irq = I8039_IGNORE_INT;
}
