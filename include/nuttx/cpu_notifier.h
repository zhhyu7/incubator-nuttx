/****************************************************************************
 * include/nuttx/cpu_notifier.h
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

#ifndef __INCLUDE_NUTTX_CPU_NOTIFIER_H
#define __INCLUDE_NUTTX_CPU_NOTIFIER_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>
#include <nuttx/notifier.h>

#include <sys/types.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* Notify that a CPU has come online.  The notification runs on the CPU it
 * is about, which is what lets a subscriber touch per-CPU hardware such as
 * an interrupt controller banked per core.
 *
 * Registering covers every CPU, not only the ones that arrive afterwards:
 * register_cpu_notifier() calls the new block on each CPU that is online
 * already, so a subscriber sees CPU_ONLINE exactly once per CPU whether it
 * registered before or after that CPU started.
 */

#define CPU_ONLINE      0x0001ul

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

/****************************************************************************
 * Name:  register_cpu_notifier
 *
 * Description:
 *   Add notifier to the CPU notifier chain, then call it with CPU_ONLINE on
 *   every CPU that is online already, each on that CPU.  Blocks until those
 *   calls have run, so it must be called from a context that can wait.
 *
 * Input Parameters:
 *    nb - New entry in notifier chain
 *
 ****************************************************************************/

void register_cpu_notifier(FAR struct notifier_block *nb);

/****************************************************************************
 * Name:  unregister_cpu_notifier
 *
 * Description:
 *   Remove notifier from the CPU notifier chain.  Unlinks only: there is no
 *   teardown notification, because NuttX has no path that takes a CPU back
 *   offline.  A subscriber that must undo per-CPU state does so itself.
 *
 * Input Parameters:
 *    nb - Entry to remove from notifier chain
 *
 ****************************************************************************/

void unregister_cpu_notifier(FAR struct notifier_block *nb);

/****************************************************************************
 * Name:  cpu_notifier_call_chain
 *
 * Description:
 *   Call functions in the CPU notifier chain.
 *
 * Input Parameters:
 *    action - Value passed unmodified to notifier function
 *    data   - Pointer passed unmodified to notifier function
 *
 ****************************************************************************/

void cpu_notifier_call_chain(unsigned long action, FAR void *data);

#endif /* __INCLUDE_NUTTX_CPU_NOTIFIER_H */
