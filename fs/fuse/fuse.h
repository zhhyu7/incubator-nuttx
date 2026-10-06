/****************************************************************************
 * fs/fuse/fuse.h
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

#ifndef __FS_FUSE_FUSE_H
#define __FS_FUSE_FUSE_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <sys/uio.h>
#include <stdbool.h>

#include <nuttx/atomic.h>
#include <nuttx/fuse.h>
#include <nuttx/queue.h>
#include <nuttx/semaphore.h>

/****************************************************************************
 * Public Types
 ****************************************************************************/

struct fuse_conn_s;
struct fuse_req_s;

/* Transport abstraction.  A transport moves FUSE request/reply bytes
 * between the core and a server; completion invokes req->done.
 * The optional maxbuf() reports the largest payload one request can carry
 * at this moment (bouncing transports bounded by their shared memory);
 * NULL means no limit.
 */

struct fuse_transport_ops_s
{
  CODE int    (*request)(FAR struct fuse_conn_s *conn,
                         FAR struct fuse_req_s *req);
  CODE void   (*destroy)(FAR struct fuse_conn_s *conn);
  CODE size_t (*maxbuf)(FAR struct fuse_conn_s *conn);
};

/* A single in-flight FUSE request */

struct fuse_req_s
{
  struct fuse_in_header   hin;   /* Request header (filled by core) */
  struct fuse_out_header  hout;  /* Reply header (filled by transport) */
  FAR void               *arg;   /* Completion callback argument */
  CODE void             (*done)(FAR struct fuse_req_s *req);
  FAR const struct iovec *vin;   /* Request args (sent to server) */
  size_t                  vin_cnt;
  FAR const struct iovec *vout;  /* Reply buffers (received from server) */
  size_t                  vout_cnt;
  sq_entry_t              entry; /* Pending/processing queue linkage */
};

/* A FUSE connection (one per mountpoint) */

struct fuse_conn_s
{
  FAR const struct fuse_transport_ops_s *transport;
  FAR void    *priv;      /* Transport private data */
  sem_t        initdone;  /* Posted when FUSE_INIT completes */
  volatile int initerror; /* -EINPROGRESS until FUSE_INIT completes */
  atomic_t     unique;    /* Request id generator */
  uint32_t     max_write; /* Negotiated maximum write size */
};

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

#ifdef CONFIG_FS_FUSE_DEV
/* Implemented by fs/fuse/fuse_dev.c */

int fuse_dev_connect(FAR struct fuse_conn_s *conn, FAR const char *options);
int fuse_dev_initialize(void);
#endif

#ifdef CONFIG_FS_VIRTIOFS
/* Implemented by fs/fuse/fuse_virtio.c */

int fuse_virtio_connect(FAR struct fuse_conn_s *conn,
                        FAR const char *options);
#endif

#endif /* __FS_FUSE_FUSE_H */
