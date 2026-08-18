/****************************************************************************
 * net/ipfilter/ipfilter_pool.c
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

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <sys/param.h>
#include <debug.h>

#include <nuttx/mm/mempool.h>
#include <nuttx/percpu.h>

#include "ipfilter/ipfilter.h"
#include "ipfilter_pool.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#ifndef CONFIG_NET_IPFILTER_POOL_MAX
#  define CONFIG_NET_IPFILTER_POOL_MAX 0
#endif

#ifdef CONFIG_NET_IPv6
#  define IPFILTER_POOL_BLOCK_SIZE sizeof(struct ipv6_filter_entry_s)
#else
#  define IPFILTER_POOL_BLOCK_SIZE sizeof(struct ipv4_filter_entry_s)
#endif

#define IPFILTER_POOL_DEFINE(name, count, ...)                         \
  MEMPOOL_DEFINE_WITH_THROTTLE_ALIGN_SECTION(name, NULL,               \
                                             IPFILTER_POOL_BLOCK_SIZE, \
                                             count,                    \
                                             CONFIG_NET_IPFILTER_POOL_MAX, \
                                             CONFIG_NET_IPFILTER_POOL_ALLOC, \
                                             0, MM_ALIGN, NULL, NULL,  \
                                             ##__VA_ARGS__)

/****************************************************************************
 * Private Data
 ****************************************************************************/

#ifdef CONFIG_BMP

#if defined(CONFIG_NET_IPFILTER_POOL_PREALLOC_CPU0) && \
    CONFIG_NET_IPFILTER_POOL_PREALLOC_CPU0 > 0
#  ifdef IPFILTER_POOL_SECTION_CPU0
IPFILTER_POOL_DEFINE(g_ipfilter_pool_cpu0,
                     CONFIG_NET_IPFILTER_POOL_PREALLOC_CPU0,
                     IPFILTER_POOL_SECTION_CPU0);
#  else
IPFILTER_POOL_DEFINE(g_ipfilter_pool_cpu0,
                     CONFIG_NET_IPFILTER_POOL_PREALLOC_CPU0);
#  endif
#endif

#if defined(CONFIG_NET_IPFILTER_POOL_PREALLOC_CPU1) && \
    CONFIG_NET_IPFILTER_POOL_PREALLOC_CPU1 > 0
#  ifdef IPFILTER_POOL_SECTION_CPU1
IPFILTER_POOL_DEFINE(g_ipfilter_pool_cpu1,
                     CONFIG_NET_IPFILTER_POOL_PREALLOC_CPU1,
                     IPFILTER_POOL_SECTION_CPU1);
#  else
IPFILTER_POOL_DEFINE(g_ipfilter_pool_cpu1,
                     CONFIG_NET_IPFILTER_POOL_PREALLOC_CPU1);
#  endif
#endif

#if defined(CONFIG_NET_IPFILTER_POOL_PREALLOC_CPU2) && \
    CONFIG_NET_IPFILTER_POOL_PREALLOC_CPU2 > 0
#  ifdef IPFILTER_POOL_SECTION_CPU2
IPFILTER_POOL_DEFINE(g_ipfilter_pool_cpu2,
                     CONFIG_NET_IPFILTER_POOL_PREALLOC_CPU2,
                     IPFILTER_POOL_SECTION_CPU2);
#  else
IPFILTER_POOL_DEFINE(g_ipfilter_pool_cpu2,
                     CONFIG_NET_IPFILTER_POOL_PREALLOC_CPU2);
#  endif
#endif

#if defined(CONFIG_NET_IPFILTER_POOL_PREALLOC_CPU3) && \
    CONFIG_NET_IPFILTER_POOL_PREALLOC_CPU3 > 0
#  ifdef IPFILTER_POOL_SECTION_CPU3
IPFILTER_POOL_DEFINE(g_ipfilter_pool_cpu3,
                     CONFIG_NET_IPFILTER_POOL_PREALLOC_CPU3,
                     IPFILTER_POOL_SECTION_CPU3);
#  else
IPFILTER_POOL_DEFINE(g_ipfilter_pool_cpu3,
                     CONFIG_NET_IPFILTER_POOL_PREALLOC_CPU3);
#  endif
#endif

#if defined(CONFIG_NET_IPFILTER_POOL_PREALLOC_CPU4) && \
    CONFIG_NET_IPFILTER_POOL_PREALLOC_CPU4 > 0
#  ifdef IPFILTER_POOL_SECTION_CPU4
IPFILTER_POOL_DEFINE(g_ipfilter_pool_cpu4,
                     CONFIG_NET_IPFILTER_POOL_PREALLOC_CPU4,
                     IPFILTER_POOL_SECTION_CPU4);
#  else
IPFILTER_POOL_DEFINE(g_ipfilter_pool_cpu4,
                     CONFIG_NET_IPFILTER_POOL_PREALLOC_CPU4);
#  endif
#endif

#if defined(CONFIG_NET_IPFILTER_POOL_PREALLOC_CPU5) && \
    CONFIG_NET_IPFILTER_POOL_PREALLOC_CPU5 > 0
#  ifdef IPFILTER_POOL_SECTION_CPU5
IPFILTER_POOL_DEFINE(g_ipfilter_pool_cpu5,
                     CONFIG_NET_IPFILTER_POOL_PREALLOC_CPU5,
                     IPFILTER_POOL_SECTION_CPU5);
#  else
IPFILTER_POOL_DEFINE(g_ipfilter_pool_cpu5,
                     CONFIG_NET_IPFILTER_POOL_PREALLOC_CPU5);
#  endif
#endif

static FAR struct mempool_s *const g_ipfilter_pools[CONFIG_NCPUS] =
{
#if defined(CONFIG_NET_IPFILTER_POOL_PREALLOC_CPU0) && \
    CONFIG_NET_IPFILTER_POOL_PREALLOC_CPU0 > 0
  [0] = &g_ipfilter_pool_cpu0,
#endif
#if defined(CONFIG_NET_IPFILTER_POOL_PREALLOC_CPU1) && \
    CONFIG_NET_IPFILTER_POOL_PREALLOC_CPU1 > 0
  [1] = &g_ipfilter_pool_cpu1,
#endif
#if defined(CONFIG_NET_IPFILTER_POOL_PREALLOC_CPU2) && \
    CONFIG_NET_IPFILTER_POOL_PREALLOC_CPU2 > 0
  [2] = &g_ipfilter_pool_cpu2,
#endif
#if defined(CONFIG_NET_IPFILTER_POOL_PREALLOC_CPU3) && \
    CONFIG_NET_IPFILTER_POOL_PREALLOC_CPU3 > 0
  [3] = &g_ipfilter_pool_cpu3,
#endif
#if defined(CONFIG_NET_IPFILTER_POOL_PREALLOC_CPU4) && \
    CONFIG_NET_IPFILTER_POOL_PREALLOC_CPU4 > 0
  [4] = &g_ipfilter_pool_cpu4,
#endif
#if defined(CONFIG_NET_IPFILTER_POOL_PREALLOC_CPU5) && \
    CONFIG_NET_IPFILTER_POOL_PREALLOC_CPU5 > 0
  [5] = &g_ipfilter_pool_cpu5,
#endif
};

#else /* !CONFIG_BMP */

#if CONFIG_NET_IPFILTER_POOL_PREALLOC > 0
#  ifdef IPFILTER_POOL_SECTION
IPFILTER_POOL_DEFINE(g_ipfilter_pool,
                     CONFIG_NET_IPFILTER_POOL_PREALLOC,
                     IPFILTER_POOL_SECTION);
#  else
IPFILTER_POOL_DEFINE(g_ipfilter_pool,
                     CONFIG_NET_IPFILTER_POOL_PREALLOC);
#  endif
#endif

#endif /* CONFIG_BMP */

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static FAR struct mempool_s *ipfilter_pool_current(void)
{
#ifdef CONFIG_BMP
  unsigned int cpu = this_cpu();

  DEBUGASSERT(cpu < nitems(g_ipfilter_pools));
  return cpu < nitems(g_ipfilter_pools) ? g_ipfilter_pools[cpu] : NULL;
#else
#  if CONFIG_NET_IPFILTER_POOL_PREALLOC > 0
  return &g_ipfilter_pool;
#  else
  return NULL;
#  endif
#endif
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

FAR struct ipfilter_entry_s *ipfilter_pool_alloc(sa_family_t family)
{
  FAR struct ipfilter_entry_s *entry;
  FAR struct mempool_s *pool;

  switch (family)
    {
#ifdef CONFIG_NET_IPv4
      case PF_INET:
#endif
#ifdef CONFIG_NET_IPv6
      case PF_INET6:
#endif
        break;

      default:
        return NULL;
    }

  pool = ipfilter_pool_current();
  if (pool == NULL)
    {
      nerr("ERROR: IP filter pool is disabled on this CPU\n");
      return NULL;
    }

  entry = mempool_zallocate(pool, 0);
  if (entry == NULL)
    {
      nerr("ERROR: IP filter pool allocation failed\n");
      return NULL;
    }

  return entry;
}

void ipfilter_pool_free(FAR struct ipfilter_entry_s *entry)
{
  FAR struct mempool_s *pool = ipfilter_pool_current();

  DEBUGASSERT(pool != NULL);
  if (pool == NULL)
    {
      nerr("ERROR: IP filter pool is disabled on this CPU\n");
      return;
    }

  mempool_release(pool, entry);
}
