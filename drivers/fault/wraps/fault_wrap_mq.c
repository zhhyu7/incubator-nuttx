/****************************************************************************
 * drivers/fault/wraps/fault_wrap_mq.c
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
#include <mqueue.h>
#include <fcntl.h>
#include <errno.h>

#include <nuttx/fs/fs.h>
#include <nuttx/mqueue.h>

#include <nuttx/fault_inject.h>

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

/* The real mqueue implementations.  All four are global out-of-line
 * symbols and valid wrap targets.
 */

int     __real_nxmq_send(mqd_t mqdes, FAR const char *msg, size_t msglen,
                         unsigned int prio);
ssize_t __real_nxmq_receive(mqd_t mqdes, FAR char *msg, size_t msglen,
                            FAR unsigned int *prio);
int     __real_file_mq_send(FAR struct file *mq, FAR const char *msg,
                            size_t msglen, unsigned int prio);
ssize_t __real_file_mq_receive(FAR struct file *mq, FAR char *msg,
                               size_t msglen, FAR unsigned int *prio);

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: __wrap_nxmq_send
 ****************************************************************************/

int __wrap_nxmq_send(mqd_t mqdes, FAR const char *msg, size_t msglen,
                     unsigned int prio)
{
  int ret;
  FAULT_INJECT_WRAP_RETVAL(mq, nxmq_send, -EINTR,
                           __real_nxmq_send(mqdes, msg, msglen, prio),
                           &ret);
  return ret;
}

/****************************************************************************
 * Name: __wrap_nxmq_receive
 ****************************************************************************/

ssize_t __wrap_nxmq_receive(mqd_t mqdes, FAR char *msg, size_t msglen,
                            FAR unsigned int *prio)
{
  ssize_t ret;
  FAULT_INJECT_WRAP_RETVAL_SSIZE(mq, nxmq_receive, -EINTR,
                                 __real_nxmq_receive(mqdes, msg, msglen,
                                                     prio), &ret);
  return ret;
}

/****************************************************************************
 * Name: __wrap_file_mq_send
 ****************************************************************************/

int __wrap_file_mq_send(FAR struct file *mq, FAR const char *msg,
                        size_t msglen, unsigned int prio)
{
  int ret;
  FAULT_INJECT_WRAP_RETVAL(mq, file_mq_send, -EINTR,
                           __real_file_mq_send(mq, msg, msglen, prio),
                           &ret);
  return ret;
}

/****************************************************************************
 * Name: __wrap_file_mq_receive
 ****************************************************************************/

ssize_t __wrap_file_mq_receive(FAR struct file *mq, FAR char *msg,
                               size_t msglen, FAR unsigned int *prio)
{
  ssize_t ret;
  FAULT_INJECT_WRAP_RETVAL_SSIZE(mq, file_mq_receive, -EINTR,
                                 __real_file_mq_receive(mq, msg, msglen,
                                                        prio), &ret);
  return ret;
}
