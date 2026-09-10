/****************************************************************************
 * drivers/i2c/i2c_mcal.c
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
#include <stdbool.h>
#include <stdint.h>

#include <nuttx/arch.h>
#include <nuttx/irq.h>
#include <nuttx/percpu.h>
#include <nuttx/spinlock.h>
#include <nuttx/kmalloc.h>
#include <nuttx/mutex.h>
#include <nuttx/i2c/i2c_mcal.h>

#if (I2C_DEV_ERR_REPORTING == STD_ON) || \
    (I2C_RUNTIME_ERR_REPORTING == STD_ON)
#  include "Det.h"
#endif

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

 /* DET (Development/Runtime Error Tracer) reporting. Det is an AUTOSAR
  * service, but the module/instance/service ids, error codes and enable
  * switches are module-specific and defined here (AUTOSAR conventions) to
  * keep this adaptation vendor-neutral.
  */

#ifndef I2C_MODULE_ID
#  define I2C_MODULE_ID                 (255U)
#endif

#ifndef I2C_MODULE_INSTANCE
#  define I2C_MODULE_INSTANCE           (0U)
#endif

#define MCAL_I2C_SID_INITIALIZE  (0x80u)
#define MCAL_I2C_SID_TRANSFER    (0x81u)
#define MCAL_I2C_SID_SEQ_END     (0x82u)

/* Adaptation layer DET error codes */

#define MCAL_I2C_E_PARAM_POINTER (0x01u)  /* NULL cfg / zero count          */
#define MCAL_I2C_E_INIT_FAILED   (0x02u)  /* alloc / irq_setup / register   */
#define MCAL_I2C_E_SEQ_NO_OWNER  (0x03u)  /* Completed seq not owned on core */

/* NuttX has no write flag: a message is a write when I2C_M_READ is clear.
 * MCAL_I2C_IS_WR_RD() matches a write immediately followed by a read.
 */

#define MCAL_I2C_IS_READ(m)      (((m)->flags & I2C_M_READ) != 0)
#define MCAL_I2C_IS_WR_RD(m, n) \
  ((n) >= 2 && !MCAL_I2C_IS_READ(&(m)[0]) && MCAL_I2C_IS_READ(&(m)[1]))

/****************************************************************************
 * Private Types
 ****************************************************************************/

/* Runtime state for one async pool slot */

struct mcal_i2c_slot_s
{
  bool              in_flight;  /* Slot currently owns an async transfer */
  uint16_t          active_seq; /* MCAL sequence id of the in-flight transfer */
  i2c_statechange_t callback;   /* completion callback */
  FAR void         *arg;        /* Callback argument */
};

/* Private device state; embeds i2c_master_s as first member */

struct mcal_i2c_priv_s
{
  struct i2c_master_s                 dev;   /* MUST be first */
  FAR const struct mcal_i2c_config_s *cfg;
  mutex_t                             lock;  /* Bus serialization (thread ctx) */
  spinlock_t                          slock; /* Per-bus async slot/ISR lock */
  FAR struct mcal_i2c_slot_s         *slots; /* Async pool state, size = cfg->async_pool_size */
  FAR struct mcal_i2c_priv_s         *flink; /* Global registry link */
};

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static int mcal_i2c_transfer(FAR struct i2c_master_s *dev,
                             FAR struct i2c_msg_s *msgs, int count);
#ifdef CONFIG_I2C_RESET
static int mcal_i2c_reset(FAR struct i2c_master_s *dev);
#endif
static uint8_t mcal_i2c_status(FAR struct i2c_master_s *dev);
static int mcal_i2c_cancel(FAR struct i2c_master_s *dev, FAR void *cookie);

/****************************************************************************
 * Private Data
 ****************************************************************************/

static const struct i2c_ops_s g_mcal_i2c_ops =
{
  .transfer = mcal_i2c_transfer,
#ifdef CONFIG_I2C_RESET
  .reset    = mcal_i2c_reset,
#endif
  .status   = mcal_i2c_status,
  .cancel   = mcal_i2c_cancel,
};

/* Registry of registered buses, used by mcal_i2c_seq_end() to locate the
 * owning bus/slot from a completed sequence id.
 */

static DEFINE_PER_CPU_BMP(FAR struct mcal_i2c_priv_s *, g_mcal_i2c_list);
#define g_mcal_i2c_list this_cpu_var_bmp(g_mcal_i2c_list)

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: mcal_i2c_map_result
 *
 * Description:
 *   Map an MCAL sequence result to a NuttX errno.
 *
 ****************************************************************************/

static int mcal_i2c_map_result(I2C_SequenceResultType result)
{
  switch (result)
    {
      case I2C_SEQ_OK:
        return OK;
      case I2C_SEQ_PENDING:
      case I2C_SEQ_QUEUED:
        return -EBUSY;
      case I2C_SEQ_FAILED:
      default:
        return -EIO;
    }
}

/****************************************************************************
 * Name: mcal_i2c_setup_job
 *
 * Description:
 *   Bind one i2c message to an MCAL job. The job direction is chosen at
 *   run time by I2c_SetupEB() from which buffer pointer is non-NULL
 *   (dst != NULL -> read, else write), so any job can carry read or write.
 *
 ****************************************************************************/

static Std_ReturnType mcal_i2c_setup_job(uint16_t job,
                                         FAR const struct i2c_msg_s *msg)
{
  if (MCAL_I2C_IS_READ(msg))
    {
      return I2c_SetupEB(job, msg->addr, NULL,
                         (I2c_DataBufferType *)msg->buffer,
                         (I2c_NumberOfDataType)msg->length);
    }

  return I2c_SetupEB(job, msg->addr,
                     (const I2c_DataBufferType *)msg->buffer, NULL,
                     (I2c_NumberOfDataType)msg->length);
}

/****************************************************************************
 * Name: mcal_i2c_setup_seq
 *
 * Description:
 *   Bind the leading message(s) of msgs[] to one sequence group and select
 *   the sequence to transmit:
 *
 *   - write followed by read: both messages go into the two-job sequence
 *     seq_wr_rd (job_wr + job_rd), issued as S addr+W ... Sr addr+R ... P.
 *   - any other single message, read or write: the single-job sequence
 *     seq_single (job_single), issued as S addr+W/R ... P.
 *
 * Returned Value:
 *   Number of messages consumed (1 or 2) on success, with *seq set;
 *   -EINVAL if I2c_SetupEB() rejects the parameters.
 *
 ****************************************************************************/

static int mcal_i2c_setup_seq(FAR const struct mcal_i2c_seq_group_s *grp,
                              FAR const struct i2c_msg_s *msgs, int count,
                              FAR uint16_t *seq)
{
  if (MCAL_I2C_IS_WR_RD(msgs, count))
    {
      if (mcal_i2c_setup_job(grp->job_wr, &msgs[0]) != E_OK ||
          mcal_i2c_setup_job(grp->job_rd, &msgs[1]) != E_OK)
        {
          return -EINVAL;
        }

      *seq = grp->seq_wr_rd;
      return 2;
    }

  if (mcal_i2c_setup_job(grp->job_single, &msgs[0]) != E_OK)
    {
      return -EINVAL;
    }

  *seq = grp->seq_single;
  return 1;
}

/****************************************************************************
 * Name: mcal_i2c_xfer_sync
 *
 * Description:
 *   Synchronous transfer path. Uses I2c_SyncTransmit(), which blocks
 *   inside the MCAL driver (CPU FIFO polling) until the sequence finishes.
 *
 ****************************************************************************/

static int mcal_i2c_xfer_sync(FAR struct mcal_i2c_priv_s *priv,
                              FAR struct i2c_msg_s *msgs, int count)
{
  int ret = OK;
  int used;
  int i;

  nxmutex_lock(&priv->lock);

  for (i = 0; i < count; i += used)
    {
      uint16_t seq;

      used = mcal_i2c_setup_seq(&priv->cfg->sync, &msgs[i],
                                count - i, &seq);
      if (used < 0)
        {
          ret = used;
          break;
        }

      if (I2c_SyncTransmit(seq) != E_OK)
        {
          ret = -EIO;
          break;
        }

      ret = mcal_i2c_map_result(I2c_GetSequenceResult(seq));
      if (ret < 0)
        {
          break;
        }
    }

  nxmutex_unlock(&priv->lock);
  return ret;
}

/****************************************************************************
 * Name: mcal_i2c_alloc_slot
 *
 * Description:
 *   Allocate a free async pool slot.
 *   Returns the slot index or -1 if none free.
 *
 ****************************************************************************/

static int mcal_i2c_alloc_slot(FAR struct mcal_i2c_priv_s *priv)
{
  irqstate_t flags;
  int i;

  flags = spin_lock_irqsave(&priv->slock);
  for (i = 0; i < priv->cfg->async_pool_size; i++)
    {
      if (!priv->slots[i].in_flight)
        {
          priv->slots[i].in_flight = true;
          spin_unlock_irqrestore(&priv->slock, flags);
          return i;
        }
    }

  spin_unlock_irqrestore(&priv->slock, flags);
  return -1;
}

/****************************************************************************
 * Name: mcal_i2c_xfer_async
 *
 * Description:
 *   Asynchronous transfer path (callback != NULL). Allocates a free
 *   pool slot, submits via I2c_AsyncTransmit(), stores the completion
 *   callback and returns immediately. The cookie encodes the slot so that
 *   I2C_CANCEL()/completion can locate it. Multiple slots allow concurrent
 *   in-flight transfers on the same bus.
 *
 ****************************************************************************/

static int mcal_i2c_xfer_async(FAR struct mcal_i2c_priv_s *priv,
                               FAR struct i2c_msg_s *msgs, int count)
{
  FAR const struct mcal_i2c_config_s *cfg = priv->cfg;
  irqstate_t flags;
  uint16_t aseq = 0;
  int slot;
  int ret;

  if (cfg->async_pool == NULL || cfg->async_pool_size == 0)
    {
      return -ENOSYS;
    }

  /* One async submission maps to exactly one MCAL sequence (one
   * completion notification): a single read or write message, or a write
   * followed by a read.
   */

  if (count != 1 && !(count == 2 && MCAL_I2C_IS_WR_RD(msgs, count)))
    {
      return -EINVAL;
    }

  nxmutex_lock(&priv->lock);

  slot = mcal_i2c_alloc_slot(priv);
  if (slot < 0)
    {
      nxmutex_unlock(&priv->lock);
      return -EBUSY;
    }

  ret = mcal_i2c_setup_seq(&cfg->async_pool[slot], msgs, count, &aseq);
  if (ret < 0)
    {
      goto errout;
    }

  /* Publish the completion context BEFORE starting the transfer: the
   * sequence may finish (and mcal_i2c_seq_end() run from the ISR) before
   * I2c_AsyncTransmit() returns, which would otherwise find a NULL
   * callback, release the slot and lose the completion.
   */

  flags = spin_lock_irqsave(&priv->slock);
  priv->slots[slot].active_seq = aseq;
  priv->slots[slot].callback   = msgs[0].callback;
  priv->slots[slot].arg        = msgs[0].arg;
  spin_unlock_irqrestore(&priv->slock, flags);

  msgs[0].cookie = &priv->slots[slot];

  /* I2c_AsyncTransmit() rejects the request when the sequence is already
   * pending/queued, the HWUnit is doing a sync or slave transfer, or the
   * bus is held by another master: all transient busy conditions.
   */

  if (I2c_AsyncTransmit(aseq) != E_OK)
    {
      ret = -EBUSY;
      goto errout;
    }

  nxmutex_unlock(&priv->lock);
  return OK;

errout:
  msgs[0].cookie = NULL;

  flags = spin_lock_irqsave(&priv->slock);
  priv->slots[slot].callback  = NULL;
  priv->slots[slot].arg       = NULL;
  priv->slots[slot].in_flight = false;
  spin_unlock_irqrestore(&priv->slock, flags);

  nxmutex_unlock(&priv->lock);
  return ret;
}

/****************************************************************************
 * Name: mcal_i2c_transfer
 *
 * Description:
 *   I2C transfer op. Dispatches to the sync or async path based on
 *   whether the first message carries a completion callback.
 *
 ****************************************************************************/

static int mcal_i2c_transfer(FAR struct i2c_master_s *dev,
                             FAR struct i2c_msg_s *msgs, int count)
{
  FAR struct mcal_i2c_priv_s *priv = (FAR struct mcal_i2c_priv_s *)dev;

  if (msgs == NULL || count <= 0)
    {
      return -EINVAL;
    }

  if (msgs[0].callback != NULL)
    {
      return mcal_i2c_xfer_async(priv, msgs, count);
    }

  return mcal_i2c_xfer_sync(priv, msgs, count);
}

#ifdef CONFIG_I2C_RESET

/****************************************************************************
 * Name: mcal_i2c_reset
 *
 * Description:
 *   Bus recovery. MCAL provides no reset API, so this delegates to the
 *   vendor bus_reset hook.
 *
 ****************************************************************************/

static int mcal_i2c_reset(FAR struct i2c_master_s *dev)
{
  FAR struct mcal_i2c_priv_s *priv = (FAR struct mcal_i2c_priv_s *)dev;

  if (priv->cfg->bus_reset == NULL)
    {
      return -ENOSYS;
    }

  return priv->cfg->bus_reset(priv);
}
#endif

/****************************************************************************
 * Name: mcal_i2c_status
 *
 * Description:
 *   Report bus status from software state. A sync transfer is blocking
 *   (the caller is waiting on it), so the only busy condition the driver
 *   tracks is an async transfer still in flight on one of the pool slots.
 *
 ****************************************************************************/

static uint8_t mcal_i2c_status(FAR struct i2c_master_s *dev)
{
  FAR struct mcal_i2c_priv_s *priv = (FAR struct mcal_i2c_priv_s *)dev;
  uint8_t status = I2C_STATUS_PRESENT;
  irqstate_t flags;
  int i;

  flags = spin_lock_irqsave(&priv->slock);
  for (i = 0; i < priv->cfg->async_pool_size; i++)
    {
      if (priv->slots[i].in_flight)
        {
          status |= I2C_STATUS_BUSY;
          break;
        }
    }

  spin_unlock_irqrestore(&priv->slock, flags);
  return status;
}

/****************************************************************************
 * Name: mcal_i2c_cancel
 *
 * Description:
 *   Cancel a pending async transfer identified by cookie, or all pending
 *   async transfers on this bus when cookie is NULL.
 *
 *   MCAL I2c_Cancel() semantics: a QUEUED sequence is dropped and its
 *   SeqEndNotification is NOT called; a PENDING (started) sequence keeps
 *   running its current job and its SeqEndNotification IS still called
 *   when it ends. Therefore:
 *   - the callback is always suppressed (cleared) here;
 *   - a QUEUED slot is released here (no notification will come);
 *   - a PENDING slot stays in_flight and is released by mcal_i2c_seq_end()
 *     once the hardware is really done with the sequence and its buffers,
 *     so the slot cannot be reused while the transfer is still running.
 *
 *   Everything runs in one critical section so that mcal_i2c_seq_end()
 *   (ISR context) cannot interleave between the state check, I2c_Cancel()
 *   and the slot update. No mutex is taken, so this is callable from the
 *   completion callback / interrupt context as well.
 *
 ****************************************************************************/

static int mcal_i2c_cancel_slot(FAR struct mcal_i2c_priv_s *priv,
                                FAR struct mcal_i2c_slot_s *slot)
{
  I2C_SequenceResultType state;
  irqstate_t flags;

  flags = spin_lock_irqsave(&priv->slock);

  if (!slot->in_flight || slot->callback == NULL)
    {
      /* Already completed, or already cancelled and waiting for the
       * SeqEndNotification of the running job: nothing to cancel.
       */

      spin_unlock_irqrestore(&priv->slock, flags);
      return -ENOENT;
    }

  state = I2c_GetSequenceResult(slot->active_seq);
  I2c_Cancel(slot->active_seq);

  slot->callback = NULL;
  slot->arg      = NULL;

  if (state == I2C_SEQ_QUEUED)
    {
      slot->in_flight = false;
    }

  spin_unlock_irqrestore(&priv->slock, flags);
  return OK;
}

static int mcal_i2c_cancel(FAR struct i2c_master_s *dev, FAR void *cookie)
{
  FAR struct mcal_i2c_priv_s *priv = (FAR struct mcal_i2c_priv_s *)dev;
  FAR struct mcal_i2c_slot_s *slot = (FAR struct mcal_i2c_slot_s *)cookie;
  int idx;

  if (priv->slots == NULL)
    {
      return -ENOSYS;
    }

  if (slot == NULL)
    {
      /* Cancel all pending transfers on this bus */

      for (idx = 0; idx < priv->cfg->async_pool_size; idx++)
        {
          mcal_i2c_cancel_slot(priv, &priv->slots[idx]);
        }

      return OK;
    }

  /* The cookie must point exactly at one of this bus's slots */

  idx = (int)(slot - priv->slots);
  if (idx < 0 || idx >= priv->cfg->async_pool_size ||
      slot != &priv->slots[idx])
    {
      return -EINVAL;
    }

  return mcal_i2c_cancel_slot(priv, slot);
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: mcal_i2c_get_config
 ****************************************************************************/

FAR const struct mcal_i2c_config_s *mcal_i2c_get_config(FAR void *handle)
{
  FAR struct mcal_i2c_priv_s *priv = (FAR struct mcal_i2c_priv_s *)handle;

  return priv->cfg;
}

/****************************************************************************
 * Name: mcal_i2c_seq_end
 *
 * Description:
 *   Async completion entry, invoked from the board SeqEndNotification
 *   wrapper. Locates the owning bus/slot by sequence id, invokes the
 *   stored callback and releases the slot.
 *
 ****************************************************************************/

void mcal_i2c_seq_end(uint16_t seq)
{
  FAR struct mcal_i2c_priv_s *priv;
  i2c_statechange_t callback = NULL;
  FAR void *arg = NULL;
  bool matched = false;
  irqstate_t flags;
  int i;

  /* The completed sequence must belong to a bus registered on this core,
   * because the core that registers a bus also enables its completion
   * interrupt to itself. The per-core registry is stable after
   * initialization, so it is traversed without a lock; each bus's slots
   * are protected by its own priv->slock, taken only while scanning/
   * releasing that bus's slots.
   */

  for (priv = g_mcal_i2c_list; priv != NULL; priv = priv->flink)
    {
      if (priv->cfg->async_pool == NULL)
        {
          continue;
        }

      flags = spin_lock_irqsave(&priv->slock);
      for (i = 0; i < priv->cfg->async_pool_size; i++)
        {
          if (priv->slots[i].in_flight &&
              (priv->cfg->async_pool[i].seq_wr_rd == seq ||
               priv->cfg->async_pool[i].seq_single == seq))
            {
              callback = priv->slots[i].callback;
              arg      = priv->slots[i].arg;
              priv->slots[i].in_flight = false;
              priv->slots[i].callback  = NULL;
              matched = true;
              spin_unlock_irqrestore(&priv->slock, flags);
              goto done;
            }
        }

      spin_unlock_irqrestore(&priv->slock, flags);
    }

done:
  if (!matched)
    {
      /* No in-flight slot on this core owns this sequence. This means the
       * completion interrupt fired on a different core than the one that
       * registered the bus, which breaks the per-core ownership invariant
       * (misconfigured interrupt routing). The completion is dropped and
       * the waiting transfer would hang.
       */

#if (I2C_RUNTIME_ERR_REPORTING == STD_ON)
      (void)Det_ReportRuntimeError(I2C_MODULE_ID, I2C_MODULE_INSTANCE,
                                   MCAL_I2C_SID_SEQ_END,
                                   MCAL_I2C_E_SEQ_NO_OWNER);
#endif
      DEBUGASSERT(matched);
    }

  if (callback != NULL)
    {
      callback(arg);
    }
}

/****************************************************************************
 * Name: mcal_i2c_initialize
 ****************************************************************************/

int mcal_i2c_initialize(FAR const I2c_ConfigType *mcal_config,
                        FAR const struct mcal_i2c_config_s *cfg,
                        size_t num)
{
  FAR struct mcal_i2c_priv_s *priv;
  int ret;
  size_t i;

  if (cfg == NULL || num == 0)
    {
#if (I2C_DEV_ERR_REPORTING == STD_ON)
      (void)Det_ReportError(I2C_MODULE_ID, I2C_MODULE_INSTANCE,
                            MCAL_I2C_SID_INITIALIZE, MCAL_I2C_E_PARAM_POINTER);
#endif
      return -EINVAL;
    }

  if (mcal_config != NULL)
    {
      I2c_Init(mcal_config);
    }

  for (i = 0; i < num; i++)
    {
      priv = kmm_zalloc(sizeof(*priv));
      if (priv == NULL)
        {
          i2cerr("ERROR: i2c%d alloc failed\n", cfg[i].bus);
#if (I2C_RUNTIME_ERR_REPORTING == STD_ON)
          (void)Det_ReportRuntimeError(I2C_MODULE_ID,
                                       I2C_MODULE_INSTANCE,
                                       MCAL_I2C_SID_INITIALIZE,
                                       MCAL_I2C_E_INIT_FAILED);
#endif
          return -ENOMEM;
        }

      priv->dev.ops = &g_mcal_i2c_ops;
      priv->cfg     = &cfg[i];

      /* priv was kmm_zalloc'd; slock is therefore already SP_UNLOCKED. */

      nxmutex_init(&priv->lock);

      if (cfg[i].async_pool_size > 0)
        {
          priv->slots = kmm_zalloc(cfg[i].async_pool_size *
                                   sizeof(struct mcal_i2c_slot_s));
          if (priv->slots == NULL)
            {
              nxmutex_destroy(&priv->lock);
              kmm_free(priv);
#if (I2C_RUNTIME_ERR_REPORTING == STD_ON)
              (void)Det_ReportRuntimeError(I2C_MODULE_ID,
                                           I2C_MODULE_INSTANCE,
                                           MCAL_I2C_SID_INITIALIZE,
                                           MCAL_I2C_E_INIT_FAILED);
#endif
              return -ENOMEM;
            }

          if (cfg[i].irq_setup != NULL)
            {
              ret = cfg[i].irq_setup(priv);
              if (ret < 0)
                {
                  i2cerr("ERROR: i2c%d irq_setup failed: %d\n",
                         cfg[i].bus, ret);
                  kmm_free(priv->slots);
                  nxmutex_destroy(&priv->lock);
                  kmm_free(priv);
#if (I2C_RUNTIME_ERR_REPORTING == STD_ON)
                  (void)Det_ReportRuntimeError(I2C_MODULE_ID,
                                               I2C_MODULE_INSTANCE,
                                               MCAL_I2C_SID_INITIALIZE,
                                               MCAL_I2C_E_INIT_FAILED);
#endif
                  return ret;
                }
            }
        }

      ret = i2c_register(&priv->dev, cfg[i].bus);
      if (ret < 0)
        {
          i2cerr("ERROR: i2c_register(%d) failed: %d\n", cfg[i].bus, ret);
          if (cfg[i].async_pool_size > 0 && cfg[i].irq_teardown != NULL)
            {
              cfg[i].irq_teardown(priv);
            }

          if (priv->slots != NULL)
            {
              kmm_free(priv->slots);
            }

          nxmutex_destroy(&priv->lock);
          kmm_free(priv);
#if (I2C_RUNTIME_ERR_REPORTING == STD_ON)
          (void)Det_ReportRuntimeError(I2C_MODULE_ID,
                                       I2C_MODULE_INSTANCE,
                                       MCAL_I2C_SID_INITIALIZE,
                                       MCAL_I2C_E_INIT_FAILED);
#endif
          return ret;
        }

      /* Register in the global list for async completion lookup */

      priv->flink = g_mcal_i2c_list;
      g_mcal_i2c_list = priv;
    }

  return OK;
}
