/****************************************************************************
 * drivers/rptun/rptun_bmp.c
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

#include <nuttx/cpu_notifier.h>
#include <nuttx/nuttx.h>
#include <nuttx/reboot_notifier.h>
#include <nuttx/rptun/rptun.h>
#include <nuttx/rptun/rptun_bmp.h>
#include <nuttx/sched.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct rptun_bmp_dev_s
{
  struct rptun_dev_s      rptun;
  rptun_callback_t        callback;
  FAR void               *arg;
  bool                    master;
  FAR struct rptun_rsc_s *rsc;
  char                    cpuname[RPMSG_NAME_SIZE + 1];
  int                     irq_event;
  int                     irq_trigger;
  cpu_set_t               cpuset;
  struct notifier_block   rebootnb;
#ifdef CONFIG_SMP
  struct notifier_block   cpunb;
#endif
};

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static FAR const char *rptun_bmp_get_cpuname(FAR struct rptun_dev_s *dev);
static struct
FAR resource_table *rptun_bmp_get_resource(FAR struct rptun_dev_s *dev);
static bool rptun_bmp_is_autostart(FAR struct rptun_dev_s *dev);
static bool rptun_bmp_is_master(FAR struct rptun_dev_s *dev);
static int rptun_bmp_start(FAR struct rptun_dev_s *dev);
static int rptun_bmp_notify(FAR struct rptun_dev_s *dev,
                            uint32_t notifyid);
static int rptun_bmp_register_callback(FAR struct rptun_dev_s *dev,
                                       rptun_callback_t callback,
                                       FAR void *arg);
static int rptun_bmp_notifier(FAR struct notifier_block *nb,
                              unsigned long action, FAR void *data);

/****************************************************************************
 * Private Data
 ****************************************************************************/

static const struct rptun_ops_s g_rptun_bmp_ops =
{
  .get_cpuname       = rptun_bmp_get_cpuname,
  .get_resource      = rptun_bmp_get_resource,
  .is_autostart      = rptun_bmp_is_autostart,
  .is_master         = rptun_bmp_is_master,
  .start             = rptun_bmp_start,
  .notify            = rptun_bmp_notify,
  .register_callback = rptun_bmp_register_callback,
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static int rptun_bmp_notifier(FAR struct notifier_block *nb,
                              unsigned long action, FAR void *data)
{
  FAR struct rptun_bmp_dev_s *dev;

  dev = container_of(nb, struct rptun_bmp_dev_s, rebootnb);
  dev->rsc->rsc_tbl_hdr.ver = 0;
  return 0;
}

static FAR const char *rptun_bmp_get_cpuname(FAR struct rptun_dev_s *dev)
{
  FAR struct rptun_bmp_dev_s *priv = (FAR struct rptun_bmp_dev_s *)dev;
  return priv->cpuname;
}

static FAR struct resource_table *
rptun_bmp_get_resource(FAR struct rptun_dev_s *dev)
{
  FAR struct rptun_bmp_dev_s *priv = (FAR struct rptun_bmp_dev_s *)dev;

  while (priv->rsc->rsc_tbl_hdr.ver == 0)
    {
      nxsig_usleep(100);
    }

  return &priv->rsc->rsc_tbl_hdr;
}

static bool rptun_bmp_is_autostart(FAR struct rptun_dev_s *dev)
{
  return true;
}

static bool rptun_bmp_is_master(FAR struct rptun_dev_s *dev)
{
  FAR struct rptun_bmp_dev_s *priv = (FAR struct rptun_bmp_dev_s *)dev;
  return priv->master;
}

static int rptun_bmp_start(FAR struct rptun_dev_s *dev)
{
  return 0;
}

static int rptun_bmp_notify(FAR struct rptun_dev_s *dev, uint32_t vqid)
{
  FAR struct rptun_bmp_dev_s *priv = (FAR struct rptun_bmp_dev_s *)dev;

  /* An empty remote_cpu means the peer OS lives in the other security state
   * of the SAME physical cores (REE <-> TEE), so the doorbell belongs to the
   * core we are running on: that is the core whose peer-world context waits
   * for it.  A fixed mask would always poke one core no matter where the
   * sender runs, serialising every notification through that core.
   *
   * Otherwise the peer is a distinct processor with its own core numbering
   * and the mask the board passed is the only valid target.
   */

  up_trigger_irq(priv->irq_trigger,
                 priv->cpuset != 0 ? priv->cpuset : 1 << this_cpu());
  return 0;
}

#ifdef CONFIG_SMP
/****************************************************************************
 * Name: rptun_bmp_cpu_notifier
 *
 * Description:
 *   Bring the doorbell of a core that has just come online into the state
 *   the tunnel asks for.  Runs on that core, which is what makes the
 *   per-core enable land in the right place.
 *
 ****************************************************************************/

static int rptun_bmp_cpu_notifier(FAR struct notifier_block *nb,
                                  unsigned long action, FAR void *data)
{
  FAR struct rptun_bmp_dev_s *priv;

  priv = container_of(nb, struct rptun_bmp_dev_s, cpunb);

  if (priv->callback != NULL)
    {
      up_enable_irq(priv->irq_event);
    }
  else
    {
      up_disable_irq(priv->irq_event);
    }

  return 0;
}
#endif

static int rptun_bmp_register_callback(FAR struct rptun_dev_s *dev,
                                       rptun_callback_t callback,
                                       FAR void *arg)
{
  FAR struct rptun_bmp_dev_s *priv = (FAR struct rptun_bmp_dev_s *)dev;

  priv->callback = callback;
  priv->arg      = arg;

#ifdef CONFIG_SMP
  /* The doorbell SGI is enabled per core: up_enable_irq() writes the
   * calling core's GICR_ISENABLER0 and affects that core only, so every
   * core has to run it for delivery to be independent of which core the
   * sender targets and of which core runs this registration.
   *
   * Subscribing to the CPU chain covers both halves of that:
   * register_cpu_notifier() runs the callback on the cores that are online,
   * and the chain runs it on each core that starts later.  The late cores
   * cannot be served by a broadcast made here, because whether a
   * redistributor keeps an SGI pending while its core is powered down is
   * implementation defined: BES1701 drops it where QEMU's virt GIC latches
   * it.
   */

  register_cpu_notifier(&priv->cpunb);
#else
  if (callback != NULL)
    {
      up_enable_irq(priv->irq_event);
    }
  else
    {
      up_disable_irq(priv->irq_event);
    }
#endif

  return 0;
}

/****************************************************************************
 * Name: rptun_bmp_interrupt
 *
 * Description:
 *   This is the interrupt handler.
 *
 * Input Parameters:
 *   irq      - unused
 *   context  - context, unused
 *   arg      - private data pointer
 *
 * Returned Value:
 *   OK always
 *
 ****************************************************************************/

static int rptun_bmp_interrupt(int irq, FAR void *context, FAR void *arg)
{
  FAR struct rptun_bmp_dev_s *priv = arg;

  if (priv != NULL && priv->callback != NULL)
    {
      priv->callback(priv->arg, RPTUN_NOTIFY_ALL);
    }

  return OK;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int rptun_bmp_init(FAR const char *cpuname, bool master,
                   FAR struct rptun_rsc_s *rsc, int irq_event,
                   int irq_trigger, cpu_set_t remote_cpu)
{
  FAR struct rptun_bmp_dev_s *dev;
  int ret;

  dev = kmm_zalloc(sizeof(*dev));
  if (dev == NULL)
    {
      return -ENOMEM;
    }

  dev->master = master;
  dev->irq_trigger = irq_trigger;
  dev->irq_event = irq_event;
  dev->rptun.ops = &g_rptun_bmp_ops;
  dev->rsc = rsc;
  dev->cpuset = remote_cpu;

  strlcpy(dev->cpuname, cpuname, sizeof(dev->cpuname));

  dev->rebootnb.notifier_call = rptun_bmp_notifier;
  register_reboot_notifier(&dev->rebootnb);

#ifdef CONFIG_SMP
  dev->cpunb.notifier_call = rptun_bmp_cpu_notifier;
#endif

  ret = irq_attach(dev->irq_event,
                   rptun_bmp_interrupt, dev);
  if (ret < 0)
    {
      goto err_irq;
    }

  ret = rptun_initialize(&dev->rptun);
  if (ret < 0)
    {
      goto err_init;
    }

  return ret;

err_init:
  irq_detach(dev->irq_event);
err_irq:
  unregister_reboot_notifier(&dev->rebootnb);
  kmm_free(dev);
  return ret;
}
