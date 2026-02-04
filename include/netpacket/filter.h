/****************************************************************************
 * include/netpacket/filter.h
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

#ifndef __INCLUDE_NETPACKET_FILTER_H
#define __INCLUDE_NETPACKET_FILTER_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/compiler.h>

#include <stdint.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* BPF codes */

#define BPF_LD                           0x00
#define BPF_LDX                          0x01
#define BPF_ST                           0x02
#define BPF_STX                          0x03
#define BPF_ALU                          0x04
#define BPF_JMP                          0x05
#define BPF_RET                          0x06
#define BPF_MISC                         0x07

#define BPF_W                            0x00
#define BPF_H                            0x08
#define BPF_B                            0x10

#define BPF_IMM                          0x00
#define BPF_ABS                          0x20
#define BPF_IND                          0x40
#define BPF_MEM                          0x60
#define BPF_LEN                          0x80
#define BPF_MSH                          0xa0

#define BPF_ADD                          0x00
#define BPF_SUB                          0x10
#define BPF_MUL                          0x20
#define BPF_DIV                          0x30
#define BPF_OR                           0x40
#define BPF_AND                          0x50
#define BPF_LSH                          0x60
#define BPF_RSH                          0x70
#define BPF_NEG                          0x80
#define BPF_MOD                          0x90
#define BPF_XOR                          0xa0

#define BPF_JA                           0x00
#define BPF_JEQ                          0x10
#define BPF_JGT                          0x20
#define BPF_JGE                          0x30
#define BPF_JSET                         0x40

#define BPF_K                            0x00
#define BPF_X                            0x08

#define BPF_MAXINSNS                     4096

#define BPF_CLASS(code)                  ((code) & 0x07)
#define BPF_SIZE(code)                   ((code) & 0x18)
#define BPF_MODE(code)                   ((code) & 0xe0)
#define BPF_OP(code)                     ((code) & 0xf0)

#define BPF_STMT(code, k)                { (unsigned short)(code), 0, 0, k }
#define BPF_JUMP(code, k, jt, jf)        { (unsigned short)(code), jt, jf, k }

/* Misc */

#define BPF_TAX                          0x00
#define BPF_TXA                          0x80

/****************************************************************************
 * Public Type Definitions
 ****************************************************************************/

/* BPF instruction format */

struct sock_filter
{
  uint16_t code;
  uint8_t  jt;
  uint8_t  jf;
  uint32_t k;
};

struct sock_fprog
{
  unsigned short len;
  FAR struct sock_filter *filter;
};

#endif /* __INCLUDE_NETPACKET_FILTER_H */
