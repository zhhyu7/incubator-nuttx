/****************************************************************************
 * boards/arm/qemu-m/virt-m/src/qemu_m_bringup.c
 *
 * SPDX-License-Identifier: Apache-2.0
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

#include <sys/mount.h>
#include <syslog.h>

#include <nuttx/fs/fs.h>
#include <nuttx/fdt.h>

#include "arm_internal.h"
#include "chip.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* NVIC external interrupts start at vector 16, so the guest IRQ number of a
 * device is its DTB "interrupts" line + 16.  This mirrors the A/R board's
 * QEMU_SPI_IRQ_BASE (32 for the GIC); the fdt_* hooks are reused unchanged.
 * The real definition lives in the chip header; keep a fallback like A/R.
 */

#ifndef QEMU_M_IRQ_BASE
#  define QEMU_M_IRQ_BASE     16
#endif

/****************************************************************************
 * Private Functions
 ****************************************************************************/

#if defined(CONFIG_LIBC_FDT) && defined(CONFIG_DEVICE_TREE)

/****************************************************************************
 * Name: register_devices_from_fdt
 *
 * Description:
 *   Discover the fixed peripherals from the device tree registered during
 *   chip boot (qemu_m_start.c).  The peripherals are mounted by the QEMU
 *   virt-m board model and discovered here exactly like the A/R board does
 *   from hw/arm/virt.c; only the interrupt base differs (NVIC vs GIC).
 *
 ****************************************************************************/

static void register_devices_from_fdt(void)
{
  const void *fdt = fdt_get();
  int ret;

  if (fdt == NULL)
    {
      return;
    }

#ifdef CONFIG_IOEXPANDER_PL061
  ret = fdt_pl061_devices_register(fdt, QEMU_M_IRQ_BASE);
  if (ret < 0)
    {
      syslog(LOG_ERR, "fdt_pl061_devices_register failed, ret=%d\n", ret);
    }
#endif

#if defined(CONFIG_TIMER) && defined(CONFIG_TIMER_SP804)
  ret = fdt_sp804_devices_register(fdt, QEMU_M_IRQ_BASE);
  if (ret < 0)
    {
      syslog(LOG_ERR, "fdt_sp804_devices_register failed, ret=%d\n", ret);
    }
#endif

#ifdef CONFIG_WATCHDOG_CMSDK
  ret = fdt_cmsdk_wdt_devices_register(fdt, QEMU_M_IRQ_BASE);
  if (ret < 0)
    {
      syslog(LOG_ERR, "fdt_cmsdk_wdt_devices_register failed, ret=%d\n",
             ret);
    }
#endif

#ifdef CONFIG_RTC_PL031
  ret = fdt_pl031_devices_register(fdt, QEMU_M_IRQ_BASE);
  if (ret < 0)
    {
      syslog(LOG_ERR, "fdt_pl031_devices_register failed, ret=%d\n", ret);
    }
#endif

#ifdef CONFIG_ADC_NPCM7XX
  ret = fdt_npcm_adc_devices_register(fdt, QEMU_M_IRQ_BASE);
  if (ret < 0)
    {
      syslog(LOG_ERR, "fdt_npcm_adc_devices_register failed, ret=%d\n", ret);
    }
#endif

#ifdef CONFIG_PWM_SIFIVE
  ret = fdt_sifive_pwm_devices_register(fdt);
  if (ret < 0)
    {
      syslog(LOG_ERR, "fdt_sifive_pwm_devices_register failed, ret=%d\n",
             ret);
    }
#endif

  UNUSED(ret);
}

#endif /* CONFIG_LIBC_FDT && CONFIG_DEVICE_TREE */

/****************************************************************************
 * Name: qemu_m_bringup
 *
 * Description:
 *   Bring up board features.  On core0 this discovers the peripheral FDT
 *   hooks; it also mounts the basic pseudo file systems.
 *
 ****************************************************************************/

static int qemu_m_bringup(void)
{
  int ret = 0;

#if defined(CONFIG_LIBC_FDT) && defined(CONFIG_DEVICE_TREE)
  register_devices_from_fdt();
#endif

#ifdef CONFIG_FS_PROCFS
  ret = nx_mount(NULL, "/proc", "procfs", 0, NULL);
  if (ret < 0)
    {
      syslog(LOG_ERR, "ERROR: Failed to mount procfs at /proc: %d\n", ret);
    }
#endif

#ifdef CONFIG_FS_TMPFS
  ret = nx_mount(NULL, CONFIG_LIBC_TMPDIR, "tmpfs", 0, NULL);
  if (ret < 0)
    {
      syslog(LOG_ERR, "ERROR: Failed to mount tmpfs at %s: %d\n",
             CONFIG_LIBC_TMPDIR, ret);
    }
#endif

  return ret;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: board_late_initialize
 ****************************************************************************/

#ifdef CONFIG_BOARD_LATE_INITIALIZE
void board_late_initialize(void)
{
  qemu_m_bringup();
}
#endif

/****************************************************************************
 * Name: board_app_initialize
 ****************************************************************************/

int board_app_initialize(uintptr_t arg)
{
  UNUSED(arg);
#ifndef CONFIG_BOARD_LATE_INITIALIZE
  return qemu_m_bringup();
#else
  return OK;
#endif
}
