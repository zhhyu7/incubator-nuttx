/****************************************************************************
 * arch/arm/src/qemu-m/qemu_m_allocateheap.c
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

#include <sys/types.h>
#include <stdint.h>

#include <nuttx/arch.h>
#include <nuttx/board.h>

#include "arm_internal.h"
#include "qemu_m_internal.h"

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: up_allocate_heap
 *
 * Description:
 *   The flat-build heap starts after the IDLE thread stack (g_idle_topstack)
 *   and extends up to, but not into, the RPTUN carveout at the top of DRAM
 *   (QEMU_M_RPTUN_BASE = 0x47e00000, design §2.3/§3.2).  The RPTUN region
 *   must stay reserved for inter-core rpmsg (KR5 task 5), so the heap must
 *   never overlap it.
 *
 ****************************************************************************/

void up_allocate_heap(void **heap_start, size_t *heap_size)
{
  *heap_start = (void *)g_idle_topstack;
  *heap_size  = (size_t)(QEMU_M_RPTUN_BASE - g_idle_topstack);
}

/****************************************************************************
 * Name: up_allocate_kheap
 ****************************************************************************/

#if defined(CONFIG_MM_KERNEL_HEAP)
void up_allocate_kheap(void **heap_start, size_t *heap_size)
{
  *heap_start = (void *)g_idle_topstack;
  *heap_size  = CONFIG_MM_KERNEL_HEAPSIZE;
}
#endif
