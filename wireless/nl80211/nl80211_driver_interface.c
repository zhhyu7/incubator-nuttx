/****************************************************************************
 * wireless/nl80211/nl80211_driver_interface.c
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

#include <string.h>
#include <stdlib.h>
#include <debug.h>

#include <nuttx/net/netdev.h>
#include <nuttx/kmalloc.h>
#include <nuttx/mutex.h>
#include <netpacket/netlink.h>
#include <netpacket/rtnetlink.h>
#include <netpacket/genetlink.h>
#include <net/netlink/netlink.h>
#include <net/nl80211.h>
#include <net/netdev/netdev.h>

#include "nl80211.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define MAX_SCAN_RESULTS 64
#define WLAN_EID_RSN 48

static struct nl80211_scan_results_s g_scan_results;
static mutex_t g_scan_lock = NXMUTEX_INITIALIZER;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: scan_results_init
 *
 * Description:
 *   Initialize scan results storage if not already initialized
 *
 * Input Parameters:
 *   results - Scan results storage
 *
 * Returned Value:
 *   0 on success; negative error code on failure
 *
 ****************************************************************************/

static int scan_results_init(FAR struct nl80211_scan_results_s *results)
{
  if (results->bss == NULL)
    {
      results->bss =
          zalloc(sizeof(struct nl80211_scan_bss_s *) * MAX_SCAN_RESULTS);

      if (!results->bss)
        {
          return -ENOMEM;
        }

      results->num = 0;
    }

  return 0;
}

/****************************************************************************
 * Name: find_existing_bss_index
 *
 * Description:
 *   Find existing BSS entry with matching BSSID
 *
 * Input Parameters:
 *   results - Scan results storage
 *   bss     - BSS information to search for
 *
 * Returned Value:
 *   Index of existing BSS entry, or -1 if not found
 *
 ****************************************************************************/

static int find_existing_bss_index(
             FAR struct nl80211_scan_results_s *results,
             FAR struct nl80211_scan_bss_s *bss)
{
  for (int i = 0; i < results->num; i++)
    {
      if (results->bss[i] &&
          memcmp(results->bss[i]->bssid, bss->bssid, ETH_ALEN) == 0)
        {
          return i;
        }
    }

  return -1;
}

/****************************************************************************
 * Name: merge_probe_response_ie
 *
 * Description:
 *   Merge probe response IE data with existing beacon IE data
 *
 * Input Parameters:
 *   existing_bss - Existing BSS entry
 *   new_bss      - New BSS data with probe response IE
 *
 * Returned Value:
 *   0 on success; negative error code on failure
 *
 ****************************************************************************/

static int merge_probe_response_ie(
                            FAR struct nl80211_scan_bss_s *existing_bss,
                            FAR struct nl80211_scan_bss_s *new_bss)
{
  size_t new_total_ie_len = new_bss->ie_len + existing_bss->beacon_ie_len;
  FAR uint8_t *new_ie_data = NULL;

  if (new_total_ie_len > 0)
    {
      new_ie_data = malloc(new_total_ie_len);
      if (!new_ie_data)
        {
          return -ENOMEM;
        }

      memcpy(new_ie_data, new_bss->ie_data, new_bss->ie_len);

      if (existing_bss->beacon_ie_len > 0 && existing_bss->ie_data)
        {
          FAR uint8_t *existing_beacon_ie;
          if (existing_bss->ie_len > 0)
            {
              existing_beacon_ie =
                  existing_bss->ie_data + existing_bss->ie_len;
            }
          else
            {
              existing_beacon_ie = existing_bss->ie_data;
            }

          if (existing_beacon_ie + existing_bss->beacon_ie_len <=
              existing_bss->ie_data + existing_bss->ie_len +
              existing_bss->beacon_ie_len)
            {
              memcpy(new_ie_data + new_bss->ie_len, existing_beacon_ie,
                     existing_bss->beacon_ie_len);
            }
        }
    }

  if (existing_bss->ie_data)
    {
      free(existing_bss->ie_data);
    }

  existing_bss->ie_data = new_ie_data;
  existing_bss->ie_len = new_bss->ie_len;

  return 0;
}

/****************************************************************************
 * Name: merge_beacon_ie
 *
 * Description:
 *   Merge beacon IE data with existing probe response IE data
 *
 * Input Parameters:
 *   existing_bss - Existing BSS entry
 *   new_bss      - New BSS data with beacon IE
 *
 * Returned Value:
 *   0 on success; negative error code on failure
 *
 ****************************************************************************/

static int merge_beacon_ie(FAR struct nl80211_scan_bss_s *existing_bss,
                           FAR struct nl80211_scan_bss_s *new_bss)
{
  size_t new_total_ie_len = existing_bss->ie_len + new_bss->beacon_ie_len;
  FAR uint8_t *new_ie_data = NULL;

  if (new_total_ie_len > 0)
    {
      new_ie_data = malloc(new_total_ie_len);
      if (!new_ie_data)
        {
          return -ENOMEM;
        }

      if (existing_bss->ie_len > 0 && existing_bss->ie_data)
        {
          memcpy(new_ie_data, existing_bss->ie_data, existing_bss->ie_len);
        }

      if (new_bss->ie_data && new_bss->beacon_ie_len > 0)
        {
          memcpy(new_ie_data + existing_bss->ie_len,
                 new_bss->ie_data, new_bss->beacon_ie_len);
        }
    }

  if (existing_bss->ie_data)
    {
      free(existing_bss->ie_data);
    }

  existing_bss->ie_data = new_ie_data;
  existing_bss->ie_len = new_total_ie_len;
  existing_bss->beacon_ie_len = new_bss->beacon_ie_len;

  return 0;
}

/****************************************************************************
 * Name: replace_both_ie
 *
 * Description:
 *   Replace both probe response and beacon IE data
 *
 * Input Parameters:
 *   existing_bss - Existing BSS entry
 *   new_bss      - New BSS data with both IE types
 *
 * Returned Value:
 *   0 on success; negative error code on failure
 *
 ****************************************************************************/

static int replace_both_ie(FAR struct nl80211_scan_bss_s *existing_bss,
                           FAR struct nl80211_scan_bss_s *new_bss)
{
  size_t total_ie_len = new_bss->ie_len + new_bss->beacon_ie_len;
  FAR uint8_t *new_ie_data = NULL;

  if (total_ie_len > 0 && new_bss->ie_data)
    {
      new_ie_data = malloc(total_ie_len);
      if (!new_ie_data)
        {
          return -ENOMEM;
        }

      memcpy(new_ie_data, new_bss->ie_data, total_ie_len);
    }

  if (existing_bss->ie_data)
    {
      free(existing_bss->ie_data);
    }

  existing_bss->ie_data = new_ie_data;

  if (new_bss->ie_len == 0 && new_bss->beacon_ie_len > 0)
    {
      existing_bss->ie_len = new_bss->beacon_ie_len;
      existing_bss->beacon_ie_len = 0;
    }
  else
    {
      existing_bss->ie_len = new_bss->ie_len;
      existing_bss->beacon_ie_len = new_bss->beacon_ie_len;
    }

  return 0;
}

/****************************************************************************
 * Name: update_bss_ie_data
 *
 * Description:
 *   Update BSS IE data based on the type of new data received
 *
 * Input Parameters:
 *   existing_bss - Existing BSS entry to update
 *   new_bss      - New BSS data
 *
 * Returned Value:
 *   0 on success; negative error code on failure
 *
 ****************************************************************************/

static int update_bss_ie_data(FAR struct nl80211_scan_bss_s *existing_bss,
                              FAR struct nl80211_scan_bss_s *new_bss)
{
  bool is_probe_response =
      (new_bss->ie_len > 0 && new_bss->beacon_ie_len == 0);
  bool is_beacon = (new_bss->beacon_ie_len > 0 && new_bss->ie_len == 0);
  bool has_both = (new_bss->ie_len > 0 && new_bss->beacon_ie_len > 0);
  int ret = 0;

  if (is_probe_response)
    {
      ret = merge_probe_response_ie(existing_bss, new_bss);
    }
  else if (is_beacon)
    {
      ret = merge_beacon_ie(existing_bss, new_bss);
    }
  else if (has_both)
    {
      ret = replace_both_ie(existing_bss, new_bss);
    }
  else
    {
      nwarn("WARN: BSS update with no IE data\n");
    }

  return ret;
}

/****************************************************************************
 * Name: update_bss_basic_info
 *
 * Description:
 *   Update basic BSS information fields
 *
 * Input Parameters:
 *   existing_bss - Existing BSS entry to update
 *   new_bss      - New BSS data
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

static void update_bss_basic_info(
                          FAR struct nl80211_scan_bss_s *existing_bss,
                          FAR struct nl80211_scan_bss_s *new_bss)
{
  existing_bss->level = new_bss->level;
  existing_bss->freq = new_bss->freq;
  existing_bss->tsf = new_bss->tsf;
  existing_bss->beacon_int = new_bss->beacon_int;
  existing_bss->caps = new_bss->caps;
  existing_bss->ts = new_bss->ts;
}

/****************************************************************************
 * Name: reorder_bss_by_signal_strength
 *
 * Description:
 *   Reorder BSS list by signal strength (descending order)
 *
 * Input Parameters:
 *   results - Scan results storage
 *   index   - Index of modified BSS entry
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

static void reorder_bss_by_signal_strength(
              FAR struct nl80211_scan_results_s *results,
              int index)
{
  FAR struct nl80211_scan_bss_s *entry;

  if (index < 0 || index >= results->num)
    {
      return;
    }

  entry = results->bss[index];

  /* Move entry up if signal is stronger than previous entries */

  while (index > 0 && entry->level > results->bss[index - 1]->level)
    {
      results->bss[index] = results->bss[index - 1];
      index--;
    }

  /* Move entry down if signal is weaker than next entries */

  while (index < results->num - 1 &&
         entry->level < results->bss[index + 1]->level)
    {
      results->bss[index] = results->bss[index + 1];
      index++;
    }

  results->bss[index] = entry;
}

/****************************************************************************
 * Name: add_new_bss_entry
 *
 * Description:
 *   Add new BSS entry to storage. If storage is full, replace the weakest
 *   signal entry if new entry has stronger signal.
 *
 * Input Parameters:
 *   results - Scan results storage
 *   bss     - New BSS data
 *
 * Returned Value:
 *   0 on success; negative error code on failure
 *
 ****************************************************************************/

static int add_new_bss_entry(FAR struct nl80211_scan_results_s *results,
                             FAR struct nl80211_scan_bss_s *bss)
{
  FAR struct nl80211_scan_bss_s *new_entry;
  size_t total_ie_len;
  int insert_index;

  /* If storage is full, check if new entry has stronger signal than
   * the weakest entry (last entry in sorted list)
   */

  if (results->num >= MAX_SCAN_RESULTS)
    {
      /* List is sorted by signal strength (descending), so last entry
       * has weakest signal
       */

      if (bss->level <= results->bss[results->num - 1]->level)
        {
          /* New entry is weaker than or equal to weakest entry, discard */

          ninfo("Scan results full, discarding weaker BSS "
                "(level=%d <= %d)\n",
                bss->level, results->bss[results->num - 1]->level);
          return 0;
        }

      /* Replace weakest entry */

      ninfo("Scan results full, replacing weakest BSS "
            "(new level=%d > old level=%d)\n",
            bss->level, results->bss[results->num - 1]->level);

      if (results->bss[results->num - 1]->ie_data)
        {
          free(results->bss[results->num - 1]->ie_data);
        }

      free(results->bss[results->num - 1]);
      results->num--;
    }

  /* Allocate new entry */

  new_entry = malloc(sizeof(struct nl80211_scan_bss_s));
  if (!new_entry)
    {
      return -ENOMEM;
    }

  memcpy(new_entry, bss, sizeof(struct nl80211_scan_bss_s));

  total_ie_len = bss->ie_len + bss->beacon_ie_len;
  if (total_ie_len > 0 && bss->ie_data)
    {
      new_entry->ie_data = malloc(total_ie_len);
      if (!new_entry->ie_data)
        {
          free(new_entry);
          return -ENOMEM;
        }

      memcpy(new_entry->ie_data, bss->ie_data, total_ie_len);
    }
  else
    {
      new_entry->ie_data = NULL;
    }

  /* Find insertion position to maintain descending order by signal
   * strength
   */

  insert_index = results->num;
  while (insert_index > 0 &&
         new_entry->level > results->bss[insert_index - 1]->level)
    {
      insert_index--;
    }

  /* Shift entries to make room for new entry */

  for (int i = results->num; i > insert_index; i--)
    {
      results->bss[i] = results->bss[i - 1];
    }

  results->bss[insert_index] = new_entry;
  results->num++;

  return 0;
}

/****************************************************************************
 * Name: scan_results_clear
 *
 * Description:
 *   Clear scan results storage
 *
 * Input Parameters:
 *   results - Scan results storage
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

static void scan_results_clear(FAR struct nl80211_scan_results_s *results)
{
  if (results->bss)
    {
      for (int i = 0; i < results->num; i++)
        {
          if (results->bss[i])
            {
              if (results->bss[i]->ie_data)
                {
                  free(results->bss[i]->ie_data);
                }

              free(results->bss[i]);
              results->bss[i] = NULL;
            }
        }

      results->num = 0;
    }
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: nl80211_driver_event_scan_start
 *
 * Description:
 *   Driver calls this function to notify scan start
 *
 * Input Parameters:
 *   drv_priv - Driver private data
 *
 * Returned Value:
 *   0 on success; negative error code on failure
 *
 ****************************************************************************/

int nl80211_driver_event_scan_start(FAR struct net_driver_s *dev)
{
  if (!dev)
    {
      nerr("ERROR: dev is NULL\n");
      return -ENODEV;
    }

  /* Clear previous scan results */

  nxmutex_lock(&g_scan_lock);
  scan_results_clear(&g_scan_results);
  nxmutex_unlock(&g_scan_lock);

  /* Send scan start event via Netlink */

  nl80211_send_scan_start(dev);

  return 0;
}

/****************************************************************************
 * Name: nl80211_driver_event_connect
 *
 * Description:
 *   Driver calls this function to provide connection success information
 *
 * Input Parameters:
 *   drv_priv     - Driver private data
 *   bssid        - BSSID address
 *   req_ies      - Request IEs
 *   req_ies_len  - Request IEs length
 *   resp_ies     - Response IEs
 *   resp_ies_len - Response IEs length
 *   subnet_status - Subnet status
 *   freq         - Frequency
 *   info_bitmap  - Information bitmap
 *
 * Returned Value:
 *   0 on success; negative error code on failure
 *
 ****************************************************************************/

int nl80211_driver_event_connect(FAR struct net_driver_s *dev,
                                 FAR const uint8_t *bssid,
                                 FAR const uint8_t *req_ies,
                                 size_t req_ies_len,
                                 FAR const uint8_t *resp_ies,
                                 size_t resp_ies_len,
                                 uint8_t subnet_status,
                                 uint32_t freq,
                                 uint8_t info_bitmap)
{
  FAR struct netlink_response_s *msg;
  FAR uint8_t *buf;
  FAR uint8_t *ptr;
  size_t attr_len;
  size_t total_len;
  bool is_ap_mode = false;

  if (!dev)
    {
      nerr("ERROR: dev is NULL\n");
      return -ENODEV;
    }

  /* Determine AP mode by checking interface type */

  if (dev->d_wdev && dev->d_wdev->iftype == NL80211_IFTYPE_AP)
    {
      is_ap_mode = true;
      ninfo("nl80211_driver_event_connect: detected AP mode\n");
    }

  /* For AP mode, send NL80211_CMD_NEW_STATION event instead of
   * NL80211_CMD_CONNECT. When device_ap_sme is active, the firmware
   * handles AUTH/ASSOC internally and fires connect_result for AP mode.
   * wpa_supplicant expects NL80211_CMD_NEW_STATION on the mlme
   * multicast group to trigger drv_event_assoc().
   */

  if (is_ap_mode && bssid)
    {
      ninfo("nl80211_driver_event_connect: AP mode - sending "
            "NL80211_CMD_NEW_STATION for %02x:%02x:%02x:%02x:%02x:%02x\n",
            bssid[0], bssid[1], bssid[2], bssid[3], bssid[4], bssid[5]);
      return nl80211_driver_event_new_station(bssid, req_ies,
                                              req_ies_len, dev->d_ifname);
    }

  /* Calculate total attribute size:
   * NL80211_ATTR_IFINDEX (u32) + NL80211_ATTR_MAC (6) +
   * NL80211_ATTR_STATUS_CODE (u16) +
   * NL80211_ATTR_REQ_IE (variable) + NL80211_ATTR_RESP_IE (variable)
   */

  attr_len = RTA_SPACE(sizeof(uint32_t))   /* IFINDEX */
           + RTA_SPACE(ETH_ALEN)           /* MAC */
           + RTA_SPACE(sizeof(uint16_t));  /* STATUS_CODE */

  if (req_ies && req_ies_len > 0)
    {
      attr_len += RTA_SPACE(req_ies_len);
    }

  if (resp_ies && resp_ies_len > 0)
    {
      attr_len += RTA_SPACE(resp_ies_len);
    }

  total_len = NLMSG_LENGTH(GENL_HDRLEN + attr_len);
  msg = kmm_zalloc(sizeof(struct netlink_response_s) + total_len);
  if (!msg)
    {
      return -ENOMEM;
    }

  /* Fill nlmsghdr */

  msg->msg.nlmsg_len = total_len;
  msg->msg.nlmsg_type = nl80211_genl_family_id();
  msg->msg.nlmsg_flags = 0;
  msg->msg.nlmsg_seq = 0;
  msg->msg.nlmsg_pid = 0;

  /* Fill genlmsghdr */

  buf = (FAR uint8_t *)NLMSG_DATA(&msg->msg);
  FAR struct genlmsghdr *gnlh = (FAR struct genlmsghdr *)buf;
  gnlh->cmd = NL80211_CMD_CONNECT;
  gnlh->version = 1;

  ptr = buf + GENL_HDRLEN;

  /* NL80211_ATTR_IFINDEX */

  FAR struct rtattr *rta = (FAR struct rtattr *)ptr;
  rta->rta_type = NL80211_ATTR_IFINDEX;
  rta->rta_len = RTA_LENGTH(sizeof(uint32_t));
  *(FAR uint32_t *)RTA_DATA(rta) = dev->d_ifindex;
  ptr += RTA_SPACE(sizeof(uint32_t));

  /* NL80211_ATTR_STATUS_CODE = 0 (success) */

  rta = (FAR struct rtattr *)ptr;
  rta->rta_type = NL80211_ATTR_STATUS_CODE;
  rta->rta_len = RTA_LENGTH(sizeof(uint16_t));
  *(FAR uint16_t *)RTA_DATA(rta) = 0;
  ptr += RTA_SPACE(sizeof(uint16_t));

  /* NL80211_ATTR_MAC */

  if (bssid)
    {
      rta = (FAR struct rtattr *)ptr;
      rta->rta_type = NL80211_ATTR_MAC;
      rta->rta_len = RTA_LENGTH(ETH_ALEN);
      memcpy(RTA_DATA(rta), bssid, ETH_ALEN);
      ptr += RTA_SPACE(ETH_ALEN);
    }

  /* NL80211_ATTR_REQ_IE */

  if (req_ies && req_ies_len > 0)
    {
      rta = (FAR struct rtattr *)ptr;
      rta->rta_type = NL80211_ATTR_REQ_IE;
      rta->rta_len = RTA_LENGTH(req_ies_len);
      memcpy(RTA_DATA(rta), req_ies, req_ies_len);
      ptr += RTA_SPACE(req_ies_len);
    }

  /* NL80211_ATTR_RESP_IE */

  if (resp_ies && resp_ies_len > 0)
    {
      rta = (FAR struct rtattr *)ptr;
      rta->rta_type = NL80211_ATTR_RESP_IE;
      rta->rta_len = RTA_LENGTH(resp_ies_len);
      memcpy(RTA_DATA(rta), resp_ies, resp_ies_len);
      ptr += RTA_SPACE(resp_ies_len);
    }

  ninfo("nl80211_driver_event_connect: broadcasting "
        "NL80211_CMD_CONNECT event, len=%d\n", total_len);

  netlink_add_broadcast(NETLINK_GENERIC,
                        nl80211_genl_mcgrp_id(1), msg);
  return 0;
}

/****************************************************************************
 * Name: nl80211_driver_event_connect_failed
 *
 * Description:
 *   Driver calls this function to provide connection reject information
 *
 * Input Parameters:
 *   drv_priv      - Driver private data
 *   bssid         - BSSID address
 *   resp_ies      - Response IEs
 *   resp_ies_len  - Response IEs length
 *   status_code   - Status code
 *   timed_out     - Timeout flag
 *
 * Returned Value:
 *   0 on success; negative error code on failure
 *
 ****************************************************************************/

int nl80211_driver_event_connect_failed(FAR struct net_driver_s *dev,
                                  FAR const uint8_t *bssid,
                                  FAR const uint8_t *resp_ies,
                                  size_t resp_ies_len,
                                  uint16_t status_code,
                                  int timed_out)
{
  FAR struct netlink_response_s *msg;
  FAR uint8_t *buf;
  FAR uint8_t *ptr;
  size_t attr_len;
  size_t total_len;

  if (!dev)
    {
      nerr("ERROR: dev is NULL\n");
      return -ENODEV;
    }

  /* In AP mode with device_ap_sme, firmware handles AUTH/ASSOC internally
   * and fires connect_result for both success and failure. For failures,
   * we should silently ignore as there's no standard nl80211 event for
   * "station failed to associate with AP".
   */

  if (dev->d_wdev && dev->d_wdev->iftype == NL80211_IFTYPE_AP)
    {
      ninfo("nl80211_driver_event_connect_failed: AP mode - "
            "ignoring station association failure, status=%d\n",
            status_code);
      return 0;
    }

  attr_len = RTA_SPACE(sizeof(uint32_t))   /* IFINDEX */
           + RTA_SPACE(ETH_ALEN)           /* MAC */
           + RTA_SPACE(sizeof(uint16_t));  /* STATUS_CODE */

  if (resp_ies && resp_ies_len > 0)
    {
      attr_len += RTA_SPACE(resp_ies_len);
    }

  if (timed_out)
    {
      attr_len += RTA_SPACE(0); /* TIMED_OUT flag */
    }

  total_len = NLMSG_LENGTH(GENL_HDRLEN + attr_len);
  msg = kmm_zalloc(sizeof(struct netlink_response_s) + total_len);
  if (!msg)
    {
      return -ENOMEM;
    }

  msg->msg.nlmsg_len = total_len;
  msg->msg.nlmsg_type = nl80211_genl_family_id();
  msg->msg.nlmsg_flags = 0;
  msg->msg.nlmsg_seq = 0;
  msg->msg.nlmsg_pid = 0;

  buf = (FAR uint8_t *)NLMSG_DATA(&msg->msg);
  FAR struct genlmsghdr *gnlh = (FAR struct genlmsghdr *)buf;
  gnlh->cmd = NL80211_CMD_CONNECT;
  gnlh->version = 1;

  ptr = buf + GENL_HDRLEN;

  /* NL80211_ATTR_IFINDEX */

  FAR struct rtattr *rta = (FAR struct rtattr *)ptr;
  rta->rta_type = NL80211_ATTR_IFINDEX;
  rta->rta_len = RTA_LENGTH(sizeof(uint32_t));
  *(FAR uint32_t *)RTA_DATA(rta) = dev->d_ifindex;
  ptr += RTA_SPACE(sizeof(uint32_t));

  /* NL80211_ATTR_STATUS_CODE */

  rta = (FAR struct rtattr *)ptr;
  rta->rta_type = NL80211_ATTR_STATUS_CODE;
  rta->rta_len = RTA_LENGTH(sizeof(uint16_t));
  *(FAR uint16_t *)RTA_DATA(rta) = status_code;
  ptr += RTA_SPACE(sizeof(uint16_t));

  /* NL80211_ATTR_MAC */

  if (bssid)
    {
      rta = (FAR struct rtattr *)ptr;
      rta->rta_type = NL80211_ATTR_MAC;
      rta->rta_len = RTA_LENGTH(ETH_ALEN);
      memcpy(RTA_DATA(rta), bssid, ETH_ALEN);
      ptr += RTA_SPACE(ETH_ALEN);
    }

  /* NL80211_ATTR_RESP_IE */

  if (resp_ies && resp_ies_len > 0)
    {
      rta = (FAR struct rtattr *)ptr;
      rta->rta_type = NL80211_ATTR_RESP_IE;
      rta->rta_len = RTA_LENGTH(resp_ies_len);
      memcpy(RTA_DATA(rta), resp_ies, resp_ies_len);
      ptr += RTA_SPACE(resp_ies_len);
    }

  /* NL80211_ATTR_TIMED_OUT */

  if (timed_out)
    {
      rta = (FAR struct rtattr *)ptr;
      rta->rta_type = NL80211_ATTR_TIMED_OUT;
      rta->rta_len = RTA_LENGTH(0);
      ptr += RTA_SPACE(0);
    }

  ninfo("nl80211_driver_event_connect_failed: broadcasting "
        "NL80211_CMD_CONNECT reject event, status=%d\n", status_code);

  netlink_add_broadcast(NETLINK_GENERIC,
                        nl80211_genl_mcgrp_id(1), msg);
  return 0;
}

/****************************************************************************
 * Name: nl80211_driver_event_disconnect
 *
 * Description:
 *   Driver calls this function to provide disassociation event
 *
 * Input Parameters:
 *   drv_priv      - Driver private data
 *   bssid         - BSSID address
 *   reason_code   - Reason code
 *
 * Returned Value:
 *   0 on success; negative error code on failure
 *
 ****************************************************************************/

int nl80211_driver_event_disconnect(FAR struct net_driver_s *dev,
                                  FAR const uint8_t *bssid,
                                  uint16_t reason_code)
{
  FAR struct netlink_response_s *msg;
  FAR uint8_t *buf;
  FAR uint8_t *ptr;
  size_t attr_len;
  size_t total_len;

  if (!dev)
    {
      nerr("ERROR: dev is NULL\n");
      return -ENODEV;
    }

  /* Determine AP mode by checking interface type */

  if (dev->d_wdev && dev->d_wdev->iftype == NL80211_IFTYPE_AP)
    {
      /* AP mode: send NL80211_CMD_DEL_STATION instead of DISCONNECT */

      ninfo("nl80211_driver_event_disconnect: AP mode "
            "reason=%d - sending DEL_STATION\n", reason_code);

      attr_len = RTA_SPACE(sizeof(uint32_t))   /* IFINDEX */
               + RTA_SPACE(6)                  /* MAC */
               + RTA_SPACE(sizeof(uint16_t));  /* REASON_CODE */

      total_len = NLMSG_LENGTH(GENL_HDRLEN + attr_len);
      msg = kmm_zalloc(sizeof(struct netlink_response_s) + total_len);
      if (!msg)
        {
          return -ENOMEM;
        }

      msg->msg.nlmsg_len = total_len;
      msg->msg.nlmsg_type = nl80211_genl_family_id();
      msg->msg.nlmsg_flags = 0;
      msg->msg.nlmsg_seq = 0;
      msg->msg.nlmsg_pid = 0;

      buf = (FAR uint8_t *)NLMSG_DATA(&msg->msg);
      FAR struct genlmsghdr *gnlh = (FAR struct genlmsghdr *)buf;
      gnlh->cmd = NL80211_CMD_DEL_STATION;
      gnlh->version = 1;

      ptr = buf + GENL_HDRLEN;

      FAR struct rtattr *rta = (FAR struct rtattr *)ptr;
      rta->rta_type = NL80211_ATTR_IFINDEX;
      rta->rta_len = RTA_LENGTH(sizeof(uint32_t));
      *(FAR uint32_t *)RTA_DATA(rta) = dev->d_ifindex;
      ptr += RTA_SPACE(sizeof(uint32_t));

      if (bssid)
        {
          rta = (FAR struct rtattr *)ptr;
          rta->rta_type = NL80211_ATTR_MAC;
          rta->rta_len = RTA_LENGTH(6);
          memcpy(RTA_DATA(rta), bssid, 6);
          ptr += RTA_SPACE(6);
        }

      rta = (FAR struct rtattr *)ptr;
      rta->rta_type = NL80211_ATTR_REASON_CODE;
      rta->rta_len = RTA_LENGTH(sizeof(uint16_t));
      *(FAR uint16_t *)RTA_DATA(rta) = reason_code;
      ptr += RTA_SPACE(sizeof(uint16_t));

      netlink_add_broadcast(NETLINK_GENERIC,
                            nl80211_genl_mcgrp_id(1), msg);
      return 0;
    }

  attr_len = RTA_SPACE(sizeof(uint32_t))   /* IFINDEX */
           + RTA_SPACE(sizeof(uint16_t))   /* REASON_CODE */
           + RTA_SPACE(0);                 /* DISCONNECTED_BY_AP flag */

  total_len = NLMSG_LENGTH(GENL_HDRLEN + attr_len);
  msg = kmm_zalloc(sizeof(struct netlink_response_s) + total_len);
  if (!msg)
    {
      return -ENOMEM;
    }

  msg->msg.nlmsg_len = total_len;
  msg->msg.nlmsg_type = nl80211_genl_family_id();
  msg->msg.nlmsg_flags = 0;
  msg->msg.nlmsg_seq = 0;
  msg->msg.nlmsg_pid = 0;

  buf = (FAR uint8_t *)NLMSG_DATA(&msg->msg);
  FAR struct genlmsghdr *gnlh = (FAR struct genlmsghdr *)buf;
  gnlh->cmd = NL80211_CMD_DISCONNECT;
  gnlh->version = 1;

  ptr = buf + GENL_HDRLEN;

  /* NL80211_ATTR_IFINDEX */

  FAR struct rtattr *rta = (FAR struct rtattr *)ptr;
  rta->rta_type = NL80211_ATTR_IFINDEX;
  rta->rta_len = RTA_LENGTH(sizeof(uint32_t));
  *(FAR uint32_t *)RTA_DATA(rta) = dev->d_ifindex;
  ptr += RTA_SPACE(sizeof(uint32_t));

  /* NL80211_ATTR_REASON_CODE */

  rta = (FAR struct rtattr *)ptr;
  rta->rta_type = NL80211_ATTR_REASON_CODE;
  rta->rta_len = RTA_LENGTH(sizeof(uint16_t));
  *(FAR uint16_t *)RTA_DATA(rta) = reason_code;
  ptr += RTA_SPACE(sizeof(uint16_t));

  /* NL80211_ATTR_DISCONNECTED_BY_AP - always set since this comes
   * from the driver (AP-initiated or firmware-initiated disconnect)
   */

  rta = (FAR struct rtattr *)ptr;
  rta->rta_type = NL80211_ATTR_DISCONNECTED_BY_AP;
  rta->rta_len = RTA_LENGTH(0);
  ptr += RTA_SPACE(0);

  ninfo("nl80211_driver_event_disconnect: broadcasting "
        "NL80211_CMD_DISCONNECT event, reason=%d\n", reason_code);

  netlink_add_broadcast(NETLINK_GENERIC,
                        nl80211_genl_mcgrp_id(1), msg);
  return 0;
}

/****************************************************************************
 * Name: nl80211_driver_event_scan_result
 *
 * Description:
 *   Driver calls this function to provide individual scan results
 *
 * Input Parameters:
 *   drv_priv - Driver private data
 *   bss      - Individual BSS information
 *
 * Returned Value:
 *   0 on success; negative error code on failure
 *
 ****************************************************************************/

int nl80211_driver_event_scan_result(FAR struct net_driver_s *dev,
                                FAR struct nl80211_scan_bss_s *bss)
{
  int ret;

  if (!bss)
    {
      nerr("ERROR: Invalid parameters\n");
      return -EINVAL;
    }

  nxmutex_lock(&g_scan_lock);

  /* Initialize scan results storage if needed */

  ret = scan_results_init(&g_scan_results);
  if (ret < 0)
    {
      goto out;
    }

  /* Look for existing BSS entry with same BSSID */

  int existing_index = find_existing_bss_index(&g_scan_results, bss);

  if (existing_index >= 0)
    {
      /* Update existing BSS entry */

      FAR struct nl80211_scan_bss_s *existing_bss =
          g_scan_results.bss[existing_index];

      /* Update IE data */

      ret = update_bss_ie_data(existing_bss, bss);
      if (ret < 0)
        {
          goto out;
        }

      /* Update basic BSS information */

      update_bss_basic_info(existing_bss, bss);

      /* Reorder based on new signal strength */

      reorder_bss_by_signal_strength(&g_scan_results, existing_index);
    }
  else
    {
      /* Add new BSS entry */

      ret = add_new_bss_entry(&g_scan_results, bss);
      if (ret < 0)
        {
          goto out;
        }
    }

  g_scan_results.fetch_time = 0; /* Can be set to current time */
  ret = 0;

out:
  nxmutex_unlock(&g_scan_lock);
  return ret;
}

/****************************************************************************
 * Name: nl80211_get_scan_results
 *
 * Description:
 *   Get stored scan results
 *
 * Input Parameters:
 *   bss_list - Pointer to store the BSS list pointer
 *   num      - Pointer to store the number of BSS entries
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

void nl80211_get_scan_results(FAR struct nl80211_scan_bss_s ***bss_list,
                              FAR int *num)
{
  if (bss_list && num)
    {
      nxmutex_lock(&g_scan_lock);
      ninfo("nl80211_get_scan_results: bss_list %p, g_scan_results.num %d\n",
            bss_list, g_scan_results.num);
      *bss_list = g_scan_results.bss;
      *num = g_scan_results.num;
      nxmutex_unlock(&g_scan_lock);
    }
}

/****************************************************************************
 * Name: nl80211_driver_event_scan_finish
 *
 * Description:
 *   Driver calls this function to notify scan completion
 *
 * Input Parameters:
 *   drv_priv - Driver private data
 *   aborted  - Whether scan was aborted
 *
 * Returned Value:
 *   0 on success; negative error code on failure
 *
 ****************************************************************************/

int nl80211_driver_event_scan_finish(FAR struct net_driver_s *dev,
                                     bool aborted)
{
  if (!dev)
    {
      nerr("ERROR: dev is NULL\n");
      return -ENODEV;
    }

  /* Send scan results event via Netlink */

  nl80211_send_scan_results(dev);

  return 0;
}

/****************************************************************************
 * Name: nl80211_driver_event_external_auth
 *
 * Description:
 *   Driver reports external authentication event
 *
 * Input Parameters:
 *   drv_priv        - nl80211 context pointer
 *   action          - Authentication action (EXT_AUTH_START/EXT_AUTH_ABORT)
 *   bssid           - BSSID address
 *   ssid            - SSID
 *   ssid_len        - SSID length
 *   key_mgmt_suite  - Key management suite
 *   status          - Authentication status code
 *   pmkid           - PMKID
 *
 * Returned Value:
 *   0 on success; negative error code on failure
 *
 ****************************************************************************/

int nl80211_driver_event_external_auth(FAR struct net_driver_s *dev,
                                       int action,
                                       FAR const uint8_t *bssid,
                                       FAR const uint8_t *ssid,
                                       size_t ssid_len,
                                       uint32_t key_mgmt_suite,
                                       uint16_t status,
                                       FAR const uint8_t *pmkid)
{
  FAR struct netlink_response_s *msg;
  FAR uint8_t *buf;
  FAR uint8_t *ptr;
  size_t attr_len;
  size_t total_len;
  int ret;

  if (!dev)
    {
      nerr("ERROR: dev is NULL\n");
      return -ENODEV;
    }

  /* Calculate total attribute size */

  attr_len = RTA_SPACE(sizeof(uint32_t))   /* IFINDEX */
           + RTA_SPACE(sizeof(uint32_t))   /* EXTERNAL_AUTH_ACTION */
           + RTA_SPACE(sizeof(uint32_t))   /* AKM_SUITES */
           + RTA_SPACE(ETH_ALEN);          /* BSSID */

  if (ssid && ssid_len > 0)
    {
      attr_len += RTA_SPACE(ssid_len);     /* SSID */
    }

  if (pmkid)
    {
      attr_len += RTA_SPACE(16);           /* PMKID */
    }

  total_len = NLMSG_LENGTH(GENL_HDRLEN + attr_len);
  msg = kmm_zalloc(sizeof(struct netlink_response_s) + total_len);
  if (!msg)
    {
      return -ENOMEM;
    }

  /* Fill nlmsghdr */

  msg->msg.nlmsg_len = total_len;
  msg->msg.nlmsg_type = nl80211_genl_family_id();
  msg->msg.nlmsg_flags = 0;
  msg->msg.nlmsg_seq = 0;
  msg->msg.nlmsg_pid = 0;

  /* Fill genlmsghdr */

  buf = (FAR uint8_t *)NLMSG_DATA(&msg->msg);
  FAR struct genlmsghdr *gnlh = (FAR struct genlmsghdr *)buf;
  gnlh->cmd = NL80211_CMD_EXTERNAL_AUTH;
  gnlh->version = 1;

  ptr = buf + GENL_HDRLEN;

  /* NL80211_ATTR_IFINDEX */

  FAR struct rtattr *rta = (FAR struct rtattr *)ptr;
  rta->rta_type = NL80211_ATTR_IFINDEX;
  rta->rta_len = RTA_LENGTH(sizeof(uint32_t));
  *(FAR uint32_t *)RTA_DATA(rta) = dev->d_ifindex;
  ptr += RTA_SPACE(sizeof(uint32_t));

  /* NL80211_ATTR_EXTERNAL_AUTH_ACTION */

  rta = (FAR struct rtattr *)ptr;
  rta->rta_type = NL80211_ATTR_EXTERNAL_AUTH_ACTION;
  rta->rta_len = RTA_LENGTH(sizeof(uint32_t));
  *(FAR uint32_t *)RTA_DATA(rta) = (uint32_t)action;
  ptr += RTA_SPACE(sizeof(uint32_t));

  /* NL80211_ATTR_AKM_SUITES */

  rta = (FAR struct rtattr *)ptr;
  rta->rta_type = NL80211_ATTR_AKM_SUITES;
  rta->rta_len = RTA_LENGTH(sizeof(uint32_t));
  *(FAR uint32_t *)RTA_DATA(rta) = key_mgmt_suite;
  ptr += RTA_SPACE(sizeof(uint32_t));

  /* NL80211_ATTR_BSSID */

  if (bssid)
    {
      rta = (FAR struct rtattr *)ptr;
      rta->rta_type = NL80211_ATTR_BSSID;
      rta->rta_len = RTA_LENGTH(ETH_ALEN);
      memcpy(RTA_DATA(rta), bssid, ETH_ALEN);
      ptr += RTA_SPACE(ETH_ALEN);
    }

  /* NL80211_ATTR_SSID */

  if (ssid && ssid_len > 0)
    {
      rta = (FAR struct rtattr *)ptr;
      rta->rta_type = NL80211_ATTR_SSID;
      rta->rta_len = RTA_LENGTH(ssid_len);
      memcpy(RTA_DATA(rta), ssid, ssid_len);
      ptr += RTA_SPACE(ssid_len);
    }

  /* NL80211_ATTR_PMKID */

  if (pmkid)
    {
      rta = (FAR struct rtattr *)ptr;
      rta->rta_type = NL80211_ATTR_PMKID;
      rta->rta_len = RTA_LENGTH(16);
      memcpy(RTA_DATA(rta), pmkid, 16);
      ptr += RTA_SPACE(16);
    }

  ninfo("sending NL80211_CMD_EXTERNAL_AUTH event, "
        "action=%d, akm=0x%lx\n", action, (unsigned long)key_mgmt_suite);

  ret = nl80211_send_to_bss_conn(msg, dev);
  return ret;
}

/****************************************************************************
 * Name: rx_mgmt_find_dev
 *
 * Description:
 *   Determine the correct network interface for a received management
 *   frame by matching the frame's DA (A1) and BSSID against interface
 *   MAC addresses.
 *
 ****************************************************************************/

int nl80211_driver_event_mgmt_rx(FAR struct net_driver_s *dev,
                                 FAR const uint8_t *frame,
                                 size_t frame_len,
                                 uint32_t freq,
                                 int ssi_signal)
{
  FAR struct netlink_response_s *msg;
  FAR uint8_t *buf;
  FAR uint8_t *ptr;
  size_t attr_len;
  size_t total_len;
  uint16_t fc = 0;
  int stype = -1;
  static const char * const stype_names[] =
    {
      "ASSOC_REQ", "ASSOC_RESP", "REASSOC_REQ", "REASSOC_RESP",
      "PROBE_REQ", "PROBE_RESP", "TIMING_ADV", "RESERVED7",
      "BEACON", "ATIM", "DISASSOC", "AUTH",
      "DEAUTH", "ACTION", "ACTION_NOACK", "RESERVED15"
    };

  /* First thing: log the frame type before touching any lock/alloc.
   * This lets us confirm whether BES firmware delivered the frame at all.
   */

  if (frame && frame_len >= 2)
    {
      fc = frame[0] | (frame[1] << 8);
      stype = (fc >> 4) & 0xf;
      ninfo("rx_mgmt_enter: fc=0x%04x stype=%d(%s) len=%zu freq=%lu\n",
            fc, stype, stype_names[stype & 0xf], frame_len,
            (unsigned long)freq);
    }
  else
    {
      ninfo("rx_mgmt_enter: frame=%p len=%zu\n", frame, frame_len);
    }

  if (!dev)
    {
      nerr("ERROR: rx_mgmt: dev is NULL\n");
      return -ENODEV;
    }

  ninfo("rx_mgmt: matched dev=%s ifindex=%d\n",
        dev->d_ifname, dev->d_ifindex);

  attr_len = RTA_SPACE(sizeof(uint32_t))   /* IFINDEX */
           + RTA_SPACE(sizeof(uint32_t))   /* WIPHY_FREQ */
           + RTA_SPACE(frame_len)          /* FRAME */
           + RTA_SPACE(sizeof(uint32_t));  /* RX_SIGNAL_DBM */

  total_len = NLMSG_LENGTH(GENL_HDRLEN + attr_len);
  msg = kmm_zalloc(sizeof(struct netlink_response_s) + total_len);
  if (!msg)
    {
      nerr("ERROR: rx_mgmt: alloc failed (%zu bytes)\n",
           sizeof(struct netlink_response_s) + total_len);
      return -ENOMEM;
    }

  msg->msg.nlmsg_len = total_len;
  msg->msg.nlmsg_type = nl80211_genl_family_id();
  msg->msg.nlmsg_flags = 0;
  msg->msg.nlmsg_seq = 0;
  msg->msg.nlmsg_pid = 0;

  /* Write via the declared flexible array member (same address as
   * NLMSG_DATA(&msg->msg)) so the genlmsghdr/attr writes below stay in
   * bounds of a declared object (Coverity OVERRUN).
   */

  buf = msg->data;
  FAR struct genlmsghdr *gnlh = (FAR struct genlmsghdr *)buf;
  gnlh->cmd = NL80211_CMD_FRAME;
  gnlh->version = 1;

  ptr = buf + GENL_HDRLEN;

  FAR struct rtattr *rta = (FAR struct rtattr *)ptr;
  rta->rta_type = NL80211_ATTR_IFINDEX;
  rta->rta_len = RTA_LENGTH(sizeof(uint32_t));
  *(FAR uint32_t *)RTA_DATA(rta) = dev->d_ifindex;
  ptr += RTA_SPACE(sizeof(uint32_t));

  rta = (FAR struct rtattr *)ptr;
  rta->rta_type = NL80211_ATTR_WIPHY_FREQ;
  rta->rta_len = RTA_LENGTH(sizeof(uint32_t));
  *(FAR uint32_t *)RTA_DATA(rta) = freq;
  ptr += RTA_SPACE(sizeof(uint32_t));

  rta = (FAR struct rtattr *)ptr;
  rta->rta_type = NL80211_ATTR_RX_SIGNAL_DBM;
  rta->rta_len = RTA_LENGTH(sizeof(uint32_t));
  *(FAR uint32_t *)RTA_DATA(rta) = (uint32_t)ssi_signal;
  ptr += RTA_SPACE(sizeof(uint32_t));

  rta = (FAR struct rtattr *)ptr;
  rta->rta_type = NL80211_ATTR_FRAME;
  rta->rta_len = RTA_LENGTH(frame_len);
  memcpy(RTA_DATA(rta), frame, frame_len);
  ptr += RTA_SPACE(frame_len);

  ninfo("rx_mgmt: sending CMD_FRAME to bss_conn, freq=%lu, len=%zu\n",
        (unsigned long)freq, frame_len);

  nl80211_send_to_bss_conn(msg, dev);
  return 0;
}

/****************************************************************************
 * Name: nl80211_driver_event_remain_on_channel
 *
 * Description:
 *   Driver reports remain on channel event
 *
 * Input Parameters:
 *   drv_priv - nl80211 context pointer
 *   freq     - Frequency
 *   duration - Duration
 *
 * Returned Value:
 *   0 on success; negative error code on failure
 *
 ****************************************************************************/

int nl80211_driver_event_remain_on_channel(FAR struct net_driver_s *dev,
                                           uint32_t freq,
                                           uint32_t duration,
                                           uint64_t cookie)
{
  FAR struct netlink_response_s *msg;
  FAR uint8_t *buf;
  FAR uint8_t *ptr;
  size_t attr_len;
  size_t total_len;

  if (!dev)
    {
      nerr("ERROR: dev is NULL\n");
      return -ENODEV;
    }

  /* NL80211_ATTR_IFINDEX(u32) + WIPHY_FREQ(u32) + DURATION(u32)
   * + COOKIE(u64)
   */

  attr_len = RTA_SPACE(sizeof(uint32_t))
           + RTA_SPACE(sizeof(uint32_t))
           + RTA_SPACE(sizeof(uint32_t))
           + RTA_SPACE(sizeof(uint64_t));

  total_len = NLMSG_LENGTH(GENL_HDRLEN + attr_len);
  msg = kmm_zalloc(sizeof(struct netlink_response_s) + total_len);
  if (!msg)
    {
      return -ENOMEM;
    }

  msg->msg.nlmsg_len = total_len;
  msg->msg.nlmsg_type = nl80211_genl_family_id();
  msg->msg.nlmsg_flags = 0;
  msg->msg.nlmsg_seq = 0;
  msg->msg.nlmsg_pid = 0;

  buf = (FAR uint8_t *)NLMSG_DATA(&msg->msg);
  FAR struct genlmsghdr *gnlh = (FAR struct genlmsghdr *)buf;
  gnlh->cmd = NL80211_CMD_REMAIN_ON_CHANNEL;
  gnlh->version = 1;

  ptr = buf + GENL_HDRLEN;

  FAR struct rtattr *rta = (FAR struct rtattr *)ptr;
  rta->rta_type = NL80211_ATTR_IFINDEX;
  rta->rta_len = RTA_LENGTH(sizeof(uint32_t));
  *(FAR uint32_t *)RTA_DATA(rta) = dev->d_ifindex;
  ptr += RTA_SPACE(sizeof(uint32_t));

  rta = (FAR struct rtattr *)ptr;
  rta->rta_type = NL80211_ATTR_WIPHY_FREQ;
  rta->rta_len = RTA_LENGTH(sizeof(uint32_t));
  *(FAR uint32_t *)RTA_DATA(rta) = freq;
  ptr += RTA_SPACE(sizeof(uint32_t));

  rta = (FAR struct rtattr *)ptr;
  rta->rta_type = NL80211_ATTR_DURATION;
  rta->rta_len = RTA_LENGTH(sizeof(uint32_t));
  *(FAR uint32_t *)RTA_DATA(rta) = duration;
  ptr += RTA_SPACE(sizeof(uint32_t));

  rta = (FAR struct rtattr *)ptr;
  rta->rta_type = NL80211_ATTR_COOKIE;
  rta->rta_len = RTA_LENGTH(sizeof(uint64_t));
  *(FAR uint64_t *)RTA_DATA(rta) = cookie;
  ptr += RTA_SPACE(sizeof(uint64_t));

  netlink_add_broadcast(NETLINK_GENERIC,
                        nl80211_genl_mcgrp_id(1), msg);
  return 0;
}

/****************************************************************************
 * Name: nl80211_driver_event_cancel_remain_on_channel
 *
 * Description:
 *   Driver reports cancel remain on channel event
 *
 * Input Parameters:
 *   drv_priv - nl80211 context pointer
 *
 * Returned Value:
 *   0 on success; negative error code on failure
 *
 ****************************************************************************/

int nl80211_driver_event_cancel_remain_on_channel(
      FAR struct net_driver_s *dev,
      uint64_t cookie)
{
  FAR struct netlink_response_s *msg;
  FAR uint8_t *buf;
  FAR uint8_t *ptr;
  size_t attr_len;
  size_t total_len;

  if (!dev)
    {
      nerr("ERROR: dev is NULL\n");
      return -ENODEV;
    }

  /* NL80211_ATTR_IFINDEX(u32) + WIPHY_FREQ(u32) + COOKIE(u64) */

  attr_len = RTA_SPACE(sizeof(uint32_t))
           + RTA_SPACE(sizeof(uint32_t))
           + RTA_SPACE(sizeof(uint64_t));

  total_len = NLMSG_LENGTH(GENL_HDRLEN + attr_len);
  msg = kmm_zalloc(sizeof(struct netlink_response_s) + total_len);
  if (!msg)
    {
      return -ENOMEM;
    }

  msg->msg.nlmsg_len = total_len;
  msg->msg.nlmsg_type = nl80211_genl_family_id();
  msg->msg.nlmsg_flags = 0;
  msg->msg.nlmsg_seq = 0;
  msg->msg.nlmsg_pid = 0;

  buf = (FAR uint8_t *)NLMSG_DATA(&msg->msg);
  FAR struct genlmsghdr *gnlh = (FAR struct genlmsghdr *)buf;
  gnlh->cmd = NL80211_CMD_CANCEL_REMAIN_ON_CHANNEL;
  gnlh->version = 1;

  ptr = buf + GENL_HDRLEN;

  FAR struct rtattr *rta = (FAR struct rtattr *)ptr;
  rta->rta_type = NL80211_ATTR_IFINDEX;
  rta->rta_len = RTA_LENGTH(sizeof(uint32_t));
  *(FAR uint32_t *)RTA_DATA(rta) = dev->d_ifindex;
  ptr += RTA_SPACE(sizeof(uint32_t));

  rta = (FAR struct rtattr *)ptr;
  rta->rta_type = NL80211_ATTR_WIPHY_FREQ;
  rta->rta_len = RTA_LENGTH(sizeof(uint32_t));
  *(FAR uint32_t *)RTA_DATA(rta) = 0;
  ptr += RTA_SPACE(sizeof(uint32_t));

  rta = (FAR struct rtattr *)ptr;
  rta->rta_type = NL80211_ATTR_COOKIE;
  rta->rta_len = RTA_LENGTH(sizeof(uint64_t));
  *(FAR uint64_t *)RTA_DATA(rta) = cookie;
  ptr += RTA_SPACE(sizeof(uint64_t));

  netlink_add_broadcast(NETLINK_GENERIC,
                        nl80211_genl_mcgrp_id(1), msg);
  return 0;
}

/****************************************************************************
 * Name: nl80211_driver_event_cqm_pktloss
 *
 * Description:
 *   Provide packet loss event to the driver
 *
 * Input Parameters:
 *   drv_priv - Driver private data
 *   tx_pktloss - Number of packets lost
 *   mac_addr - Station MAC address
 *
 * Returned Value:
 *   0 on success; negative error code on failure
 *
 ****************************************************************************/

int nl80211_driver_event_cqm_pktloss(FAR struct net_driver_s *dev,
                                 uint32_t tx_pktloss,
                                 FAR const uint8_t *mac_addr)
{
  FAR struct netlink_response_s *msg;
  FAR uint8_t *buf;
  FAR uint8_t *ptr;
  FAR struct rtattr *rta;
  FAR struct rtattr *cqm_attr;
  FAR uint8_t *cqm_ptr;
  size_t attr_len;
  size_t total_len;

  if (!dev || !mac_addr)
    {
      nerr("ERROR: Invalid parameters\n");
      return -EINVAL;
    }

  /* Calculate total attribute size */

  attr_len = RTA_SPACE(sizeof(uint32_t))
             + RTA_SPACE(ETH_ALEN)
             + RTA_SPACE(RTA_SPACE(sizeof(uint32_t)));

  total_len = NLMSG_LENGTH(GENL_HDRLEN + attr_len);
  msg = kmm_zalloc(sizeof(struct netlink_response_s) + total_len);
  if (!msg)
    {
      return -ENOMEM;
    }

  /* Fill nlmsghdr */

  msg->msg.nlmsg_len = total_len;
  msg->msg.nlmsg_type = nl80211_genl_family_id();
  msg->msg.nlmsg_flags = 0;
  msg->msg.nlmsg_seq = 0;
  msg->msg.nlmsg_pid = 0;

  /* Fill genlmsghdr */

  buf = (FAR uint8_t *)NLMSG_DATA(&msg->msg);
  FAR struct genlmsghdr *gnlh = (FAR struct genlmsghdr *)buf;
  gnlh->cmd = NL80211_CMD_NOTIFY_CQM;
  gnlh->version = 1;

  ptr = buf + GENL_HDRLEN;

  /* NL80211_ATTR_IFINDEX */

  rta = (FAR struct rtattr *)ptr;
  rta->rta_type = NL80211_ATTR_IFINDEX;
  rta->rta_len = RTA_LENGTH(sizeof(uint32_t));
  *(FAR uint32_t *)RTA_DATA(rta) = dev->d_ifindex;
  ptr += RTA_SPACE(sizeof(uint32_t));

  /* NL80211_ATTR_MAC */

  rta = (FAR struct rtattr *)ptr;
  rta->rta_type = NL80211_ATTR_MAC;
  rta->rta_len = RTA_LENGTH(ETH_ALEN);
  memcpy(RTA_DATA(rta), mac_addr, ETH_ALEN);
  ptr += RTA_SPACE(ETH_ALEN);

  /* NL80211_ATTR_CQM (nested) */

  cqm_attr = (FAR struct rtattr *)ptr;
  cqm_attr->rta_type = NL80211_ATTR_CQM;
  cqm_ptr = (FAR uint8_t *)RTA_DATA(cqm_attr);

  /* NL80211_ATTR_CQM_PKT_LOSS_EVENT */

  rta = (FAR struct rtattr *)cqm_ptr;
  rta->rta_type = NL80211_ATTR_CQM_PKT_LOSS_EVENT;
  rta->rta_len = RTA_LENGTH(sizeof(uint32_t));
  *(FAR uint32_t *)RTA_DATA(rta) = tx_pktloss;
  cqm_ptr += RTA_SPACE(sizeof(uint32_t));

  cqm_attr->rta_len = RTA_LENGTH(cqm_ptr
                                 - (FAR uint8_t *)RTA_DATA(cqm_attr));
  ptr = (FAR uint8_t *)cqm_attr + RTA_SPACE(cqm_attr->rta_len);

  msg->msg.nlmsg_len = NLMSG_LENGTH(ptr - buf);

  ninfo("nl80211_driver_event_cqm_pktloss: sending "
        "NL80211_CMD_NOTIFY_CQM, pktloss=%lu\n",
        (unsigned long)tx_pktloss);

  nl80211_send_to_bss_conn(msg, dev);
  return 0;
}

/****************************************************************************
 * Name: nl80211_driver_event_mgmt_tx_status
 *
 * Description:
 *   Provide TX status event to the driver.
 *
 * Input Parameters:
 *   drv_priv - Driver private data
 *   type - Frame type
 *   stype - Frame subtype
 *   dst - Destination address
 *   data - Frame data
 *   data_len - Frame data length
 *   ack - Whether frame was acknowledged
 *   link_id - Link ID
 *
 * Returned Value:
 *   0 on success, negative error code on failure
 *
 ****************************************************************************/

int nl80211_driver_event_mgmt_tx_status(FAR struct net_driver_s *dev,
                                   uint16_t type,
                                   uint16_t stype,
                                   FAR const uint8_t *dst,
                                   FAR const uint8_t *data,
                                   size_t data_len,
                                   int ack,
                                   int link_id,
                                   uint64_t cookie)
{
  FAR struct netlink_response_s *msg;
  FAR uint8_t *buf;
  FAR uint8_t *ptr;
  size_t attr_len;
  size_t total_len;

  if (!dev)
    {
      nerr("ERROR: dev is NULL\n");
      return -ENODEV;
    }

  /* Calculate total attribute size */

  attr_len = RTA_SPACE(sizeof(uint32_t))   /* IFINDEX */
           + RTA_SPACE(sizeof(uint64_t))   /* COOKIE */
           + RTA_SPACE(data_len);          /* FRAME */

  if (ack)
    {
      attr_len += RTA_SPACE(0);            /* ACK flag */
    }

  total_len = NLMSG_LENGTH(GENL_HDRLEN + attr_len);
  msg = kmm_zalloc(sizeof(struct netlink_response_s) + total_len);
  if (!msg)
    {
      return -ENOMEM;
    }

  /* Fill nlmsghdr */

  msg->msg.nlmsg_len = total_len;
  msg->msg.nlmsg_type = nl80211_genl_family_id();
  msg->msg.nlmsg_flags = 0;
  msg->msg.nlmsg_seq = 0;
  msg->msg.nlmsg_pid = 0;

  /* Fill genlmsghdr */

  buf = (FAR uint8_t *)NLMSG_DATA(&msg->msg);
  FAR struct genlmsghdr *gnlh = (FAR struct genlmsghdr *)buf;
  gnlh->cmd = NL80211_CMD_FRAME_TX_STATUS;
  gnlh->version = 1;

  ptr = buf + GENL_HDRLEN;

  /* NL80211_ATTR_IFINDEX */

  FAR struct rtattr *rta = (FAR struct rtattr *)ptr;
  rta->rta_type = NL80211_ATTR_IFINDEX;
  rta->rta_len = RTA_LENGTH(sizeof(uint32_t));
  *(FAR uint32_t *)RTA_DATA(rta) = dev->d_ifindex;
  ptr += RTA_SPACE(sizeof(uint32_t));

  /* NL80211_ATTR_FRAME */

  if (data && data_len > 0)
    {
      rta = (FAR struct rtattr *)ptr;
      rta->rta_type = NL80211_ATTR_FRAME;
      rta->rta_len = RTA_LENGTH(data_len);
      memcpy(RTA_DATA(rta), data, data_len);
      ptr += RTA_SPACE(data_len);
    }

  /* NL80211_ATTR_COOKIE - must match the cookie returned by CMD_FRAME */

  rta = (FAR struct rtattr *)ptr;
  rta->rta_type = NL80211_ATTR_COOKIE;
  rta->rta_len = RTA_LENGTH(sizeof(uint64_t));
  *(FAR uint64_t *)RTA_DATA(rta) = cookie;
  ptr += RTA_SPACE(sizeof(uint64_t));

  /* NL80211_ATTR_ACK */

  if (ack)
    {
      rta = (FAR struct rtattr *)ptr;
      rta->rta_type = NL80211_ATTR_ACK;
      rta->rta_len = RTA_LENGTH(0);
      ptr += RTA_SPACE(0);
    }

  ninfo("nl80211_driver_event_mgmt_tx_status: sending unicast "
        "NL80211_CMD_FRAME_TX_STATUS, ack=%d, len=%d\n", ack, data_len);

  nl80211_send_to_bss_conn(msg, dev);
  return 0;
}

/****************************************************************************
 * Name: nl80211_driver_event_eapol_tx_status
 *
 * Description:
 *   Provide EAPOL TX status event to the driver.
 *
 * Input Parameters:
 *   drv_priv - Driver private data
 *   dst - Destination address
 *   data - Frame data
 *   data_len - Frame data length
 *   ack - Whether frame was acknowledged
 *   link_id - Link ID
 *
 * Returned Value:
 *   0 on success, negative error code on failure
 *
 ****************************************************************************/

int nl80211_driver_event_eapol_tx_status(FAR struct net_driver_s *dev,
                                         FAR const uint8_t *dst,
                                         FAR const uint8_t *data,
                                         size_t data_len,
                                         int ack,
                                         int link_id)
{
  FAR struct netlink_response_s *msg;
  FAR uint8_t *buf;
  FAR uint8_t *ptr;
  FAR struct rtattr *rta;
  size_t attr_len;
  size_t total_len;

  if (!dev)
    {
      nerr("ERROR: dev is NULL\n");
      return -ENODEV;
    }

  /* EAPOL frames are sent as NL80211_CMD_FRAME_TX_STATUS
   * wpa_supplicant will recognize EAPOL frames by their ethertype
   */

  /* Calculate total attribute size */

  attr_len = RTA_SPACE(sizeof(uint32_t))   /* IFINDEX */
           + RTA_SPACE(sizeof(uint64_t))   /* COOKIE */
           + RTA_SPACE(data_len);          /* FRAME */

  if (ack)
    {
      attr_len += RTA_SPACE(0);            /* ACK flag */
    }

  total_len = NLMSG_LENGTH(GENL_HDRLEN + attr_len);
  msg = kmm_zalloc(sizeof(struct netlink_response_s) + total_len);
  if (!msg)
    {
      return -ENOMEM;
    }

  /* Fill nlmsghdr */

  msg->msg.nlmsg_len = total_len;
  msg->msg.nlmsg_type = nl80211_genl_family_id();
  msg->msg.nlmsg_flags = 0;
  msg->msg.nlmsg_seq = 0;
  msg->msg.nlmsg_pid = 0;

  /* Fill genlmsghdr */

  buf = (FAR uint8_t *)NLMSG_DATA(&msg->msg);
  FAR struct genlmsghdr *gnlh = (FAR struct genlmsghdr *)buf;
  gnlh->cmd = NL80211_CMD_FRAME_TX_STATUS;
  gnlh->version = 1;

  ptr = buf + GENL_HDRLEN;

  /* NL80211_ATTR_IFINDEX */

  rta = (FAR struct rtattr *)ptr;
  rta->rta_type = NL80211_ATTR_IFINDEX;
  rta->rta_len = RTA_LENGTH(sizeof(uint32_t));
  *(FAR uint32_t *)RTA_DATA(rta) = dev->d_ifindex;
  ptr += RTA_SPACE(sizeof(uint32_t));

  /* NL80211_ATTR_FRAME - EAPOL frame */

  if (data && data_len > 0)
    {
      rta = (FAR struct rtattr *)ptr;
      rta->rta_type = NL80211_ATTR_FRAME;
      rta->rta_len = RTA_LENGTH(data_len);
      memcpy(RTA_DATA(rta), data, data_len);
      ptr += RTA_SPACE(data_len);
    }

  /* NL80211_ATTR_COOKIE */

  rta = (FAR struct rtattr *)ptr;
  rta->rta_type = NL80211_ATTR_COOKIE;
  rta->rta_len = RTA_LENGTH(sizeof(uint64_t));
  *(FAR uint64_t *)RTA_DATA(rta) = 0;
  ptr += RTA_SPACE(sizeof(uint64_t));

  /* NL80211_ATTR_ACK */

  if (ack)
    {
      rta = (FAR struct rtattr *)ptr;
      rta->rta_type = NL80211_ATTR_ACK;
      rta->rta_len = RTA_LENGTH(0);
      ptr += RTA_SPACE(0);
    }

  msg->msg.nlmsg_len = NLMSG_LENGTH(ptr - buf);

  ninfo("nl80211_driver_event_eapol_tx_status: sending "
        "NL80211_CMD_FRAME_TX_STATUS (EAPOL), ack=%d, len=%zu\n",
        ack, data_len);

  nl80211_send_to_bss_conn(msg, dev);
  return 0;
}

/****************************************************************************
 * Name: nl80211_driver_event_new_station
 *
 * Description:
 *   Notify wpa_supplicant that a new STA has associated to our AP.
 *   This is used in AP mode with device_ap_sme=1 where the firmware
 *   handles AUTH/ASSOC internally. When a STA successfully associates,
 *   this function sends NL80211_CMD_NEW_STATION via mlme multicast
 *   broadcast so wpa_supplicant's process_global_event() can process it.
 *
 * Input Parameters:
 *   mac       - STA's MAC address (6 bytes)
 *   ie        - Association request IEs (optional, can be NULL)
 *   ie_len    - Length of association request IEs
 *   ifname    - Interface name (e.g. "wlan1")
 *
 * Returned Value:
 *   0 on success; negative error code on failure
 *
 ****************************************************************************/

int nl80211_driver_event_new_station(FAR const uint8_t *mac,
                                     FAR const uint8_t *ie,
                                     size_t ie_len,
                                     FAR const char *ifname)
{
  FAR struct netlink_response_s *msg;
  FAR struct net_driver_s *dev;
  FAR uint8_t *buf;
  FAR uint8_t *ptr;
  size_t attr_len;
  size_t total_len;

  if (!mac || !ifname)
    {
      return -EINVAL;
    }

  dev = netdev_findbyname(ifname);
  if (!dev)
    {
      nerr("ERROR: %s not found\n", ifname);
      return -ENODEV;
    }

  /* Calculate total attribute size */

  attr_len = RTA_SPACE(sizeof(uint32_t))   /* IFINDEX */
           + RTA_SPACE(6);                 /* MAC */

  if (ie && ie_len > 0)
    {
      attr_len += RTA_SPACE(ie_len);       /* IE */
    }

  total_len = NLMSG_LENGTH(GENL_HDRLEN + attr_len);
  msg = kmm_zalloc(sizeof(struct netlink_response_s) + total_len);
  if (!msg)
    {
      return -ENOMEM;
    }

  /* Fill nlmsghdr */

  msg->msg.nlmsg_len = total_len;
  msg->msg.nlmsg_type = nl80211_genl_family_id();
  msg->msg.nlmsg_flags = 0;
  msg->msg.nlmsg_seq = 0;
  msg->msg.nlmsg_pid = 0;

  /* Fill genlmsghdr */

  buf = (FAR uint8_t *)NLMSG_DATA(&msg->msg);
  FAR struct genlmsghdr *gnlh = (FAR struct genlmsghdr *)buf;
  gnlh->cmd = NL80211_CMD_NEW_STATION;
  gnlh->version = 1;

  ptr = buf + GENL_HDRLEN;

  /* NL80211_ATTR_IFINDEX */

  FAR struct rtattr *rta = (FAR struct rtattr *)ptr;
  rta->rta_type = NL80211_ATTR_IFINDEX;
  rta->rta_len = RTA_LENGTH(sizeof(uint32_t));
  *(FAR uint32_t *)RTA_DATA(rta) = dev->d_ifindex;
  ptr += RTA_SPACE(sizeof(uint32_t));

  /* NL80211_ATTR_MAC */

  rta = (FAR struct rtattr *)ptr;
  rta->rta_type = NL80211_ATTR_MAC;
  rta->rta_len = RTA_LENGTH(6);
  memcpy(RTA_DATA(rta), mac, 6);
  ptr += RTA_SPACE(6);

  /* NL80211_ATTR_IE (optional - association request IEs) */

  if (ie && ie_len > 0)
    {
      rta = (FAR struct rtattr *)ptr;
      rta->rta_type = NL80211_ATTR_IE;
      rta->rta_len = RTA_LENGTH(ie_len);
      memcpy(RTA_DATA(rta), ie, ie_len);
      ptr += RTA_SPACE(ie_len);
    }

  ninfo("nl80211_driver_event_new_station: sending "
        "NL80211_CMD_NEW_STATION for %02x:%02x:%02x:%02x:%02x:%02x on %s\n",
        mac[0], mac[1], mac[2], mac[3], mac[4], mac[5], ifname);

  /* Broadcast to mlme multicast group so process_global_event() picks
   * it up. This is how Linux kernel sends NL80211_CMD_NEW_STATION.
   */

  netlink_add_broadcast(NETLINK_GENERIC,
                        nl80211_genl_mcgrp_id(1), msg);
  return 0;
}
