/****************************************************************************
 * wireless/nl80211/nl80211_ops.c
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
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <debug.h>

#include <nuttx/bits.h>
#include <nuttx/kmalloc.h>
#include <nuttx/mutex.h>
#include <netpacket/genetlink.h>
#include <netpacket/netlink.h>
#include <netpacket/rtnetlink.h>
#include <net/cfg80211.h>
#include <net/netdev/netdev.h>
#include <net/netlink/netlink.h>
#include <net/nl80211.h>

#include "nl80211.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define MAX_FRAME_REGISTRATIONS 64

/****************************************************************************
 * Private Types
 ****************************************************************************/

/* Frame registration table for routing RX management frames to the
 * correct BSS connection. wpa_supplicant registers multiple sockets
 * for different frame types:
 *   - nl_mgmt (bss conn): ACTION, AUTH frames -> process_bss_event()
 *   - nl_preq: PROBE_REQ frames -> process_bss_event()
 * We store each registration so we can route frames correctly.
 */

struct frame_registration
{
  FAR struct netlink_conn_s *conn;
  uint16_t frame_type;  /* e.g. 0xb0 for AUTH, 0xd0 for ACTION, 0x40 for PROBE_REQ */
  uint8_t match[8];
  uint8_t match_len;
  int ifindex;          /* interface index that registered this frame */
};

/* Bounds-checked netlink message builder, modelled on the Linux kernel's
 * nla_put()/nla_nest_start() API.  All attribute serialization in this
 * file goes through these helpers.  Every write is bounds-checked against
 * "end" (the end of the buffer allocated by nl80211_alloc_reply_msg()), so
 * a reply can never overrun its buffer: a write that would not fit is
 * dropped and the builder keeps a structurally valid message, matching the
 * best-effort behaviour of the kernel's dump path.  The fields mirror the
 * head/tail/end pointers of struct sk_buff:
 *
 *   head  - start of the generic netlink header (== NLMSG_DATA)
 *   tail  - current write position (attributes grow upward)
 *   end   - one past the last usable payload byte (head + buflen)
 */

struct nl80211_msg
{
  FAR struct netlink_response_s *resp;
  FAR uint8_t *head;
  FAR uint8_t *tail;
  FAR uint8_t *end;
};

/****************************************************************************
 * Private Data
 ****************************************************************************/

static struct frame_registration g_frame_regs[MAX_FRAME_REGISTRATIONS];
static int g_frame_reg_count;

/* Fallback BSS conn - last registered conn, used for non-frame events */

static FAR struct netlink_conn_s *g_bss_conn;

/* Mutex to protect g_frame_regs, g_frame_reg_count and g_bss_conn */

static mutex_t g_frame_lock = NXMUTEX_INITIALIZER;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: nl80211_msg_init
 *
 * Description:
 *   Bind a message builder to a freshly allocated reply and reserve room
 *   for the generic netlink header.
 *
 * Input Parameters:
 *   m      - Message builder to initialize
 *   resp   - Freshly allocated (zeroed) reply the builder writes into
 *   buflen - Usable payload size after the netlink header, in bytes; must
 *            match the value passed to nl80211_alloc_reply_msg()
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

static inline void
nl80211_msg_init(FAR struct nl80211_msg *m,
                 FAR struct netlink_response_s *resp, int buflen)
{
  m->resp = resp;

  /* Write through the declared flexible array member (same address as
   * NLMSG_DATA(&resp->msg)) so all builder writes stay in bounds of a
   * declared object (Coverity OVERRUN).
   */

  m->head = resp->data;
  m->tail = m->head + GENL_HDRLEN;
  m->end = m->head + buflen;
}

/****************************************************************************
 * Name: nl80211_msg_tailroom
 *
 * Description:
 *   Return the number of payload bytes still available.  Best-effort loops
 *   that emit a variable number of items (interface combinations, HE
 *   iftype data) use this to stop before an item would not fit.
 *
 * Input Parameters:
 *   m - Message builder
 *
 * Returned Value:
 *   Number of unused bytes remaining in the buffer.
 *
 ****************************************************************************/

static inline size_t nl80211_msg_tailroom(FAR struct nl80211_msg *m)
{
  return m->tail < m->end ? (size_t)(m->end - m->tail) : 0;
}

/****************************************************************************
 * Name: nl80211_nla_reserve
 *
 * Description:
 *   Reserve space for one attribute header plus "attrlen" payload bytes,
 *   writing the rtattr header and advancing the write pointer.  The
 *   alignment padding is already zero because the buffer is kmm_zalloc()ed.
 *
 * Input Parameters:
 *   m        - Message builder
 *   attrtype - Attribute type (rta_type)
 *   attrlen  - Payload length in bytes (excludes the header)
 *
 * Returned Value:
 *   Pointer to the attribute payload area, or NULL if it would not fit.
 *
 ****************************************************************************/

static inline FAR void *
nl80211_nla_reserve(FAR struct nl80211_msg *m, uint16_t attrtype,
                    uint16_t attrlen)
{
  FAR struct rtattr *rta;

  if (m->tail + RTA_SPACE(attrlen) > m->end)
    {
      return NULL;
    }

  rta = (FAR struct rtattr *)m->tail;
  rta->rta_type = attrtype;
  rta->rta_len = RTA_LENGTH(attrlen);
  m->tail += RTA_SPACE(attrlen);
  return RTA_DATA(rta);
}

/****************************************************************************
 * Name: nl80211_nla_put
 *
 * Description:
 *   Append an attribute, copying "attrlen" bytes from "data" into the
 *   payload.
 *
 * Input Parameters:
 *   m        - Message builder
 *   attrtype - Attribute type (rta_type)
 *   attrlen  - Payload length in bytes
 *   data     - Payload source (may be NULL when attrlen is 0)
 *
 * Returned Value:
 *   0 on success; -EMSGSIZE if the attribute would not fit.
 *
 ****************************************************************************/

static inline int
nl80211_nla_put(FAR struct nl80211_msg *m, uint16_t attrtype,
                uint16_t attrlen, FAR const void *data)
{
  FAR void *dst = nl80211_nla_reserve(m, attrtype, attrlen);

  if (dst == NULL)
    {
      return -EMSGSIZE;
    }

  if (attrlen > 0 && data != NULL)
    {
      memcpy(dst, data, attrlen);
    }

  return 0;
}

/****************************************************************************
 * Name: nl80211_nla_put_u8
 *
 * Description:
 *   Append an unsigned 8-bit integer attribute.
 *
 * Input Parameters:
 *   m        - Message builder
 *   attrtype - Attribute type (rta_type)
 *   v        - Integer value to store
 *
 * Returned Value:
 *   0 on success; -EMSGSIZE if the attribute would not fit.
 *
 ****************************************************************************/

static inline int
nl80211_nla_put_u8(FAR struct nl80211_msg *m, uint16_t attrtype, uint8_t v)
{
  return nl80211_nla_put(m, attrtype, sizeof(v), &v);
}

/****************************************************************************
 * Name: nl80211_nla_put_u16
 *
 * Description:
 *   Append an unsigned 16-bit integer attribute.
 *
 * Input Parameters:
 *   m        - Message builder
 *   attrtype - Attribute type (rta_type)
 *   v        - Integer value to store
 *
 * Returned Value:
 *   0 on success; -EMSGSIZE if the attribute would not fit.
 *
 ****************************************************************************/

static inline int
nl80211_nla_put_u16(FAR struct nl80211_msg *m, uint16_t attrtype,
                    uint16_t v)
{
  return nl80211_nla_put(m, attrtype, sizeof(v), &v);
}

/****************************************************************************
 * Name: nl80211_nla_put_u32
 *
 * Description:
 *   Append an unsigned 32-bit integer attribute.
 *
 * Input Parameters:
 *   m        - Message builder
 *   attrtype - Attribute type (rta_type)
 *   v        - Integer value to store
 *
 * Returned Value:
 *   0 on success; -EMSGSIZE if the attribute would not fit.
 *
 ****************************************************************************/

static inline int
nl80211_nla_put_u32(FAR struct nl80211_msg *m, uint16_t attrtype,
                    uint32_t v)
{
  return nl80211_nla_put(m, attrtype, sizeof(v), &v);
}

/****************************************************************************
 * Name: nl80211_nla_put_u64
 *
 * Description:
 *   Append an unsigned 64-bit integer attribute.
 *
 * Input Parameters:
 *   m        - Message builder
 *   attrtype - Attribute type (rta_type)
 *   v        - Integer value to store
 *
 * Returned Value:
 *   0 on success; -EMSGSIZE if the attribute would not fit.
 *
 ****************************************************************************/

static inline int
nl80211_nla_put_u64(FAR struct nl80211_msg *m, uint16_t attrtype,
                    uint64_t v)
{
  return nl80211_nla_put(m, attrtype, sizeof(v), &v);
}

/****************************************************************************
 * Name: nl80211_nla_put_flag
 *
 * Description:
 *   Append a zero-length flag attribute (its presence conveys a boolean).
 *
 * Input Parameters:
 *   m        - Message builder
 *   attrtype - Attribute type (rta_type)
 *
 * Returned Value:
 *   0 on success; -EMSGSIZE if the attribute would not fit.
 *
 ****************************************************************************/

static inline int
nl80211_nla_put_flag(FAR struct nl80211_msg *m, uint16_t attrtype)
{
  return nl80211_nla_put(m, attrtype, 0, NULL);
}

/****************************************************************************
 * Name: nl80211_nla_put_string
 *
 * Description:
 *   Append a NUL-terminated string attribute (length includes the NUL).
 *
 * Input Parameters:
 *   m        - Message builder
 *   attrtype - Attribute type (rta_type)
 *   str      - NUL-terminated string to store
 *
 * Returned Value:
 *   0 on success; -EMSGSIZE if the attribute would not fit.
 *
 ****************************************************************************/

static inline int
nl80211_nla_put_string(FAR struct nl80211_msg *m, uint16_t attrtype,
                       FAR const char *str)
{
  return nl80211_nla_put(m, attrtype, strlen(str) + 1, str);
}

/****************************************************************************
 * Name: nl80211_nla_nest_start
 *
 * Description:
 *   Open a nested attribute container.  Children are appended with the
 *   normal put helpers and the container is closed with
 *   nl80211_nla_nest_end() (or discarded with nl80211_nla_nest_cancel()).
 *
 * Input Parameters:
 *   m        - Message builder
 *   attrtype - Attribute type (rta_type) of the container
 *
 * Returned Value:
 *   The container attribute pointer, or NULL if it would not fit.
 *
 ****************************************************************************/

static inline FAR struct rtattr *
nl80211_nla_nest_start(FAR struct nl80211_msg *m, uint16_t attrtype)
{
  FAR struct rtattr *start;

  if (m->tail + RTA_SPACE(0) > m->end)
    {
      return NULL;
    }

  start = (FAR struct rtattr *)m->tail;
  start->rta_type = attrtype;
  start->rta_len = RTA_LENGTH(0);
  m->tail += RTA_SPACE(0);
  return start;
}

/****************************************************************************
 * Name: nl80211_nla_nest_end
 *
 * Description:
 *   Close a nested attribute, back-patching its length to cover all the
 *   children written since nl80211_nla_nest_start().  "tail" is already
 *   4-byte aligned because every child advanced it by RTA_SPACE().  A NULL
 *   "start" (failed nest_start) is ignored.
 *
 * Input Parameters:
 *   m     - Message builder
 *   start - Container returned by nl80211_nla_nest_start()
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

static inline void
nl80211_nla_nest_end(FAR struct nl80211_msg *m, FAR struct rtattr *start)
{
  if (start != NULL)
    {
      start->rta_len = (uint16_t)(m->tail - (FAR uint8_t *)start);
    }
}

/****************************************************************************
 * Name: nl80211_nla_nest_cancel
 *
 * Description:
 *   Discard a nested attribute, rewinding the write position to before its
 *   header.  A NULL "start" is ignored.
 *
 * Input Parameters:
 *   m     - Message builder
 *   start - Container returned by nl80211_nla_nest_start()
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

static inline void
nl80211_nla_nest_cancel(FAR struct nl80211_msg *m, FAR struct rtattr *start)
{
  if (start != NULL)
    {
      m->tail = (FAR uint8_t *)start;
    }
}

/****************************************************************************
 * Name: nl80211_msg_finalize
 *
 * Description:
 *   Finalize the netlink message header length to cover the generic
 *   netlink header plus all attributes written.
 *
 * Input Parameters:
 *   m - Message builder
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

static inline void nl80211_msg_finalize(FAR struct nl80211_msg *m)
{
  m->resp->msg.nlmsg_len = NLMSG_LENGTH((size_t)(m->tail - m->head));
}

/****************************************************************************
 * Name: nl80211_get_wiphy_idx
 *
 * Description:
 *   Derive a unique wiphy index for a given wiphy pointer by scanning
 *   all registered netdevs. Each distinct wiphy pointer gets an index
 *   based on the order it first appears (by ifindex).
 *   This avoids adding wiphy_idx to struct wiphy (not in Linux).
 *
 * Returned Value:
 *   Wiphy index on success, UINT32_MAX if target is NULL or not found.
 *
 ****************************************************************************/

static uint32_t nl80211_get_wiphy_idx(FAR struct wiphy *target)
{
  FAR struct wiphy *seen[16];
  int seen_count = 0;
  int k;

  if (!target)
    {
      return UINT32_MAX;
    }

  net_lock();

  for (k = 1; k <= 16; k++)
    {
      FAR struct net_driver_s *d = netdev_findbyindex(k);
      if (!d || !d->d_wiphy)
        {
          continue;
        }

      /* Check if we already counted this wiphy */

      int dup = 0;
      int s;
      for (s = 0; s < seen_count; s++)
        {
          if (seen[s] == (FAR struct wiphy *)d->d_wiphy)
            {
              dup = 1;
              break;
            }
        }

      if (!dup)
        {
          if ((FAR struct wiphy *)d->d_wiphy == target)
            {
              net_unlock();
              return (uint32_t)seen_count;
            }

          if (seen_count < 16)
            {
              seen[seen_count++] = (FAR struct wiphy *)d->d_wiphy;
            }
        }
    }

  net_unlock();
  return UINT32_MAX;
}

/****************************************************************************
 * Name: nl80211_find_wiphy_by_idx
 *
 * Description:
 *   Find a wiphy pointer and its associated netdev given a wiphy index.
 *
 ****************************************************************************/

static FAR struct wiphy *nl80211_find_wiphy_by_idx(uint32_t idx,
                                                   FAR struct net_driver_s
                                                   **out_dev)
{
  FAR struct wiphy *seen[16];
  int seen_count = 0;
  int k;

  net_lock();

  for (k = 1; k <= 16; k++)
    {
      FAR struct net_driver_s *d = netdev_findbyindex(k);
      if (!d || !d->d_wiphy)
        {
          continue;
        }

      int dup = 0;
      int s;
      for (s = 0; s < seen_count; s++)
        {
          if (seen[s] == (FAR struct wiphy *)d->d_wiphy)
            {
              dup = 1;
              break;
            }
        }

      if (!dup)
        {
          if ((uint32_t)seen_count == idx)
            {
              if (out_dev)
                {
                  *out_dev = d;
                }

              net_unlock();
              return (FAR struct wiphy *)d->d_wiphy;
            }

          if (seen_count < 16)
            {
              seen[seen_count++] = (FAR struct wiphy *)d->d_wiphy;
            }
        }
    }

  net_unlock();
  return NULL;
}

/****************************************************************************
 * Name: nl80211_get_channel
 *
 * Description:
 *   Helper to allocate channel info
 *
 * Input Parameters:
 *   freq - Channel frequency
 *
 * Returned Value:
 *   Pointer to allocated channel structure, or NULL on failure
 *
 ****************************************************************************/

static FAR struct ieee80211_channel *nl80211_get_channel(int freq)
{
  FAR struct ieee80211_channel *channel;

  channel = (FAR struct ieee80211_channel *)
            kmm_zalloc(sizeof(struct ieee80211_channel));
  if (channel)
    {
      channel->center_freq = freq;
    }

  return channel;
}

/****************************************************************************
 * Name: nl80211_cmd_get_protocol_features
 *
 * Description:
 *   Handle NL80211_CMD_GET_PROTOCOL_FEATURES command
 *
 * Input Parameters:
 *   conn  - Netlink connection
 *   nlmsg - Netlink message header
 *   gnlh  - Generic netlink message header
 *   tb    - Netlink attributes array
 *
 * Returned Value:
 *   0 on success; negative error code on failure
 *
 ****************************************************************************/

static int nl80211_cmd_get_protocol_features(
             FAR struct netlink_conn_s *conn,
             FAR const struct nlmsghdr *nlmsg,
             FAR struct genlmsghdr *gnlh,
             FAR struct nlattr **tb)
{
  FAR struct netlink_response_s *msg;
  FAR struct genlmsghdr *resp_gnlh;
  struct nl80211_msg m;
  size_t buflen = GENL_HDRLEN + RTA_SPACE(sizeof(uint32_t));

  ninfo("nl80211_cmd_get_protocol_features: called\n");

  /* Allocate response (single-shot reply, not a multipart dump) */

  msg = (FAR struct netlink_response_s *)
        kmm_zalloc(sizeof(struct netlink_response_s) + buflen);
  if (!msg)
    {
      return -ENOMEM;
    }

  msg->msg.nlmsg_type = nlmsg->nlmsg_type;
  msg->msg.nlmsg_flags = 0;
  msg->msg.nlmsg_seq = nlmsg->nlmsg_seq;
  msg->msg.nlmsg_pid = nlmsg->nlmsg_pid;

  resp_gnlh = (FAR struct genlmsghdr *)NLMSG_DATA(&msg->msg);
  resp_gnlh->cmd = NL80211_CMD_GET_PROTOCOL_FEATURES;
  resp_gnlh->version = 1;

  nl80211_msg_init(&m, msg, buflen);

  /* Reference Linux nl80211_get_protocol_features:
   * It returns NL80211_PROTOCOL_FEATURE_SPLIT_WIPHY_DUMP.
   */

  if (nl80211_nla_put_u32(&m, NL80211_ATTR_PROTOCOL_FEATURES,
                          NL80211_PROTOCOL_FEATURE_SPLIT_WIPHY_DUMP) < 0)
    {
      kmm_free(msg);
      return -EMSGSIZE;
    }

  nl80211_msg_finalize(&m);
  netlink_add_response((NETLINK_HANDLE)conn, msg);

  return 0;
}

/****************************************************************************
 * Name: nl80211_cmd_register_frame
 *
 * Description:
 *   Handle NL80211_CMD_REGISTER_FRAME command. Registers a connection to
 *   receive management frames of a specific type. This is used by
 *   wpa_supplicant to receive AUTH, ACTION, and PROBE_REQ frames.
 *
 * Input Parameters:
 *   conn  - Netlink connection
 *   nlmsg - Netlink message header
 *   gnlh  - Generic netlink message header
 *   tb    - Netlink attributes array
 *
 * Returned Value:
 *   0 on success; negative error code on failure
 *
 ****************************************************************************/

static int nl80211_cmd_register_frame(FAR struct netlink_conn_s *conn,
                                      FAR const struct nlmsghdr *nlmsg,
                                      FAR struct genlmsghdr *gnlh,
                                      FAR struct nlattr **tb)
{
  /* Save the connection pointer and frame type registration.
   * In Linux, events like NL80211_CMD_FRAME are sent as unicast to the
   * socket that registered for that frame type. We track registrations
   * so we can route frames to the correct connection.
   */

  uint16_t frame_type = 0;
  FAR const uint8_t *match = NULL;
  uint8_t match_len = 0;
  int i;

  if (tb[NL80211_ATTR_FRAME_TYPE])
    {
      frame_type = (uint16_t)nla_get_u16(tb[NL80211_ATTR_FRAME_TYPE]);
    }

  if (tb[NL80211_ATTR_FRAME_MATCH])
    {
      match = nla_data(tb[NL80211_ATTR_FRAME_MATCH]);
      match_len = nla_len(tb[NL80211_ATTR_FRAME_MATCH]);
      if (match_len > 8)
        {
          match_len = 8;
        }
    }

  /* Check for existing duplicate registration (same frame_type + match
   * + ifindex). If found, just update the conn pointer.
   * Different interfaces must have separate registrations even for the
   * same frame type, so ifindex is part of the uniqueness key.
   */

  int req_ifindex = 0;
  if (tb[NL80211_ATTR_IFINDEX])
    {
      req_ifindex = nla_get_u32(tb[NL80211_ATTR_IFINDEX]);
    }

  nxmutex_lock(&g_frame_lock);

  for (i = 0; i < g_frame_reg_count; i++)
    {
      if (g_frame_regs[i].frame_type == frame_type &&
          g_frame_regs[i].ifindex == req_ifindex &&
          g_frame_regs[i].match_len == match_len &&
          (match_len == 0 ||
           memcmp(g_frame_regs[i].match, match, match_len) == 0))
        {
          /* Update conn pointer to the current (new) connection */

          g_frame_regs[i].conn = conn;

          ninfo("nl80211_cmd_register_frame: updated existing reg[%d] "
                "type=0x%04x conn=%p ifindex=%d\n", i, frame_type, conn,
                g_frame_regs[i].ifindex);
          goto update_bss;
        }
    }

  /* No duplicate found — store new registration if we have room */

  if (g_frame_reg_count < MAX_FRAME_REGISTRATIONS)
    {
      struct frame_registration *reg =
        &g_frame_regs[g_frame_reg_count++];

      reg->conn = conn;
      reg->frame_type = frame_type;
      reg->match_len = match_len;
      reg->ifindex = req_ifindex;

      if (match_len > 0 && match)
        {
          memcpy(reg->match, match, match_len);
        }
    }
  else
    {
      nwarn("nl80211_cmd_register_frame: table full (%d), "
            "type=0x%04x dropped\n", g_frame_reg_count, frame_type);
    }

update_bss:

  /* Always update fallback bss conn */

  g_bss_conn = conn;

  nxmutex_unlock(&g_frame_lock);

  /* If this is a Probe Request registration (frame_type == 0x0040),
   * tell the driver to start reporting probe requests.
   * Without this call, the firmware never delivers Probe Request
   * frames during P2P listen, making the device invisible to peers.
   */

  if (frame_type == 0x0040 && tb[NL80211_ATTR_IFINDEX])
    {
      FAR struct net_driver_s *dev;
      int ifindex = nla_get_u32(tb[NL80211_ATTR_IFINDEX]);

      dev = netdev_findbyindex(ifindex);
      if (dev && dev->d_cfg80211ops &&
          dev->d_cfg80211ops->probe_req_report)
        {
          dev->d_cfg80211ops->probe_req_report(dev->d_wiphy,
                                               dev->d_wdev, 1);
          ninfo("nl80211_cmd_register_frame: enabled probe_req_report "
               "for ifindex=%d\n", ifindex);
        }
    }

  ninfo("nl80211_cmd_register_frame: saved bss conn=%p pid=%lu\n",
       conn, (unsigned long)conn->pid);
  return 0;
}

/****************************************************************************
 * Name: nl80211_unregister_frame_conn
 *
 * Description:
 *   Remove all frame registrations associated with a given netlink
 *   connection.  This must be called when a netlink socket is closed
 *   so that stale connection pointers do not remain in the registration
 *   table.  Without this cleanup, after interface_remove / interface_add
 *   cycles the table fills up with entries pointing to freed connections,
 *   causing management frames (e.g. AUTH for SAE) to be delivered to
 *   dead sockets instead of the newly registered one.
 *
 * Input Parameters:
 *   conn - The netlink connection being closed
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

void nl80211_unregister_frame_conn(FAR struct netlink_conn_s *conn)
{
  int i;
  int j;

  nxmutex_lock(&g_frame_lock);

  for (i = 0, j = 0; i < g_frame_reg_count; i++)
    {
      if (g_frame_regs[i].conn != conn)
        {
          if (i != j)
            {
              g_frame_regs[j] = g_frame_regs[i];
            }

          j++;
        }
    }

  if (j < g_frame_reg_count)
    {
      ninfo("nl80211_unregister_frame_conn: removed %d regs for conn=%p "
            "(%d -> %d)\n", g_frame_reg_count - j, conn,
            g_frame_reg_count, j);
      g_frame_reg_count = j;
    }

  /* Clear fallback bss conn if it matches */

  if (g_bss_conn == conn)
    {
      g_bss_conn = NULL;
    }

  nxmutex_unlock(&g_frame_lock);
}

/****************************************************************************
 * Name: nl80211_alloc_reply_msg
 *
 * Description:
 *   Allocate a netlink reply message with generic netlink header
 *
 * Input Parameters:
 *   req    - Request message to reply to
 *   cmd    - Generic netlink command
 *   buflen - Buffer length for message payload
 *
 * Returned Value:
 *   Pointer to allocated message, or NULL on failure
 *
 ****************************************************************************/

static FAR struct netlink_response_s *
nl80211_alloc_reply_msg(FAR const struct nlmsghdr *req, int cmd,
                        int buflen)
{
  FAR struct netlink_response_s *msg;
  struct genlmsghdr *gnlh;

  msg = (FAR struct netlink_response_s *)
        kmm_zalloc(sizeof(struct netlink_response_s) + buflen);
  if (!msg)
    {
      return NULL;
    }

  msg->msg.nlmsg_type = req->nlmsg_type;
  msg->msg.nlmsg_flags = NLM_F_MULTI;
  msg->msg.nlmsg_seq = req->nlmsg_seq;
  msg->msg.nlmsg_pid = req->nlmsg_pid;

  gnlh = (struct genlmsghdr *)NLMSG_DATA(&msg->msg);
  gnlh->cmd = cmd;
  gnlh->version = 1;
  gnlh->reserved = 0;

  return msg;
}

/****************************************************************************
 * Name: nl80211_send_done_msg
 *
 * Description:
 *   Send a NLMSG_DONE message to indicate end of multi-part message
 *
 * Input Parameters:
 *   conn - Netlink connection
 *   req  - Request message
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

static void nl80211_send_done_msg(FAR struct netlink_conn_s *conn,
                                  FAR const struct nlmsghdr *req)
{
  FAR struct netlink_response_s *msg;

  msg = (FAR struct netlink_response_s *)
        kmm_zalloc(sizeof(struct netlink_response_s) + sizeof(int));
  if (msg)
    {
      msg->msg.nlmsg_len = NLMSG_LENGTH(sizeof(int));
      msg->msg.nlmsg_type = NLMSG_DONE;
      msg->msg.nlmsg_flags = NLM_F_MULTI;
      msg->msg.nlmsg_seq = req->nlmsg_seq;
      msg->msg.nlmsg_pid = req->nlmsg_pid;
      *(int *)NLMSG_DATA(&msg->msg) = 0;
      netlink_add_response((NETLINK_HANDLE)conn, msg);
    }
}

/****************************************************************************
 * Name: nl80211_cmd_get_wiphy
 *
 * Description:
 *   Handle NL80211_CMD_GET_WIPHY command. Returns wireless PHY information
 *   including supported bands, channels, cipher suites, and capabilities.
 *
 * Input Parameters:
 *   conn  - Netlink connection
 *   nlmsg - Netlink message header
 *   gnlh  - Generic netlink message header
 *   tb    - Netlink attributes array
 *
 * Returned Value:
 *   0 on success; negative error code on failure
 *
 ****************************************************************************/

static int nl80211_cmd_get_wiphy(FAR struct netlink_conn_s *conn,
                                 FAR const struct nlmsghdr *nlmsg,
                                 FAR struct genlmsghdr *gnlh,
                                 FAR struct nlattr **tb)
{
  FAR struct net_driver_s *dev = NULL;
  FAR struct wiphy *wiphy = NULL;
  FAR struct netlink_response_s *msg;
  struct nl80211_msg m;
  int i;
  int j;
  uint32_t wiphy_idx = 0;

  /* Try to find device and wiphy in order of priority:
   * 1. If IFINDEX is provided, use it directly
   * 2. If WIPHY index is provided, find the matching wiphy
   * 3. Otherwise, find first device with a valid wiphy
   *
   * Note: Following Linux kernel convention, wiphy_idx is not stored
   * in struct wiphy. In this implementation, we use the network device
   * index as the wiphy index since there's typically a 1:1 mapping.
   */

  if (tb[NL80211_ATTR_IFINDEX])
    {
      int ifindex = nla_get_u32(tb[NL80211_ATTR_IFINDEX]);

      dev = netdev_findbyindex(ifindex);
      if (dev)
        {
          wiphy = dev->d_wiphy;
          if (wiphy)
            {
              wiphy_idx = nl80211_get_wiphy_idx(wiphy);
            }
        }
    }
  else if (tb[NL80211_ATTR_WIPHY])
    {
      wiphy_idx = nla_get_u32(tb[NL80211_ATTR_WIPHY]);

      /* Search for a device whose wiphy has matching index */

      wiphy = nl80211_find_wiphy_by_idx(wiphy_idx, &dev);
    }
  else
    {
      /* No specific device requested, find first available with wiphy */

      int k;

      for (k = 1; k <= 16; k++)
        {
          dev = netdev_findbyindex(k);
          if (dev && dev->d_wiphy)
            {
              wiphy = dev->d_wiphy;
              wiphy_idx = nl80211_get_wiphy_idx(wiphy);
              break;
            }
        }
    }

  if (!wiphy)
    {
      return -ENODEV;
    }

  /* --- Message 1: Basic Info --- */

  char phy_name[16];
  size_t buflen1;

  snprintf(phy_name, sizeof(phy_name), "phy%" PRIu32, wiphy_idx);

  buflen1 = GENL_HDRLEN
          + RTA_SPACE(sizeof(uint32_t))     /* NL80211_ATTR_WIPHY */
          + RTA_SPACE(strlen(phy_name) + 1) /* NL80211_ATTR_WIPHY_NAME */
          + RTA_SPACE(sizeof(uint8_t))      /* MAX_NUM_SCAN_SSIDS */
          + RTA_SPACE(sizeof(uint8_t));     /* MAX_NUM_SCHED_SCAN_SSIDS */

  msg = nl80211_alloc_reply_msg(nlmsg, NL80211_CMD_NEW_WIPHY,
                                (int)buflen1);
  if (!msg)
    {
      return -ENOMEM;
    }

  nl80211_msg_init(&m, msg, (int)buflen1);

  nl80211_nla_put_u32(&m, NL80211_ATTR_WIPHY, wiphy_idx);
  nl80211_nla_put_string(&m, NL80211_ATTR_WIPHY_NAME, phy_name);

  if (wiphy->max_scan_ssids)
    {
      nl80211_nla_put_u8(&m, NL80211_ATTR_MAX_NUM_SCAN_SSIDS,
                         wiphy->max_scan_ssids);
    }

  if (wiphy->max_sched_scan_ssids)
    {
      nl80211_nla_put_u8(&m, NL80211_ATTR_MAX_NUM_SCHED_SCAN_SSIDS,
                         wiphy->max_sched_scan_ssids);
    }

  nl80211_msg_finalize(&m);
  ninfo("nl80211_cmd_get_wiphy: sending msg 1 (basic), len=%lu\n",
       (unsigned long)msg->msg.nlmsg_len);
  netlink_add_response((NETLINK_HANDLE)conn, msg);

  /* --- Message 2: Supported Commands --- */

  /* NL80211_ATTR_SUPPORTED_COMMANDS
   *
   * Build the list dynamically based on what the driver actually
   * implements in its cfg80211_ops.  In particular, AUTH/ASSOC/
   * DEAUTH/DISASSOC are only advertised when the corresponding
   * callbacks are registered.  If they are NULL (typical for fullmac
   * drivers like BES), wpa_supplicant will not set
   * WPA_DRIVER_FLAGS_SME and will use NL80211_CMD_CONNECT instead,
   * letting the firmware handle auth/assoc internally.
   */

  FAR struct cfg80211_ops *ops = dev ? dev->d_cfg80211ops : NULL;
  struct rtattr *cmds_nest;
  size_t buflen2;

  uint32_t supported_cmds[40];
  int ncmds = 0;

  /* Always-present commands */

  supported_cmds[ncmds++] = NL80211_CMD_CONNECT;
  supported_cmds[ncmds++] = NL80211_CMD_DISCONNECT;

  /* SME commands: only advertise when driver provides the callbacks.
   * wpa_supplicant sets WPA_DRIVER_FLAGS_SME when it sees
   * NL80211_CMD_AUTHENTICATE in the supported list, which forces
   * userspace SME (auth + assoc frames built by supplicant).
   * Fullmac drivers that only implement .connect must NOT advertise
   * these, otherwise the supplicant bypasses .connect entirely.
   */

  if (ops && ops->auth)
    {
      supported_cmds[ncmds++] = NL80211_CMD_AUTHENTICATE;
    }

  if (ops && ops->assoc)
    {
      supported_cmds[ncmds++] = NL80211_CMD_ASSOCIATE;
    }

  if (ops && ops->deauth)
    {
      supported_cmds[ncmds++] = NL80211_CMD_DEAUTHENTICATE;
    }

  if (ops && ops->disassoc)
    {
      supported_cmds[ncmds++] = NL80211_CMD_DISASSOCIATE;
    }

  supported_cmds[ncmds++] = NL80211_CMD_TRIGGER_SCAN;
  supported_cmds[ncmds++] = NL80211_CMD_GET_INTERFACE;
  supported_cmds[ncmds++] = NL80211_CMD_GET_WIPHY;
  supported_cmds[ncmds++] = NL80211_CMD_NEW_SCAN_RESULTS;
  supported_cmds[ncmds++] = NL80211_CMD_GET_SCAN;
  supported_cmds[ncmds++] = NL80211_CMD_FRAME;
  supported_cmds[ncmds++] = NL80211_CMD_REGISTER_FRAME;
  supported_cmds[ncmds++] = NL80211_CMD_SET_WIPHY;
  supported_cmds[ncmds++] = NL80211_CMD_NEW_INTERFACE;
  supported_cmds[ncmds++] = NL80211_CMD_DEL_INTERFACE;
  supported_cmds[ncmds++] = NL80211_CMD_GET_KEY;
  supported_cmds[ncmds++] = NL80211_CMD_NEW_KEY;
  supported_cmds[ncmds++] = NL80211_CMD_DEL_KEY;
  supported_cmds[ncmds++] = NL80211_CMD_SET_KEY;
  supported_cmds[ncmds++] = NL80211_CMD_SET_STATION;
  supported_cmds[ncmds++] = NL80211_CMD_NEW_STATION;
  supported_cmds[ncmds++] = NL80211_CMD_DEL_STATION;
  supported_cmds[ncmds++] = NL80211_CMD_GET_STATION;
  supported_cmds[ncmds++] = NL80211_CMD_GET_PROTOCOL_FEATURES;
  supported_cmds[ncmds++] = NL80211_CMD_START_AP;
  supported_cmds[ncmds++] = NL80211_CMD_STOP_AP;
  supported_cmds[ncmds++] = NL80211_CMD_SET_BEACON;
  supported_cmds[ncmds++] = NL80211_CMD_SET_BSS;
  supported_cmds[ncmds++] = NL80211_CMD_PROBE_CLIENT;

  buflen2 = GENL_HDRLEN
          + RTA_SPACE(sizeof(uint32_t))          /* NL80211_ATTR_WIPHY */
          + RTA_SPACE(0)                         /* SUPPORTED_COMMANDS */
          + ncmds * RTA_SPACE(sizeof(uint32_t)); /* one u32 per command */

  msg = nl80211_alloc_reply_msg(nlmsg, NL80211_CMD_NEW_WIPHY,
                                (int)buflen2);
  if (!msg) return -ENOMEM;

  nl80211_msg_init(&m, msg, (int)buflen2);
  nl80211_nla_put_u32(&m, NL80211_ATTR_WIPHY, wiphy_idx);

  cmds_nest = nl80211_nla_nest_start(&m, NL80211_ATTR_SUPPORTED_COMMANDS);
  for (i = 0; i < ncmds; i++)
    {
      nl80211_nla_put_u32(&m, i + 1, supported_cmds[i]);
    }

  nl80211_nla_nest_end(&m, cmds_nest);

  nl80211_msg_finalize(&m);
  ninfo("nl80211_cmd_get_wiphy: sending msg 2 (cmds), len=%lu\n",
       (unsigned long)msg->msg.nlmsg_len);
  netlink_add_response((NETLINK_HANDLE)conn, msg);

  /* --- Message 3: Iftypes & Ciphers --- */

  uint32_t akm_suites[] =
  {
    0x000fac01,  /* 802.1X */
    0x000fac02,  /* PSK */
    0x000fac03,  /* FT-802.1X */
    0x000fac04,  /* FT-PSK */
    0x000fac06,  /* PSK-SHA256 */
    0x000fac08,  /* SAE */
    0x000fac09,  /* FT-SAE */
    0x000fac12,  /* OWE */
  };

  size_t combos_len = 0;
  size_t buflen3;

  /* Interface combinations are a variable-length nested structure; size
   * them from the same per-combination worst case used by the emit loop
   * below (comb + limits container + n_limits * (limit + LIMIT_MAX +
   * types container + up to 16 type flags) + MAXNUM + NUM_CHANNELS).
   */

  if (wiphy->iface_combinations && wiphy->n_iface_combinations > 0)
    {
      combos_len = RTA_SPACE(0);   /* NL80211_ATTR_INTERFACE_COMBINATIONS */
      for (i = 0; i < wiphy->n_iface_combinations; i++)
        {
          const struct ieee80211_iface_combination *comb =
            &wiphy->iface_combinations[i];

          combos_len += RTA_SPACE(0) + RTA_SPACE(0)
                      + comb->n_limits *
                          (RTA_SPACE(0) + RTA_SPACE(sizeof(uint32_t))
                           + RTA_SPACE(0) + 16 * RTA_SPACE(0))
                      + RTA_SPACE(sizeof(uint32_t))
                      + RTA_SPACE(sizeof(uint32_t));
        }
    }

  buflen3 = GENL_HDRLEN
          + RTA_SPACE(sizeof(uint32_t))       /* NL80211_ATTR_WIPHY */
          + RTA_SPACE(0) + 16 * RTA_SPACE(0)  /* SUPPORTED_IFTYPES + flags */
          + combos_len                        /* INTERFACE_COMBINATIONS */
          + RTA_SPACE(wiphy->n_cipher_suites * sizeof(uint32_t))
          + RTA_SPACE(sizeof(akm_suites))     /* AKM_SUITES */
          + RTA_SPACE(sizeof(uint32_t))       /* FEATURE_FLAGS */
          + RTA_SPACE(8)                      /* EXT_FEATURES */
          + RTA_SPACE(sizeof(uint32_t))       /* MAX_REMAIN_ON_CHANNEL */
          + RTA_SPACE(0);                     /* OFFCHANNEL_TX_OK */

  msg = nl80211_alloc_reply_msg(nlmsg, NL80211_CMD_NEW_WIPHY,
                                (int)buflen3);
  if (!msg) return -ENOMEM;

  nl80211_msg_init(&m, msg, (int)buflen3);
  nl80211_nla_put_u32(&m, NL80211_ATTR_WIPHY, wiphy_idx);

  /* NL80211_ATTR_SUPPORTED_IFTYPES */

  struct rtattr *iftypes_nest =
    nl80211_nla_nest_start(&m, NL80211_ATTR_SUPPORTED_IFTYPES);
  for (i = 0; i < 16; i++)
    {
      if (wiphy->interface_modes & (1 << i))
        {
          nl80211_nla_put_flag(&m, i);
        }
    }

  nl80211_nla_nest_end(&m, iftypes_nest);

  /* NL80211_ATTR_INTERFACE_COMBINATIONS
   *
   * Advertise valid interface combinations so wpa_supplicant can
   * determine MCC (Multi-Channel Concurrency) support.
   */

  if (wiphy->iface_combinations && wiphy->n_iface_combinations > 0)
    {
      struct rtattr *combs_nest =
        nl80211_nla_nest_start(&m, NL80211_ATTR_INTERFACE_COMBINATIONS);

      for (i = 0; i < wiphy->n_iface_combinations; i++)
        {
          const struct ieee80211_iface_combination *comb =
            &wiphy->iface_combinations[i];
          struct rtattr *comb_nest;
          struct rtattr *limits_nest;
          size_t comb_max;

          /* Fail-safe: stop before overrunning the reply buffer, the
           * way the kernel's bounds-checked nla_put would return -ENOBUFS.
           * Worst case for one combination = comb header + limits container
           * + n_limits * (limit header + LIMIT_MAX + types header + up to
           * 16 type flags) + MAXNUM + NUM_CHANNELS.
           */

          comb_max = RTA_SPACE(0) + RTA_SPACE(0)
                   + comb->n_limits *
                       (RTA_SPACE(0) + RTA_SPACE(sizeof(uint32_t))
                        + RTA_SPACE(0) + 16 * RTA_SPACE(0))
                   + RTA_SPACE(sizeof(uint32_t))
                   + RTA_SPACE(sizeof(uint32_t));

          if (nl80211_msg_tailroom(&m) < comb_max)
            {
              break;
            }

          comb_nest = nl80211_nla_nest_start(&m, i + 1);

          /* NL80211_IFACE_COMB_LIMITS */

          limits_nest =
            nl80211_nla_nest_start(&m, NL80211_IFACE_COMB_LIMITS);

          for (j = 0; j < comb->n_limits; j++)
            {
              const struct ieee80211_iface_limit *limit =
                &comb->limits[j];
              struct rtattr *lim_nest = nl80211_nla_nest_start(&m, j + 1);
              struct rtattr *types_nest;
              int t;

              /* NL80211_IFACE_LIMIT_MAX */

              nl80211_nla_put_u32(&m, NL80211_IFACE_LIMIT_MAX, limit->max);

              /* NL80211_IFACE_LIMIT_TYPES */

              types_nest =
                nl80211_nla_nest_start(&m, NL80211_IFACE_LIMIT_TYPES);
              for (t = 0; t < 16; t++)
                {
                  if (limit->types & (1 << t))
                    {
                      nl80211_nla_put_flag(&m, t);
                    }
                }

              nl80211_nla_nest_end(&m, types_nest);
              nl80211_nla_nest_end(&m, lim_nest);
            }

          nl80211_nla_nest_end(&m, limits_nest);

          /* NL80211_IFACE_COMB_MAXNUM / NUM_CHANNELS */

          nl80211_nla_put_u32(&m, NL80211_IFACE_COMB_MAXNUM,
                              comb->max_interfaces);
          nl80211_nla_put_u32(&m, NL80211_IFACE_COMB_NUM_CHANNELS,
                              comb->num_different_channels);

          nl80211_nla_nest_end(&m, comb_nest);
        }

      nl80211_nla_nest_end(&m, combs_nest);
    }

  /* NL80211_ATTR_CIPHER_SUITES */

  if (wiphy->n_cipher_suites > 0)
    {
      nl80211_nla_put(&m, NL80211_ATTR_CIPHER_SUITES,
                      wiphy->n_cipher_suites * sizeof(uint32_t),
                      wiphy->cipher_suites);
    }

  /* NL80211_ATTR_AKM_SUITES - advertise supported AKM suites
   * These are RSN OUI values that wpa_supplicant uses to determine
   * which key management algorithms the driver supports.
   */

  nl80211_nla_put(&m, NL80211_ATTR_AKM_SUITES, sizeof(akm_suites),
                  akm_suites);

  /* NL80211_ATTR_FEATURE_FLAGS - advertise SAE support */

  nl80211_nla_put_u32(&m, NL80211_ATTR_FEATURE_FLAGS,
                      NL80211_FEATURE_SAE | NL80211_FEATURE_SK_TX_STATUS);

  /* NL80211_ATTR_EXT_FEATURES - advertise SAE password support */

  uint8_t ext_features[8];
  memset(ext_features, 0, sizeof(ext_features));
  nl80211_nla_put(&m, NL80211_ATTR_EXT_FEATURES, sizeof(ext_features),
                  ext_features);

  /* NL80211_ATTR_MAX_REMAIN_ON_CHANNEL_DURATION */

  nl80211_nla_put_u32(&m, NL80211_ATTR_MAX_REMAIN_ON_CHANNEL_DURATION,
                      5000);

  /* NL80211_ATTR_OFFCHANNEL_TX_OK - flag attribute (zero-length)
   * Tells wpa_supplicant that the driver supports off-channel TX.
   * wpa_supplicant sets WPA_DRIVER_FLAGS_OFFCHANNEL_TX and then
   * includes NL80211_ATTR_OFFCHANNEL_TX_OK in CMD_FRAME requests,
   * which is required for P2P action frames (e.g. Provision Discovery).
   */

  nl80211_nla_put_flag(&m, NL80211_ATTR_OFFCHANNEL_TX_OK);

  /* NL80211_ATTR_DEVICE_AP_SME removed:
   * Without device_ap_sme, wpa_supplicant handles AUTH/ASSOC in software.
   * use_monitor will be 0 because we report both NL80211_CMD_PROBE_CLIENT
   * and NL80211_FEATURE_SK_TX_STATUS, so no monitor interface is created.
   * wpa_supplicant subscribes to AUTH/ASSOC/DEAUTH/DISASSOC/PROBE_REQ
   * via nl80211_mgmt_subscribe_ap() and handles them in hostapd code.
   */

  nl80211_msg_finalize(&m);
  ninfo("nl80211_cmd_get_wiphy: sending msg 3 (iftypes/ciphers/akm), "
       "len=%lu\n", (unsigned long)msg->msg.nlmsg_len);
  netlink_add_response((NETLINK_HANDLE)conn, msg);

  /* --- Message 4+: Bands (One msg per band) --- */

  for (i = 0; i < NUM_NL80211_BANDS; i++)
    {
      if (!wiphy->bands[i])
        {
          continue;
        }

      struct ieee80211_supported_band *band = wiphy->bands[i];

      /* Size the reply from the attributes actually emitted below, using
       * RTA_SPACE(sizeof(...)) for each one instead of guessed constants.
       * Unlike the kernel's nla_put, the raw writers here are not
       * individually bounds-checked, so the buffer must be large enough;
       * the HE loop additionally guards against overrun (see he_entry_max).
       */

      size_t he_entry_max = 0;
      if (band->iftype_data && band->n_iftype_data > 0)
        {
          const struct ieee80211_sta_he_cap *hc =
            &band->iftype_data[0].he_cap;

          /* Per-iftype: entry hdr + iftypes flags + MAC + PHY + MCS + PPE */

          he_entry_max = RTA_SPACE(0)
            + RTA_SPACE((NL80211_IFTYPE_MAX + 1) * RTA_SPACE(0))
            + RTA_SPACE(sizeof(hc->he_cap_elem.mac_cap_info))
            + RTA_SPACE(sizeof(hc->he_cap_elem.phy_cap_info))
            + RTA_SPACE(sizeof(hc->he_mcs_nss_supp))
            + RTA_SPACE(sizeof(hc->ppe_thres));
        }

      /* WIPHY + BANDS/band/FREQS hdrs + per-channel freqs + RATES +
       * HT/VHT fixed attrs + IFTYPE_DATA (HE) + genl hdr/alignment slop.
       */

      size_t band_buflen = RTA_SPACE(sizeof(uint32_t))
        + RTA_SPACE(0) * 3
        + band->n_channels *
            (RTA_SPACE(0) + RTA_SPACE(sizeof(uint32_t)) * 2 +
             RTA_SPACE(0) * 3)
        + RTA_SPACE(0)
        + band->n_bitrates *
            (RTA_SPACE(0) + RTA_SPACE(sizeof(uint32_t)) + RTA_SPACE(0))
        + RTA_SPACE(sizeof(uint16_t))
        + RTA_SPACE(sizeof(uint8_t)) * 2
        + RTA_SPACE(16)
        + RTA_SPACE(sizeof(uint32_t))
        + RTA_SPACE(sizeof(band->vht_cap.vht_mcs))
        + RTA_SPACE(0)
        + band->n_iftype_data * he_entry_max
        + 32;

      msg = nl80211_alloc_reply_msg(nlmsg, NL80211_CMD_NEW_WIPHY,
                                    (int)band_buflen);
      if (!msg) return -ENOMEM;

      nl80211_msg_init(&m, msg, (int)band_buflen);

      nl80211_nla_put_u32(&m, NL80211_ATTR_WIPHY, wiphy_idx);

      /* NL80211_ATTR_WIPHY_BANDS -> band[i] -> FREQS */

      struct rtattr *bands_nest =
        nl80211_nla_nest_start(&m, NL80211_ATTR_WIPHY_BANDS);
      struct rtattr *band_nest = nl80211_nla_nest_start(&m, i);
      struct rtattr *freqs_nest =
        nl80211_nla_nest_start(&m, NL80211_BAND_ATTR_FREQS);

      for (j = 0; j < band->n_channels; j++)
        {
          struct ieee80211_channel *chan = &band->channels[j];
          struct rtattr *freq_nest = nl80211_nla_nest_start(&m, j);

          /* NL80211_FREQUENCY_ATTR_FREQ */

          nl80211_nla_put_u32(&m, NL80211_FREQUENCY_ATTR_FREQ,
                              chan->center_freq);

          /* NL80211_FREQUENCY_ATTR_DISABLED */

          if (chan->flags & IEEE80211_CHAN_DISABLED)
            {
              nl80211_nla_put_flag(&m, NL80211_FREQUENCY_ATTR_DISABLED);
            }

          /* NL80211_FREQUENCY_ATTR_NO_IR */

          if (chan->flags & IEEE80211_CHAN_NO_IR)
            {
              nl80211_nla_put_flag(&m, NL80211_FREQUENCY_ATTR_NO_IR);
            }

          /* NL80211_FREQUENCY_ATTR_RADAR */

          if (chan->flags & IEEE80211_CHAN_RADAR)
            {
              nl80211_nla_put_flag(&m, NL80211_FREQUENCY_ATTR_RADAR);
            }

          /* NL80211_FREQUENCY_ATTR_MAX_TX_POWER (dBm to mBm) */

          if (chan->max_power)
            {
              nl80211_nla_put_u32(&m, NL80211_FREQUENCY_ATTR_MAX_TX_POWER,
                                  chan->max_power * 100);
            }

          nl80211_nla_nest_end(&m, freq_nest);
        }

      nl80211_nla_nest_end(&m, freqs_nest);

      /* NL80211_BAND_ATTR_RATES */

      if (band->n_bitrates > 0)
        {
          struct rtattr *rates_nest =
            nl80211_nla_nest_start(&m, NL80211_BAND_ATTR_RATES);

          for (j = 0; j < band->n_bitrates; j++)
            {
              struct rtattr *rate_nest = nl80211_nla_nest_start(&m, j);

              /* NL80211_BITRATE_ATTR_RATE */

              nl80211_nla_put_u32(&m, NL80211_BITRATE_ATTR_RATE,
                                  band->bitrates[j].bitrate);

              /* NL80211_BITRATE_ATTR_2GHZ_SHORTPREAMBLE */

              if (band->bitrates[j].flags &
                  IEEE80211_RATE_SHORT_PREAMBLE)
                {
                  nl80211_nla_put_flag(&m,
                    NL80211_BITRATE_ATTR_2GHZ_SHORTPREAMBLE);
                }

              nl80211_nla_nest_end(&m, rate_nest);
            }

          nl80211_nla_nest_end(&m, rates_nest);
        }

      /* NL80211_BAND_ATTR_HT_CAPA */

      if (band->ht_cap.ht_supported)
        {
          FAR void *mcs;

          nl80211_nla_put_u16(&m, NL80211_BAND_ATTR_HT_CAPA,
                              band->ht_cap.cap);
          nl80211_nla_put_u8(&m, NL80211_BAND_ATTR_HT_AMPDU_FACTOR,
                             band->ht_cap.ampdu_factor);
          nl80211_nla_put_u8(&m, NL80211_BAND_ATTR_HT_AMPDU_DENSITY,
                             band->ht_cap.ampdu_density);

          /* NL80211_BAND_ATTR_HT_MCS_SET (16 bytes, zero-padded) */

          mcs = nl80211_nla_reserve(&m, NL80211_BAND_ATTR_HT_MCS_SET, 16);
          if (mcs != NULL)
            {
              memcpy(mcs, band->ht_cap.mcs.rx_mask,
                     sizeof(band->ht_cap.mcs.rx_mask));
            }
        }

      /* NL80211_BAND_ATTR_VHT_CAPA */

      if (band->vht_cap.vht_supported)
        {
          nl80211_nla_put_u32(&m, NL80211_BAND_ATTR_VHT_CAPA,
                              band->vht_cap.cap);
          nl80211_nla_put(&m, NL80211_BAND_ATTR_VHT_MCS_SET,
                          sizeof(band->vht_cap.vht_mcs),
                          &band->vht_cap.vht_mcs);
        }

      /* NL80211_BAND_ATTR_IFTYPE_DATA (HE capability) */

      if (band->iftype_data && band->n_iftype_data > 0)
        {
          struct rtattr *iftype_data_nest;
          int idx;
          int n_emitted = 0;

          iftype_data_nest =
            nl80211_nla_nest_start(&m, NL80211_BAND_ATTR_IFTYPE_DATA);

          for (idx = 0; idx < band->n_iftype_data; idx++)
            {
              const struct ieee80211_sband_iftype_data *iftype_data =
                &band->iftype_data[idx];
              const struct ieee80211_sta_he_cap *hc =
                &iftype_data->he_cap;
              struct rtattr *iftype_entry;
              struct rtattr *iftype_flags_nest;
              int k;

              if (!hc->has_he)
                {
                  continue;
                }

              /* Fail-safe: stop before overrunning the reply buffer, the
               * way the kernel's bounds-checked nla_put returns -ENOBUFS.
               */

              if (nl80211_msg_tailroom(&m) < he_entry_max)
                {
                  break;
                }

              /* Nested iftype entry.  The attribute type is the 1-based
               * index of the entry (kernel uses nla_nest_start(msg, i + 1)).
               */

              iftype_entry = nl80211_nla_nest_start(&m, n_emitted + 1);
              n_emitted++;

              /* NL80211_BAND_IFTYPE_ATTR_IFTYPES (nested flags) */

              iftype_flags_nest =
                nl80211_nla_nest_start(&m,
                                       NL80211_BAND_IFTYPE_ATTR_IFTYPES);
              for (k = 0; k <= NL80211_IFTYPE_MAX; k++)
                {
                  if (iftype_data->types_mask & BIT(k))
                    {
                      nl80211_nla_put_flag(&m, k);
                    }
                }

              nl80211_nla_nest_end(&m, iftype_flags_nest);

              /* HE capability MAC/PHY/MCS/PPE */

              nl80211_nla_put(&m, NL80211_BAND_IFTYPE_ATTR_HE_CAP_MAC,
                              sizeof(hc->he_cap_elem.mac_cap_info),
                              hc->he_cap_elem.mac_cap_info);
              nl80211_nla_put(&m, NL80211_BAND_IFTYPE_ATTR_HE_CAP_PHY,
                              sizeof(hc->he_cap_elem.phy_cap_info),
                              hc->he_cap_elem.phy_cap_info);
              nl80211_nla_put(&m, NL80211_BAND_IFTYPE_ATTR_HE_CAP_MCS_SET,
                              sizeof(hc->he_mcs_nss_supp),
                              &hc->he_mcs_nss_supp);
              nl80211_nla_put(&m, NL80211_BAND_IFTYPE_ATTR_HE_CAP_PPE,
                              sizeof(hc->ppe_thres),
                              hc->ppe_thres);

              nl80211_nla_nest_end(&m, iftype_entry);
            }

          /* Only keep the IFTYPE_DATA container if at least one iftype
           * advertised HE; otherwise cancel it so no empty attribute is
           * added to the message.
           */

          if (n_emitted > 0)
            {
              nl80211_nla_nest_end(&m, iftype_data_nest);
            }
          else
            {
              nl80211_nla_nest_cancel(&m, iftype_data_nest);
            }
        }

      nl80211_nla_nest_end(&m, band_nest);
      nl80211_nla_nest_end(&m, bands_nest);

      nl80211_msg_finalize(&m);
      ninfo("nl80211_cmd_get_wiphy: sending msg 4+ (band %d), len=%lu\n",
           i, (unsigned long)msg->msg.nlmsg_len);
      netlink_add_response((NETLINK_HANDLE)conn, msg);
    }

  nl80211_send_done_msg(conn, nlmsg);

  return 0;
}

/****************************************************************************
 * Name: nl80211_cmd_get_interface
 *
 * Description:
 *   Handle NL80211_CMD_GET_INTERFACE command. Returns network interface
 *   information including interface type, MAC address, and wiphy index.
 *
 * Input Parameters:
 *   conn  - Netlink connection
 *   nlmsg - Netlink message header
 *   gnlh  - Generic netlink message header
 *   tb    - Netlink attributes array
 *
 * Returned Value:
 *   0 on success; negative error code on failure
 *
 ****************************************************************************/

static int nl80211_cmd_get_interface(FAR struct netlink_conn_s *conn,
                                     FAR const struct nlmsghdr *nlmsg,
                                     FAR struct genlmsghdr *gnlh,
                                     FAR struct nlattr **tb)
{
  FAR struct net_driver_s *dev;
  FAR struct netlink_response_s *msg;
  FAR struct genlmsghdr *resp_gnlh;
  FAR const uint8_t *mac;
  struct nl80211_msg m;
  uint32_t wiphy_idx = 0;
  uint32_t iftype = NL80211_IFTYPE_STATION;
  size_t buflen;
  int ifindex;

  ninfo("nl80211_cmd_get_interface: type=%d seq=%lu pid=%lu\n",
       nlmsg->nlmsg_type, (unsigned long)nlmsg->nlmsg_seq,
       (unsigned long)nlmsg->nlmsg_pid);

  if (!tb[NL80211_ATTR_IFINDEX])
    {
      nerr("ERROR: nl80211_cmd_get_interface: missing ifindex\n");
      return -EINVAL;
    }

  ifindex = nla_get_u32(tb[NL80211_ATTR_IFINDEX]);
  ninfo("nl80211_cmd_get_interface: ifindex=%d\n", ifindex);

  dev = netdev_findbyindex(ifindex);
  if (!dev)
    {
      nerr("ERROR: nl80211_cmd_get_interface: device not found\n");
      return -ENODEV;
    }

  /* Allocate response (single-shot reply, not a multipart dump) */

  buflen = GENL_HDRLEN
         + RTA_SPACE(sizeof(uint32_t))   /* NL80211_ATTR_IFINDEX */
         + RTA_SPACE(IFNAMSIZ)           /* NL80211_ATTR_IFNAME  */
         + RTA_SPACE(sizeof(uint32_t))   /* NL80211_ATTR_WIPHY   */
         + RTA_SPACE(sizeof(uint32_t))   /* NL80211_ATTR_IFTYPE  */
         + RTA_SPACE(6);                 /* NL80211_ATTR_MAC     */

  msg = (FAR struct netlink_response_s *)
        kmm_zalloc(sizeof(struct netlink_response_s) + buflen);
  if (!msg)
    {
      return -ENOMEM;
    }

  msg->msg.nlmsg_type = nlmsg->nlmsg_type;
  msg->msg.nlmsg_flags = 0;
  msg->msg.nlmsg_seq = nlmsg->nlmsg_seq;
  msg->msg.nlmsg_pid = nlmsg->nlmsg_pid;

  resp_gnlh = (FAR struct genlmsghdr *)NLMSG_DATA(&msg->msg);
  resp_gnlh->cmd = NL80211_CMD_NEW_INTERFACE;
  resp_gnlh->version = 1;

  if (dev->d_wiphy != NULL)
    {
      uint32_t idx = nl80211_get_wiphy_idx((FAR struct wiphy *)dev->d_wiphy);
      if (idx != UINT32_MAX)
        {
          wiphy_idx = idx;
        }
    }

  if (dev->d_wdev)
    {
      FAR struct wireless_dev *wdev = (FAR struct wireless_dev *)dev->d_wdev;
      iftype = wdev->iftype;
    }

  mac = dev->d_wiphy ? dev->d_wiphy->perm_addr
                     : dev->d_mac.ether.ether_addr_octet;

  nl80211_msg_init(&m, msg, buflen);

  nl80211_nla_put_u32(&m, NL80211_ATTR_IFINDEX, dev->d_ifindex);
  nl80211_nla_put_string(&m, NL80211_ATTR_IFNAME, dev->d_ifname);
  nl80211_nla_put_u32(&m, NL80211_ATTR_WIPHY, wiphy_idx);
  nl80211_nla_put_u32(&m, NL80211_ATTR_IFTYPE, iftype);
  nl80211_nla_put(&m, NL80211_ATTR_MAC, 6, mac);

  nl80211_msg_finalize(&m);
  netlink_add_response((NETLINK_HANDLE)conn, msg);

  return 0;
}

/****************************************************************************
 * Name: nl80211_cmd_set_interface
 *
 * Description:
 *   Handle NL80211_CMD_SET_INTERFACE command. Changes interface type
 *   (e.g., from STATION to AP mode).
 *
 * Input Parameters:
 *   conn  - Netlink connection
 *   nlmsg - Netlink message header
 *   gnlh  - Generic netlink message header
 *   tb    - Netlink attributes array
 *
 * Returned Value:
 *   0 on success; negative error code on failure
 *
 ****************************************************************************/

static int nl80211_cmd_set_interface(FAR struct netlink_conn_s *conn,
                                     FAR const struct nlmsghdr *nlmsg,
                                     FAR struct genlmsghdr *gnlh,
                                     FAR struct nlattr **tb)
{
  FAR struct net_driver_s *dev;
  FAR struct cfg80211_ops *ops;
  int ifindex;
  uint32_t iftype;
  int ret;

  ninfo("nl80211_cmd_set_interface: called\n");

  if (!tb[NL80211_ATTR_IFINDEX])
    {
      return -EINVAL;
    }

  ifindex = nla_get_u32(tb[NL80211_ATTR_IFINDEX]);
  dev = netdev_findbyindex(ifindex);
  if (!dev)
    {
      return -ENODEV;
    }

  if (tb[NL80211_ATTR_IFTYPE])
    {
      iftype = nla_get_u32(tb[NL80211_ATTR_IFTYPE]);
      ninfo("nl80211_cmd_set_interface: setting ifindex %d to type %lu\n",
           ifindex, (unsigned long)iftype);

      /* Map P2P iftypes to their base types for firmware compatibility.
       * P2P_CLIENT is functionally STA, P2P_GO is functionally AP.
       * The P2P protocol is handled by wpa_supplicant, not firmware.
       */

      uint32_t hw_iftype = iftype;
      if (iftype == NL80211_IFTYPE_P2P_CLIENT)
        {
          hw_iftype = NL80211_IFTYPE_STATION;
        }
      else if (iftype == NL80211_IFTYPE_P2P_GO)
        {
          hw_iftype = NL80211_IFTYPE_AP;
        }

      ops = dev->d_cfg80211ops;
      if (ops && ops->change_virtual_intf)
        {
          ret = ops->change_virtual_intf(dev->d_wiphy, dev,
                                         (enum nl80211_iftype)hw_iftype,
                                         NULL);
          if (ret < 0)
            {
              return ret;
            }
        }
    }

  return 0;
}

/****************************************************************************
 * Name: nl80211_cmd_trigger_scan
 *
 * Description:
 *   Handle NL80211_CMD_TRIGGER_SCAN command. Initiates a wireless network
 *   scan with specified SSIDs and channels.
 *
 * Input Parameters:
 *   conn  - Netlink connection
 *   nlmsg - Netlink message header
 *   gnlh  - Generic netlink message header
 *   tb    - Netlink attributes array
 *
 * Returned Value:
 *   0 on success; negative error code on failure
 *
 ****************************************************************************/

static int nl80211_cmd_trigger_scan(FAR struct netlink_conn_s *conn,
                                    FAR const struct nlmsghdr *nlmsg,
                                    FAR struct genlmsghdr *gnlh,
                                    FAR struct nlattr **tb)
{
  FAR struct net_driver_s *dev;
  FAR struct cfg80211_ops *ops;
  FAR struct cfg80211_scan_request *request;
  FAR struct nlattr *attr;
  int ifindex;
  int rem;
  int ret;
  int n_ssids = 0;
  int n_channels = 0;
  int i;

  if (!tb[NL80211_ATTR_IFINDEX])
    {
      return -EINVAL;
    }

  ifindex = nla_get_u32(tb[NL80211_ATTR_IFINDEX]);
  dev = netdev_findbyindex(ifindex);
  if (!dev)
    {
      return -ENODEV;
    }

  ops = dev->d_cfg80211ops;
  if (!ops || !ops->scan)
    {
      return -EOPNOTSUPP;
    }

  /* Count SSIDs */

  if (tb[NL80211_ATTR_SCAN_SSIDS])
    {
      nla_for_each_nested(attr, tb[NL80211_ATTR_SCAN_SSIDS], rem)
        {
          n_ssids++;
        }
    }

  ninfo("nl80211_cmd_trigger_scan: n_ssids = %d\n", n_ssids);

  /* Count Channels */

  if (tb[NL80211_ATTR_SCAN_FREQUENCIES])
    {
      nla_for_each_nested(attr, tb[NL80211_ATTR_SCAN_FREQUENCIES], rem)
        {
          n_channels++;
        }
    }

  ninfo("nl80211_cmd_trigger_scan: n_channels = %d\n", n_channels);

  /* Allocate scan request */

  request = (FAR struct cfg80211_scan_request *)kmm_zalloc(
      sizeof(struct cfg80211_scan_request) +
      n_channels * sizeof(struct ieee80211_channel *) +
      n_ssids * sizeof(struct cfg80211_ssid));

  if (request == NULL)
    {
      return -ENOMEM;
    }

  request->wiphy = dev->d_wiphy;
  request->wdev = dev->d_wdev;
  request->n_ssids = n_ssids;
  request->n_channels = n_channels;

  /* Populate BSSID */

  if (tb[NL80211_ATTR_BSSID])
    {
      memcpy(request->bssid, nla_data(tb[NL80211_ATTR_BSSID]), ETH_ALEN);
    }
  else if (tb[NL80211_ATTR_MAC])
    {
      memcpy(request->bssid, nla_data(tb[NL80211_ATTR_MAC]), ETH_ALEN);
    }
  else
    {
      memset(request->bssid, 0xff, ETH_ALEN);
    }

  /* Populate SSIDs */

  if (n_ssids > 0)
    {
      request->ssids =
        (FAR struct cfg80211_ssid *)(request->channels + n_channels);
      i = 0;
      nla_for_each_nested(attr, tb[NL80211_ATTR_SCAN_SSIDS], rem)
        {
          request->ssids[i].ssid_len = nla_len(attr);
          if (request->ssids[i].ssid_len > IEEE80211_MAX_SSID_LEN)
            {
              request->ssids[i].ssid_len = IEEE80211_MAX_SSID_LEN;
            }

          memcpy(request->ssids[i].ssid, nla_data(attr),
                 request->ssids[i].ssid_len);
          i++;
        }
    }

  /* Populate Channels */

  if (n_channels > 0)
    {
      i = 0;
      nla_for_each_nested(attr, tb[NL80211_ATTR_SCAN_FREQUENCIES], rem)
        {
          uint32_t freq = nla_get_u32(attr);
          request->channels[i] = nl80211_get_channel(freq);
          if (request->channels[i] == NULL)
            {
              nerr("ERROR: Failed to get channel for freq %u\n", freq);
              ret = -ENOMEM;
              goto free_channels;
            }

          i++;
        }
    }

  /* Other attributes */

  if (tb[NL80211_ATTR_IE])
    {
      request->ie = nla_data(tb[NL80211_ATTR_IE]);
      request->ie_len = nla_len(tb[NL80211_ATTR_IE]);
    }

  if (tb[NL80211_ATTR_MEASUREMENT_DURATION])
    {
      request->duration = nla_get_u32(tb[NL80211_ATTR_MEASUREMENT_DURATION]);
    }

  if (tb[NL80211_ATTR_MEASUREMENT_DURATION_MANDATORY])
    {
      request->duration_mandatory = true;
    }

  /* Call driver */

  ninfo("nl80211_cmd_trigger_scan: calling driver scan\n");
  ret = ops->scan(dev->d_wiphy, request);

free_channels:

  /* Free channels */

  for (i = 0; i < n_channels; i++)
    {
      if (request->channels[i])
        {
          kmm_free(request->channels[i]);
        }
    }

  kmm_free(request);

  if (ret < 0)
    {
      return ret;
    }

  return 0;
}

/****************************************************************************
 * Name: nl80211_cmd_abort_scan
 *
 * Description:
 *   Handle NL80211_CMD_ABORT_SCAN command. Aborts an ongoing wireless
 *   network scan. The driver indicates the status of the scan through
 *   cfg80211_scan_done().
 *
 * Input Parameters:
 *   conn  - Netlink connection
 *   nlmsg - Netlink message header
 *   gnlh  - Generic netlink message header
 *   tb    - Netlink attributes array
 *
 * Returned Value:
 *   0 on success; negative error code on failure
 *
 ****************************************************************************/

static int nl80211_cmd_abort_scan(FAR struct netlink_conn_s *conn,
                                  FAR const struct nlmsghdr *nlmsg,
                                  FAR struct genlmsghdr *gnlh,
                                  FAR struct nlattr **tb)
{
  FAR struct net_driver_s *dev;
  FAR struct cfg80211_ops *ops;
  int ifindex;

  if (!tb[NL80211_ATTR_IFINDEX])
    {
      return -EINVAL;
    }

  ifindex = nla_get_u32(tb[NL80211_ATTR_IFINDEX]);
  dev = netdev_findbyindex(ifindex);
  if (!dev)
    {
      return -ENODEV;
    }

  ops = dev->d_cfg80211ops;
  if (!ops || !ops->abort_scan)
    {
      return -EOPNOTSUPP;
    }

  ops->abort_scan(dev->d_wiphy, dev->d_wdev);
  return 0;
}

/****************************************************************************
 * Name: nl80211_parse_key_attrs
 *
 * Description:
 *   Parse nested key attributes from NL80211_ATTR_KEY
 *
 * Input Parameters:
 *   key_attr - Nested key attribute
 *   key_tb   - Array to store parsed key attributes
 *
 * Returned Value:
 *   0 on success; negative error code on failure
 *
 ****************************************************************************/

static int nl80211_parse_key_attrs(FAR struct nlattr *key_attr,
                                   FAR struct nlattr **key_tb)
{
  FAR struct nlattr *pos;
  int rem;
  int type;

  memset(key_tb, 0, sizeof(FAR struct nlattr *) *
         (__NL80211_KEY_AFTER_LAST));

  nla_for_each_nested(pos, key_attr, rem)
    {
      type = nla_type(pos);
      if (type < __NL80211_KEY_AFTER_LAST)
        {
          key_tb[type] = pos;
        }
    }

  return 0;
}

/****************************************************************************
 * Name: nl80211_cmd_del_key
 *
 * Description:
 *   Handle NL80211_CMD_DEL_KEY command. Deletes a cryptographic key.
 *
 * Input Parameters:
 *   conn  - Netlink connection
 *   nlmsg - Netlink message header
 *   gnlh  - Generic netlink message header
 *   tb    - Netlink attributes array
 *
 * Returned Value:
 *   0 on success; negative error code on failure
 *
 ****************************************************************************/

static int nl80211_cmd_del_key(FAR struct netlink_conn_s *conn,
                               FAR const struct nlmsghdr *nlmsg,
                               FAR struct genlmsghdr *gnlh,
                               FAR struct nlattr **tb)
{
  FAR struct net_driver_s *dev;
  FAR struct cfg80211_ops *ops;
  FAR struct nlattr *key_tb[__NL80211_KEY_AFTER_LAST];
  uint8_t key_idx = 0;
  bool pairwise = false;
  FAR const uint8_t *mac_addr = NULL;
  int ifindex;
  int ret;

  if (!tb[NL80211_ATTR_IFINDEX])
    {
      return -EINVAL;
    }

  ifindex = nla_get_u32(tb[NL80211_ATTR_IFINDEX]);
  dev = netdev_findbyindex(ifindex);
  if (!dev)
    {
      return -ENODEV;
    }

  ops = dev->d_cfg80211ops;
  if (!ops || !ops->del_key)
    {
      return -EOPNOTSUPP;
    }

  if (tb[NL80211_ATTR_KEY])
    {
      nl80211_parse_key_attrs(tb[NL80211_ATTR_KEY], key_tb);
      if (key_tb[NL80211_KEY_IDX])
        {
          key_idx = *(FAR uint8_t *)nla_data(key_tb[NL80211_KEY_IDX]);
        }
    }
  else if (tb[NL80211_ATTR_KEY_IDX])
    {
      key_idx = *(FAR uint8_t *)nla_data(tb[NL80211_ATTR_KEY_IDX]);
    }

  if (tb[NL80211_ATTR_MAC])
    {
      mac_addr = nla_data(tb[NL80211_ATTR_MAC]);
      pairwise = true;
    }

  ret = ops->del_key(dev->d_wiphy, dev, -1, key_idx, pairwise, mac_addr);

  /* -ENOENT is acceptable for del_key (key doesn't exist) */

  if (ret == -ENOENT)
    {
      ret = 0;
    }

  return ret;
}

/****************************************************************************
 * Name: nl80211_cmd_new_key
 *
 * Description:
 *   Handle NL80211_CMD_NEW_KEY command. Adds a new cryptographic key.
 *
 * Input Parameters:
 *   conn  - Netlink connection
 *   nlmsg - Netlink message header
 *   gnlh  - Generic netlink message header
 *   tb    - Netlink attributes array
 *
 * Returned Value:
 *   0 on success; negative error code on failure
 *
 ****************************************************************************/

static int nl80211_cmd_new_key(FAR struct netlink_conn_s *conn,
                               FAR const struct nlmsghdr *nlmsg,
                               FAR struct genlmsghdr *gnlh,
                               FAR struct nlattr **tb)
{
  FAR struct net_driver_s *dev;
  FAR struct cfg80211_ops *ops;
  FAR struct nlattr *key_tb[__NL80211_KEY_AFTER_LAST];
  struct key_params params;
  uint8_t key_idx = 0;
  bool pairwise = false;
  FAR const uint8_t *mac_addr = NULL;
  int ifindex;
  int ret;

  if (!tb[NL80211_ATTR_IFINDEX])
    {
      return -EINVAL;
    }

  ifindex = nla_get_u32(tb[NL80211_ATTR_IFINDEX]);
  dev = netdev_findbyindex(ifindex);
  if (!dev)
    {
      return -ENODEV;
    }

  ops = dev->d_cfg80211ops;
  if (!ops || !ops->add_key)
    {
      return -EOPNOTSUPP;
    }

  memset(&params, 0, sizeof(params));

  if (tb[NL80211_ATTR_KEY])
    {
      nl80211_parse_key_attrs(tb[NL80211_ATTR_KEY], key_tb);
      if (key_tb[NL80211_KEY_DATA])
        {
          params.key = nla_data(key_tb[NL80211_KEY_DATA]);
          params.key_len = nla_len(key_tb[NL80211_KEY_DATA]);
        }

      if (key_tb[NL80211_KEY_IDX])
        {
          key_idx = *(FAR uint8_t *)nla_data(key_tb[NL80211_KEY_IDX]);
        }

      if (key_tb[NL80211_KEY_CIPHER])
        {
          params.cipher = nla_get_u32(key_tb[NL80211_KEY_CIPHER]);
        }

      if (key_tb[NL80211_KEY_SEQ])
        {
          params.seq = nla_data(key_tb[NL80211_KEY_SEQ]);
          params.seq_len = nla_len(key_tb[NL80211_KEY_SEQ]);
        }

      if (key_tb[NL80211_KEY_MODE])
        {
          params.mode = *(FAR uint8_t *)nla_data(key_tb[NL80211_KEY_MODE]);
        }
    }

  /* Also check top-level attributes as fallback */

  if (!params.key && tb[NL80211_ATTR_KEY_DATA])
    {
      params.key = nla_data(tb[NL80211_ATTR_KEY_DATA]);
      params.key_len = nla_len(tb[NL80211_ATTR_KEY_DATA]);
    }

  if (tb[NL80211_ATTR_KEY_IDX])
    {
      key_idx = *(FAR uint8_t *)nla_data(tb[NL80211_ATTR_KEY_IDX]);
    }

  if (!params.cipher && tb[NL80211_ATTR_KEY_CIPHER])
    {
      params.cipher = nla_get_u32(tb[NL80211_ATTR_KEY_CIPHER]);
    }

  if (!params.seq && tb[NL80211_ATTR_KEY_SEQ])
    {
      params.seq = nla_data(tb[NL80211_ATTR_KEY_SEQ]);
      params.seq_len = nla_len(tb[NL80211_ATTR_KEY_SEQ]);
    }

  if (tb[NL80211_ATTR_MAC])
    {
      mac_addr = nla_data(tb[NL80211_ATTR_MAC]);
      pairwise = true;
    }

  ret = ops->add_key(dev->d_wiphy, dev, -1, key_idx,
                     pairwise, mac_addr, &params);
  return ret;
}

/****************************************************************************
 * Name: nl80211_cmd_set_key
 *
 * Description:
 *   Handle NL80211_CMD_SET_KEY command. Sets key as default TX/RX key.
 *
 * Input Parameters:
 *   conn  - Netlink connection
 *   nlmsg - Netlink message header
 *   gnlh  - Generic netlink message header
 *   tb    - Netlink attributes array
 *
 * Returned Value:
 *   0 on success; negative error code on failure
 *
 ****************************************************************************/

static int nl80211_cmd_set_key(FAR struct netlink_conn_s *conn,
                               FAR const struct nlmsghdr *nlmsg,
                               FAR struct genlmsghdr *gnlh,
                               FAR struct nlattr **tb)
{
  FAR struct net_driver_s *dev;
  FAR struct cfg80211_ops *ops;
  FAR struct nlattr *key_tb[__NL80211_KEY_AFTER_LAST];
  uint8_t key_idx = 0;
  bool unicast = false;
  bool multicast = false;
  int ifindex;
  int ret;

  if (!tb[NL80211_ATTR_IFINDEX])
    {
      return -EINVAL;
    }

  ifindex = nla_get_u32(tb[NL80211_ATTR_IFINDEX]);
  dev = netdev_findbyindex(ifindex);
  if (!dev)
    {
      return -ENODEV;
    }

  ops = dev->d_cfg80211ops;
  if (!ops || !ops->set_default_key)
    {
      return -EOPNOTSUPP;
    }

  if (tb[NL80211_ATTR_KEY])
    {
      nl80211_parse_key_attrs(tb[NL80211_ATTR_KEY], key_tb);
      if (key_tb[NL80211_KEY_IDX])
        {
          key_idx = *(FAR uint8_t *)nla_data(key_tb[NL80211_KEY_IDX]);
        }

      if (key_tb[NL80211_KEY_DEFAULT])
        {
          /* Check default types if present */

          if (key_tb[NL80211_KEY_DEFAULT_TYPES])
            {
              FAR struct nlattr *type_tb[NUM_NL80211_KEY_DEFAULT_TYPES];
              FAR struct nlattr *pos;
              int rem;
              int type;

              memset(type_tb, 0, sizeof(type_tb));
              nla_for_each_nested(pos,
                                  key_tb[NL80211_KEY_DEFAULT_TYPES], rem)
                {
                  type = nla_type(pos);
                  if (type < NUM_NL80211_KEY_DEFAULT_TYPES)
                    {
                      type_tb[type] = pos;
                    }
                }

              if (type_tb[NL80211_KEY_DEFAULT_TYPE_UNICAST])
                {
                  unicast = true;
                }

              if (type_tb[NL80211_KEY_DEFAULT_TYPE_MULTICAST])
                {
                  multicast = true;
                }
            }
          else
            {
              unicast = true;
              multicast = true;
            }
        }

      if (key_tb[NL80211_KEY_DEFAULT_MGMT])
        {
          if (ops->set_default_mgmt_key)
            {
              ret = ops->set_default_mgmt_key(dev->d_wiphy, dev,
                                              -1, key_idx);
              return ret;
            }

          return -EOPNOTSUPP;
        }
    }

  ret = ops->set_default_key(dev->d_wiphy, dev, -1, key_idx,
                             unicast, multicast);
  return ret;
}

/****************************************************************************
 * Name: nl80211_cmd_flush_pmksa
 *
 * Description:
 *   Handle NL80211_CMD_FLUSH_PMKSA command. Flushes PMKSA cache.
 *
 * Input Parameters:
 *   conn  - Netlink connection
 *   nlmsg - Netlink message header
 *   gnlh  - Generic netlink message header
 *   tb    - Netlink attributes array
 *
 * Returned Value:
 *   0 on success; negative error code on failure
 *
 ****************************************************************************/

static int nl80211_cmd_flush_pmksa(FAR struct netlink_conn_s *conn,
                                   FAR const struct nlmsghdr *nlmsg,
                                   FAR struct genlmsghdr *gnlh,
                                   FAR struct nlattr **tb)
{
  /* FLUSH_PMKSA: no driver op available in cfg80211_ops,
   * just return success to avoid error in wpa_supplicant.
   */

  return 0;
}

/****************************************************************************
 * Name: nl80211_cmd_set_pmksa
 *
 * Description:
 *   Handle NL80211_CMD_SET_PMKSA command. Sets PMKSA cache entry.
 *
 * Input Parameters:
 *   conn  - Netlink connection
 *   nlmsg - Netlink message header
 *   gnlh  - Generic netlink message header
 *   tb    - Netlink attributes array
 *
 * Returned Value:
 *   0 on success; negative error code on failure
 *
 ****************************************************************************/

static int nl80211_cmd_set_pmksa(FAR struct netlink_conn_s *conn,
                                 FAR const struct nlmsghdr *nlmsg,
                                 FAR struct genlmsghdr *gnlh,
                                 FAR struct nlattr **tb)
{
  /* SET_PMKSA: wpa_supplicant sends this to cache PMKSA entries
   * (especially for SAE/WPA3). No driver op needed — just ACK it.
   */

  return 0;
}

/****************************************************************************
 * Name: nl80211_cmd_set_rekey_offload
 *
 * Description:
 *   Handle NL80211_CMD_SET_REKEY_OFFLOAD command. Configures GTK rekey
 *   offload parameters.
 *
 * Input Parameters:
 *   conn  - Netlink connection
 *   nlmsg - Netlink message header
 *   gnlh  - Generic netlink message header
 *   tb    - Netlink attributes array
 *
 * Returned Value:
 *   0 on success; negative error code on failure
 *
 ****************************************************************************/

static int nl80211_cmd_set_rekey_offload(FAR struct netlink_conn_s *conn,
                                         FAR const struct nlmsghdr *nlmsg,
                                         FAR struct genlmsghdr *gnlh,
                                         FAR struct nlattr **tb)
{
  /* SET_REKEY_OFFLOAD: wpa_supplicant sends GTK rekey data for WoWLAN
   * offload. Not supported on this platform — just ACK it.
   */

  return 0;
}

/****************************************************************************
 * Name: nl80211_cmd_disconnect
 *
 * Description:
 *   Handle NL80211_CMD_DISCONNECT command. Disconnects from current AP.
 *
 * Input Parameters:
 *   conn  - Netlink connection
 *   nlmsg - Netlink message header
 *   gnlh  - Generic netlink message header
 *   tb    - Netlink attributes array
 *
 * Returned Value:
 *   0 on success; negative error code on failure
 *
 ****************************************************************************/

static int nl80211_cmd_disconnect(FAR struct netlink_conn_s *conn,
                                  FAR const struct nlmsghdr *nlmsg,
                                  FAR struct genlmsghdr *gnlh,
                                  FAR struct nlattr **tb)
{
  FAR struct net_driver_s *dev;
  FAR struct cfg80211_ops *ops;
  uint16_t reason_code = WLAN_REASON_DEAUTH_LEAVING;
  int ifindex;

  if (!tb[NL80211_ATTR_IFINDEX])
    {
      return -EINVAL;
    }

  ifindex = nla_get_u32(tb[NL80211_ATTR_IFINDEX]);
  dev = netdev_findbyindex(ifindex);
  if (!dev)
    {
      return -ENODEV;
    }

  ops = dev->d_cfg80211ops;
  if (!ops || !ops->disconnect)
    {
      return -EOPNOTSUPP;
    }

  if (tb[NL80211_ATTR_REASON_CODE])
    {
      reason_code = nla_get_u16(tb[NL80211_ATTR_REASON_CODE]);
    }

  return ops->disconnect(dev->d_wiphy, dev, reason_code);
}

/****************************************************************************
 * Name: nl80211_cmd_set_station
 *
 * Description:
 *   Handle NL80211_CMD_SET_STATION command. Modifies station parameters.
 *
 * Input Parameters:
 *   conn  - Netlink connection
 *   nlmsg - Netlink message header
 *   gnlh  - Generic netlink message header
 *   tb    - Netlink attributes array
 *
 * Returned Value:
 *   0 on success; negative error code on failure
 *
 ****************************************************************************/

static int nl80211_cmd_set_station(FAR struct netlink_conn_s *conn,
                                   FAR const struct nlmsghdr *nlmsg,
                                   FAR struct genlmsghdr *gnlh,
                                   FAR struct nlattr **tb)
{
  FAR struct net_driver_s *dev;
  FAR struct cfg80211_ops *ops;
  struct station_parameters params;
  FAR const uint8_t *mac;
  int ifindex;

  if (!tb[NL80211_ATTR_IFINDEX] || !tb[NL80211_ATTR_MAC])
    {
      return -EINVAL;
    }

  ifindex = nla_get_u32(tb[NL80211_ATTR_IFINDEX]);
  dev = netdev_findbyindex(ifindex);
  if (!dev)
    {
      return -ENODEV;
    }

  ops = dev->d_cfg80211ops;
  if (!ops || !ops->change_station)
    {
      return -EOPNOTSUPP;
    }

  mac = nla_data(tb[NL80211_ATTR_MAC]);
  memset(&params, 0, sizeof(params));

  if (tb[NL80211_ATTR_STA_FLAGS2])
    {
      FAR struct nl80211_sta_flag_update *flags;
      flags = nla_data(tb[NL80211_ATTR_STA_FLAGS2]);
      params.sta_flags_mask = flags->mask;
      params.sta_flags_set = flags->set;
    }

  if (tb[NL80211_ATTR_STA_AID])
    {
      params.aid = nla_get_u16(tb[NL80211_ATTR_STA_AID]);
    }

  if (tb[NL80211_ATTR_STA_LISTEN_INTERVAL])
    {
      params.listen_interval =
        nla_get_u16(tb[NL80211_ATTR_STA_LISTEN_INTERVAL]);
    }

  return ops->change_station(dev->d_wiphy, dev, mac, &params);
}

/****************************************************************************
 * Name: nl80211_cmd_connect
 *
 * Description:
 *   Handle NL80211_CMD_CONNECT command. Initiates connection to an AP.
 *
 * Input Parameters:
 *   conn  - Netlink connection
 *   nlmsg - Netlink message header
 *   gnlh  - Generic netlink message header
 *   tb    - Netlink attributes array
 *
 * Returned Value:
 *   0 on success; negative error code on failure
 *
 ****************************************************************************/

static int nl80211_cmd_connect(FAR struct netlink_conn_s *conn,
                               FAR const struct nlmsghdr *nlmsg,
                               FAR struct genlmsghdr *gnlh,
                               FAR struct nlattr **tb)
{
  FAR struct net_driver_s *dev;
  FAR struct cfg80211_ops *ops;
  struct cfg80211_connect_params *connect_params;
  int ifindex;
  int ret;

  if (!tb[NL80211_ATTR_IFINDEX])
    {
      return -EINVAL;
    }

  ifindex = nla_get_u32(tb[NL80211_ATTR_IFINDEX]);
  dev = netdev_findbyindex(ifindex);
  if (!dev)
    {
      return -ENODEV;
    }

  ops = dev->d_cfg80211ops;
  if (!ops || !ops->connect)
    {
      return -EOPNOTSUPP;
    }

  /* Allocate connect params */

  connect_params =
    (FAR struct cfg80211_connect_params *)
    kmm_zalloc(sizeof(struct cfg80211_connect_params));
  if (connect_params == NULL)
    {
      return -ENOMEM;
    }

  /* Extract parameters */

  if (tb[NL80211_ATTR_WIPHY_FREQ])
    {
      uint32_t freq = nla_get_u32(tb[NL80211_ATTR_WIPHY_FREQ]);
      connect_params->channel = nl80211_get_channel(freq);
      if (connect_params->channel == NULL)
        {
          kmm_free(connect_params);
          return -ENOMEM;
        }
    }

  if (tb[NL80211_ATTR_SSID])
    {
      connect_params->ssid = nla_data(tb[NL80211_ATTR_SSID]);
      connect_params->ssid_len = nla_len(tb[NL80211_ATTR_SSID]);
    }

  if (tb[NL80211_ATTR_MAC])
    {
      connect_params->bssid = nla_data(tb[NL80211_ATTR_MAC]);
    }

  if (tb[NL80211_ATTR_AUTH_TYPE])
    {
      connect_params->auth_type = nla_get_u32(tb[NL80211_ATTR_AUTH_TYPE]);
    }

  if (tb[NL80211_ATTR_IE])
    {
      connect_params->ie = nla_data(tb[NL80211_ATTR_IE]);
      connect_params->ie_len = nla_len(tb[NL80211_ATTR_IE]);
    }

  if (tb[NL80211_ATTR_KEY_DATA])
    {
      connect_params->key = nla_data(tb[NL80211_ATTR_KEY_DATA]);
      connect_params->key_len = nla_len(tb[NL80211_ATTR_KEY_DATA]);
    }

  if (tb[NL80211_ATTR_KEY_IDX])
    {
      connect_params->key_idx = nla_get_u32(tb[NL80211_ATTR_KEY_IDX]);
    }

  /* Parse WPA/crypto parameters */

  if (tb[NL80211_ATTR_WPA_VERSIONS])
    {
      connect_params->crypto.wpa_versions =
        nla_get_u32(tb[NL80211_ATTR_WPA_VERSIONS]);
    }

  if (tb[NL80211_ATTR_CIPHER_SUITES_PAIRWISE])
    {
      FAR uint32_t *suites =
        nla_data(tb[NL80211_ATTR_CIPHER_SUITES_PAIRWISE]);
      int len = nla_len(tb[NL80211_ATTR_CIPHER_SUITES_PAIRWISE]);
      int count = len / sizeof(uint32_t);

      if (count > NL80211_MAX_NR_CIPHER_SUITES)
        {
          count = NL80211_MAX_NR_CIPHER_SUITES;
        }

      for (int i = 0; i < count; i++)
        {
          connect_params->crypto.ciphers_pairwise[i] = suites[i];
        }

      connect_params->crypto.n_ciphers_pairwise = count;
    }

  if (tb[NL80211_ATTR_CIPHER_SUITE_GROUP])
    {
      connect_params->crypto.cipher_group =
          nla_get_u32(tb[NL80211_ATTR_CIPHER_SUITE_GROUP]);
    }

  if (tb[NL80211_ATTR_AKM_SUITES])
    {
      FAR uint32_t *suites = nla_data(tb[NL80211_ATTR_AKM_SUITES]);
      int len = nla_len(tb[NL80211_ATTR_AKM_SUITES]);
      int count = len / sizeof(uint32_t);

      if (count > CFG80211_MAX_NUM_AKM_SUITES)
        {
          count = CFG80211_MAX_NUM_AKM_SUITES;
        }

      for (int i = 0; i < count; i++)
        {
          connect_params->crypto.akm_suites[i] = suites[i];
        }

      connect_params->crypto.n_akm_suites = count;
    }

  if (tb[NL80211_ATTR_PRIVACY])
    {
      connect_params->privacy = true;
    }

  if (tb[NL80211_ATTR_CONTROL_PORT])
    {
      connect_params->crypto.control_port = true;
    }

  if (tb[NL80211_ATTR_CONTROL_PORT_ETHERTYPE])
    {
      connect_params->crypto.control_port_ethertype =
          nla_get_u16(tb[NL80211_ATTR_CONTROL_PORT_ETHERTYPE]);
    }

  if (tb[NL80211_ATTR_CONTROL_PORT_NO_ENCRYPT])
    {
      connect_params->crypto.control_port_no_encrypt = true;
    }

  if (tb[NL80211_ATTR_USE_MFP])
    {
      connect_params->mfp = nla_get_u32(tb[NL80211_ATTR_USE_MFP]);
    }

  if (tb[NL80211_ATTR_EXTERNAL_AUTH_SUPPORT])
    {
      connect_params->flags |= CONNECT_REQ_EXTERNAL_AUTH_SUPPORT;
    }

  if (tb[NL80211_ATTR_PREV_BSSID])
    {
      connect_params->prev_bssid = nla_data(tb[NL80211_ATTR_PREV_BSSID]);
    }

  /* Call driver */

  ret = ops->connect(dev->d_wiphy, dev, connect_params);

  /* Free allocated memory */

  if (connect_params->channel)
    {
      kmm_free(connect_params->channel);
    }

  kmm_free(connect_params);

  if (ret < 0)
    {
      return ret;
    }

  return 0;
}

/****************************************************************************
 * Name: nl80211_send_ack
 *
 * Description:
 *   Send an ACK response message
 *
 * Input Parameters:
 *   conn - Netlink connection
 *   req  - Request message to acknowledge
 *
 * Returned Value:
 *   0 on success; negative error code on failure
 *
 ****************************************************************************/

static int nl80211_send_ack(FAR struct netlink_conn_s *conn,
                            FAR const struct nlmsghdr *req)
{
  FAR struct netlink_response_s *response;
  FAR struct nlmsgerr *err;
  int payload_len = sizeof(struct nlmsgerr);

  response = (FAR struct netlink_response_s *)
             kmm_zalloc(sizeof(struct netlink_response_s) +
                        NLMSG_ALIGN(payload_len));
  if (response == NULL)
    {
      return -ENOMEM;
    }

  response->msg.nlmsg_len = NLMSG_LENGTH(payload_len);
  response->msg.nlmsg_type = NLMSG_ERROR;
  response->msg.nlmsg_flags = 0;
  response->msg.nlmsg_seq = req->nlmsg_seq;
  response->msg.nlmsg_pid = req->nlmsg_pid;

  err = (FAR struct nlmsgerr *)NLMSG_DATA(&response->msg);
  err->error = 0;
  memcpy(&err->msg, req, sizeof(struct nlmsghdr));

  netlink_add_response((NETLINK_HANDLE)conn, response);
  return 0;
}

/****************************************************************************
 * Name: nl80211_cmd_frame
 *
 * Description:
 *   Handle NL80211_CMD_FRAME command. Transmits a management frame.
 *
 * Input Parameters:
 *   conn  - Netlink connection
 *   nlmsg - Netlink message header
 *   gnlh  - Generic netlink message header
 *   tb    - Netlink attributes array
 *
 * Returned Value:
 *   0 on success; negative error code on failure
 *
 ****************************************************************************/

static int nl80211_cmd_frame(FAR struct netlink_conn_s *conn,
                            FAR const struct nlmsghdr *nlmsg,
                            FAR struct genlmsghdr *gnlh,
                            FAR struct nlattr **tb)
{
  FAR struct net_driver_s *dev;
  FAR struct cfg80211_ops *ops;
  struct cfg80211_mgmt_tx_params params;
  uint64_t cookie = 0;
  int ifindex;
  int ret;

  if (!tb[NL80211_ATTR_IFINDEX] || !tb[NL80211_ATTR_FRAME])
    {
      return -EINVAL;
    }

  ifindex = nla_get_u32(tb[NL80211_ATTR_IFINDEX]);
  dev = netdev_findbyindex(ifindex);
  if (!dev)
    {
      return -ENODEV;
    }

  ops = dev->d_cfg80211ops;
  if (!ops || !ops->mgmt_tx)
    {
      return -EOPNOTSUPP;
    }

  memset(&params, 0, sizeof(params));

  if (tb[NL80211_ATTR_WIPHY_FREQ])
    {
      uint32_t freq = nla_get_u32(tb[NL80211_ATTR_WIPHY_FREQ]);
      params.chan = nl80211_get_channel(freq);
      if (params.chan == NULL)
        {
          return -ENOMEM;
        }
    }

  if (tb[NL80211_ATTR_DURATION])
    {
      params.wait = nla_get_u32(tb[NL80211_ATTR_DURATION]);
    }

  if (tb[NL80211_ATTR_OFFCHANNEL_TX_OK])
    {
      params.offchan = true;
    }

  if (tb[NL80211_ATTR_TX_NO_CCK_RATE])
    {
      params.no_cck = true;
    }

  if (tb[NL80211_ATTR_DONT_WAIT_FOR_ACK])
    {
      params.dont_wait_for_ack = true;
    }

  params.buf = nla_data(tb[NL80211_ATTR_FRAME]);
  params.len = nla_len(tb[NL80211_ATTR_FRAME]);

  ret = ops->mgmt_tx(dev->d_wiphy, dev->d_wdev, &params, &cookie);

  if (params.chan)
    {
      kmm_free(params.chan);
    }

  if (ret < 0)
    {
      return ret;
    }

  /* Send response with cookie */

  FAR struct netlink_response_s *msg;
  struct nl80211_msg m;
  size_t buflen = GENL_HDRLEN + RTA_SPACE(sizeof(uint64_t));

  msg = nl80211_alloc_reply_msg(nlmsg, NL80211_CMD_FRAME, (int)buflen);
  if (!msg)
    {
      return -ENOMEM;
    }

  /* CMD_FRAME is a single response, not multipart.
   * NLM_F_MULTI must NOT be set, otherwise nl_recvmsgs() will keep
   * looping and block on the next recvfrom() on the blocking command
   * socket (global->nl).
   */

  msg->msg.nlmsg_flags = 0;

  nl80211_msg_init(&m, msg, (int)buflen);
  if (nl80211_nla_put_u64(&m, NL80211_ATTR_COOKIE, cookie) < 0)
    {
      kmm_free(msg);
      return -EMSGSIZE;
    }

  nl80211_msg_finalize(&m);
  netlink_add_response((NETLINK_HANDLE)conn, msg);

  return 0;
}

/****************************************************************************
 * Name: nl80211_cmd_remain_on_channel
 *
 * Description:
 *   Handle NL80211_CMD_REMAIN_ON_CHANNEL command. Stays on a channel for
 *   a specified duration (used for P2P operations).
 *
 * Input Parameters:
 *   conn  - Netlink connection
 *   nlmsg - Netlink message header
 *   gnlh  - Generic netlink message header
 *   tb    - Netlink attributes array
 *
 * Returned Value:
 *   0 on success; negative error code on failure
 *
 ****************************************************************************/

static int nl80211_cmd_remain_on_channel(FAR struct netlink_conn_s *conn,
                                         FAR const struct nlmsghdr *nlmsg,
                                         FAR struct genlmsghdr *gnlh,
                                         FAR struct nlattr **tb)
{
  FAR struct net_driver_s *dev;
  FAR struct cfg80211_ops *ops;
  FAR struct ieee80211_channel *chan = NULL;
  uint64_t cookie = 0;
  unsigned int duration;
  int ifindex;
  int ret;

  if (!tb[NL80211_ATTR_IFINDEX] || !tb[NL80211_ATTR_WIPHY_FREQ] ||
      !tb[NL80211_ATTR_DURATION])
    {
      return -EINVAL;
    }

  ifindex = nla_get_u32(tb[NL80211_ATTR_IFINDEX]);
  dev = netdev_findbyindex(ifindex);
  if (!dev)
    {
      return -ENODEV;
    }

  ops = dev->d_cfg80211ops;
  if (!ops || !ops->remain_on_channel)
    {
      return -EOPNOTSUPP;
    }

  chan = nl80211_get_channel(nla_get_u32(tb[NL80211_ATTR_WIPHY_FREQ]));
  if (!chan)
    {
      return -ENOMEM;
    }

  duration = nla_get_u32(tb[NL80211_ATTR_DURATION]);

  ret = ops->remain_on_channel(dev->d_wiphy, dev->d_wdev,
                                chan, duration, &cookie);
  kmm_free(chan);

  if (ret < 0)
    {
      return ret;
    }

  /* Send response with cookie */

  FAR struct netlink_response_s *msg;
  struct nl80211_msg m;
  size_t buflen = GENL_HDRLEN + RTA_SPACE(sizeof(uint64_t));

  msg = nl80211_alloc_reply_msg(nlmsg, NL80211_CMD_REMAIN_ON_CHANNEL,
                                (int)buflen);
  if (!msg)
    {
      return -ENOMEM;
    }

  msg->msg.nlmsg_flags = 0;

  nl80211_msg_init(&m, msg, (int)buflen);
  if (nl80211_nla_put_u64(&m, NL80211_ATTR_COOKIE, cookie) < 0)
    {
      kmm_free(msg);
      return -EMSGSIZE;
    }

  nl80211_msg_finalize(&m);
  netlink_add_response((NETLINK_HANDLE)conn, msg);

  return 0;
}

/****************************************************************************
 * Name: nl80211_cmd_cancel_remain_on_channel
 *
 * Description:
 *   Handle NL80211_CMD_CANCEL_REMAIN_ON_CHANNEL command. Cancels a
 *   remain-on-channel operation.
 *
 * Input Parameters:
 *   conn  - Netlink connection
 *   nlmsg - Netlink message header
 *   gnlh  - Generic netlink message header
 *   tb    - Netlink attributes array
 *
 * Returned Value:
 *   0 on success; negative error code on failure
 *
 ****************************************************************************/

static int nl80211_cmd_cancel_remain_on_channel(
                                     FAR struct netlink_conn_s *conn,
                                     FAR const struct nlmsghdr *nlmsg,
                                     FAR struct genlmsghdr *gnlh,
                                     FAR struct nlattr **tb)
{
  FAR struct net_driver_s *dev;
  FAR struct cfg80211_ops *ops;
  uint64_t cookie = 0;
  int ifindex;

  if (!tb[NL80211_ATTR_IFINDEX])
    {
      return -EINVAL;
    }

  ifindex = nla_get_u32(tb[NL80211_ATTR_IFINDEX]);
  dev = netdev_findbyindex(ifindex);
  if (!dev)
    {
      return -ENODEV;
    }

  ops = dev->d_cfg80211ops;
  if (!ops || !ops->cancel_remain_on_channel)
    {
      return -EOPNOTSUPP;
    }

  if (tb[NL80211_ATTR_COOKIE])
    {
      cookie = nla_get_u64(tb[NL80211_ATTR_COOKIE]);
    }

  return ops->cancel_remain_on_channel(dev->d_wiphy, dev->d_wdev, cookie);
}

/****************************************************************************
 * Name: nl80211_cmd_external_auth
 *
 * Description:
 *   Handle NL80211_CMD_EXTERNAL_AUTH command. Processes external
 *   authentication status (e.g., SAE authentication).
 *
 * Input Parameters:
 *   conn  - Netlink connection
 *   nlmsg - Netlink message header
 *   gnlh  - Generic netlink message header
 *   tb    - Netlink attributes array
 *
 * Returned Value:
 *   0 on success; negative error code on failure
 *
 ****************************************************************************/

static int nl80211_cmd_external_auth(FAR struct netlink_conn_s *conn,
                                     FAR const struct nlmsghdr *nlmsg,
                                     FAR struct genlmsghdr *gnlh,
                                     FAR struct nlattr **tb)
{
  FAR struct net_driver_s *dev;
  FAR struct cfg80211_ops *ops;
  struct cfg80211_external_auth_params params;
  int ifindex;

  if (!tb[NL80211_ATTR_IFINDEX])
    {
      return -EINVAL;
    }

  ifindex = nla_get_u32(tb[NL80211_ATTR_IFINDEX]);
  dev = netdev_findbyindex(ifindex);
  if (!dev)
    {
      return -ENODEV;
    }

  ops = dev->d_cfg80211ops;
  if (!ops || !ops->external_auth)
    {
      return -EOPNOTSUPP;
    }

  memset(&params, 0, sizeof(params));

  if (tb[NL80211_ATTR_STATUS_CODE])
    {
      params.status = nla_get_u16(tb[NL80211_ATTR_STATUS_CODE]);
    }

  if (tb[NL80211_ATTR_SSID])
    {
      size_t ssid_len = nla_len(tb[NL80211_ATTR_SSID]);
      if (ssid_len > IEEE80211_MAX_SSID_LEN)
        {
          ssid_len = IEEE80211_MAX_SSID_LEN;
        }

      memcpy(params.ssid.ssid, nla_data(tb[NL80211_ATTR_SSID]), ssid_len);
      params.ssid.ssid_len = ssid_len;
    }

  if (tb[NL80211_ATTR_BSSID])
    {
      memcpy(params.bssid, nla_data(tb[NL80211_ATTR_BSSID]), ETH_ALEN);
    }
  else if (tb[NL80211_ATTR_MAC])
    {
      memcpy(params.bssid, nla_data(tb[NL80211_ATTR_MAC]), ETH_ALEN);
    }

  if (tb[NL80211_ATTR_AKM_SUITES])
    {
      params.key_mgmt_suite = nla_get_u32(tb[NL80211_ATTR_AKM_SUITES]);
    }

  if (tb[NL80211_ATTR_PMKID])
    {
      params.pmkid = nla_data(tb[NL80211_ATTR_PMKID]);
    }

  ninfo("nl80211_cmd_external_auth: status=%d, akm=0x%x\n",
       params.status, params.key_mgmt_suite);

  return ops->external_auth(dev->d_wiphy, dev, &params);
}

/****************************************************************************
 * Name: nl80211_cmd_get_reg
 *
 * Description:
 *   Handle NL80211_CMD_GET_REG command. Returns the current regulatory
 *   domain information. Currently returns world regulatory domain "00"
 *   which allows operation on all channels with default restrictions.
 *
 * Input Parameters:
 *   conn  - Netlink connection
 *   nlmsg - Netlink message header
 *   gnlh  - Generic netlink message header
 *   tb    - Netlink attributes array
 *
 * Returned Value:
 *   0 on success; negative error code on failure
 *
 ****************************************************************************/

static int nl80211_cmd_get_reg(FAR struct netlink_conn_s *conn,
                               FAR const struct nlmsghdr *nlmsg,
                               FAR struct genlmsghdr *gnlh,
                               FAR struct nlattr **tb)
{
  FAR struct netlink_response_s *msg;
  FAR struct genlmsghdr *resp_gnlh;
  struct nl80211_msg m;
  size_t buflen = GENL_HDRLEN + RTA_SPACE(3);

  ninfo("nl80211_cmd_get_reg: called\n");

  msg = (FAR struct netlink_response_s *)
    kmm_zalloc(sizeof(struct netlink_response_s) + buflen);
  if (!msg)
    {
      return -ENOMEM;
    }

  msg->msg.nlmsg_type = nlmsg->nlmsg_type;
  msg->msg.nlmsg_flags = 0;
  msg->msg.nlmsg_seq = nlmsg->nlmsg_seq;
  msg->msg.nlmsg_pid = nlmsg->nlmsg_pid;

  resp_gnlh = (FAR struct genlmsghdr *)NLMSG_DATA(&msg->msg);
  resp_gnlh->cmd = NL80211_CMD_GET_REG;
  resp_gnlh->version = 1;

  nl80211_msg_init(&m, msg, buflen);

  /* Return world regulatory domain "00" */

  if (nl80211_nla_put(&m, NL80211_ATTR_REG_ALPHA2, 3, "00") < 0)
    {
      kmm_free(msg);
      return -EMSGSIZE;
    }

  nl80211_msg_finalize(&m);
  netlink_add_response((NETLINK_HANDLE)conn, msg);

  return 0;
}

/****************************************************************************
 * AP Mode Commands
 ****************************************************************************/

/****************************************************************************
 * Name: nl80211_cmd_set_wiphy
 *
 * Description:
 *   Handle NL80211_CMD_SET_WIPHY command. Sets wireless PHY parameters.
 *
 * Input Parameters:
 *   conn  - Netlink connection
 *   nlmsg - Netlink message header
 *   gnlh  - Generic netlink message header
 *   tb    - Netlink attributes array
 *
 * Returned Value:
 *   0 on success; negative error code on failure
 *
 ****************************************************************************/

static int nl80211_cmd_set_wiphy(FAR struct netlink_conn_s *conn,
                                 FAR const struct nlmsghdr *nlmsg,
                                 FAR struct genlmsghdr *gnlh,
                                 FAR struct nlattr **tb)
{
  ninfo("nl80211_cmd_set_wiphy: called\n");

  /* SET_WIPHY is used for setting frequency/channel for AP mode.
   * The BES driver handles channel setting internally via start_ap,
   * so we just ACK this command.
   */

  return 0;
}

/****************************************************************************
 * Name: nl80211_cmd_start_ap
 *
 * Description:
 *   Handle NL80211_CMD_START_AP command. Starts an access point.
 *
 * Input Parameters:
 *   conn  - Netlink connection
 *   nlmsg - Netlink message header
 *   gnlh  - Generic netlink message header
 *   tb    - Netlink attributes array
 *
 * Returned Value:
 *   0 on success; negative error code on failure
 *
 ****************************************************************************/

static int nl80211_cmd_start_ap(FAR struct netlink_conn_s *conn,
                                FAR const struct nlmsghdr *nlmsg,
                                FAR struct genlmsghdr *gnlh,
                                FAR struct nlattr **tb)
{
  FAR struct net_driver_s *dev;
  FAR struct cfg80211_ops *ops;
  FAR struct cfg80211_ap_settings *settings;
  int ifindex;
  int ret;

  ninfo("nl80211_cmd_start_ap: called\n");

  if (!tb[NL80211_ATTR_IFINDEX])
    {
      return -EINVAL;
    }

  ifindex = nla_get_u32(tb[NL80211_ATTR_IFINDEX]);
  dev = netdev_findbyindex(ifindex);
  if (!dev)
    {
      return -ENODEV;
    }

  ops = dev->d_cfg80211ops;
  if (!ops || !ops->start_ap)
    {
      return -EOPNOTSUPP;
    }

  settings = (FAR struct cfg80211_ap_settings *)
    kmm_zalloc(sizeof(struct cfg80211_ap_settings));
  if (!settings)
    {
      return -ENOMEM;
    }

  /* Parse beacon data */

  if (tb[NL80211_ATTR_BEACON_HEAD])
    {
      settings->beacon.head = nla_data(tb[NL80211_ATTR_BEACON_HEAD]);
      settings->beacon.head_len = nla_len(tb[NL80211_ATTR_BEACON_HEAD]);
    }

  if (tb[NL80211_ATTR_BEACON_TAIL])
    {
      settings->beacon.tail = nla_data(tb[NL80211_ATTR_BEACON_TAIL]);
      settings->beacon.tail_len = nla_len(tb[NL80211_ATTR_BEACON_TAIL]);
    }

  if (tb[NL80211_ATTR_BEACON_INTERVAL])
    {
      settings->beacon_interval =
        nla_get_u32(tb[NL80211_ATTR_BEACON_INTERVAL]);
    }

  if (tb[NL80211_ATTR_DTIM_PERIOD])
    {
      settings->dtim_period = nla_get_u32(tb[NL80211_ATTR_DTIM_PERIOD]);
    }

  if (tb[NL80211_ATTR_SSID])
    {
      settings->ssid = nla_data(tb[NL80211_ATTR_SSID]);
      settings->ssid_len = nla_len(tb[NL80211_ATTR_SSID]);
    }

  if (tb[NL80211_ATTR_HIDDEN_SSID])
    {
      settings->hidden_ssid = nla_get_u32(tb[NL80211_ATTR_HIDDEN_SSID]);
    }

  /* Parse crypto settings */

  if (tb[NL80211_ATTR_CIPHER_SUITE_GROUP])
    {
      settings->crypto.cipher_group =
        nla_get_u32(tb[NL80211_ATTR_CIPHER_SUITE_GROUP]);
    }

  if (tb[NL80211_ATTR_CIPHER_SUITES_PAIRWISE])
    {
      FAR uint32_t *suites =
        nla_data(tb[NL80211_ATTR_CIPHER_SUITES_PAIRWISE]);
      int len = nla_len(tb[NL80211_ATTR_CIPHER_SUITES_PAIRWISE]);
      int count = len / sizeof(uint32_t);
      if (count > NL80211_MAX_NR_CIPHER_SUITES)
        count = NL80211_MAX_NR_CIPHER_SUITES;
      for (int i = 0; i < count; i++)
        settings->crypto.ciphers_pairwise[i] = suites[i];
      settings->crypto.n_ciphers_pairwise = count;
    }

  if (tb[NL80211_ATTR_AKM_SUITES])
    {
      FAR uint32_t *suites = nla_data(tb[NL80211_ATTR_AKM_SUITES]);
      int len = nla_len(tb[NL80211_ATTR_AKM_SUITES]);
      int count = len / sizeof(uint32_t);
      if (count > CFG80211_MAX_NUM_AKM_SUITES)
        count = CFG80211_MAX_NUM_AKM_SUITES;
      for (int i = 0; i < count; i++)
        settings->crypto.akm_suites[i] = suites[i];
      settings->crypto.n_akm_suites = count;
    }

  if (tb[NL80211_ATTR_WPA_VERSIONS])
    {
      settings->crypto.wpa_versions =
        nla_get_u32(tb[NL80211_ATTR_WPA_VERSIONS]);
    }

  if (tb[NL80211_ATTR_CONTROL_PORT_NO_ENCRYPT])
    {
      settings->crypto.control_port_no_encrypt = true;
    }

  if (tb[NL80211_ATTR_PRIVACY])
    {
      settings->privacy = true;
    }

  if (tb[NL80211_ATTR_AUTH_TYPE])
    {
      settings->auth_type = nla_get_u32(tb[NL80211_ATTR_AUTH_TYPE]);
    }

  if (tb[NL80211_ATTR_INACTIVITY_TIMEOUT])
    {
      settings->inactivity_timeout =
        nla_get_u16(tb[NL80211_ATTR_INACTIVITY_TIMEOUT]);
    }

  if (tb[NL80211_ATTR_SMPS_MODE])
    {
      settings->smps_mode = (uint8_t)nla_get_u16(tb[NL80211_ATTR_SMPS_MODE]);
    }

  /* Parse channel definition */

  if (tb[NL80211_ATTR_WIPHY_FREQ])
    {
      uint32_t freq = nla_get_u32(tb[NL80211_ATTR_WIPHY_FREQ]);
      settings->chandef.chan = nl80211_get_channel(freq);
      if (settings->chandef.chan == NULL)
        {
          kmm_free(settings);
          return -ENOMEM;
        }

      settings->chandef.center_freq1 = freq;
    }

  if (tb[NL80211_ATTR_CHANNEL_WIDTH])
    {
      settings->chandef.width = nla_get_u32(tb[NL80211_ATTR_CHANNEL_WIDTH]);
    }

  if (tb[NL80211_ATTR_CENTER_FREQ1])
    {
      settings->chandef.center_freq1 =
        nla_get_u32(tb[NL80211_ATTR_CENTER_FREQ1]);
    }

  if (tb[NL80211_ATTR_CENTER_FREQ2])
    {
      settings->chandef.center_freq2 =
        nla_get_u32(tb[NL80211_ATTR_CENTER_FREQ2]);
    }

  if (tb[NL80211_ATTR_PROBE_RESP])
    {
      settings->beacon.probe_resp =
        nla_data(tb[NL80211_ATTR_PROBE_RESP]);
      settings->beacon.probe_resp_len =
        nla_len(tb[NL80211_ATTR_PROBE_RESP]);
    }

  ninfo("nl80211_cmd_start_ap: freq=%lu bint=%d dtim=%d ssid_len=%d\n",
       (unsigned long)settings->chandef.center_freq1,
       settings->beacon_interval,
       settings->dtim_period,
       (int)settings->ssid_len);

  ret = ops->start_ap(dev->d_wiphy, dev, settings);

  if (settings->chandef.chan)
    {
      kmm_free(settings->chandef.chan);
    }

  kmm_free(settings);
  return ret;
}

/****************************************************************************
 * Name: nl80211_cmd_set_beacon
 *
 * Description:
 *   Handle NL80211_CMD_SET_BEACON command. Updates AP beacon parameters.
 *
 * Input Parameters:
 *   conn  - Netlink connection
 *   nlmsg - Netlink message header
 *   gnlh  - Generic netlink message header
 *   tb    - Netlink attributes array
 *
 * Returned Value:
 *   0 on success; negative error code on failure
 *
 ****************************************************************************/

static int nl80211_cmd_set_beacon(FAR struct netlink_conn_s *conn,
                                  FAR const struct nlmsghdr *nlmsg,
                                  FAR struct genlmsghdr *gnlh,
                                  FAR struct nlattr **tb)
{
  FAR struct net_driver_s *dev;
  FAR struct cfg80211_ops *ops;
  struct cfg80211_beacon_data info;
  int ifindex;

  ninfo("nl80211_cmd_set_beacon: called\n");

  if (!tb[NL80211_ATTR_IFINDEX])
    {
      return -EINVAL;
    }

  ifindex = nla_get_u32(tb[NL80211_ATTR_IFINDEX]);
  dev = netdev_findbyindex(ifindex);
  if (!dev)
    {
      return -ENODEV;
    }

  ops = dev->d_cfg80211ops;
  if (!ops || !ops->change_beacon)
    {
      return -EOPNOTSUPP;
    }

  memset(&info, 0, sizeof(info));

  if (tb[NL80211_ATTR_BEACON_HEAD])
    {
      info.head = nla_data(tb[NL80211_ATTR_BEACON_HEAD]);
      info.head_len = nla_len(tb[NL80211_ATTR_BEACON_HEAD]);
    }

  if (tb[NL80211_ATTR_BEACON_TAIL])
    {
      info.tail = nla_data(tb[NL80211_ATTR_BEACON_TAIL]);
      info.tail_len = nla_len(tb[NL80211_ATTR_BEACON_TAIL]);
    }

  if (tb[NL80211_ATTR_PROBE_RESP])
    {
      info.probe_resp = nla_data(tb[NL80211_ATTR_PROBE_RESP]);
      info.probe_resp_len = nla_len(tb[NL80211_ATTR_PROBE_RESP]);
    }

  return ops->change_beacon(dev->d_wiphy, dev, &info);
}

/****************************************************************************
 * Name: nl80211_cmd_stop_ap
 *
 * Description:
 *   Handle NL80211_CMD_STOP_AP command. Stops an access point.
 *
 * Input Parameters:
 *   conn  - Netlink connection
 *   nlmsg - Netlink message header
 *   gnlh  - Generic netlink message header
 *   tb    - Netlink attributes array
 *
 * Returned Value:
 *   0 on success; negative error code on failure
 *
 ****************************************************************************/

static int nl80211_cmd_stop_ap(FAR struct netlink_conn_s *conn,
                               FAR const struct nlmsghdr *nlmsg,
                               FAR struct genlmsghdr *gnlh,
                               FAR struct nlattr **tb)
{
  FAR struct net_driver_s *dev;
  FAR struct cfg80211_ops *ops;
  int ifindex;

  ninfo("nl80211_cmd_stop_ap: called\n");

  if (!tb[NL80211_ATTR_IFINDEX])
    {
      return -EINVAL;
    }

  ifindex = nla_get_u32(tb[NL80211_ATTR_IFINDEX]);
  dev = netdev_findbyindex(ifindex);
  if (!dev)
    {
      return -ENODEV;
    }

  ops = dev->d_cfg80211ops;
  if (!ops || !ops->stop_ap)
    {
      return -EOPNOTSUPP;
    }

  return ops->stop_ap(dev->d_wiphy, dev, 0);
}

/****************************************************************************
 * Name: nl80211_cmd_authenticate
 *
 * Description:
 *   Handle NL80211_CMD_AUTHENTICATE. Builds an 802.11 AUTH frame from the
 *   request attributes and transmits it via the driver's mgmt_tx callback
 *   so it gets relayed to the target AP in hwsim.
 *
 ****************************************************************************/

static int nl80211_cmd_authenticate(FAR struct netlink_conn_s *conn,
                                    FAR const struct nlmsghdr *nlmsg,
                                    FAR struct genlmsghdr *gnlh,
                                    FAR struct nlattr **tb)
{
  FAR struct net_driver_s *dev;
  FAR struct cfg80211_ops *ops;
  int ifindex;
  uint32_t freq = 0;
  uint32_t auth_type = 0;
  FAR const uint8_t *bssid = NULL;
  FAR const uint8_t *ssid = NULL;
  size_t ssid_len = 0;
  FAR const uint8_t *auth_data = NULL;
  size_t auth_data_len = 0;
  uint8_t frame[512];
  size_t frame_len;
  struct cfg80211_mgmt_tx_params tx_params;
  uint64_t cookie;

  if (!tb[NL80211_ATTR_IFINDEX])
    {
      return -EINVAL;
    }

  ifindex = nla_get_u32(tb[NL80211_ATTR_IFINDEX]);
  dev = netdev_findbyindex(ifindex);
  if (!dev)
    {
      return -ENODEV;
    }

  ops = dev->d_cfg80211ops;
  if (!ops || !ops->mgmt_tx)
    {
      return -EOPNOTSUPP;
    }

  if (tb[NL80211_ATTR_WIPHY_FREQ])
    freq = nla_get_u32(tb[NL80211_ATTR_WIPHY_FREQ]);

  if (tb[NL80211_ATTR_MAC])
    bssid = nla_data(tb[NL80211_ATTR_MAC]);

  if (tb[NL80211_ATTR_AUTH_TYPE])
    auth_type = nla_get_u32(tb[NL80211_ATTR_AUTH_TYPE]);

  /* SSID is sent by wpa_supplicant for BSS identification.
   * In Linux kernel, nl80211_authenticate uses it for cfg80211_get_bss()
   * lookup. Here we parse and log it; once cfg80211 BSS management is
   * implemented, it should be used to look up the target BSS and pass
   * it via cfg80211_auth_request to ops->auth().
   */

  if (tb[NL80211_ATTR_SSID])
    {
      ssid = nla_data(tb[NL80211_ATTR_SSID]);
      ssid_len = nla_len(tb[NL80211_ATTR_SSID]);
    }

  if (tb[NL80211_ATTR_AUTH_DATA])
    {
      auth_data = nla_data(tb[NL80211_ATTR_AUTH_DATA]);
      auth_data_len = nla_len(tb[NL80211_ATTR_AUTH_DATA]);
    }
  else if (tb[NL80211_ATTR_SAE_DATA])
    {
      auth_data = nla_data(tb[NL80211_ATTR_SAE_DATA]);
      auth_data_len = nla_len(tb[NL80211_ATTR_SAE_DATA]);
    }

  nerr("nl80211_cmd_authenticate: freq=%lu auth_type=%lu "
       "ssid_len=%zu auth_data_len=%zu ssid=%.*s\n",
       (unsigned long)freq, (unsigned long)auth_type,
       ssid_len, auth_data_len,
       (int)ssid_len, ssid ? (FAR const char *)ssid : "");

  /* Build 802.11 Authentication frame:
   * FC (2) + Duration (2) + DA (6) + SA (6) + BSSID (6) + SeqCtrl (2)
   * + Auth Alg (2) + Auth Seq (2) + Status (2) + auth_data
   */

  memset(frame, 0, sizeof(frame));
  frame_len = 0;

  /* Frame Control: Auth (type=0 mgmt, subtype=0xb0>>4=11) */

  frame[0] = 0xb0;  /* Auth frame */
  frame[1] = 0x00;
  frame_len = 2;

  /* Duration */

  frame[2] = 0; frame[3] = 0;
  frame_len = 4;

  /* DA = BSSID (AP) */

  if (bssid)
    memcpy(&frame[4], bssid, 6);
  frame_len = 10;

  /* SA = our MAC */

  memcpy(&frame[10],
         dev->d_wiphy ? dev->d_wiphy->perm_addr
                      : dev->d_mac.ether.ether_addr_octet, 6);
  frame_len = 16;

  /* BSSID */

  if (bssid)
    memcpy(&frame[16], bssid, 6);
  frame_len = 22;

  /* Sequence Control */

  frame[22] = 0; frame[23] = 0;
  frame_len = 24;

  /* Auth Algorithm Number */

  if (auth_type == NL80211_AUTHTYPE_SAE)
    {
      frame[24] = 3; frame[25] = 0;  /* SAE = 3 */
    }
  else
    {
      frame[24] = (uint8_t)(auth_type & 0xff);
      frame[25] = 0;
    }

  frame_len = 26;

  /* Auth Transaction Sequence Number + Status Code from auth_data */

  if (auth_data && auth_data_len > 0)
    {
      size_t copy = auth_data_len;
      if (frame_len + copy > sizeof(frame))
        copy = sizeof(frame) - frame_len;
      memcpy(&frame[frame_len], auth_data, copy);
      frame_len += copy;
    }
  else
    {
      /* Default: seq=1, status=0 */

      frame[26] = 1; frame[27] = 0;  /* Seq = 1 */
      frame[28] = 0; frame[29] = 0;  /* Status = 0 */
      frame_len = 30;
    }

  /* Transmit via mgmt_tx to relay to the AP */

  memset(&tx_params, 0, sizeof(tx_params));
  tx_params.buf = frame;
  tx_params.len = frame_len;

  /* Set channel for TX */

  if (freq > 0)
    {
      FAR struct ieee80211_channel *chan = nl80211_get_channel(freq);
      tx_params.chan = chan;
      ops->mgmt_tx(dev->d_wiphy, dev->d_wdev, &tx_params, &cookie);
      if (chan)
        kmm_free(chan);
    }
  else
    {
      ops->mgmt_tx(dev->d_wiphy, dev->d_wdev, &tx_params, &cookie);
    }

  return 0;
}

/****************************************************************************
 * Name: nl80211_cmd_associate
 *
 * Description:
 *   Handle NL80211_CMD_ASSOCIATE. Builds an 802.11 Association Request
 *   frame and transmits it via mgmt_tx.
 *
 ****************************************************************************/

static int nl80211_cmd_associate(FAR struct netlink_conn_s *conn,
                                 FAR const struct nlmsghdr *nlmsg,
                                 FAR struct genlmsghdr *gnlh,
                                 FAR struct nlattr **tb)
{
  FAR struct net_driver_s *dev;
  FAR struct cfg80211_ops *ops;
  int ifindex;
  uint32_t freq = 0;
  FAR const uint8_t *bssid = NULL;
  FAR const uint8_t *ie = NULL;
  size_t ie_len = 0;
  FAR const uint8_t *ssid = NULL;
  size_t ssid_len = 0;
  uint8_t frame[512];
  size_t frame_len;
  struct cfg80211_mgmt_tx_params tx_params;
  uint64_t cookie;

  if (!tb[NL80211_ATTR_IFINDEX])
    {
      return -EINVAL;
    }

  ifindex = nla_get_u32(tb[NL80211_ATTR_IFINDEX]);
  dev = netdev_findbyindex(ifindex);
  if (!dev)
    {
      return -ENODEV;
    }

  ops = dev->d_cfg80211ops;
  if (!ops || !ops->mgmt_tx)
    {
      return -EOPNOTSUPP;
    }

  if (tb[NL80211_ATTR_WIPHY_FREQ])
    freq = nla_get_u32(tb[NL80211_ATTR_WIPHY_FREQ]);

  if (tb[NL80211_ATTR_MAC])
    bssid = nla_data(tb[NL80211_ATTR_MAC]);

  if (tb[NL80211_ATTR_SSID])
    {
      ssid = nla_data(tb[NL80211_ATTR_SSID]);
      ssid_len = nla_len(tb[NL80211_ATTR_SSID]);
    }

  if (tb[NL80211_ATTR_IE])
    {
      ie = nla_data(tb[NL80211_ATTR_IE]);
      ie_len = nla_len(tb[NL80211_ATTR_IE]);
    }

  nerr("nl80211_cmd_associate: freq=%lu ssid_len=%zu ie_len=%zu\n",
       (unsigned long)freq, ssid_len, ie_len);

  /* Build 802.11 Association Request frame */

  memset(frame, 0, sizeof(frame));
  frame_len = 0;

  /* Frame Control: Assoc Request (subtype=0) */

  frame[0] = 0x00; frame[1] = 0x00;
  frame_len = 2;

  /* Duration */

  frame[2] = 0; frame[3] = 0;
  frame_len = 4;

  /* DA = BSSID */

  if (bssid)
    memcpy(&frame[4], bssid, 6);
  frame_len = 10;

  /* SA = our MAC */

  memcpy(&frame[10],
         dev->d_wiphy ? dev->d_wiphy->perm_addr
                      : dev->d_mac.ether.ether_addr_octet, 6);
  frame_len = 16;

  /* BSSID */

  if (bssid)
    memcpy(&frame[16], bssid, 6);
  frame_len = 22;

  /* Sequence Control */

  frame[22] = 0; frame[23] = 0;
  frame_len = 24;

  /* Capability Info (2) + Listen Interval (2) */

  frame[24] = 0x31; frame[25] = 0x04;  /* ESS + Privacy + Short Preamble */
  frame[26] = 0x0a; frame[27] = 0x00;  /* Listen interval = 10 */
  frame_len = 28;

  /* SSID IE */

  if (ssid && ssid_len > 0 && frame_len + 2 + ssid_len < sizeof(frame))
    {
      frame[frame_len++] = 0;  /* SSID element ID */
      frame[frame_len++] = ssid_len;
      memcpy(&frame[frame_len], ssid, ssid_len);
      frame_len += ssid_len;
    }

  /* Extra IEs */

  if (ie && ie_len > 0 && frame_len + ie_len < sizeof(frame))
    {
      memcpy(&frame[frame_len], ie, ie_len);
      frame_len += ie_len;
    }

  /* Transmit */

  memset(&tx_params, 0, sizeof(tx_params));
  tx_params.buf = frame;
  tx_params.len = frame_len;

  if (freq > 0)
    {
      FAR struct ieee80211_channel *chan = nl80211_get_channel(freq);
      tx_params.chan = chan;
      ops->mgmt_tx(dev->d_wiphy, dev->d_wdev, &tx_params, &cookie);
      if (chan)
        kmm_free(chan);
    }
  else
    {
      ops->mgmt_tx(dev->d_wiphy, dev->d_wdev, &tx_params, &cookie);
    }

  return 0;
}

/****************************************************************************
 * Name: nl80211_cmd_deauthenticate
 *
 * Description:
 *   Handle NL80211_CMD_DEAUTHENTICATE. Builds a Deauth frame and sends it.
 *
 ****************************************************************************/

static int nl80211_cmd_deauthenticate(FAR struct netlink_conn_s *conn,
                                      FAR const struct nlmsghdr *nlmsg,
                                      FAR struct genlmsghdr *gnlh,
                                      FAR struct nlattr **tb)
{
  FAR struct net_driver_s *dev;
  FAR struct cfg80211_ops *ops;
  int ifindex;
  FAR const uint8_t *bssid = NULL;
  uint16_t reason = 0;
  uint8_t frame[26];
  struct cfg80211_mgmt_tx_params tx_params;
  uint64_t cookie;

  if (!tb[NL80211_ATTR_IFINDEX])
    {
      return -EINVAL;
    }

  ifindex = nla_get_u32(tb[NL80211_ATTR_IFINDEX]);
  dev = netdev_findbyindex(ifindex);
  if (!dev)
    {
      return -ENODEV;
    }

  ops = dev->d_cfg80211ops;
  if (!ops || !ops->mgmt_tx)
    {
      return -EOPNOTSUPP;
    }

  if (tb[NL80211_ATTR_MAC])
    bssid = nla_data(tb[NL80211_ATTR_MAC]);

  if (tb[NL80211_ATTR_REASON_CODE])
    reason = nla_get_u16(tb[NL80211_ATTR_REASON_CODE]);

  nerr("nl80211_cmd_deauthenticate: reason=%d\n", reason);

  /* Build Deauth frame: FC + Dur + DA + SA + BSSID + SeqCtrl + Reason */

  memset(frame, 0, sizeof(frame));
  frame[0] = 0xc0; frame[1] = 0x00;  /* Deauth */
  if (bssid)
    {
      memcpy(&frame[4], bssid, 6);
      memcpy(&frame[16], bssid, 6);
    }

  memcpy(&frame[10],
         dev->d_wiphy ? dev->d_wiphy->perm_addr
                      : dev->d_mac.ether.ether_addr_octet, 6);
  frame[24] = reason & 0xff;
  frame[25] = (reason >> 8) & 0xff;

  memset(&tx_params, 0, sizeof(tx_params));
  tx_params.buf = frame;
  tx_params.len = 26;
  ops->mgmt_tx(dev->d_wiphy, dev->d_wdev, &tx_params, &cookie);

  /* Also notify locally as a disconnect event */

  if (bssid)
    nl80211_driver_event_disconnect(dev, bssid, reason);

  return 0;
}

/****************************************************************************
 * Name: nl80211_cmd_disassociate
 *
 * Description:
 *   Handle NL80211_CMD_DISASSOCIATE. Builds a Disassoc frame and sends it.
 *
 ****************************************************************************/

static int nl80211_cmd_disassociate(FAR struct netlink_conn_s *conn,
                                    FAR const struct nlmsghdr *nlmsg,
                                    FAR struct genlmsghdr *gnlh,
                                    FAR struct nlattr **tb)
{
  FAR struct net_driver_s *dev;
  FAR struct cfg80211_ops *ops;
  int ifindex;
  FAR const uint8_t *bssid = NULL;
  uint16_t reason = 0;
  uint8_t frame[26];
  struct cfg80211_mgmt_tx_params tx_params;
  uint64_t cookie;

  if (!tb[NL80211_ATTR_IFINDEX])
    {
      return -EINVAL;
    }

  ifindex = nla_get_u32(tb[NL80211_ATTR_IFINDEX]);
  dev = netdev_findbyindex(ifindex);
  if (!dev)
    {
      return -ENODEV;
    }

  ops = dev->d_cfg80211ops;
  if (!ops || !ops->mgmt_tx)
    {
      return -EOPNOTSUPP;
    }

  if (tb[NL80211_ATTR_MAC])
    bssid = nla_data(tb[NL80211_ATTR_MAC]);

  if (tb[NL80211_ATTR_REASON_CODE])
    reason = nla_get_u16(tb[NL80211_ATTR_REASON_CODE]);

  nerr("nl80211_cmd_disassociate: reason=%d\n", reason);

  /* Build Disassoc frame */

  memset(frame, 0, sizeof(frame));
  frame[0] = 0xa0; frame[1] = 0x00;  /* Disassoc */
  if (bssid)
    {
      memcpy(&frame[4], bssid, 6);
      memcpy(&frame[16], bssid, 6);
    }

  memcpy(&frame[10],
         dev->d_wiphy ? dev->d_wiphy->perm_addr
                      : dev->d_mac.ether.ether_addr_octet, 6);
  frame[24] = reason & 0xff;
  frame[25] = (reason >> 8) & 0xff;

  memset(&tx_params, 0, sizeof(tx_params));
  tx_params.buf = frame;
  tx_params.len = 26;
  ops->mgmt_tx(dev->d_wiphy, dev->d_wdev, &tx_params, &cookie);

  return 0;
}

/****************************************************************************
 * Name: nl80211_cmd_set_bss
 *
 * Description:
 *   Handle NL80211_CMD_SET_BSS command. Sets BSS parameters.
 *
 * Input Parameters:
 *   conn  - Netlink connection
 *   nlmsg - Netlink message header
 *   gnlh  - Generic netlink message header
 *   tb    - Netlink attributes array
 *
 * Returned Value:
 *   0 on success; negative error code on failure
 *
 ****************************************************************************/

static int nl80211_cmd_set_bss(FAR struct netlink_conn_s *conn,
                               FAR const struct nlmsghdr *nlmsg,
                               FAR struct genlmsghdr *gnlh,
                               FAR struct nlattr **tb)
{
  ninfo("nl80211_cmd_set_bss: called\n");

  /* SET_BSS is used to set BSS attributes like CTS protection,
   * short preamble, short slot time, HT opmode, basic rates.
   * The BES driver handles these internally, so we just ACK.
   */

  return 0;
}

/****************************************************************************
 * Name: nl80211_bcast_station_event
 *
 * Description:
 *   Broadcast an AP client join/leave event (NL80211_CMD_NEW_STATION /
 *   NL80211_CMD_DEL_STATION) to the nl80211 "mlme" multicast group, as
 *   Linux does, so nl80211 observers (e.g. "iw event") and applications
 *   can watch SoftAP station connect/disconnect.
 *
 *   wpa_supplicant also joins the mlme group, but in host-SME AP mode
 *   (device_ap_sme=0, which is how this platform runs its SoftAP) its
 *   NL80211_CMD_NEW_STATION/DEL_STATION event handlers no-op: they only act
 *   when device_ap_sme is set (see driver_nl80211_event.c
 *   nl80211_new_station_event/nl80211_del_station_event). So the echo is
 *   harmless - wpa just logs a debug line and returns.
 *
 *   Only emitted for AP-type interfaces, after the station add/del command
 *   has succeeded, so STA-mode connect/disconnect is unaffected.
 *
 * Input Parameters:
 *   dev - Network device (must be an AP interface)
 *   mac - Station MAC address (6 bytes)
 *   cmd - NL80211_CMD_NEW_STATION or NL80211_CMD_DEL_STATION
 *
 ****************************************************************************/

static void nl80211_bcast_station_event(FAR struct net_driver_s *dev,
                                        FAR const uint8_t *mac, uint8_t cmd)
{
  FAR struct netlink_response_s *msg;
  FAR struct genlmsghdr *gnlh;
  struct nl80211_msg m;
  size_t attr_len;
  size_t total_len;

  if (!dev || !mac)
    {
      return;
    }

  /* AP interfaces only - never disturb STA-mode connect/disconnect. */

  if (!dev->d_wdev || dev->d_wdev->iftype != NL80211_IFTYPE_AP)
    {
      return;
    }

  attr_len = RTA_SPACE(sizeof(uint32_t))   /* IFINDEX */
           + RTA_SPACE(ETH_ALEN);          /* MAC */

  total_len = NLMSG_LENGTH(GENL_HDRLEN + attr_len);
  msg = (FAR struct netlink_response_s *)
        kmm_zalloc(sizeof(struct netlink_response_s) + total_len);
  if (!msg)
    {
      return;
    }

  msg->msg.nlmsg_type = nl80211_genl_family_id();
  msg->msg.nlmsg_flags = 0;
  msg->msg.nlmsg_seq = 0;
  msg->msg.nlmsg_pid = 0;

  /* Write via the declared flexible array member (same address as
   * NLMSG_DATA(&msg->msg)) so the genlmsghdr/attr writes stay in bounds
   * of a declared object (Coverity OVERRUN, CID 9150686).
   */

  gnlh = (FAR struct genlmsghdr *)msg->data;
  gnlh->cmd = cmd;
  gnlh->version = 1;

  nl80211_msg_init(&m, msg, GENL_HDRLEN + attr_len);

  nl80211_nla_put_u32(&m, NL80211_ATTR_IFINDEX, dev->d_ifindex);
  nl80211_nla_put(&m, NL80211_ATTR_MAC, ETH_ALEN, mac);

  nl80211_msg_finalize(&m);

  /* netlink_add_broadcast() derives the multicast bitmask from the group
   * number, so pass the group ID directly.
   */

  netlink_add_broadcast(NETLINK_GENERIC,
                        nl80211_genl_mcgrp_id(1), msg);
}

/****************************************************************************
 * Name: nl80211_cmd_new_station
 *
 * Description:
 *   Handle NL80211_CMD_NEW_STATION command. Adds a new station (AP mode).
 *
 * Input Parameters:
 *   conn  - Netlink connection
 *   nlmsg - Netlink message header
 *   gnlh  - Generic netlink message header
 *   tb    - Netlink attributes array
 *
 * Returned Value:
 *   0 on success; negative error code on failure
 *
 ****************************************************************************/

static int nl80211_cmd_new_station(FAR struct netlink_conn_s *conn,
                                   FAR const struct nlmsghdr *nlmsg,
                                   FAR struct genlmsghdr *gnlh,
                                   FAR struct nlattr **tb)
{
  FAR struct net_driver_s *dev;
  FAR struct cfg80211_ops *ops;
  struct station_parameters params;
  FAR const uint8_t *mac;
  int ifindex;
  int ret;

  ninfo("nl80211_cmd_new_station: called\n");

  if (!tb[NL80211_ATTR_IFINDEX] || !tb[NL80211_ATTR_MAC])
    {
      return -EINVAL;
    }

  ifindex = nla_get_u32(tb[NL80211_ATTR_IFINDEX]);
  dev = netdev_findbyindex(ifindex);
  if (!dev)
    {
      return -ENODEV;
    }

  ops = dev->d_cfg80211ops;
  if (!ops || !ops->add_station)
    {
      return -EOPNOTSUPP;
    }

  mac = nla_data(tb[NL80211_ATTR_MAC]);
  memset(&params, 0, sizeof(params));

  if (tb[NL80211_ATTR_STA_AID])
    {
      params.aid = nla_get_u16(tb[NL80211_ATTR_STA_AID]);
    }

  if (tb[NL80211_ATTR_STA_LISTEN_INTERVAL])
    {
      params.listen_interval =
        nla_get_u16(tb[NL80211_ATTR_STA_LISTEN_INTERVAL]);
    }

  if (tb[NL80211_ATTR_STA_SUPPORTED_RATES])
    {
      params.link_sta_params.supported_rates =
        nla_data(tb[NL80211_ATTR_STA_SUPPORTED_RATES]);
      params.link_sta_params.supported_rates_len =
        nla_len(tb[NL80211_ATTR_STA_SUPPORTED_RATES]);
    }

  if (tb[NL80211_ATTR_STA_CAPABILITY])
    {
      params.capability = nla_get_u16(tb[NL80211_ATTR_STA_CAPABILITY]);
    }

  if (tb[NL80211_ATTR_STA_FLAGS2])
    {
      FAR struct nl80211_sta_flag_update *flags =
        nla_data(tb[NL80211_ATTR_STA_FLAGS2]);
      params.sta_flags_mask = flags->mask;
      params.sta_flags_set = flags->set;
    }

  if (tb[NL80211_ATTR_HT_CAPABILITY])
    {
      params.link_sta_params.ht_capa =
        nla_data(tb[NL80211_ATTR_HT_CAPABILITY]);
    }

  if (tb[NL80211_ATTR_VHT_CAPABILITY])
    {
      params.link_sta_params.vht_capa =
        nla_data(tb[NL80211_ATTR_VHT_CAPABILITY]);
    }

  if (tb[NL80211_ATTR_HE_CAPABILITY])
    {
      params.link_sta_params.he_capa =
        nla_data(tb[NL80211_ATTR_HE_CAPABILITY]);
      params.link_sta_params.he_capa_len =
        nla_len(tb[NL80211_ATTR_HE_CAPABILITY]);
    }

  if (tb[NL80211_ATTR_OPMODE_NOTIF])
    {
      params.link_sta_params.opmode_notif =
        (uint8_t)nla_get_u16(tb[NL80211_ATTR_OPMODE_NOTIF]);
      params.link_sta_params.opmode_notif_used = true;
    }

  if (tb[NL80211_ATTR_STA_EXT_CAPABILITY])
    {
      params.ext_capab = nla_data(tb[NL80211_ATTR_STA_EXT_CAPABILITY]);
      params.ext_capab_len = nla_len(tb[NL80211_ATTR_STA_EXT_CAPABILITY]);
    }

  ret = ops->add_station(dev->d_wiphy, dev, mac, &params);
  if (ret == 0)
    {
      /* AP client joined: notify observers on the mlme group (AP only). */

      nl80211_bcast_station_event(dev, mac, NL80211_CMD_NEW_STATION);
    }

  return ret;
}

/****************************************************************************
 * Name: nl80211_cmd_del_station
 *
 * Description:
 *   Handle NL80211_CMD_DEL_STATION command. Removes a station (AP mode).
 *
 * Input Parameters:
 *   conn  - Netlink connection
 *   nlmsg - Netlink message header
 *   gnlh  - Generic netlink message header
 *   tb    - Netlink attributes array
 *
 * Returned Value:
 *   0 on success; negative error code on failure
 *
 ****************************************************************************/

static int nl80211_cmd_del_station(FAR struct netlink_conn_s *conn,
                                   FAR const struct nlmsghdr *nlmsg,
                                   FAR struct genlmsghdr *gnlh,
                                   FAR struct nlattr **tb)
{
  FAR struct net_driver_s *dev;
  FAR struct cfg80211_ops *ops;
  struct station_del_parameters params;
  int ifindex;
  int ret;

  ninfo("nl80211_cmd_del_station: called\n");

  if (!tb[NL80211_ATTR_IFINDEX])
    {
      return -EINVAL;
    }

  ifindex = nla_get_u32(tb[NL80211_ATTR_IFINDEX]);
  dev = netdev_findbyindex(ifindex);
  if (!dev)
    {
      return -ENODEV;
    }

  ops = dev->d_cfg80211ops;
  if (!ops || !ops->del_station)
    {
      return -EOPNOTSUPP;
    }

  memset(&params, 0, sizeof(params));

  if (tb[NL80211_ATTR_MAC])
    {
      params.mac = nla_data(tb[NL80211_ATTR_MAC]);
    }

  if (tb[NL80211_ATTR_REASON_CODE])
    {
      params.reason_code = nla_get_u16(tb[NL80211_ATTR_REASON_CODE]);
    }

  ret = ops->del_station(dev->d_wiphy, dev, &params);

  /* When flushing all stations (no MAC specified), -ENOENT means
   * no stations are connected - treat as success.
   */

  if (ret == -ENOENT && !params.mac)
    {
      ret = 0;
    }

  if (ret == 0 && params.mac)
    {
      /* AP client left: notify observers on the mlme group (AP only). */

      nl80211_bcast_station_event(dev, params.mac,
                                  NL80211_CMD_DEL_STATION);
    }

  return ret;
}

/****************************************************************************
 * Name: nl80211_cmd_set_tx_queue
 *
 * Description:
 *   Handle NL80211_CMD_SET_TX_QUEUE command. Sets TX queue parameters.
 *
 * Input Parameters:
 *   conn  - Netlink connection
 *   nlmsg - Netlink message header
 *   gnlh  - Generic netlink message header
 *   tb    - Netlink attributes array
 *
 * Returned Value:
 *   0 on success; negative error code on failure
 *
 ****************************************************************************/

static int nl80211_cmd_set_tx_queue(FAR struct netlink_conn_s *conn,
                                    FAR const struct nlmsghdr *nlmsg,
                                    FAR struct genlmsghdr *gnlh,
                                    FAR struct nlattr **tb)
{
  ninfo("nl80211_cmd_set_tx_queue: called\n");

  /* TX queue parameters - ACK for now */

  return 0;
}

/****************************************************************************
 * Name: nl80211_cmd_get_scan
 *
 * Description:
 *   Handle NL80211_CMD_GET_SCAN command. Returns stored scan results.
 *
 * Input Parameters:
 *   conn  - Netlink connection
 *   nlmsg - Netlink message header
 *   gnlh  - Generic netlink message header
 *   tb    - Netlink attributes array
 *
 * Returned Value:
 *   0 on success; negative error code on failure
 *
 ****************************************************************************/

static int nl80211_cmd_get_scan(FAR struct netlink_conn_s *conn,
                                FAR const struct nlmsghdr *nlmsg,
                                FAR struct genlmsghdr *gnlh,
                                FAR struct nlattr **tb)
{
  FAR struct net_driver_s *dev;
  FAR struct nl80211_scan_bss_s **bss_list = NULL;
  int num_bss = 0;
  int ifindex;
  int i;

  if (!tb[NL80211_ATTR_IFINDEX])
    {
      return -EINVAL;
    }

  ifindex = nla_get_u32(tb[NL80211_ATTR_IFINDEX]);
  dev = netdev_findbyindex(ifindex);
  if (!dev)
    {
      return -ENODEV;
    }

  nl80211_get_scan_results(&bss_list, &num_bss);
  ninfo("nl80211_cmd_get_scan: num_bss %d\n", num_bss);

  if (!bss_list || num_bss == 0)
    {
      nl80211_send_done_msg(conn, nlmsg);
      return 0;
    }

  for (i = 0; i < num_bss; i++)
    {
      FAR struct nl80211_scan_bss_s *bss = bss_list[i];
      FAR struct netlink_response_s *msg;
      FAR struct rtattr *bss_nest;
      struct nl80211_msg m;

      if (!bss) continue;

      /* Calculate required buffer size (max 2048)
       * When ie_len == 0 but beacon_ie_len > 0, beacon IEs are sent as
       * both NL80211_BSS_INFORMATION_ELEMENTS and NL80211_BSS_BEACON_IES
       * (matching Linux kernel behavior), so account for double copy.
       */

      size_t alloc_len = 256;
      if (bss->ie_len > 0)
        {
          alloc_len += bss->ie_len + 32;
        }

      if (bss->beacon_ie_len > 0)
        {
          alloc_len += bss->beacon_ie_len + 32;
          if (bss->ie_len == 0)
            {
              /* beacon IE will also be sent as INFORMATION_ELEMENTS */

              alloc_len += bss->beacon_ie_len + 32;
            }
        }

      if (alloc_len > 2048)
        {
          alloc_len = 2048;
        }

      msg = nl80211_alloc_reply_msg(nlmsg, NL80211_CMD_NEW_SCAN_RESULTS,
                                     alloc_len);
      if (!msg)
        {
          continue; /* Skip if OOM, try next */
        }

      nl80211_msg_init(&m, msg, alloc_len);

      /* NL80211_ATTR_IFINDEX */

      nl80211_nla_put_u32(&m, NL80211_ATTR_IFINDEX, ifindex);

      /* NL80211_ATTR_BSS (nested) */

      bss_nest = nl80211_nla_nest_start(&m, NL80211_ATTR_BSS);

      nl80211_nla_put(&m, NL80211_BSS_BSSID, 6, bss->bssid);
      nl80211_nla_put_u32(&m, NL80211_BSS_FREQUENCY, bss->freq);
      nl80211_nla_put_u16(&m, NL80211_BSS_BEACON_INTERVAL, bss->beacon_int);
      nl80211_nla_put_u16(&m, NL80211_BSS_CAPABILITY, bss->caps);
      nl80211_nla_put_u32(&m, NL80211_BSS_SIGNAL_MBM,
                          bss->level * 100); /* dBm to mBm */
      nl80211_nla_put_u64(&m, NL80211_BSS_TSF, bss->tsf);

      /* NL80211_BSS_INFORMATION_ELEMENTS
       * If probe response IEs are available, use them.
       * Otherwise, fall back to beacon IEs (matching Linux kernel behavior).
       * wpa_supplicant's wpa_bss_get_ie() only searches ie_len range,
       * so without this fallback, RSN/WPA IEs from beacon-only BSSes
       * would not be found, causing incorrect [WEP] flags.  The tailroom
       * check keeps these best-effort so a large IE never drops the
       * mandatory BSS attributes already written above.
       */

        {
          FAR const uint8_t *ie_src = NULL;
          size_t ie_src_len = 0;

          if (bss->ie_len > 0 && bss->ie_data)
            {
              ie_src = bss->ie_data;
              ie_src_len = bss->ie_len;
            }
          else if (bss->beacon_ie_len > 0 && bss->ie_data)
            {
              ie_src = bss->ie_data + bss->ie_len;
              ie_src_len = bss->beacon_ie_len;
            }

          if (ie_src && ie_src_len > 0 &&
              nl80211_msg_tailroom(&m) >= RTA_SPACE(ie_src_len))
            {
              nl80211_nla_put(&m, NL80211_BSS_INFORMATION_ELEMENTS,
                              ie_src_len, ie_src);
            }
        }

      /* NL80211_BSS_BEACON_IES */

      if (bss->beacon_ie_len > 0 && bss->ie_data &&
          nl80211_msg_tailroom(&m) >= RTA_SPACE(bss->beacon_ie_len))
        {
          nl80211_nla_put(&m, NL80211_BSS_BEACON_IES, bss->beacon_ie_len,
                          bss->ie_data + bss->ie_len);
        }

      nl80211_nla_nest_end(&m, bss_nest);

      nl80211_msg_finalize(&m);
      ninfo("nl80211_cmd_get_scan: msg->msg.nlmsg_len %lu\n",
           (unsigned long)msg->msg.nlmsg_len);

      netlink_add_response((NETLINK_HANDLE)conn, msg);
    }

  nl80211_send_done_msg(conn, nlmsg);
  return 0;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: nl80211_send_to_bss_conn
 *
 * Description:
 *   Send a netlink message as unicast to the BSS event connection.
 *   This emulates the Linux kernel behavior where NL80211_CMD_EXTERNAL_AUTH,
 *   NL80211_CMD_FRAME, and NL80211_CMD_FRAME_TX_STATUS are sent as unicast
 *   to the nl_mgmt socket, processed by wpa_supplicant's
 *   process_bss_event().
 *
 *   For NL80211_CMD_FRAME events, the function intelligently routes frames
 *   to the correct connection based on frame type and match data registered
 *   via NL80211_CMD_REGISTER_FRAME. This ensures AUTH, ACTION, and
 *   PROBE_REQ frames reach the appropriate wpa_supplicant socket.
 *
 * Input Parameters:
 *   msg - Netlink response message to send (will be freed on error)
 *
 * Returned Value:
 *   0 on success; negative error code on failure
 *   -ENOENT if no BSS connection is registered
 *
 ****************************************************************************/

int nl80211_send_to_bss_conn(FAR struct netlink_response_s *msg,
                             FAR struct net_driver_s *dev)
{
  FAR struct genlmsghdr *gnlh;
  FAR struct netlink_conn_s *target_conn = NULL;
  int cmd = -1;

  gnlh = (FAR struct genlmsghdr *)NLMSG_DATA(&msg->msg);
  cmd = gnlh->cmd;

  /* For CMD_FRAME events, try to route to the correct conn based on
   * the frame type in the payload. Extract the 802.11 frame header
   * from NL80211_ATTR_FRAME to determine the frame subtype.
   */

  nxmutex_lock(&g_frame_lock);

  if (cmd == NL80211_CMD_FRAME && g_frame_reg_count > 0)
    {
      FAR uint8_t *ptr = (FAR uint8_t *)gnlh + GENL_HDRLEN;
      FAR uint8_t *end = (FAR uint8_t *)&msg->msg + msg->msg.nlmsg_len;
      FAR const uint8_t *frame_data = NULL;
      size_t frame_len = 0;
      uint16_t fc = 0;
      int i;

      /* Parse attributes to find NL80211_ATTR_FRAME */

      while (ptr + sizeof(struct rtattr) <= end)
        {
          FAR struct rtattr *rta = (FAR struct rtattr *)ptr;
          if (rta->rta_len < sizeof(struct rtattr))
            {
              break;
            }

          if (rta->rta_type == NL80211_ATTR_FRAME)
            {
              frame_data = (FAR const uint8_t *)RTA_DATA(rta);
              frame_len = rta->rta_len - sizeof(struct rtattr);
              break;
            }

          ptr += RTA_ALIGN(rta->rta_len);
        }

      if (frame_data && frame_len >= 2)
        {
          fc = frame_data[0] | (frame_data[1] << 8);
          uint16_t stype_fc = fc & 0x00fc;  /* subtype field in fc */

          /* Match against registrations. The frame_type in registration
           * is the full fc subtype value (e.g. 0xb0 for AUTH).
           * We compare the subtype portion of fc.
           */

          for (i = g_frame_reg_count - 1; i >= 0; i--)
            {
              if (g_frame_regs[i].frame_type == stype_fc)
                {
                  /* If dev is specified, prefer registrations for the
                   * same interface. Skip registrations for other
                   * interfaces.
                   */

                  if (dev != NULL && g_frame_regs[i].ifindex != 0 &&
                      g_frame_regs[i].ifindex != dev->d_ifindex)
                    {
                      continue;
                    }

                  /* If there's match data, check it against the frame
                   * body (after the 802.11 header, 24 bytes).
                   */

                  if (g_frame_regs[i].match_len > 0 && frame_len > 24)
                    {
                      FAR const uint8_t *body = frame_data + 24;
                      size_t body_len = frame_len - 24;
                      if (body_len >= g_frame_regs[i].match_len &&
                          memcmp(body, g_frame_regs[i].match,
                                 g_frame_regs[i].match_len) == 0)
                        {
                          target_conn = g_frame_regs[i].conn;
                          break;
                        }
                    }
                  else if (g_frame_regs[i].match_len == 0)
                    {
                      /* Empty match = match all frames of this type */

                      target_conn = g_frame_regs[i].conn;
                      break;
                    }
                }
            }
        }
    }

  /* Fall back to g_bss_conn if no specific match found.
   * If dev is specified, try to find any conn registered for that
   * interface's ifindex first.
   */

  if (target_conn == NULL && dev != NULL)
    {
      int i;
      for (i = g_frame_reg_count - 1; i >= 0; i--)
        {
          if (g_frame_regs[i].ifindex == dev->d_ifindex)
            {
              target_conn = g_frame_regs[i].conn;
              break;
            }
        }
    }

  if (target_conn == NULL)
    {
      target_conn = g_bss_conn;
    }

  if (target_conn == NULL)
    {
      nxmutex_unlock(&g_frame_lock);
      nerr("ERROR: nl80211_send_to_bss_conn:"
           " no bss conn registered\n");
      kmm_free(msg);
      return -ENOENT;
    }

  netlink_add_response((NETLINK_HANDLE)target_conn, msg);

  nxmutex_unlock(&g_frame_lock);

  ninfo("nl80211_send_to_bss_conn: conn=%p cmd=%d\n",
        target_conn, cmd);

  return 0;
}

/****************************************************************************
 * Name: nl80211_handle_msg
 *
 * Description:
 *   Main message handler for nl80211 generic netlink messages. Routes
 *   incoming commands to appropriate handler functions.
 *
 * Input Parameters:
 *   conn  - Netlink connection
 *   nlmsg - Netlink message header
 *
 * Returned Value:
 *   0 on success; negative error code on failure
 *
 ****************************************************************************/

int nl80211_handle_msg(FAR struct netlink_conn_s *conn,
                       FAR const struct nlmsghdr *nlmsg)
{
  FAR struct genlmsghdr *gnlh = (FAR struct genlmsghdr *)NLMSG_DATA(nlmsg);
  FAR struct nlattr *tb[NL80211_ATTR_MAX + 1];
  int ret;

  /* Parse attributes */

  ret = nlmsg_parse(nlmsg, GENL_HDRLEN, tb, NL80211_ATTR_MAX, NULL, NULL);
  if (ret < 0)
    {
      return ret;
    }

  switch (gnlh->cmd)
    {
      case NL80211_CMD_TRIGGER_SCAN:
        ret = nl80211_cmd_trigger_scan(conn, nlmsg, gnlh, tb);
        break;

      case NL80211_CMD_ABORT_SCAN:
        ret = nl80211_cmd_abort_scan(conn, nlmsg, gnlh, tb);
        break;

      case NL80211_CMD_CONNECT:
        ret = nl80211_cmd_connect(conn, nlmsg, gnlh, tb);
        break;

      case NL80211_CMD_REGISTER_FRAME:
        ret = nl80211_cmd_register_frame(conn, nlmsg, gnlh, tb);
        break;

      case NL80211_CMD_GET_INTERFACE:
        ret = nl80211_cmd_get_interface(conn, nlmsg, gnlh, tb);
        break;
      case NL80211_CMD_SET_INTERFACE:
        ret = nl80211_cmd_set_interface(conn, nlmsg, gnlh, tb);
        break;
      case NL80211_CMD_GET_WIPHY:
        ret = nl80211_cmd_get_wiphy(conn, nlmsg, gnlh, tb);
        break;

      case NL80211_CMD_GET_PROTOCOL_FEATURES:
        ret = nl80211_cmd_get_protocol_features(conn, nlmsg, gnlh, tb);
        break;

      case NL80211_CMD_GET_SCAN:
        ret = nl80211_cmd_get_scan(conn, nlmsg, gnlh, tb);
        break;

      case NL80211_CMD_DEL_KEY:
        ret = nl80211_cmd_del_key(conn, nlmsg, gnlh, tb);
        break;

      case NL80211_CMD_NEW_KEY:
        ret = nl80211_cmd_new_key(conn, nlmsg, gnlh, tb);
        break;

      case NL80211_CMD_SET_KEY:
        ret = nl80211_cmd_set_key(conn, nlmsg, gnlh, tb);
        break;

      case NL80211_CMD_SET_PMKSA:
        ret = nl80211_cmd_set_pmksa(conn, nlmsg, gnlh, tb);
        break;

      case NL80211_CMD_FLUSH_PMKSA:
        ret = nl80211_cmd_flush_pmksa(conn, nlmsg, gnlh, tb);
        break;

      case NL80211_CMD_SET_REKEY_OFFLOAD:
        ret = nl80211_cmd_set_rekey_offload(conn, nlmsg, gnlh, tb);
        break;

      case NL80211_CMD_DISCONNECT:
        ret = nl80211_cmd_disconnect(conn, nlmsg, gnlh, tb);
        break;

      case NL80211_CMD_SET_STATION:
        ret = nl80211_cmd_set_station(conn, nlmsg, gnlh, tb);
        break;

      case NL80211_CMD_EXTERNAL_AUTH:
        ret = nl80211_cmd_external_auth(conn, nlmsg, gnlh, tb);
        break;

      case NL80211_CMD_FRAME:
        ret = nl80211_cmd_frame(conn, nlmsg, gnlh, tb);
        break;

      case NL80211_CMD_REMAIN_ON_CHANNEL:
        ret = nl80211_cmd_remain_on_channel(conn, nlmsg, gnlh, tb);
        break;

      case NL80211_CMD_CANCEL_REMAIN_ON_CHANNEL:
        ret = nl80211_cmd_cancel_remain_on_channel(conn, nlmsg, gnlh, tb);
        break;

      case NL80211_CMD_GET_REG:
        ret = nl80211_cmd_get_reg(conn, nlmsg, gnlh, tb);
        break;

      case NL80211_CMD_SET_WIPHY:
        ret = nl80211_cmd_set_wiphy(conn, nlmsg, gnlh, tb);
        break;

      case NL80211_CMD_START_AP:
        ret = nl80211_cmd_start_ap(conn, nlmsg, gnlh, tb);
        break;

      case NL80211_CMD_SET_BEACON:
        ret = nl80211_cmd_set_beacon(conn, nlmsg, gnlh, tb);
        break;

      case NL80211_CMD_STOP_AP:
        ret = nl80211_cmd_stop_ap(conn, nlmsg, gnlh, tb);
        break;

      case NL80211_CMD_SET_BSS:
        ret = nl80211_cmd_set_bss(conn, nlmsg, gnlh, tb);
        break;

      case NL80211_CMD_NEW_STATION:
        ret = nl80211_cmd_new_station(conn, nlmsg, gnlh, tb);
        break;

      case NL80211_CMD_DEL_STATION:
        ret = nl80211_cmd_del_station(conn, nlmsg, gnlh, tb);
        break;

      case NL80211_CMD_SET_TX_BITRATE_MASK:
        ret = nl80211_cmd_set_tx_queue(conn, nlmsg, gnlh, tb);
        break;

      case NL80211_CMD_UNEXPECTED_FRAME:
        /* Register for spurious class 3 frames from unassociated STAs.
         * Just accept the registration - we don't need to do anything.
         */

        nerr("nl80211: NL80211_CMD_UNEXPECTED_FRAME registered\n");
        ret = 0;
        break;

      case NL80211_CMD_REGISTER_BEACONS:
        /* Register to receive beacons from other BSSes for OLBC handling.
         * Just accept the registration - no action needed.
         */

        nerr("nl80211: NL80211_CMD_REGISTER_BEACONS registered\n");
        ret = 0;
        break;

      case NL80211_CMD_AUTHENTICATE:
        ret = nl80211_cmd_authenticate(conn, nlmsg, gnlh, tb);
        break;

      case NL80211_CMD_ASSOCIATE:
        ret = nl80211_cmd_associate(conn, nlmsg, gnlh, tb);
        break;

      case NL80211_CMD_DEAUTHENTICATE:
        ret = nl80211_cmd_deauthenticate(conn, nlmsg, gnlh, tb);
        break;

      case NL80211_CMD_DISASSOCIATE:
        ret = nl80211_cmd_disassociate(conn, nlmsg, gnlh, tb);
        break;

      default:
        nwarn("nl80211: Unknown command %d\n", gnlh->cmd);
        return -EOPNOTSUPP;
    }

  if (ret >= 0)
    {
      /* For dump requests (NLM_F_DUMP), the response ends with NLMSG_DONE.
       * Do not send ACK for dump requests - the DONE message serves as the
       * terminator. Sending ACK would confuse libnl's nl_recvmsgs() which
       * expects either DONE (for multipart) or ACK (for non-multipart).
       */

      if (!(nlmsg->nlmsg_flags & NLM_F_DUMP) &&
          (nlmsg->nlmsg_flags & NLM_F_ACK))
        {
          nl80211_send_ack(conn, nlmsg);
        }

      return nlmsg->nlmsg_len;
    }

  return ret;
}

/****************************************************************************
 * Name: nl80211_send_scan_results
 *
 * Description:
 *   Send scan results event (NL80211_CMD_NEW_SCAN_RESULTS) to userspace.
 *   This is usually called from driver callback when scan is completed.
 *   The event notifies wpa_supplicant that scan results are available
 *   and can be retrieved via NL80211_CMD_GET_SCAN.
 *
 *   The message is broadcast to the "scan" multicast group so all
 *   listening applications receive the notification.
 *
 * Input Parameters:
 *   dev - Network device that completed the scan
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

void nl80211_send_scan_results(FAR struct net_driver_s *dev)
{
  FAR struct netlink_response_s *msg;
  FAR struct genlmsghdr *resp_gnlh;
  struct nl80211_msg m;
  size_t buflen = GENL_HDRLEN + RTA_SPACE(sizeof(uint32_t));

  if (!dev)
    {
      return;
    }

  /* Allocate response buffer */

  msg = (FAR struct netlink_response_s *)
        kmm_zalloc(sizeof(struct netlink_response_s) + buflen);
  if (msg == NULL)
    {
      return;
    }

  /* Construct message */

  msg->msg.nlmsg_type = nl80211_genl_family_id();
  msg->msg.nlmsg_flags = 0;
  msg->msg.nlmsg_seq = 0;
  msg->msg.nlmsg_pid = 0; /* From kernel */

  resp_gnlh = (FAR struct genlmsghdr *)NLMSG_DATA(&msg->msg);
  resp_gnlh->cmd = NL80211_CMD_NEW_SCAN_RESULTS;
  resp_gnlh->version = 1;

  nl80211_msg_init(&m, msg, buflen);
  if (nl80211_nla_put_u32(&m, NL80211_ATTR_IFINDEX, dev->d_ifindex) < 0)
    {
      kmm_free(msg);
      return;
    }

  nl80211_msg_finalize(&m);

  /* Broadcast to "scan" group */

  netlink_add_broadcast(NETLINK_GENERIC,
                        nl80211_genl_mcgrp_id(0), msg);
}

/****************************************************************************
 * Name: nl80211_send_scan_start
 *
 * Description:
 *   Send scan trigger event (NL80211_CMD_TRIGGER_SCAN) to userspace.
 *   This indicates that a scan has been started and is in progress.
 *   This event is optional but helps userspace track scan state.
 *
 *   The message is broadcast to the "scan" multicast group. When the
 *   scan completes, nl80211_send_scan_results() should be called to
 *   notify that results are available.
 *
 * Input Parameters:
 *   dev - Network device that started the scan
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

void nl80211_send_scan_start(FAR struct net_driver_s *dev)
{
  FAR struct netlink_response_s *msg;
  FAR struct genlmsghdr *resp_gnlh;
  struct nl80211_msg m;
  size_t buflen = GENL_HDRLEN + RTA_SPACE(sizeof(uint32_t));

  if (!dev)
    {
      return;
    }

  /* Allocate response buffer */

  msg = (FAR struct netlink_response_s *)
        kmm_zalloc(sizeof(struct netlink_response_s) + buflen);
  if (msg == NULL)
    {
      return;
    }

  /* Construct message */

  msg->msg.nlmsg_type = nl80211_genl_family_id();
  msg->msg.nlmsg_flags = 0;
  msg->msg.nlmsg_seq = 0;
  msg->msg.nlmsg_pid = 0; /* From kernel */

  resp_gnlh = (FAR struct genlmsghdr *)NLMSG_DATA(&msg->msg);
  resp_gnlh->cmd = NL80211_CMD_TRIGGER_SCAN;
  resp_gnlh->version = 1;

  nl80211_msg_init(&m, msg, buflen);
  if (nl80211_nla_put_u32(&m, NL80211_ATTR_IFINDEX, dev->d_ifindex) < 0)
    {
      kmm_free(msg);
      return;
    }

  nl80211_msg_finalize(&m);

  /* Broadcast to "scan" group */

  netlink_add_broadcast(NETLINK_GENERIC,
                        nl80211_genl_mcgrp_id(0), msg);
}
