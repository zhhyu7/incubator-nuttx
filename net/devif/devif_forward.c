/****************************************************************************
 * net/devif/devif_forward.c
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

#include <stdbool.h>
#include <string.h>
#include <assert.h>
#include <debug.h>

#include <nuttx/mm/iob.h>
#include <nuttx/net/netdev.h>

#include "netforward/netforward.h"
#include "devif/devif.h"

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: devif_forward
 *
 * Description:
 *   Called from protocol-specific IP forwarding logic to re-send a packet.
 *
 * Input Parameters:
 *   fwd - An initialized instance of the common forwarding structure that
 *         includes everything needed to perform the forwarding operation.
 *
 * Returned Value:
 *   None
 *
 * Assumptions:
 *   The network is locked.
 *
 ****************************************************************************/

void devif_forward(FAR struct forward_s *fwd)
{
  FAR struct net_driver_s *dev = fwd->f_dev;

  /* Move the IOB chain that contains the L3 header and any data payload.
   * Its source already placed it in a buffer for dev when needed (d_alloc
   * or insufficient headroom, see netdev_iob_clone_alloc).
   */

  DEBUGASSERT(fwd->f_iob->io_offset >= NET_LL_GUARDSIZE(dev));
  netdev_iob_replace(dev, fwd->f_iob);
  dev->d_sndlen = 0;
  fwd->f_iob = NULL;
}
