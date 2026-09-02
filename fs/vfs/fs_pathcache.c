/****************************************************************************
 * fs/vfs/fs_pathcache.c
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
#include <fcntl.h>
#include <limits.h>
#include <stdbool.h>
#include <string.h>

#include <nuttx/fs/fs.h>
#include <nuttx/lib/lib.h>
#include <nuttx/lru_cache.h>
#include <nuttx/nuttx.h>

#include "inode/inode.h"
#include "vfs.h"

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct pathcache_match_s
{
  FAR const char *path;
  size_t len;
};

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static void pathcache_get(FAR void *value);
static void pathcache_put(FAR void *value);
static bool pathcache_try_get(FAR void *value);

/****************************************************************************
 * Private Data
 ****************************************************************************/

static struct lru_cache_s g_pathcache;
static const struct lru_cache_ops_s g_pathcache_ops =
{
  .get_cb     = pathcache_get,
  .put_cb     = pathcache_put,
  .try_get_cb = pathcache_try_get,
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: pathcache_cacheable
 *
 * Description:
 *   Check whether the requested flags can reuse an idle cached file.  Only
 *   read-only opens are reusable.  O_CLOEXEC is descriptor-local and O_CREAT
 *   changes cache-miss behavior.  O_APPEND is left to the file system, which
 *   decides where a read-only append open starts.
 ****************************************************************************/

static bool pathcache_cacheable(int oflags)
{
  const int supported = O_ACCMODE | O_CLOEXEC | O_CREAT | O_DIRECTORY;

  return (oflags & O_ACCMODE) == O_RDONLY &&
         (oflags & ~supported) == 0;
}

/****************************************************************************
 * Name: pathcache_mountpoint
 ****************************************************************************/

static FAR struct inode *pathcache_mountpoint(FAR struct file *filep)
{
  if ((filep->f_oflags & O_DIRECTORY) != 0)
    {
      FAR struct fs_dirent_s *dir = filep->f_priv;
      return dir == NULL ? NULL : dir->fd_root;
    }

  return filep->f_inode;
}

/****************************************************************************
 * Name: pathcache_get
 *
 * Description:
 *   Increase reference count of a cached file entry.
 *
 * Input Parameters:
 *   value - Pointer to the cached file structure
 *
 ****************************************************************************/

static void pathcache_get(FAR void *value)
{
  FAR struct file *filep = value;
  atomic_add(&filep->f_refs, 1);
}

/****************************************************************************
 * Name: pathcache_put
 *
 * Description:
 *   Decrease reference count of a cached file entry.
 *
 * Input Parameters:
 *   value - Pointer to the cached file structure
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

static void pathcache_put(FAR void *value)
{
  FAR struct file *filep = value;
  FAR struct inode *inode;
  int ret = OK;

  if (atomic_sub(&filep->f_refs, 1) != 1)
    {
      return;
    }

  inode = filep->f_inode;
  if (inode != NULL && inode->u.i_ops != NULL &&
      inode->u.i_ops->close != NULL)
    {
      ret = inode->u.i_ops->close(filep);
      if (ret < 0)
        {
          ferr("ERROR: Failed to close cached file: %d\n", ret);
        }
    }

  if (inode != NULL)
    {
      inode_release(inode);
    }

  file_deallocate(filep);
}

/****************************************************************************
 * Name: pathcache_try_get
 *
 * Description:
 *   Reserve a cached file only while the cache holds its sole reference.
 *   An idle file has no descriptor pointing at it, so the cache lock held
 *   by the caller is the only way to reach it and the reservation cannot
 *   race with a close.
 ****************************************************************************/

static bool pathcache_try_get(FAR void *value)
{
  FAR struct file *filep = value;
  int32_t idle = 1;

  return atomic_cmpxchg(&filep->f_refs, &idle, 2);
}

/****************************************************************************
 * Name: pathcache_is_match
 *
 * Description:
 *   Match callback to check if a cache entry belongs to the unmounting
 *   mountpoint.
 *
 * Input Parameters:
 *   cache  - Pointer to the cache structure (unused)
 *   key    - The cache key (unused)
 *   keylen - Length of the cache key (unused)
 *   value  - Pointer to the cached file structure
 *   arg    - Pointer to the unmounting mountpoint inode
 *
 * Returned Value:
 *   1 if entry should be removed; 0 otherwise
 *
 ****************************************************************************/

static int pathcache_is_match(FAR struct lru_cache_s *cache,
                              FAR const void *key, size_t keylen,
                              FAR void *value, FAR void *arg)
{
  /* Check if this file belongs to the unmounting mountpoint */

  return pathcache_mountpoint(value) == arg;
}

/****************************************************************************
 * Name: pathcache_is_child
 *
 * Description:
 *   Match everything below a path, excluding the path itself.
 *
 ****************************************************************************/

static int pathcache_is_child(FAR struct lru_cache_s *cache,
                              FAR const void *key, size_t keylen,
                              FAR void *value, FAR void *arg)
{
  FAR struct pathcache_match_s *match = arg;
  FAR const char *path = key;

  if (keylen <= match->len ||
      memcmp(path, match->path, match->len) != 0)
    {
      return 0;
    }

  return path[match->len] == '/';
}

/****************************************************************************
 * Name: pathcache_strip
 *
 * Description:
 *   Turn a canonical path into its parent directory, in place.
 *
 ****************************************************************************/

static bool pathcache_strip(FAR char *path, FAR size_t *len)
{
  FAR char *slash = memrchr(path, '/', *len);

  if (slash == NULL)
    {
      return false;
    }

  if (slash == path)
    {
      path[1] = '\0';
      *len = 1;
    }
  else
    {
      *slash = '\0';
      *len = slash - path;
    }

  return true;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: pathcache_find
 *
 * Description:
 *   Reserve an idle cached struct file for an fd-based open.  The cache
 *   and lookup references make f_refs equal to two until
 *   pathcache_unreserve() drops the lookup reservation.  Files and
 *   directories share one keyspace, so a kind mismatch is a miss.
 ****************************************************************************/

FAR struct file *pathcache_find(FAR const char *path, int oflags)
{
  FAR struct file *filep = NULL;
  FAR char *pathbuffer;
  FAR char *abspath;

  if (path == NULL || !pathcache_cacheable(oflags))
    {
      return NULL;
    }

  /* Get the canonical absolute path. */

  pathbuffer = lib_get_tempbuffer(PATH_MAX);
  if (pathbuffer == NULL)
    {
      return NULL;
    }

  abspath = lib_realpath(path, pathbuffer, true);
  if (abspath != NULL)
    {
      filep = lru_cache_lookup(&g_pathcache, abspath, strlen(abspath) + 1);

      /* Still unreachable from any descriptor, so dropping it is safe. */

      if (filep != NULL && (oflags & O_DIRECTORY) != 0 &&
          (filep->f_oflags & O_DIRECTORY) == 0)
        {
          pathcache_put(filep);
          filep = NULL;
        }
    }

  lib_put_tempbuffer(pathbuffer);
  return filep;
}

/****************************************************************************
 * Name: pathcache_insert
 *
 * Description:
 *   Add the opened struct file after its fd reference has been installed.
 ****************************************************************************/

void pathcache_insert(FAR const char *path, FAR struct file *filep)
{
  FAR struct inode *inode;
  FAR char *pathbuffer;
  FAR char *abspath;

  DEBUGASSERT(filep != NULL);

  inode = pathcache_mountpoint(filep);
  if (path == NULL || inode == NULL || !INODE_IS_PATHCACHE(inode) ||
      !pathcache_cacheable(filep->f_oflags))
    {
      return;
    }

  if ((filep->f_oflags & O_DIRECTORY) != 0)
    {
      if (inode->u.i_mops == NULL || inode->u.i_mops->rewinddir == NULL)
        {
          return;
        }
    }
  else if (inode->u.i_mops == NULL || inode->u.i_mops->seek == NULL)
    {
      return;
    }

  pathbuffer = lib_get_tempbuffer(PATH_MAX);
  if (pathbuffer == NULL)
    {
      return;
    }

  abspath = lib_realpath(path, pathbuffer, true);
  if (abspath != NULL)
    {
      filep->f_oflags &= ~O_CLOEXEC;

      lru_cache_insert(&g_pathcache, abspath, strlen(abspath) + 1,
                       filep);
    }

  lib_put_tempbuffer(pathbuffer);
}

/****************************************************************************
 * Name: pathcache_unreserve
 *
 * Description:
 *   Drop the lookup reservation acquired by pathcache_find().
 ****************************************************************************/

void pathcache_unreserve(FAR struct file *filep)
{
  DEBUGASSERT(filep != NULL);

  pathcache_put(filep);
}

/****************************************************************************
 * Name: pathcache_release
 *
 * Description:
 *   Drop one reference to a file on a pathcache mount.  Cleanup and close
 *   notification of the last non-cache reference complete before the
 *   reference count exposes the object as idle again.  Returns true when
 *   the reference has been consumed; false leaves it to the caller.
 ****************************************************************************/

bool pathcache_release(FAR struct file *filep)
{
  FAR struct inode *inode;
  FAR char *pathbuffer;
  FAR char *abspath;
  FAR char *path;
  bool released = false;
  int32_t refs;

  DEBUGASSERT(filep != NULL);

  /* Only read-only files are cached, and fcntl() cannot change that. */

  inode = pathcache_mountpoint(filep);
  if (inode == NULL || !INODE_IS_PATHCACHE(inode) ||
      (filep->f_oflags & O_ACCMODE) != O_RDONLY)
    {
      return false;
    }

  /* Deciding who is last and dropping the reference must be one CAS, or
   * two concurrent puts can both see a busy count and skip the release.
   * At two, pin the file instead, so eviction cannot free it under us and
   * pathcache_find() sees it busy.
   */

  for (; ; )
    {
      refs = atomic_read(&filep->f_refs);
      if (refs < 2)
        {
          return false;
        }

      if (atomic_cmpxchg(&filep->f_refs, &refs, refs == 2 ? 3 : refs - 1))
        {
          break;
        }
    }

  if (refs > 2)
    {
      return true;
    }

  path = lib_get_tempbuffer(PATH_MAX);
  if (path != NULL && file_fcntl(filep, F_GETPATH, path) < 0)
    {
      lib_put_tempbuffer(path);
      path = NULL;
    }

  if (path == NULL)
    {
      goto out_with_pin;
    }

  pathbuffer = lib_get_tempbuffer(PATH_MAX);
  if (pathbuffer == NULL)
    {
      goto out_with_path;
    }

  abspath = lib_realpath(path, pathbuffer, true);
  if (abspath == NULL)
    {
      goto out_with_pathbuffer;
    }

  /* A file shared by two descriptors outside the cache also sits at two. */

  if (!lru_cache_contains(&g_pathcache, abspath, strlen(abspath) + 1,
                          filep))
    {
      goto out_with_pathbuffer;
    }

  if (!pathcache_cacheable(filep->f_oflags))
    {
      lru_cache_remove(&g_pathcache, abspath, strlen(abspath) + 1);
      goto out_with_pathbuffer;
    }

  file_closelk(filep);
#ifdef CONFIG_FS_NOTIFY
  notify_close(path, filep->f_oflags);
#endif

  released = true;
  pathcache_put(filep);
  pathcache_put(filep);

out_with_pathbuffer:
  lib_put_tempbuffer(pathbuffer);
out_with_path:
  lib_put_tempbuffer(path);
out_with_pin:
  if (!released)
    {
      pathcache_put(filep);
    }

  return released;
}

/****************************************************************************
 * Name: pathcache_remove
 *
 * Description:
 *   Remove the selected entries before an operation changes the file system
 *   namespace.
 *
 ****************************************************************************/

void pathcache_remove(FAR const char *path, int flags)
{
  struct pathcache_match_s match;
  FAR char *pathbuffer;
  FAR char *abspath;
  size_t len;

  DEBUGASSERT(path != NULL);

  pathbuffer = lib_get_tempbuffer(PATH_MAX);
  if (pathbuffer == NULL)
    {
      return;
    }

  abspath = lib_realpath(path, pathbuffer, true);
  if (abspath == NULL)
    {
      goto out;
    }

  len = strlen(abspath);

  if ((flags & PATHCACHE_SUBTREE) != 0)
    {
      match.path = abspath;
      match.len = len;
      lru_cache_remove_if(&g_pathcache, pathcache_is_child, &match);
    }

  if ((flags & PATHCACHE_SELF) != 0)
    {
      lru_cache_remove(&g_pathcache, abspath, len + 1);
    }

  if ((flags & PATHCACHE_PARENT) != 0 && pathcache_strip(abspath, &len))
    {
      lru_cache_remove(&g_pathcache, abspath, len + 1);
    }

out:
  lib_put_tempbuffer(pathbuffer);
}

/****************************************************************************
 * Name: pathcache_error
 *
 * Description:
 *   Remove a cached object after an operation reports an error that may make
 *   the reusable lower file state unreliable.
 *
 ****************************************************************************/

void pathcache_error(FAR struct file *filep, int errcode)
{
  FAR struct inode *inode;
  FAR char *path;

  DEBUGASSERT(filep != NULL);

  if (errcode == -ENOSYS || errcode == -ENOTTY)
    {
      return;
    }

  inode = pathcache_mountpoint(filep);
  if (inode == NULL || !INODE_IS_PATHCACHE(inode))
    {
      return;
    }

  path = lib_get_tempbuffer(PATH_MAX);
  if (path != NULL && file_fcntl(filep, F_GETPATH, path) < 0)
    {
      lib_put_tempbuffer(path);
      path = NULL;
    }

  if (path != NULL)
    {
      pathcache_remove(path, PATHCACHE_SELF);
      lib_put_tempbuffer(path);
    }
}

/****************************************************************************
 * Name: pathcache_umount
 *
 * Description:
 *   Invalidate all cache entries belonging to a specific mountpoint
 *
 * Input Parameters:
 *   mountpt - Mountpoint inode being unmounted
 *
 ****************************************************************************/

void pathcache_umount(FAR struct inode *mountpt)
{
  lru_cache_remove_if(&g_pathcache, pathcache_is_match, mountpt);
}

/****************************************************************************
 * Name: pathcache_initialize
 *
 * Description:
 *   Initialize the pathcache
 *
 * Input Parameters:
 *   None
 *
 ****************************************************************************/

void pathcache_initialize(void)
{
  lru_cache_init(&g_pathcache, CONFIG_FS_PATHCACHE_MAX_ENTRIES,
                 CONFIG_FS_PATHCACHE_HASH_SIZE, &g_pathcache_ops);
}
