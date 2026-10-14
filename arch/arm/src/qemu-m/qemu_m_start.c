/****************************************************************************
 * arch/arm/src/qemu-m/qemu_m_start.c
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
#include <nuttx/init.h>

#include <arch/board/board.h>

#if defined(CONFIG_LIBC_FDT) && defined(CONFIG_DEVICE_TREE)
#  include <nuttx/fdt.h>
#endif

#include "arm_internal.h"
#include "chip.h"
#include "nvic.h"
#include "mpu.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* Idle thread stack sits just after .bss (see g_idle_topstack). */

#define HEAP_BASE ((uintptr_t)_ebss + CONFIG_IDLETHREAD_STACKSIZE)

/****************************************************************************
 * Public Data
 ****************************************************************************/

/* g_idle_topstack: The IDLE thread stack starts at the end of BSS and is of
 * size CONFIG_IDLETHREAD_STACKSIZE.  The heap continues from there to the
 * end of the kernel RAM region.
 */

const uintptr_t g_idle_topstack = HEAP_BASE;

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: __start
 *
 * Description:
 *   This is the reset entry point.  virt-m loads the ELF into RAM and the
 *   QEMU machine points the core's VTOR at the image base, so on reset the
 *   CPU fetches the initial MSP and this entry from the vector table.
 *
 ****************************************************************************/

void __start(void)
{
#ifndef CONFIG_BUILD_PIC
  const uint32_t *src;
  uint32_t *dest;
#endif

  /* If enabled, reset the MPU */

  mpu_early_reset();
#ifdef CONFIG_ARM_MPU
  mpu_showtype();
#endif
  arm_fpuconfig();

#ifndef CONFIG_BUILD_PIC

  /* Clear .bss.  The image runs from RAM, but .bss is not part of the
   * loaded ELF and must be zeroed here.
   */

#ifndef CONFIG_ARCH_SKIP_ZERO_BSS
  for (dest = (uint32_t *)_sbss; dest < (uint32_t *)_ebss; )
    {
      *dest++ = 0;
    }
#endif

  /* Copy .data from its load address to its run address.  For virt-m these
   * are the same DRAM region (see nuttx.ld), so this is normally a no-op;
   * it is kept for correctness if the layout ever changes.
   */

  for (src = (const uint32_t *)_eronly,
       dest = (uint32_t *)_sdata; dest < (uint32_t *)_edata;
      )
    {
      *dest++ = *src++;
    }
#endif

  /* Perform early serial initialization so the console is usable early. */

#ifdef USE_EARLYSERIALINIT
  arm_earlyserialinit();
#endif

#ifdef CONFIG_ARM_MPU
  /* Enable the MPU with the privileged default memory map (PRIVDEFENA=1):
   * unmapped regions fall back to the architectural memory map, so the
   * single-image RAM (text + data + stack all in the SRAM region) stays
   * accessible.  Unlike the mps chip we must NOT mark the text range
   * read-only here: on virt-m the image runs from RAM and .text/.data/.bss
   * share one SRAM segment, so a power-of-two MPU region over .text would
   * round up and swallow the stack, causing a DACCVIOL on the first push.
   * Fine-grained per-region protection is configured later (REQ-1.7).
   */

  mpu_control(true, false, true);
#endif

  /* Register the QEMU-generated device tree so the board FDT hooks can
   * discover the peripherals later in bring-up.  Same role as the A/R chip's
   * qemu_boot.c fdt_register(); M-profile has no bootloader-supplied DTB
   * pointer, so the base is the fixed VIRT_M_DTB_BASE.
   */

#if defined(CONFIG_LIBC_FDT) && defined(CONFIG_DEVICE_TREE)
  fdt_register((const char *)QEMU_M_DTB_BASE);
#endif

  /* Then start NuttX */

  nx_start();

  /* Shouldn't get here */

  for (; ; );
}
