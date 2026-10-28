/****************************************************************************
 * drivers/fault/wraps/fault_wrap_reset.c
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

#include <errno.h>

#include <nuttx/reset/reset.h>

#include <nuttx/fault_inject.h>

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

/* The real reset-controller entry points.  Only genuine global functions
 * are wrapped: reset_control_reset_reason(), reset_control_assert_reason()
 * and reset_control_deassert() (drivers/reset/core.c).
 *
 * The plain reset_control_reset() / reset_control_assert() variants are
 * static inline wrappers in <nuttx/reset/reset.h> that forward to the
 * _reason() functions, so ld --wrap cannot intercept them directly; every
 * such call is already funnelled through the _reason() symbols wrapped
 * here.
 */

int __real_reset_control_reset_reason(FAR struct reset_control *rstc,
                                      unsigned int reason);
int __real_reset_control_assert_reason(FAR struct reset_control *rstc,
                                       unsigned int reason);
int __real_reset_control_deassert(FAR struct reset_control *rstc);

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: __wrap_reset_control_reset_reason
 ****************************************************************************/

int __wrap_reset_control_reset_reason(FAR struct reset_control *rstc,
                                      unsigned int reason)
{
  int ret;
  FAULT_INJECT_WRAP_RETVAL(reset, reset_control_reset_reason, -EIO,
                           __real_reset_control_reset_reason(rstc, reason),
                           &ret);
  return ret;
}

/****************************************************************************
 * Name: __wrap_reset_control_assert_reason
 ****************************************************************************/

int __wrap_reset_control_assert_reason(FAR struct reset_control *rstc,
                                       unsigned int reason)
{
  int ret;
  FAULT_INJECT_WRAP_RETVAL(reset, reset_control_assert_reason, -EIO,
                           __real_reset_control_assert_reason(rstc, reason),
                           &ret);
  return ret;
}

/****************************************************************************
 * Name: __wrap_reset_control_deassert
 ****************************************************************************/

int __wrap_reset_control_deassert(FAR struct reset_control *rstc)
{
  int ret;
  FAULT_INJECT_WRAP_RETVAL(reset, reset_control_deassert, -EIO,
                           __real_reset_control_deassert(rstc), &ret);
  return ret;
}
