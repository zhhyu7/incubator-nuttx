/****************************************************************************
 * fs/fuse/fuse_dev.c
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

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdlib.h>
#include <string.h>
#include <sys/param.h>

#include <nuttx/fs/fs.h>
#include <nuttx/fs/uio.h>
#include <nuttx/mutex.h>
#include <nuttx/nuttx.h>

#include "fs_heap.h"
#include "fuse.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define FUSE_DEV_NPOLLWAITERS 2

/* Sentinel for fuse_dev_s.conn:  the peer went away (server close or
 * unmount).  NULL means no filesystem has been mounted yet.
 */

#define FUSE_DEV_DEAD         ((FAR struct fuse_conn_s *)-1)

/****************************************************************************
 * Private Types
 ****************************************************************************/

/* Per-open state of the /dev/fuse character device.  A FUSE server opens
 * /dev/fuse, mounts a filesystem with "fd=<n>" (which links a connection to
 * this state through fuse_dev_connect) and then reads requests and writes
 * replies.
 */

struct fuse_dev_s
{
  mutex_t                 lock;       /* Protects the queues and crefs */
  sem_t                   reader;     /* Wakes a blocked reader */
  sq_queue_t              pending;    /* Requests awaiting read by server */
  sq_queue_t              processing; /* Requests awaiting a reply */
  FAR struct fuse_conn_s *conn;       /* NULL, live conn or FUSE_DEV_DEAD */
  uint8_t                 crefs;      /* Reference count */
  FAR struct pollfd      *fds[FUSE_DEV_NPOLLWAITERS];
};

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static int     fuse_dev_open(FAR struct file *filep);
static int     fuse_dev_close(FAR struct file *filep);
static ssize_t fuse_dev_readv(FAR struct file *filep, FAR struct uio *uio);
static ssize_t fuse_dev_writev(FAR struct file *filep, FAR struct uio *uio);
static int     fuse_dev_poll(FAR struct file *filep, FAR struct pollfd *fds,
                             bool setup);

static int     fuse_dev_request(FAR struct fuse_conn_s *conn,
                                FAR struct fuse_req_s *req);
static void    fuse_dev_destroy(FAR struct fuse_conn_s *conn);

/****************************************************************************
 * Private Data
 ****************************************************************************/

static const struct file_operations g_fuse_dev_fops =
{
  fuse_dev_open,   /* open */
  fuse_dev_close,  /* close */
  NULL,            /* read */
  NULL,            /* write */
  NULL,            /* seek */
  NULL,            /* ioctl */
  NULL,            /* truncate */
  fuse_dev_poll,   /* poll */
  fuse_dev_readv,  /* readv */
  fuse_dev_writev, /* writev */
};

static const struct fuse_transport_ops_s g_fuse_dev_transport =
{
  fuse_dev_request,  /* request */
  fuse_dev_destroy,  /* destroy */
  NULL               /* maxbuf */
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: fuse_dev_notify
 *
 * Description:
 *   Wake the server:  notify pollers with events and post the reader
 *   semaphore.  The post is skipped when enough wakeups are already
 *   pending so that the semaphore count stays bounded.  Must be called
 *   with dev->lock held.
 *
 ****************************************************************************/

static inline void
fuse_dev_notify(FAR struct fuse_dev_s *dev, pollevent_t events)
{
  poll_notify(dev->fds, FUSE_DEV_NPOLLWAITERS, events);

  if (events & (POLLIN | POLLHUP))
    {
      int sval = 0;

      nxsem_get_value(&dev->reader, &sval);
      if (sval <= 1)
        {
          nxsem_post(&dev->reader);
        }
    }
}

/****************************************************************************
 * Name: fuse_dev_abort
 ****************************************************************************/

static void fuse_dev_abort(FAR struct fuse_dev_s *dev)
{
  FAR struct fuse_conn_s *conn;
  FAR sq_entry_t *entry;

  nxmutex_lock(&dev->lock);
  conn = dev->conn;
  dev->conn = FUSE_DEV_DEAD;

  if (conn != NULL && conn != FUSE_DEV_DEAD)
    {
      fuse_abort(conn);
    }

  while ((entry = sq_remfirst(&dev->pending)) != NULL)
    {
      FAR struct fuse_req_s *req = container_of(entry, struct fuse_req_s,
                                                entry);
      nxmutex_unlock(&dev->lock);
      req->hout.error = -ENOTCONN;
      req->done(req);
      nxmutex_lock(&dev->lock);
    }

  while ((entry = sq_remfirst(&dev->processing)) != NULL)
    {
      FAR struct fuse_req_s *req = container_of(entry, struct fuse_req_s,
                                                entry);
      nxmutex_unlock(&dev->lock);
      req->hout.error = -ENOTCONN;
      req->done(req);
      nxmutex_lock(&dev->lock);
    }

  fuse_dev_notify(dev, POLLHUP);
  nxmutex_unlock(&dev->lock);
}

/****************************************************************************
 * Name: fuse_dev_open
 ****************************************************************************/

static int fuse_dev_open(FAR struct file *filep)
{
  FAR struct fuse_dev_s *dev;

  dev = fs_heap_zalloc(sizeof(*dev));
  if (dev == NULL)
    {
      return -ENOMEM;
    }

  nxmutex_init(&dev->lock);
  nxsem_init(&dev->reader, 0, 0);
  sq_init(&dev->pending);
  sq_init(&dev->processing);
  dev->crefs = 1;

  filep->f_priv = dev;
  return 0;
}

/****************************************************************************
 * Name: fuse_dev_unref
 *
 * Description:
 *   Drop a reference and free the device state when the last one is gone.
 *
 ****************************************************************************/

static void fuse_dev_unref(FAR struct fuse_dev_s *dev)
{
  bool destroy;

  nxmutex_lock(&dev->lock);
  destroy = (--dev->crefs == 0);
  nxmutex_unlock(&dev->lock);

  if (destroy)
    {
      nxsem_destroy(&dev->reader);
      nxmutex_destroy(&dev->lock);
      fs_heap_free(dev);
    }
}

/****************************************************************************
 * Name: fuse_dev_close
 ****************************************************************************/

static int fuse_dev_close(FAR struct file *filep)
{
  FAR struct fuse_dev_s *dev = filep->f_priv;

  /* The server is gone: abort in-flight requests and wake any reader */

  fuse_dev_abort(dev);
  fuse_dev_unref(dev);

  filep->f_priv = NULL;
  return 0;
}

/****************************************************************************
 * Name: fuse_dev_readv
 *
 * Description:
 *   The server reads one FUSE request (fuse_in_header followed by the
 *   request arguments) per call.
 *
 ****************************************************************************/

static ssize_t fuse_dev_readv(FAR struct file *filep, FAR struct uio *uio)
{
  FAR struct fuse_dev_s *dev = filep->f_priv;
  FAR struct fuse_req_s *req;
  FAR sq_entry_t *entry;
  bool oneway = false;
  size_t resid;
  size_t i;
  int ret;

  for (; ; )
    {
      nxmutex_lock(&dev->lock);
      if (dev->conn == FUSE_DEV_DEAD)
        {
          nxmutex_unlock(&dev->lock);
          return -ENODEV;
        }

      entry = sq_remfirst(&dev->pending);
      if (entry != NULL)
        {
          req = container_of(entry, struct fuse_req_s, entry);
          if (uio->uio_resid >= req->hin.len)
            {
              if (req->hin.unique)
                {
                  sq_addlast(&req->entry, &dev->processing);
                }
              else
                {
                  oneway = true;
                }

              nxmutex_unlock(&dev->lock);
              break;
            }

          /* Fail the request like Linux does and tell the server that its
           * read buffer is too small.
           */

          nxmutex_unlock(&dev->lock);
          req->hout.error = -EIO;
          req->done(req);
          continue;
        }

      nxmutex_unlock(&dev->lock);

      if ((filep->f_oflags & O_NONBLOCK) != 0)
        {
          return -EAGAIN;
        }

      ret = nxsem_wait(&dev->reader);
      if (ret < 0)
        {
          return ret;
        }
    }

  resid = uio->uio_resid;
  uio_copyfrom(uio, 0, &req->hin, sizeof(req->hin));
  uio_advance(uio, sizeof(req->hin));

  for (i = 0; i < req->vin_cnt; i++)
    {
      uio_copyfrom(uio, 0, req->vin[i].iov_base, req->vin[i].iov_len);
      uio_advance(uio, req->vin[i].iov_len);
    }

  if (oneway)
    {
      req->done(req);
    }

  return resid - uio->uio_resid;
}

/****************************************************************************
 * Name: fuse_dev_writev
 *
 * Description:
 *   The server writes one FUSE reply (fuse_out_header followed by the reply
 *   payload) per call.  The matching request is found by unique id.
 *
 ****************************************************************************/

static ssize_t fuse_dev_writev(FAR struct file *filep, FAR struct uio *uio)
{
  FAR struct fuse_dev_s *dev = filep->f_priv;
  FAR struct fuse_req_s *req = NULL;
  struct fuse_out_header hout;
  FAR sq_entry_t *entry;
  size_t datalen;
  size_t resid;
  size_t i;

  resid = uio->uio_resid;
  if (resid < sizeof(hout))
    {
      return -EINVAL;
    }

  uio_copyto(uio, 0, &hout, sizeof(hout));
  uio_advance(uio, sizeof(hout));

  datalen = hout.len - sizeof(hout);
  if (hout.len < sizeof(hout) || datalen > uio->uio_resid)
    {
      return -EINVAL;
    }

  nxmutex_lock(&dev->lock);

  if (hout.unique == 0)
    {
      int ret;

      if (dev->conn == NULL || dev->conn == FUSE_DEV_DEAD)
        {
          nxmutex_unlock(&dev->lock);
          return -ENOTCONN;
        }

      ret = fuse_notify(dev->conn, &hout, uio);
      nxmutex_unlock(&dev->lock);
      if (ret < 0)
        {
          return ret;
        }

      return resid - uio->uio_resid;
    }

  /* Find and detach the matching in-flight request */

  sq_for_every(&dev->processing, entry)
    {
      FAR struct fuse_req_s *tmp = container_of(entry, struct fuse_req_s,
                                                entry);

      if (tmp->hin.unique == hout.unique)
        {
          sq_rem(&tmp->entry, &dev->processing);
          req = tmp;
          break;
        }
    }

  nxmutex_unlock(&dev->lock);

  if (req == NULL)
    {
      return -ENOENT;
    }

  memcpy(&req->hout, &hout, sizeof(hout));

  /* Scatter the reply payload into the request reply buffers */

  for (i = 0; i < req->vout_cnt && datalen > 0; i++)
    {
      size_t chunk = MIN(req->vout[i].iov_len, datalen);

      uio_copyto(uio, 0, req->vout[i].iov_base, chunk);
      uio_advance(uio, chunk);
      datalen -= chunk;
    }

  req->done(req);
  return resid - uio->uio_resid;
}

/****************************************************************************
 * Name: fuse_dev_poll
 ****************************************************************************/

static int fuse_dev_poll(FAR struct file *filep, FAR struct pollfd *fds,
                         bool setup)
{
  FAR struct fuse_dev_s *dev = filep->f_priv;
  pollevent_t eventset;
  int ret = 0;
  int i;

  nxmutex_lock(&dev->lock);

  if (setup)
    {
      for (i = 0; i < FUSE_DEV_NPOLLWAITERS; i++)
        {
          if (dev->fds[i] == NULL)
            {
              dev->fds[i] = fds;
              fds->priv   = &dev->fds[i];
              break;
            }
        }

      if (i >= FUSE_DEV_NPOLLWAITERS)
        {
          ret = -EBUSY;
          goto errout;
        }

      eventset = POLLOUT;
      if (!sq_empty(&dev->pending))
        {
          eventset |= POLLIN;
        }

      if (dev->conn == FUSE_DEV_DEAD)
        {
          eventset |= POLLHUP;
        }

      poll_notify(&fds, 1, eventset);
    }
  else if (fds->priv != NULL)
    {
      FAR struct pollfd **slot = fds->priv;

      *slot     = NULL;
      fds->priv = NULL;
    }

errout:
  nxmutex_unlock(&dev->lock);
  return ret;
}

/****************************************************************************
 * Name: fuse_dev_request
 ****************************************************************************/

static int fuse_dev_request(FAR struct fuse_conn_s *conn,
                            FAR struct fuse_req_s *req)
{
  FAR struct fuse_dev_s *dev = conn->priv;

  nxmutex_lock(&dev->lock);

  /* Refuse unless this connection is still the one attached here */

  if (dev->conn != conn)
    {
      nxmutex_unlock(&dev->lock);
      return -ENOTCONN;
    }

  sq_addlast(&req->entry, &dev->pending);
  fuse_dev_notify(dev, POLLIN);
  nxmutex_unlock(&dev->lock);

  return 0;
}

/****************************************************************************
 * Name: fuse_dev_destroy
 ****************************************************************************/

static void fuse_dev_destroy(FAR struct fuse_conn_s *conn)
{
  FAR struct fuse_dev_s *dev = conn->priv;

  /* The mount is gone: disconnect, fail any in-flight request and wake a
   * blocked server so its read loop can exit.
   */

  fuse_dev_abort(dev);
  fuse_dev_unref(dev);

  conn->priv = NULL;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: fuse_dev_connect
 *
 * Description:
 *   Attach a connection to an open /dev/fuse instance identified by the
 *   "fd=<n>" mount option.
 *
 ****************************************************************************/

int fuse_dev_connect(FAR struct fuse_conn_s *conn, FAR const char *options)
{
  FAR struct fuse_dev_s *dev;
  FAR struct file *filep;
  FAR const char *opt;
  int ret;

  opt = strstr(options, "fd=");
  if (opt == NULL)
    {
      return -EINVAL;
    }

  ret = file_get(atoi(opt + 3), &filep);
  if (ret < 0)
    {
      return ret;
    }

  /* The fd must refer to a /dev/fuse instance */

  if (filep->f_priv == NULL || filep->f_inode == NULL ||
      filep->f_inode->u.i_ops != &g_fuse_dev_fops)
    {
      file_put(filep);
      return -EINVAL;
    }

  dev = filep->f_priv;
  nxmutex_lock(&dev->lock);

  /* Only a fresh, never attached instance accepts a connection */

  if (dev->conn != NULL)
    {
      nxmutex_unlock(&dev->lock);
      file_put(filep);
      return -EBUSY;
    }

  conn->priv      = dev;
  conn->transport = &g_fuse_dev_transport;
  dev->conn       = conn;
  dev->crefs++;

  nxmutex_unlock(&dev->lock);
  file_put(filep);

  return 0;
}

/****************************************************************************
 * Name: fuse_dev_initialize
 *
 * Description:
 *   Register the /dev/fuse character device.
 *
 ****************************************************************************/

int fuse_dev_initialize(void)
{
  return register_driver("/dev/fuse", &g_fuse_dev_fops, 0600, NULL);
}
