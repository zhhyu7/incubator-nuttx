/****************************************************************************
 * drivers/fault/wraps/fault_wrap_i2c.c
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

#include <sys/types.h>
#include <stdint.h>
#include <errno.h>

#include <nuttx/i2c/i2c_master.h>

#include <nuttx/fault_inject.h>

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

/* The real I2C convenience helpers.  i2c_read()/i2c_write()/i2c_writeread()
 * are genuine global functions (drivers/i2c/i2c_{read,write,writeread}.c),
 * not vtable ops, so ld --wrap can intercept every in-image caller and
 * redirect it to the __wrap_* variants below; the originals remain
 * reachable via __real_*.
 */

int __real_i2c_read(FAR struct i2c_master_s *dev,
                    FAR const struct i2c_config_s *config,
                    FAR uint8_t *buffer, int buflen);
int __real_i2c_write(FAR struct i2c_master_s *dev,
                     FAR const struct i2c_config_s *config,
                     FAR const uint8_t *buffer, int buflen);
int __real_i2c_writeread(FAR struct i2c_master_s *dev,
                         FAR const struct i2c_config_s *config,
                         FAR const uint8_t *wbuffer, int wbuflen,
                         FAR uint8_t *rbuffer, int rbuflen);

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: __wrap_i2c_read
 ****************************************************************************/

int __wrap_i2c_read(FAR struct i2c_master_s *dev,
                    FAR const struct i2c_config_s *config,
                    FAR uint8_t *buffer, int buflen)
{
  int ret;
  FAULT_INJECT_WRAP_RETVAL(i2c, i2c_read, -EIO,
                           __real_i2c_read(dev, config, buffer, buflen),
                           &ret);
  return ret;
}

/****************************************************************************
 * Name: __wrap_i2c_write
 ****************************************************************************/

int __wrap_i2c_write(FAR struct i2c_master_s *dev,
                     FAR const struct i2c_config_s *config,
                     FAR const uint8_t *buffer, int buflen)
{
  int ret;
  FAULT_INJECT_WRAP_RETVAL(i2c, i2c_write, -EIO,
                           __real_i2c_write(dev, config, buffer, buflen),
                           &ret);
  return ret;
}

/****************************************************************************
 * Name: __wrap_i2c_writeread
 ****************************************************************************/

int __wrap_i2c_writeread(FAR struct i2c_master_s *dev,
                         FAR const struct i2c_config_s *config,
                         FAR const uint8_t *wbuffer, int wbuflen,
                         FAR uint8_t *rbuffer, int rbuflen)
{
  int ret;
  FAULT_INJECT_WRAP_RETVAL(i2c, i2c_writeread, -EIO,
                           __real_i2c_writeread(dev, config, wbuffer,
                                                wbuflen, rbuffer, rbuflen),
                           &ret);
  return ret;
}
