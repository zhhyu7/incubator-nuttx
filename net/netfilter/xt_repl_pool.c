/****************************************************************************
 * net/netfilter/xt_repl_pool.c
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

#include <errno.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>

#include <debug.h>
#include <sys/param.h>

#include <nuttx/kmalloc.h>
#include <nuttx/mm/mempool.h>
#include <nuttx/percpu.h>

#include "xt_repl_pool.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#if defined(CONFIG_NET_IPv4) && defined(CONFIG_NET_NAT)
#  define XT_REPL_IPV4_NAT_BLOCKS 1
#else
#  define XT_REPL_IPV4_NAT_BLOCKS 0
#endif

#if defined(CONFIG_NET_IPv4) && defined(CONFIG_NET_IPFILTER)
#  define XT_REPL_IPV4_FILTER_BLOCKS 1
#else
#  define XT_REPL_IPV4_FILTER_BLOCKS 0
#endif

#if defined(CONFIG_NET_IPv6) && defined(CONFIG_NET_IPFILTER)
#  define XT_REPL_IPV6_FILTER_BLOCKS 1
#else
#  define XT_REPL_IPV6_FILTER_BLOCKS 0
#endif

#define XT_REPL_NBLOCKS (XT_REPL_IPV4_NAT_BLOCKS + \
                         XT_REPL_IPV4_FILTER_BLOCKS + \
                         XT_REPL_IPV6_FILTER_BLOCKS)

#if XT_REPL_NBLOCKS == 0
#  undef XT_REPL_NBLOCKS
#  define XT_REPL_NBLOCKS 1
#endif

#define XT_REPL_HEADER_SIZE \
  ALIGN_UP(sizeof(struct xt_repl_block_s), MM_ALIGN)
#define XT_REPL_BLOCK_SIZE(size) (XT_REPL_HEADER_SIZE + (size))

#define XT_REPL_POOL_DEFINE(name, size, ...)                           \
  MEMPOOL_DEFINE_WITH_THROTTLE_ALIGN_SECTION(name, NULL,               \
                                             XT_REPL_BLOCK_SIZE(size), \
                                             XT_REPL_NBLOCKS,          \
                                             XT_REPL_NBLOCKS,          \
                                             0, 0, MM_ALIGN,           \
                                             NULL, NULL,               \
                                             ##__VA_ARGS__)

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct xt_repl_block_s
{
  FAR struct mempool_s *pool;
  size_t capacity;
};

/****************************************************************************
 * Private Data
 ****************************************************************************/

#ifdef CONFIG_BMP

#if defined(CONFIG_NET_IPTABLES_REPL_SIZE_CPU0) && \
    CONFIG_NET_IPTABLES_REPL_SIZE_CPU0 > 0
#  ifdef XT_REPL_SECTION_CPU0
XT_REPL_POOL_DEFINE(g_xt_repl_pool_cpu0,
                    CONFIG_NET_IPTABLES_REPL_SIZE_CPU0,
                    XT_REPL_SECTION_CPU0);
#  else
XT_REPL_POOL_DEFINE(g_xt_repl_pool_cpu0,
                    CONFIG_NET_IPTABLES_REPL_SIZE_CPU0);
#  endif
#endif

#if defined(CONFIG_NET_IPTABLES_REPL_SIZE_CPU1) && \
    CONFIG_NET_IPTABLES_REPL_SIZE_CPU1 > 0
#  ifdef XT_REPL_SECTION_CPU1
XT_REPL_POOL_DEFINE(g_xt_repl_pool_cpu1,
                    CONFIG_NET_IPTABLES_REPL_SIZE_CPU1,
                    XT_REPL_SECTION_CPU1);
#  else
XT_REPL_POOL_DEFINE(g_xt_repl_pool_cpu1,
                    CONFIG_NET_IPTABLES_REPL_SIZE_CPU1);
#  endif
#endif

#if defined(CONFIG_NET_IPTABLES_REPL_SIZE_CPU2) && \
    CONFIG_NET_IPTABLES_REPL_SIZE_CPU2 > 0
#  ifdef XT_REPL_SECTION_CPU2
XT_REPL_POOL_DEFINE(g_xt_repl_pool_cpu2,
                    CONFIG_NET_IPTABLES_REPL_SIZE_CPU2,
                    XT_REPL_SECTION_CPU2);
#  else
XT_REPL_POOL_DEFINE(g_xt_repl_pool_cpu2,
                    CONFIG_NET_IPTABLES_REPL_SIZE_CPU2);
#  endif
#endif

#if defined(CONFIG_NET_IPTABLES_REPL_SIZE_CPU3) && \
    CONFIG_NET_IPTABLES_REPL_SIZE_CPU3 > 0
#  ifdef XT_REPL_SECTION_CPU3
XT_REPL_POOL_DEFINE(g_xt_repl_pool_cpu3,
                    CONFIG_NET_IPTABLES_REPL_SIZE_CPU3,
                    XT_REPL_SECTION_CPU3);
#  else
XT_REPL_POOL_DEFINE(g_xt_repl_pool_cpu3,
                    CONFIG_NET_IPTABLES_REPL_SIZE_CPU3);
#  endif
#endif

#if defined(CONFIG_NET_IPTABLES_REPL_SIZE_CPU4) && \
    CONFIG_NET_IPTABLES_REPL_SIZE_CPU4 > 0
#  ifdef XT_REPL_SECTION_CPU4
XT_REPL_POOL_DEFINE(g_xt_repl_pool_cpu4,
                    CONFIG_NET_IPTABLES_REPL_SIZE_CPU4,
                    XT_REPL_SECTION_CPU4);
#  else
XT_REPL_POOL_DEFINE(g_xt_repl_pool_cpu4,
                    CONFIG_NET_IPTABLES_REPL_SIZE_CPU4);
#  endif
#endif

#if defined(CONFIG_NET_IPTABLES_REPL_SIZE_CPU5) && \
    CONFIG_NET_IPTABLES_REPL_SIZE_CPU5 > 0
#  ifdef XT_REPL_SECTION_CPU5
XT_REPL_POOL_DEFINE(g_xt_repl_pool_cpu5,
                    CONFIG_NET_IPTABLES_REPL_SIZE_CPU5,
                    XT_REPL_SECTION_CPU5);
#  else
XT_REPL_POOL_DEFINE(g_xt_repl_pool_cpu5,
                    CONFIG_NET_IPTABLES_REPL_SIZE_CPU5);
#  endif
#endif

static FAR struct mempool_s *const g_xt_repl_pools[CONFIG_NCPUS] =
{
#if defined(CONFIG_NET_IPTABLES_REPL_SIZE_CPU0) && \
    CONFIG_NET_IPTABLES_REPL_SIZE_CPU0 > 0
  [0] = &g_xt_repl_pool_cpu0,
#endif
#if defined(CONFIG_NET_IPTABLES_REPL_SIZE_CPU1) && \
    CONFIG_NET_IPTABLES_REPL_SIZE_CPU1 > 0
  [1] = &g_xt_repl_pool_cpu1,
#endif
#if defined(CONFIG_NET_IPTABLES_REPL_SIZE_CPU2) && \
    CONFIG_NET_IPTABLES_REPL_SIZE_CPU2 > 0
  [2] = &g_xt_repl_pool_cpu2,
#endif
#if defined(CONFIG_NET_IPTABLES_REPL_SIZE_CPU3) && \
    CONFIG_NET_IPTABLES_REPL_SIZE_CPU3 > 0
  [3] = &g_xt_repl_pool_cpu3,
#endif
#if defined(CONFIG_NET_IPTABLES_REPL_SIZE_CPU4) && \
    CONFIG_NET_IPTABLES_REPL_SIZE_CPU4 > 0
  [4] = &g_xt_repl_pool_cpu4,
#endif
#if defined(CONFIG_NET_IPTABLES_REPL_SIZE_CPU5) && \
    CONFIG_NET_IPTABLES_REPL_SIZE_CPU5 > 0
  [5] = &g_xt_repl_pool_cpu5,
#endif
};

#else /* !CONFIG_BMP */

#if CONFIG_NET_IPTABLES_REPL_SIZE > 0
#  ifdef XT_REPL_SECTION
XT_REPL_POOL_DEFINE(g_xt_repl_pool,
                    CONFIG_NET_IPTABLES_REPL_SIZE,
                    XT_REPL_SECTION);
#  else
XT_REPL_POOL_DEFINE(g_xt_repl_pool,
                    CONFIG_NET_IPTABLES_REPL_SIZE);
#  endif
#endif

#endif /* CONFIG_BMP */

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static FAR struct mempool_s *xt_repl_pool_current(void)
{
#ifdef CONFIG_BMP
  unsigned int cpu = this_cpu();

  DEBUGASSERT(cpu < nitems(g_xt_repl_pools));
  return cpu < nitems(g_xt_repl_pools) ? g_xt_repl_pools[cpu] : NULL;
#else
#  if CONFIG_NET_IPTABLES_REPL_SIZE > 0
  return &g_xt_repl_pool;
#  else
  return NULL;
#  endif
#endif
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

FAR void *xt_repl_pool_alloc(size_t size)
{
  FAR struct mempool_s *pool = xt_repl_pool_current();
  FAR struct xt_repl_block_s *block;
  FAR void *buffer;
  size_t capacity;

  if (pool != NULL)
    {
      capacity = pool->blocksize - XT_REPL_HEADER_SIZE;
      if (size <= capacity)
        {
          block = mempool_tryallocate(pool);
          if (block != NULL)
            {
              block->pool = pool;
              block->capacity = capacity;
              buffer = (FAR char *)block + XT_REPL_HEADER_SIZE;
              memset(buffer, 0, capacity);
              return buffer;
            }
        }
    }

  if (size > SIZE_MAX - XT_REPL_HEADER_SIZE)
    {
      nerr("ERROR: Iptables repl size %zu overflows allocation\n", size);
      return NULL;
    }

  block = kmm_zalloc(XT_REPL_HEADER_SIZE + size);
  if (block == NULL)
    {
      nerr("ERROR: Iptables repl heap allocation failed for %zu bytes\n",
           size);
      return NULL;
    }

  block->pool = NULL;
  block->capacity = size;
  return (FAR char *)block + XT_REPL_HEADER_SIZE;
}

void xt_repl_pool_free(FAR void *buffer)
{
  FAR struct xt_repl_block_s *block;

  if (buffer == NULL)
    {
      return;
    }

  block = (FAR struct xt_repl_block_s *)
          ((FAR char *)buffer - XT_REPL_HEADER_SIZE);

  if (block->pool != NULL)
    {
      mempool_release(block->pool, block);
    }
  else
    {
      kmm_free(block);
    }
}

size_t xt_repl_pool_capacity(FAR const void *buffer)
{
  FAR const struct xt_repl_block_s *block;

  if (buffer == NULL)
    {
      return 0;
    }

  block = (FAR const struct xt_repl_block_s *)
          ((FAR const char *)buffer - XT_REPL_HEADER_SIZE);
  return block->capacity;
}
