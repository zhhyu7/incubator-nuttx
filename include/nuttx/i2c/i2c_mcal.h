/****************************************************************************
 * include/nuttx/i2c/i2c_mcal.h
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

#ifndef __INCLUDE_NUTTX_I2C_I2C_MCAL_H
#define __INCLUDE_NUTTX_I2C_I2C_MCAL_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include <nuttx/i2c/i2c_master.h>
#include "I2c.h"

/****************************************************************************
 * Public Types
 ****************************************************************************/

/* One sequence/job group. Groups are named by transfer shape. group carries:
 *
 * - seq_single: a single-job sequence (job_single) for one message, read
 *   OR write (plain read, plain write, or a vendor command without a
 *   register address).
 * - seq_wr_rd: a two-job sequence (job_wr + job_rd) with a repeated start,
 *   for a write followed by a read (e.g. a register read of any address
 *   width).
 *
 * The same layout is used for both the synchronous group and each
 * asynchronous pool slot.
 */

struct mcal_i2c_seq_group_s
{
  uint16_t seq_single; /* 1-job sequence id: single read or write message */
  uint16_t seq_wr_rd;  /* 2-job sequence id: write + Sr + read            */
  uint16_t job_single; /* Job of seq_single                               */
  uint16_t job_wr;     /* seq_wr_rd job 0: write phase                    */
  uint16_t job_rd;     /* seq_wr_rd job 1: read phase                     */
};

/* Board-level configuration for one I2C bus.
 *
 * Vendor-neutral by design: it must NOT contain any vendor-specific type.
 * All vendor-specific behaviour (module handle, pin description,
 * DMA channel acquisition, interrupt enable/routing) lives behind the
 * opaque "hw" handle and the irq_setup/irq_teardown/bus_reset hooks,
 * which are implemented in the board / vendor layer.
 */

struct mcal_i2c_config_s
{
  int      bus;        /* I2C bus number -> /dev/i2cN minor */

  /* Synchronous group */

  struct mcal_i2c_seq_group_s sync;

  /* Asynchronous pool. async_pool_size == 0 disables the asynchronous
   * path (sync-only); a non-zero size enables it and sets the maximum
   * number of concurrent in-flight transfers.
   */

  FAR const struct mcal_i2c_seq_group_s  *async_pool;
  uint8_t                                 async_pool_size;

  /* Opaque vendor context and hooks (implemented in board/vendor layer).
   * The generic layer only passes the mcal_i2c device handle to the hooks.
   */

  FAR void *hw;

  /* Enable interrupts / DMA for this bus (vendor-specific). Called during
   * initialization. Returns 0 on success.
   */

  CODE int (*irq_setup)(FAR void *handle);

  /* Disable interrupts / DMA for this bus. */

  CODE void (*irq_teardown)(FAR void *handle);

#ifdef CONFIG_I2C_RESET
  /* Bus recovery by toggling SCL (optional, may be NULL). MCAL provides no
   * reset API, so recovery is a vendor GPIO operation.
   */

  CODE int (*bus_reset)(FAR void *handle);
#endif
};

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

#undef EXTERN
#if defined(__cplusplus)
#define EXTERN extern "C"
extern "C"
{
#else
#define EXTERN extern
#endif

/****************************************************************************
 * Name: mcal_i2c_initialize
 *
 * Description:
 *   Initialize MCAL I2C buses for the current core and register them with
 *   the NuttX I2C framework (/dev/i2cN).
 *
 *   The core is selected by the caller (board): pass I2C config for the
 *   current partition.This function does not perform per-core filtering.
 *
 * Input Parameters:
 *   mcal_config - MCAL I2c config pointer for the current partition
 *                 NULL if this core has no I2C.
 *   cfg         - Array of per-bus board configurations
 *   num         - Number of entries in cfg[]
 *
 * Returned Value:
 *   OK on success; a negated errno value on failure.
 *
 ****************************************************************************/

int mcal_i2c_initialize(FAR const I2c_ConfigType *mcal_config,
                        FAR const struct mcal_i2c_config_s *cfg,
                        size_t num);

/****************************************************************************
 * Name: mcal_i2c_seq_end
 *
 * Description:
 *   Asynchronous sequence completion handler. Called from the board-level
 *   SeqEndNotification callback wrapper when an async sequence finishes.
 *   Looks up the owning bus/slot by sequence id, invokes the stored NuttX
 *   callback and releases the in-flight slot.
 *
 * Input Parameters:
 *   seq - MCAL sequence id that just completed
 *
 ****************************************************************************/

void mcal_i2c_seq_end(uint16_t seq);

/****************************************************************************
 * Name: mcal_i2c_get_config
 *
 * Description:
 *   Return the board configuration bound to a mcal_i2c device handle.
 *   Helper for vendor irq_setup/bus_reset hooks so they can reach the
 *   opaque hw context (which holds the HWUnit index and vendor-specific
 *   irq/DMA routing) without touching the private structure layout.
 *
 ****************************************************************************/

FAR const struct mcal_i2c_config_s *mcal_i2c_get_config(FAR void *handle);

#undef EXTERN
#if defined(__cplusplus)
}
#endif

#endif /* __INCLUDE_NUTTX_I2C_I2C_MCAL_H */
