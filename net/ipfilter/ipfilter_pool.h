/****************************************************************************
 * net/ipfilter/ipfilter_pool.h
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

#ifndef __NET_IPFILTER_IPFILTER_POOL_H
#define __NET_IPFILTER_IPFILTER_POOL_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <sys/socket.h>

#include <nuttx/compiler.h>

/****************************************************************************
 * Public Type Declarations
 ****************************************************************************/

struct ipfilter_entry_s;

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

FAR struct ipfilter_entry_s *ipfilter_pool_alloc(sa_family_t family);
void ipfilter_pool_free(FAR struct ipfilter_entry_s *entry);

#endif /* __NET_IPFILTER_IPFILTER_POOL_H */
