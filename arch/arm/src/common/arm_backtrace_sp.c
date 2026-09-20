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

/****************************************************************************
 * Private Data
 ****************************************************************************/

static void **g_backtrace_code_regions;

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
static bool in_code_region(unsigned long pc)
{
  int i = 0;

  if (g_backtrace_code_regions)
    {
      while (g_backtrace_code_regions[i] &&
             (g_backtrace_code_regions[i] !=
              g_backtrace_code_regions[i + 1]))
        {
          if (g_backtrace_code_regions[i] <= (void *)pc &&
              g_backtrace_code_regions[i + 1] > (void *)pc)
            {
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

#ifdef CONFIG_ARM_UNWINDER_STACK_POINTER_INSTRUCTION_CHECK
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
#endif

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
static int backtrace_branch(unsigned long base, unsigned long top,
                            unsigned long sp, void **buffer, int size,
                            int *skip)
{
  unsigned long addr;
#ifdef CONFIG_ARM_UNWINDER_STACK_POINTER_INSTRUCTION_CHECK
  unsigned long site;
#endif
  int i;
  irqstate_t flags;

  /* Reject an sp outside [base, top) rather than scan up to top: doing so
   * would parse unrelated memory as stack, or fault on an unmapped address
   * while dumping a crash.  Stack overflow puts sp below base; a nested
   * interrupt puts it on the interrupt stack instead of the task stack.
   */

  if (sp < base || sp >= top)
    {
      return 0;
    }

  flags = enter_critical_section();

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

  leave_critical_section(flags);

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

      if (up_interrupt_context())
        {
          uint32_t *regs = (uint32_t *)running_regs();
          unsigned long usp = up_getusrsp(regs);
          unsigned long upc = up_getusrpc(regs);

#if CONFIG_ARCH_INTERRUPTSTACK > 7
          ret = backtrace_branch(up_get_intstackbase(this_cpu()),
                                 up_get_intstackbase(this_cpu()) +
                                 INTSTACK_SIZE, sp, buffer, size, &skip);
#else
          /* Without a dedicated interrupt stack the handler runs on the
           * interrupted task's stack.
           */

          ret = backtrace_branch(base, top, sp, buffer, size, &skip);
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
                  ret += backtrace_branch(base, top, usp, &buffer[ret],
                                          size - ret, &skip);
                }
            }
        }
      else
        {
          ret = backtrace_branch(base, top, sp, buffer, size, &skip);
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

      if (regs[REG_PC] && skip-- <= 0)
        {
          buffer[ret++] = (void *)regs[REG_PC];
        }

      sp = regs[REG_SP];
      ret += backtrace_branch(base, top, sp,
                              &buffer[ret], size - ret, &skip);
    }

  return ret;
}
