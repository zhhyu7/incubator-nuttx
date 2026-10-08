/****************************************************************************
 * sched/sched/sched_processtickless.c
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
#include <nuttx/compiler.h>
#include <nuttx/nuttx.h>

#include <time.h>
#include <sys/param.h>
#include <assert.h>
#include <debug.h>

#include "sched/sched.h"
#include "wdog/wdog.h"
#include "hrtimer/hrtimer.h"
#include "clock/clock.h"
#include "nuttx/seqlock.h"

#ifdef CONFIG_CLOCK_TIMEKEEPING
#  include "clock/clock_timekeeping.h"
#endif

#ifdef CONFIG_SCHED_TICKLESS

/****************************************************************************
 * Public Data
 ****************************************************************************/

#ifdef CONFIG_SCHED_TICKLESS_LIMIT_MAX_SLEEP
/* By default, the RTOS tickless logic assumes that range of times that can
 * be represented by the underlying hardware time is so large that no special
 * precautions need to taken.  That is not always the case.  If there is a
 * limit to the maximum timing interval that be represented by the timer,
 * then that limit must be respected.
 *
 * If CONFIG_SCHED_TICKLESS_LIMIT_MAX_SLEEP is defined, then a 32-bit global
 * variable called g_oneshot_maxticks variable is enabled. The variable
 * is initialized by platform-specific logic at runtime to the maximum delay
 * that the timer can wait (in microseconds).  The RTOS tickless logic will
 * then limit all requested delays to this value (in ticks).
 */

DEFINE_PER_CPU_BMP(uint32_t, g_oneshot_maxticks) = UINT32_MAX;
#define g_oneshot_maxticks this_cpu_var_bmp(g_oneshot_maxticks)
#endif

/****************************************************************************
 * Private Functions
 ****************************************************************************/

#if !defined(CONFIG_CLOCK_TIMEKEEPING) && !defined(CONFIG_ALARM_ARCH) && \
    !defined(CONFIG_TIMER_ARCH)
int up_timer_gettick(FAR clock_t *ticks)
{
  struct timespec ts;
  int ret;
  ret = up_timer_gettime(&ts);
  *ticks = clock_time2ticks_floor(&ts);
  return ret;
}
#endif

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: nxsched_tick_expiration
 *
 * Description:
 *   If CONFIG_SCHED_TICKLESS is defined, then this function is provided by
 *   the RTOS base code and called from platform-specific code when the
 *   interval timer used to implement the tick-less OS expires.
 *
 * Input Parameters:
 *   None.
 *
 * Returned Value:
 *   Base code implementation assumes that this function is called from
 *   interrupt handling logic with interrupts disabled.
 *
 ****************************************************************************/

void nxsched_process_timer(void)
{
  uint64_t        nsec;
  struct timespec ts =
    {
      0
    };

  up_timer_gettime(&ts);
  nsec = clock_time2nsec(&ts);

  hrtimer_process(nsec);
}

/****************************************************************************
 * Name:  nxsched_get_next_expired
 *
 * Description:
 *   Get the time remaining until the next timer expiration.
 *
 * Input Parameters:
 *   None
 *
 * Returned Value:
 *   The time remaining until the next timer expiration.
 *
 * Note:
 *   There is two special returned value
 *   Zero     : The timer already expired or in processing.
 *   CLOCK_MAX: There is no working timer.
 *
 ****************************************************************************/

clock_t nxsched_get_next_expired(void)
{
  return wd_get_next_expire(clock_systime_ticks());
}

#endif /* CONFIG_SCHED_TICKLESS */
