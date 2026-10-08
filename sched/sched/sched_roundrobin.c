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
 *   The number if ticks remaining until the next time slice expires.
 *   CLOCK_MAX is returned if there is no time slicing (i.e., the task at
 *   the head of the ready-to-run list does not support round robin
 *   scheduling).
 *
 *   The value one may returned under certain circumstances that probably
 *   can't happen.  The value one is the minimal timer setup and it means
 *   that a context switch is needed now, but cannot be performed because
 *   noswitches == true.
 *
 * Assumptions:
 *   - Interrupts are disabled
 *   - The task associated with TCB uses the round robin scheduling
 *     policy
 *
 ****************************************************************************/

clock_t nxsched_process_roundrobin(FAR struct tcb_s *tcb, clock_t ticks,
                                   bool noswitches)
{
  FAR struct tcb_s *ptcb;  /* Pointer to the next ready-to-run TCB */
  clock_t ret;
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

  ret = tcb->timeslice;
  if (tcb->timeslice <= 0)
    {
      /* The timeslice expired but the reset (and the possible rotation)
       * cannot be performed if the scheduler is locked or the caller is
       * mid-switch.  Return CLOCK_MAX: nothing is to be timed;
       * sched_unlock() (or the resume path) resets the timeslice and
       * re-arms the scheduler event timer.
       */

      if (nxsched_islocked_tcb(tcb) || noswitches)
        {
          ret = 0u;
        }
      else
        {
          /* Reset the timeslice. */

          tcb->timeslice = MSEC2TICK(CONFIG_RR_INTERVAL);
          ret = tcb->timeslice;

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
                      ret = 0u;
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
                  ret = 0u;
                }
            }
        }
    }

  /* If a context switch occurred, the roundrobin timer has been
   * re-armed by nxsched_resume_roundrobin() for the new task.
   * Return 0 to prevent the caller from re-arming it again.
   */

  return ret;
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

      delay = nxsched_process_roundrobin(tcb, elapsed, false);

      if (delay != 0u)
        {
          /* Re-arm the round-robin timer. */

          event->tcb = tcb;
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
