/****************************************************************************
 * drivers/fault/wraps/fault_wrap_clk.c
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
#include <errno.h>

#include <nuttx/clk/clk.h>

#include <nuttx/fault_inject.h>

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

/* The real clock-tree entry points.  clk_enable() ungates a clock and
 * clk_set_rate() reprograms its rate; both are genuine global functions
 * (drivers/clk/clk.c), not vtable ops, so ld --wrap intercepts them and the
 * originals remain reachable via __real_*.
 */

int __real_clk_enable(FAR struct clk_s *clk);
int __real_clk_set_rate(FAR struct clk_s *clk, uint32_t rate);

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: __wrap_clk_enable
 ****************************************************************************/

int __wrap_clk_enable(FAR struct clk_s *clk)
{
  int ret;
  FAULT_INJECT_WRAP_RETVAL(clk, clk_enable, -EIO,
                           __real_clk_enable(clk), &ret);
  return ret;
}

/****************************************************************************
 * Name: __wrap_clk_set_rate
 ****************************************************************************/

int __wrap_clk_set_rate(FAR struct clk_s *clk, uint32_t rate)
{
  int ret;
  FAULT_INJECT_WRAP_RETVAL(clk, clk_set_rate, -EINVAL,
                           __real_clk_set_rate(clk, rate), &ret);
  return ret;
}
