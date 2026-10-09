/****************************************************************************
 * boards/arm/qemu-m/virt-m/scripts/memory.ld
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

#include <nuttx/config.h>

/* virt-m loads the single ELF into DRAM and runs from there (no XIP, no
 * pflash boot).  The QEMU machine sets each core's VTOR to
 * VIRT_M_BOOT_VECTOR_BASE = 0x41000000, so the image (vector table first)
 * must start there.  The 16 MiB below 0x41000000 is the DTB reservation;
 * the top 2 MiB (0x47e00000..0x48000000) is the RPTUN carveout and must
 * not be part of the kernel image/heap (design §2.3/§3.2).
 *
 * A single DRAM region is used for both "flash" (load) and "sram" (run):
 * text/rodata/data/bss are laid out contiguously, so LOADADDR(.data)
 * equals its run address and the .data copy in _start is a no-op.
 */

#define QEMU_M_RAM_START   0x21000000
#define QEMU_M_RAM_END     0x27e00000               /* RPTUN carveout base */
#define QEMU_M_RAM_SIZE    (QEMU_M_RAM_END - QEMU_M_RAM_START)

MEMORY
{
  ram (rwx) : ORIGIN = QEMU_M_RAM_START, LENGTH = QEMU_M_RAM_SIZE
}
