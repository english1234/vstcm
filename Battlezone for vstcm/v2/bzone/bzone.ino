/*
   Atari Vector game simulator

   Copyright 1991, 1993, 1996 Hedley Rainnie, Doug Neubauer, and Eric Smith
   Copyright 2015 Hedley Rainnie

   6502 simulator by Hedley Rainnie, Doug Neubauer, and Eric Smith

   Adapted for vstcm by Robin Champion June 2022
   https://github.com/english1234/vstcm

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

*/

#include <stdio.h>
#include <SD.h>
#include <IRremote.hpp>
#include <sys/types.h>
#include "bzone.h"

void dopush(uint8_t val, uint16_t PC)
{
  uint16_t addr;
  addr = g_cpu_SP + 0x100;
  g_cpu_SP--;
  g_cpu_SP &= 0xff;
  memwr (addr, val, PC, 0);
}

uint8_t dopop(uint16_t PC)
{
  uint16_t addr;
  g_cpu_SP++;
  g_cpu_SP &= 0xff;
  addr = g_cpu_SP + 0x100;
  return (memrd(addr, PC, 0));
}

void loop()
{
  int32_t PC;
  int32_t opcode;
  int32_t addr;
  int32_t A;
  int32_t X;
  int32_t Y;
  DECLARE_CC;
  uint32_t totcycles;

  A = g_cpu_save_A;
  X = g_cpu_save_X;
  Y = g_cpu_save_Y;
  byte_to_flags (g_cpu_save_flags);
  PC = g_cpu_save_PC;
  totcycles = g_cpu_save_totcycles;

test_interrupt:

#ifdef WRAP_CYC_COUNT   // I've commented out this definition for the moment just to see what happens (RC)
  if (totcycles > WRAP_CYC_COUNT)
  {
    if (g_cpu_irq_cycle >= (totcycles - WRAP_CYC_COUNT))
      g_cpu_irq_cycle -= WRAP_CYC_COUNT;

    if (g_vctr_vg_done_cyc >= (totcycles - WRAP_CYC_COUNT))
      g_vctr_vg_done_cyc -= WRAP_CYC_COUNT;

    totcycles -= WRAP_CYC_COUNT;
    g_cpu_cyc_wraps++;
  }
#endif

  if (totcycles > g_cpu_irq_cycle)
  {
    if (!g_sys_self_test)
    {
      /* do NMI */
      dopush(PC >> 8, PC);
      dopush(PC & 0xff, PC);
      dopush(flags_to_byte, PC);
      SET_I;
      PC = memrdwd(0xfffa, PC, totcycles);
      totcycles += 7;
      //          g_cpu_irq_cycle += 6144; // <<<<<< hkjr 03/30/14. NMI in an MMI occasionally with a # like this...
      g_cpu_irq_cycle += g_cpu_irq_cycle_off;
    }
  }

  while (1) {
#if 0
    pc_ring[ring_idx++] = PC;
    ring_idx &= 0x3f;
#endif
#ifdef TRACEFIFO
    fifo[pcpos].PC = PC;
    fifo[pcpos].A = A;
    fifo[pcpos].X = X;
    fifo[pcpos].Y = Y;
    fifo[pcpos].flags = flags_to_byte;
    fifo[pcpos].SP = g_cpu_SP;
    pcpos = (pcpos + 1) & 0x1fff;
#endif
    if (PC == 0x7985)
      start_sample(A);       // pokey audio
    else if (PC == 0x6a22)
      enable_sound(SMART);    // smart missile (pokey ch3&4)

    opcode = g_sys_mem[PC++].cell;

    switch (opcode)
    { // execute opcode
      case 0x69:  /* ADC */  EA_IMM    DO_ADC   C( 2)  break;
      case 0x6d:  /* ADC */  EA_ABS    DO_ADC   C( 3)  break;
      case 0x65:  /* ADC */  EA_ZP     DO_ADC   C( 4)  break;
      case 0x61:  /* ADC */  EA_IND_X  DO_ADC   C( 6)  break;
      case 0x71:  /* ADC */  EA_IND_Y  DO_ADC   C( 5)  break;
      case 0x75:  /* ADC */  EA_ZP_X   DO_ADC   C( 4)  break;
      case 0x7d:  /* ADC */  EA_ABS_X  DO_ADC   C( 4)  break;
      case 0x79:  /* ADC */  EA_ABS_Y  DO_ADC   C( 4)  break;

      case 0x29:  /* AND */  EA_IMM    DO_AND   C( 2)  break;
      case 0x2d:  /* AND */  EA_ABS    DO_AND   C( 4)  break;
      case 0x25:  /* AND */  EA_ZP     DO_AND   C( 3)  break;
      case 0x21:  /* AND */  EA_IND_X  DO_AND   C( 6)  break;
      case 0x31:  /* AND */  EA_IND_Y  DO_AND   C( 5)  break;
      case 0x35:  /* AND */  EA_ZP_X   DO_AND   C( 4)  break;
      case 0x39:  /* AND */  EA_ABS_Y  DO_AND   C( 4)  break;
      case 0x3d:  /* AND */  EA_ABS_X  DO_AND   C( 4)  break;

      case 0x0e:  /* ASL */  EA_ABS    DO_ASL   C( 6)  break;
      case 0x06:  /* ASL */  EA_ZP     DO_ASL   C( 5)  break;
      case 0x0a:  /* ASL */            DO_ASLA  C( 2)  break;
      case 0x16:  /* ASL */  EA_ZP_X   DO_ASL   C( 6)  break;
      case 0x1e:  /* ASL */  EA_ABS_X  DO_ASL   C( 7)  break;

      case 0x90:  /* BCC */		 DO_BCC   C( 2)  goto test_interrupt;
      case 0xb0:  /* BCS */		 DO_BCS   C( 2)  goto test_interrupt;
      case 0xf0:  /* BEQ */		 DO_BEQ   C( 2)  goto test_interrupt;
      case 0x30:  /* BMI */		 DO_BMI   C( 2)  goto test_interrupt;
      case 0xd0:  /* BNE */		 DO_BNE   C( 2)  goto test_interrupt;
      case 0x10:  /* BPL */		 DO_BPL   C( 2)  goto test_interrupt;
      case 0x50:  /* BVC */		 DO_BVC   C( 2)  goto test_interrupt;
      case 0x70:  /* BVS */		 DO_BVS   C( 2)  goto test_interrupt;

      case 0x2c:  /* BIT */  EA_ABS    DO_BIT   C( 4)  break;
      case 0x24:  /* BIT */  EA_ZP     DO_BIT   C( 3)  break;

#if 1
      case 0x00:  /* BRK */            DO_BRK   C( 7)  break;
#endif

      case 0x18:  /* CLC */            DO_CLC   C( 2)  break;
      case 0xd8:  /* CLD */            DO_CLD   C( 2)  break;
      case 0x58:  /* CLI */            DO_CLI   C( 2)  goto test_interrupt;
      case 0xb8:  /* CLV */            DO_CLV   C( 2)  break;

      case 0xc9:  /* CMP */  EA_IMM    DO_CMP   C( 2)  break;
      case 0xcd:  /* CMP */  EA_ABS    DO_CMP   C( 4)  break;
      case 0xc5:  /* CMP */  EA_ZP     DO_CMP   C( 3)  break;
      case 0xc1:  /* CMP */  EA_IND_X  DO_CMP   C( 6)  break;
      case 0xd1:  /* CMP */  EA_IND_Y  DO_CMP   C( 5)  break;
      case 0xd5:  /* CMP */  EA_ZP_X   DO_CMP   C( 4)  break;
      case 0xd9:  /* CMP */  EA_ABS_Y  DO_CMP   C( 4)  break;
      case 0xdd:  /* CMP */  EA_ABS_X  DO_CMP   C( 4)  break;

      case 0xe0:  /* CPX */  EA_IMM    DO_CPX   C( 2)  break;
      case 0xec:  /* CPX */  EA_ABS    DO_CPX   C( 4)  break;
      case 0xe4:  /* CPX */  EA_ZP     DO_CPX   C( 3)  break;

      case 0xc0:  /* CPY */  EA_IMM    DO_CPY   C( 2)  break;
      case 0xcc:  /* CPY */  EA_ABS    DO_CPY   C( 4)  break;
      case 0xc4:  /* CPY */  EA_ZP     DO_CPY   C( 3)  break;

      case 0xce:  /* DEC */  EA_ABS    DO_DEC   C( 6)  break;
      case 0xc6:  /* DEC */  EA_ZP     DO_DEC   C( 5)  break;
      case 0xd6:  /* DEC */  EA_ZP_X   DO_DEC   C( 6)  break;
      case 0xde:  /* DEC */  EA_ABS_X  DO_DEC   C( 7)  break;

      case 0xca:  /* DEX */            DO_DEX   C( 2)  break;
      case 0x88:  /* DEY */            DO_DEY   C( 2)  break;

      case 0x49:  /* EOR */  EA_IMM    DO_EOR   C( 2)  break;
      case 0x4d:  /* EOR */  EA_ABS    DO_EOR   C( 4)  break;
      case 0x45:  /* EOR */  EA_ZP     DO_EOR   C( 3)  break;
      case 0x41:  /* EOR */  EA_IND_X  DO_EOR   C( 6)  break;
      case 0x51:  /* EOR */  EA_IND_Y  DO_EOR   C( 5)  break;
      case 0x55:  /* EOR */  EA_ZP_X   DO_EOR   C( 4)  break;
      case 0x59:  /* EOR */  EA_ABS_Y  DO_EOR   C( 4)  break;
      case 0x5d:  /* EOR */  EA_ABS_X  DO_EOR   C( 4)  break;

      case 0xee:  /* INC */  EA_ABS    DO_INC   C( 6)  break;
      case 0xe6:  /* INC */  EA_ZP     DO_INC   C( 5)  break;
      case 0xf6:  /* INC */  EA_ZP_X   DO_INC   C( 6)  break;
      case 0xfe:  /* INC */  EA_ABS_X  DO_INC   C( 7)  break;

      case 0xe8:  /* INX */            DO_INX   C( 2)  break;
      case 0xc8:  /* INY */            DO_INY   C( 2)  break;

      case 0x4c:  /* JMP */  EA_ABS    DO_JMP   C( 3)  goto test_interrupt;
      case 0x6c:  /* JMP */  EA_IND    DO_JMP   C( 5)  goto test_interrupt;

      case 0x20:  /* JSR */  EA_ABS    DO_JSR   C( 6)  goto test_interrupt;

      case 0xa9:  /* LDA */  EA_IMM    DO_LDA   C( 2)  break;
      case 0xad:  /* LDA */  EA_ABS    DO_LDA   C( 4)  break;
      case 0xa5:  /* LDA */  EA_ZP     DO_LDA   C( 3)  break;
      case 0xa1:  /* LDA */  EA_IND_X  DO_LDA   C( 6)  break;
      case 0xb1:  /* LDA */  EA_IND_Y  DO_LDA   C( 5)  break;
      case 0xb5:  /* LDA */  EA_ZP_X   DO_LDA   C( 4)  break;
      case 0xb9:  /* LDA */  EA_ABS_Y  DO_LDA   C( 4)  break;
      case 0xbd:  /* LDA */  EA_ABS_X  DO_LDA   C( 4)  break;

      case 0xa2:  /* LDX */  EA_IMM    DO_LDX   C( 2)  break;
      case 0xae:  /* LDX */  EA_ABS    DO_LDX   C( 4)  break;
      case 0xa6:  /* LDX */  EA_ZP     DO_LDX   C( 3)  break;
      case 0xbe:  /* LDX */  EA_ABS_Y  DO_LDX   C( 4)  break;
      case 0xb6:  /* LDX */  EA_ZP_Y   DO_LDX   C( 4)  break;

      case 0xa0:  /* LDY */  EA_IMM    DO_LDY   C( 2)  break;
      case 0xac:  /* LDY */  EA_ABS    DO_LDY   C( 4)  break;
      case 0xa4:  /* LDY */  EA_ZP     DO_LDY   C( 3)  break;
      case 0xb4:  /* LDY */  EA_ZP_X   DO_LDY   C( 4)  break;
      case 0xbc:  /* LDY */  EA_ABS_X  DO_LDY   C( 4)  break;

      case 0x4e:  /* LSR */  EA_ABS    DO_LSR   C( 6)  break;
      case 0x46:  /* LSR */  EA_ZP     DO_LSR   C( 5)  break;
      case 0x4a:  /* LSR */            DO_LSRA  C( 2)  break;
      case 0x56:  /* LSR */  EA_ZP_X   DO_LSR   C( 6)  break;
      case 0x5e:  /* LSR */  EA_ABS_X  DO_LSR   C( 7)  break;

      case 0xea:  /* NOP */                     C( 2)  break;

      case 0x09:  /* ORA */  EA_IMM    DO_ORA   C( 2)  break;
      case 0x0d:  /* ORA */  EA_ABS    DO_ORA   C( 4)  break;
      case 0x05:  /* ORA */  EA_ZP     DO_ORA   C( 3)  break;
      case 0x01:  /* ORA */  EA_IND_X  DO_ORA   C( 6)  break;
      case 0x11:  /* ORA */  EA_IND_Y  DO_ORA   C( 5)  break;
      case 0x15:  /* ORA */  EA_ZP_X   DO_ORA   C( 4)  break;
      case 0x19:  /* ORA */  EA_ABS_Y  DO_ORA   C( 4)  break;
      case 0x1d:  /* ORA */  EA_ABS_X  DO_ORA   C( 4)  break;

      case 0x48:  /* PHA */            DO_PHA   C( 3)  break;
      case 0x08:  /* PHP */            DO_PHP   C( 3)  break;
      case 0x68:  /* PLA */            DO_PLA   C( 4)  break;
      case 0x28:  /* PLP */            DO_PLP   C( 4)  goto test_interrupt;

      case 0x2e:  /* ROL */  EA_ABS    DO_ROL   C( 6)  break;
      case 0x26:  /* ROL */  EA_ZP     DO_ROL   C( 5)  break;
      case 0x2a:  /* ROL */            DO_ROLA  C( 2)  break;
      case 0x36:  /* ROL */  EA_ZP_X   DO_ROL   C( 6)  break;
      case 0x3e:  /* ROL */  EA_ABS_X  DO_ROL   C( 7)  break;

      case 0x6e:  /* ROR */  EA_ABS    DO_ROR   C( 6)  break;
      case 0x66:  /* ROR */  EA_ZP     DO_ROR   C( 5)  break;
      case 0x6a:  /* ROR */            DO_RORA  C( 2)  break;
      case 0x76:  /* ROR */  EA_ZP_X   DO_ROR   C( 6)  break;
      case 0x7e:  /* ROR */  EA_ABS_X  DO_ROR   C( 7)  break;

      case 0x40:  /* RTI */            DO_RTI   C( 6)  goto test_interrupt;
      case 0x60:  /* RTS */            DO_RTS   C( 6)  goto test_interrupt;

      case 0xe9:  /* SBC */  EA_IMM    DO_SBC   C( 2)  break;
      case 0xed:  /* SBC */  EA_ABS    DO_SBC   C( 4)  break;
      case 0xe5:  /* SBC */  EA_ZP     DO_SBC   C( 3)  break;
      case 0xe1:  /* SBC */  EA_IND_X  DO_SBC   C( 6)  break;
      case 0xf1:  /* SBC */  EA_IND_Y  DO_SBC   C( 5)  break;
      case 0xf5:  /* SBC */  EA_ZP_X   DO_SBC   C( 4)  break;
      case 0xf9:  /* SBC */  EA_ABS_Y  DO_SBC   C( 4)  break;
      case 0xfd:  /* SBC */  EA_ABS_X  DO_SBC   C( 4)  break;

      case 0x38:  /* SEC */            DO_SEC   C( 2)  break;
      case 0xf8:  /* SED */            DO_SED   C( 2)  break;
      case 0x78:  /* SEI */            DO_SEI   C( 2)  break;

      case 0x8d:  /* STA */  EA_ABS    DO_STA   C( 4)  break;
      case 0x85:  /* STA */  EA_ZP     DO_STA   C( 3)  break;
      case 0x81:  /* STA */  EA_IND_X  DO_STA   C( 6)  break;
      case 0x91:  /* STA */  EA_IND_Y  DO_STA   C( 6)  break;
      case 0x95:  /* STA */  EA_ZP_X   DO_STA   C( 4)  break;
      case 0x99:  /* STA */  EA_ABS_Y  DO_STA   C( 5)  break;
      case 0x9d:  /* STA */  EA_ABS_X  DO_STA   C( 5)  break;

      case 0x8e:  /* STX */  EA_ABS    DO_STX   C( 4)  break;
      case 0x86:  /* STX */  EA_ZP     DO_STX   C( 3)  break;
      case 0x96:  /* STX */  EA_ZP_Y   DO_STX   C( 4)  break;

      case 0x8c:  /* STY */  EA_ABS    DO_STY   C( 4)  break;
      case 0x84:  /* STY */  EA_ZP     DO_STY   C( 3)  break;
      case 0x94:  /* STY */  EA_ZP_X   DO_STY   C( 4)  break;

      case 0xaa:  /* TAX */            DO_TAX   C( 2)  break;
      case 0xa8:  /* TAY */            DO_TAY   C( 2)  break;
      case 0x98:  /* TYA */            DO_TYA   C( 2)  break;
      case 0xba:  /* TSX */            DO_TSX   C( 2)  break;
      case 0x8a:  /* TXA */            DO_TXA   C( 2)  break;
      case 0x9a:  /* TXS */            DO_TXS   C( 2)  break;

      default:
        break;
    }
  }
}

// memory and I/O functions for Atari Vector game simulator

/*
   This used to decrement the switch variable if it was non-zero, so that
   they would automatically release.  This has been changed to increment
   it if less than zero, so switches set by the debugger will release, but
   to leave it alone if it is greater than zero, for keyboard handling.
*/
int32_t check_switch_decr(int32_t *sw)
{
  if ((*sw) < 0)
  {
    (*sw)++;
    if ((*sw) == 0)
      ;
  }

  return ((*sw) != 0);
}

uint8_t MEMRD(uint32_t addr, int32_t PC, uint32_t cyc)
{
  uint8_t tag;
  uint8_t result = 0;

  if (!(tag = g_sys_mem[addr].tagr))
    return (g_sys_mem[addr].cell);

  switch (tag & 0x3f)
  {
    case MEMORY:
      result = g_sys_mem[addr].cell;
      break;
    case MEMORY1:
      result = g_sys_sram[addr & 0x3ff];
      break;
    case MEMORY_BB:
      break;
    case VECRAM:
      result = g_sys_vram[addr & 0xfff];
      break;
    case COININ:
      result =
        ((! check_switch_decr(&g_sys_cslot_right))) |
        ((! check_switch_decr(&g_sys_cslot_left)) << 1) |
        ((! check_switch_decr(&g_sys_cslot_util)) << 2) |
        ((! check_switch_decr(&g_sys_slam)) << 3) |
        ((! g_sys_self_test) << 4) |
        (1 << 5) | // signature analysis
        (vg_done(cyc) << 6) |
        // clock toggles at 3 KHz
        ((cyc >> 1) & 0x80);

      break;
    case EAROMRD:
      result = 0;
      break;
    case OPTSW1:
      result = g_sys_optionreg[0];
      break;
    case OPTSW2:
      result = g_sys_optionreg[1];
      break;
    case OPT1_2BIT:
      result = 0xfc | ((g_sys_optionreg[0] >> (2 * (3 - (addr & 0x3)))) & 0x3);
      break;
    case ASTEROIDS_SW1:
      break;
    case ASTEROIDS_SW2:
      break;
    case POKEY1:
      result = pokey_read(0, addr & 0x0f, PC, cyc);
      break;
    case POKEY2:
      result = pokey_read (1, addr & 0x0f, PC, cyc);
      break;
    case POKEY3:
      result = pokey_read (2, addr & 0x0f, PC, cyc);
      break;
    case POKEY4:
      result = pokey_read (3, addr & 0x0f, PC, cyc);
      break;
    case BZ_SOUND:
      break;
    case BZ_INPUTS:
      result = g_soc_curr_switch = read_gpio();
      break;
    case MBLO:
      result = mb_result & 0xff;
      break;
    case MBHI:
      result = (mb_result >> 8) & 0xff;
      break;
    case MBSTAT:
      result = 0x00;  /* always done! */
      break;
    case UNKNOWN:
      result = 0xff;
      break;
    default:
      result = 0xff;
      break;
  }

  if (tag & BREAKTAG)
    ;

  return result;
}

void MEMWR(uint32_t addr, int32_t val, int32_t PC, uint32_t cyc)
{
  uint8_t tag;
  int32_t newbank;

  if (!(tag = g_sys_mem[addr].tagw))
    g_sys_mem[addr].cell = val;
  else
  {
    switch (tag & 0x3f)
    {
      case MEMORY:
        g_sys_mem[addr].cell = val;
        break;
      case MEMORY1:
        g_sys_sram[addr & 0x3ff] = val;
        break;
      case MEMORY_BB:
        break;
      case VECRAM:
        g_sys_vram[addr & 0xfff] = val;
        break;
      case COINOUT:
        newbank = (val >> 2) & 1;
        g_sys_bank = newbank;
        break;
      case INTACK:
        g_cpu_irq_cycle = cyc + 6144;
        break;
      case WDCLR:
      case EAROMCON:
      case EAROMWR:
        /* none of these are implemented yet, but they're OK. */
        break;
      case VGRST:
        vg_reset(cyc);
        break;
      case VGO:
        g_vctr_vg_count++;
        // while (0 == g_soc_sixty_hz);    //requires IRQ to work
        g_soc_sixty_hz = 0;
        vg_go(cyc);
        break;
      case DMACNT:
        break;
      case COLORRAM:
        break;
      case TEMP_OUTPUTS:
        break;
      case ASTEROIDS_OUT:
        break;
      case ASTEROIDS_EXP:
        break;
      case ASTEROIDS_THUMP:
        break;
      case ASTEROIDS_SND:
        break;
      case ASTEROIDS_SND_RST:
        break;
      case POKEY1:
        pokey_write(0, addr & 0x0f, val, PC, cyc);
        break;
      case POKEY2:
        pokey_write(1, addr & 0x0f, val, PC, cyc);
        break;
      case POKEY3:
        pokey_write(2, addr & 0x0f, val, PC, cyc);
        break;
      case POKEY4:
        pokey_write(3, addr & 0x0f, val, PC, cyc);
        break;
      case BZ_SOUND:
        /*
          BZ_SOUNDS[7]  motoren
          BZ_SOUNDS[6]  start led
          BZ_SOUNDS[5]  sound en
          BZ_SOUNDS[4]  engine H/L
          BZ_SOUNDS[3]  shell L/S
          BZ_SOUNDS[2]  shell enabl
          BZ_SOUNDS[1]  explo L/S
          BZ_SOUNDS[0]  explo en
        */
        if (val & bit(5))
          g_aud_enable = 1;
        else
          g_aud_enable = 0;

        if (val & bit(0)) // expl
          enable_sound((val & bit(1)) ? EXPLODE_HI : EXPLODE_LO);

        if (val & bit(2))  // shell
          enable_sound((val & bit(3)) ? SHELL_HI : SHELL_LO);

        if (val & bit(7))
        { // motor
          disable_sound(MOTOR_HI);
          disable_sound(MOTOR_LO);
          enable_sound((val & bit(4)) ? MOTOR_HI : MOTOR_LO);
        }

        // execute a function here to flash LEDs
        break;
      case MBSTART:
        /* printf("@%04x MBSTART wr addr %04x val %02x\n", PC, addr & 0x1f, val); */
        mb_go(addr & 0x1f, val);
        break;
      case IGNWRT:
        break;
      case ROMWRT:
        break;
      case UNKNOWN:
        break;
      default:
        break;
    }
  }
}

// Bzone audio support

const int16_t explode_lo[] = {
  //#include "expLo.hex"
};
const int16_t explode_hi[] = {
  //#include "expHi.hex"
};
const int16_t shell_lo[] = {
  //#include "shellLo.hex"
};
const int16_t shell_hi[] = {
  //#include "shellHi.hex"
};
const int16_t motor_lo[] = {
  //#include "motorLo.hex"
};
const int16_t motor_hi[] = {
  //#include "motorHi.hex"
};
// Pokey sounds:
// {1 radar,  2 bump,  4 blocked,  8 extra life,  0x10 enemy appears,  0x20 saucer hit,  0x40 short saucer sound,  0x80 high score melody}
const int16_t radar[] = {
  //#include "radar.hex"
};
const int16_t bump[] = {
  //#include "bump.hex"
};
const int16_t blocked[] = {
  //#include "blocked.hex"
};
const int16_t life[] = {
  //#include "life.hex"
};
const int16_t enemy[] = {
  //#include "start.hex"
};
const int16_t saucer_hit[] = {
  //#include "saucerhit.hex"
};
const int16_t saucer[] = {
  //#include "saucer.hex"
};
const int16_t high_score[] = {
  //#include "hiscore.hex"
};
const int16_t smart[] = {
  //#include "smart.hex"
};

// Add this to the mix

typedef struct _sound_rec {
  const int16_t *ptr;
  uint32_t len; // Len of the sample after processing
  uint32_t idx; // Curr index into the sample.
  uint32_t oneshot; // When 1 don't repeat
} sound_rec;

sound_rec sounds[] = {
  { 0, 0, 0, 0 },
  { explode_lo, 0, 0, 1},
  { explode_hi, 0, 0, 1 },
  { shell_lo, 0, 0, 1 },
  { shell_hi, 0, 0, 1 },
  { motor_lo, 0, 0, 0 },
  { motor_hi, 0, 0, 0 },
  { smart, 0, 0, 1},
  { radar, 0, 0, 1 },
  { bump, 0, 0, 1},
  { blocked, 0, 0, 1},
  { life, 0, 0, 1},
  { enemy, 0, 0, 1},
  { saucer_hit, 0, 0, 1},
  { saucer, 0, 0, 1},
  { high_score, 0, 0, 1}
};

void add_sounds()
{
  uint i;
  uint32_t len;
  for (i = 0; i < sizeof(sounds) / sizeof(sound_rec); i++) {
    uint32_t p = (uint32_t)sounds[i].ptr;
    if (p) {
      len = REG32(p + 0x28);
      len /= 2; // Len in 16bit samples
      len += AUDACITY_WAV_HDR_OFF;
      sounds[i].len = len;
      sounds[i].idx = AUDACITY_WAV_HDR_OFF;
    }
  }
}

void enable_sound(uint32_t mask)
{
  g_aud_smask |= mask;
}

void disable_sound(uint32_t mask)
{
  g_aud_smask &= ~mask;
}

void start_sample(uint32_t mask)
{
  // {1 radar,  2 bump,  4 blocked,  8 extra life,  0x10 enemy appears,  0x20 saucer hit,  0x40 short saucer sound,  0x80 high score melody}
  if (mask)
    g_aud_smask |= (mask << 8);
}

int16_t get_sample()
{
  uint32_t i;
  int16_t worklist[16];
  uint16_t idx = 0;
  int16_t mixer(int16_t *dat, uint16_t factor, uint32_t n);

  if (0 == g_aud_enable)
    return 0;

  for (i = 1; i < sizeof(sounds) / sizeof(sound_rec); i++) {
    if (g_aud_smask & bit(i)) {
      worklist[idx++] = sounds[i].ptr[sounds[i].idx];
      sounds[i].idx++;
      if (sounds[i].idx == sounds[i].len) {
        sounds[i].idx = AUDACITY_WAV_HDR_OFF;
        if (sounds[i].oneshot) {
          g_aud_smask &= ~bit(i);
        }
      }
    }
  }

  if (idx == 1)
    return worklist[0];
  else
    return mixer(worklist, 32768 / idx, idx << 1);
}

void init_dac()
{
  // Apparently, pin 10 has to be defined as an OUTPUT pin to designate the Arduino as the SPI master.
  // Even if pin 10 is not being used... Is this true for Teensy 4.1?
  // The default mode is INPUT. You must explicitly set pin 10 to OUTPUT (and leave it as OUTPUT).
  pinMode(10, OUTPUT);
  digitalWriteFast(10, HIGH);
  delayNanoseconds(100);

  // Set chip select pins to output
  pinMode(SS0_IC5_RED, OUTPUT);
  digitalWriteFast(SS0_IC5_RED, HIGH);
  delayNanoseconds(100);
  pinMode(SS1_IC4_X_Y, OUTPUT);
  digitalWriteFast(SS1_IC4_X_Y, HIGH);
  delayNanoseconds(100);
  pinMode(SS2_IC3_GRE_BLU, OUTPUT);
  digitalWriteFast(SS2_IC3_GRE_BLU, HIGH);
  delayNanoseconds(100);

  pinMode(SDI, OUTPUT);       // Set up clock and data output to DACs
  pinMode(SCK, OUTPUT);

  delay(1);         // https://www.pjrc.com/better-spi-bus-design-in-3-steps/

  SPI.begin();

  // Setup the SPI DMA callback
  callbackHandler.attachImmediate(&callback);
  callbackHandler.clearEvent();
}

void callback(EventResponderRef eventResponder)
{
  // End SPI DMA write to DAC
  //  SPI.endTransaction();
  digitalWriteFast(activepin, HIGH);
  activepin = 0;
}

void dac_out(uint16_t ch1, uint16_t ch2, int z)
{
  static uint16_t old_ch1; // previously used values of x, y & z to prevent needless redrawing (RC June 2022)
  static uint16_t old_ch2;
  static int old_z;
  int new_z;

  // values of z are 0, 3, 4, 6, 7, 8, 10, 12, 14, 60
  // scale z to 8 bits

  // attempt to scale z to something useable
  if (z != old_z)
  {
    if (z == 60)
      new_z = 15 * 10 + 5;
    else
      new_z = z * 15 + 5;

    if (ch2 < 1024)   // if Y coordinate is high enough, swap to RED, otherwise GREEN
    {
      MCP4922_write(SS2_IC3_GRE_BLU, DAC_CHAN_A, 0);
      MCP4922_write(SS0_IC5_RED, DAC_CHAN_B, new_z << 4);
    }
    else
    {
      MCP4922_write(SS2_IC3_GRE_BLU, DAC_CHAN_A, new_z << 4);
      MCP4922_write(SS0_IC5_RED, DAC_CHAN_B, 0);
    }

    old_z = z;    // remember for next time
  }

  // A small optimisation (if)
  if (ch1 != old_ch1)
  {
    goto_x(ch1);
    old_ch1 = ch1;
  }

  if (ch2 != old_ch2)
  {
    goto_y(ch2);
    old_ch2 = ch2;
  }
}

uint16_t read_gpio()
{
  //Serial.println("read gpio");
  int com = 0;    // Command received from IR remote

  if (IrReceiver.decode())    // Check if a button has been pressed on the IR remote
  {
    IrReceiver.resume(); // Enable receiving of the next value
    /*
       HX1838 Infrared Remote Control Module (£1/$1/1€ on Aliexpress)

       1     0x45 | 2     0x46 | 3     0x47
       4     0x44 | 5     0x40 | 6     0x43
       7     0x07 | 8     0x15 | 9     0x09
     * *     0x00 | 0     ???? | #     0x0D -> need to test value for 0
       OK    0x1C |
       Left  0x08 | Right 0x5A
       Up    0x18 | Down  0x52

    */
    com = IrReceiver.decodedIRData.command;
    Serial.println(com, HEX);

    if (com == 0x45)  // 1
      return 8;       // left forward
    if (com == 0x07)  // 7
      return 4;       // left reverse
    if (com == 0x47)  // 3
      return 2;       // right forward
    if (com == 0x09)  // 9
      return 1;       // right forward
    if (com == 0x1C)  // OK
      return 32;      // start game
    if (com == 0x0D)  // #
      return 16;      // left reverse

  }
  else
    //   return GPIOB->IDR; <- any pressed buttons
    return 0;
}

// Atari DVG and AVG simulators

void avg_vector_timer(int32_t deltax, int32_t deltay)
{
#if 0
  deltax = labs(deltax);
  deltay = labs(deltay);
  g_vctr_vg_done_cyc += max(deltax, deltay) >> 17;
#endif
}

/*
  static void dvg_vector_timer(int32_t scale)
  {
  g_vctr_vg_done_cyc += 4 << scale;
  }

  static void dvg_draw_vector_list(void)
  {
  static int32_t pc;
  static int32_t sp;
  static int32_t stack[MAXSTACK];

  static int32_t scale;

  static int32_t currentx;
  static int32_t currenty;

  int32_t done = 0;

  int32_t firstwd, secondwd;
  int32_t opcode;

  int32_t x, y;
  int32_t z, temp;
  int32_t a;

  int32_t oldx, oldy;
  int32_t deltax, deltay;
  pc = 0;
  sp = 0;
  scale = 0;

  if (g_vctr_portrait) {
    currentx = (1023) * 8192;
    currenty = (512) * 8192;
  } else {
    currentx = (512) * 8192;
    currenty = (1023) * 8192;
  }
  while (!done) {
    g_vctr_vg_done_cyc += 8;
    firstwd = memrdwd(map_addr(pc), 0, 0);
    opcode = firstwd >> 12;
    pc++;
    if ((opcode >= 0) && (opcode <= DLABS)) {
      secondwd = memrdwd(map_addr(pc), 0, 0);
      pc++;
    }
    switch (opcode) {
      case 0:
      case 1:
      case 2:
      case 3:
      case 4:
      case 5:
      case 6:
      case 7:
      case 8:
      case 9:
        y = firstwd & 0x03ff;
        if (firstwd & 0x0400) {
          y = -y;
        }
        x = secondwd & 0x03ff;
        if (secondwd & 0x400) {
          x = -x;
        }
        z = secondwd >> 12;
        oldx = currentx;
        oldy = currenty;
        temp = (scale + opcode) & 0x0f;
        if (temp > 9) {
          temp = -1;
        }
        deltax = (x << 21) >> (30 - temp);
        deltay = (y << 21) >> (30 - temp);
        currentx += deltax;
        currenty -= deltay;
        dvg_vector_timer(temp);
        draw_line(oldx, oldy, currentx, currenty, 7, z);
        break;
      case DLABS:
        x = twos_comp_val(secondwd, 12);
        y = twos_comp_val(firstwd, 12);
        scale = secondwd >> 12;
        currentx = x;
        currenty = (896 - y);
        break;
      case DHALT:
        done = 1;
        break;
      case DJSRL:
        a = firstwd & 0x0fff;
        stack[sp] = pc;
        if (sp == (MAXSTACK - 1)) {
          done = 1;
          sp = 0;
        } else {
          sp++;
        }
        pc = a;
        break;
      case DRTSL:
        if (sp == 0) {
          done = 1;
          sp = MAXSTACK - 1;
        } else {
          sp--;
        }
        pc = stack[sp];
        break;
      case DJMPL:
        a = firstwd & 0x0fff;
        pc = a;
        break;
      case DSVEC:
        y = firstwd & 0x0300;
        if (firstwd & 0x0400) {
          y = -y;
        }
        x = (firstwd & 0x03) << 8;
        if (firstwd & 0x04) {
          x = -x;
        }
        z = (firstwd >> 4) & 0x0f;
        temp = 2 + ((firstwd >> 2) & 0x02) + ((firstwd >> 11) & 0x01);
        oldx = currentx; oldy = currenty;
        temp = (scale + temp) & 0x0f;
        if (temp > 9) {
          temp = -1;
        }
        deltax = (x << 21) >> (30 - temp);
        deltay = (y << 21) >> (30 - temp);
        currentx += deltax;
        currenty -= deltay;
        dvg_vector_timer(temp);
        draw_line(oldx, oldy, currentx, currenty, 7, z);
        break;
      default:
        done = 1;
    }
  }
  }
*/

void avg_draw_vector_list(void)
{
  static int pc;
  static int sp;
  static int stack [MAXSTACK];

  static long xscale, yscale;   // June 2022 RC separate X & Y scales to fill screen
  static int statz;
  static int color;

  int done = 0;

  int firstwd, secondwd;
  int opcode;

  int x, y, z, b, l, a;

  long oldx, oldy;
  long deltax, deltay;

  pc = 0;
  sp = 0;
#define XREF 8192   // The smaller these numbers are, the faster the game executes!
#define YREF 8192
  xscale = 8192;
  yscale = 8192;
  statz = 0;
  color = 0;

  //#define HSIZE 384
#define HSIZE 512
#define VSIZE 512

  if (g_vctr_portrait) {
    currentx = HSIZE * 8192;
    currenty = VSIZE * 8192;
  } else {
    currentx = VSIZE * 8192;
    currenty = HSIZE * 8192;
  }

  firstwd = memrdwd(map_addr(pc), 0, 0);
  secondwd = memrdwd(map_addr(pc + 1), 0, 0);

  while (!done)
  {
    g_vctr_vg_done_cyc += 8;
    firstwd = memrdwd(map_addr(pc), 0, 0);
    opcode = firstwd >> 13;
    pc++;

    if (opcode == VCTR)
    {
      secondwd = memrdwd(map_addr(pc), 0, 0);
      pc++;
    }

    if ((opcode == STAT) && ((firstwd & 0x1000) != 0))
      opcode = SCAL;

    switch (opcode)
    {
      case VCTR:
        x = twos_comp_val(secondwd, 13);
        y = twos_comp_val(firstwd, 13);
        z = 2 * (secondwd >> 13);
        if (z == 2)
          z = statz;

        oldx = currentx;
        oldy = currenty;
        deltax = x * xscale; deltay = y * yscale;
        currentx += deltax;
        currenty -= deltay;
        avg_vector_timer(deltax, deltay);
        draw_line(oldx >> 13, oldy >> 13, currentx >> 13, currenty >> 13, color, z);
        break;

      case SVEC:
        x = twos_comp_val(firstwd, 5) << 1;
        y = twos_comp_val(firstwd >> 8, 5) << 1;
        z = 2 * ((firstwd >> 5) & 7);

        if (z == 2)
          z = statz;

        oldx = currentx;
        oldy = currenty;
        deltax = x * xscale; deltay = y * yscale;
        currentx += deltax;
        currenty -= deltay;
        avg_vector_timer(deltax, deltay);
        draw_line(oldx >> 13, oldy >> 13, currentx >> 13, currenty >> 13, color, z);
        break;

      case STAT:
        color = firstwd & 0x0f;
        statz = (firstwd >> 4) & 0x0f;
        /* should do e, h, i flags here! */
        break;

      case SCAL:          // scaling of graphics
        b = (firstwd >> 8) & 0x07;
        l = firstwd & 0xff;
        xscale = (XREF - (l << 6)) >> b;
        yscale = (YREF - (l << 6)) >> b;
        /* scale = (1.0-(l/256.0)) * (2.0 / (1 << b)); */
        break;

      case CNTR:    // centre?
        //        d = firstwd & 0xff; // seems to be unused
        if (g_vctr_portrait) {
          currentx = HSIZE * 8192;
          currenty = VSIZE * 8192;
        } else {
          currentx = VSIZE * 8192;
          currenty = HSIZE * 8192;
        }
        break;

      case RTSL:

        if (sp != 0)
          sp--;

        pc = stack[sp];
        break;

      case HALT:
        done = 1;
        break;

      case JMPL:
        a = firstwd & 0x1fff;
        pc = a;
        break;

      case JSRL:
        a = firstwd & 0x1fff;
        stack[sp] = pc;

        if (sp != (MAXSTACK - 1))
          sp++;

        pc = a;
        break;

      default:
        break;
    }
  }
}

int32_t vg_done(uint32_t cyc)
{
  if (g_vctr_vg_busy && (cyc > g_vctr_vg_done_cyc))
    g_vctr_vg_busy = 0;

  return !g_vctr_vg_busy;
}

void vg_go(uint32_t cyc)
{
  g_vctr_vg_busy = 1;
  g_vctr_vg_done_cyc = cyc + 8;
  //    dvg_draw_vector_list();
  avg_draw_vector_list();
}

void vg_reset(uint32_t cyc)
{
  g_vctr_vg_busy = 0;
}

void tag_area (unsigned addr, unsigned len, int dir, int tag)
{
  unsigned i;

  for (i = 0; i < len; i++)
  {
    if (dir & RD)
      g_sys_mem[addr].tagr = tag;

    if (dir & WR)
      g_sys_mem[addr].tagw = tag;

    addr++;
  }
}

void read_rom_image (char *fn, unsigned faddr, unsigned len, unsigned off_set)
{
  unsigned j;

  // open the file on the sd card
  File dataFile = SD.open(fn, FILE_READ);

  if (dataFile)
  {
    Serial.println(fn);

    for (j = 0; j < len; j++)
    {
      g_sys_mem[faddr].cell = dataFile.read();
      g_sys_mem[faddr].tagr = 0;
      g_sys_mem[faddr].tagw = ROMWRT;
      faddr++;
    }

    // close the file:
    dataFile.close();
  }
  else
    // if the file didn't open, print an error:
    Serial.println("Error opening file");
}

void setup_roms_and_tags (rom_info *rom_list, tag_info *tag_list)
{
  while (rom_list->name != NULL)
  {
    read_rom_image(rom_list->name, rom_list->addr, rom_list->len, rom_list->offset);
    rom_list++;
  }

  while (tag_list->len != 0)
  {
    tag_area(tag_list->addr, tag_list->len, tag_list->dir, tag_list->tag);
    tag_list++;
  }
}

void copy_rom(unsigned src, unsigned dest, unsigned len)
{
  unsigned i;

  for (i = 0; i < len; i++)
  {
    g_sys_mem[dest].cell = g_sys_mem[src].cell;
    g_sys_mem[dest].tagr = g_sys_mem[src].tagr;
    g_sys_mem[dest].tagw = g_sys_mem[src].tagw;
    dest++;
    src++;
  }
}

void setup_game(void)
{
  tag_area (0x0000, 0x10000, RD | WR, UNKNOWN);   // maybe not necessary?
  setup_roms_and_tags (battlezone_roms, battlezone_tags);

  /* copy_rom (0x5000, 0x4000, 0x1000); */
  /* copy_rom (0x5000, 0xd000, 0x3000); */
  copy_rom (0x7ffa, 0xfffa, 6);

  g_vctr_vector_mem_offset = 0x2000;
  g_sys_optionreg[0] = (~0xE8) & 0xff; // Inverted! 0xE8 -> English, bonus 15k&100k, missile @10k, 5 tanks
}

// main functions

void InitSD()
{
  // see if the card is present and can be initialised:
  if (!SD.begin(chipSelect))
  {
    Serial.println("Card failed, or not present");
    // don't do anything more:
    return;
  }
  else
    Serial.println("Card initialised.");
}

void setup()
{
  Serial.begin(115200);
  while ( !Serial && millis() < 4000 );

  // Configure buttons on vstcm for input using built in pullup resistors
  pinMode(0, INPUT_PULLUP);
  pinMode(1, INPUT_PULLUP);
  pinMode(2, INPUT_PULLUP);
  pinMode(3, INPUT_PULLUP);
  pinMode(4, INPUT_PULLUP);

  // Apparently, pin 10 has to be defined as an OUTPUT pin to designate the Arduino as the SPI master.
  // Even if pin 10 is not being used... Is this true for Teensy 4.1?
  // The default mode is INPUT. You must explicitly set pin 10 to OUTPUT (and leave it as OUTPUT).
  pinMode(10, OUTPUT);
  digitalWriteFast(10, HIGH);
  delayNanoseconds(100);

  InitSD();                 // Initialise SD card
  IR_remote_setup();        // Set up IR remote
  add_sounds();
  init_dac();

  setup_game();
  g_cpu_save_PC = (memrd(0xfffd, 0, 0) << 8) | memrd(0xfffc, 0, 0);
  g_cpu_save_A = 0;
  g_cpu_save_X = 0;
  g_cpu_save_Y = 0;
  g_cpu_save_flags = 0;
  g_cpu_save_totcycles = 0;
  g_cpu_irq_cycle = 6144;

  // code seems to run a little faster if we do begin and end here rather than on each cycle
  SPI.beginTransaction(SPISettings(115000000, MSBFIRST, SPI_MODE0));
  SPI.endTransaction();

  // Turn beam off while setting up game
  MCP4922_write(SS2_IC3_GRE_BLU, DAC_CHAN_A, 0);
}

// math box simulation (Battlezone/Red Baron/Tempest)

/* math box scratch registers */
int16_t mb_reg[16];

/* math box result */
int16_t mb_result = 0;

/*define MB_TEST*/

void mb_go(int addr, uint8_t data)
{
  int32_t mb_temp;  /* temp 32-bit multiply results */
  int16_t mb_q;     /* temp used in division */
  int msb;
  switch (addr) {
    case 0x00: mb_result = REG0 = (REG0 & 0xff00) | data;        break;
    case 0x01: mb_result = REG0 = (REG0 & 0x00ff) | (data << 8); break;
    case 0x02: mb_result = REG1 = (REG1 & 0xff00) | data;        break;
    case 0x03: mb_result = REG1 = (REG1 & 0x00ff) | (data << 8); break;
    case 0x04: mb_result = REG2 = (REG2 & 0xff00) | data;        break;
    case 0x05: mb_result = REG2 = (REG2 & 0x00ff) | (data << 8); break;
    case 0x06: mb_result = REG3 = (REG3 & 0xff00) | data;        break;
    case 0x07: mb_result = REG3 = (REG3 & 0x00ff) | (data << 8); break;
    case 0x08: mb_result = REG4 = (REG4 & 0xff00) | data;        break;
    case 0x09: mb_result = REG4 = (REG4 & 0x00ff) | (data << 8); break;
    case 0x0a: mb_result = REG5 = (REG5 & 0xff00) | data;        break;
    /* note: no function loads low part of REG5 without performing a computation */

    case 0x0c: mb_result = REG6 = data; break;
    /* note: no function loads high part of REG6 */

    case 0x15: mb_result = REG7 = (REG7 & 0xff00) | data;        break;
    case 0x16: mb_result = REG7 = (REG7 & 0x00ff) | (data << 8); break;

    case 0x1a: mb_result = REG8 = (REG8 & 0xff00) | data;        break;
    case 0x1b: mb_result = REG8 = (REG8 & 0x00ff) | (data << 8); break;

    case 0x0d: mb_result = REGa = (REGa & 0xff00) | data;        break;
    case 0x0e: mb_result = REGa = (REGa & 0x00ff) | (data << 8); break;
    case 0x0f: mb_result = REGb = (REGb & 0xff00) | data;        break;
    case 0x10: mb_result = REGb = (REGb & 0x00ff) | (data << 8); break;

    case 0x17: mb_result = REG7; break;
    case 0x19: mb_result = REG8; break;
    case 0x18: mb_result = REG9; break;

    case 0x0b:

      REG5 = (REG5 & 0x00ff) | (data << 8);

      REGf = 0xffff;
      REG4 -= REG2;
      REG5 -= REG3;

step_048:

      mb_temp = ((int32_t) REG0) * ((int32_t) REG4);
      REGc = mb_temp >> 16;
      REGe = mb_temp & 0xffff;

      mb_temp = ((int32_t) - REG1) * ((int32_t) REG5);
      REG7 = mb_temp >> 16;
      mb_q = mb_temp & 0xffff;

      REG7 += REGc;

      /* rounding */
      REGe = (REGe >> 1) & 0x7fff;
      REGc = (mb_q >> 1) & 0x7fff;
      mb_q = REGc + REGe;
      if (mb_q < 0)
        REG7++;

      mb_result = REG7;

      if (REGf < 0)
        break;

      REG7 += REG2;

    /* fall into command 12 */

    case 0x12:

      mb_temp = ((int32_t) REG1) * ((int32_t) REG4);
      REGc = mb_temp >> 16;
      REG9 = mb_temp & 0xffff;

      mb_temp = ((int32_t) REG0) * ((int32_t) REG5);
      REG8 = mb_temp >> 16;
      mb_q = mb_temp & 0xffff;

      REG8 += REGc;

      /* rounding */
      REG9 = (REG9 >> 1) & 0x7fff;
      REGc = (mb_q >> 1) & 0x7fff;
      REG9 += REGc;
      if (REG9 < 0)
        REG8++;
      REG9 <<= 1;  /* why? only to get the desired load address? */

      mb_result = REG8;

      if (REGf < 0)
        break;

      REG8 += REG3;

      REG9 &= 0xff00;

    /* fall into command 13 */

    case 0x13:
      REGc = REG9;
      mb_q = REG8;
      goto step_0bf;

    case 0x14:
      REGc = REGa;
      mb_q = REGb;

step_0bf:
      REGe = REG7 ^ mb_q;  /* save sign of result */
      REGd = mb_q;
      if (mb_q >= 0)
        mb_q = REGc;
      else
      {
        REGd = - mb_q - 1;
        mb_q = - REGc - 1;
        if ((mb_q < 0) && ((mb_q + 1) < 0))
          REGd++;
        mb_q++;
      }

      /* step 0c9: */
      /* REGc = abs (REG7) */
      if (REG7 >= 0)
        REGc = REG7;
      else
        REGc = -REG7;

      REGf = REG6;  /* step counter */

      do
      {
        REGd -= REGc;
        msb = ((mb_q & 0x8000) != 0);
        mb_q <<= 1;
        if (REGd >= 0)
          mb_q++;
        else
          REGd += REGc;
        REGd <<= 1;
        REGd += msb;
      }
      while (--REGf >= 0);

      if (REGe >= 0)
        mb_result = mb_q;
      else
        mb_result = - mb_q;
      break;

    case 0x11:
      REG5 = (REG5 & 0x00ff) | (data << 8);
      REGf = 0x0000;  /* do everything in one step */
      goto step_048;
      break;

    case 0x1c:
      /* window test? */
      REG5 = (REG5 & 0x00ff) | (data << 8);
      do
      {
        REGe = (REG4 + REG7) >> 1;
        REGf = (REG5 + REG8) >> 1;
        if ((REGb < REGe) && (REGf < REGe) && ((REGe + REGf) >= 0))
        {
          REG7 = REGe;
          REG8 = REGf;
        }
        else
        {
          REG4 = REGe;
          REG5 = REGf;
        }
      }
      while (--REG6 >= 0);

      mb_result = REG8;
      break;

    case 0x1d:
      REG3 = (REG3 & 0x00ff) | (data << 8);

      REG2 -= REG0;
      if (REG2 < 0)
        REG2 = -REG2;

      REG3 -= REG1;
      if (REG3 < 0)
        REG3 = -REG3;

    /* fall into command 1e */

    case 0x1e:
      /* result = max (REG2, REG3) + 3/8 * min (REG2, REG3) */
      if (REG3 >= REG2)
      {
        REGc = REG2;
        REGd = REG3;
      }
      else
      {
        REGd = REG2;
        REGc = REG3;
      }
      REGc >>= 2;
      REGd += REGc;
      REGc >>= 1;
      mb_result = REGd = (REGc + REGd);
      break;

    case 0x1f:
      break;
  }
}

// null display for Atari Vector game simulator

void plot(int32_t x, int32_t y, int32_t z)
{
  const uint16_t vctr_maxx = 1024;   // values too high, should be 1280x1024?
  const uint16_t vctr_maxy = 1024;
  const uint16_t vctr_minx = 256;
  const uint16_t vctr_miny = 256;

  // clip off screen drawing - needs work!

  if (x > vctr_maxx || x < vctr_minx || y > vctr_maxy || y < vctr_miny)
  {
    MCP4922_write(SS2_IC3_GRE_BLU, DAC_CHAN_A, 0);
    return;
  }

  // scale by 3x
  // Looks on the scope as if X is wrapping. Scale it less. we do Y also for symmetry
  // dac_out((x << 1) + x, (y << 1) + y, z);
  //dac_out(x*5-1536, y*8 - 4096, z);

  // problem with scaling is making weird vectors when missile explodes
  // try serial printing values to scale correctly

  /* Serial.print("x ");
    Serial.print(x*8-4097);
    Serial.print(" y ");
    Serial.println(y*8-4097);
  */
  dac_out(x * 8 - 4097 , y * 8 - 4097, z);
}

void draw_line(int32_t x1, int32_t y1, int32_t x2, int32_t y2, int32_t c, int32_t z)
{
  int32_t dx;
  int32_t dy;
  int32_t sx;
  int32_t sy;
  int32_t err;
  int32_t e2;

  dx = abs(x2 - x1);
  dy = abs(y2 - y1);

  x1 += 256;
  x2 += 256;
  y1 += 256;
  y2 += 256;

  sx = x1 < x2 ? 1 : -1;
  sy = y1 < y2 ? 1 : -1;

  err = dx - dy;

  while (1)
  {
    plot(x1, y1, z);
    if (x1 == x2 && y1 == y2)
      break;

    e2 = 2 * err;

    if (e2 > -dy)
    {
      err -= dy;
      x1 += sx;
    }

    if (x1 == x2 && y1 == y2)
    {
      plot(x1, y1, z);
      break;
    }

    if (e2 < dx)
    {
      err += dx;
      y1 += sy;
    }
  }

  // ensure that we end up exactly where we want
  // goto_x(x1);
  // goto_y(y1);
  // brightness(0,0,0);
}

#ifdef POKEY_DEBUG
uint8_t pokey_wreg_inited [MAX_POKEY][MAX_REG] = { { 0 } };
#endif

uint8_t pokey_read (int pokeynum, int reg, int PC, unsigned long cyc)
{
  switch (reg)
  {
    case RANDOM:
      if ((pokey_wreg [pokeynum] [SKCTL] & 0x03) != 0x00)
        pokey_rreg [pokeynum] [RANDOM] = (rand () >> 12) & 0xff;
      return (pokey_rreg [pokeynum] [RANDOM]);
    default:
#ifdef POKEY_DEBUG
      printf ("pokey %d read reg %1x (%s)\n", pokeynum, reg, pokey_rreg_name [reg]);
#endif
      return (pokey_rreg [pokeynum] [RANDOM]);
  }
}

void pokey_write (int pokeynum, int reg, uint8_t val, int PC, unsigned long cyc)
{
#ifdef POKEY_DEBUG
  if (! pokey_wreg_inited [pokeynum] [reg])
  {
    pokey_wreg_inited [pokeynum] [reg] = 1;
    pokey_wreg [pokeynum] [reg] = val + 1;  /* make sure we log it */
  }
  if (pokey_wreg [pokeynum] [reg] != val)
  {
    printf ("pokey %d reg %1x (%s) write data %02x\n", pokeynum, reg, pokey_wreg_name [reg], val);
  }
#endif
  pokey_wreg [pokeynum] [reg] = val;
}

extern int _end;

caddr_t _sbrk(int incr)
{
  static unsigned char *heap = NULL;
  unsigned char *prev_heap;
  if (heap == NULL) {
    heap = (unsigned char *)&_end;
  }
  prev_heap = heap;
  // Check for out of space here...
  heap += incr;
  return (caddr_t) prev_heap;
}

void IR_remote_setup()
{
  // Start the receiver and if not 3. parameter specified,
  // take LED_BUILTIN pin from the internal boards definition as default feedback LED
  IrReceiver.begin(IR_RECEIVE_PIN, ENABLE_LED_FEEDBACK);

  // attachInterrupt(digitalPinToInterrupt(IR_RECEIVE_PIN), IR_remote_loop, CHANGE);
}

void goto_x(uint16_t x)
{
  // If FLIP X then invert x axis
  MCP4922_write(SS1_IC4_X_Y, 0, 4095 - x);
  // else
  //   MCP4922_write(SS1_IC4_X_Y, 0, x);
}

void goto_y(uint16_t y)
{
#ifdef FLIP_Y     // If FLIP Y then invert y axis
  MCP4922_write(SS1_IC4_X_Y, 1, 4095 - y);
#else
  MCP4922_write(SS1_IC4_X_Y, 1, y);
#endif
}

void MCP4922_write(int cs_pin, byte dac, uint16_t value)
{
  dac = dac << 7; // dac value is either 0 or 128

  value &= 0x0FFF; // mask out just the 12 bits of data

  // add the output channel A or B on the selected DAC, and buffer flag
#ifdef BUFFERED
  // select the output channel on the selected DAC, buffered, no gain
  value |= 0x7000 | (dac == 128 ? 0x8000 : 0x0000);
#else
  // select the output channel on the selected DAC, unbuffered, no gain
  value |= 0x3000 | (dac == 128 ? 0x8000 : 0x0000);
#endif

  while (activepin != 0)  // wait until previous transfer is complete
    ;

  activepin = cs_pin;     // store to deactivate at end of transfer
  digitalWriteFast(cs_pin, LOW);

  dmabuf[0] = dac | 0x30 | ((value >> 8) & 0x0f);
  dmabuf[1] = value & 0xff;

  // if we don't use a clean begin & end transaction then other code stops working properly, such as button presses
  // SPI.beginTransaction(SPISettings(115000000, MSBFIRST, SPI_MODE0));

  // This uses non blocking SPI with DMA
  SPI.transfer(dmabuf, nullptr, 2, callbackHandler);
}
