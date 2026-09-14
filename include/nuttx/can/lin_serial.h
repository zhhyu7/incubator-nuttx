/****************************************************************************
 * include/nuttx/can/lin_serial.h
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

#ifndef __INCLUDE_NUTTX_CAN_LIN_SERIAL_H
#define __INCLUDE_NUTTX_CAN_LIN_SERIAL_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdbool.h>
#include <stdint.h>

#ifdef CONFIG_LIN_SERIAL

/****************************************************************************
 * Public Types
 ****************************************************************************/

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
 * Name: lin_serial_register
 *
 * Description:
 *   Register a LIN-over-tty network interface.  The driver is media
 *   agnostic: the caller (board/bringup logic) supplies the serial device
 *   node that carries the LIN traffic.  A SocketCAN compatible netdev
 *   named "lin<ifno>" is created.
 *
 *   The interface starts with classic checksum.  The master/slave role and
 *   echo behaviour are fixed at registration time.  Baud rate can be
 *   changed at run time via ioctl (SIOCSCANBITRATE) before calling ifup.
 *
 * Input Parameters:
 *   devpath - Path to the serial device node backing this LIN interface.
 *   ifno    - Interface number, used to form the netdev name "lin%d".
 *   baud    - Initial baud rate applied to the backing tty (e.g. 19200).
 *             0 keeps the tty's current setting.  On a virtual serial
 *             port the value is a harmless no-op.
 *   master  - true for master role, false for slave role.
 *   echo    - true if the UART echoes TX bytes back on the RX line
 *             (standard LIN single-wire transceiver), false otherwise.
 *
 * Returned Value:
 *   OK on success; a negated errno value on failure.
 *
 ****************************************************************************/

int lin_serial_register(FAR const char *devpath, int ifno, uint32_t baud,
                        bool master, bool echo);

/****************************************************************************
 * Name: lin_serial_unregister
 *
 * Description:
 *   Unregister a LIN-over-tty network interface previously created by
 *   lin_serial_register() and release all associated resources.  The
 *   interface must be down (ifdown) before calling this function.
 *
 * Input Parameters:
 *   ifno - Interface number passed to lin_serial_register() (the netdev
 *          name is "lin<ifno>").
 *
 * Returned Value:
 *   OK on success; a negated errno value on failure.
 *
 ****************************************************************************/

int lin_serial_unregister(int ifno);

#undef EXTERN
#if defined(__cplusplus)
}
#endif

#endif /* CONFIG_LIN_SERIAL */
#endif /* __INCLUDE_NUTTX_CAN_LIN_SERIAL_H */
