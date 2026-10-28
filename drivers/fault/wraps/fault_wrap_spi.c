/****************************************************************************
 * drivers/fault/wraps/fault_wrap_spi.c
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

#include <errno.h>

#include <nuttx/spi/spi_transfer.h>

#include <nuttx/fault_inject.h>

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

/* The real SPI transfer implementation.  spi_transfer() is a genuine global
 * function (drivers/spi/spi_transfer.c), not a vtable op, so ld --wrap can
 * intercept every in-image caller and redirect it to __wrap_spi_transfer();
 * the original definition remains reachable via __real_spi_transfer().
 */

int __real_spi_transfer(FAR struct spi_dev_s *spi,
                        FAR struct spi_sequence_s *seq);

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: __wrap_spi_transfer
 ****************************************************************************/

int __wrap_spi_transfer(FAR struct spi_dev_s *spi,
                        FAR struct spi_sequence_s *seq)
{
  int ret;
  FAULT_INJECT_WRAP_RETVAL(spi, spi_transfer, -EIO,
                           __real_spi_transfer(spi, seq), &ret);
  return ret;
}
