/****************************************************************************
 * arch/arm64/src/common/arm64_arch_timer.c
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
#include <debug.h>
#include <assert.h>
#include <stdio.h>

#include <nuttx/arch.h>
#include <arch/irq.h>
#include <arch/chip/chip.h>
#include <nuttx/kmalloc.h>
#include <nuttx/spinlock.h>
#include <nuttx/timers/arch_alarm.h>

#include "arm64_arch.h"
#include "arm64_internal.h"
#include "arm64_gic.h"
#include "arm64_arch_timer.h"
#include "arm64_smp.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* The timer registers and the timer interrupt are both selected by the
 * configured exception level and TrustZone state -- never by the level the
 * CPU happens to be running at.  EL3 and Secure EL1 share the same Secure
 * physical timer, whose control registers are the EL1 secure bank in both
 * cases.  The counter is read through the physical counter everywhere the
 * two agree; only the virtual timer needs its own read, since it is the
 * one an offset can be applied to.
 */

#if CONFIG_ARCH_ARM64_EXCEPTION_LEVEL == 2
/* NuttX runs directly at NS-EL2: EL2 hypervisor timer. */

#  define ARM64_ARCH_TIMER_IRQ              GIC_IRQ_HTM
#  define ARM64_TIMER_CVAL                  cnthp_cval_el2
#  define ARM64_TIMER_TVAL                  cnthp_tval_el2
#  define ARM64_TIMER_CTL                   cnthp_ctl_el2
#  define ARM64_TIMER_COUNT                 cntpct_el0
#elif CONFIG_ARCH_ARM64_EXCEPTION_LEVEL == 3 || \
      defined(CONFIG_ARCH_TRUSTZONE_SECURE)
/* Secure physical timer. */

#  define ARM64_ARCH_TIMER_IRQ              GIC_IRQ_STM
#  define ARM64_TIMER_CVAL                  cntps_cval_el1
#  define ARM64_TIMER_TVAL                  cntps_tval_el1
#  define ARM64_TIMER_CTL                   cntps_ctl_el1
#  define ARM64_TIMER_COUNT                 cntpct_el0
#elif defined(CONFIG_ARCH_TRUSTZONE_NONSECURE)
/* Non-secure physical timer. */

#  define ARM64_ARCH_TIMER_IRQ              GIC_IRQ_PTM
#  define ARM64_TIMER_CVAL                  cntp_cval_el0
#  define ARM64_TIMER_TVAL                  cntp_tval_el0
#  define ARM64_TIMER_CTL                   cntp_ctl_el0
#  define ARM64_TIMER_COUNT                 cntpct_el0
#else
/* Plain EL1 with neither EL2 nor TrustZone: virtual timer. */

#  define ARM64_ARCH_TIMER_IRQ              GIC_IRQ_VTM
#  define ARM64_TIMER_CVAL                  cntv_cval_el0
#  define ARM64_TIMER_TVAL                  cntv_tval_el0
#  define ARM64_TIMER_CTL                   cntv_ctl_el0
#  define ARM64_TIMER_COUNT                 cntvct_el0
#endif

/****************************************************************************
 * Inline Functions
 ****************************************************************************/

static inline void arm64_arch_timer_set_compare(uint64_t value)
{
  write_sysreg(value, ARM64_TIMER_CVAL);
}

static inline void arm64_arch_timer_set_relative(uint64_t value)
{
  write_sysreg(value, ARM64_TIMER_TVAL);
}

static inline void arm64_arch_timer_enable(bool enable)
{
  modify_sysreg(enable ? CNTP_CTL_ENABLE_BIT : 0u,
                CNTP_CTL_ENABLE_BIT, ARM64_TIMER_CTL);
}

static inline void arm64_arch_timer_set_irq_mask(bool mask)
{
  modify_sysreg(mask ? CNTP_CTL_IMASK_BIT : 0u,
                CNTP_CTL_IMASK_BIT, ARM64_TIMER_CTL);
}

static inline uint64_t arm64_arch_timer_count(void)
{
  return read_sysreg(ARM64_TIMER_COUNT);
}

static inline uint64_t arm64_arch_timer_get_cntfrq(void)
{
  return read_sysreg(cntfrq_el0);
}

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: arm64_arch_timer_compare_isr
 *
 * Description:
 *   Common timer interrupt callback.  When any oneshot timer interrupt
 *   expires, this function will be called.  It will forward the call to
 *   the next level up.
 *
 * Input Parameters:
 *   oneshot - The state associated with the expired timer
 *
 * Returned Value:
 *   Always returns OK
 *
 ****************************************************************************/

static int arm64_arch_timer_compare_isr(int irq, void *regs, void *arg)
{
  struct oneshot_lowerhalf_s *priv = (struct oneshot_lowerhalf_s *)arg;

  arm64_arch_timer_set_compare(UINT64_MAX);

  /* Then perform the callback */

  oneshot_process_callback(priv);

  return OK;
}

static clkcnt_t arm64_oneshot_max_delay(struct oneshot_lowerhalf_s *lower)
{
  return UINT64_MAX;
}

static clkcnt_t arm64_oneshot_current(struct oneshot_lowerhalf_s *lower)
{
  /* We do not need memory barrier here. */

  return arm64_arch_timer_count();
}

static void arm64_oneshot_start_absolute(struct oneshot_lowerhalf_s *lower,
                                         clkcnt_t expected)
{
  arm64_arch_timer_set_compare(expected);
}

static void arm64_oneshot_start(struct oneshot_lowerhalf_s *lower,
                                clkcnt_t delta)
{
  arm64_arch_timer_set_relative(delta);
}

static void arm64_oneshot_cancel(struct oneshot_lowerhalf_s *lower)
{
  arm64_arch_timer_set_compare(UINT64_MAX);
}

/****************************************************************************
 * Name: arm64_oneshot_initialize_per_cpu
 *
 * Description:
 *   Initialize the ARM generic timer for secondary CPUs.
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

static void arm64_oneshot_initialize_per_cpu(void)
{
  /* In secure world, mark the non-secure timer IRQ as non-secure
   * so that normal world can receive it.  PPI security group is
   * per-CPU, so this must run on every core.
   */

#ifdef CONFIG_ARCH_TRUSTZONE_SECURE
  up_secure_irq(GIC_IRQ_PTM, false);
  up_secure_irq(GIC_IRQ_HTM, false);

  /* Release the virtual timer too. A NuttX non-secure world uses the
   * non-secure physical timer (PTM), but a Linux REE entered at EL1NS and
   * not in hyp mode (arch_timer_select_ppi() -> ARCH_TIMER_VIRT_PPI) uses
   * the virtual timer instead -- this holds even when EL2 exists, because
   * the bootloader has already dropped to EL1 before handing over. Leaving
   * VTM in Group-1-Secure means such a REE never receives a single timer
   * interrupt: it cannot sleep, its timers never fire, and it only makes
   * progress while busy-spinning.
   */

  up_secure_irq(GIC_IRQ_VTM, false);
#endif

  /* First mask the timer interrupt and set compare to max
   * to ensure no spurious interrupt fires before the oneshot
   * framework is fully initialized.
   */

  arm64_arch_timer_set_irq_mask(true);
  arm64_arch_timer_set_compare(UINT64_MAX);
  arm64_arch_timer_enable(true);

  /* Now safe to enable the IRQ line in GIC */

  up_enable_irq(ARM64_ARCH_TIMER_IRQ);
}

/****************************************************************************
 * Private Data
 ****************************************************************************/

static const struct oneshot_operations_s g_arm64_oneshot_ops =
{
  .current        = arm64_oneshot_current,
  .start          = arm64_oneshot_start,
  .start_absolute = arm64_oneshot_start_absolute,
  .cancel         = arm64_oneshot_cancel,
  .max_delay      = arm64_oneshot_max_delay
};

static struct oneshot_lowerhalf_s g_arm64_oneshot_lowerhalf =
{
  .ops = &g_arm64_oneshot_ops
};

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: oneshot_initialize
 *
 * Description:
 *   Initialize the oneshot timer and return a oneshot lower half driver
 *   instance.
 *
 * Returned Value:
 *   On success, a non-NULL instance of the oneshot lower-half driver is
 *   returned.  NULL is return on any failure.
 *
 ****************************************************************************/

struct oneshot_lowerhalf_s *arm64_oneshot_initialize(void)
{
  struct oneshot_lowerhalf_s *priv = &g_arm64_oneshot_lowerhalf;
  uint64_t freq;

  tmrinfo("oneshot_initialize\n");

  /* Attach handler */

  irq_attach(ARM64_ARCH_TIMER_IRQ, arm64_arch_timer_compare_isr, priv);

  arm64_oneshot_initialize_per_cpu();

  freq = arm64_arch_timer_get_cntfrq();

  DEBUGASSERT(freq <= UINT32_MAX);

  oneshot_count_init(priv, (uint32_t)freq);

  /* Now that oneshot is fully initialized, unmask the timer interrupt.
   * This must happen after oneshot_count_init so the callback is ready.
   */

  arm64_arch_timer_set_irq_mask(false);

  tmrinfo("oneshot_initialize ok %p \n", priv);

  return priv;
}

void up_timer_initialize(void)
{
  up_alarm_set_lowerhalf(arm64_oneshot_initialize());
}

void arm64_timer_secondary_init(void)
{
  arm64_oneshot_initialize_per_cpu();
  arm64_arch_timer_set_irq_mask(false);
}
