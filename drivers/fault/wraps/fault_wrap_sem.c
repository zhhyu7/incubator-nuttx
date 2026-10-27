/****************************************************************************
 * drivers/fault/wraps/fault_wrap_sem.c
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

#include <time.h>
#include <errno.h>

#include <nuttx/semaphore.h>

#include <nuttx/fault_inject.h>

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

/* The real semaphore slow-path implementations.  The inline fast-path
 * variants (nxsem_wait/trywait/post) cannot be wrapped because ld --wrap
 * does not intercept inlined call sites; the *_slow globals are the real
 * out-of-line symbols and the correct wrap targets.
 */

int __real_nxsem_wait_slow(FAR sem_t *sem);
int __real_nxsem_trywait_slow(FAR sem_t *sem);
int __real_nxsem_post_slow(FAR sem_t *sem);
int __real_nxsem_tickwait_slow(FAR sem_t *sem, clock_t delay);
int __real_nxsem_clockwait_slow(FAR sem_t *sem, clockid_t clockid,
                                FAR const struct timespec *abstime);

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: __wrap_nxsem_wait_slow
 ****************************************************************************/

int __wrap_nxsem_wait_slow(FAR sem_t *sem)
{
  int ret;
  FAULT_INJECT_WRAP_RETVAL(sem, nxsem_wait_slow, -EINVAL,
                           __real_nxsem_wait_slow(sem), &ret);
  return ret;
}

/****************************************************************************
 * Name: __wrap_nxsem_trywait_slow
 ****************************************************************************/

int __wrap_nxsem_trywait_slow(FAR sem_t *sem)
{
  int ret;
  FAULT_INJECT_WRAP_RETVAL(sem, nxsem_trywait_slow, -EAGAIN,
                           __real_nxsem_trywait_slow(sem), &ret);
  return ret;
}

/****************************************************************************
 * Name: __wrap_nxsem_post_slow
 ****************************************************************************/

int __wrap_nxsem_post_slow(FAR sem_t *sem)
{
  int ret;
  FAULT_INJECT_WRAP_RETVAL(sem, nxsem_post_slow, -EINVAL,
                           __real_nxsem_post_slow(sem), &ret);
  return ret;
}

/****************************************************************************
 * Name: __wrap_nxsem_tickwait_slow
 ****************************************************************************/

int __wrap_nxsem_tickwait_slow(FAR sem_t *sem, clock_t delay)
{
  int ret;
  FAULT_INJECT_WRAP_RETVAL(sem, nxsem_tickwait_slow, -ETIMEDOUT,
                           __real_nxsem_tickwait_slow(sem, delay), &ret);
  return ret;
}

/****************************************************************************
 * Name: __wrap_nxsem_clockwait_slow
 ****************************************************************************/

int __wrap_nxsem_clockwait_slow(FAR sem_t *sem, clockid_t clockid,
                                FAR const struct timespec *abstime)
{
  int ret;
  FAULT_INJECT_WRAP_RETVAL(sem, nxsem_clockwait_slow, -ETIMEDOUT,
                           __real_nxsem_clockwait_slow(sem, clockid,
                                                       abstime), &ret);
  return ret;
}
