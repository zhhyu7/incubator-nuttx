/****************************************************************************
 * drivers/fault/wraps/fault_wraps.h
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
 * Unified fault wrapper symbol list.
 *
 * All sub-module wrap symbols are collected here, guarded by their
 * respective CONFIG_DRIVERS_FAULT_INJECT_WRAP_XXX macros.  The build
 * system only needs to preprocess this single file to produce one
 * fault_wrapsymbol.cmd — no per-module .cmd files required.
 *
 * When adding a new fault wrapper module:
 *   1. Add a new #ifdef CONFIG_DRIVERS_FAULT_INJECT_WRAP_XXX section below
 *   2. List the symbols with UP_WRAPOPT(symbol)
 *   3. Add CONFIG option in drivers/fault/Kconfig
 *   4. Add source file in drivers/fault/wraps/ and update Make.defs/CMake
 *   No changes to tools/Config.mk, tools/Unix.mk, or root CMakeLists.txt
 *   are needed.
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#ifndef UP_WRAPOPT
#define UP_WRAPOPT(f) --wrap=f
#endif

WRAPOPTSTARTS

#ifdef CONFIG_DRIVERS_FAULT_INJECT_WRAP_MM
UP_WRAPOPT(mm_malloc)
UP_WRAPOPT(mm_free)
UP_WRAPOPT(mm_realloc)
UP_WRAPOPT(mm_calloc)
UP_WRAPOPT(mm_zalloc)
UP_WRAPOPT(mm_memalign)
#endif

#ifdef CONFIG_DRIVERS_FAULT_INJECT_WRAP_IRQ
UP_WRAPOPT(irq_attach)
UP_WRAPOPT(irq_attach_thread)
UP_WRAPOPT(irq_attach_wqueue)
UP_WRAPOPT(up_enable_irq)
UP_WRAPOPT(up_disable_irq)
#endif

#ifdef CONFIG_DRIVERS_FAULT_INJECT_WRAP_FS
UP_WRAPOPT(file_readv)
UP_WRAPOPT(file_writev)
UP_WRAPOPT(register_driver)
UP_WRAPOPT(inode_find)
#endif

#ifdef CONFIG_DRIVERS_FAULT_INJECT_WRAP_SEM
UP_WRAPOPT(nxsem_wait_slow)
UP_WRAPOPT(nxsem_trywait_slow)
UP_WRAPOPT(nxsem_post_slow)
UP_WRAPOPT(nxsem_tickwait_slow)
UP_WRAPOPT(nxsem_clockwait_slow)
#endif

#ifdef CONFIG_DRIVERS_FAULT_INJECT_WRAP_MQ
UP_WRAPOPT(nxmq_send)
UP_WRAPOPT(nxmq_receive)
UP_WRAPOPT(file_mq_send)
UP_WRAPOPT(file_mq_receive)
#endif

#ifdef CONFIG_DRIVERS_FAULT_INJECT_WRAP_WQ
UP_WRAPOPT(work_queue)
UP_WRAPOPT(work_queue_wq)
UP_WRAPOPT(work_cancel)
#endif

#ifdef CONFIG_DRIVERS_FAULT_INJECT_WRAP_SPI
UP_WRAPOPT(spi_transfer)
#endif

#ifdef CONFIG_DRIVERS_FAULT_INJECT_WRAP_I2C
UP_WRAPOPT(i2c_read)
UP_WRAPOPT(i2c_write)
UP_WRAPOPT(i2c_writeread)
#endif

#ifdef CONFIG_DRIVERS_FAULT_INJECT_WRAP_CAN
UP_WRAPOPT(can_receive)
UP_WRAPOPT(can_txready)
#endif

#ifdef CONFIG_DRIVERS_FAULT_INJECT_WRAP_CLK
UP_WRAPOPT(clk_enable)
UP_WRAPOPT(clk_set_rate)
#endif

#ifdef CONFIG_DRIVERS_FAULT_INJECT_WRAP_RESET
UP_WRAPOPT(reset_control_reset_reason)
UP_WRAPOPT(reset_control_assert_reason)
UP_WRAPOPT(reset_control_deassert)
#endif
