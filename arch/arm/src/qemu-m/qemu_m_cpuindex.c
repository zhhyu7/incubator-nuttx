/****************************************************************************
 * arch/arm/src/qemu-m/qemu_m_cpuindex.c
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
#include <stdint.h>

#include <nuttx/arch.h>

#include "arm_internal.h"
#include "qemu_m_internal.h"

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: up_cpu_index
 *
 * Description:
 *   Return the real core number.  The virt-m machine maps a per-core
 *   read-only CORE_ID register at the same address inside every core's
 *   view (QEMU_M_CORE_ID_INDEX), returning that core's linear index.  This
 *   is the single source of core identity for percpu, MHU and RPTUN.
 *
 ****************************************************************************/

#ifdef CONFIG_ARCH_HAVE_MULTICPU
int up_cpu_index(void)
{
  return (int)getreg32(QEMU_M_CORE_ID_INDEX);
}
#endif /* CONFIG_ARCH_HAVE_MULTICPU */
