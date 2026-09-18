/****************************************************************************
 * drivers/syslog/vsyslog.c
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

#include <stdio.h>
#include <syslog.h>
#include <errno.h>

#include <nuttx/arch.h>
#include <nuttx/init.h>
#include <nuttx/clock.h>
#include <nuttx/streams.h>
#include <nuttx/syslog/syslog.h>

#include "sched/sched.h"
#include "syslog.h"

/****************************************************************************
 * Private Data
 ****************************************************************************/

/* CONFIG_SYSLOG_TIMESTAMP_BOTH enables both clock sources, so enable the
 * individual realtime and boottime timestamps.
 */

#ifdef CONFIG_SYSLOG_TIMESTAMP_BOTH
#  define CONFIG_SYSLOG_TIMESTAMP_REALTIME 1
#  define CONFIG_SYSLOG_TIMESTAMP_BOOTTIME 1
#endif

#ifdef CONFIG_SYSLOG_COLOR_OUTPUT
static FAR const char * const g_priority_color[] =
  {
    "\e[31;1;5m", /* LOG_EMERG, Red, Bold, Blinking */
    "\e[31;1m",   /* LOG_ALERT, Red, Bold */
    "\e[31;1m",   /* LOG_CRIT, Red, Bold */
    "\e[31m",     /* LOG_ERR, Red */
    "\e[33m",     /* LOG_WARNING, Yellow */
    "\e[1m",      /* LOG_NOTICE, Bold */
    "",           /* LOG_INFO, Normal */
    "\e[2m",      /* LOG_DEBUG, Dim */
  };

#  define SYSLOG_COLOR_RESET     "\e[0m"
#  define SYSLOG_COLOR_RESET_LEN (sizeof(SYSLOG_COLOR_RESET) - 1)
#endif

#ifdef CONFIG_SYSLOG_PRIORITY
static const char g_priority_letter[] =
  {
    /* EMERG, ALERT, CRIT, ERROR, WARN, NOTICE, INFO, DEBUG */

    'F', 'A', 'C', 'E', 'W', 'N', 'I', 'D'
  };
#endif

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: nx_vsyslog
 *
 * Description:
 *   nx_vsyslog() handles the system logging system calls. It is functionally
 *   equivalent to vsyslog() except that (1) the per-process priority
 *   filtering has already been performed and the va_list parameter is
 *   passed by reference.  That is because the va_list is a structure in
 *   some compilers and passing of structures in the NuttX sycalls does
 *   not work.
 *
 ****************************************************************************/

void nx_vsyslog(uint64_t meta, FAR const IPTR char *fmt, FAR va_list *ap)
{
  struct lib_syslograwstream_s stream;
#ifdef CONFIG_SYSLOG_PROCESS_NAME
  FAR struct tcb_s *tcb = this_task();
#endif
#ifdef CONFIG_SYSLOG_TIMESTAMP_BOOTTIME
  struct timespec ts_boot =
    {
      0
    };

#endif
#ifdef CONFIG_SYSLOG_TIMESTAMP_REALTIME
  struct timespec ts =
    {
      0
    };

#endif
#ifdef CONFIG_SYSLOG_TIMESTAMP_FORMATTED
  struct tm tm =
    {
      0
    };

  char date_buf[CONFIG_SYSLOG_TIMESTAMP_BUFFER];
#endif

  /* Per-tag filtering */

  if (!syslog_is_enabled(meta))
    {
      return;
    }

  /* Save errno so that nx_vsyslog does not clobber it. The syslog
   * formatting path calls libc functions (e.g. strftime -> tzset -> open)
   * that may modify errno as a side effect.
   */

  int saved_errno = get_errno();

  /* Wrap the low-level output in a stream object and let lib_vsprintf
   * do the work.
   */

  lib_syslograwstream_open(&stream);

#ifdef CONFIG_SYSLOG_TIMESTAMP
  /* Get the current time.  Since debug output may be generated very early
   * in the start-up sequence, hardware timer support may not yet be
   * available.
   */

  if (OSINIT_HW_READY())
    {
#  ifdef CONFIG_SYSLOG_TIMESTAMP_REALTIME
      /* Use CLOCK_REALTIME if so configured */

      nxclock_gettime(CLOCK_REALTIME, &ts);
#  endif

#  ifdef CONFIG_SYSLOG_TIMESTAMP_BOOTTIME
      /* Use CLOCK_MONOTONIC if so configured */

      nxclock_gettime(CLOCK_MONOTONIC, &ts_boot);
#  endif

      /* Prepend the message with the current time, if available */

#  ifdef CONFIG_SYSLOG_TIMESTAMP_FORMATTED
#    ifdef CONFIG_SYSLOG_TIMESTAMP_LOCALTIME
      localtime_r(&ts.tv_sec, &tm);
#    else
      gmtime_r(&ts.tv_sec, &tm);
#    endif
#  endif
    }

#  ifdef CONFIG_SYSLOG_TIMESTAMP_FORMATTED
  date_buf[0] = '\0';
  strftime(date_buf, CONFIG_SYSLOG_TIMESTAMP_BUFFER,
           CONFIG_SYSLOG_TIMESTAMP_FORMAT, &tm);
#  endif
#endif

#if defined(CONFIG_SYSLOG_COLOR_OUTPUT) || defined(CONFIG_SYSLOG_TIMESTAMP) || \
    defined(CONFIG_SMP) || defined(CONFIG_SYSLOG_PROCESSID) || \
    defined(CONFIG_SYSLOG_PRIORITY) || defined(CONFIG_SYSLOG_PREFIX) || \
    defined(CONFIG_SYSLOG_PROCESS_NAME)

  lib_sprintf_internal(&stream.common,
#ifdef CONFIG_SYSLOG_COLOR_OUTPUT
  /* Reset the terminal style. */

                       SYSLOG_COLOR_RESET
#endif

#ifdef CONFIG_SYSLOG_TIMESTAMP_REALTIME
#  ifdef CONFIG_SYSLOG_TIMESTAMP_FORMATTED
#    if defined(CONFIG_SYSLOG_TIMESTAMP_FORMAT_MICROSECOND)
                       "[%s.%06ld] "
#    elif defined(CONFIG_SYSLOG_TIMESTAMP_FORMAT_MILLISECOND)
                       "[%s.%03ld] "
#    else
                       "[%s] "
#    endif
#  else
#    if defined(CONFIG_SYSLOG_TIMESTAMP_FORMAT_MICROSECOND)
                       "[%5ju.%06ld] "
#    elif defined(CONFIG_SYSLOG_TIMESTAMP_FORMAT_MILLISECOND)
                       "[%5ju.%03ld] "
#    else
                       "[%5ju] "
#    endif
#  endif
#endif

#ifdef CONFIG_SYSLOG_TIMESTAMP_BOOTTIME
  /* Prepend the boottime (monotonic seconds.subseconds). */

#  if defined(CONFIG_SYSLOG_TIMESTAMP_FORMAT_MICROSECOND)
                       "[%ju.%06ld] "
#  elif defined(CONFIG_SYSLOG_TIMESTAMP_FORMAT_MILLISECOND)
                       "[%ju.%03ld] "
#  else
                       "[%ju] "
#  endif
#endif

#ifdef CONFIG_SYSLOG_PROCESSID
  /* Prepend the Thread ID */

                       "[%2d] "
#endif

#ifdef CONFIG_SYSLOG_COLOR_OUTPUT
  /* Set the terminal style according to message priority. */

                       "%s"
#endif

#ifdef CONFIG_SYSLOG_PRIORITY
  /* Prepend the message priority. */

                       "[%c] "
#endif

#ifndef CONFIG_UP
                       "[%d] "
#endif

#ifdef CONFIG_SYSLOG_PREFIX
  /* Prepend the prefix, if available */

                       "[%s] "
#endif
#ifdef CONFIG_SYSLOG_PROCESS_NAME
  /* Prepend the thread name */

                       "%s: "
#endif
#ifdef CONFIG_SYSLOG_TIMESTAMP_REALTIME
#  ifdef CONFIG_SYSLOG_TIMESTAMP_FORMATTED
#    if defined(CONFIG_SYSLOG_TIMESTAMP_FORMAT_MICROSECOND)
                       , date_buf, ts.tv_nsec / NSEC_PER_USEC
#    elif defined(CONFIG_SYSLOG_TIMESTAMP_FORMAT_MILLISECOND)
                       , date_buf, ts.tv_nsec / NSEC_PER_MSEC
#    else
                       , date_buf
#    endif
#  else
#    if defined(CONFIG_SYSLOG_TIMESTAMP_FORMAT_MICROSECOND)
                       , (uintmax_t)ts.tv_sec
                       , ts.tv_nsec / NSEC_PER_USEC
#    elif defined(CONFIG_SYSLOG_TIMESTAMP_FORMAT_MILLISECOND)
                       , (uintmax_t)ts.tv_sec
                       , ts.tv_nsec / NSEC_PER_MSEC
#    else
                       , (uintmax_t)ts.tv_sec
#    endif
#  endif
#endif

#ifdef CONFIG_SYSLOG_TIMESTAMP_BOOTTIME
#  if defined(CONFIG_SYSLOG_TIMESTAMP_FORMAT_MICROSECOND)
                       , (uintmax_t)ts_boot.tv_sec
                       , ts_boot.tv_nsec / NSEC_PER_USEC
#  elif defined(CONFIG_SYSLOG_TIMESTAMP_FORMAT_MILLISECOND)
                       , (uintmax_t)ts_boot.tv_sec
                       , ts_boot.tv_nsec / NSEC_PER_MSEC
#  else
                       , (uintmax_t)ts_boot.tv_sec
#  endif
#endif

#ifdef CONFIG_SYSLOG_PROCESSID
  /* Prepend the Thread ID */

                       , gettid()
#endif

#ifdef CONFIG_SYSLOG_COLOR_OUTPUT
  /* Set the terminal style according to message priority. */

                       , g_priority_color[LOG_GETPRI(meta)]
#endif

#ifdef CONFIG_SYSLOG_PRIORITY
  /* Prepend the message priority. */

                       , g_priority_letter[LOG_GETPRI(meta)]
#endif

#ifndef CONFIG_UP
                       , this_cpu()
#endif

#ifdef CONFIG_SYSLOG_PREFIX
  /* Prepend the prefix, if available */

                       , CONFIG_SYSLOG_PREFIX_STRING
#endif

#ifdef CONFIG_SYSLOG_PROCESS_NAME
  /* Prepend the thread name */

                       , get_task_name(tcb)
#endif
                    );

#endif /* CONFIG_SYSLOG_COLOR_OUTPUT || CONFIG_SYSLOG_TIMESTAMP || ... */

  /* Generate the output */

  lib_vsprintf_internal(&stream.common, fmt, *ap);

  if (stream.last_ch != '\n')
    {
      lib_stream_putc(&stream.common, '\n');
    }

#ifdef CONFIG_SYSLOG_COLOR_OUTPUT
  /* Reset the terminal style back to normal. */

  lib_stream_puts(&stream.common, SYSLOG_COLOR_RESET,
                  SYSLOG_COLOR_RESET_LEN);
#endif

  /* Flush and destroy the syslog stream buffer */

  lib_syslograwstream_close(&stream);

  /* Restore errno that may have been clobbered by libc calls
   * (e.g. strftime -> tzset -> open sets ENOENT when timezone
   * file is not found).
   */

  set_errno(saved_errno);
}
