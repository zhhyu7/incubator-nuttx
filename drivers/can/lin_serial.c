/****************************************************************************
 * drivers/can/lin_serial.c
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

/* Generic LIN-over-tty netdev lower-half driver.
 *
 * This driver provides a SocketCAN compatible LIN interface ("lin%d") on
 * top of any NuttX serial device node ("/dev/ttySx").  It is media-agnostic:
 * the board/bringup logic supplies the serial device path, and the
 * driver handles framing, master/slave role, baud rate, and sleep/wakeup
 * through standard file operations and ioctl.
 */

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <debug.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <termios.h>

#include <nuttx/can/lin_serial.h>
#include <nuttx/fs/fs.h>
#include <nuttx/kmalloc.h>
#include <nuttx/lin.h>
#include <nuttx/net/netdev_lowerhalf.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* Max TX buffer: SYNC(1) + PID(1) + data(8) + checksum(1) = 11 */

#define LIN_SERIAL_FRAME_MAX      (2 + CAN_MAX_DLEN + 1)
#define LIN_SERIAL_SYNC_BYTE      0x55

/* RX reassembly states */

#define LIN_RX_IDLE               0   /* Idle, not receiving */
#define LIN_RX_WAIT_SYNC          1   /* Slave: waiting for SYNC byte */
#define LIN_RX_WAIT_PID           2   /* Slave: waiting for PID byte */
#define LIN_RX_COLLECTING         3   /* Master/Slave: collecting data */
#define LIN_RX_ECHO_SYNC          4   /* Master echo: expecting SYNC echo */
#define LIN_RX_ECHO_PID           5   /* Master echo: expecting PID echo */

/* LIN driver state helpers (atomic — no lock needed) */

#define LIN_SERIAL_SETSTOP(p)     atomic_set(&(p)->state, CAN_STATE_STOPPED)
#define LIN_SERIAL_SETNORMAL(p)   atomic_set(&(p)->state, CAN_STATE_OPERATIONAL)
#define LIN_SERIAL_SETBUSY(p)     atomic_set(&(p)->state, CAN_STATE_BUSY)
#define LIN_SERIAL_SETSPENDING(p) atomic_set(&(p)->state, CAN_STATE_SPENDING)
#define LIN_SERIAL_SETSLEEP(p)    atomic_set(&(p)->state, CAN_STATE_SLEEP)
#define LIN_SERIAL_ISUP(p)        (atomic_read(&(p)->state) != CAN_STATE_STOPPED)
#define LIN_SERIAL_ISNORMAL(p)    (atomic_read(&(p)->state) == CAN_STATE_OPERATIONAL)
#define LIN_SERIAL_ISBUSY(p)      (atomic_read(&(p)->state) == CAN_STATE_BUSY)
#define LIN_SERIAL_ISSLEEP(p)     (atomic_read(&(p)->state) == CAN_STATE_SLEEP)

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct lin_serial_dev_s
{
  /* Network device (must be first for casting) */

  struct netdev_lowerhalf_s dev;

  /* Backing serial device */

  struct file      filep;

  uint32_t         baud;
  bool             master;
  bool             echo;          /* true: UART echoes TX on RX (LIN bus) */

  /* Controller state */

  atomic_t         state;         /* LIN controller state (atomic) */

  /* RX reassembly (zero-copy into netpkt IOB) */

  FAR netpkt_t    *rx_pkt;        /* Pre-allocated pkt for current frame */
  uint16_t         rx_checksum;   /* Running checksum accumulator */
  uint8_t          rx_state;      /* Reassembly state machine */
  uint8_t          rx_pos;        /* Byte fill position in frame->data */

  /* RX frame queue */

  netpkt_queue_t   rx_queue;
  mutex_t          mutex;         /* Protects rx_queue and rx reassembly state */

  /* Work items and poll */

  struct pollfd    fds;
  struct work_s    rx_work;       /* RX deferred processing */
  struct work_s    idle_work;     /* Bus idle timeout → sleep */
};

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static int lin_serial_ifup(FAR struct netdev_lowerhalf_s *dev);
static int lin_serial_ifdown(FAR struct netdev_lowerhalf_s *dev);
static int lin_serial_transmit(FAR struct netdev_lowerhalf_s *dev,
                               FAR netpkt_t *pkt);
static FAR netpkt_t *lin_serial_receive(FAR struct netdev_lowerhalf_s *dev);

#ifdef CONFIG_NETDEV_IOCTL
static int lin_serial_ioctl(FAR struct netdev_lowerhalf_s *dev, int cmd,
                            unsigned long arg);
#endif /* CONFIG_NETDEV_IOCTL */

/****************************************************************************
 * Private Data
 ****************************************************************************/

static const struct netdev_ops_s g_lin_serial_ops =
{
  .ifup     = lin_serial_ifup,     /* ifup */
  .ifdown   = lin_serial_ifdown,   /* ifdown */
  .transmit = lin_serial_transmit, /* transmit */
  .receive  = lin_serial_receive,  /* receive */
#ifdef CONFIG_NETDEV_IOCTL
  .ioctl    = lin_serial_ioctl     /* ioctl */
#endif
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: lin_serial_pid_parity
 *
 * Description:
 *   Compute the two parity bits (P0, P1) for a 6-bit LIN identifier
 *   and return the protected PID (id | P0<<6 | P1<<7).
 *
 ****************************************************************************/

static inline_function uint8_t lin_serial_pid_parity(uint8_t id)
{
  uint8_t p0;
  uint8_t p1;

  id &= LIN_ID_MASK;
  p0 = ((id >> 0) ^ (id >> 1) ^ (id >> 2) ^ (id >> 4)) & 1u;
  p1 = ~((id >> 1) ^ (id >> 3) ^ (id >> 4) ^ (id >> 5)) & 1u;
  return id | (uint8_t)(p0 << 6) | (uint8_t)(p1 << 7);
}

/****************************************************************************
 * Name: lin_serial_checksum
 *
 * Description:
 *   Compute the LIN checksum over data bytes.  If enhanced is true, the
 *   protected PID byte is included in the calculation (LIN 2.x).  The
 *   caller must pass the full 8-bit protected PID (with parity bits),
 *   not the raw 6-bit ID.  Returns the inverted 8-bit checksum.
 *
 ****************************************************************************/

static uint8_t lin_serial_checksum(bool enhanced,
                                   uint8_t protected_pid,
                                   FAR const uint8_t *data,
                                   uint8_t len)
{
  uint16_t sum = 0;
  uint8_t i;

  /* Enhanced checksum includes the protected PID (with parity bits) */

  if (enhanced)
    {
      sum = protected_pid;
    }

  for (i = 0; i < len; i++)
    {
      sum += data[i];
      if (sum > 0xff)
        {
          sum -= 0xff;
        }
    }

  return (uint8_t)(~sum);
}

/****************************************************************************
 * Name: lin_serial_setbaud
 *
 * Description:
 *   Apply the requested baud rate to the backing tty via TCGETS/TCSETS.
 *   On a virtual serial port this is a harmless no-op; on a real UART it
 *   configures the hardware baud rate generator.
 *
 ****************************************************************************/

static int lin_serial_setbaud(FAR struct lin_serial_dev_s *priv,
                              uint32_t baud)
{
  struct termios term;
  int ret;

  if (baud == 0)
    {
      return OK;
    }

  ret = file_ioctl(&priv->filep, TCGETS, &term);
  if (ret < 0)
    {
      nwarn("%s: TCGETS failed: %d\n", priv->dev.netdev.d_ifname, ret);
      return ret;
    }

  cfsetspeed(&term, baud);

  ret = file_ioctl(&priv->filep, TCSETS, &term);
  if (ret < 0)
    {
      nwarn("%s: TCSETS failed: %d\n", priv->dev.netdev.d_ifname, ret);
      return ret;
    }

  priv->baud = baud;
  return OK;
}

/****************************************************************************
 * Name: lin_serial_enqueue_notify
 *
 * Description:
 *   Common helper: allocate a RX netpkt, copy the caller-prepared
 *   can_frame into it, enqueue to rx_queue, and signal upper-half.
 *   Used by report_event / report_error / report_tcf wrappers.
 *
 ****************************************************************************/

static int lin_serial_enqueue_notify(FAR struct lin_serial_dev_s *priv,
                                     FAR const struct can_frame *src)
{
  FAR netpkt_t *pkt;
  FAR struct can_frame *frame;

  pkt = netpkt_alloc(&priv->dev, NETPKT_RX);
  if (pkt == NULL)
    {
      return -ENOMEM;
    }

  frame = (FAR struct can_frame *)netpkt_getdata(&priv->dev, pkt);
  memcpy(frame, src, sizeof(struct can_frame));

  netpkt_setdatalen(&priv->dev, pkt, sizeof(struct can_frame));
  nxmutex_lock(&priv->mutex);
  netpkt_add_queue(pkt, &priv->rx_queue);
  nxmutex_unlock(&priv->mutex);
  netdev_lower_rxready(&priv->dev);
  return OK;
}

/****************************************************************************
 * Name: lin_serial_report_event
 *
 * Description:
 *   Build a LIN event frame (LIN_EVT_FLAG) and enqueue it to rx_queue so the
 *   application layer is notified of state changes (passive sleep, passive
 *   wakeup, etc.).
 *
 ****************************************************************************/

static inline_function void
lin_serial_report_event(FAR struct lin_serial_dev_s *priv,
                        uint8_t event)
{
  struct can_frame frame;

  memset(&frame, 0, sizeof(frame));
  frame.can_id  = LIN_EVT_FLAG;
  frame.can_dlc = 1;
  frame.data[0] = event;

  lin_serial_enqueue_notify(priv, &frame);
}

/****************************************************************************
 * Name: lin_serial_report_error
 *
 * Description:
 *   Build a LIN error frame (LIN_ERR_FLAG) and enqueue it so the
 *   application layer is notified of bus errors (checksum, parity, etc.).
 *
 ****************************************************************************/

static inline_function void
lin_serial_report_error(FAR struct lin_serial_dev_s *priv,
                        uint8_t err_class,
                        uint8_t err_reason)
{
  struct can_frame frame;

  /* LIN_ERR_TX → data[1], LIN_ERR_RX → data[2],
   * LIN_ERR_BUS → data[3], LIN_ERR_CRTL → data[4].
   * ffs gives the bit position: TX=0→1, RX=1→2, etc.
   */

  uint8_t err_reason_idx = ffs(err_class);

  memset(&frame, 0, sizeof(frame));
  frame.can_id  = LIN_ERR_FLAG;
  frame.can_dlc = CAN_MAX_DLEN;
  frame.data[0] = err_class;
  frame.data[err_reason_idx] = err_reason;

  lin_serial_enqueue_notify(priv, &frame);
}

/****************************************************************************
 * Name: lin_serial_report_tcf
 *
 * Description:
 *   Build a LIN TX-confirmation frame (LIN_TCF_FLAG) and enqueue it.
 *   Upper layer (comlin_If) uses this to confirm successful transmission.
 *   The original 6-bit LIN ID is preserved in can_id alongside the flag.
 *
 ****************************************************************************/

static inline_function void
lin_serial_report_tcf(FAR struct lin_serial_dev_s *priv,
                      uint8_t id)
{
  struct can_frame frame;

  memset(&frame, 0, sizeof(frame));
  frame.can_id  = (id & LIN_ID_MASK) | LIN_TCF_FLAG;
  frame.can_dlc = 0;

  lin_serial_enqueue_notify(priv, &frame);
}

/****************************************************************************
 * Name: lin_serial_send_frame
 *
 * Description:
 *   Serialize a LIN frame (SYNC + PID [+ data + checksum]) and write it
 *   to the backing serial device.  Used by the TX path (transmit) and
 *   internal control frames (sleep/wakeup signals).
 *
 *   Normal frame: [SYNC 0x55] [PID] [data 2/4/8B] [checksum]
 *   RTR (header only): [SYNC 0x55] [PID]
 *
 *   On real UART hardware, a BREAK is sent before SYNC via TCSBRK ioctl.
 *   On virtual serial ports the BREAK is omitted (no physical bus).
 *
 ****************************************************************************/

static int lin_serial_send_frame(FAR struct lin_serial_dev_s *priv,
                                 FAR struct can_frame *frame)
{
  uint8_t tx_buf[LIN_SERIAL_FRAME_MAX];
  uint8_t id = frame->can_id;
  bool enhanced = (frame->can_id & LIN_CHECKSUM_EXTENDED) != 0;
  ssize_t write_len;
  uint8_t pid;
  uint8_t dlc;
  uint8_t tx_len;

#ifdef CONFIG_LIN_SERIAL_MODE_HARDWARE
  int ret;

  ret = file_ioctl(&priv->filep, TCSBRK, 0);
  if (ret < 0)
    {
      nerr("%s: TCSBRK failed: %d\n", priv->dev.netdev.d_ifname, ret);
      return ret;
    }
#endif

  pid = lin_serial_pid_parity(id);
  tx_buf[0] = LIN_SERIAL_SYNC_BYTE;
  tx_buf[1] = pid;

  if (frame->can_id & LIN_RTR_FLAG)
    {
      tx_len = 2;
    }
  else
    {
      dlc = frame->can_dlc > CAN_MAX_DLEN ? CAN_MAX_DLEN : frame->can_dlc;
      memcpy(&tx_buf[2], frame->data, dlc);
      tx_buf[2 + dlc] = lin_serial_checksum(enhanced, pid,
                                            frame->data, dlc);
      tx_len = 2 + dlc + 1;
    }

#ifndef CONFIG_LIN_SERIAL_MODE_HARDWARE
  /* Virtual mode: prepend 1-byte length prefix for frame delimiting
   * on the chardev TCP socket (raw_uart transport protocol).
   */

  write_len = file_write(&priv->filep, &tx_len, 1);
  if (write_len < 0)
    {
      nerr("%s: failed to write length prefix\n", priv->dev.netdev.d_ifname);
      return write_len;
    }
#endif

  write_len = file_write(&priv->filep, tx_buf, tx_len);
  if (write_len < 0)
    {
      nerr("%s: failed to write frame\n", priv->dev.netdev.d_ifname);
      return write_len;
    }

  return OK;
}

#if CONFIG_LIN_SERIAL_IDLE_TIMEOUT > 0
/****************************************************************************
 * Name: lin_serial_idle_timeout
 *
 * Description:
 *   Work queue callback: bus idle timeout expired, transition to SLEEP.
 *
 ****************************************************************************/

static void lin_serial_idle_timeout(FAR void *arg)
{
  FAR struct lin_serial_dev_s *priv = (FAR struct lin_serial_dev_s *)arg;

  int expect = CAN_STATE_OPERATIONAL;
  if (atomic_cmpxchg(&priv->state, &expect, CAN_STATE_SLEEP))
    {
      /* Discard any partially received frame to avoid an rx_pkt leak. */

      nxmutex_lock(&priv->mutex);
      if (priv->rx_state != LIN_RX_IDLE && priv->rx_pkt != NULL)
        {
          netpkt_free(&priv->dev, priv->rx_pkt, NETPKT_RX);
          priv->rx_pkt = NULL;
        }

      priv->rx_state = LIN_RX_IDLE;
      priv->rx_pos   = 0;
      nxmutex_unlock(&priv->mutex);

      lin_serial_report_event(priv, LIN_EVT_SLEEP_IDLE);
    }
}
#endif

/****************************************************************************
 * Name: lin_serial_restart_idle_timer
 *
 * Description:
 *   Restart the bus idle timeout timer.  Called after every successful
 *   frame TX or RX.  If the timer expires without any bus activity the
 *   interface transitions to SLEEP automatically.
 *
 ****************************************************************************/

static inline_function void lin_serial_restart_idle_timer(
  FAR struct lin_serial_dev_s *priv)
{
#if CONFIG_LIN_SERIAL_IDLE_TIMEOUT > 0
  work_queue(LPWORK, &priv->idle_work, lin_serial_idle_timeout,
             priv,
             SEC2TICK(CONFIG_LIN_SERIAL_IDLE_TIMEOUT));
#else
  UNUSED(priv);
#endif
}

/****************************************************************************
 * Name: lin_serial_process_rx
 *
 * Description:
 *   LIN frame reassembly from a byte stream with zero-copy into netpkt.
 *
 *   Master mode: stays in RX_IDLE until transmit() RTR seeds the state:
 *     - echo=false → RX_COLLECTING (data bytes arrive immediately)
 *     - echo=true  → RX_ECHO_SYNC → RX_ECHO_PID → RX_COLLECTING
 *   Slave mode: WAIT_SYNC → WAIT_PID → COLLECTING (TODO — not yet
 *     implemented; currently falls through to a no-op break).
 *   COLLECTING is shared by both roles for data + checksum reception.
 *
 ****************************************************************************/

static void lin_serial_process_rx(FAR struct lin_serial_dev_s *priv,
                                  FAR const uint8_t *data,
                                  ssize_t len)
{
  FAR struct can_frame *frame;
  ssize_t i;

  for (i = 0; i < len; i++)
    {
      switch (priv->rx_state)
        {
          case LIN_RX_IDLE:
          case LIN_RX_WAIT_SYNC:
          case LIN_RX_WAIT_PID:

            /* IDLE: not receiving — discard.
             * WAIT_SYNC / WAIT_PID: TODO — Slave header parsing.
             */

            break;

          case LIN_RX_ECHO_SYNC:

            /* Master RTR echo: wait for the echoed SYNC byte.
             * If the byte is not 0x55, stay in this state and keep
             * scanning — the correct echo may arrive after noise bytes.
             */

            if (data[i] == LIN_SERIAL_SYNC_BYTE)
              {
                priv->rx_state = LIN_RX_ECHO_PID;
              }

            break;

          case LIN_RX_ECHO_PID:
            {
              /* Master RTR echo: verify the echoed PID byte matches
               * the expected protected ID.  On mismatch, fall back to
               * ECHO_SYNC and keep scanning — the SYNC+PID pair may
               * still arrive after spurious bytes.
               */

              uint8_t expected_pid;

              nxmutex_lock(&priv->mutex);
              frame = (FAR struct can_frame *)
                netpkt_getdata(&priv->dev, priv->rx_pkt);

              if (priv->rx_pkt == NULL)
                {
                  priv->rx_state = LIN_RX_IDLE;
                  nxmutex_unlock(&priv->mutex);
                  break;
                }

              expected_pid = lin_serial_pid_parity(frame->can_id);

              if (data[i] == expected_pid)
                {
                  priv->rx_state = LIN_RX_COLLECTING;
                }
              else
                {
                  priv->rx_state = LIN_RX_ECHO_SYNC;
                }

              nxmutex_unlock(&priv->mutex);
            }
            break;

          case LIN_RX_COLLECTING:
            nxmutex_lock(&priv->mutex);

            if (priv->rx_pkt == NULL)
              {
                priv->rx_state = LIN_RX_IDLE;
                nxmutex_unlock(&priv->mutex);
                break;
              }

            frame = (FAR struct can_frame *)
              netpkt_getdata(&priv->dev, priv->rx_pkt);

            if (frame->can_dlc > CAN_MAX_DLEN)
              {
                frame->can_dlc = CAN_MAX_DLEN;
              }

            priv->rx_checksum += data[i];
            if (priv->rx_checksum > 0xff)
              {
                priv->rx_checksum -= 0xff;
              }

            if (priv->rx_pos < frame->can_dlc)
              {
                frame->data[priv->rx_pos++] = data[i];
                nxmutex_unlock(&priv->mutex);
                break;
              }

            /* Checksum byte received — verify. */

            if (priv->rx_checksum != 0xff)
              {
                nwarn("%s: checksum error\n", priv->dev.netdev.d_ifname);
                netpkt_free(&priv->dev, priv->rx_pkt, NETPKT_RX);
                priv->rx_pkt = NULL;
                priv->rx_state = LIN_RX_IDLE;
                nxmutex_unlock(&priv->mutex);

                /* Report the error outside the lock: report_error()
                 * takes priv->mutex internally, so calling it while
                 * holding the lock would self-deadlock on the
                 * non-recursive mutex.
                 */

                lin_serial_report_error(priv, LIN_ERR_RX,
                                        LIN_ERR_RX_CKSUM);
                break;
              }

            lin_serial_restart_idle_timer(priv);
            netpkt_setdatalen(&priv->dev, priv->rx_pkt,
                              sizeof(struct can_frame));
            netpkt_add_queue(priv->rx_pkt, &priv->rx_queue);
            priv->rx_pkt = NULL;
            priv->rx_state = LIN_RX_IDLE;
            nxmutex_unlock(&priv->mutex);
            netdev_lower_rxready(&priv->dev);
            break;
        }
    }
}

/****************************************************************************
 * Name: lin_serial_rx_worker
 *
 * Description:
 *   LPWORK callback that performs non-blocking reads from the backing tty,
 *   processes all available data through the reassembly state machine,
 *   and returns.  The next poll event will schedule another invocation.
 *
 ****************************************************************************/

static void lin_serial_rx_worker(FAR void *arg)
{
  FAR struct lin_serial_dev_s *priv = (FAR struct lin_serial_dev_s *)arg;
  uint8_t tmp[LIN_SERIAL_FRAME_MAX];
  ssize_t read_size;
  int expect;

  if (!LIN_SERIAL_ISUP(priv))
    {
      return;
    }

#ifndef CONFIG_LIN_SERIAL_MODE_HARDWARE
  /* Virtual mode: each frame is prefixed with a 1-byte length.
   * Read the length byte first, then read exactly that many bytes.
   */

  while (true)
    {
      uint8_t len_byte;

      read_size = file_read(&priv->filep, &len_byte, 1);
      if (read_size <= 0)
        {
          break;
        }

      if (len_byte == 0 || len_byte > sizeof(tmp))
        {
          continue;
        }

      read_size = file_read(&priv->filep, tmp, len_byte);
      if (read_size <= 0 || read_size != len_byte)
        {
          break;
        }

      expect = CAN_STATE_SLEEP;
      if (atomic_cmpxchg(&priv->state, &expect, CAN_STATE_OPERATIONAL))
        {
          lin_serial_report_event(priv, LIN_EVT_WAKEUP_PASSIVE);
        }

      lin_serial_process_rx(priv, tmp, read_size);
    }
#else
  /* Hardware mode: drain all available data from the tty */

  while ((read_size = file_read(&priv->filep, tmp, sizeof(tmp))) > 0)
    {
      /* Passive wakeup — if we are in SLEEP and received any
       * data on the bus, transition to OPERATIONAL first so that
       * the frame data is processed in the correct state.
       */

      expect = CAN_STATE_SLEEP;
      if (atomic_cmpxchg(&priv->state, &expect, CAN_STATE_OPERATIONAL))
        {
          lin_serial_report_event(priv, LIN_EVT_WAKEUP_PASSIVE);
        }

      lin_serial_process_rx(priv, tmp, read_size);
    }
#endif

  if (read_size < 0 && read_size != -EAGAIN)
    {
      nerr("%s: read error: %zd\n", priv->dev.netdev.d_ifname, read_size);
    }
}

/****************************************************************************
 * Name: lin_serial_poll_cb
 *
 * Description:
 *   Poll callback invoked by the serial driver (possibly from IRQ context)
 *   when data is available on the backing tty.  Schedules a LPWORK item
 *   for the actual read — no blocking allowed here.
 *
 ****************************************************************************/

static void lin_serial_poll_cb(FAR struct pollfd *fds)
{
  FAR struct lin_serial_dev_s *priv =
    (FAR struct lin_serial_dev_s *)fds->arg;

  if (LIN_SERIAL_ISUP(priv) && (fds->revents & POLLIN))
    {
      work_queue(LPWORK, &priv->rx_work, lin_serial_rx_worker, priv, 0);
    }
}

/****************************************************************************
 * Name: lin_serial_ifup
 *
 * Description:
 *   Bring up the LIN interface.  Registers a poll callback on the backing
 *   tty so that incoming data is processed via LPWORK.
 *
 ****************************************************************************/

static int lin_serial_ifup(FAR struct netdev_lowerhalf_s *dev)
{
  FAR struct lin_serial_dev_s *priv = (FAR struct lin_serial_dev_s *)dev;
  int ret;

  int expect = CAN_STATE_STOPPED;
  if (!atomic_cmpxchg(&priv->state, &expect, CAN_STATE_OPERATIONAL))
    {
      ninfo("%s: interface is already up\n", priv->dev.netdev.d_ifname);
      return -EALREADY;
    }

  /* Register poll callback — the serial driver will invoke it when
   * data arrives.  The callback schedules a LPWORK item to do the
   * actual (potentially blocking) file_read.
   */

  priv->fds.arg    = priv;
  priv->fds.cb     = lin_serial_poll_cb;
  priv->fds.events = POLLIN;

  ret = file_poll(&priv->filep, &priv->fds, true);
  if (ret < 0)
    {
      LIN_SERIAL_SETSTOP(priv);
      work_cancel_sync(LPWORK, &priv->rx_work);
      nerr("%s: poll setup failed: %d\n", priv->dev.netdev.d_ifname, ret);
      return ret;
    }

  netdev_lower_carrier_on(dev);
  ninfo("%s: interface up\n", priv->dev.netdev.d_ifname);
  return OK;
}

/****************************************************************************
 * Name: lin_serial_ifdown
 *
 * Description:
 *   Shut down the LIN interface.  Unregisters the poll callback, cancels
 *   any pending work, drains the RX queue.
 *
 ****************************************************************************/

static int lin_serial_ifdown(FAR struct netdev_lowerhalf_s *dev)
{
  FAR struct lin_serial_dev_s *priv = (FAR struct lin_serial_dev_s *)dev;
  FAR netpkt_t *pkt;

  /* If the LIN interface is down already, return OK */

  if (atomic_xchg(&priv->state, CAN_STATE_STOPPED) == CAN_STATE_STOPPED)
    {
      ninfo("%s: interface is already down\n", priv->dev.netdev.d_ifname);
      return OK;
    }

  /* Unregister poll — no more rx_work will be queued after this */

  file_poll(&priv->filep, &priv->fds, false);

  /* Wait for any in-flight rx_work to finish */

  work_cancel_sync(LPWORK, &priv->rx_work);

#if CONFIG_LIN_SERIAL_IDLE_TIMEOUT > 0
  work_cancel_sync(LPWORK, &priv->idle_work);
#endif

  /* Safe to drain — no concurrent access possible */

  while ((pkt = netpkt_remove_queue(&priv->rx_queue)) != NULL)
    {
      netpkt_free(&priv->dev, pkt, NETPKT_RX);
    }

  /* Free any in-flight rx_pkt and reset reassembly state */

  if (priv->rx_pkt != NULL)
    {
      netpkt_free(&priv->dev, priv->rx_pkt, NETPKT_RX);
      priv->rx_pkt = NULL;
    }

  priv->rx_state = 0;
  priv->rx_pos   = 0;

  netdev_lower_carrier_off(dev);
  ninfo("%s: interface down\n", priv->dev.netdev.d_ifname);
  return OK;
}

/****************************************************************************
 * Name: lin_serial_transmit
 *
 * Description:
 *   Transmit a CAN/LIN frame over the backing tty device.
 *   Master mode: send full frame or Header-only (RTR).
 *   Slave mode: not yet implemented (returns -ENOSYS for cache ops).
 *
 ****************************************************************************/

static int lin_serial_transmit(FAR struct netdev_lowerhalf_s *dev,
                               FAR netpkt_t *pkt)
{
  FAR struct lin_serial_dev_s *priv = (FAR struct lin_serial_dev_s *)dev;
  FAR struct can_frame *frame;
  uint32_t pkt_len;
  int expect;
  int ret;

  /* Check the frame length */

  pkt_len = netpkt_getdatalen(dev, pkt);
  if (pkt_len != sizeof(struct can_frame))
    {
      nwarn("%s: frame size error %" PRIu32 "\n",
            priv->dev.netdev.d_ifname, pkt_len);
      return -EINVAL;
    }

  frame = (FAR struct can_frame *)netpkt_getdata(dev, pkt);

  /* Slave cache response path — TODO: implement in slave phase */

  if (!priv->master && (frame->can_id & LIN_CACHE_RESPONSE))
    {
      netpkt_free(&priv->dev, pkt, NETPKT_TX);
      return -ENOSYS;
    }

  /* Master mode: send frame on the bus. */

  expect = CAN_STATE_OPERATIONAL;
  if (!atomic_cmpxchg(&priv->state, &expect, CAN_STATE_BUSY))
    {
      nwarn("%s: not in operational state\n", priv->dev.netdev.d_ifname);
      return -EIO;
    }

  ret = lin_serial_send_frame(priv, frame);
  if (ret < 0)
    {
      LIN_SERIAL_SETNORMAL(priv);
      return ret;
    }

  if (frame->can_id & LIN_RTR_FLAG)
    {
      nxmutex_lock(&priv->mutex);

      /* Free stale rx_pkt from a previous RTR whose slave response
       * never arrived — prevents IOB leak.
       */

      if (priv->rx_pkt != NULL)
        {
          netpkt_free(&priv->dev, priv->rx_pkt, NETPKT_RX);
          priv->rx_pkt = NULL;
        }

      if (frame->can_id & LIN_CHECKSUM_EXTENDED)
        {
          priv->rx_checksum =
            lin_serial_pid_parity(frame->can_id);
        }
      else
        {
          priv->rx_checksum = 0;
        }

      pkt->io_conn   = NULL;
      priv->rx_pkt   = pkt;
      priv->rx_pos   = 0;
      priv->rx_state = priv->echo ? LIN_RX_ECHO_SYNC
                                  : LIN_RX_COLLECTING;
      nxmutex_unlock(&priv->mutex);
    }
  else
    {
      /* Non-RTR: data frame sent successfully — report TCF to upper
       * layer before freeing the TX pkt, consistent with hardware
       * driver (aurix_lin_sync.c) behavior.
       */

      lin_serial_report_tcf(priv, frame->can_id & LIN_ID_MASK);
      netpkt_free(&priv->dev, pkt, NETPKT_TX);
    }

  netdev_lower_txdone(&priv->dev);
  LIN_SERIAL_SETNORMAL(priv);
  lin_serial_restart_idle_timer(priv);
  return OK;
}

/****************************************************************************
 * Name: lin_serial_receive
 *
 * Description:
 *   Dequeue a received frame from the RX queue and return it to the
 *   upper-half stack.
 *
 ****************************************************************************/

static FAR netpkt_t *lin_serial_receive(FAR struct netdev_lowerhalf_s *dev)
{
  FAR struct lin_serial_dev_s *priv = (FAR struct lin_serial_dev_s *)dev;
  FAR netpkt_t *pkt;

  nxmutex_lock(&priv->mutex);
  pkt = netpkt_remove_queue(&priv->rx_queue);
  nxmutex_unlock(&priv->mutex);
  return pkt;
}

#ifdef CONFIG_NETDEV_IOCTL
/****************************************************************************
 * Name: lin_serial_wakeup
 *
 * Description:
 *   Process a wakeup request from ioctl.  Unless the wakeup is internal,
 *   a wakeup signal is sent on the bus:
 *     Hardware mode: a UART BREAK pulse (standard LIN wakeup).
 *     Virtual mode: a wakeup frame (PID 0x3e, data 0xff...).
 *
 ****************************************************************************/

static int lin_serial_wakeup(FAR struct lin_serial_dev_s *priv,
                             bool internal)
{
  int ret = OK;

  if (LIN_SERIAL_ISNORMAL(priv))
    {
      /* Do nothing if LIN is already in operational mode */
    }
  else if (!LIN_SERIAL_ISSLEEP(priv))
    {
      ret = -EBUSY;
    }
  else
    {
      if (internal)
        {
          LIN_SERIAL_SETNORMAL(priv);

          /* Refresh the idle timer so a pending idle_timeout cannot
           * sleep the interface right after it was woken.
           */

          lin_serial_restart_idle_timer(priv);
        }
      else
        {
#ifdef CONFIG_LIN_SERIAL_MODE_HARDWARE
          /* A UART BREAK produces a dominant pulse (250us~5ms at typical
           * LIN baud rates), which is the standard LIN wakeup signal.
           */

          ret = file_ioctl(&priv->filep, TCSBRK, 0);
          if (ret < 0)
            {
              nerr("%s: TCSBRK (wakeup) failed: %d\n",
                   priv->dev.netdev.d_ifname, ret);
            }
#else
          struct can_frame frame;

          frame.can_id  = 0x3e;
          frame.can_dlc = 8;
          memset(frame.data, 0xff, 8);

          ret = lin_serial_send_frame(priv, &frame);
#endif

          if (ret == OK)
            {
              LIN_SERIAL_SETNORMAL(priv);
              lin_serial_restart_idle_timer(priv);
            }
        }
    }

  return ret;
}

/****************************************************************************
 * Name: lin_serial_sleep
 *
 * Description:
 *   Transition the LIN controller to sleep state.  If master and not
 *   internal, sends the sleep diagnostic frame (PID 0x3c, data
 *   0x00/0xff...) first.
 *
 ****************************************************************************/

static int lin_serial_sleep(FAR struct lin_serial_dev_s *priv,
                            bool internal)
{
  int ret = OK;

  if (LIN_SERIAL_ISSLEEP(priv))
    {
      /* Already in sleep mode */
    }
  else if (!LIN_SERIAL_ISNORMAL(priv))
    {
      ret = -EBUSY;
    }
  else
    {
      if (internal)
        {
          LIN_SERIAL_SETSLEEP(priv);

          /* Entered sleep actively: drop the pending idle timer. */

#if CONFIG_LIN_SERIAL_IDLE_TIMEOUT > 0
          work_cancel(LPWORK, &priv->idle_work);
#endif
        }
      else if (priv->master)
        {
          struct can_frame frame;

          /* Master: transition through SPENDING while the sleep
           * frame is on the wire, then enter SLEEP on success.
           */

          frame.can_id  = 0x3c;
          frame.can_dlc = 8;
          frame.data[0] = 0x00;
          memset(&frame.data[1], 0xff, 7);

          LIN_SERIAL_SETSPENDING(priv);
          ret = lin_serial_send_frame(priv, &frame);
          if (ret == OK)
            {
              LIN_SERIAL_SETSLEEP(priv);
#if CONFIG_LIN_SERIAL_IDLE_TIMEOUT > 0
              work_cancel(LPWORK, &priv->idle_work);
#endif
            }
          else
            {
              LIN_SERIAL_SETNORMAL(priv);
            }
        }
      else
        {
          ret = -ENOTSUP;
        }
    }

  return ret;
}

/****************************************************************************
 * Name: lin_serial_ioctl
 *
 * Description:
 *   Handle ioctl commands for the LIN interface: state control (sleep /
 *   wakeup) and baud rate configuration.
 *
 ****************************************************************************/

static int lin_serial_ioctl(FAR struct netdev_lowerhalf_s *dev, int cmd,
                            unsigned long arg)
{
  FAR struct lin_serial_dev_s *priv = (FAR struct lin_serial_dev_s *)dev;
  int ret = -ENOTTY;

  switch (cmd)
    {
#ifdef CONFIG_NETDEV_CAN_STATE_IOCTL
      case SIOCGCANSTATE:
        {
          FAR struct can_ioctl_state_s *st =
            (FAR struct can_ioctl_state_s *)arg;
          st->state = (enum can_ioctl_state_e)atomic_read(&priv->state);
          ret = OK;
        }
        break;

      case SIOCSCANSTATE:
        {
          FAR struct can_ioctl_state_s *state =
            (FAR struct can_ioctl_state_s *)arg;
          if (state->state == CAN_STATE_SLEEP)
            {
              ret = lin_serial_sleep(priv, (bool)state->priv);
            }
          else if (state->state == CAN_STATE_OPERATIONAL)
            {
              ret = lin_serial_wakeup(priv, (bool)state->priv);
            }
          else
            {
              ret = -ENOTSUP;
            }
        }
        break;
#endif /* CONFIG_NETDEV_CAN_STATE_IOCTL */

      /* Baud rate: reuse the CAN bitrate ioctls. */

      case SIOCGCANBITRATE:
        {
          FAR struct can_ioctl_data_s *data =
            (FAR struct can_ioctl_data_s *)arg;
          data->arbi_bitrate = priv->baud;
          data->data_bitrate = priv->baud;
          ret = OK;
        }
        break;

      case SIOCSCANBITRATE:
        {
          FAR struct can_ioctl_data_s *data =
            (FAR struct can_ioctl_data_s *)arg;
          ret = lin_serial_setbaud(priv, data->arbi_bitrate);
        }
        break;

      default:
        break;
    }

  return ret;
}

#endif /* CONFIG_NETDEV_IOCTL */

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: lin_serial_register
 *
 * Description:
 *   Register a LIN-over-tty network interface.  The caller supplies the
 *   serial device node that carries the LIN traffic and the initial baud
 *   rate.  The interface starts with classic checksum.  The master/slave
 *   role and echo behaviour are fixed at registration time.  Baud rate
 *   can be changed at run time via ioctl (SIOCSCANBITRATE) before calling
 *   ifup.
 *
 ****************************************************************************/

int lin_serial_register(FAR const char *devpath, int ifno, uint32_t baud,
                        bool master, bool echo)
{
  FAR struct lin_serial_dev_s *priv;
  int ret;

  if (devpath == NULL || ifno < 0)
    {
      return -EINVAL;
    }

  priv = kmm_zalloc(sizeof(struct lin_serial_dev_s));
  if (priv == NULL)
    {
      nerr("lin%d: failed to allocate device state\n", ifno);
      return -ENOMEM;
    }

  priv->master   = master;
  priv->echo     = echo;
  priv->baud     = baud;
  nxmutex_init(&priv->mutex);

  /* Open the backing serial device */

  ret = file_open(&priv->filep, devpath, O_RDWR | O_NONBLOCK | O_CLOEXEC);
  if (ret < 0)
    {
      nerr("lin%d: failed to open %s: %d\n", ifno, devpath, ret);
      nxmutex_destroy(&priv->mutex);
      kmm_free(priv);
      return ret;
    }

  /* Apply the initial baud rate (no-op on virtual serial ports) */

  ret = lin_serial_setbaud(priv, priv->baud);
  if (ret < 0)
    {
      nwarn("lin%d: initial setbaud failed: %d\n", ifno, ret);
    }

  /* Register the SocketCAN compatible netdev */

  snprintf(priv->dev.netdev.d_ifname, IFNAMSIZ, "lin%d", ifno);
  priv->dev.netdev.d_pktsize = CAN_MTU;
  priv->dev.ops = &g_lin_serial_ops;
  IFF_SET_LOOPBACK(priv->dev.netdev.d_flags);

  ret = netdev_lower_register(&priv->dev, NET_LL_CAN);
  if (ret < 0)
    {
      nerr("lin%d: netdev_lower_register failed: %d\n", ifno, ret);
      file_close(&priv->filep);
      nxmutex_destroy(&priv->mutex);
      kmm_free(priv);
      return ret;
    }

  ninfo("lin%d: registered on %s (role=%s, baud=%" PRIu32 ")\n",
        ifno, devpath, priv->master ? "master" : "slave",
        priv->baud);
  return OK;
}

/****************************************************************************
 * Name: lin_serial_unregister
 *
 * Description:
 *   Unregister the LIN-over-tty network interface "lin<ifno>" and release
 *   all resources.  The interface must be down (ifdown) before calling
 *   this function.
 *
 ****************************************************************************/

int lin_serial_unregister(int ifno)
{
  FAR struct net_driver_s *netdev;
  FAR struct netdev_lowerhalf_s *lower;
  FAR struct lin_serial_dev_s *priv;
  char ifname[IFNAMSIZ];
  int ret;

  if (ifno < 0)
    {
      return -EINVAL;
    }

  snprintf(ifname, sizeof(ifname), "lin%d", ifno);
  netdev = netdev_findbyname(ifname);
  if (netdev == NULL)
    {
      nerr("lin: device %s not found\n", ifname);
      return -ENODEV;
    }

  lower = container_of(netdev, struct netdev_lowerhalf_s, netdev);
  priv  = (FAR struct lin_serial_dev_s *)lower;

  if (lower->ops != &g_lin_serial_ops)
    {
      nerr("lin: %s is not a lin_serial device\n", ifname);
      return -EINVAL;
    }

  if (LIN_SERIAL_ISUP(priv))
    {
      nerr("%s: interface still up, call ifdown first\n",
           priv->dev.netdev.d_ifname);
      return -EBUSY;
    }

  ret = netdev_lower_unregister(lower);
  if (ret < 0)
    {
      nerr("%s: netdev_lower_unregister failed: %d\n",
           priv->dev.netdev.d_ifname, ret);
      return ret;
    }

  file_close(&priv->filep);
  nxmutex_destroy(&priv->mutex);
  kmm_free(priv);
  return OK;
}
