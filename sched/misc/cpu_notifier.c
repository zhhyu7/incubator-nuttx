/****************************************************************************
 * sched/misc/cpu_notifier.c
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

#include <nuttx/arch.h>
#include <nuttx/atomic.h>
#include <nuttx/cpu_notifier.h>
#include <nuttx/notifier.h>
#include <nuttx/percpu.h>
#include <nuttx/sched.h>
#include <nuttx/spinlock.h>

#include <sys/types.h>

/****************************************************************************
 * Private Types
 ****************************************************************************/

/* Carries one notifier block to a CPU that is already online, so the
 * callback runs there rather than on the registering CPU.
 */

struct cpu_notifier_arg_s
{
  FAR struct notifier_block *nb;
  unsigned long              action;
  FAR void                  *data;
};

/****************************************************************************
 * Private Data
 ****************************************************************************/

/* An atomic chain, not a blocking one: the notification is raised from the
 * secondary bring-up path, where the starting CPU still holds the scheduler
 * lock and cannot wait on a mutex.
 */

static struct atomic_notifier_head g_cpu_notifier_list =
               ATOMIC_NOTIFIER_INITVALUE;

/* The CPUs that have reported in, so a subscriber that registers late can
 * still be told about them.  CPU0 is online from the moment the OS runs and
 * never travels the secondary bring-up path, so it starts out set.
 */

static cpu_set_t g_cpu_notifier_online = 1;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static int cpu_notifier_call_one(FAR void *arg)
{
  FAR struct cpu_notifier_arg_s *a = arg;

  /* Call the block directly instead of walking the chain: the chain lock is
   * not held here, and the other subscribers have been told already.
   */

  return a->nb->notifier_call(a->nb, a->action, a->data);
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name:  register_cpu_notifier
 *
 * Description:
 *   Add notifier to the CPU notifier chain
 *
 * Input Parameters:
 *    nb - New entry in notifier chain
 *
 ****************************************************************************/

void register_cpu_notifier(FAR struct notifier_block *nb)
{
  struct cpu_notifier_arg_s arg;

  /* Join the chain before reading the online set, never the other way
   * round.  A CPU that comes online in between sets its own bit before it
   * runs the chain, so this order can only call the block twice, while the
   * reverse order could miss that CPU entirely.
   */

  atomic_notifier_chain_register(&g_cpu_notifier_list, nb);

  arg.nb     = nb;
  arg.action = CPU_ONLINE;
  arg.data   = NULL;

  /* Waiting here is safe precisely because every target is online: an
   * offline CPU would never acknowledge.
   */

  nxsched_cpu_call(g_cpu_notifier_online, cpu_notifier_call_one, &arg);
}

/****************************************************************************
 * Name:  unregister_cpu_notifier
 *
 * Description:
 *   Remove notifier from the CPU notifier chain
 *
 * Input Parameters:
 *    nb - Entry to remove from notifier chain
 *
 ****************************************************************************/

void unregister_cpu_notifier(FAR struct notifier_block *nb)
{
  atomic_notifier_chain_unregister(&g_cpu_notifier_list, nb);
}

/****************************************************************************
 * Name:  cpu_notifier_call_chain
 *
 * Description:
 *   Call functions in the CPU notifier chain.
 *
 * Input Parameters:
 *    action - Value passed unmodified to notifier function
 *    data   - Pointer passed unmodified to notifier function
 *
 ****************************************************************************/

void cpu_notifier_call_chain(unsigned long action, FAR void *data)
{
  if (action == CPU_ONLINE)
    {
      atomic_or(&g_cpu_notifier_online, 1 << this_cpu());
    }

  atomic_notifier_call_chain(&g_cpu_notifier_list, action, data);
}
