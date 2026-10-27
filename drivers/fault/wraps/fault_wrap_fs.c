/****************************************************************************
 * drivers/fault/wraps/fault_wrap_fs.c
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
#include <sys/uio.h>
#include <errno.h>

#include <nuttx/fs/fs.h>

#include <nuttx/fault_inject.h>

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

/* struct inode_search_s is declared in the internal fs/inode/inode.h, which
 * is not on the public include path.  A file-scope forward declaration makes
 * the __real_/__wrap_ prototypes agree on a single incomplete type (only a
 * pointer to it is ever passed through, so the definition is not needed).
 */

struct inode_search_s;

/* The real fs/VFS implementations.  When ld --wrap redirects the in-image
 * callers of file_readv() etc. to __wrap_file_readv(), the original
 * definitions are reachable via __real_file_readv().
 *
 * open series (nx_open / file_open) are intentionally NOT wrapped: both are
 * variadic (va_list) functions and ld --wrap cannot forward a va_list
 * correctly.  File-open failure paths are covered indirectly via the
 * inode_find (RETURN_ERR) wrapper or the mm allocation wrappers.
 */

ssize_t __real_file_readv(FAR struct file *filep,
                          FAR const struct iovec *iov, int iovcnt);
ssize_t __real_file_writev(FAR struct file *filep,
                           FAR const struct iovec *iov, int iovcnt);
int     __real_register_driver(FAR const char *path,
                               FAR const struct file_operations *fops,
                               mode_t mode, FAR void *priv);
int     __real_inode_find(FAR struct inode_search_s *desc);

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: __wrap_file_readv
 ****************************************************************************/

ssize_t __wrap_file_readv(FAR struct file *filep,
                          FAR const struct iovec *iov, int iovcnt)
{
  ssize_t ret;
  FAULT_INJECT_WRAP_RETVAL_SSIZE(fs, file_readv, -EIO,
                                 __real_file_readv(filep, iov, iovcnt),
                                 &ret);
  return ret;
}

/****************************************************************************
 * Name: __wrap_file_writev
 ****************************************************************************/

ssize_t __wrap_file_writev(FAR struct file *filep,
                           FAR const struct iovec *iov, int iovcnt)
{
  ssize_t ret;
  FAULT_INJECT_WRAP_RETVAL_SSIZE(fs, file_writev, -EIO,
                                 __real_file_writev(filep, iov, iovcnt),
                                 &ret);
  return ret;
}

/****************************************************************************
 * Name: __wrap_register_driver
 ****************************************************************************/

int __wrap_register_driver(FAR const char *path,
                           FAR const struct file_operations *fops,
                           mode_t mode, FAR void *priv)
{
  int ret;
  FAULT_INJECT_WRAP_RETVAL(fs, register_driver, -ENOMEM,
                           __real_register_driver(path, fops, mode, priv),
                           &ret);
  return ret;
}

/****************************************************************************
 * Name: __wrap_inode_find
 ****************************************************************************/

int __wrap_inode_find(FAR struct inode_search_s *desc)
{
  int ret;
  FAULT_INJECT_WRAP_RETVAL(fs, inode_find, -ENOENT,
                           __real_inode_find(desc), &ret);
  return ret;
}
