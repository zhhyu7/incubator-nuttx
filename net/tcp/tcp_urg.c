/****************************************************************************
 * net/tcp/tcp_urg.c
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
#include <sys/socket.h>

#include <nuttx/mm/iob.h>
#include <nuttx/net/netdev.h>

#include "devif/devif.h"
#include "socket/socket.h"
#include "tcp/tcp.h"

#ifdef CONFIG_NET_TCPURGDATA

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* SO_OOBINLINE keeps the urgent byte in the normal stream.  The option
 * cannot be set at all when socket options are disabled.
 */

#ifdef CONFIG_NET_SOCKOPTS
#  define tcp_urg_inline(conn) \
     _SO_GETOPT((conn)->sconn.s_options, SO_OOBINLINE)
#else
#  define tcp_urg_inline(conn) (false)
#endif

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: tcp_urg_input
 *
 * Description:
 *   Record the urgent pointer of an incoming segment and copy the urgent
 *   byte aside once in-order data covers it.  The payload is never
 *   modified:  the byte is taken out of the normal stream later, by
 *   tcp_urg_clip(), which keeps rcvseq, the receive window and the ACKs
 *   unaffected and lets a superseded urgent byte be delivered as normal
 *   data instead of being lost.
 *
 *   BSD/Linux put the urgent pointer one byte past the urgent byte, so the
 *   byte itself lives at 'segment sequence + urgp - 1'.
 *
 * Input Parameters:
 *   conn - The TCP connection of interest
 *   tcp  - TCP header of the incoming segment, or NULL for a reassembled
 *          out-of-order segment, whose header was consumed earlier
 *   iob  - I/O buffer chain holding the payload
 *   off  - Offset of the payload within 'iob' (L3/L4 headers for a device
 *          buffer, zero for a reassembled segment)
 *   seq  - Sequence number of the first payload byte
 *   len  - Number of payload bytes
 *
 * Returned Value:
 *   TCP_URGDATA if the urgent byte has just become readable, else zero.
 *
 * Assumptions:
 *   The network is locked.
 *
 ****************************************************************************/

uint32_t tcp_urg_input(FAR struct tcp_conn_s *conn,
                       FAR struct tcp_hdr_s *tcp, FAR struct iob_s *iob,
                       unsigned int off, uint32_t seq, uint16_t len)
{
  /* A new urgent pointer?  Ignore one that points into data we have already
   * consumed, and do not let a duplicate or reordered segment move the mark
   * backwards.
   */

  if (tcp != NULL && (tcp->flags & TCP_URG) != 0)
    {
      uint16_t urgp = (tcp->urgp[0] << 8) | tcp->urgp[1];

      if (urgp > 0)
        {
          uint32_t up = TCP_SEQ_ADD(tcp_getsequence(tcp->seqno), urgp - 1);

          if (TCP_SEQ_GTE(up, tcp_getsequence(conn->rcvseq)) &&
              (conn->urg_state == TCP_URG_NONE ||
               TCP_SEQ_GT(up, conn->rcv_up)))
            {
              conn->rcv_up    = up;
              conn->urg_state = TCP_URG_NOTYET;
            }
        }
    }

  /* Is the urgent byte inside this payload?  This also covers a pointer
   * that arrived in an earlier segment.
   */

  if (conn->urg_state != TCP_URG_NOTYET || iob == NULL || len == 0 ||
      TCP_SEQ_LT(conn->rcv_up, seq) ||
      TCP_SEQ_GTE(conn->rcv_up, TCP_SEQ_ADD(seq, len)))
    {
      return 0;
    }

  if (iob_copyout(&conn->urg_data, iob, 1,
                  off + TCP_SEQ_SUB(conn->rcv_up, seq)) != 1)
    {
      return 0;
    }

  conn->urg_state = TCP_URG_VALID;
  return TCP_URGDATA;
}

/****************************************************************************
 * Name: tcp_urg_clip
 *
 * Description:
 *   Adjust a pending read of '*len' bytes starting at sequence number 'seq'
 *   so that the urgent byte is never returned as normal data:
 *
 *     - urgent byte comes first:  report it as skipped, the caller steps
 *       over it;
 *     - urgent byte is further ahead:  shorten the read to stop right
 *       before it, which makes it first on the next read;
 *     - otherwise:  leave the read alone.
 *
 *   With SO_OOBINLINE the byte belongs to the normal stream and the read is
 *   never touched.
 *
 * Input Parameters:
 *   conn - The TCP connection of interest
 *   seq  - Sequence number of the first byte about to be consumed
 *   len  - Number of bytes to consume, updated on return
 *
 * Returned Value:
 *   1 if the byte at 'seq' must be skipped, else zero.
 *
 * Assumptions:
 *   The network is locked.
 *
 ****************************************************************************/

int tcp_urg_clip(FAR struct tcp_conn_s *conn, uint32_t seq,
                 FAR size_t *len)
{
  uint32_t offset;

  if (conn->urg_state == TCP_URG_NONE || tcp_urg_inline(conn))
    {
      return 0;
    }

  /* Where is the mark relative to the first byte being read? */

  offset = TCP_SEQ_SUB(conn->rcv_up, seq);
  if (offset >= *len)
    {
      /* Not in this read, or already behind us */

      return 0;
    }

  if (offset > 0)
    {
      /* Stop just before the urgent byte */

      *len = offset;
      return 0;
    }

  /* The urgent byte is first:  step over it */

  if (*len > 0)
    {
      (*len)--;
    }

  return 1;
}

/****************************************************************************
 * Name: tcp_urg_mark
 *
 * Description:
 *   Record that the last of 'pending' bytes queued for transmission is the
 *   urgent byte.  The mark is kept as an absolute sequence number, one byte
 *   past it, matching the on-the-wire format, so that every outgoing
 *   segment can derive its own urgent pointer in tcp_sendcommon() and
 *   retransmission, trimming, chunking and coalescing need no urgent
 *   specific code.
 *
 * Input Parameters:
 *   conn    - The TCP connection of interest
 *   pending - Number of bytes queued but not yet acknowledged, including
 *             the urgent byte
 *
 * Returned Value:
 *   None
 *
 * Assumptions:
 *   The network is locked.
 *
 ****************************************************************************/

void tcp_urg_mark(FAR struct tcp_conn_s *conn, uint32_t pending)
{
  conn->snd_up = TCP_SEQ_ADD(conn->snd_wl2, pending);
}

#endif /* CONFIG_NET_TCPURGDATA */
