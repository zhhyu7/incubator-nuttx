/****************************************************************************
 * boards/arm/qemu-m/virt-m/include/board.h
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

#ifndef __BOARDS_ARM_QEMU_M_VIRT_M_INCLUDE_BOARD_H
#define __BOARDS_ARM_QEMU_M_VIRT_M_INCLUDE_BOARD_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* Clocking.  The QEMU virt-m machine wires the Armv7m container's main CPU
 * clock (cpuclk) at 25 MHz and the systick reference clock (refclk) at
 * 1 MHz (see the QEMU machine's SYSCLK/REFCLK).  SysTick counts the CPU
 * clock, so the SysTick input is 25 MHz.
 */

#define QEMU_M_SYSCLK_FREQUENCY  (25 * 1000 * 1000)
#define QEMU_M_SYSTICK_CLOCK     QEMU_M_SYSCLK_FREQUENCY

/****************************************************************************
 * Public Data
 ****************************************************************************/

#ifndef __ASSEMBLY__

#undef EXTERN
#if defined(__cplusplus)
#define EXTERN extern "C"
extern "C"
{
#else
#define EXTERN extern
#endif

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

#undef EXTERN
#if defined(__cplusplus)
}
#endif

#endif /* __ASSEMBLY__ */
#endif /* __BOARDS_ARM_QEMU_M_VIRT_M_INCLUDE_BOARD_H */
