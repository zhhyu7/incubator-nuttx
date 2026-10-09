/****************************************************************************
 * arch/arm/include/qemu-m/irq.h
 *
 * QEMU virt-m (M-profile) public IRQ contract.
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

/* This file should never be included directly but, rather,
 * only indirectly through nuttx/irq.h
 */

#ifndef __ARCH_ARM_INCLUDE_QEMU_M_IRQ_H
#define __ARCH_ARM_INCLUDE_QEMU_M_IRQ_H

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* The QEMU virt-m NVIC provides 128 external interrupt lines (see the QEMU
 * machine's num-irq=128).  NuttX IRQ numbers add the 16 M-profile system
 * exception vectors, so NR_IRQS = 16 + 128 = 144.
 */

#define NR_IRQS                 144  /* Total number of interrupts */

#endif /* __ARCH_ARM_INCLUDE_QEMU_M_IRQ_H */
