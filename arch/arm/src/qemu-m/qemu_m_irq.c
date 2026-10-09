/****************************************************************************
 * arch/arm/src/qemu-m/qemu_m_irq.c
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

#include <assert.h>
#include <debug.h>

#include <nuttx/arch.h>
#include <nuttx/irq.h>

#include "ram_vectors.h"
#include "arm_internal.h"
#include "nvic.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define NVIC_ENA_OFFSET    (0)
#define NVIC_CLRENA_OFFSET (NVIC_IRQ0_31_CLEAR - NVIC_IRQ0_31_ENABLE)

#define DEFPRIORITY32      (NVIC_SYSH_PRIORITY_DEFAULT << 24 | \
                            NVIC_SYSH_PRIORITY_DEFAULT << 16 | \
                            NVIC_SYSH_PRIORITY_DEFAULT << 8  | \
                            NVIC_SYSH_PRIORITY_DEFAULT)

/* arm_m/nvic.h has no NVIC_IRQ_RESERVED alias; vector 0 is the reserved
 * slot (only used with CONFIG_DEBUG_FEATURES, same as the mps chip).
 */

#define QEMU_M_IRQ_RESERVED (0)

/****************************************************************************
 * Private Functions
 ****************************************************************************/

#ifdef CONFIG_DEBUG_FEATURES
static int qemu_m_nmi(int irq, void *context, void *arg)
{
  up_irq_save();
  _err("PANIC!!! NMI received\n");
  PANIC();
  return 0;
}

static int qemu_m_pendsv(int irq, void *context, void *arg)
{
#ifndef CONFIG_ARCH_HIPRI_INTERRUPT
  up_irq_save();
  _err("PANIC!!! PendSV received\n");
  PANIC();
#endif
  return 0;
}

static int qemu_m_reserved(int irq, void *context, void *arg)
{
  up_irq_save();
  _err("PANIC!!! Reserved interrupt\n");
  PANIC();
  return 0;
}
#endif

/****************************************************************************
 * Name: qemu_m_prioritize_syscall
 *
 * Description:
 *   Set the priority of the SVCall exception (system handler 11).
 *
 ****************************************************************************/

static inline void qemu_m_prioritize_syscall(int priority)
{
  uint32_t regval;

  regval  = getreg32(NVIC_SYSH8_11_PRIORITY);
  regval &= ~NVIC_SYSH_PRIORITY_PR11_MASK;
  regval |= (priority << NVIC_SYSH_PRIORITY_PR11_SHIFT);
  putreg32(regval, NVIC_SYSH8_11_PRIORITY);
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: up_prioritize_irq
 *
 * Description:
 *   Set the priority of an IRQ.
 *
 ****************************************************************************/

int up_prioritize_irq(int irq, int priority)
{
  uint32_t regaddr;
  uint32_t regval;
  int shift;

  DEBUGASSERT(irq >= 0 && irq < NR_IRQS &&
              (unsigned)priority <= NVIC_SYSH_PRIORITY_MIN);

  if (irq < 16)
    {
      regaddr = NVIC_SYSH_PRIORITY(irq);
      irq    -= 4;
    }
  else
    {
      irq    -= 16;
      regaddr = NVIC_IRQ_PRIORITY(irq);
    }

  regval      = getreg32(regaddr);
  shift       = ((irq & 3) << 3);
  regval     &= ~(0xff << shift);
  regval     |= (priority << shift);
  putreg32(regval, regaddr);

  return OK;
}

/****************************************************************************
 * Name: up_irqinitialize
 ****************************************************************************/

void up_irqinitialize(void)
{
  uint32_t regaddr;
  int num_priority_registers;
  int i;

  /* Disable all interrupts */

  for (i = 0; i < NR_IRQS - NVIC_IRQ_FIRST; i += 32)
    {
      putreg32(0xffffffff, NVIC_IRQ_CLEAR(i));
    }

  putreg32((uint32_t)_vectors, NVIC_VECTAB);

#ifdef CONFIG_ARCH_RAMVECTORS
  arm_ramvec_initialize();
#endif

  /* Set all interrupts (and exceptions) to the default priority */

  putreg32(DEFPRIORITY32, NVIC_SYSH4_7_PRIORITY);
  putreg32(DEFPRIORITY32, NVIC_SYSH8_11_PRIORITY);
  putreg32(DEFPRIORITY32, NVIC_SYSH12_15_PRIORITY);

  /* The NVIC ICTR register (bits 0-4) holds the number of interrupt lines */

  num_priority_registers = (getreg32(NVIC_ICTR) + 1) * 8;

  regaddr = NVIC_IRQ0_3_PRIORITY;
  while (num_priority_registers--)
    {
      putreg32(DEFPRIORITY32, regaddr);
      regaddr += 4;
    }

  /* Attach the SVCall and Hard Fault exception handlers. */

  irq_attach(NVIC_IRQ_SVCALL, arm_svcall, NULL);
  irq_attach(NVIC_IRQ_HARDFAULT, arm_hardfault, NULL);

  up_prioritize_irq(NVIC_IRQ_PENDSV, NVIC_SYSH_PRIORITY_MIN);
  qemu_m_prioritize_syscall(NVIC_SYSH_SVCALL_PRIORITY);

#ifdef CONFIG_ARM_MPU
  irq_attach(NVIC_IRQ_MEMFAULT, arm_memfault, NULL);
  up_enable_irq(NVIC_IRQ_MEMFAULT);
#endif

#ifdef CONFIG_DEBUG_FEATURES
  irq_attach(NVIC_IRQ_NMI, qemu_m_nmi, NULL);
#ifndef CONFIG_ARM_MPU
  irq_attach(NVIC_IRQ_MEMFAULT, arm_memfault, NULL);
#endif
  irq_attach(NVIC_IRQ_BUSFAULT, arm_busfault, NULL);
  irq_attach(NVIC_IRQ_USAGEFAULT, arm_usagefault, NULL);
  irq_attach(NVIC_IRQ_PENDSV, qemu_m_pendsv, NULL);
  irq_attach(NVIC_IRQ_DBGMONITOR, arm_dbgmonitor, NULL);
  irq_attach(QEMU_M_IRQ_RESERVED, qemu_m_reserved, NULL);
#endif

#ifndef CONFIG_SUPPRESS_INTERRUPTS
  arm_color_intstack();
  up_irq_enable();
#endif
}

/****************************************************************************
 * Name: qemu_m_irqinfo
 ****************************************************************************/

static int qemu_m_irqinfo(int irq, uintptr_t *regaddr, uint32_t *bit,
                          uintptr_t offset)
{
  int n;

  DEBUGASSERT(irq >= NVIC_IRQ_NMI && irq < NR_IRQS);

  /* Check for external interrupt */

  if (irq >= NVIC_IRQ_FIRST)
    {
      n        = irq - NVIC_IRQ_FIRST;
      *regaddr = NVIC_IRQ_ENABLE(n) + offset;
      *bit     = (uint32_t)0x1 << (n & 0x1f);
    }

  /* Handle processor exceptions.  Only a few can be disabled */

  else
    {
      *regaddr = NVIC_SYSHCON;
      if (irq == NVIC_IRQ_MEMFAULT)
        {
          *bit = NVIC_SYSHCON_MEMFAULTENA;
        }
      else if (irq == NVIC_IRQ_BUSFAULT)
        {
          *bit = NVIC_SYSHCON_BUSFAULTENA;
        }
      else if (irq == NVIC_IRQ_USAGEFAULT)
        {
          *bit = NVIC_SYSHCON_USGFAULTENA;
        }
      else if (irq == NVIC_IRQ_SYSTICK)
        {
          *regaddr = NVIC_SYSTICK_CTRL;
          *bit = NVIC_SYSTICK_CTRL_ENABLE;
        }
      else
        {
          return -EINVAL;
        }
    }

  return OK;
}

/****************************************************************************
 * Name: up_disable_irq
 ****************************************************************************/

void up_disable_irq(int irq)
{
  uintptr_t regaddr;
  uint32_t regval;
  uint32_t bit;

  if (qemu_m_irqinfo(irq, &regaddr, &bit, NVIC_CLRENA_OFFSET) == 0)
    {
      if (irq >= NVIC_IRQ_FIRST)
        {
          putreg32(bit, regaddr);
        }
      else
        {
          regval  = getreg32(regaddr);
          regval &= ~bit;
          putreg32(regval, regaddr);
        }
    }
}

/****************************************************************************
 * Name: up_enable_irq
 ****************************************************************************/

void up_enable_irq(int irq)
{
  uintptr_t regaddr;
  uint32_t regval;
  uint32_t bit;

  if (qemu_m_irqinfo(irq, &regaddr, &bit, NVIC_ENA_OFFSET) == 0)
    {
      if (irq >= NVIC_IRQ_FIRST)
        {
          putreg32(bit, regaddr);
        }
      else
        {
          regval  = getreg32(regaddr);
          regval |= bit;
          putreg32(regval, regaddr);
        }
    }
}

/****************************************************************************
 * Name: arm_ack_irq
 ****************************************************************************/

void arm_ack_irq(int irq)
{
}
