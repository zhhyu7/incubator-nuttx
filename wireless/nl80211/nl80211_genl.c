/****************************************************************************
 * wireless/nl80211/nl80211_genl.c
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
#include <nuttx/net/genetlink.h>
#include <net/netlink/netlink.h>

#include "nl80211.h"

/****************************************************************************
 * Private Data
 ****************************************************************************/

static const struct genl_multicast_group g_nl80211_mcgrps[] =
{
  { .name = "scan"       },  /* index 0: NL80211_MCGRP_SCAN       */
  { .name = "mlme"       },  /* index 1: NL80211_MCGRP_MLME       */
  { .name = "regulatory" },  /* index 2: NL80211_MCGRP_REGULATORY  */
  { .name = "vendor"     },  /* index 3: NL80211_MCGRP_VENDOR      */
};

/* nl80211 generic netlink family definition */

static struct genl_family g_nl80211_family =
{
  .name        = "nl80211",
  .version     = 1,
  .hdrsize     = 0,
  .maxattr     = 0,  /* We parse attrs manually */
  .cmd_handler = nl80211_handle_msg,
  .mcgrps      = g_nl80211_mcgrps,
  .n_mcgrps    = sizeof(g_nl80211_mcgrps) /
                 sizeof(g_nl80211_mcgrps[0]),
};

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: nl80211_genl_family_id
 *
 * Description:
 *   Get the dynamically assigned generic netlink family ID for nl80211.
 *   This ID is assigned by the kernel during family registration and is
 *   used in netlink message headers to identify nl80211 messages.
 *
 * Input Parameters:
 *   None
 *
 * Returned Value:
 *   The nl80211 generic netlink family ID
 *
 ****************************************************************************/

uint16_t nl80211_genl_family_id(void)
{
  return g_nl80211_family.id;
}

/****************************************************************************
 * Name: nl80211_genl_mcgrp_id
 *
 * Description:
 *   Get the multicast group ID for a specific nl80211 multicast group.
 *   Multicast groups are used to broadcast events to multiple listeners.
 *
 *   Available groups:
 *   - index 0: "scan"       - Scan-related events (scan start, results)
 *   - index 1: "mlme"       - MLME events (connect, disconnect, auth)
 *   - index 2: "regulatory" - Regulatory domain changes
 *   - index 3: "vendor"     - Vendor-specific events
 *
 * Input Parameters:
 *   index - Multicast group index (0-3)
 *
 * Returned Value:
 *   The multicast group ID for the specified index
 *
 ****************************************************************************/

uint16_t nl80211_genl_mcgrp_id(int index)
{
  return g_nl80211_family.mcgrp_offset + index;
}

/****************************************************************************
 * Name: nl80211_init
 *
 * Description:
 *   Register the nl80211 generic netlink family with the kernel.
 *   This must be called during system initialization before any nl80211
 *   operations can be performed. Registration assigns a family ID and
 *   multicast group IDs that are used for all subsequent nl80211
 *   communication.
 *
 *   The registration includes:
 *   - Family name "nl80211"
 *   - Command handler (nl80211_handle_msg)
 *   - Four multicast groups (scan, mlme, regulatory, vendor)
 *
 * Input Parameters:
 *   None
 *
 * Returned Value:
 *   0 on success; negative error code on failure
 *
 ****************************************************************************/

int nl80211_init(void)
{
  return genl_register_family(&g_nl80211_family);
}
