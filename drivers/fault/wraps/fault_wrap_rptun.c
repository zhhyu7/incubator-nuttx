/****************************************************************************
 * drivers/fault/wraps/fault_wrap_rptun.c
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

#include <nuttx/rptun/rptun.h>

#include <nuttx/fault_inject.h>

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

/* The real rptun_* implementations.  When the wrapsymbol tool rewrites the
 * link, every reference to rptun_boot() etc. in the rest of the kernel
 * image is redirected to __wrap_rptun_boot() below; the original
 * definitions are renamed to __real_rptun_*().  Wrapping these top-level
 * global entry points keeps drivers/rptun/rptun.c completely untouched.
 */

int __real_rptun_boot(FAR const char *cpuname);
int __real_rptun_poweroff(FAR const char *cpuname);
int __real_rptun_reset(FAR const char *cpuname, unsigned long value);
int __real_rptun_wait(FAR const char *cpuname, unsigned long phase);

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: __wrap_rptun_boot
 *
 * Description:
 *   Fault point rptun.boot: RETURN_ERR fakes a remote-core bring-up
 *   failure, DELAY stalls the start, SKIP bypasses the real bring-up.
 *
 ****************************************************************************/

int __wrap_rptun_boot(FAR const char *cpuname)
{
  int ret;
  FAULT_INJECT_WRAP_RETVAL(rptun, rptun_boot, -EIO,
                           __real_rptun_boot(cpuname), &ret);
  return ret;
}

/****************************************************************************
 * Name: __wrap_rptun_poweroff
 *
 * Description:
 *   Fault point rptun.poweroff: RETURN_ERR fakes a power-off failure,
 *   DELAY stalls the stop, SKIP bypasses the real power-off.
 *
 ****************************************************************************/

int __wrap_rptun_poweroff(FAR const char *cpuname)
{
  int ret;
  FAULT_INJECT_WRAP_RETVAL(rptun, rptun_poweroff, -EIO,
                           __real_rptun_poweroff(cpuname), &ret);
  return ret;
}

/****************************************************************************
 * Name: __wrap_rptun_reset
 *
 * Description:
 *   Fault point rptun.reset: RETURN_ERR fakes a reset failure, DELAY
 *   stalls the reset, SKIP bypasses the real reset.
 *
 ****************************************************************************/

int __wrap_rptun_reset(FAR const char *cpuname, unsigned long value)
{
  int ret;
  FAULT_INJECT_WRAP_RETVAL(rptun, rptun_reset, -EIO,
                           __real_rptun_reset(cpuname, value), &ret);
  return ret;
}

/****************************************************************************
 * Name: __wrap_rptun_wait
 *
 * Description:
 *   Fault point rptun.wait: RETURN_ERR fakes a cross-core sync-wait
 *   failure (-ETIMEDOUT), DELAY stretches the wait, SKIP bypasses it.
 *
 ****************************************************************************/

int __wrap_rptun_wait(FAR const char *cpuname, unsigned long phase)
{
  int ret;
  FAULT_INJECT_WRAP_RETVAL(rptun, rptun_wait, -ETIMEDOUT,
                           __real_rptun_wait(cpuname, phase), &ret);
  return ret;
}
