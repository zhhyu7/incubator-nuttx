/****************************************************************************
 * arch/arm/src/qemu-m/qemu_m_serial.c
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

#include <nuttx/serial/uart_pl011.h>

#include "arm_internal.h"

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: arm_serialinit
 *
 * Description:
 *   Register the serial console and serial ports.  virt-m uses the generic
 *   PL011 driver (same as the A/R configs); only the base/IRQ
 *   differ, and those come from the driver's own CONFIG_UARTn_PL011_*.
 *
 ****************************************************************************/

void arm_serialinit(void)
{
  pl011_serialinit();
}

#if USE_EARLYSERIALINIT

/****************************************************************************
 * Name: arm_earlyserialinit
 *
 * Description:
 *   Performs the low level UART initialization early so the serial console
 *   is available during bootup.  Must be called before arm_serialinit.
 *
 ****************************************************************************/

void arm_earlyserialinit(void)
{
  pl011_earlyserialinit();
}
#endif
