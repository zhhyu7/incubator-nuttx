/****************************************************************************
 * arch/arm/src/qemu-m/chip.h
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

#ifndef __ARCH_ARM_SRC_QEMU_M_CHIP_H
#define __ARCH_ARM_SRC_QEMU_M_CHIP_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>
#include <arch/qemu-m/irq.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define ARM_PERIPHERAL_INTERRUPTS NR_IRQS

/* The QEMU virt-m machine writes the generated device tree into guest RAM at
 * this fixed base (VIRT_M_DTB_BASE in the QEMU machine); it is registered in
 * the chip boot path (qemu_m_start.c) and consumed by the board FDT hooks.
 */

#define QEMU_M_DTB_BASE           0x20000000

/* NVIC external interrupts start at vector 16, so the guest IRQ number of a
 * device is its DTB "interrupts" line + 16.  This mirrors the A/R chip's
 * QEMU_SPI_IRQ_BASE (32 for the GIC); the fdt_* hooks are reused unchanged.
 */

#define QEMU_M_IRQ_BASE           16

#endif /* __ARCH_ARM_SRC_QEMU_M_CHIP_H */
