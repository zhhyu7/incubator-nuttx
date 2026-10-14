/****************************************************************************
 * sched/sched/sched_roundrobin.c
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
#include <nuttx/nuttx.h>

#include <stdint.h>
#include <stdbool.h>
#include <sched.h>
#include <sys/param.h>
#include <assert.h>

#include <nuttx/sched.h>
#include <nuttx/clock.h>
#include <nuttx/percpu.h>
#include <nuttx/hrtimer.h>

#include "sched/sched.h"
#include "clock/clock.h"

#ifdef CONFIG_SMP

/****************************************************************************
 * Private Type Declarations
 ****************************************************************************/

struct roundrobin_call_s
{
  struct cpu_call_data_s data;
  pid_t                  pid;
};

/****************************************************************************
 * Private Data
 ****************************************************************************/

static DEFINE_PER_CPU_SMP(struct roundrobin_call_s, g_call_data);

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static int nxsched_roundrobin_handler(FAR void *cookie)
{
  FAR struct roundrobin_call_s *call = cookie;
  irqstate_t        flags = enter_critical_section();
  pid_t             pid   = call->pid;
  FAR struct tcb_s *tcb   = nxsched_get_tcb(pid);

  /* Clear the pid to indicate the handler has completed. */

  DEBUGASSERT(pid != 0);

  call->pid = 0;

  if (pid != 0 && tcb && tcb->task_state != TSTATE_TASK_INVALID &&
      (atomic_read(&tcb->flags) & TCB_FLAG_EXIT_PROCESSING) == 0)
    {
      /* There is no TCB with this pid or, if there is, it is not a task. */

      if (tcb->task_state == TSTATE_TASK_RUNNING && tcb->cpu == this_cpu() &&
          nxsched_switch_running(tcb->cpu, true))
        {
          nxsched_switch(this_task(), tcb);
        }
    }

  nxsched_put_tcb(tcb);
  leave_critical_section(flags);
  return OK;
}
#endif

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name:  nxsched_process_roundrobin
 *
 * Description:
 *   Check if the currently executing task has exceeded its time slice.
 *
 * Input Parameters:
 *   tcb - The TCB of the currently executing task
 *   ticks - The number of ticks that have elapsed on the interval timer.
 *   noswitches - True: Can't do context switches now.
 *
 * Returned Value:
 *   None
 *
 * Assumptions:
 *   - Interrupts are disabled
 *   - The task associated with TCB uses the round robin scheduling
 *     policy
 *
 ****************************************************************************/

void nxsched_process_roundrobin(FAR struct tcb_s *tcb, clock_t ticks,
                                bool noswitches)
{
  FAR struct tcb_s *ptcb;  /* Pointer to the next ready-to-run TCB */
  int decr;

  /* How much can we decrement the timeslice delay?  If 'ticks' is greater
   * than the timeslice value, then we ignore any excess amount.
   *
   * 'ticks' should not be greater than the remaining timeslice.  But that
   * event seems to be possible, perhaps in cases where pre-emption has been
   * disabled or the noswitches flag is set.  This might cause jitter of a
   * few ticks in the slicing because the excess amount is not handled.
   */

  DEBUGASSERT(tcb != NULL);
  decr = MIN(tcb->timeslice, ticks);

  /* Decrement the timeslice counter */

  tcb->timeslice -= decr;

  /* Did decrementing the timeslice counter cause the timeslice to expire? */

  if (tcb->timeslice <= 0)
    {
      /* The timeslice expired but the reset (and the possible rotation)
       * cannot be performed if the scheduler is locked or the caller is
       * mid-switch.  The timeslice is left expired: sched_unlock() (or the
       * resume path) resets the timeslice and re-arms the roundrobin timer.
       */

      if (!nxsched_islocked_tcb(tcb) && !noswitches)
        {
          /* Reset the timeslice. */

          tcb->timeslice = MSEC2TICK(CONFIG_RR_INTERVAL);

          /* We know we are at the head of the ready to run prioritized
           * list.  We must be the highest priority task eligible for
           * execution.  Check the next task in the ready to run list.  If
           * it is the same priority, then we need to relinquish the CPU and
           * give that task a shot.
           */

#ifdef CONFIG_SMP
          ptcb = (FAR struct tcb_s *)dq_peek(list_readytorun());
#else
          ptcb = tcb->flink;
#endif
          if (ptcb &&
              ptcb->sched_priority >= tcb->sched_priority)
            {
              FAR struct tcb_s *rtcb = this_task();

              /* Just resetting the task priority to its current value.
               * This will cause the task to be rescheduled behind any
               * other tasks at the same priority.
               */

#ifdef CONFIG_SMP
              DEBUGASSERT(tcb->task_state == TSTATE_TASK_RUNNING);
              if (tcb->cpu != this_cpu())
                {
                  FAR struct roundrobin_call_s *call =
                    &per_cpu_var_smp(g_call_data, tcb->cpu);

                  if (call->pid == 0)
                    {
                      call->pid = tcb->pid;
                      nxsched_cpu_call_init(&call->data,
                                            nxsched_roundrobin_handler,
                                            call);
                      nxsched_cpu_call_single_async(tcb->cpu, &call->data);
                    }
                }
              else if (nxsched_switch_running(tcb->cpu, true))
#else
              if (nxsched_reprioritize_rtr(tcb, tcb->sched_priority))
#endif
                {
                  /* nxsched_switch() calls nxsched_suspend/resume_roundrobin
                   * which re-arm the roundrobin timer for the new task.
                   */

                  nxsched_switch(this_task(), rtcb);
                }
            }
        }
    }
}

#ifdef CONFIG_SCHED_TICKLESS

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct roundrobin_event_s
{
  hrtimer_t         timer;
  FAR struct tcb_s *tcb;
};

/****************************************************************************
 * Private Data
 ****************************************************************************/

static DEFINE_PER_CPU_BSS_SMP(struct roundrobin_event_s, g_roundrobin_event);
#define g_roundrobin_event this_cpu_var_smp(g_roundrobin_event)

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static uint64_t roundrobin_timer_expired(FAR const hrtimer_t *timer,
                                         uint64_t expired)
{
  FAR struct roundrobin_event_s *event =
    container_of(timer, struct roundrobin_event_s, timer);
  clock_t            now   = div_const(expired, NSEC_PER_TICK);
  irqstate_t         flags = enter_critical_section();
  FAR struct tcb_s  *tcb   = event->tcb;
  clock_t            delay = 0u;

  if (tcb)
    {
      clock_t elapsed = now - tcb->starttime;

      event->tcb      = NULL;
      tcb->starttime  = now;

      nxsched_process_roundrobin(tcb, elapsed, false);

      /* Re-arm the roundrobin timer if the timeslice is still valid and
       * this task is still the running task of this CPU.  Otherwise, a
       * context switch has already re-armed the timer for the new task
       * via nxsched_resume_roundrobin(), or the expired timeslice waits
       * for sched_unlock() (or the resume path) to reset it.
       */

      if (tcb->timeslice > 0 && tcb->task_state == TSTATE_TASK_RUNNING)
        {
          /* Re-arm the round-robin timer. */

          event->tcb = tcb;
          delay      = tcb->timeslice;
        }
    }

  leave_critical_section(flags);

  return TICK2NSEC((uint64_t)delay);
}

/****************************************************************************
 * Name:  nxsched_resume_roundrobin
 *
 * Description:
 *   Called when a task using round-robin scheduling is being switched in.
 *   Subtracts the elapsed suspension time from the task's timeslice so
 *   that only actual CPU execution time is counted.
 *
 * Input Parameters:
 *   tcb - The TCB of the thread that is being resumed.
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

void nxsched_resume_roundrobin(FAR struct tcb_s *tcb, clock_t now)
{
  DEBUGASSERT(tcb->timeslice >= 0);
  tcb->starttime = now;

  g_roundrobin_event.tcb = tcb;
  hrtimer_start(&g_roundrobin_event.timer, roundrobin_timer_expired,
                TICK2NSEC((uint64_t)now + tcb->timeslice),
                HRTIMER_MODE_ABS);
}

/****************************************************************************
 * Name:  nxsched_suspend_roundrobin
 *
 * Description:
 *   Called when a task using round-robin scheduling is being switched out.
 *   Records the current time so that the elapsed time can be subtracted
 *   from the timeslice when the task is resumed.
 *
 * Input Parameters:
 *   tcb - The TCB of the thread that is being suspended.
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

void nxsched_suspend_roundrobin(FAR struct tcb_s *tcb, clock_t now)
{
  clock_t elapsed = now - tcb->starttime;

  if (g_roundrobin_event.tcb != NULL)
    {
      g_roundrobin_event.tcb = NULL;
      hrtimer_cancel(&g_roundrobin_event.timer);
    }

  tcb->starttime = now;

  if (elapsed > 0)
    {
      nxsched_process_roundrobin(tcb, elapsed, true);
    }
}

#endif /* CONFIG_SCHED_TICKLESS */

#if defined(CONFIG_SMP) && defined(CONFIG_SCHED_TICKLESS)
struct roundrobin_policy_s
{
  FAR struct tcb_s *tcb;
  bool              rr;
};
#endif

/* Must run on the CPU of a running task, in a critical section, so that
 * neither the timer callback nor a switch sees the policy change half
 * done.
 */

static void roundrobin_set_policy(FAR struct tcb_s *tcb, bool rr)
{
#ifdef CONFIG_SCHED_TICKLESS
  bool    running = tcb == this_task();
  clock_t now     = clock_systime_ticks();

  if (running && (atomic_read(&tcb->flags) & TCB_FLAG_POLICY_MASK) ==
                 TCB_FLAG_SCHED_RR)
    {
      nxsched_suspend_roundrobin(tcb, now);
    }
#endif

  atomic_and(&tcb->flags, ~TCB_FLAG_POLICY_MASK);
  if (rr)
    {
      atomic_or(&tcb->flags, TCB_FLAG_SCHED_RR);
      tcb->timeslice = MSEC2TICK(CONFIG_RR_INTERVAL);
    }
  else
    {
      tcb->timeslice = 0;
    }

#ifdef CONFIG_SCHED_TICKLESS
  if (running && rr)
    {
      nxsched_resume_roundrobin(tcb, now);
    }
#endif
}

#if defined(CONFIG_SMP) && defined(CONFIG_SCHED_TICKLESS)
static int roundrobin_policy_handler(FAR void *cookie)
{
  FAR struct roundrobin_policy_s *arg = cookie;
  irqstate_t flags = enter_critical_section();

  roundrobin_set_policy(arg->tcb, arg->rr);
  leave_critical_section(flags);
  return OK;
}
#endif

static void roundrobin_switch_policy(FAR struct tcb_s *tcb, bool rr)
{
  irqstate_t flags = enter_critical_section();

#if defined(CONFIG_SMP) && defined(CONFIG_SCHED_TICKLESS)
  /* The RR timer is per-CPU, so a task running on another CPU is updated
   * there.  It stays pinned to that CPU until the call has completed.
   */

  if (tcb->task_state == TSTATE_TASK_RUNNING && tcb->cpu != this_cpu())
    {
      struct roundrobin_policy_s arg;
      bool locked;

      arg.tcb = tcb;
      arg.rr  = rr;
      locked  = (atomic_or(&tcb->flags, TCB_FLAG_CPU_LOCKED) &
                 TCB_FLAG_CPU_LOCKED) != 0;

      if (nxsched_cpu_call_single(tcb->cpu, roundrobin_policy_handler,
                                  &arg) < 0)
        {
          roundrobin_set_policy(tcb, rr);
        }

      if (!locked)
        {
          atomic_and(&tcb->flags, ~TCB_FLAG_CPU_LOCKED);
        }
    }
  else
#endif
    {
      roundrobin_set_policy(tcb, rr);
    }

  leave_critical_section(flags);
}

/****************************************************************************
 * Name:  nxsched_start_roundrobin
 *
 * Description:
 *   Switch a thread to round-robin scheduling.  If the thread is running,
 *   the round-robin timer of its CPU is armed for it immediately.
 *
 * Input Parameters:
 *   tcb - The TCB of the thread to start.
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

void nxsched_start_roundrobin(FAR struct tcb_s *tcb)
{
  roundrobin_switch_policy(tcb, true);
}

/****************************************************************************
 * Name:  nxsched_stop_roundrobin
 *
 * Description:
 *   Terminate round-robin scheduling on a thread.  If the thread is
 *   running, the round-robin timer of its CPU is dropped immediately.
 *
 * Input Parameters:
 *   tcb - The TCB of the thread to stop.
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

void nxsched_stop_roundrobin(FAR struct tcb_s *tcb)
{
  roundrobin_switch_policy(tcb, false);
}
