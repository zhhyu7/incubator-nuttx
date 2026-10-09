/****************************************************************************
 * arch/arm/src/qemu-m/qemu_m_internal.h
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

#ifndef __ARCH_ARM_SRC_QEMU_M_QEMU_M_INTERNAL_H
#define __ARCH_ARM_SRC_QEMU_M_QEMU_M_INTERNAL_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* Memory map (single source of truth mirrored from the QEMU machine's
 * include/hw/arm/virt-m.h; design §2.3/§3.2).
 */

/* PL011 UART0 console */

#define QEMU_M_UART0_BASE      0x09000000

/* Per-core CORE_ID identity register block, mapped at the same address in
 * every core's view but returning that core's index / the core count.
 */

#define QEMU_M_CORE_ID_BASE    0xe0100000
#define QEMU_M_CORE_ID_INDEX   (QEMU_M_CORE_ID_BASE + 0x000) /* RO core idx */
#define QEMU_M_CORE_ID_NCPUS   (QEMU_M_CORE_ID_BASE + 0x004) /* RO N        */

/* DRAM / RPTUN carveout */

#define QEMU_M_RAM_BASE        0x20000000
#define QEMU_M_RPTUN_BASE      0x27e00000  /* heap must stay below this */

#endif /* __ARCH_ARM_SRC_QEMU_M_QEMU_M_INTERNAL_H */
