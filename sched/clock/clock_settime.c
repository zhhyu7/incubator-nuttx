/****************************************************************************
 * sched/clock/clock_settime.c
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

#include <stdint.h>
#include <time.h>
#include <assert.h>
#include <errno.h>
#include <debug.h>
#include <sys/time.h>

#include <nuttx/arch.h>
#include <nuttx/nuttx.h>
#include <nuttx/sched.h>
#include <nuttx/fs/fs.h>
#include <nuttx/clock_notifier.h>
#include <nuttx/irq.h>
#include <nuttx/spinlock.h>
#include <nuttx/timers/ptp_clock.h>

#include "clock/clock.h"
#ifdef CONFIG_CLOCK_TIMEKEEPING
#  include "clock/clock_timekeeping.h"
#endif

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static void nxclock_set_realtime(FAR const struct timespec *tp)
{
#ifndef CONFIG_CLOCK_TIMEKEEPING
  struct timespec bias;
  irqstate_t flags;
#  ifdef CONFIG_CLOCK_ADJTIME
  const struct timeval zerodelta = {
    0, 0
  };
#  endif

  /* Interrupts are disabled here so that the in-memory time
   * representation and the RTC setting will be as close as
   * possible.
   */

  /* Get the elapsed time since power up (in milliseconds).  This is a
   * bias value that we need to use to correct the base time.
   */

  clock_systime_timespec(&bias);

  flags = spin_lock_irqsave(&g_basetime_lock);

  clock_timespec_subtract(tp, &bias, &g_basetime);
  clock_notifier_call_chain(CLOCK_REALTIME, tp);

  spin_unlock_irqrestore(&g_basetime_lock, flags);

  /* Setup the RTC (lo- or high-res) */

#  ifdef CONFIG_RTC
  if (g_rtc_enabled)
    {
      up_rtc_settime(tp);
    }
#  endif

#  ifdef CONFIG_CLOCK_ADJTIME
  /* Cancel any ongoing adjustment */

  adjtime(&zerodelta, NULL);
#  endif
#else
  clock_timekeeping_set_wall_time(tp);
#endif
}

#if CONFIG_SCHED_CRITMONITOR_MAXTIME_THREAD >= 0
/* Write-side counterpart of clock_process_runtime(): expand the same group
 * sum inline and shift tcb->run_time under the group lock so a subsequent
 * clock_gettime() reads back *ts.
 */

static void clock_set_process_runtime(FAR struct tcb_s *tcb,
                                      clockid_t clock_type,
                                      FAR const struct timespec *ts)
{
  FAR struct task_group_s *group   = tcb->group;
  clock_t                  current = 0u;
  unsigned long            freq    = perf_getfreq();
  clock_t                  target  = ((uint64_t)ts->tv_sec  * freq +
                                      (uint64_t)ts->tv_nsec * freq /
                                      NSEC_PER_SEC);
  irqstate_t               flags   = spin_lock_irqsave(&group->tg_lock);

# ifdef HAVE_GROUP_MEMBERS
  if (clock_type == CLOCK_PROCESS_CPUTIME_ID)
    {
      FAR sq_entry_t   *curr;
      FAR struct tcb_s *member;

      sq_for_every(&group->tg_members, curr)
        {
          member = container_of(curr, struct tcb_s, member);

          current += member->run_time;
        }
    }
  else
# endif /* HAVE_GROUP_MEMBERS */
    {
      current = tcb->run_time;
    }

  /* run_time is a pure accumulator the scheduler only adds to; adding
   * (target - current) shifts the next read to target.  The delta lands
   * on this task, a member of the summed group for the process clock.
   */

  tcb->run_time += target - current;

  spin_unlock_irqrestore(&group->tg_lock, flags);
}
#endif

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: clock_settime
 *
 * Description:
 *   Clock Functions based on POSIX APIs
 *
 *   CLOCK_REALTIME - POSIX demands this to be present. This is the wall
 *   time clock.
 *
 ****************************************************************************/

int clock_settime(clockid_t clock_id, FAR const struct timespec *tp)
{
  int ret = -EINVAL;

  if (tp != NULL && clock_timespec_validate(tp))
    {
      if (clock_id == CLOCK_REALTIME)
        {
          nxclock_set_realtime(tp);
          ret = 0;
        }
#ifdef CONFIG_PTP_CLOCK
      else if ((clock_id & CLOCK_MASK) == CLOCK_FD)
        {
          FAR struct file *filep;

          ret = ptp_clockid_to_filep(clock_id, &filep);
          if (ret >= 0)
            {
              ret = file_ioctl(filep, PTP_CLOCK_SETTIME, tp);
              file_put(filep);
            }
        }
#endif
#if CONFIG_SCHED_CRITMONITOR_MAXTIME_THREAD >= 0
      else if ((clock_id & CLOCK_MASK) == CLOCK_PROCESS_CPUTIME_ID ||
               (clock_id & CLOCK_MASK) == CLOCK_THREAD_CPUTIME_ID)
        {
          clockid_t  clock_type = clock_id & CLOCK_MASK;
          pid_t             pid = clock_id >> CLOCK_SHIFT;
          FAR struct tcb_s *tcb = pid == 0 ? this_task() :
                                             nxsched_get_tcb(pid);

          if (tcb != NULL)
            {
              clock_set_process_runtime(tcb, clock_type, tp);

              if (pid != 0)
                {
                  nxsched_put_tcb(tcb);
                }

              ret = 0;
            }
        }
#endif
    }

  if (ret < 0)
    {
      set_errno(-ret);
      ret = ERROR;
    }

  return ret;
}
