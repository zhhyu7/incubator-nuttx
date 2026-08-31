/****************************************************************************
 * sched/group/group_leave.c
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

#include <sched.h>
#include <assert.h>
#include <errno.h>
#include <debug.h>

#include <nuttx/nuttx.h>
#include <nuttx/irq.h>
#include <nuttx/fs/fs.h>
#include <nuttx/net/net.h>
#include <nuttx/sched.h>
#include <nuttx/spinlock.h>
#include <nuttx/mutex.h>
#include <nuttx/environ.h>

#ifdef CONFIG_BINFMT_LOADABLE
#  include <nuttx/binfmt/binfmt.h>
#endif

#include "signal/signal.h"
#include "pthread/pthread.h"
#include "mqueue/mqueue.h"
#include "group/group.h"
#include "task/task.h"
#include "tls/tls.h"

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: group_release
 *
 * Description:
 *   Release group resources after the last member has left the group.
 *
 * Input Parameters:
 *   group - The group to be released.
 *
 * Returned Value:
 *   None.
 *
 * Assumptions:
 *   Called during task deletion in a safe context.  No special precautions
 *   are required here.
 *
 ****************************************************************************/

static inline void group_release(FAR struct task_group_s *group)
{
  /* Destroy the mutex */

  nxrmutex_destroy(&group->tg_mutex);

#if defined(CONFIG_SCHED_HAVE_PARENT) && defined(CONFIG_SCHED_CHILD_STATUS)
  /* Free all un-reaped child exit status */

  group_remove_children(group);
#endif

  /* Release pending signals */

  nxsig_release(group);

#ifndef CONFIG_DISABLE_PTHREAD
  /* Release pthread resources */

  pthread_release(group);
#endif

  /* Destroy the mm_map list */

  mm_map_destroy(&group->tg_mm_map);

  /* Free all file-related resources now.  We really need to close files as
   * soon as possible while we still have a functioning task.
   */

  /* Free resources held by the file descriptor list */

  fdlist_free(&group->tg_fdlist);

#ifdef CONFIG_BINFMT_LOADABLE
  /* If the exiting task was loaded into RAM from a file, then we need to
   * lease all of the memory resource when the last thread exits the task
   * group.
   */

  if (group->tg_bininfo != NULL)
    {
      binfmt_exit(group->tg_bininfo);
      group->tg_bininfo = NULL;
    }
#endif

  task_uninit_info(group);
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: group_leave
 *
 * Description:
 *   Release a reference on a group.  This function is called when a task or
 *   thread exits.  It decrements the reference count on the group.  If the
 *   reference count decrements to zero, then it frees the group and all of
 *   resources contained in the group.
 *
 * Input Parameters:
 *   tcb - The TCB of the task that is exiting.
 *
 * Returned Value:
 *   None.
 *
 * Assumptions:
 *   Called during task deletion in a safe context.  No special precautions
 *   are required here.
 *
 ****************************************************************************/

void group_leave(FAR struct tcb_s *tcb)
{
  FAR struct task_group_s *group;
#ifdef HAVE_GROUP_MEMBERS
  irqstate_t flags;
#endif

  DEBUGASSERT(tcb);

  /* Make sure that we have a group. */

  group = tcb->group;
  if (group)
    {
      bool release = !(atomic_read(&tcb->flags) & TCB_FLAG_TTYPE_KERNEL);

      /* Remove the member from group. */

#ifdef HAVE_GROUP_MEMBERS
      flags = spin_lock_irqsave(&group->tg_lock);
      sq_rem(&tcb->member, &group->tg_members);
      release = release && sq_empty(&group->tg_members);
      spin_unlock_irqrestore(&group->tg_lock, flags);

      /* Have all of the members left the group? */
#endif

      if (release)
        {
          /* Yes.. Release all of the resource held by the task group */

          group_release(group);
        }
    }
}

/****************************************************************************
 * Name: group_drop
 *
 * Description:
 *   Release the group's memory. This function is called whenever a reference
 *   to the group structure is released. It is not dependent on member count,
 *   but rather external references, which include:
 *   - Waiter list for waitpid()
 *
 * Input Parameters:
 *   group - The group that is to be dropped
 *
 * Returned Value:
 *   None.
 *
 * Assumptions:
 *   Called during task deletion or context switch in a safe context.  No
 *   special precautions are required here.
 *
 ****************************************************************************/

void group_drop(FAR struct task_group_s *group)
{
  /* Kernel-thread groups use a static global (g_kthread_group) and are
   * never ref-counted via group_join.  Skip the ref-count entirely to
   * avoid underflowing tg_refs; the static object must never be freed.
   */

  if (atomic_read(&group->tg_flags) & GROUP_FLAG_KERNEL)
    {
      return;
    }

  /* Free the group when tg_refs reaches zero.  Every group member holds one
   * reference (the main task's is taken in group_create, each pthread's in
   * group_join) and drops it here, so the count reaching zero means the last
   * member is gone and group_release() has already run.  atomic_sub()
   * returns the pre-decrement value, so == 1 means it just dropped to zero.
   */

  if (atomic_sub(&group->tg_refs, 1) == 1)
    {
#if defined(CONFIG_SCHED_WAITPID) && !defined(CONFIG_SCHED_HAVE_PARENT)
      /* Keep the group until pending waitpid() waiters have drained. */

      if (group->tg_nwaiters > 0)
        {
          sinfo("Keep group %p (waiters > 0)\n", group);
          return;
        }
#endif

#if !defined(CONFIG_BUILD_KERNEL) && !defined(CONFIG_ARCH_ADDRENV)

      /* Kernel build not use group_heap_initialize.
       * If use addrenv, it will uninitialized in up_addrenv_destroy.
       */

      group_heap_uninitialize(group->tg_heap);
#endif
#ifdef CONFIG_SIGNAL_FD
      /* Free the lazily-allocated signalfd poll waiter array, if any. */

      kmm_free(group->tg_sigfds);
#endif
      kmm_free(group);
    }
}
