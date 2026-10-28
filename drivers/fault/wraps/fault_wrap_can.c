/****************************************************************************
 * drivers/fault/wraps/fault_wrap_can.c
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
#include <stdint.h>
#include <errno.h>

#include <nuttx/can/can.h>

#include <nuttx/fault_inject.h>

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

/* The real CAN upper-half entry points.  can_receive() is the RX path a
 * lower-half driver calls (usually from the RX IRQ) to push a frame into
 * the FIFO; can_txready() is called when the hardware can accept another
 * frame.  Both are genuine global functions (drivers/can/can.c), not vtable
 * ops, so ld --wrap intercepts them and the originals remain reachable via
 * __real_*.
 *
 * NOTE: can_receive() / can_txready() are normally invoked from the CAN
 * lower-half RX/TX ISR (interrupt context).  Prefer RETURN_ERR / SKIP.
 * The DELAY action is declared supported by the wrapper macro, but the
 * framework runs up_udelay() unconditionally with no IRQ-context guard, so
 * arming DELAY on these points busy-waits inside the ISR and can stall
 * interrupt latency -- avoid DELAY here unless you understand that cost.
 */

int __real_can_receive(FAR struct can_dev_s *dev,
                       FAR struct can_hdr_s *hdr,
                       FAR uint8_t *data);
int __real_can_txready(FAR struct can_dev_s *dev);

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: __wrap_can_receive
 ****************************************************************************/

int __wrap_can_receive(FAR struct can_dev_s *dev,
                       FAR struct can_hdr_s *hdr,
                       FAR uint8_t *data)
{
  int ret;
  FAULT_INJECT_WRAP_RETVAL(can, can_receive, -ENOMEM,
                           __real_can_receive(dev, hdr, data), &ret);
  return ret;
}

/****************************************************************************
 * Name: __wrap_can_txready
 ****************************************************************************/

int __wrap_can_txready(FAR struct can_dev_s *dev)
{
  int ret;
  FAULT_INJECT_WRAP_RETVAL(can, can_txready, -EIO,
                           __real_can_txready(dev), &ret);
  return ret;
}
