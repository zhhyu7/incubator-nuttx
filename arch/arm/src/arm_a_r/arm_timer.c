/****************************************************************************
 * arch/arm/src/arm_a_r/arm_timer.c
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
#include <nuttx/bits.h>
#include <nuttx/irq.h>
#include <nuttx/kmalloc.h>

#include <arch/barriers.h>
#include <arch/irq.h>

#include <sys/param.h>

#include "arm_timer.h"
#include "gic.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define CNT_CTL_ENABLE_BIT      0
#define CNT_CTL_IMASK_BIT       1
#define CNT_CTL_ISTATUS_BIT     2

/* AArch32 CNTP_* is banked per security state only when EL3 can execute
 * AArch32 (FEAT_AA32EL3).  Under an external AArch64 EL3 a single instance
 * remains, mapped to CNTP_CTL_EL0, which is the EL1 physical timer the
 * normal world owns, and the secure physical timer moves to CNTPS_*_EL1,
 * which AArch32 cannot encode.  A secure payload there has to use the
 * virtual timer, so ARM_HAVE_EL3_MONITOR gates this rather than the
 * security state alone.  The interrupt and the registers are selected
 * together because the count and the compare must share a timebase.
 *
 * https://support.arm.com/documentation/ddi0601/2026-09/AArch32-Registers
 */

#if defined(CONFIG_ARCH_TRUSTZONE_SECURE) && \
    defined(CONFIG_ARM_HAVE_EL3_MONITOR)
#  define ARM_TIMER_IRQ         GIC_IRQ_VTM
#  define ARM_TIMER_COUNT       CNTVCT
#  define ARM_TIMER_TVAL        CNTV_TVAL
#  define ARM_TIMER_CVAL        CNTV_CVAL
#  define ARM_TIMER_CTL         CNTV_CTL
#else
#  if defined(CONFIG_ARCH_TRUSTZONE_SECURE) || \
      defined(CONFIG_ARCH_TRUSTZONE_DISABLED)
#    define ARM_TIMER_IRQ       GIC_IRQ_STM
#  else
#    define ARM_TIMER_IRQ       GIC_IRQ_PTM
#  endif
#  define ARM_TIMER_COUNT       CNTPCT
#  define ARM_TIMER_TVAL        CNTP_TVAL
#  define ARM_TIMER_CVAL        CNTP_CVAL
#  define ARM_TIMER_CTL         CNTP_CTL
#endif

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static inline void arm_timer_set_freq(uint32_t freq)
{
  CP15_SET(CNTFRQ, freq);
  UP_ISB();
}

static inline uint64_t arm_timer_phy_count(void)
{
  return CP15_GET64(ARM_TIMER_COUNT);
}

static inline void arm_timer_phy_set_relative(uint32_t tval)
{
  CP15_SET(ARM_TIMER_TVAL, tval);
}

static inline void arm_timer_phy_set_absolute(uint64_t cval)
{
  CP15_SET64(ARM_TIMER_CVAL, cval);
}

static inline void arm_timer_phy_enable(bool enable)
{
  CP15_MODIFY((uint32_t)enable << CNT_CTL_ENABLE_BIT,
              BIT(CNT_CTL_ENABLE_BIT), ARM_TIMER_CTL);
  UP_ISB();
}

static inline void arm_timer_phy_set_irq_mask(bool mask)
{
  CP15_MODIFY((uint32_t)mask << CNT_CTL_IMASK_BIT,
              BIT(CNT_CTL_IMASK_BIT), ARM_TIMER_CTL);
  UP_ISB();
}

static int arm_timer_interrupt(int irq, void *regs, void *arg)
{
  struct oneshot_lowerhalf_s *priv = (struct oneshot_lowerhalf_s *)arg;

  arm_timer_phy_set_absolute(UINT64_MAX);

  oneshot_process_callback(priv);

  return OK;
}

static clkcnt_t arm_oneshot_max_delay(struct oneshot_lowerhalf_s *lower)
{
  return UINT64_MAX;
}

static clkcnt_t arm_oneshot_current(struct oneshot_lowerhalf_s *lower)
{
  /* We do not need memory barrier here. */

  return arm_timer_phy_count();
}

static void arm_oneshot_start_absolute(struct oneshot_lowerhalf_s *lower,
                                       clkcnt_t expected)
{
  arm_timer_phy_set_absolute(expected);
}

static void arm_oneshot_start(struct oneshot_lowerhalf_s *lower,
                              clkcnt_t delta)
{
  arm_timer_phy_set_relative(MIN(UINT32_MAX, delta));
}

static void arm_oneshot_cancel(struct oneshot_lowerhalf_s *lower)
{
  arm_timer_phy_set_absolute(UINT64_MAX);
}

static void arm_timer_initialize_per_cpu(unsigned int freq)
{
  /* In secure world, mark the non-secure timer IRQ as non-secure
   * so that normal world can receive it.  PPI security group is
   * per-CPU, so this must run on every core.
   */

#ifdef CONFIG_ARCH_TRUSTZONE_SECURE
  up_secure_irq(GIC_IRQ_PTM, false);
#endif

  /* Enable timer */

  if (freq)
    {
      arm_timer_set_freq(freq);
    }

  arm_timer_phy_set_absolute(UINT64_MAX);
  arm_timer_phy_enable(true);
  arm_timer_phy_set_irq_mask(false);

  up_enable_irq(ARM_TIMER_IRQ);
}

/****************************************************************************
 * Private Data
 ****************************************************************************/

static const struct oneshot_operations_s g_arm_oneshot_ops =
{
  .current        = arm_oneshot_current,
  .start          = arm_oneshot_start,
  .start_absolute = arm_oneshot_start_absolute,
  .cancel         = arm_oneshot_cancel,
  .max_delay      = arm_oneshot_max_delay,
};

static DEFINE_PER_CPU_BMP(struct oneshot_lowerhalf_s,
                          g_arm_oneshot_lowerhalf) =
{
  .ops = &g_arm_oneshot_ops
};
#define g_arm_oneshot_lowerhalf this_cpu_var_bmp(g_arm_oneshot_lowerhalf)

/****************************************************************************
 * Public Functions
 ****************************************************************************/

struct oneshot_lowerhalf_s *arm_timer_initialize(unsigned int freq)
{
  struct oneshot_lowerhalf_s *lower = &g_arm_oneshot_lowerhalf;

  /* The init freq is for trust-zone only since CNTFRQ is only
   * allowed to access in secure state.
   */

  arm_timer_initialize_per_cpu(freq);

  oneshot_count_init(lower, arm_timer_get_freq());

  irq_attach(ARM_TIMER_IRQ, arm_timer_interrupt, lower);

  return lower;
}

void arm_timer_secondary_init(unsigned int freq)
{
  arm_timer_initialize_per_cpu(freq);
}
