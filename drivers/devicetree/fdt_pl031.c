/****************************************************************************
 * drivers/devicetree/fdt_pl031.c
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

#include <debug.h>
#include <errno.h>

#include <nuttx/fdt.h>
#include <nuttx/timers/rtc.h>
#include <nuttx/timers/pl031.h>

#include <libfdt.h>

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: fdt_pl031_devices_register
 *
 * Description:
 *   Scan the device tree for "arm,pl031" nodes and register each as an RTC
 *   lower-half character device (/dev/rtc0, /dev/rtc1, ...).  The PL031
 *   driver itself is reused unchanged; this hook only parses reg/interrupts
 *   from the FDT and wires the lower-half.
 *
 * Input Parameters:
 *   fdt      - Device tree handle
 *   irqbase  - Interrupt base number (added to the FDT-supplied line)
 *
 * Returned Value:
 *   Return OK on success; a negated errno on failure.
 *
 ****************************************************************************/

int fdt_pl031_devices_register(FAR const void *fdt, int irqbase)
{
  FAR struct rtc_lowerhalf_s *lower;
  uintptr_t addr;
  int offset = -1;
  int irqnum;
  int minor = 0;

  for (; ; )
    {
      offset = fdt_node_offset_by_compatible(fdt, offset, "arm,pl031");
      if (offset == -FDT_ERR_NOTFOUND)
        {
          break;
        }

      addr = fdt_get_reg_base(fdt, offset, 0);
      irqnum = fdt_get_irq(fdt, offset, 1, irqbase);
      if (addr == 0 || irqnum < 0)
        {
          return -EINVAL;
        }

      lower = pl031_initialize(addr, irqnum);
      if (lower == NULL)
        {
          return -ENODEV;
        }

      if (rtc_initialize(minor++, lower) < 0)
        {
          return -EEXIST;
        }
    }

  return OK;
}
