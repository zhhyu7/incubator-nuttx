/****************************************************************************
 * drivers/fault/wraps/fault_wrap_wq.c
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

#include <sys/types.h>
#include <errno.h>

#include <nuttx/clock.h>
#include <nuttx/wqueue.h>

#include <nuttx/fault_inject.h>

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

/* The real workqueue implementations.  work_queue_wq is defined only on
 * the kernel side (no ambiguity).  work_queue / work_cancel also exist in
 * libc under CONFIG_LIBC_USRWORK && !__KERNEL__; in a flat build without
 * CONFIG_LIBC_USRWORK the libc copies are compiled out, so the kernel-side
 * definitions are the unambiguous wrap targets here.
 *
 * NOTE (per adaptation review): the DELAY action is unsafe for workqueue
 * wrappers because waking a worker re-enters work_queue() under a spinlock.
 * Only SKIP / RETURN_ERR should be armed on this group operationally; the
 * macro still advertises DELAY as supported, so avoid DELAY at runtime.
 */

int __real_work_queue(int qid, FAR struct work_s *work, worker_t worker,
                      FAR void *arg, clock_t delay);
int __real_work_queue_wq(FAR struct kwork_wqueue_s *wqueue,
                         FAR struct work_s *work, worker_t worker,
                         FAR void *arg, clock_t delay);
int __real_work_cancel(int qid, FAR struct work_s *work);

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: __wrap_work_queue
 ****************************************************************************/

int __wrap_work_queue(int qid, FAR struct work_s *work, worker_t worker,
                      FAR void *arg, clock_t delay)
{
  int ret;
  FAULT_INJECT_WRAP_RETVAL(wq, work_queue, -EINVAL,
                           __real_work_queue(qid, work, worker, arg, delay),
                           &ret);
  return ret;
}

/****************************************************************************
 * Name: __wrap_work_queue_wq
 ****************************************************************************/

int __wrap_work_queue_wq(FAR struct kwork_wqueue_s *wqueue,
                         FAR struct work_s *work, worker_t worker,
                         FAR void *arg, clock_t delay)
{
  int ret;
  FAULT_INJECT_WRAP_RETVAL(wq, work_queue_wq, -EINVAL,
                           __real_work_queue_wq(wqueue, work, worker,
                                                arg, delay), &ret);
  return ret;
}

/****************************************************************************
 * Name: __wrap_work_cancel
 ****************************************************************************/

int __wrap_work_cancel(int qid, FAR struct work_s *work)
{
  int ret;
  FAULT_INJECT_WRAP_RETVAL(wq, work_cancel, -EINVAL,
                           __real_work_cancel(qid, work), &ret);
  return ret;
}
