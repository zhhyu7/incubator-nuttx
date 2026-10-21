/****************************************************************************
 * arch/arm64/src/common/arm64_boot.c
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
#include <nuttx/init.h>

#include <arch/barriers.h>

#include "arm64_internal.h"
#include "arm64_arch.h"
#include "arm64_mpu.h"
#include "arm64_mmu.h"

#if defined(CONFIG_BMP) && defined(CONFIG_ARM64_PSCI)
#  include "arm64_cpu_psci.h"
#endif

/****************************************************************************
 * Public Data
 ****************************************************************************/

extern void *_vector_table[];

/****************************************************************************
 * Private Data
 ****************************************************************************/

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/* Some ARM aarch64 Cortex-family processors have not EL3
 * these two function should never called
 * defined to available compile error when with gcc option
 */

void arm64_boot_elx_init(void)
{
  /* Initialize percpu reg */

  zero_sysreg(tpidr_elx);

  /* Before we disable dcache, we should flush all dcache. */

  up_flush_dcache_all();

  /* Setup vector table */

  write_sysreg((uint64_t)_vector_table, vbar_elx);
  UP_ISB();

  write_sysreg(SCTLR_ELX_RES1
#ifndef CONFIG_ARM64_ICACHE_DISABLE
              | SCTLR_I_BIT
#endif
              | SCTLR_SA_BIT, sctlr_elx);

#if CONFIG_ARCH_ARM64_EXCEPTION_LEVEL == 3
  /* CPTR_EL3 reset value: do not trap system-register, FP/SIMD,
   * or lower-level CPTR/CPACR accesses during the EL2 handoff.
   */

  zero_sysreg(cptr_el3);

  write_sysreg(SCR_RES1 | SCR_IRQ_BIT |
               SCR_FIQ_BIT | SCR_EA_BIT, scr_el3);

#  if CONFIG_ARM64_GIC_VERSION > 2
  modify_sysreg(ICC_SRE_ELX_DFB_BIT |
                ICC_SRE_ELX_DIB_BIT |
                ICC_SRE_ELX_SRE_BIT |
                ICC_SRE_EL3_EN_BIT,
                ICC_SRE_ELX_DFB_BIT |
                ICC_SRE_ELX_DIB_BIT |
                ICC_SRE_ELX_SRE_BIT |
                ICC_SRE_EL3_EN_BIT,
                ICC_SRE_EL3);
#  endif

#elif CONFIG_ARCH_ARM64_EXCEPTION_LEVEL == 2

#  ifdef CONFIG_ARCH_CLUSTER_PMU
  modify_sysreg(ACTLR_CLPMU_BIT, ACTLR_CLPMU_BIT, actlr_el2);
#  endif

  /* Route IRQs to the EL2 vector table when NuttX stays at EL2. */

  modify_sysreg(HCR_IMO_BIT, HCR_IMO_BIT, hcr_el2);

#  ifdef CONFIG_ARCH_ARMV8R
  zero_sysreg(cnthps_ctl_el2);
#  else
  zero_sysreg(cnthp_ctl_el2);
#  endif

#  ifdef CONFIG_ARCH_SET_VMPIDR_EL2
  write_sysreg(read_sysreg(mpidr_el1), vmpidr_el2);
#  endif

  /* Enable this if/when we use the hypervisor timer.
   * write_cnthp_cval_el2(~(uint64_t)0);
   */

#else
  write_sysreg(CPACR_EL1_FPEN_NOTRAP, cpacr_el1);

  write_sysreg(~(uint64_t)0, cntv_cval_el0);
#endif

  UP_ISB();
}

void arm64_boot_elx_downgrade(uint64_t switch_addr)
{
  int el = arm64_current_el();

  if (el == MODE_EL2)
    {
#ifdef CONFIG_ARCH_CLUSTER_PMU
      modify_sysreg(ACTLR_CLPMU_BIT, ACTLR_CLPMU_BIT, actlr_el2);
#endif

#ifdef CONFIG_ARM64_MTE
      modify_sysreg(HCR_RW_BIT | HCR_ATA_BIT,
                    HCR_RW_BIT | HCR_ATA_BIT,
                    hcr_el2);
#else
      modify_sysreg(HCR_RW_BIT, HCR_RW_BIT, hcr_el2);
#endif

      /* Keep architectural RES1 bits set and leave EL1 SIMD/FP and
       * CPACR_EL1 accesses untrapped during the EL1 handoff.
       */

      write_sysreg(CPTR_EL2_RES1, cptr_el2);

      /* Enable EL1 access to timers */

      modify_sysreg(CNTHCTL_EL2_EL1PCEN_EN |
                    CNTHCTL_EL2_EL1PCTEN_EN,
                    CNTHCTL_EL2_EL1PCEN_EN |
                    CNTHCTL_EL2_EL1PCTEN_EN,
                    cnthctl_el2);

      zero_sysreg(cntvoff_el2);       /* Set 64-bit virtual timer offset to 0 */

#ifdef CONFIG_ARCH_ARMV8R
      zero_sysreg(cnthps_ctl_el2);
#else
      zero_sysreg(cnthp_ctl_el2);
#endif

#ifdef CONFIG_ARCH_SET_VMPIDR_EL2
      write_sysreg(read_sysreg(mpidr_el1), vmpidr_el2);
#endif

      write_sysreg(switch_addr, elr_el2);
      write_sysreg(SPSR_DAIF_MASK | SPSR_MODE_EL1T, spsr_el2);
    }
  else if (el == MODE_EL3)
    {
      /* CPTR_EL3 reset value: do not trap system-register, FP/SIMD,
       * or lower-level CPTR/CPACR accesses during the EL2 handoff.
       */

      zero_sysreg(cptr_el3);
      write_sysreg(SCR_NS_BIT | SCR_RES1 | SCR_RW_BIT | SCR_ST_BIT |
                   SCR_HCE_BIT | SCR_SMD_BIT, scr_el3);

#if CONFIG_ARM64_GIC_VERSION > 2
      modify_sysreg(ICC_SRE_ELX_DFB_BIT |
                    ICC_SRE_ELX_DIB_BIT |
                    ICC_SRE_ELX_SRE_BIT |
                    ICC_SRE_EL3_EN_BIT,
                    ICC_SRE_ELX_DFB_BIT |
                    ICC_SRE_ELX_DIB_BIT |
                    ICC_SRE_ELX_SRE_BIT |
                    ICC_SRE_EL3_EN_BIT,
                    ICC_SRE_EL3);
#endif

      write_sysreg(switch_addr, elr_el3);
      write_sysreg(SPSR_DAIF_MASK | SPSR_MODE_EL2T, spsr_el3);
    }
}

void arm64_boot_primary_c_routine(void)
{
#ifdef CONFIG_ARCH_HAVE_MPU
  arm64_mpu_init(true);
#endif

#ifdef CONFIG_ARCH_HAVE_MMU
  /* MAP IO and DRAM, enable MMU. */

  arm64_mmu_init(true);
#endif

#ifdef CONFIG_ARM64_MTE
  arm64_mte_init();
#endif

  up_enable_dcache();

#ifdef CONFIG_PERCPU_SECTION
  /* Copy the percpu data segment of cpu0 to the other CPUs. */

  for (int cpu = 1; cpu < CONFIG_NCPUS; cpu++)
    {
      memcpy((void *)((uintptr_t)_sdata_percpu + PERCPU_OFFSET * cpu),
             _ldata_percpu,
             (uintptr_t)_edata_percpu - (uintptr_t)_sdata_percpu);
    }
#endif

#ifdef CONFIG_ARM64_PSCI
#  if defined(CONFIG_ARCH_CHIP_QEMU_WITH_HV)
  arm64_psci_init("hvc");
#  elif CONFIG_ARCH_ARM64_EXCEPTION_LEVEL < 3
  arm64_psci_init("smc");
#  endif
#endif

#if defined(CONFIG_BMP) && defined(CONFIG_ARM64_PSCI)
  /* BMP on a PSCI platform: the secondaries are held in reset.  Once the
   * PSCI conduit, shared page tables and per-CPU data are ready, release
   * each secondary into the shared image entry (__start); they spin on
   * OSINIT until the primary reaches OSREADY.  Only the primary runs this
   * routine, so no this_cpu() guard is needed.  SMP starts secondaries
   * later via the scheduler; non-PSCI reset-release platforms self-release
   * and never compile this.
   */

  for (int cpu = 1; cpu < CONFIG_NCPUS; cpu++)
    {
      psci_cpu_on(arm64_get_mpid(cpu), (uintptr_t)__start);
    }
#endif

  arm64_chip_boot();

#ifdef USE_EARLYSERIALINIT
  /* Perform early serial initialization if we are going to use the serial
   * driver.
   */

  arm64_earlyserialinit();
#endif
  nx_start();
}
