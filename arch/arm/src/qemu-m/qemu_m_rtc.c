/****************************************************************************
 * arch/arm/src/qemu-m/qemu_m_rtc.c
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
#include <nuttx/arch.h>

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: up_rtc_initialize
 *
 * Description:
 *   The RTC on virt-m is a memory-mapped PL031 driven by a lower-half RTC
 *   driver (CONFIG_RTC_ARCH): the generic arch_rtc.c supplies up_rtc_time()/
 *   up_rtc_settime() against the lower-half registered from the device tree
 *   during board bring-up (fdt_pl031).  The clock subsystem still calls this
 *   arch entry early in start-up, before the lower-half exists, so there is
 *   nothing to do here beyond reporting success.
 *
 ****************************************************************************/

int up_rtc_initialize(void)
{
  return OK;
}
