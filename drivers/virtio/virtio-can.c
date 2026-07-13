/****************************************************************************
 * drivers/virtio/virtio-can.c
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

#include <assert.h>
#include <debug.h>
#include <errno.h>
#include <string.h>
#include <sys/endian.h>
#include <sys/param.h>

#include <nuttx/can.h>
#include <nuttx/kmalloc.h>
#include <nuttx/mm/iob.h>
#include <nuttx/nuttx.h>
#include <nuttx/percpu.h>
#include <nuttx/net/can.h>
#include <nuttx/net/netdev_lowerhalf.h>
#include <nuttx/semaphore.h>
#include <nuttx/spinlock.h>
#include <nuttx/virtio/virtio.h>
#include <nuttx/virtio/virtio-can.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define VIRTIO_CAN_TXQ    0
#define VIRTIO_CAN_RXQ    1
#define VIRTIO_CAN_CTLQ   2
#define VIRTIO_CAN_NUM_VQ 3

/* The sdu payload sits at the same offset in a classic and an FD frame, so
 * the device can write it straight into the netpkt without knowing which.
 */

#define VIRTIO_CAN_DOFF   offsetof(struct canfd_frame, data)

/* Frame buffer: L2 guard followed by the largest SocketCAN frame.  The
 * stack reads and writes it in place; the wire header lives at the tail of
 * the guard and the guard's first byte, never covered by the header, holds
 * the TX result.
 */

#define VIRTIO_CAN_FRAMESZ \
  (CONFIG_NET_LL_GUARDSIZE + sizeof(struct canfd_frame))

/* Wire message offset placing the sdu on the SocketCAN data. */

#define VIRTIO_CAN_HDRLEN  offsetof(struct virtio_can_rx_s, sdu)
#define VIRTIO_CAN_WOFF \
  (CONFIG_NET_LL_GUARDSIZE + VIRTIO_CAN_DOFF - VIRTIO_CAN_HDRLEN)

static_assert(offsetof(struct virtio_can_tx_out_s, sdu) ==
              offsetof(struct virtio_can_rx_s, sdu),
              "tx and rx wire headers must match");
static_assert(CONFIG_NET_LL_GUARDSIZE + VIRTIO_CAN_DOFF >
              VIRTIO_CAN_HDRLEN,
              "the wire header and the TX result byte must fit in front "
              "of the sdu");

/****************************************************************************
 * Private Types
 ****************************************************************************/

/* TX slot; pkt == NULL marks it free. */

struct virtio_can_txbuf_s
{
  FAR netpkt_t *pkt;
  FAR void     *paymap;
  uint16_t      wirelen;
};

struct virtio_can_priv_s
{
  struct netdev_lowerhalf_s dev;
  FAR struct virtio_device *vdev;
  bool                      rtr;  /* VIRTIO_CAN_F_RTR_FRAMES negotiated */
  spinlock_t                vqlock[VIRTIO_CAN_NUM_VQ];

  /* All TX and RX ring resources are sized from the virtqueues and
   * allocated once at probe time.
   */

  FAR struct virtio_can_txbuf_s  *txbuf;
  FAR uint8_t                   **rxfb;   /* The RX frame buffers */
  uint16_t                        txnum;
  uint16_t                        rxnum;
};

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static int  virtio_can_ifup(FAR struct netdev_lowerhalf_s *dev);
static int  virtio_can_ifdown(FAR struct netdev_lowerhalf_s *dev);
static int  virtio_can_transmit(FAR struct netdev_lowerhalf_s *dev,
                                FAR netpkt_t *pkt);
static FAR netpkt_t *virtio_can_receive(FAR struct netdev_lowerhalf_s *dev);
static FAR netpkt_t *virtio_can_alloc(FAR struct netdev_lowerhalf_s *dev,
                                      int priority, uint16_t size,
                                      bool throttled, unsigned int timeout);

static void virtio_can_free_bufs(FAR struct virtio_can_priv_s *priv);
static int  virtio_can_probe(FAR struct virtio_device *vdev);
static void virtio_can_remove(FAR struct virtio_device *vdev);

/****************************************************************************
 * Private Data
 ****************************************************************************/

static DEFINE_PER_CPU_BMP(struct virtio_driver, g_virtio_can_driver) =
{
  VIRTIO_ID_CAN,     /* Device id */
  virtio_can_probe,  /* Probe */
  virtio_can_remove, /* Remove */
};
#define g_virtio_can_driver this_cpu_var_bmp(g_virtio_can_driver)

static const struct netdev_ops_s g_virtio_can_ops =
{
  .ifup     = virtio_can_ifup,
  .ifdown   = virtio_can_ifdown,
  .transmit = virtio_can_transmit,
  .receive  = virtio_can_receive,
  .alloc    = virtio_can_alloc,
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: virtio_can_frame2wire
 *
 * Description:
 *   Encode a SocketCAN frame from a netpkt into a VirtIO CAN tx request.
 *   Returns NULL when the frame cannot be represented on the wire.
 *
 ****************************************************************************/

static size_t virtio_can_frame2wire(FAR struct virtio_can_priv_s *priv,
                                    FAR uint8_t *data, size_t len)
{
  FAR struct virtio_can_tx_out_s *out;
  uint32_t flags = 0;
  uint32_t canid;
  uint8_t  dlen;

  if (len >= sizeof(struct canfd_frame))
    {
      FAR struct canfd_frame *cfd = (FAR struct canfd_frame *)data;

      canid = cfd->can_id;
      dlen  = MIN(cfd->len, CANFD_MAX_DLEN);
      flags = VIRTIO_CAN_FLAGS_FD;
    }
  else
    {
      FAR struct can_frame *cf = (FAR struct can_frame *)data;

      canid = cf->can_id;
      dlen  = MIN(cf->can_dlc, CAN_MAX_DLEN);
    }

  /* The wire format has no error frame encoding, and a remote frame needs
   * the RTR feature.  Neither can be sent as a plain data frame.
   */

  if ((canid & CAN_ERR_FLAG) != 0 ||
      ((canid & CAN_RTR_FLAG) != 0 && !priv->rtr))
    {
      return 0;
    }

  if (canid & CAN_EFF_FLAG)
    {
      flags |= VIRTIO_CAN_FLAGS_EXTENDED;
    }

  if (canid & CAN_RTR_FLAG)
    {
      flags |= VIRTIO_CAN_FLAGS_RTR;
    }

  /* Overwrites the SocketCAN header; the sdu is untouched. */

  out = (FAR struct virtio_can_tx_out_s *)
        (data + VIRTIO_CAN_DOFF - VIRTIO_CAN_HDRLEN);
  memset(out, 0, VIRTIO_CAN_HDRLEN);
  out->msg_type = htole16(VIRTIO_CAN_TX);
  out->length   = htole16(dlen);
  out->flags    = htole32(flags);
  out->can_id   = htole32(canid & CAN_EFF_MASK);

  return VIRTIO_CAN_HDRLEN + dlen;
}

/****************************************************************************
 * Name: virtio_can_wire2frame
 *
 * Description:
 *   Decode a VirtIO CAN rx message into a SocketCAN frame.
 *
 ****************************************************************************/

static size_t virtio_can_wire2frame(FAR const struct virtio_can_rx_s *rx,
                                    FAR void *data)
{
  uint32_t flags = le32toh(rx->flags);
  uint32_t canid = le32toh(rx->can_id);

  if (flags & VIRTIO_CAN_FLAGS_EXTENDED)
    {
      canid |= CAN_EFF_FLAG;
    }

  if (flags & VIRTIO_CAN_FLAGS_RTR)
    {
      canid |= CAN_RTR_FLAG;
    }

  /* Fill only the frame header; the sdu already sits at the payload
   * offset where the device wrote it, so it must not be touched here.
   */

  if (flags & VIRTIO_CAN_FLAGS_FD)
    {
      FAR struct canfd_frame *cfd = data;

      cfd->can_id = canid;
      cfd->len    = MIN(le16toh(rx->length), CANFD_MAX_DLEN);
      cfd->flags  = 0;
      cfd->__res0 = 0;
      cfd->__res1 = 0;
      return sizeof(struct canfd_frame);
    }
  else
    {
      FAR struct can_frame *cf = data;

      cf->can_id  = canid;
      cf->can_dlc = MIN(le16toh(rx->length), CAN_MAX_DLEN);
      cf->__pad   = 0;
      cf->__res0  = 0;
      cf->__res1  = 0;
      return sizeof(struct can_frame);
    }
}

/****************************************************************************
 * Name: virtio_can_tx_free
 *
 * Description:
 *   Release a TX frame buffer.
 *
 ****************************************************************************/

static void virtio_can_tx_free(FAR void *arg, FAR void *data)
{
  FAR struct virtio_can_priv_s *priv = arg;

  virtio_free_buf(priv->vdev, data);
}

/****************************************************************************
 * Name: virtio_can_iobnext
 *
 * Description:
 *   Grow a TX chain from the device buffers; keeps io_arg as priv.
 *
 ****************************************************************************/

static FAR struct iob_s *virtio_can_iobnext(FAR void *arg, uint16_t size,
                                            bool throttled,
                                            unsigned int timeout)
{
  FAR struct virtio_can_priv_s *priv = arg;

  return (FAR struct iob_s *)virtio_can_alloc(&priv->dev, 0, size,
                                              throttled, timeout);
}

/****************************************************************************
 * Name: virtio_can_alloc
 *
 * Description:
 *   Supply a device-visible TX buffer; NULL falls back to the pool.
 *
 ****************************************************************************/

static FAR netpkt_t *virtio_can_alloc(FAR struct netdev_lowerhalf_s *dev,
                                      int priority, uint16_t size,
                                      bool throttled, unsigned int timeout)
{
  FAR struct virtio_can_priv_s *priv = (FAR struct virtio_can_priv_s *)dev;
  FAR struct iob_s *iob;
  FAR uint8_t *fb;

  if (size > VIRTIO_CAN_FRAMESZ)
    {
      return NULL;
    }

  fb = virtio_zalloc_buf(priv->vdev, VIRTIO_CAN_FRAMESZ, 16);
  if (fb == NULL)
    {
      return NULL;
    }

  iob = iob_alloc_with_data(fb, VIRTIO_CAN_FRAMESZ, virtio_can_iobnext,
                            virtio_can_tx_free, priv);
  if (iob == NULL)
    {
      virtio_free_buf(priv->vdev, fb);
      return NULL;
    }

  return (FAR netpkt_t *)iob;
}

/****************************************************************************
 * Name: virtio_can_free_bufs
 ****************************************************************************/

static void virtio_can_free_bufs(FAR struct virtio_can_priv_s *priv)
{
  FAR struct virtio_device *vdev = priv->vdev;
  int i;

  for (i = 0; priv->txbuf != NULL && i < priv->txnum; i++)
    {
      FAR struct virtio_can_txbuf_s *buf = &priv->txbuf[i];

      if (buf->pkt != NULL)
        {
          netpkt_free(&priv->dev, buf->pkt, NETPKT_TX);
          buf->pkt = NULL;
        }
    }

  for (i = 0; priv->rxfb != NULL && i < priv->rxnum; i++)
    {
      if (priv->rxfb[i] != NULL)
        {
          virtio_free_buf(vdev, priv->rxfb[i]);
          priv->rxfb[i] = NULL;
        }
    }

  kmm_free(priv->txbuf);
  kmm_free(priv->rxfb);
  priv->txbuf = NULL;
  priv->rxfb  = NULL;
}

/****************************************************************************
 * Name: virtio_can_rx_post
 *
 * Description:
 *   Post one frame buffer; never more buffers than ring entries.
 *
 ****************************************************************************/

static void virtio_can_rx_post(FAR struct virtio_can_priv_s *priv,
                               FAR uint8_t *fb)
{
  FAR struct virtqueue *vq = priv->vdev->vrings_info[VIRTIO_CAN_RXQ].vq;
  FAR spinlock_t *lock = &priv->vqlock[VIRTIO_CAN_RXQ];
  struct virtqueue_buf vb[1];

  vb[0].buf = fb + VIRTIO_CAN_WOFF;
  vb[0].len = VIRTIO_CAN_HDRLEN + CANFD_MAX_DLEN;

  DEBUGVERIFY(virtqueue_add_buffer_lock(vq, vb, 0, 1, fb, lock));
  virtqueue_kick_lock(vq, lock);
}

/****************************************************************************
 * Name: virtio_can_rx_free
 *
 * Description:
 *   Repost a released RX frame buffer.
 *
 ****************************************************************************/

static void virtio_can_rx_free(FAR void *arg, FAR void *data)
{
  virtio_can_rx_post(arg, data);
}

/****************************************************************************
 * Name: virtio_can_rx_ready
 ****************************************************************************/

static void virtio_can_rx_ready(FAR struct virtqueue *vq)
{
  FAR struct virtio_can_priv_s *priv = vq->vq_dev->priv;

  virtqueue_disable_cb_lock(vq, &priv->vqlock[VIRTIO_CAN_RXQ]);
  netdev_lower_rxready(&priv->dev);
}

/****************************************************************************
 * Name: virtio_can_tx_reap
 *
 * Description:
 *   Reap completed transmissions and free their slots.
 *
 ****************************************************************************/

static void virtio_can_tx_reap(FAR struct virtio_can_priv_s *priv)
{
  FAR struct virtqueue *vq = priv->vdev->vrings_info[VIRTIO_CAN_TXQ].vq;
  FAR spinlock_t *lock = &priv->vqlock[VIRTIO_CAN_TXQ];
  FAR struct virtio_can_txbuf_s *buf;
  FAR uint8_t *wire;
  FAR uint8_t *fb;
  irqstate_t flags;
  bool reaped = false;

  while ((buf = virtqueue_get_buffer_lock(vq, NULL, NULL, lock)) != NULL)
    {
      /* The device reports a rejected frame in the result byte. */

      fb = netpkt_getdata(&priv->dev, buf->pkt) - CONFIG_NET_LL_GUARDSIZE;
      if (fb[0] != VIRTIO_CAN_RESULT_OK)
        {
          NETDEV_TXERRORS(&priv->dev.netdev);
        }

      wire = fb + VIRTIO_CAN_WOFF;
      virtio_unmap_buf(priv->vdev, wire, buf->wirelen,
                       VIRTIO_DMA_TO_DEVICE, buf->paymap);
      netpkt_free(&priv->dev, buf->pkt, NETPKT_TX);

      flags = spin_lock_irqsave(lock);
      buf->pkt = NULL;
      spin_unlock_irqrestore(lock, flags);
      reaped = true;
    }

  if (reaped)
    {
      netdev_lower_txdone(&priv->dev);
    }
}

/****************************************************************************
 * Name: virtio_can_tx_done
 ****************************************************************************/

static void virtio_can_tx_done(FAR struct virtqueue *vq)
{
  FAR struct virtio_can_priv_s *priv = vq->vq_dev->priv;

  /* Depending on the transport this callback may run in interrupt
   * context, where reaping is not possible: returning a slot frees its
   * payload mapping, and the mapping heap must not be entered from an
   * interrupt that may have preempted an allocation on that same heap.
   * Silence the doorbell and wake the netdev worker; its work pass
   * enters receive() first, which starts with a reap.
   */

  virtqueue_disable_cb_lock(vq, &priv->vqlock[VIRTIO_CAN_TXQ]);
  netdev_lower_txdone(&priv->dev);
}

/****************************************************************************
 * Name: virtio_can_ctl_done
 ****************************************************************************/

static void virtio_can_ctl_done(FAR struct virtqueue *vq)
{
  FAR struct virtio_can_priv_s *priv = vq->vq_dev->priv;
  FAR sem_t *sem;

  while ((sem = virtqueue_get_buffer_lock(vq, NULL, NULL,
                &priv->vqlock[VIRTIO_CAN_CTLQ])) != NULL)
    {
      nxsem_post(sem);
    }
}

/****************************************************************************
 * Name: virtio_can_control
 ****************************************************************************/

static int virtio_can_control(FAR struct virtio_can_priv_s *priv,
                              uint16_t mode)
{
  FAR struct virtio_device *vdev = priv->vdev;
  FAR struct virtqueue *vq = vdev->vrings_info[VIRTIO_CAN_CTLQ].vq;
  struct virtio_can_control_out_s out;
  struct virtio_can_control_in_s in;
  struct virtqueue_buf vb[2];
  FAR struct virtio_can_control_in_s *res;
  FAR void *outmap;
  FAR void *inmap;
  sem_t sem;
  int ret;

  out.msg_type = htole16(mode);
  in.result    = VIRTIO_CAN_RESULT_NOT_OK;

  ret = virtio_map_buf(vdev, &out, sizeof(out), VIRTIO_DMA_TO_DEVICE,
                       &outmap);
  if (ret < 0)
    {
      return ret;
    }

  vb[0].buf = outmap;
  vb[0].len = sizeof(out);

  ret = virtio_map_buf(vdev, &in, sizeof(in), VIRTIO_DMA_FROM_DEVICE,
                       &inmap);
  if (ret < 0)
    {
      goto out_out;
    }

  res = inmap;
  vb[1].buf = inmap;
  vb[1].len = sizeof(in);

  nxsem_init(&sem, 0, 0);

  ret = virtqueue_add_buffer_lock(vq, vb, 1, 1, &sem,
                                  &priv->vqlock[VIRTIO_CAN_CTLQ]);
  if (ret >= 0)
    {
      virtqueue_kick_lock(vq, &priv->vqlock[VIRTIO_CAN_CTLQ]);
      nxsem_wait_uninterruptible(&sem);
      if (res->result != VIRTIO_CAN_RESULT_OK)
        {
          ret = -EIO;
        }
    }

  nxsem_destroy(&sem);
  virtio_unmap_buf(vdev, &in, sizeof(in), VIRTIO_DMA_FROM_DEVICE, inmap);
out_out:
  virtio_unmap_buf(vdev, &out, sizeof(out), VIRTIO_DMA_TO_DEVICE, outmap);
  return ret;
}

/****************************************************************************
 * Name: virtio_can_transmit
 *
 * Description:
 *   Queue one frame as a header/payload/result chain.  The ring share
 *   sized at probe time bounds the frames in flight at once.
 *
 ****************************************************************************/

static int virtio_can_transmit(FAR struct netdev_lowerhalf_s *dev,
                               FAR netpkt_t *pkt)
{
  FAR struct virtio_can_priv_s *priv = (FAR struct virtio_can_priv_s *)dev;
  FAR struct virtqueue *vq = priv->vdev->vrings_info[VIRTIO_CAN_TXQ].vq;
  FAR spinlock_t *lock = &priv->vqlock[VIRTIO_CAN_TXQ];
  FAR struct virtio_can_txbuf_s *buf = NULL;
  struct virtqueue_buf vb[2];
  FAR uint8_t *wire;
  FAR uint8_t *fb;
  size_t wirelen;
  irqstate_t flags;
  bool retried = false;
  int ret;
  int i;

retry:
  virtio_can_tx_reap(priv);

  flags = spin_lock_irqsave(lock);
  for (i = 0; i < priv->txnum; i++)
    {
      if (priv->txbuf[i].pkt == NULL)
        {
          buf = &priv->txbuf[i];
          buf->pkt = pkt;
          break;
        }
    }

  spin_unlock_irqrestore(lock, flags);
  if (buf == NULL)
    {
      /* Every slot is in flight.  Arm the completion callback so that
       * txdone wakes the upper half, which pauses on -EBUSY and keeps the
       * frame at the head of its queue.  A completion that raced the
       * arming is reaped here and the slot search runs once more.
       */

      if (virtqueue_enable_cb_lock(vq, lock) != 0 && !retried)
        {
          retried = true;
          goto retry;
        }

      return -EBUSY;
    }

  /* Build the wire header in the L2 guard; the map bounces only a
   * frame from the buffer pool.
   */

  wirelen = virtio_can_frame2wire(priv, netpkt_getdata(dev, pkt),
                                  netpkt_getdatalen(dev, pkt));
  if (wirelen == 0)
    {
      ret = -EOPNOTSUPP;
      goto err;
    }

  fb = netpkt_getdata(dev, pkt) - CONFIG_NET_LL_GUARDSIZE;
  wire = fb + VIRTIO_CAN_WOFF;
  ret = virtio_map_buf(priv->vdev, wire, wirelen, VIRTIO_DMA_TO_DEVICE,
                       &buf->paymap);
  if (ret < 0)
    {
      goto err;
    }

  buf->wirelen = wirelen;
  fb[0] = VIRTIO_CAN_RESULT_NOT_OK;

  vb[0].buf = buf->paymap;
  vb[0].len = wirelen;
  vb[1].buf = fb;
  vb[1].len = sizeof(fb[0]);

  ret = virtqueue_add_buffer_lock(vq, vb, 1, 1, buf, lock);
  if (ret < 0)
    {
      virtio_unmap_buf(priv->vdev, wire, wirelen, VIRTIO_DMA_TO_DEVICE,
                       buf->paymap);
      goto err;
    }

  virtqueue_kick_lock(vq, lock);
  return OK;

err:
  flags = spin_lock_irqsave(lock);
  buf->pkt = NULL;
  spin_unlock_irqrestore(lock, flags);
  return ret;
}

/****************************************************************************
 * Name: virtio_can_receive
 ****************************************************************************/

static FAR netpkt_t *virtio_can_receive(FAR struct netdev_lowerhalf_s *dev)
{
  FAR struct virtio_can_priv_s *priv = (FAR struct virtio_can_priv_s *)dev;
  FAR struct virtqueue *vq = priv->vdev->vrings_info[VIRTIO_CAN_RXQ].vq;
  FAR spinlock_t *lock = &priv->vqlock[VIRTIO_CAN_RXQ];
  FAR struct virtio_can_rx_s *hdr;
  FAR struct iob_s *iob;
  FAR uint8_t *fb;
  size_t framelen;

  /* Reap TX in thread context on behalf of the completion callback. */

  virtio_can_tx_reap(priv);

  for (; ; )
    {
      fb = virtqueue_get_buffer_lock(vq, NULL, NULL, lock);
      if (fb == NULL)
        {
          /* On empty the callback is re-armed and the queue checked once
           * more: a frame that landed in the window would otherwise wait
           * for a doorbell that has already been rung.
           */

          if (virtqueue_enable_cb_lock(vq, lock) == 0)
            {
              return NULL;
            }

          fb = virtqueue_get_buffer_lock(vq, NULL, NULL, lock);
          if (fb == NULL)
            {
              return NULL;
            }

          virtqueue_disable_cb_lock(vq, lock);
        }

      /* Rewrite the wire header as the SocketCAN one, in place. */

      hdr = (FAR struct virtio_can_rx_s *)(fb + VIRTIO_CAN_WOFF);
      if (le16toh(hdr->msg_type) != VIRTIO_CAN_RX)
        {
          virtio_can_rx_post(priv, fb);
          NETDEV_RXERRORS(&priv->dev.netdev);
          continue;
        }

      framelen = virtio_can_wire2frame(hdr, (FAR struct canfd_frame *)
                                       (fb + CONFIG_NET_LL_GUARDSIZE));

      iob = iob_alloc_with_data(fb, VIRTIO_CAN_FRAMESZ, NULL,
                                virtio_can_rx_free, priv);
      if (iob == NULL)
        {
          virtio_can_rx_post(priv, fb);
          NETDEV_RXDROPPED(&priv->dev.netdev);
          continue;
        }

      iob_reserve(iob, CONFIG_NET_LL_GUARDSIZE);
      iob->io_len    = framelen;
      iob->io_pktlen = framelen;
      return (FAR netpkt_t *)iob;
    }
}

/****************************************************************************
 * Name: virtio_can_ifup
 ****************************************************************************/

static int virtio_can_ifup(FAR struct netdev_lowerhalf_s *dev)
{
  FAR struct virtio_can_priv_s *priv = (FAR struct virtio_can_priv_s *)dev;
  uint16_t status;
  int ret;

  virtio_read_config_member(priv->vdev, struct virtio_can_config_s,
                            status, &status);
  if ((le16toh(status) & VIRTIO_CAN_S_CTRL_BUSOFF) != 0)
    {
      return -ENETDOWN;
    }

  ret = virtio_can_control(priv, VIRTIO_CAN_SET_CTRL_MODE_START);
  if (ret >= 0)
    {
      netdev_lower_carrier_on(dev);
    }

  return ret;
}

/****************************************************************************
 * Name: virtio_can_ifdown
 ****************************************************************************/

static int virtio_can_ifdown(FAR struct netdev_lowerhalf_s *dev)
{
  FAR struct virtio_can_priv_s *priv = (FAR struct virtio_can_priv_s *)dev;

  netdev_lower_carrier_off(dev);
  return virtio_can_control(priv, VIRTIO_CAN_SET_CTRL_MODE_STOP);
}

/****************************************************************************
 * Name: virtio_can_probe
 ****************************************************************************/

static int virtio_can_probe(FAR struct virtio_device *vdev)
{
  FAR struct virtio_can_priv_s *priv;
  FAR const char *vqnames[VIRTIO_CAN_NUM_VQ];
  vq_callback callback[VIRTIO_CAN_NUM_VQ];
  int ret;
  int i;

  priv = kmm_zalloc(sizeof(*priv));
  if (priv == NULL)
    {
      return -ENOMEM;
    }

  priv->vdev = vdev;
  vdev->priv = priv;
  for (i = 0; i < VIRTIO_CAN_NUM_VQ; i++)
    {
      spin_lock_init(&priv->vqlock[i]);
    }

  virtio_set_status(vdev, VIRTIO_CONFIG_STATUS_DRIVER);
  virtio_negotiate_features(vdev,
                            (1UL << VIRTIO_CAN_F_CAN_CLASSIC) |
                            (1UL << VIRTIO_CAN_F_CAN_FD) |
                            (1UL << VIRTIO_CAN_F_RTR_FRAMES), NULL);
  virtio_set_status(vdev, VIRTIO_CONFIG_FEATURES_OK);
  priv->rtr = virtio_has_feature(vdev, VIRTIO_CAN_F_RTR_FRAMES);

  /* The spec requires at least one frame type to be negotiated. */

  if (!virtio_has_feature(vdev, VIRTIO_CAN_F_CAN_CLASSIC) &&
      !virtio_has_feature(vdev, VIRTIO_CAN_F_CAN_FD))
    {
      vrterr("No CAN frame type negotiated\n");
      ret = -ENODEV;
      goto err_with_priv;
    }

  vqnames[VIRTIO_CAN_TXQ]  = "virtio_can_tx";
  vqnames[VIRTIO_CAN_RXQ]  = "virtio_can_rx";
  vqnames[VIRTIO_CAN_CTLQ] = "virtio_can_ctl";
  callback[VIRTIO_CAN_TXQ]  = virtio_can_tx_done;
  callback[VIRTIO_CAN_RXQ]  = virtio_can_rx_ready;
  callback[VIRTIO_CAN_CTLQ] = virtio_can_ctl_done;

  ret = virtio_create_virtqueues(vdev, 0, VIRTIO_CAN_NUM_VQ, vqnames,
                                 callback, NULL);
  if (ret < 0)
    {
      goto err_with_priv;
    }

  /* TX takes two descriptors per frame, RX one. */

  priv->txnum = vdev->vrings_info[VIRTIO_CAN_TXQ].vq->vq_nentries / 2;
  priv->rxnum = vdev->vrings_info[VIRTIO_CAN_RXQ].vq->vq_nentries;

  priv->txbuf = kmm_zalloc(priv->txnum * sizeof(*priv->txbuf));
  priv->rxfb  = kmm_zalloc(priv->rxnum * sizeof(*priv->rxfb));
  if (priv->txbuf == NULL || priv->rxfb == NULL)
    {
      ret = -ENOMEM;
      goto err_with_vq;
    }

  for (i = 0; i < priv->rxnum; i++)
    {
      priv->rxfb[i] = virtio_zalloc_buf(vdev, VIRTIO_CAN_FRAMESZ, 16);
      if (priv->rxfb[i] == NULL)
        {
          ret = -ENOMEM;
          goto err_with_vq;
        }
    }

  virtio_set_status(vdev, VIRTIO_CONFIG_STATUS_DRIVER_OK);

  priv->dev.ops = &g_virtio_can_ops;
  priv->dev.rxtype = NETDEV_RX_THREAD;
  priv->dev.priority = CONFIG_DRIVERS_VIRTIO_CAN_RXTHREAD_PRIORITY;

  ret = netdev_lower_register(&priv->dev, NET_LL_CAN);
  if (ret < 0)
    {
      goto err_with_vq;
    }

  /* Arm the callbacks after registration.  The TX callback stays disabled
   * until transmit() runs out of slots.
   */

  virtqueue_enable_cb_lock(vdev->vrings_info[VIRTIO_CAN_RXQ].vq,
                           &priv->vqlock[VIRTIO_CAN_RXQ]);
  virtqueue_enable_cb_lock(vdev->vrings_info[VIRTIO_CAN_CTLQ].vq,
                           &priv->vqlock[VIRTIO_CAN_CTLQ]);

  /* Prime the RX ring. */

  for (i = 0; i < priv->rxnum; i++)
    {
      virtio_can_rx_post(priv, priv->rxfb[i]);
    }

  return OK;

err_with_vq:
  virtio_reset_device(vdev);
  virtio_delete_virtqueues(vdev);

err_with_priv:
  virtio_can_free_bufs(priv);
  kmm_free(priv);
  return ret;
}

/****************************************************************************
 * Name: virtio_can_remove
 ****************************************************************************/

static void virtio_can_remove(FAR struct virtio_device *vdev)
{
  FAR struct virtio_can_priv_s *priv = vdev->priv;

  netdev_lower_unregister(&priv->dev);
  virtio_reset_device(vdev);
  virtio_delete_virtqueues(vdev);
  virtio_can_free_bufs(priv);
  kmm_free(priv);
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: virtio_register_can_driver
 ****************************************************************************/

int virtio_register_can_driver(void)
{
  return virtio_register_driver(&g_virtio_can_driver);
}
