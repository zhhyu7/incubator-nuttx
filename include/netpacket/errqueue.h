/****************************************************************************
 * include/netpacket/errqueue.h
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

#ifndef __INCLUDE_NETPACKET_ERRQUEUE_H
#define __INCLUDE_NETPACKET_ERRQUEUE_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <stdint.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define SO_EE_ORIGIN_NONE       0
#define SO_EE_ORIGIN_LOCAL      1
#define SO_EE_ORIGIN_ICMP       2
#define SO_EE_ORIGIN_ICMP6      3

/****************************************************************************
 * Public Type Definitions
 ****************************************************************************/

struct sock_extended_err
{
  uint32_t ee_errno;
  uint8_t  ee_origin;
  uint8_t  ee_type;
  uint8_t  ee_code;
  uint8_t  ee_pad;
  uint32_t ee_info;
  uint32_t ee_data;
};

#endif /* __INCLUDE_NETPACKET_ERRQUEUE_H */
