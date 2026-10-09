/****************************************************************************
 * boards/arm/qemu-m/virt-m/scripts/nuttx.ld
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

#include "memory.ld"

OUTPUT_ARCH(arm)
EXTERN(_vectors)
ENTRY(__start)

SECTIONS
{
  .text : {
      _stext = ABSOLUTE(.);
      /* The M-profile vector table must be first: the QEMU machine points
       * each core's VTOR at the image base (0x21000000, see
       * VIRT_M_BOOT_VECTOR_BASE in the QEMU virt-m machine).
       */
      KEEP(*(.vectors))
      *(.text .text.*)
      *(.fixup)
      *(.gnu.warning)
      *(.rodata .rodata.*)
      *(.gnu.linkonce.t.*)
      *(.glue_7)
      *(.glue_7t)
      *(.gcc_except_table)
      *(.gnu.linkonce.r.*)
      _etext = ABSOLUTE(.);
  } > ram

  .init_section : ALIGN(4) {
      _sinit = ABSOLUTE(.);
      KEEP(*(SORT_BY_INIT_PRIORITY(.init_array.*) SORT_BY_INIT_PRIORITY(.ctors.*)))
      KEEP(*(.init_array EXCLUDE_FILE(*crtbegin.o *crtbegin?.o *crtend.o *crtend?.o) .ctors))
      _einit = ABSOLUTE(.);
  } > ram

  .ARM.extab : ALIGN(4) {
      *(.ARM.extab*)
  } > ram

  .ARM.exidx : ALIGN(4) {
      __exidx_start = ABSOLUTE(.);
      *(.ARM.exidx*)
      __exidx_end = ABSOLUTE(.);
  } > ram

  .tdata : {
      _stdata = ABSOLUTE(.);
      *(.tdata .tdata.* .gnu.linkonce.td.*);
      _etdata = ABSOLUTE(.);
  } > ram

  .tbss : {
      _stbss = ABSOLUTE(.);
      *(.tbss .tbss.* .gnu.linkonce.tb.* .tcommon);
      _etbss = ABSOLUTE(.);
  } > ram

#ifdef CONFIG_PERCPU_SECTION
  /* Reserved for BMP (KR5 task 5). Harmless in the single-core UP build:
   * with CONFIG_NCPUS == 1 the reserved region has zero size.
   */

  . = ALIGN(64);
  _ldata_percpu = LOADADDR(.percpu_data);
  .percpu_data : {
      _sdata_percpu = ABSOLUTE(.);
      *(.data..percpu)
      . = ALIGN(8);
      _edata_percpu = ABSOLUTE(.);
  } > ram AT > ram

  .percpu_bss : {
      _sbss_percpu = ABSOLUTE(.);
      *(.bss..percpu)
      . = ALIGN(64);
      _ebss_percpu = ABSOLUTE(.);
  } > ram

  _percpu_size = _ebss_percpu - _sdata_percpu;
  _percpu_offset = _percpu_size;

  .percpu_reserved : {
      . = . + _percpu_size * (CONFIG_NCPUS - 1);
  } > ram
#endif

  _eronly = LOADADDR(.data);

  .data : ALIGN(4) {
      _sdata = ABSOLUTE(.);
      *(.data .data.*)
      *(.gnu.linkonce.d.*)
      CONSTRUCTORS
      . = ALIGN(4);
      _edata = ABSOLUTE(.);
  } > ram AT > ram

  .bss : ALIGN(4) {
      _sbss = ABSOLUTE(.);
      *(.bss .bss.*)
      *(.gnu.linkonce.b.*)
      *(COMMON)
      . = ALIGN(4);
      _ebss = ABSOLUTE(.);
  } > ram

  /* Stabs debugging sections. */

  .stab 0 : { *(.stab) }
  .stabstr 0 : { *(.stabstr) }
  .stab.excl 0 : { *(.stab.excl) }
  .stab.exclstr 0 : { *(.stab.exclstr) }
  .stab.index 0 : { *(.stab.index) }
  .stab.indexstr 0 : { *(.stab.indexstr) }
  .comment 0 : { *(.comment) }
  .debug_abbrev 0 : { *(.debug_abbrev) }
  .debug_info 0 : { *(.debug_info) }
  .debug_line 0 : { *(.debug_line) }
  .debug_pubnames 0 : { *(.debug_pubnames) }
  .debug_aranges 0 : { *(.debug_aranges) }
}
