/****************************************************************************
 * net/tcp/tcp_backlog.c
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

#include <nuttx/net/netconfig.h>
#if defined(CONFIG_NET) && defined(CONFIG_NET_TCP) && defined(CONFIG_NET_TCPBACKLOG)

#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <debug.h>

#include <nuttx/nuttx.h>
#include <nuttx/queue.h>
#include <nuttx/net/net.h>

#include "devif/devif.h"
#include "utils/utils.h"
#include "tcp/tcp.h"

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: tcp_backlog_conn
 *
 * Description:
 *   Recover the backlogged tcp_conn_s from one of its pending-list nodes.
 *   Each backlogged connection is linked into its parent listener's pending
 *   list through its own tcp_conn_s::bl_node, so the owning connection is
 *   obtained with container_of().
 *
 ****************************************************************************/

static inline FAR struct tcp_conn_s *tcp_backlog_conn(FAR sq_entry_t *node)
{
  return container_of(node, struct tcp_conn_s, bl_node);
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: tcp_backlogcreate
 *
 * Description:
 *   Called from the listen() logic to setup the backlog as specified in the
 *   the listen arguments.
 *
 * Assumptions:
 *   Called from normal task logic.  The network may or may not be locked.
 *
 ****************************************************************************/

int tcp_backlogcreate(FAR struct tcp_conn_s *conn, int nblg)
{
  ninfo("conn=%p nblg=%d\n", conn, nblg);

#ifdef CONFIG_DEBUG_FEATURES
  if (!conn)
    {
      return -EINVAL;
    }
#endif

  /* Clamp the requested backlog to the valid range.  nblg <= 0 disables
   * backlog support, leaving the connection as a non-listener (bl_max == 0).
   */

  if (nblg > SOMAXCONN)
    {
      nblg = SOMAXCONN;
    }
  else if (nblg < 0)
    {
      nblg = 0;
    }

  /* Destroy any existing backlog (shouldn't be any).  Since the network is
   * locked, destroying the old backlog and replacing it with the new one is
   * an atomic operation.
   */

  conn_lock(&conn->sconn);
  tcp_backlogdestroy(conn);

  /* Install the new backlog in the connection.  nblg has been clamped to
   * [0, SOMAXCONN]; a value of 0 simply disables backlog support and leaves
   * the connection as a non-listener (bl_max == 0).
   */

  conn->bl_max = nblg;

  conn_unlock(&conn->sconn);
  return OK;
}

/****************************************************************************
 * Name: tcp_backlogdestroy
 *
 * Description:
 *   (1) Called from tcp_free() whenever a connection is freed.
 *   (2) Called from tcp_backlogcreate() to destroy any old backlog
 *
 *   NOTE: This function may re-enter tcp_free when a connection that
 *   is freed that has pending connections.
 *
 * Assumptions:
 *   Called from network socket logic with the network locked
 *
 ****************************************************************************/

int tcp_backlogdestroy(FAR struct tcp_conn_s *conn)
{
  FAR sq_entry_t        *node;
  FAR struct tcp_conn_s *blconn;

  ninfo("conn=%p\n", conn);

#ifdef CONFIG_DEBUG_FEATURES
  if (!conn)
    {
      return -EINVAL;
    }
#endif

  /* Handle any pending connections in the backlog */

  while ((node = sq_remfirst(&conn->bl_node)) != NULL)
    {
      blconn = tcp_backlog_conn(node);

      /* REVISIT
       * -- such connections really need to be gracefully closed
       */

      blconn->blparent = NULL;
      blconn->crefs    = 0;
      tcp_free(blconn);
    }

  /* Mark the connection as no longer a listener */

  conn->bl_max   = 0;
  conn->bl_count = 0;

  return OK;
}

/****************************************************************************
 * Name: tcp_backlogadd
 *
 * Description:
 *  Called tcp_listen when a new connection is made with a listener socket
 *  but when there is no accept() in place to receive the connection.  This
 *  function adds the new connection to the backlog.
 *
 * Assumptions:
 *   Called from network socket logic with the network locked
 *
 ****************************************************************************/

int tcp_backlogadd(FAR struct tcp_conn_s *conn,
                   FAR struct tcp_conn_s *blconn)
{
  ninfo("conn=%p blconn=%p\n", conn, blconn);

  if (conn->bl_max == 0 || !blconn)
    {
      return -EINVAL;
    }

  /* Make sure that the backlog is not already full */

  if (conn->bl_count >= conn->bl_max)
    {
      nerr("ERROR: There are no free slots for TCP BACKLOG!\n");
      return -ENOMEM;
    }

  /* Link the new connection into the end of the pending list (FIFO) through
   * its own node, and record its parent listener.
   */

  blconn->blparent = conn;
  sq_addlast(&blconn->bl_entry, &conn->bl_node);
  conn->bl_count++;

  return OK;
}

/****************************************************************************
 * Name: tcp_backlogpending
 *
 * Description:
 *  Called from poll().  Before waiting for a new connection, poll will
 *  call this API to see if there are pending connections in the backlog.
 *
 * Assumptions:
 *   Called from network socket logic with the network locked
 *
 ****************************************************************************/

bool tcp_backlogpending(FAR struct tcp_conn_s *conn)
{
  return (conn && !sq_empty(&conn->bl_node));
}

/****************************************************************************
 * Name: tcp_backlogavailable
 *
 * Description:
 *  Called from tcp_input().  Before alloc a new accept connection, tcp_input
 *  will call this API to see if there is a free slot in the backlog.
 *
 * Assumptions:
 *   Called from network socket logic with the network locked
 *
 ****************************************************************************/

bool tcp_backlogavailable(FAR struct tcp_conn_s *conn)
{
  return (conn && conn->bl_count < conn->bl_max);
}

/****************************************************************************
 * Name: tcp_backlogremove
 *
 * Description:
 *  Called from accept().  Before waiting for a new connection, accept will
 *  call this API to see if there are pending connections in the backlog.
 *
 * Assumptions:
 *   Called from network socket logic with the network locked
 *
 ****************************************************************************/

FAR struct tcp_conn_s *tcp_backlogremove(FAR struct tcp_conn_s *conn)
{
  FAR sq_entry_t        *node;
  FAR struct tcp_conn_s *blconn = NULL;

#ifdef CONFIG_DEBUG_FEATURES
  if (!conn)
    {
      return NULL;
    }
#endif

  /* Remove the connection at the head of the pending list (FIFO) */

  node = sq_remfirst(&conn->bl_node);
  if (node)
    {
      blconn           = tcp_backlog_conn(node);
      blconn->blparent = NULL;
      conn->bl_count--;
    }

  ninfo("conn=%p, returning %p\n", conn, blconn);
  return blconn;
}

/****************************************************************************
 * Name: tcp_backlogdelete
 *
 * Description:
 *  Called from tcp_free() when a connection is freed that this also
 *  retained in the pending connection list of a listener.  We simply need
 *  to remove the defunct connection from the list.
 *
 * Assumptions:
 *   Called from network socket logic with the network locked
 *
 ****************************************************************************/

int tcp_backlogdelete(FAR struct tcp_conn_s *conn,
                      FAR struct tcp_conn_s *blconn)
{
  FAR sq_entry_t *node;
  FAR sq_entry_t *prev;

  ninfo("conn=%p blconn=%p\n", conn, blconn);

#ifdef CONFIG_DEBUG_FEATURES
  if (!conn)
    {
      return -EINVAL;
    }
#endif

  /* Find the node that holds the connection */

  for (node = sq_peek(&conn->bl_node), prev = NULL;
       node;
       prev = node, node = sq_next(node))
    {
      if (tcp_backlog_conn(node) == blconn)
        {
          if (prev)
            {
              /* Remove the node from the middle of the pending list */

              sq_remafter(prev, &conn->bl_node);
            }
          else
            {
              /* Remove the node from the head of the pending list */

              sq_remfirst(&conn->bl_node);
            }

          conn->bl_count--;
          return OK;
        }
    }

  nerr("ERROR: Failed to find pending connection\n");
  return -EINVAL;
}

#endif /* CONFIG_NET && CONFIG_NET_TCP && CONFIG_NET_TCPBACKLOG */
