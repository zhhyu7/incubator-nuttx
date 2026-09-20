/****************************************************************************
 * arch/arm/src/common/arm_backtrace_sp.c
 *
 * Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.  The
 * ASF licenses this file to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance with the
 * License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.  See the
 * License for the specific language governing permissions and limitations
 * under the License.
 *
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <strings.h>

#include <nuttx/arch.h>

#include "sched/sched.h"
#include "arm_internal.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* Macro and definitions for simple decoding of instuctions.
 * To check an instruction, it is ANDed with the IMASK_ and
 * the result is compared with the IOP_. The macro INSTR_IS
 * does this and returns !0 to indicate a match.
 */

#define INSTR_IS(i, o)      (((i) & (IMASK_##o)) == (IOP_##o))

#define IMASK_T_BLX         0xff80      /* blx rN */
#define IOP_T_BLX           0x4780

#define IMASK_T_BL          0xf800      /* bl/blx <imm> first halfword */
#define IOP_T_BL            0xf000

/* The prologue guided unwinder decodes Thumb/Thumb-2 function prologues to
 * recover the frame layout.  Doing so means reading the code region as data,
 * so it shares the ARM_UNWINDER_STACK_POINTER_INSTRUCTION_CHECK switch with
 * the call site validation.  Like that validation, it assumes Thumb code.
 */

#ifdef CONFIG_ARM_UNWINDER_STACK_POINTER_INSTRUCTION_CHECK

/* Maximum number of bytes scanned backwards from the PC while looking for
 * the function prologue.  Without CONFIG_ALLSYMS there is no function
 * boundary information available, so a fixed window is used.  A PC further
 * than this from its function entry cannot be resolved, and unwinding of
 * that frame falls back to the plain stack scan.
 */

#define PROLOGUE_WINDOW CONFIG_ARM_UNWINDER_STACK_POINTER_PROLOGUE_WINDOW

/* 16 bit PUSH {reglist}       1011 010 M reglist[7:0] */

#define IMASK_T_PUSH        0xff00
#define IOP_T_PUSH          0xb400      /* M == 0, LR not pushed */
#define IMASK_T_PUSH_LR     0xff00
#define IOP_T_PUSH_LR       0xb500      /* M == 1, LR pushed     */

/* 16 bit SUB SP, #imm7 * 4    1011 0000 1 imm7 */

#define IMASK_T_SUB_SP      0xff80
#define IOP_T_SUB_SP        0xb080

/* 16 bit ADD r7, SP, #imm8 * 4  1010 1111 imm8 */

#define IMASK_T_ADD_FP      0xff00
#define IOP_T_ADD_FP        0xaf00

/* 16 bit MOV r7, SP */

#define IOP_T_MOV_FP        0x466f

/* First halfword of the 32 bit Thumb-2 prologue instructions */

#define IOP_T2_PUSH_W       0xe92d      /* PUSH.W {reglist}          */
#define IOP_T2_VPUSH        0xed2d      /* VPUSH {slist}             */
#define IOP_T2_SUB_W_I0     0xf1ad      /* SUB.W SP,SP,#modimm i=0   */
#define IOP_T2_SUB_W_I1     0xf5ad      /* SUB.W SP,SP,#modimm i=1   */
#define IOP_T2_SUBW_I0      0xf2ad      /* SUBW  SP,SP,#imm12  i=0   */
#define IOP_T2_SUBW_I1      0xf6ad      /* SUBW  SP,SP,#imm12  i=1   */
#define IOP_T2_STR_SP_WB    0xf84d      /* STR.W Rt,[SP,#-imm8]!     */
#define IOP_T2_STRD_SP_WB   0xe96d      /* STRD Rt,Rt2,[SP,#-imm]!   */

/* Second halfword of STR.W LR, [SP, #-imm8]!: Rt == LR, P/U/W == 1/0/1 */

#define T2_STR_LR_PREIDX    0xed00

/* Second halfword field checks: Rd == SP (bits[11:8] == 0b1101) */

#define T2_RD_SP_MASK       0x0f00
#define T2_RD_SP            0x0d00

/* PUSH.W register list: R0-R12 in bits[12:0], LR in bit 14 */

#define T2_PUSH_REGS        0x1fff
#define T2_PUSH_LR          0x4000

/****************************************************************************
 * Private Types
 ****************************************************************************/

/* One unwound frame: the layout recovered from the function prologue and
 * the return address read out of it
 */

struct frame_info_s
{
  int  frame_size;                      /* Total frame size, in words     */
  int  lr_offset;                       /* Saved LR word offset from SP,
                                         * -1 when LR is not on the stack */
  bool has_fp;                          /* r7 is used as a frame pointer  */
  unsigned long lr;                     /* Return address in the frame    */
  unsigned long site;                   /* Call that produced it          */
};
#endif /* CONFIG_ARM_UNWINDER_STACK_POINTER_INSTRUCTION_CHECK */

/****************************************************************************
 * Private Data
 ****************************************************************************/

static void **g_backtrace_code_regions;

/* Index of the region that matched last, see in_code_region() */

static int g_backtrace_code_region_hint;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: in_code_region
 *
 * Description:
 *  in_code_region()  check if the program counter is in the program
 *  section, program counter should always be within the view of executable
 *  sections.
 *
 * Input Parameters:
 *   pc    - Program counter address
 *
 * Returned Value:
 *   A boolean value: true the counter is valid
 *
 ****************************************************************************/

nosanitize_address
static inline bool in_code_region(unsigned long pc)
{
  int i = 0;

  if (g_backtrace_code_regions)
    {
      /* Consecutive calls almost always land in the same region, so try the
       * one that matched last before walking the list again.  A stale hint
       * only costs the walk it was meant to save, so this needs no locking;
       * up_backtrace_init_code_regions() resets it when the list changes.
       */

      i = g_backtrace_code_region_hint;
      if (g_backtrace_code_regions[i] != NULL &&
          g_backtrace_code_regions[i] <= (void *)pc &&
          g_backtrace_code_regions[i + 1] > (void *)pc)
        {
          return true;
        }

      i = 0;
      while (g_backtrace_code_regions[i] &&
             (g_backtrace_code_regions[i] !=
              g_backtrace_code_regions[i + 1]))
        {
          if (g_backtrace_code_regions[i] <= (void *)pc &&
              g_backtrace_code_regions[i + 1] > (void *)pc)
            {
              g_backtrace_code_region_hint = i;
              return true;
            }

          i += 2;
        }
    }

  /* When g_backtrace_code_regions is null, try to use all the text section */

  else if (pc >= (unsigned long)_START_TEXT && pc < (unsigned long)_END_TEXT)
    {
      return true;
    }

  return false;
}

#ifdef CONFIG_ARM_UNWINDER_STACK_POINTER_INSTRUCTION_CHECK

/****************************************************************************
 * Name: code_region_floor
 *
 * Description:
 *  First address of the executable region holding pc.
 *
 *  The backwards scan for a prologue walks thousands of addresses that all
 *  belong to one region, and asking in_code_region() at every step means
 *  re-walking the region list every time.  Resolving the bound once lets the
 *  scan check it with a single comparison instead, which matters because the
 *  scan is the dominant cost of a backtrace.
 *
 * Input Parameters:
 *   pc - An address expected to be inside an executable region
 *
 * Returned Value:
 *   The first address of that region, or 0 when pc is not in one
 *
 ****************************************************************************/

nosanitize_address
static inline unsigned long code_region_floor(unsigned long pc)
{
  int i;

  if (g_backtrace_code_regions == NULL)
    {
      return pc >= (unsigned long)_START_TEXT &&
             pc < (unsigned long)_END_TEXT ?
             (unsigned long)_START_TEXT : 0;
    }

  /* Same hint as in_code_region().  The caller asks once per candidate
   * prologue, all for the same PC and so all for the same region, and with a
   * small window that walk would otherwise outweigh what the bound saves
   * inside the scan.
   */

  i = g_backtrace_code_region_hint;
  if (g_backtrace_code_regions[i] != NULL &&
      g_backtrace_code_regions[i] <= (void *)pc &&
      g_backtrace_code_regions[i + 1] > (void *)pc)
    {
      return (unsigned long)g_backtrace_code_regions[i];
    }

  for (i = 0; g_backtrace_code_regions[i] != NULL &&
              g_backtrace_code_regions[i] !=
              g_backtrace_code_regions[i + 1];
       i += 2)
    {
      if (g_backtrace_code_regions[i] <= (void *)pc &&
          g_backtrace_code_regions[i + 1] > (void *)pc)
        {
          g_backtrace_code_region_hint = i;
          return (unsigned long)g_backtrace_code_regions[i];
        }
    }

  return 0;
}

/****************************************************************************
 * Name: validate_bl_site
 *
 * Description:
 *  Check that a candidate return address was really produced by a call
 *  instruction, i.e. that the halfwords just below it decode as BLX rN,
 *  BL <imm> or BLX <imm>.
 *
 * Input Parameters:
 *   candidate - Candidate return address read from the stack
 *   site      - Receives the address of the branch instruction
 *
 * Returned Value:
 *   true when the candidate is preceded by a call instruction
 *
 ****************************************************************************/

nosanitize_address
static bool validate_bl_site(unsigned long candidate, unsigned long *site)
{
  unsigned long addr = (candidate & ~1ul) - 2;
  uint16_t ins1;
  uint16_t ins2;

  if (!in_code_region(addr))
    {
      return false;
    }

  /* 16 bit BLX rN: the whole call is the halfword at candidate - 2 */

  ins1 = *(uint16_t *)addr;
  if (INSTR_IS(ins1, T_BLX))
    {
      *site = addr;
      return true;
    }

  /* 32 bit BL / BLX <imm>: first halfword at candidate - 4, second at
   * candidate - 2.
   *
   * BL       11110 S imm10 : 11 J1 1 J2 imm11   -> hw2 & 0xd000 == 0xd000
   * BLX<imm> 11110 S imm10 : 11 J1 0 J2 imm10 H -> hw2 & 0xd000 == 0xc000
   */

  if (addr < 2)
    {
      return false;
    }

  addr -= 2;
  if (!in_code_region(addr))
    {
      return false;
    }

  ins1 = *(uint16_t *)addr;
  ins2  = *(uint16_t *)(addr + 2);

  if (INSTR_IS(ins1, T_BL) &&
      ((ins2 & 0xd000) == 0xd000 || (ins2 & 0xd000) == 0xc000))
    {
      *site = addr;
      return true;
    }

  return false;
}

/****************************************************************************
 * Name: thumb_expand_imm
 *
 * Description:
 *  Implements the Thumb-2 ThumbExpandImm() pseudo code used by the data
 *  processing instructions with a modified immediate operand.
 *
 * Input Parameters:
 *   imm12 - The i:imm3:imm8 field extracted from the instruction
 *
 * Returned Value:
 *   The expanded 32 bit immediate value
 *
 ****************************************************************************/

static inline unsigned int thumb_expand_imm(unsigned int imm12)
{
  unsigned int imm8 = imm12 & 0xff;
  unsigned int rot;

  if ((imm12 & 0xc00) == 0)
    {
      switch (imm12 & 0x300)
        {
          case 0x000:
            return imm8;
          case 0x100:
            return (imm8 << 16) | imm8;
          case 0x200:
            return (imm8 << 24) | (imm8 << 8);
          default:
            return (imm8 << 24) | (imm8 << 16) | (imm8 << 8) | imm8;
        }
    }

  /* Rotated form: ROR(0x80 | imm8[6:0], imm12[11:7]) */

  rot = (imm12 >> 7) & 0x1f;
  imm8 = 0x80 | (imm12 & 0x7f);

  return (imm8 >> rot) | (imm8 << (32 - rot));
}

/****************************************************************************
 * Name: decode_sub_sp_w
 *
 * Description:
 *  Decode a 32 bit "SUB{W} SP, SP, #imm" instruction and return the number
 *  of words it removes from the stack.
 *
 * Input Parameters:
 *   ins1 - First halfword
 *   ins2 - Second halfword
 *
 * Returned Value:
 *   Number of 32 bit words allocated, or -1 if this is not a stack
 *   adjustment that can be decoded.
 *
 ****************************************************************************/

static inline int decode_sub_sp_w(uint16_t ins1, uint16_t ins2)
{
  unsigned int imm12;
  unsigned int bytes;

  if ((ins2 & T2_RD_SP_MASK) != T2_RD_SP)
    {
      return -1;
    }

  imm12 = (ins2 & 0xff) | ((ins2 >> 4) & 0x700);

  switch (ins1)
    {
      case IOP_T2_SUB_W_I0:
        bytes = thumb_expand_imm(imm12);
        break;

      case IOP_T2_SUB_W_I1:
        bytes = thumb_expand_imm(imm12 | 0x800);
        break;

      case IOP_T2_SUBW_I0:
        bytes = imm12;
        break;

      case IOP_T2_SUBW_I1:
        bytes = imm12 | 0x800;
        break;

      default:
        return -1;
    }

  /* A stack adjustment is always a multiple of four bytes.  Anything else
   * means the halfwords were not really a SUB SP instruction.
   */

  if ((bytes & 3) != 0 || bytes > (PROLOGUE_WINDOW * 64))
    {
      return -1;
    }

  return bytes >> 2;
}

/****************************************************************************
 * Name: thumb_insn_len
 *
 * Description:
 *  Length in bytes of the Thumb instruction that starts with this halfword.
 *
 ****************************************************************************/

static inline unsigned long thumb_insn_len(uint16_t ins1)
{
  switch (ins1 & 0xf800)
    {
      case 0xe800:
      case 0xf000:
      case 0xf800:
        return 4;

      default:
        return 2;
    }
}

/****************************************************************************
 * Name: ends_prologue
 *
 * Description:
 *  Check whether an instruction ends the prologue region.  A prologue always
 *  completes before the function transfers control anywhere, and it never
 *  contains a stack release, so any branch or epilogue instruction marks the
 *  end of the window in which stack allocations may be accounted for.
 *
 ****************************************************************************/

nosanitize_address
static inline bool ends_prologue(unsigned long addr)
{
  uint16_t ins1 = *(uint16_t *)addr;

  /* 16 bit branches: B<cond>/SVC/UDF, B, BX/BLX reg, CBZ/CBNZ */

  if ((ins1 & 0xf000) == 0xd000 || (ins1 & 0xf800) == 0xe000 ||
      (ins1 & 0xff00) == 0x4700 || (ins1 & 0xf500) == 0xb100)
    {
      return true;
    }

  /* 16 bit epilogue: POP {reglist}, POP {..., PC}, ADD SP, #imm */

  if ((ins1 & 0xfe00) == 0xbc00 || (ins1 & 0xff80) == 0xb000)
    {
      return true;
    }

  if (thumb_insn_len(ins1) == 2 || !in_code_region(addr + 2))
    {
      return false;
    }

  /* 32 bit B/BL/BLX all have bit 15 set in the second halfword */

  if ((ins1 & 0xf800) == 0xf000 && (*(uint16_t *)(addr + 2) & 0x8000))
    {
      return true;
    }

  /* 32 bit epilogue and table branches */

  if (ins1 == 0xe8bd || ins1 == 0xecbd || (ins1 & 0xfff0) == 0xe8d0)
    {
      return true;
    }

  return false;
}

/****************************************************************************
 * Name: prologue_step
 *
 * Description:
 *  Decode one instruction of a function prologue.
 *
 * Input Parameters:
 *   addr        - Address of the instruction
 *   frame_size  - Running total of allocated words, updated in place
 *   lr_from_cfa - Word distance of the saved LR below the CFA, set when
 *                 this instruction is the PUSH that spilled LR
 *   has_fp      - Set when this instruction installs r7 as frame pointer
 *
 * Returned Value:
 *   Instruction length in bytes, or 0 when this is not a prologue
 *   instruction.
 *
 ****************************************************************************/

nosanitize_address
static int prologue_step(unsigned long addr, int *frame_size,
                         int *lr_from_cfa, bool *has_fp)
{
  uint16_t ins1 = *(uint16_t *)addr;
  uint16_t ins2;
  int words;

  /* 16 bit PUSH {reglist} / PUSH {reglist, LR}.  Registers are stored in
   * ascending order, so LR ends up in the highest slot of the block.
   */

  if (INSTR_IS(ins1, T_PUSH) || INSTR_IS(ins1, T_PUSH_LR))
    {
      if (INSTR_IS(ins1, T_PUSH_LR))
        {
          *frame_size += 1;
          *lr_from_cfa = *frame_size;
        }

      *frame_size += popcount(ins1 & 0xff);
      return 2;
    }

  /* 16 bit SUB SP, #imm7 * 4 */

  if (INSTR_IS(ins1, T_SUB_SP))
    {
      *frame_size += ins1 & 0x7f;
      return 2;
    }

  /* Frame pointer setup: ADD r7, SP, #imm or MOV r7, SP */

  if (INSTR_IS(ins1, T_ADD_FP) || ins1 == IOP_T_MOV_FP)
    {
      *has_fp = true;
      return 2;
    }

  if (!in_code_region(addr + 2))
    {
      return 0;
    }

  ins2 = *(uint16_t *)(addr + 2);

  /* 32 bit PUSH.W {reglist} */

  if (ins1 == IOP_T2_PUSH_W)
    {
      if (ins2 & T2_PUSH_LR)
        {
          *frame_size += 1;
          *lr_from_cfa = *frame_size;
        }

      *frame_size += popcount(ins2 & T2_PUSH_REGS);
      return 4;
    }

  /* 32 bit VPUSH {slist}: the block is always imm8 words long */

  if (ins1 == IOP_T2_VPUSH && (ins2 & 0x0e00) == 0x0a00)
    {
      *frame_size += ins2 & 0xff;
      return 4;
    }

  /* 32 bit STR.W LR, [SP, #-imm8]!  Hand written helpers use this instead
   * of PUSH.  LR lands at the new bottom of the stack.
   */

  if (ins1 == IOP_T2_STR_SP_WB && (ins2 & 0xff00) == T2_STR_LR_PREIDX)
    {
      words = (ins2 & 0xff) >> 2;
      if (words <= 0)
        {
          return 0;
        }

      *frame_size += words;
      *lr_from_cfa = *frame_size;
      return 4;
    }

  /* 32 bit STRD Rt, Rt2, [SP, #-imm8 * 4]!  Rt goes to the new bottom of
   * the stack, Rt2 one word above it.
   */

  if (ins1 == IOP_T2_STRD_SP_WB)
    {
      words = ins2 & 0xff;
      if (words <= 0)
        {
          return 0;
        }

      *frame_size += words;
      if (((ins2 >> 12) & 0xf) == 14)
        {
          *lr_from_cfa = *frame_size;
        }
      else if (((ins2 >> 8) & 0xf) == 14)
        {
          *lr_from_cfa = *frame_size - 1;
        }

      return 4;
    }

  /* 32 bit SUB.W / SUBW SP, SP, #imm */

  if (ins1 == IOP_T2_SUB_W_I0 || ins1 == IOP_T2_SUB_W_I1 ||
      ins1 == IOP_T2_SUBW_I0 || ins1 == IOP_T2_SUBW_I1)
    {
      words = decode_sub_sp_w(ins1, ins2);
      if (words >= 0)
        {
          *frame_size += words;
          return 4;
        }
    }

  return 0;
}

/****************************************************************************
 * Name: decode_prologue_forward
 *
 * Description:
 *  Replay a prologue forward from its first stack allocation, accumulating
 *  every allocation until the function transfers control or the PC is
 *  reached.  Instructions that are not stack allocations are stepped over,
 *  because the compiler freely interleaves them with the prologue.
 *
 * Input Parameters:
 *   start - Address of the first prologue instruction
 *   pc    - Program counter the unwinder is standing on.  Instructions at or
 *           above it have not executed yet and must not be accounted for.
 *   info  - Receives the recovered frame layout
 *
 * Returned Value:
 *   true when a consistent frame with a stacked LR was recovered
 *
 ****************************************************************************/

nosanitize_address
static bool decode_prologue_forward(unsigned long start, unsigned long pc,
                                    struct frame_info_s *info)
{
  unsigned long addr = start;
  unsigned long len;
  int lr_from_cfa = -1;
  int frame_size = 0;
  bool has_fp = false;

  while (addr < pc)
    {
      len = prologue_step(addr, &frame_size, &lr_from_cfa, &has_fp);
      if (len == 0)
        {
          if (ends_prologue(addr))
            {
              break;
            }

          len = thumb_insn_len(*(uint16_t *)addr);
        }

      addr += len;
    }

  /* The prologue must have spilled LR, and the slot has to sit inside the
   * frame it belongs to.  lr_from_cfa counts words below the CFA, so only
   * 1..frame_size is valid: zero would place LR at the CFA, above the
   * frame, which is what a malformed STRD decodes to.
   */

  if (lr_from_cfa <= 0 || frame_size < lr_from_cfa)
    {
      return false;
    }

  info->frame_size = frame_size;
  info->lr_offset  = frame_size - lr_from_cfa;
  info->has_fp     = has_fp;

  return true;
}

/****************************************************************************
 * Name: is_lr_spill
 *
 * Description:
 *  Check whether the instruction at addr is one of the forms the compiler
 *  and the hand written helpers use to spill LR onto the stack.
 *
 ****************************************************************************/

nosanitize_address
static inline bool is_lr_spill(unsigned long addr)
{
  uint16_t ins1 = *(uint16_t *)addr;
  uint16_t ins2;

  /* 16 bit PUSH {reglist, LR} */

  if (INSTR_IS(ins1, T_PUSH_LR))
    {
      return true;
    }

  if (thumb_insn_len(ins1) == 2 || !in_code_region(addr + 2))
    {
      return false;
    }

  ins2 = *(uint16_t *)(addr + 2);

  /* 32 bit PUSH.W {reglist, LR} */

  if (ins1 == IOP_T2_PUSH_W && (ins2 & T2_PUSH_LR))
    {
      return true;
    }

  /* 32 bit STR.W LR, [SP, #-imm8]! */

  if (ins1 == IOP_T2_STR_SP_WB && (ins2 & 0xff00) == T2_STR_LR_PREIDX)
    {
      return true;
    }

  /* 32 bit STRD with LR in either slot */

  if (ins1 == IOP_T2_STRD_SP_WB &&
      (((ins2 >> 12) & 0xf) == 14 || ((ins2 >> 8) & 0xf) == 14))
    {
      return true;
    }

  return false;
}

/****************************************************************************
 * Name: prologue_start
 *
 * Description:
 *  Step backwards over the register spills that the compiler emits before
 *  the PUSH that saves LR, for example the argument spill of a variadic
 *  function.  Those words are part of the frame and must be accounted for,
 *  otherwise the recovered frame is short.
 *
 * Input Parameters:
 *   lr_push - Address of the PUSH that spilled LR
 *
 * Returned Value:
 *   Address of the first prologue instruction
 *
 ****************************************************************************/

nosanitize_address
static unsigned long prologue_start(unsigned long lr_push)
{
  unsigned long addr = lr_push;
  int i;

  for (i = 0; i < 4; i++)
    {
      if (!in_code_region(addr - 2))
        {
          break;
        }

      /* 16 bit PUSH {reglist} without LR, or SUB SP, #imm */

      if (INSTR_IS(*(uint16_t *)(addr - 2), T_PUSH) ||
          INSTR_IS(*(uint16_t *)(addr - 2), T_SUB_SP))
        {
          addr -= 2;
          continue;
        }

      /* 32 bit PUSH.W {reglist} without LR */

      if (in_code_region(addr - 4) &&
          *(uint16_t *)(addr - 4) == IOP_T2_PUSH_W &&
          !(*(uint16_t *)(addr - 2) & T2_PUSH_LR))
        {
          addr -= 4;
          continue;
        }

      break;
    }

  return addr;
}

/****************************************************************************
 * Name: analyze_prologue_reverse
 *
 * Description:
 *  Recover one frame: scan backwards from the PC for the PUSH that spilled
 *  LR, replay the prologue forward from there to rebuild the frame layout,
 *  and read the return address out of the frame that layout describes.
 *
 *  Literal pools and jump tables live inside the code region, so a data word
 *  can easily look like a PUSH {..., LR}.  A candidate whose LR slot does
 *  not hold a validated return address is therefore discarded and the scan
 *  resumes at the next halfword.
 *
 * Input Parameters:
 *   pc   - A program counter inside the function body
 *   sp   - Stack pointer of the frame the PC belongs to
 *   top  - First address past the end of that stack
 *   info - Receives the recovered frame
 *
 * Returned Value:
 *   true when a frame holding a validated return address was recovered
 *
 ****************************************************************************/

nosanitize_address
static bool analyze_prologue_reverse(unsigned long pc, unsigned long sp,
                                     unsigned long top,
                                     struct frame_info_s *info)
{
  unsigned long floor;
  unsigned long lr_addr;
  unsigned long addr;
  int offset;

  pc &= ~1ul;

  /* The scan only ever moves down within the region holding the PC, so its
   * lower bound is fixed for the whole loop.
   */

  floor = code_region_floor(pc);
  if (floor == 0)
    {
      return false;
    }

  for (offset = 2; offset <= PROLOGUE_WINDOW; offset += 2)
    {
      addr = pc - offset;
      if (addr < floor)
        {
          break;
        }

      if (!is_lr_spill(addr) ||
          !decode_prologue_forward(prologue_start(addr), pc, info))
        {
          continue;
        }

      lr_addr = sp + info->lr_offset * 4;
      if (lr_addr + 4 > top)
        {
          continue;
        }

      info->lr = *(unsigned long *)lr_addr;
      if (in_code_region(info->lr) &&
          validate_bl_site(info->lr, &info->site))
        {
          return true;
        }
    }

  return false;
}

#endif /* CONFIG_ARM_UNWINDER_STACK_POINTER_INSTRUCTION_CHECK */

/****************************************************************************
 * Name: backtrace_branch
 *
 * Description:
 *  backtrace() parsing the return address through branch instruction.
 *  This is the word by word stack scan.  It is used standalone when the
 *  prologue unwinder is disabled, and as the fallback path when prologue
 *  analysis fails.
 *
 ****************************************************************************/

nosanitize_address
static int backtrace_branch(unsigned long top, unsigned long sp,
                            void **buffer, int size, int *skip)
{
  unsigned long addr;
#ifdef CONFIG_ARM_UNWINDER_STACK_POINTER_INSTRUCTION_CHECK
  unsigned long site;
#endif
  int i;

  for (i = 0; i < size && sp < top; sp += sizeof(unsigned long))
    {
      addr = *(unsigned long *)sp;
      if (!in_code_region(addr))
        {
          continue;
        }

#ifdef CONFIG_ARM_UNWINDER_STACK_POINTER_INSTRUCTION_CHECK
      if (!validate_bl_site(addr, &site))
        {
          continue;
        }

      if ((*skip)-- <= 0)
        {
          buffer[i++] = (void *)site;
        }
#else
      addr = (addr & ~1ul) - 2;
      if (!in_code_region(addr))
        {
          continue;
        }

      if ((*skip)-- <= 0)
        {
          buffer[i++] = (void *)addr;
        }
#endif
    }

  return i;
}

/****************************************************************************
 * Name: backtrace_stack
 *
 * Description:
 *  Unwind one stack frame by frame, decoding the prologue of the function
 *  that the PC belongs to and reading the saved LR out of the frame it
 *  describes.  Whatever the walk cannot resolve is handed to the word by
 *  word branch scan, which is the whole of the walk when prologue decoding
 *  is off.
 *
 ****************************************************************************/

nosanitize_address
static int backtrace_stack(unsigned long base, unsigned long top,
                           unsigned long sp, unsigned long pc,
                           void **buffer, int size, int *skip)
{
#ifdef CONFIG_ARM_UNWINDER_STACK_POINTER_INSTRUCTION_CHECK
  struct frame_info_s fi;
#endif
  irqstate_t flags;
  int i = 0;

  /* Reject an sp outside [base, top) rather than scan up to top: doing so
   * would parse unrelated memory as stack, or fault on an unmapped address
   * while dumping a crash.  Stack overflow puts sp below base; a nested
   * interrupt puts it on the interrupt stack instead of the task stack.
   */

  if (sp < base || sp >= top)
    {
      return 0;
    }

  flags = up_irq_save();

#ifdef CONFIG_ARM_UNWINDER_STACK_POINTER_INSTRUCTION_CHECK
  while (i < size && sp < top && analyze_prologue_reverse(pc, sp, top, &fi))
    {
      if ((*skip)-- <= 0)
        {
          buffer[i++] = (void *)fi.site;
        }

      sp += fi.frame_size * 4;
      pc  = fi.lr;
    }
#endif

  /* Scan what is left of the stack for return addresses confirmed by a call
   * instruction.  That covers the frames whose prologue could not be decoded
   * (assembly stub, leaf, heavily optimised function) and, when prologue
   * decoding is off, the whole stack.
   */

  if (i < size && sp < top)
    {
      i += backtrace_branch(top, sp, &buffer[i], size - i, skip);
    }

  up_irq_restore(flags);

  return i;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: up_backtrace_init_code_regions
 *
 * Description:
 *  The up call up_backtrace_init_code_regions() will set the start
 *  and end addresses of the customized program sections, this method
 *  will help the different boards to configure the current text
 *  sections for some complicate platforms
 *
 * Input Parameters:
 *   regions  The start and end address of the text segment
 *            This interface supports the input of multiple
 *            groups of sections, Each set of the sections
 *            must be a pair, the end of the area must specify
 *            two NULL porint, e.g :
 *
 *            static void *g_code_regions[] =
 *              {
 *                _START_TEXT,  _END_TEXT,
 *                _START2_TEXT, _END2_TEXT,
 *                _START3_TEXT, _END3_TEXT,
 *                NULL,         NULL,
 *              };
 *
 *              up_backtrace_init_code_regions(g_code_regions);
 *
 ****************************************************************************/

nosanitize_address
void up_backtrace_init_code_regions(void **regions)
{
  /* The hint indexes the old list and the new one may be shorter */

  g_backtrace_code_region_hint = 0;
  g_backtrace_code_regions = regions;
}

/****************************************************************************
 * Name: up_backtrace
 *
 * Description:
 *  up_backtrace()  returns  a backtrace for the TCB, in the array
 *  pointed to by buffer.  A backtrace is the series of currently active
 *  function calls for the program.  Each item in the array pointed to by
 *  buffer is of type void *, and is the return address from the
 *  corresponding stack frame.  The size argument specifies the maximum
 *  number of addresses that can be stored in buffer.   If  the backtrace is
 *  larger than size, then the addresses corresponding to the size most
 *  recent function calls are returned; to obtain the complete backtrace,
 *  make sure that buffer and size are large enough.
 *
 * Input Parameters:
 *   tcb    - Address of the task's TCB
 *   buffer - Return address from the corresponding stack frame
 *   size   - Maximum number of addresses that can be stored in buffer
 *
 * Returned Value:
 *   up_backtrace() returns the number of addresses returned in buffer
 *
 * Assumptions:
 *   Have to make sure tcb keep safe during function executing, it means
 *   1. Tcb have to be self or not-running.  In SMP case, the running task
 *      PC & SP cannot be backtrace, as whose get from tcb is not the newest.
 *   2. Tcb have to keep not be freed.  In task exiting case, have to
 *      make sure the tcb get from pid and up_backtrace in one critical
 *      section procedure.
 *
 ****************************************************************************/

nosanitize_address
int up_backtrace(struct tcb_s *tcb,
                 void **buffer, int size, int skip)
{
  struct tcb_s *rtcb = running_task();
  unsigned long base;
  unsigned long top;
  unsigned long sp;
  unsigned long pc;
  int ret;

  if (size <= 0 || !buffer)
    {
      return 0;
    }

  if (tcb == NULL)
    {
      tcb = rtcb;
    }

  base = (unsigned long)tcb->stack_base_ptr;
  top  = base + tcb->adj_stack_size;

  if (tcb == rtcb)
    {
      sp = up_getsp();
      pc = up_getpc();

      if (up_interrupt_context())
        {
          uint32_t *regs = (uint32_t *)running_regs();
          unsigned long usp = up_getusrsp(regs);
          unsigned long upc = up_getusrpc(regs);

#if CONFIG_ARCH_INTERRUPTSTACK > 7
          ret = backtrace_stack(up_get_intstackbase(this_cpu()),
                                up_get_intstackbase(this_cpu()) +
                                INTSTACK_SIZE, sp, pc, buffer, size,
                                &skip);
#else
          /* Without a dedicated interrupt stack the handler runs on the
           * interrupted task's stack.
           */

          ret = backtrace_stack(base, top, sp, pc, buffer, size, &skip);
#endif
          if (ret < size && usp != 0)
            {
              /* Crossing over to the stack that was interrupted.  The PC the
               * exception was taken at is the innermost frame of that stack
               * and is held in the saved context rather than on the stack,
               * so it has to be recorded here or the faulting function is
               * missing from the backtrace.
               */

              if (upc != 0 && in_code_region(upc) && skip-- <= 0)
                {
                  buffer[ret++] = (void *)upc;
                }

              if (ret < size)
                {
                  ret += backtrace_stack(base, top, usp, upc,
                                         &buffer[ret], size - ret, &skip);
                }
            }
        }
      else
        {
          ret = backtrace_stack(base, top, sp, pc, buffer, size, &skip);
        }
    }
  else
    {
      uint32_t *regs = (uint32_t *)tcb->xcp.regs;

      if (regs == NULL)
        {
          return 0;
        }

      ret = 0;

      pc = regs[REG_PC];
      sp = regs[REG_SP];

      if (pc && skip-- <= 0)
        {
          buffer[ret++] = (void *)pc;
        }

      ret += backtrace_stack(base, top, sp, pc, &buffer[ret],
                             size - ret, &skip);
    }

  return ret;
}
