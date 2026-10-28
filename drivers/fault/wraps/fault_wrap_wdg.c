/****************************************************************************
 * drivers/fault/wraps/fault_wrap_wdg.c
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

#include <stddef.h>

#include <nuttx/timers/watchdog.h>

#include <nuttx/fault_inject.h>

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

/* The real watchdog_* implementations.  When the wrapsymbol tool rewrites
 * the link, references to watchdog_register() / watchdog_unregister() in
 * the rest of the kernel image are redirected to the __wrap_* functions
 * below; the originals are renamed to __real_watchdog_*().  Wrapping these
 * global entry points keeps drivers/timers/watchdog.c untouched.
 *
 * watchdog_register() has three prototypes selected by the AUTOMONITOR
 * build option, so the __real/__wrap signatures must mirror the same
 * conditionals.
 */

#if defined(CONFIG_WATCHDOG_AUTOMONITOR_BY_ONESHOT)
FAR void *__real_watchdog_register(FAR const char *path,
                                   FAR struct watchdog_lowerhalf_s *lower,
                                   FAR struct oneshot_lowerhalf_s *oneshot);
#elif defined(CONFIG_WATCHDOG_AUTOMONITOR_BY_TIMER)
FAR void *__real_watchdog_register(FAR const char *path,
                                   FAR struct watchdog_lowerhalf_s *lower,
                                   FAR struct timer_lowerhalf_s *timer);
#else
FAR void *__real_watchdog_register(FAR const char *path,
                                   FAR struct watchdog_lowerhalf_s *lower);
#endif

void __real_watchdog_unregister(FAR void *handle);

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: __wrap_watchdog_register
 *
 * Description:
 *   Fault point wdg.register: RETURN_NULL fakes a watchdog-registration
 *   failure (driver bring-up cannot obtain a handle); DELAY stalls the
 *   registration.
 *
 ****************************************************************************/

#if defined(CONFIG_WATCHDOG_AUTOMONITOR_BY_ONESHOT)
FAR void *__wrap_watchdog_register(FAR const char *path,
                                   FAR struct watchdog_lowerhalf_s *lower,
                                   FAR struct oneshot_lowerhalf_s *oneshot)
{
  FAR void *ret;
  FAULT_INJECT_WRAP_RETVAL_NULL(wdg, watchdog_register,
                                __real_watchdog_register(path, lower,
                                                         oneshot), &ret);
  return ret;
}
#elif defined(CONFIG_WATCHDOG_AUTOMONITOR_BY_TIMER)
FAR void *__wrap_watchdog_register(FAR const char *path,
                                   FAR struct watchdog_lowerhalf_s *lower,
                                   FAR struct timer_lowerhalf_s *timer)
{
  FAR void *ret;
  FAULT_INJECT_WRAP_RETVAL_NULL(wdg, watchdog_register,
                                __real_watchdog_register(path, lower,
                                                         timer), &ret);
  return ret;
}
#else
FAR void *__wrap_watchdog_register(FAR const char *path,
                                   FAR struct watchdog_lowerhalf_s *lower)
{
  FAR void *ret;
  FAULT_INJECT_WRAP_RETVAL_NULL(wdg, watchdog_register,
                                __real_watchdog_register(path, lower), &ret);
  return ret;
}
#endif

/****************************************************************************
 * Name: __wrap_watchdog_unregister
 *
 * Description:
 *   Fault point wdg.unregister: SKIP swallows the unregister (the device
 *   node is left in place); DELAY stalls the teardown.
 *
 ****************************************************************************/

void __wrap_watchdog_unregister(FAR void *handle)
{
  FAULT_INJECT_WRAP_RETVAL_VOID(wdg, watchdog_unregister,
                                __real_watchdog_unregister(handle));
}
