/****************************************************************************
 * wireless/nl80211/nl80211.h
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

#ifndef __NL80211_H
#define __NL80211_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <time.h>
#include <netinet/if_ether.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* Forward declarations */

struct netlink_conn_s;
struct netlink_response_s;
struct nlmsghdr;

/****************************************************************************
 * Public Types
 ****************************************************************************/

/* BSS information in scan results */

struct nl80211_scan_bss_s
{
  uint8_t bssid[6];            /* BSSID */
  int freq;                    /* Frequency (MHz) */
  uint16_t beacon_int;         /* Beacon interval */
  uint16_t caps;               /* Capability information */
  int level;                   /* Signal strength */
  uint64_t tsf;                /* Timestamp */
  struct timespec ts;          /* Timestamp when scan was reported */
  size_t ie_len;               /* IE data length */
  size_t beacon_ie_len;        /* Beacon IE data length */
  FAR uint8_t *ie_data;        /* IE data */
};

/* Scan results collection */

struct nl80211_scan_results_s
{
  FAR struct nl80211_scan_bss_s **bss;       /* BSS results array */
  size_t num;                                /* Number of results */
  unsigned int fetch_time;                   /* Fetch time */
};

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

/****************************************************************************
 * Name: nl80211_driver_event_scan_start
 *
 * Description:
 *   Driver calls this function to notify scan start
 *
 * Input Parameters:
 *   dev - Network device pointer
 *
 * Returned Value:
 *   0 on success; negative error code on failure
 *
 ****************************************************************************/

int nl80211_driver_event_scan_start(FAR struct net_driver_s *dev);

/****************************************************************************
 * Name: nl80211_driver_event_scan_finish
 *
 * Description:
 *   Driver calls this function to notify scan completion
 *
 * Input Parameters:
 *   dev     - Network device pointer
 *   aborted - Whether scan was aborted
 *
 * Returned Value:
 *   0 on success; negative error code on failure
 *
 ****************************************************************************/

int nl80211_driver_event_scan_finish(FAR struct net_driver_s *dev,
                                     bool aborted);

/****************************************************************************
 * Name: nl80211_driver_event_scan_result
 *
 * Description:
 *   Driver calls this function to provide individual scan results
 *
 * Input Parameters:
 *   dev - Network device pointer
 *   bss - Individual BSS information
 *
 * Returned Value:
 *   0 on success; negative error code on failure
 *
 ****************************************************************************/

int nl80211_driver_event_scan_result(FAR struct net_driver_s *dev,
                                     FAR struct nl80211_scan_bss_s *bss);

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
                                 uint8_t info_bitmap);

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
                                        int timed_out);

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
                                    uint16_t reason_code);

/****************************************************************************
 * Name: nl80211_driver_event_external_auth
 *
 * Description:
 *   Driver reports external authentication event
 *
 * Input Parameters:
 *   dev             - Network device pointer
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
                                       FAR const uint8_t *pmkid);

/****************************************************************************
 * Name: nl80211_driver_event_mgmt_rx
 *
 * Description:
 *   Driver reports management frame receive event
 *
 * Input Parameters:
 *   dev        - Network device pointer
 *   frame      - Management frame data
 *   frame_len  - Management frame length
 *   freq       - Receive frequency
 *   ssi_signal - Signal strength
 *
 * Returned Value:
 *   0 on success; negative error code on failure
 *
 ****************************************************************************/

int nl80211_driver_event_mgmt_rx(FAR struct net_driver_s *dev,
                                 FAR const uint8_t *frame,
                                 size_t frame_len,
                                 uint32_t freq,
                                 int ssi_signal);

/****************************************************************************
 * Name: nl80211_driver_event_remain_on_channel
 *
 * Description:
 *   Driver reports remain on channel event
 *
 * Input Parameters:
 *   dev      - Network device pointer
 *   freq     - Frequency
 *   duration - Duration
 *   cookie   - ROC cookie from driver
 *
 * Returned Value:
 *   0 on success; negative error code on failure
 *
 ****************************************************************************/

int nl80211_driver_event_remain_on_channel(FAR struct net_driver_s *dev,
                                           uint32_t freq,
                                           uint32_t duration,
                                           uint64_t cookie);

/****************************************************************************
 * Name: nl80211_driver_event_cancel_remain_on_channel
 *
 * Description:
 *   Driver reports cancel remain on channel event
 *
 * Input Parameters:
 *   dev    - Network device pointer
 *   cookie - ROC cookie from driver
 *
 * Returned Value:
 *   0 on success; negative error code on failure
 *
 ****************************************************************************/

int nl80211_driver_event_cancel_remain_on_channel(
    FAR struct net_driver_s *dev,
    uint64_t cookie);

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
                                     FAR const uint8_t *mac_addr);

/****************************************************************************
 * Name: nl80211_driver_event_mgmt_tx_status
 *
 * Description:
 *   Provide TX status event to the driver.
 *
 * Input Parameters:
 *   dev - Network device pointer
 *   type - Frame type
 *   stype - Frame subtype
 *   dst - Destination address
 *   data - Frame data
 *   data_len - Frame data length
 *   ack - Whether frame was acknowledged
 *   link_id - Link ID
 *   cookie - TX cookie from mgmt_tx (must match CMD_FRAME response)
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
                                        uint64_t cookie);

/****************************************************************************
 * Name: nl80211_driver_event_eapol_tx_status
 *
 * Description:
 *   Provide EAPOL TX status event to the driver.
 *
 * Input Parameters:
 *   dev - Network device pointer
 *   dst - Destination address
 *   data - EAPOL frame data
 *   data_len - EAPOL frame data length
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
                                         int link_id);

/****************************************************************************
 * Name: nl80211_driver_event_new_station
 *
 * Description:
 *   Notify wpa_supplicant that a new STA has associated to our AP.
 *   Used in AP mode with device_ap_sme=1.
 *
 * Input Parameters:
 *   mac    - STA's MAC address (6 bytes)
 *   ie     - Association request IEs (optional, can be NULL)
 *   ie_len - Length of association request IEs
 *   ifname - Interface name (e.g. "wlan1")
 *
 * Returned Value:
 *   0 on success; negative error code on failure
 *
 ****************************************************************************/

int nl80211_driver_event_new_station(FAR const uint8_t *mac,
                                     FAR const uint8_t *ie,
                                     size_t ie_len,
                                     FAR const char *ifname);

/****************************************************************************
 * Name: nl80211_genl_family_id
 *
 * Description:
 *   Return the dynamically assigned generic netlink family ID for nl80211.
 *
 * Input Parameters:
 *   None
 *
 * Returned Value:
 *   The nl80211 generic netlink family ID
 *
 ****************************************************************************/

uint16_t nl80211_genl_family_id(void);

/****************************************************************************
 * Name: nl80211_genl_mcgrp_id
 *
 * Description:
 *   Return the multicast group ID for a given nl80211 group index.
 *   Index 0 = scan, 1 = mlme, 2 = regulatory, 3 = vendor.
 *
 * Input Parameters:
 *   index - Group index (0=scan, 1=mlme, 2=regulatory, 3=vendor)
 *
 * Returned Value:
 *   The multicast group ID for the specified index
 *
 ****************************************************************************/

uint16_t nl80211_genl_mcgrp_id(int index);

/****************************************************************************
 * Name: nl80211_init
 *
 * Description:
 *   Register the nl80211 generic netlink family.
 *
 * Input Parameters:
 *   None
 *
 * Returned Value:
 *   0 on success; negative error code on failure
 *
 ****************************************************************************/

int nl80211_init(void);

/****************************************************************************
 * Name: nl80211_handle_msg
 *
 * Description:
 *   Handle incoming nl80211 generic netlink messages.
 *
 * Input Parameters:
 *   conn  - Netlink connection pointer
 *   nlmsg - Netlink message header pointer
 *
 * Returned Value:
 *   0 on success; negative error code on failure
 *
 ****************************************************************************/

int nl80211_handle_msg(FAR struct netlink_conn_s *conn,
                       FAR const struct nlmsghdr *nlmsg);

/****************************************************************************
 * Name: nl80211_send_to_bss_conn
 *
 * Description:
 *   Send a netlink message as unicast to the BSS event connection.
 *   In Linux, events like NL80211_CMD_EXTERNAL_AUTH, NL80211_CMD_FRAME,
 *   and NL80211_CMD_FRAME_TX_STATUS are sent as unicast to the per-BSS
 *   socket (nl_mgmt). wpa_supplicant processes these in process_bss_event()
 *   rather than do_process_drv_event(). This function emulates that by
 *   sending to the connection that registered for mgmt frames.
 *
 *   When dev is non-NULL, the message is routed to the connection that
 *   registered frame handlers for that specific interface (by ifindex).
 *   This is essential in multi-process scenarios (e.g. separate
 *   wpa_supplicant per P2P interface) where each process has its own
 *   netlink connection.
 *
 * Input Parameters:
 *   msg - The netlink response message to send (will be freed by receiver)
 *   dev - Target network device for routing (may be NULL for fallback)
 *
 * Returned Value:
 *   0 on success; negative error code on failure
 *
 ****************************************************************************/

int nl80211_send_to_bss_conn(FAR struct netlink_response_s *msg,
                             FAR struct net_driver_s *dev);

/****************************************************************************
 * Name: nl80211_unregister_frame_conn
 *
 * Description:
 *   Remove all frame registrations associated with a given netlink
 *   connection.  Called when a netlink socket is closed to prevent
 *   stale connection pointers from remaining in the registration table.
 *
 * Input Parameters:
 *   conn - The netlink connection being closed
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

void nl80211_unregister_frame_conn(FAR struct netlink_conn_s *conn);

/****************************************************************************
 * Name: nl80211_send_scan_results
 *
 * Description:
 *   Send scan results event (NL80211_CMD_NEW_SCAN_RESULTS) to userspace.
 *
 * Input Parameters:
 *   dev - Network device pointer
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

void nl80211_send_scan_results(FAR struct net_driver_s *dev);

/****************************************************************************
 * Name: nl80211_send_scan_start
 *
 * Description:
 *   Send scan trigger event (NL80211_CMD_TRIGGER_SCAN) to userspace.
 *
 * Input Parameters:
 *   dev - Network device pointer
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

void nl80211_send_scan_start(FAR struct net_driver_s *dev);

/****************************************************************************
 * Name: nl80211_get_scan_results
 *
 * Description:
 *   Get stored scan results
 *
 * Input Parameters:
 *   bss_list - Pointer to receive the BSS array pointer
 *              (array of pointers to nl80211_scan_bss_s)
 *   num      - Pointer to store the number of BSS entries
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

void nl80211_get_scan_results(FAR struct nl80211_scan_bss_s ***bss_list,
                              FAR int *num);

#endif /* __NL80211_H */
