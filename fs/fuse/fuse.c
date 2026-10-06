/****************************************************************************
 * fs/fuse/fuse.c
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

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <string.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <sys/statfs.h>

#include <nuttx/fs/fs.h>
#include <nuttx/fs/ioctl.h>
#include <nuttx/fs/uio.h>

#include "inode/inode.h"
#include "fs_heap.h"
#include "fuse.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#ifdef CONFIG_FS_LARGEFILE
#  define OFFSET_MAX INT64_MAX
#else
#  define OFFSET_MAX INT32_MAX
#endif

/* Number of scatter/gather segments in a single FUSE_READ/FUSE_WRITE
 * request.  For writes slot 0 carries the fuse_write_in header, so one
 * segment less is available for caller data.
 */

#define FUSE_IOV_SEGS 8

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct fuse_dir_s
{
  struct fs_dirent_s base;
  uint64_t           fh;
  uint64_t           nodeid;
  uint64_t           offset;
};

struct fuse_file_s
{
  uint64_t fh;
  uint64_t nodeid;
  atomic_t refs;
  char     relpath[];
};

struct fuse_poll_s
{
  struct list_node   node;
  FAR struct file   *file;
  FAR struct pollfd *fds;
  uint32_t           kh;
};

struct fuse_req_init_s
{
  struct fuse_req_s    req;
  struct fuse_init_in  iin;
  struct fuse_init_out iout;
  struct iovec         vin[1];
  struct iovec         vout[1];
};

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static int     fuse_open(FAR struct file *filep, FAR const char *relpath,
                         int oflags, mode_t mode);
static int     fuse_close(FAR struct file *filep);
static ssize_t fuse_readv(FAR struct file *filep, FAR struct uio *uio);
static ssize_t fuse_writev(FAR struct file *filep, FAR struct uio *uio);
static off_t   fuse_seek(FAR struct file *filep, off_t offset, int whence);
static int     fuse_ioctl(FAR struct file *filep, int cmd,
                          unsigned long arg);
static int     fuse_truncate(FAR struct file *filep, off_t length);
static int     fuse_poll(FAR struct file *filep,
                         FAR struct pollfd *fds, bool setup);
static int     fuse_sync(FAR struct file *filep);
static int     fuse_dup(FAR const struct file *oldp, FAR struct file *newp);
static int     fuse_fstat(FAR const struct file *filep,
                          FAR struct stat *buf);
static int     fuse_fchstat(FAR const struct file *filep,
                            FAR const struct stat *buf, int flags);
static int     fuse_opendir(FAR struct inode *mountpt,
                            FAR const char *relpath,
                            FAR struct fs_dirent_s **dir);
static int     fuse_closedir(FAR struct inode *mountpt,
                             FAR struct fs_dirent_s *dir);
static int     fuse_readdir(FAR struct inode *mountpt,
                            FAR struct fs_dirent_s *dir,
                            FAR struct dirent *entry);
static int     fuse_rewinddir(FAR struct inode *mountpt,
                              FAR struct fs_dirent_s *dir);
static int     fuse_bind(FAR struct inode *blkdriver, FAR const void *data,
                         FAR void **handle);
static int     fuse_unbind(FAR void *handle, FAR struct inode **blkdriver,
                           unsigned int flags);
static int     fuse_statfs(FAR struct inode *mountpt,
                           FAR struct statfs *buf);
static int     fuse_unlink(FAR struct inode *mountpt,
                           FAR const char *relpath);
static int     fuse_mkdir(FAR struct inode *mountpt,
                          FAR const char *relpath, mode_t mode);
static int     fuse_rmdir(FAR struct inode *mountpt,
                          FAR const char *relpath);
static int     fuse_rename(FAR struct inode *mountpt,
                           FAR const char *oldrelpath,
                           FAR const char *newrelpath);
static int     fuse_stat(FAR struct inode *mountpt,
                         FAR const char *relpath, FAR struct stat *buf);
static int     fuse_chstat(FAR struct inode *mountpt,
                           FAR const char *relpath,
                           FAR const struct stat *buf, int flags);
static int     fuse_syncfs(FAR struct inode *mountpt);
#ifdef CONFIG_FS_LINKS
static int     fuse_link(FAR struct inode *mountpt,
                         FAR const char *relpath1,
                         FAR const char *relpath2);
static int     fuse_symlink(FAR struct inode *mountpt,
                            FAR const char *path1,
                            FAR const char *relpath2);
static ssize_t fuse_readlink(FAR struct inode *mountpt,
                             FAR const char *relpath,
                             FAR char *buf, size_t buflen);
static int     fuse_lstat(FAR struct inode *mountpt,
                          FAR const char *relpath, FAR struct stat *buf);
#endif

/****************************************************************************
 * Public Data
 ****************************************************************************/

const struct mountpt_operations g_fuse_operations =
{
  fuse_open,      /* open */
  fuse_close,     /* close */
  NULL,           /* read */
  NULL,           /* write */
  fuse_seek,      /* seek */
  fuse_ioctl,     /* ioctl */
  fuse_truncate,  /* truncate */
  fuse_poll,      /* poll */
  fuse_readv,     /* readv */
  fuse_writev,    /* writev */
#ifdef CONFIG_FS_FILEMAP
  NULL,           /* mmap */
#endif
  fuse_sync,      /* sync */
  fuse_dup,       /* dup */
  fuse_fstat,     /* fstat */
  fuse_fchstat,   /* fchstat */
  fuse_opendir,   /* opendir */
  fuse_closedir,  /* closedir */
  fuse_readdir,   /* readdir */
  fuse_rewinddir, /* rewinddir */
  fuse_bind,      /* bind */
  fuse_unbind,    /* unbind */
  fuse_statfs,    /* statfs */
  fuse_unlink,    /* unlink */
  fuse_mkdir,     /* mkdir */
  fuse_rmdir,     /* rmdir */
  fuse_rename,    /* rename */
  fuse_stat,      /* stat */
  fuse_chstat,    /* chstat */
  fuse_syncfs,    /* syncfs */
#ifdef CONFIG_FS_LINKS
  fuse_link,      /* link */
  fuse_symlink,   /* symlink */
  fuse_readlink,  /* readlink */
  fuse_lstat,     /* lstat */
#endif
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: fuse_is_root
 ****************************************************************************/

static inline bool fuse_is_root(FAR const char *relpath)
{
  return relpath[0] == '\0' || (relpath[0] == '/' && relpath[1] == '\0');
}

/****************************************************************************
 * Name: fuse_attr_to_stat
 ****************************************************************************/

static void fuse_attr_to_stat(FAR const struct fuse_attr *attr,
                              FAR struct stat *st)
{
  memset(st, 0, sizeof(*st));

  st->st_ino     = attr->ino;
  st->st_size    = attr->size;
  st->st_blocks  = attr->blocks;
  st->st_blksize = attr->blksize;
  st->st_mode    = attr->mode;
  st->st_nlink   = attr->nlink;
  st->st_uid     = attr->uid;
  st->st_gid     = attr->gid;
  st->st_rdev    = attr->rdev;

  st->st_atim.tv_sec  = attr->atime;
  st->st_atim.tv_nsec = attr->atimensec;
  st->st_mtim.tv_sec  = attr->mtime;
  st->st_mtim.tv_nsec = attr->mtimensec;
  st->st_ctim.tv_sec  = attr->ctime;
  st->st_ctim.tv_nsec = attr->ctimensec;
}

/****************************************************************************
 * Name: fuse_req_done
 ****************************************************************************/

static void fuse_req_done(FAR struct fuse_req_s *req)
{
  nxsem_post((FAR sem_t *)req->arg);
}

/****************************************************************************
 * Name: fuse_request_oneway
 ****************************************************************************/

static int fuse_request_oneway(FAR struct fuse_conn_s *conn,
                               FAR struct fuse_req_s *req,
                               FAR const struct iovec *vin, size_t vin_cnt,
                               FAR const struct iovec *vout, size_t vout_cnt)
{
  bool wait = false;
  sem_t done;
  size_t i;
  int ret;

  if (req->hin.opcode != FUSE_INIT)
    {
      while ((ret = conn->initerror) == -EINPROGRESS)
        {
          nxsem_wait_uninterruptible(&conn->initdone);
          nxsem_post(&conn->initdone);
        }

      if (ret < 0)
        {
          return ret;
        }
    }

  req->hin.len = sizeof(req->hin);
  for (i = 0; i < vin_cnt; i++)
    {
      req->hin.len += vin[i].iov_len;
    }

  req->hin.uid = geteuid();
  req->hin.gid = getegid();
  req->hin.pid = getpid();

  req->vin      = vin;
  req->vin_cnt  = vin_cnt;
  req->vout     = vout;
  req->vout_cnt = vout_cnt;

  if (req->done == NULL)
    {
      wait = true;
      nxsem_init(&done, 0, 0);
      req->arg  = &done;
      req->done = fuse_req_done;
    }

  ret = conn->transport->request(conn, req);
  if (ret >= 0 && wait)
    {
      nxsem_wait_uninterruptible(&done);
      ret = req->hout.error;
    }

  if (wait)
    {
      nxsem_destroy(&done);
      req->arg  = NULL;
      req->done = NULL;
    }

  return ret;
}

/****************************************************************************
 * Name: fuse_request
 ****************************************************************************/

static inline int fuse_request(FAR struct fuse_conn_s *conn,
                               FAR struct fuse_req_s *req,
                               FAR const struct iovec *vin, size_t vin_cnt,
                               FAR const struct iovec *vout, size_t vout_cnt)
{
  do
    {
      req->hin.unique = atomic_add_relaxed(&conn->unique, 1);
    }
  while (req->hin.unique == 0);

  return fuse_request_oneway(conn, req, vin, vin_cnt, vout, vout_cnt);
}

/****************************************************************************
 * Name: fuse_lookup
 ****************************************************************************/

static int fuse_lookup(FAR struct fuse_conn_s *conn, uint64_t parent,
                       FAR const char *name, size_t namelen,
                       FAR struct fuse_entry_out *entry)
{
  struct fuse_req_s req;
  struct iovec vout[1];
  struct iovec vin[2];
  char nul = '\0';

  memset(&req, 0, sizeof(req));

  req.hin.opcode = FUSE_LOOKUP;
  req.hin.nodeid = parent;

  /* FUSE requires the name to be NUL terminated on the wire.  Send the
   * component and its terminator as two segments so the caller does not
   * need to copy the name out of the (unmodifiable) path.
   */

  vin[0].iov_base = (FAR void *)name;
  vin[0].iov_len  = namelen;
  vin[1].iov_base = &nul;
  vin[1].iov_len  = 1;

  vout[0].iov_base = entry;
  vout[0].iov_len  = sizeof(*entry);

  return fuse_request(conn, &req, vin, 2, vout, 1);
}

/****************************************************************************
 * Name: fuse_getattr
 *
 * Description:
 *   Get node attributes, optionally using an open file handle.
 ****************************************************************************/

static int fuse_getattr(FAR struct fuse_conn_s *conn,
                        uint64_t nodeid, FAR const uint64_t *fh,
                        FAR struct fuse_attr_out *aout)
{
  struct fuse_getattr_in gin;
  struct fuse_req_s req;
  struct iovec vout[1];
  struct iovec vin[1];

  memset(&req, 0, sizeof(req));
  memset(&gin, 0, sizeof(gin));

  req.hin.opcode = FUSE_GETATTR;
  req.hin.nodeid = nodeid;

  if (fh != NULL)
    {
      gin.getattr_flags = FUSE_GETATTR_FH;
      gin.fh            = *fh;
    }

  vin[0].iov_base = &gin;
  vin[0].iov_len  = sizeof(gin);

  vout[0].iov_base = aout;
  vout[0].iov_len  = sizeof(*aout);

  return fuse_request(conn, &req, vin, 1, vout, 1);
}

/****************************************************************************
 * Name: fuse_setattr
 *
 * Description:
 *   Set node attributes, optionally using an open file handle.
 ****************************************************************************/

static int fuse_setattr(FAR struct fuse_conn_s *conn,
                        uint64_t nodeid, FAR const uint64_t *fh,
                        FAR const struct stat *buf, int flags)
{
  struct fuse_setattr_in sin;
  struct fuse_attr_out aout;
  struct fuse_req_s req;
  struct iovec vout[1];
  struct iovec vin[1];

  memset(&req, 0, sizeof(req));
  memset(&sin, 0, sizeof(sin));

  req.hin.opcode = FUSE_SETATTR;
  req.hin.nodeid = nodeid;

  if (fh != NULL)
    {
      sin.valid |= FATTR_FH;
      sin.fh     = *fh;
    }

  if (flags & CH_STAT_MODE)
    {
      sin.valid |= FATTR_MODE;
      sin.mode   = buf->st_mode;
    }

  if (flags & CH_STAT_UID)
    {
      sin.valid |= FATTR_UID;
      sin.uid    = buf->st_uid;
    }

  if (flags & CH_STAT_GID)
    {
      sin.valid |= FATTR_GID;
      sin.gid    = buf->st_gid;
    }

  if (flags & CH_STAT_SIZE)
    {
      sin.valid |= FATTR_SIZE;
      sin.size   = buf->st_size;
    }

  if (flags & CH_STAT_ATIME)
    {
      sin.valid    |= FATTR_ATIME;
      sin.atime     = buf->st_atim.tv_sec;
      sin.atimensec = buf->st_atim.tv_nsec;
    }

  if (flags & CH_STAT_MTIME)
    {
      sin.valid    |= FATTR_MTIME;
      sin.mtime     = buf->st_mtim.tv_sec;
      sin.mtimensec = buf->st_mtim.tv_nsec;
    }

  if (flags & CH_STAT_CTIME)
    {
      sin.valid    |= FATTR_CTIME;
      sin.ctime     = buf->st_ctim.tv_sec;
      sin.ctimensec = buf->st_ctim.tv_nsec;
    }

  vin[0].iov_base = &sin;
  vin[0].iov_len  = sizeof(sin);

  vout[0].iov_base = &aout;
  vout[0].iov_len  = sizeof(aout);

  return fuse_request(conn, &req, vin, 1, vout, 1);
}

/****************************************************************************
 * Name: fuse_open_node
 *
 * Description:
 *   Open a FUSE node and return the server-side file handle.
 *
 ****************************************************************************/

static int fuse_open_node(FAR struct fuse_conn_s *conn, uint64_t nodeid,
                          int oflags, FAR struct fuse_open_out *oout)
{
  struct fuse_open_in oin;
  struct fuse_req_s req;
  struct iovec vout[1];
  struct iovec vin[1];

  memset(&req, 0, sizeof(req));
  memset(&oin, 0, sizeof(oin));

  req.hin.opcode = FUSE_OPEN;
  req.hin.nodeid = nodeid;

  oin.flags = oflags;

  vin[0].iov_base = &oin;
  vin[0].iov_len  = sizeof(oin);

  vout[0].iov_base = oout;
  vout[0].iov_len  = sizeof(*oout);

  return fuse_request(conn, &req, vin, 1, vout, 1);
}

/****************************************************************************
 * Name: fuse_unlink_node
 *
 * Description:
 *   Remove a named child node from a FUSE directory.
 *
 ****************************************************************************/

static int fuse_unlink_node(FAR struct fuse_conn_s *conn, uint64_t parent,
                            FAR const char *name)
{
  struct fuse_req_s req;
  struct iovec vin[1];

  memset(&req, 0, sizeof(req));

  req.hin.opcode = FUSE_UNLINK;
  req.hin.nodeid = parent;

  vin[0].iov_base = (FAR void *)name;
  vin[0].iov_len  = strlen(name) + 1;

  return fuse_request(conn, &req, vin, 1, NULL, 0);
}

/****************************************************************************
 * Name: fuse_poll_node
 *
 * Description:
 *   Query the server for readiness of a file handle.
 *
 ****************************************************************************/

static int fuse_poll_node(FAR struct fuse_conn_s *conn, uint64_t nodeid,
                          uint64_t fh, uint64_t kh, pollevent_t events,
                          FAR struct fuse_poll_out *pout)
{
  struct fuse_poll_in pin;
  struct fuse_req_s req;
  struct iovec vout[1];
  struct iovec vin[1];
  int ret;

  memset(&req, 0, sizeof(req));
  memset(&pin, 0, sizeof(pin));

  req.hin.opcode = FUSE_POLL;
  req.hin.nodeid = nodeid;

  pin.fh     = fh;
  pin.kh     = kh;
  pin.flags  = FUSE_POLL_SCHEDULE_NOTIFY;
  pin.events = events;

  vin[0].iov_base  = &pin;
  vin[0].iov_len   = sizeof(pin);
  vout[0].iov_base = pout;
  vout[0].iov_len  = sizeof(*pout);

  ret = fuse_request(conn, &req, vin, 1, vout, 1);
  if (ret >= 0 && req.hout.len != sizeof(req.hout) + sizeof(*pout))
    {
      ret = -EIO;
    }

  return ret;
}

/****************************************************************************
 * Name: fuse_readlink_node
 ****************************************************************************/

static ssize_t fuse_readlink_node(FAR struct fuse_conn_s *conn,
                                  uint64_t nodeid, FAR char *buf,
                                  size_t buflen)
{
  struct fuse_req_s req;
  struct iovec vout[1];
  int ret;

  memset(&req, 0, sizeof(req));

  req.hin.opcode = FUSE_READLINK;
  req.hin.nodeid = nodeid;

  vout[0].iov_base = buf;
  vout[0].iov_len  = buflen;

  ret = fuse_request(conn, &req, NULL, 0, vout, 1);
  if (ret < 0)
    {
      return ret;
    }

  /* Actual bytes = total response len - out_header size */

  return req.hout.len - sizeof(req.hout);
}

/****************************************************************************
 * Name: fuse_follow_link
 *
 * Description:
 *   Dereference the symbolic link in entry.  The target is read from the
 *   server and resolved component by component, recursing on nested links
 *   up to SYMLOOP_MAX levels.  A relative target is resolved against
 *   *parent (the directory containing the link); an absolute target is
 *   resolved against the mountpoint root.  On success entry holds the
 *   resolved node and *parent the directory containing it.
 *
 ****************************************************************************/

static int fuse_follow_link(FAR struct fuse_conn_s *conn,
                            FAR uint64_t *parent,
                            FAR struct fuse_entry_out *entry,
                            int depth)
{
  FAR const char *path;
  FAR char *target;
  int ret;

  if (depth >= SYMLOOP_MAX)
    {
      return -ELOOP;
    }

  target = lib_get_tempbuffer(PATH_MAX);
  if (target == NULL)
    {
      return -ENOMEM;
    }

  ret = fuse_readlink_node(conn, entry->nodeid, target, PATH_MAX);
  if (ret < 0)
    {
      goto out;
    }
  else if (ret >= PATH_MAX)
    {
      ret = -ENAMETOOLONG;
      goto out;
    }
  else if (*target == '/')
    {
      ret = -ENOTSUP;
      goto out;
    }

  target[ret] = '\0';
  path = target;

  for (; ; )
    {
      FAR const char *slash = strchrnul(path, '/');

      ret = fuse_lookup(conn, *parent, path, slash - path, entry);
      if (ret < 0)
        {
          break;
        }

      /* Resolve a nested link before deciding how to continue */

      if (S_ISLNK(entry->attr.mode))
        {
          ret = fuse_follow_link(conn, parent, entry, depth + 1);
          if (ret < 0)
            {
              break;
            }
        }

      if (slash[0] == '\0' || slash[1] == '\0')
        {
          break;
        }

      *parent = entry->nodeid;
      path = slash + 1;
    }

out:
  lib_put_tempbuffer(target);
  return ret;
}

/****************************************************************************
 * Name: fuse_lookup_parent
 ****************************************************************************/

static int fuse_lookup_parent(FAR struct fuse_conn_s *conn,
                              FAR const char *relpath,
                              FAR uint64_t *parent,
                              FAR const char **name)
{
  uint64_t node = FUSE_ROOT_ID;

  for (; ; )
    {
      FAR const char *slash = strchrnul(relpath, '/');
      struct fuse_entry_out entry;
      int ret;

      if (slash[0] == '\0' || slash[1] == '\0')
        {
          break;
        }

      ret = fuse_lookup(conn, node, relpath, slash - relpath, &entry);
      if (ret < 0)
        {
          return ret;
        }

      /* Always dereference an intermediate symbolic link */

      if (S_ISLNK(entry.attr.mode))
        {
          ret = fuse_follow_link(conn, &node, &entry, 0);
          if (ret < 0)
            {
              return ret;
            }
        }

      node = entry.nodeid;
      relpath = slash + 1;
    }

  *parent = node;
  *name = relpath;

  return 0;
}

/****************************************************************************
 * Name: fuse_lookup_path
 ****************************************************************************/

static int fuse_lookup_path(FAR struct fuse_conn_s *conn,
                            FAR const char *relpath,
                            FAR struct fuse_entry_out *entry,
                            bool follow)
{
  FAR const char *name;
  uint64_t parent;
  int ret;

  if (fuse_is_root(relpath))
    {
      struct fuse_attr_out aout;

      /* Root inode: use GETATTR instead of LOOKUP */

      ret = fuse_getattr(conn, FUSE_ROOT_ID, NULL, &aout);
      if (ret < 0)
        {
          return ret;
        }

      memset(entry, 0, sizeof(*entry));

      entry->nodeid          = FUSE_ROOT_ID;
      entry->attr_valid      = aout.attr_valid;
      entry->attr_valid_nsec = aout.attr_valid_nsec;
      entry->attr            = aout.attr;

      return 0;
    }

  ret = fuse_lookup_parent(conn, relpath, &parent, &name);
  if (ret < 0)
    {
      return ret;
    }

  /* Look up the final component, ignoring a trailing '/' */

  ret = fuse_lookup(conn, parent, name,
                    strchrnul(name, '/') - name, entry);
  if (ret < 0)
    {
      return ret;
    }

  /* Dereference the final component only when the caller asked for
   * follow semantics.
   */

  if (follow && S_ISLNK(entry->attr.mode))
    {
      ret = fuse_follow_link(conn, &parent, entry, 0);
    }

  return ret;
}

/****************************************************************************
 * Name: fuse_init_done
 *
 * Description:
 *   Publish the result of the asynchronous FUSE_INIT handshake.
 *
 ****************************************************************************/

static void fuse_init_done(FAR struct fuse_req_s *req)
{
  FAR struct fuse_req_init_s *init = container_of(req,
                                                  struct fuse_req_init_s,
                                                  req);
  FAR struct fuse_conn_s *conn = req->arg;
  int ret;

  ret = req->hout.error;
  if (ret < 0)
    {
      goto end;
    }

  if (req->hout.len != sizeof(req->hout) + sizeof(init->iout))
    {
      ret = -EIO;
      goto end;
    }

  if (init->iout.major != FUSE_KERNEL_VERSION)
    {
      ret = -EPROTO;
      goto end;
    }

  conn->max_write = init->iout.max_write > 0 ?
                    init->iout.max_write : FUSE_MAX_WRITE_BUFFER;

end:
  fs_heap_free(init);
  conn->initerror = ret;
  nxsem_post(&conn->initdone);
}

/****************************************************************************
 * Name: fuse_init
 *
 * Description:
 *   Submit the FUSE_INIT handshake without waiting for its reply.
 *
 ****************************************************************************/

static int fuse_init(FAR struct fuse_conn_s *conn)
{
  FAR struct fuse_req_init_s *init;
  int ret;

  init = fs_heap_zalloc(sizeof(*init));
  if (init == NULL)
    {
      return -ENOMEM;
    }

  init->req.done = fuse_init_done;
  init->req.arg  = conn;

  init->req.hin.opcode = FUSE_INIT;
  init->req.hin.nodeid = FUSE_ROOT_ID;

  init->iin.major = FUSE_KERNEL_VERSION;
  init->iin.minor = FUSE_KERNEL_MINOR_VERSION;

  init->vin[0].iov_base  = &init->iin;
  init->vin[0].iov_len   = sizeof(init->iin);
  init->vout[0].iov_base = &init->iout;
  init->vout[0].iov_len  = sizeof(init->iout);

  ret = fuse_request(conn, &init->req, init->vin, 1, init->vout, 1);
  if (ret < 0)
    {
      fs_heap_free(init);
    }

  return ret;
}

/****************************************************************************
 * Name: fuse_open
 ****************************************************************************/

static int fuse_open(FAR struct file *filep, FAR const char *relpath,
                     int oflags, mode_t mode)
{
  FAR struct inode *inode = filep->f_inode;
  FAR struct fuse_conn_s *conn = inode->i_private;
  FAR struct fuse_file_s *fp;
  struct fuse_entry_out entry;
  struct fuse_open_out oout;
  struct iovec vout[2];
  struct iovec vin[2];
  size_t pathlen;
  int ret;

  /* Record the relative path so that FIOC_FILEPATH can report it later:
   * FUSE has no reverse (nodeid to name) query.
   */

  pathlen = strlen(relpath) + 1;
  fp = fs_heap_zalloc(sizeof(*fp) + pathlen);
  if (fp == NULL)
    {
      return -ENOMEM;
    }

  if ((oflags & O_CREAT) != 0)
    {
      struct fuse_create_in cin;
      struct fuse_req_s req;
      FAR const char *name;
      uint64_t parent;

      ret = fuse_lookup_parent(conn, relpath, &parent, &name);
      if (ret < 0)
        {
          fs_heap_free(fp);
          return ret;
        }

      memset(&cin, 0, sizeof(cin));
      memset(&req, 0, sizeof(req));

      req.hin.opcode = FUSE_CREATE;
      req.hin.nodeid = parent;

      cin.flags = oflags & ~O_CREAT;
      cin.mode  = mode;
      cin.umask = getumask();

      vin[0].iov_base = &cin;
      vin[0].iov_len  = sizeof(cin);
      vin[1].iov_base = (FAR void *)name;
      vin[1].iov_len  = strlen(name) + 1;

      vout[0].iov_base = &entry;
      vout[0].iov_len  = sizeof(entry);
      vout[1].iov_base = &oout;
      vout[1].iov_len  = sizeof(oout);

      ret = fuse_request(conn, &req, vin, 2, vout, 2);
    }
  else /* Normal open: lookup then FUSE_OPEN */
    {
      ret = fuse_lookup_path(conn, relpath, &entry, true);
      if (ret < 0)
        {
          fs_heap_free(fp);
          return ret;
        }

      ret = fuse_open_node(conn, entry.nodeid, oflags, &oout);
    }

  if (ret < 0)
    {
      fs_heap_free(fp);
      return ret;
    }

  fp->fh     = oout.fh;
  fp->nodeid = entry.nodeid;
  atomic_set(&fp->refs, 1);
  memcpy(fp->relpath, relpath, pathlen);

  filep->f_priv = fp;
  return 0;
}

/****************************************************************************
 * Name: fuse_close
 ****************************************************************************/

static int fuse_close(FAR struct file *filep)
{
  FAR struct inode *inode = filep->f_inode;
  FAR struct fuse_conn_s *conn = inode->i_private;
  FAR struct fuse_file_s *fp = filep->f_priv;
  struct fuse_release_in rin;
  struct fuse_req_s req;
  struct iovec vin[1];

  /* The handle is shared by dup'ed files: send FUSE_RELEASE only when
   * the last user goes away.
   */

  if (atomic_sub_relaxed(&fp->refs, 1) == 1)
    {
      memset(&req, 0, sizeof(req));
      memset(&rin, 0, sizeof(rin));

      req.hin.opcode = FUSE_RELEASE;
      req.hin.nodeid = fp->nodeid;

      rin.fh = fp->fh;

      vin[0].iov_base = &rin;
      vin[0].iov_len  = sizeof(rin);

      fuse_request(conn, &req, vin, 1, NULL, 0);
      fs_heap_free(fp);
    }

  filep->f_priv = NULL;

  return 0;
}

/****************************************************************************
 * Name: fuse_maxbuf
 *
 * Description:
 *   Largest payload one request can carry at this moment, as reported by
 *   the transport.  Transports without a limit do not implement the
 *   hook.
 *
 ****************************************************************************/

static inline size_t fuse_maxbuf(FAR struct fuse_conn_s *conn)
{
  if (conn->transport->maxbuf == NULL)
    {
      return SIZE_MAX;
    }

  return conn->transport->maxbuf(conn);
}

/****************************************************************************
 * Name: fuse_uio_slice
 *
 * Description:
 *   Collect up to maxsegs iovec segments covering at most maxbytes bytes
 *   of the uio, starting at its cursor.  Returns the number of segments
 *   collected; *bytes receives the covered length.
 *
 ****************************************************************************/

static size_t fuse_uio_slice(FAR struct uio *uio, FAR struct iovec *iov,
                             size_t maxsegs, size_t maxbytes,
                             FAR size_t *bytes)
{
  size_t offset = uio->uio_offset_in_iov;
  size_t total = 0;
  size_t count = 0;
  int i;

  for (i = 0; count < maxsegs && i < uio->uio_iovcnt; i++)
    {
      size_t len = uio->uio_iov[i].iov_len - offset;

      if (len > maxbytes - total)
        {
          len = maxbytes - total;
        }

      if (len > 0)
        {
          FAR char *buf = uio->uio_iov[i].iov_base;

          iov[count].iov_base = buf + offset;
          iov[count].iov_len  = len;
          total += len;
          count++;
        }

      offset = 0;
      if (total >= maxbytes)
        {
          break;
        }
    }

  *bytes = total;
  return count;
}

/****************************************************************************
 * Name: fuse_readv
 ****************************************************************************/

static ssize_t fuse_readv(FAR struct file *filep, FAR struct uio *uio)
{
  FAR struct inode *inode = filep->f_inode;
  FAR struct fuse_conn_s *conn = inode->i_private;
  FAR struct fuse_file_s *fp = filep->f_priv;
  struct fuse_read_in rin;
  struct fuse_req_s req;
  struct iovec vout[FUSE_IOV_SEGS];
  struct iovec vin[1];
  size_t total = 0;
  ssize_t ret = 0;

  /* Transfer in rounds:  each one is sized by the transport capacity
   * (unlimited on non bouncing transports) and by the slice segment
   * array, scattering the reply directly into the caller's buffers.
   */

  while (uio->uio_resid > 0)
    {
      ssize_t nread;
      size_t chunk;
      size_t count;

      count = fuse_uio_slice(uio, vout, FUSE_IOV_SEGS,
                             fuse_maxbuf(conn), &chunk);
      if (chunk == 0)
        {
          ret = -ENOMEM;
          break;
        }

      memset(&req, 0, sizeof(req));
      memset(&rin, 0, sizeof(rin));

      req.hin.opcode = FUSE_READ;
      req.hin.nodeid = fp->nodeid;

      rin.fh     = fp->fh;
      rin.offset = filep->f_pos;
      rin.size   = chunk;

      vin[0].iov_base = &rin;
      vin[0].iov_len  = sizeof(rin);

      ret = fuse_request(conn, &req, vin, 1, vout, count);
      if (ret < 0)
        {
          break;
        }

      /* Actual bytes read = total response len - out_header size,
       * capped to this round in case of a misbehaving server.
       */

      nread = req.hout.len - sizeof(req.hout);
      if (nread <= 0)
        {
          break;
        }

      if (nread > chunk)
        {
          nread = chunk;
        }

      filep->f_pos += nread;
      total += nread;
      uio_advance(uio, nread);

      /* A short round means EOF or the server capped the reply */

      if (nread < chunk)
        {
          break;
        }
    }

  return total > 0 ? total : ret;
}

/****************************************************************************
 * Name: fuse_writev
 ****************************************************************************/

static ssize_t fuse_writev(FAR struct file *filep, FAR struct uio *uio)
{
  FAR struct inode *inode = filep->f_inode;
  FAR struct fuse_conn_s *conn = inode->i_private;
  FAR struct fuse_file_s *fp = filep->f_priv;
  struct fuse_write_out wout;
  struct fuse_write_in win;
  struct fuse_req_s req;
  struct iovec vout[1];
  struct iovec vin[FUSE_IOV_SEGS];
  size_t total = 0;
  int ret;

  while (uio->uio_resid > 0)
    {
      size_t maxbytes = fuse_maxbuf(conn);
      size_t chunk;
      size_t count;

      /* Each round carries what the smaller of the negotiated max_write
       * and the transport capacity allows, gathered from the uio cursor;
       * uio_advance below keeps the cursor in sync with the bytes the
       * server accepted.
       */

      if (maxbytes > conn->max_write)
        {
          maxbytes = conn->max_write;
        }

      count = fuse_uio_slice(uio, &vin[1], FUSE_IOV_SEGS - 1,
                             maxbytes, &chunk);
      if (chunk == 0)
        {
          return total > 0 ? total : -ENOMEM;
        }

      memset(&req, 0, sizeof(req));
      memset(&win, 0, sizeof(win));

      req.hin.opcode = FUSE_WRITE;
      req.hin.nodeid = fp->nodeid;

      win.fh     = fp->fh;
      win.offset = filep->f_pos;
      win.size   = chunk;

      vin[0].iov_base = &win;
      vin[0].iov_len  = sizeof(win);

      vout[0].iov_base = &wout;
      vout[0].iov_len  = sizeof(wout);

      ret = fuse_request(conn, &req, vin, count + 1, vout, 1);
      if (ret < 0)
        {
          return total > 0 ? total : ret;
        }

      /* Cap to the bytes actually gathered:  uio_advance must not move
       * past the sent range even if the server overstates wout.size.
       */

      if (wout.size > chunk)
        {
          wout.size = chunk;
        }

      filep->f_pos += wout.size;
      total += wout.size;
      uio_advance(uio, wout.size);

      if (wout.size < chunk)
        {
          break;
        }
    }

  return total;
}

/****************************************************************************
 * Name: fuse_seek
 ****************************************************************************/

static off_t fuse_seek(FAR struct file *filep, off_t offset, int whence)
{
  FAR struct inode *inode = filep->f_inode;
  FAR struct fuse_conn_s *conn = inode->i_private;
  FAR struct fuse_file_s *fp = filep->f_priv;
  struct fuse_lseek_out lout;
  struct fuse_lseek_in lin;
  struct fuse_req_s req;
  struct iovec vout[1];
  struct iovec vin[1];
  int ret;

  memset(&req, 0, sizeof(req));
  memset(&lin, 0, sizeof(lin));

  req.hin.opcode = FUSE_LSEEK;
  req.hin.nodeid = fp->nodeid;

  lin.fh     = fp->fh;
  lin.offset = offset;
  lin.whence = whence;

  vin[0].iov_base = &lin;
  vin[0].iov_len  = sizeof(lin);

  vout[0].iov_base = &lout;
  vout[0].iov_len  = sizeof(lout);

  ret = fuse_request(conn, &req, vin, 1, vout, 1);
  if (ret >= 0)
    {
      filep->f_pos = lout.offset;
    }
  else if (ret != -ENOSYS)
    {
      return ret;
    }
  else /* Fallback: local seek */
    {
      switch (whence)
        {
          case SEEK_SET:
            filep->f_pos = offset;
            break;

          case SEEK_CUR:
            filep->f_pos += offset;
            break;

          case SEEK_END:
            {
              struct stat st;
              ret = fuse_fstat(filep, &st);
              if (ret < 0)
                {
                  return ret;
                }

              filep->f_pos = st.st_size + offset;
            }
            break;

          default:
            return -EINVAL;
        }
    }

  return filep->f_pos;
}

/****************************************************************************
 * Name: fuse_filelock
 *
 * Description:
 *   Translate a FIOC_GETLK/FIOC_SETLK/FIOC_SETLKW request into the
 *   corresponding FUSE lock operation.  The flock range is converted to
 *   the absolute inclusive byte range used on the wire.
 *
 ****************************************************************************/

static int fuse_filelock(FAR struct file *filep, int cmd,
                         FAR struct flock *flock)
{
  FAR struct inode *inode = filep->f_inode;
  FAR struct fuse_conn_s *conn = inode->i_private;
  FAR struct fuse_file_s *fp = filep->f_priv;
  struct fuse_lk_out lout;
  struct fuse_lk_in lin;
  struct fuse_req_s req;
  struct iovec vout[1];
  struct iovec vin[1];
  off_t start;
  off_t end;
  int ret;

  switch (flock->l_type)
    {
      case F_RDLCK:
      case F_WRLCK:
      case F_UNLCK:
        break;

      default:
        return -EINVAL;
    }

  switch (flock->l_whence)
    {
      case SEEK_SET:
        start = 0;
        break;

      case SEEK_CUR:
        start = filep->f_pos;
        break;

      case SEEK_END:
        {
          struct stat st;

          ret = fuse_fstat(filep, &st);
          if (ret < 0)
            {
              return ret;
            }

          start = st.st_size;
        }
        break;

      default:
        return -EINVAL;
    }

  if (flock->l_start > OFFSET_MAX - start)
    {
      return -EOVERFLOW;
    }

  start += flock->l_start;
  if (start < 0)
    {
      return -EINVAL;
    }

  if (flock->l_len > 0)
    {
      if (flock->l_len - 1 > OFFSET_MAX - start)
        {
          return -EOVERFLOW;
        }

      end = start + flock->l_len - 1;
    }
  else if (flock->l_len < 0)
    {
      if (start + flock->l_len < 0)
        {
          return -EINVAL;
        }

      end = start - 1;
      start += flock->l_len;
    }
  else
    {
      end = OFFSET_MAX;
    }

  memset(&req, 0, sizeof(req));
  memset(&lin, 0, sizeof(lin));

  req.hin.opcode = cmd == FIOC_GETLK ? FUSE_GETLK :
                   cmd == FIOC_SETLK ? FUSE_SETLK : FUSE_SETLKW;
  req.hin.nodeid = fp->nodeid;

  /* POSIX record locks are owned by the process */

  lin.fh       = fp->fh;
  lin.owner    = getpid();
  lin.lk.start = start;
  lin.lk.end   = end;
  lin.lk.type  = flock->l_type;
  lin.lk.pid   = getpid();

  vin[0].iov_base = &lin;
  vin[0].iov_len  = sizeof(lin);

  vout[0].iov_base = &lout;
  vout[0].iov_len  = sizeof(lout);

  ret = fuse_request(conn, &req, vin, 1, vout,
                     cmd == FIOC_GETLK ? 1 : 0);
  if (ret == -ENOSYS)
    {
      /* No lock support in the server: return ENOTTY so that the VFS
       * falls back to the local advisory lock implementation.
       */

      return -ENOTTY;
    }
  else if (ret < 0 || cmd != FIOC_GETLK)
    {
      return ret;
    }

  /* F_GETLK: report the first conflicting lock, or F_UNLCK if none */

  if (lout.lk.type == F_UNLCK)
    {
      flock->l_type = F_UNLCK;
    }
  else
    {
      flock->l_type   = lout.lk.type;
      flock->l_whence = SEEK_SET;
      flock->l_start  = lout.lk.start;
      flock->l_len    = lout.lk.end >= OFFSET_MAX ? 0 :
                        lout.lk.end - lout.lk.start + 1;
      flock->l_pid    = lout.lk.pid;
    }

  return 0;
}

/****************************************************************************
 * Name: fuse_filepath
 *
 * Description:
 *   Report the path recorded at open time:  the mountpoint path followed
 *   by the file's relative path.  FUSE has no reverse (nodeid to name)
 *   query, so a server-side rename since open is not reflected here.
 *
 ****************************************************************************/

static int fuse_filepath(FAR struct file *filep, FAR char *path)
{
  FAR struct fuse_file_s *fp = filep->f_priv;
  int ret;

  ret = inode_getpath(filep->f_inode, path, PATH_MAX);
  if (ret < 0)
    {
      return ret;
    }

  strlcat(path, fp->relpath, PATH_MAX);
  return 0;
}

/****************************************************************************
 * Name: fuse_ioctl_iov_size
 *
 * Description:
 *   Calculate the total payload size of an ioctl iovec list.
 *
 ****************************************************************************/

static inline size_t
fuse_ioctl_iov_size(FAR const struct fuse_ioctl_iovec *iov, size_t count)
{
  size_t total = 0;
  size_t i;

  for (i = 0; i < count; i++)
    {
      total += iov[i].len;
    }

  return total;
}

/****************************************************************************
 * Name: fuse_ioctl
 ****************************************************************************/

static int fuse_ioctl(FAR struct file *filep, int cmd, unsigned long arg)
{
  FAR struct inode *inode = filep->f_inode;
  FAR struct fuse_conn_s *conn = inode->i_private;
  FAR struct fuse_file_s *fp = filep->f_priv;
  struct fuse_ioctl_iovec iov[FUSE_IOV_SEGS * 2];
  struct iovec vout[FUSE_IOV_SEGS];
  struct iovec vin[FUSE_IOV_SEGS];
  struct fuse_ioctl_out iout;
  struct fuse_ioctl_in iin;
  struct fuse_req_s req;
  ssize_t arglen = ioctl_arglen(cmd, arg);

  switch (cmd)
    {
      case FIOC_SETLK:
      case FIOC_SETLKW:
      case FIOC_GETLK:
        return fuse_filelock(filep, cmd,
                             (FAR struct flock *)(uintptr_t)arg);

      case FIOC_FILEPATH:
        return fuse_filepath(filep, (FAR char *)(uintptr_t)arg);
    }

  memset(&req, 0, sizeof(req));
  memset(&iin, 0, sizeof(iin));
  memset(&iout, 0, sizeof(iout));

  req.hin.opcode = FUSE_IOCTL;
  req.hin.nodeid = fp->nodeid;

  iin.fh       = fp->fh;
  iin.flags    = arglen < 0 ? FUSE_IOCTL_UNRESTRICTED : 0;
  iin.cmd      = cmd;
  iin.arg      = arg;
  iin.in_size  = MAX(arglen, 0);
  iin.out_size = MAX(arglen, 0);

  for (; ; )
    {
      size_t i = 0;
      size_t j = 0;
      int ret;

      vin[i].iov_base  = &iin;
      vin[i++].iov_len = sizeof(iin);
      if (iout.in_iovs > 0)
        {
          for (; i <= iout.in_iovs; i++)
            {
              vin[i].iov_base = (FAR void *)(uintptr_t)iov[i - 1].base;
              vin[i].iov_len  = iov[i - 1].len;
            }
        }
      else if (iin.in_size > 0)
        {
          vin[i].iov_base  = (FAR void *)(uintptr_t)arg;
          vin[i++].iov_len = iin.in_size;
        }

      vout[j].iov_base  = &iout;
      vout[j++].iov_len = sizeof(iout);
      if (iout.out_iovs > 0)
        {
          for (; j <= iout.out_iovs; j++)
            {
              vout[j].iov_base =
                (FAR void *)(uintptr_t)iov[iout.in_iovs + j - 1].base;
              vout[j].iov_len = iov[iout.in_iovs + j - 1].len;
            }
        }
      else if (iin.out_size > 0)
        {
          vout[j].iov_base  = (FAR void *)(uintptr_t)arg;
          vout[j++].iov_len = iin.out_size;
        }
      else
        {
          vout[j].iov_base  = iov;
          vout[j++].iov_len = sizeof(iov);
        }

      ret = fuse_request(conn, &req, vin, i, vout, j);
      if (ret < 0)
        {
          return ret;
        }

      if (req.hout.len < sizeof(req.hout) + sizeof(iout))
        {
          return -EIO;
        }

      if (!(iout.flags & FUSE_IOCTL_RETRY))
        {
          return iout.result;
        }

      if (iin.out_size != 0)
        {
          return -ENOTSUP; /* Can retry only when out size is zero */
        }

      if (req.hout.len != sizeof(req.hout) + sizeof(iout) +
                          sizeof(iov[0]) * (iout.in_iovs + iout.out_iovs))
        {
          return -EIO;
        }

      if (iout.in_iovs > FUSE_IOV_SEGS - 1 ||
          iout.out_iovs > FUSE_IOV_SEGS - 1)
        {
          return -E2BIG;
        }

      iin.in_size = fuse_ioctl_iov_size(iov, iout.in_iovs);
      iin.out_size = fuse_ioctl_iov_size(iov + iout.in_iovs, iout.out_iovs);
    }
}

/****************************************************************************
 * Name: fuse_truncate
 ****************************************************************************/

static int fuse_truncate(FAR struct file *filep, off_t length)
{
  struct stat buf;

  buf.st_size = length;
  return fuse_fchstat(filep, &buf, CH_STAT_SIZE);
}

/****************************************************************************
 * Name: fuse_poll_teardown
 ****************************************************************************/

static inline void
fuse_poll_teardown(FAR struct file *filep, FAR struct pollfd *fds)
{
  FAR struct inode *inode = filep->f_inode;
  FAR struct fuse_conn_s *conn = inode->i_private;
  FAR struct fuse_poll_s *poll = fds->priv;
  irqstate_t flags;

  flags = spin_lock_irqsave(&conn->polllock);
  if (poll != NULL)
    {
      list_delete(&poll->node);
      poll->fds->priv = NULL;
      poll->fds = NULL;
      spin_unlock_irqrestore(&conn->polllock, flags);

      file_put(poll->file);
      fs_heap_free(poll);
    }
  else
    {
      spin_unlock_irqrestore(&conn->polllock, flags);
    }
}

/****************************************************************************
 * Name: fuse_poll_setup
 ****************************************************************************/

static int fuse_poll_setup(FAR struct file *filep, FAR struct pollfd *fds)
{
  FAR struct inode *inode = filep->f_inode;
  FAR struct fuse_conn_s *conn = inode->i_private;
  FAR struct fuse_file_s *fp = filep->f_priv;
  FAR struct fuse_poll_s *poll;
  struct fuse_poll_out pout;
  irqstate_t flags;
  int ret;

  poll = fs_heap_zalloc(sizeof(*poll));
  if (poll == NULL)
    {
      return -ENOMEM;
    }

  file_ref(filep);
  poll->file = filep;

  poll->kh = atomic_add_relaxed(&conn->kh, 1) + 1;

  flags = spin_lock_irqsave(&conn->polllock);
  fds->priv = poll;
  poll->fds = fds;
  list_add_tail(&conn->polls, &poll->node);
  spin_unlock_irqrestore(&conn->polllock, flags);

  ret = fuse_poll_node(conn, fp->nodeid, fp->fh,
                       poll->kh, fds->events, &pout);
  if (ret < 0)
    {
      fuse_poll_teardown(filep, fds);
      return ret;
    }

  if (pout.revents)
    {
      poll_notify(&fds, 1, pout.revents);
    }

  return 0;
}

/****************************************************************************
 * Name: fuse_poll_wakeup
 ****************************************************************************/

static void fuse_poll_wakeup(FAR struct fuse_conn_s *conn, uint32_t kh)
{
  irqstate_t flags = spin_lock_irqsave_nopreempt(&conn->polllock);
  FAR struct fuse_poll_s *poll;

  list_for_every_entry(&conn->polls, poll, struct fuse_poll_s, node)
    {
      FAR struct pollfd *fds = poll->fds;

      if (poll->kh == kh)
        {
          poll_notify(&fds, 1, fds->events & (POLLIN | POLLOUT | POLLPRI));
          break;
        }
    }

  spin_unlock_irqrestore_nopreempt(&conn->polllock, flags);
}

/****************************************************************************
 * Name: fuse_poll
 ****************************************************************************/

static int fuse_poll(FAR struct file *filep,
                     FAR struct pollfd *fds, bool setup)
{
  if (setup)
    {
      return fuse_poll_setup(filep, fds);
    }
  else
    {
      fuse_poll_teardown(filep, fds);
      return 0;
    }
}

/****************************************************************************
 * Name: fuse_sync
 ****************************************************************************/

static int fuse_sync(FAR struct file *filep)
{
  FAR struct inode *inode = filep->f_inode;
  FAR struct fuse_conn_s *conn = inode->i_private;
  FAR struct fuse_file_s *fp = filep->f_priv;
  struct fuse_fsync_in fin;
  struct fuse_req_s req;
  struct iovec vin[1];

  memset(&req, 0, sizeof(req));
  memset(&fin, 0, sizeof(fin));

  req.hin.opcode = FUSE_FSYNC;
  req.hin.nodeid = fp->nodeid;

  fin.fh = fp->fh;

  vin[0].iov_base = &fin;
  vin[0].iov_len  = sizeof(fin);

  return fuse_request(conn, &req, vin, 1, NULL, 0);
}

/****************************************************************************
 * Name: fuse_dup
 ****************************************************************************/

static int fuse_dup(FAR const struct file *oldp, FAR struct file *newp)
{
  FAR struct fuse_file_s *fp = oldp->f_priv;

  /* Both files share the same server-side handle, so bump the reference
   * instead of duplicating fh, which would make FUSE_RELEASE drop the
   * same handle twice.
   */

  atomic_add_relaxed(&fp->refs, 1);
  newp->f_priv = fp;

  return 0;
}

/****************************************************************************
 * Name: fuse_fstat
 ****************************************************************************/

static int fuse_fstat(FAR const struct file *filep, FAR struct stat *buf)
{
  FAR struct inode *inode = filep->f_inode;
  FAR struct fuse_conn_s *conn = inode->i_private;
  FAR struct fuse_file_s *fp = filep->f_priv;
  struct fuse_attr_out aout;
  int ret;

  ret = fuse_getattr(conn, fp->nodeid, &fp->fh, &aout);
  if (ret < 0)
    {
      return ret;
    }

  fuse_attr_to_stat(&aout.attr, buf);
  return 0;
}

/****************************************************************************
 * Name: fuse_fchstat
 ****************************************************************************/

static int fuse_fchstat(FAR const struct file *filep,
                        FAR const struct stat *buf, int flags)
{
  FAR struct inode *inode = filep->f_inode;
  FAR struct fuse_conn_s *conn = inode->i_private;
  FAR struct fuse_file_s *fp = filep->f_priv;

  return fuse_setattr(conn, fp->nodeid, &fp->fh, buf, flags);
}

/****************************************************************************
 * Name: fuse_opendir
 ****************************************************************************/

static int fuse_opendir(FAR struct inode *mountpt, FAR const char *relpath,
                        FAR struct fs_dirent_s **dir)
{
  FAR struct fuse_conn_s *conn = mountpt->i_private;
  FAR struct fuse_dir_s *fdir;
  struct fuse_open_out oout;
  struct fuse_open_in oin;
  struct fuse_req_s req;
  struct iovec vout[1];
  struct iovec vin[1];
  uint64_t nodeid;
  int ret;

  if (fuse_is_root(relpath))
    {
      nodeid = FUSE_ROOT_ID;
    }
  else
    {
      struct fuse_entry_out entry;

      ret = fuse_lookup_path(conn, relpath, &entry, true);
      if (ret < 0)
        {
          return ret;
        }

      nodeid = entry.nodeid;
    }

  fdir = fs_heap_zalloc(sizeof(*fdir));
  if (fdir == NULL)
    {
      return -ENOMEM;
    }

  memset(&req, 0, sizeof(req));
  memset(&oin, 0, sizeof(oin));

  req.hin.opcode = FUSE_OPENDIR;
  req.hin.nodeid = nodeid;

  oin.flags = O_RDONLY;

  vin[0].iov_base = &oin;
  vin[0].iov_len  = sizeof(oin);

  vout[0].iov_base = &oout;
  vout[0].iov_len  = sizeof(oout);

  ret = fuse_request(conn, &req, vin, 1, vout, 1);
  if (ret < 0)
    {
      fs_heap_free(fdir);
      return ret;
    }

  fdir->fh     = oout.fh;
  fdir->nodeid = nodeid;

  *dir = &fdir->base;
  return 0;
}

/****************************************************************************
 * Name: fuse_closedir
 ****************************************************************************/

static int fuse_closedir(FAR struct inode *mountpt,
                         FAR struct fs_dirent_s *dir)
{
  FAR struct fuse_conn_s *conn = mountpt->i_private;
  FAR struct fuse_dir_s *fdir = (FAR struct fuse_dir_s *)dir;
  struct fuse_release_in rin;
  struct fuse_req_s req;
  struct iovec vin[1];

  memset(&req, 0, sizeof(req));
  memset(&rin, 0, sizeof(rin));

  req.hin.opcode = FUSE_RELEASEDIR;
  req.hin.nodeid = fdir->nodeid;

  rin.fh = fdir->fh;

  vin[0].iov_base = &rin;
  vin[0].iov_len  = sizeof(rin);

  fuse_request(conn, &req, vin, 1, NULL, 0);
  fs_heap_free(fdir);

  return 0;
}

/****************************************************************************
 * Name: fuse_readdir
 ****************************************************************************/

static int fuse_readdir(FAR struct inode *mountpt,
                        FAR struct fs_dirent_s *dir,
                        FAR struct dirent *entry)
{
  FAR struct fuse_conn_s *conn = mountpt->i_private;
  FAR struct fuse_dir_s *fdir = (FAR struct fuse_dir_s *)dir;
  FAR struct fuse_dirent *fde;
  struct fuse_read_in rin;
  struct fuse_req_s req;
  struct iovec vout[1];
  struct iovec vin[1];

  /* Room for a single entry with the longest representable name:  only
   * the first entry of a reply is consumed before the stream offset is
   * advanced, so requesting more would just be discarded and refetched.
   */

  char buf[FUSE_DIRENT_ALIGN(FUSE_NAME_OFFSET + NAME_MAX)]
       aligned_data(sizeof(uint64_t));
  int ret;
  int len;

  memset(&req, 0, sizeof(req));
  memset(&rin, 0, sizeof(rin));

  req.hin.opcode = FUSE_READDIR;
  req.hin.nodeid = fdir->nodeid;

  rin.fh     = fdir->fh;
  rin.offset = fdir->offset;
  rin.size   = sizeof(buf);

  vin[0].iov_base = &rin;
  vin[0].iov_len  = sizeof(rin);

  vout[0].iov_base = buf;
  vout[0].iov_len  = sizeof(buf);

  ret = fuse_request(conn, &req, vin, 1, vout, 1);
  if (ret < 0)
    {
      return ret;
    }

  len = req.hout.len - sizeof(req.hout);
  if (len < FUSE_NAME_OFFSET)
    {
      return -ENOENT;
    }

  fde = (FAR struct fuse_dirent *)buf;
  if (fde->namelen == 0 || fde->namelen >= sizeof(entry->d_name))
    {
      return -ENOENT;
    }

  memcpy(entry->d_name, fde->name, fde->namelen);
  entry->d_name[fde->namelen] = '\0';
  entry->d_type = fde->type;

  fdir->offset = fde->off;
  return 0;
}

/****************************************************************************
 * Name: fuse_rewinddir
 ****************************************************************************/

static int fuse_rewinddir(FAR struct inode *mountpt,
                          FAR struct fs_dirent_s *dir)
{
  FAR struct fuse_dir_s *fdir = (FAR struct fuse_dir_s *)dir;

  fdir->offset = 0;
  return 0;
}

/****************************************************************************
 * Name: fuse_bind
 ****************************************************************************/

static int fuse_bind(FAR struct inode *blkdriver, FAR const void *data,
                     FAR void **handle)
{
  FAR struct fuse_conn_s *conn;
  int ret = -EINVAL;

  if (data == NULL)
    {
      return -EINVAL;
    }

  conn = fs_heap_zalloc(sizeof(*conn));
  if (conn == NULL)
    {
      return -ENOMEM;
    }

  nxsem_init(&conn->initdone, 0, 0);
  conn->initerror = -EINPROGRESS;
  conn->max_write = FUSE_MAX_WRITE_BUFFER;

  spin_lock_init(&conn->polllock);
  list_initialize(&conn->polls);

  /* Select the transport from the mount options.  Each transport claims a
   * distinct option keyword: "fd=" for the local /dev/fuse character
   * device, "tag=" for the virtio-conn device.
   */

#ifdef CONFIG_FS_FUSE_DEV
  ret = fuse_dev_connect(conn, data);
#endif

#ifdef CONFIG_FS_VIRTIOFS
  if (ret < 0)
    {
      ret = fuse_virtio_connect(conn, data);
    }
#endif

  if (ret < 0)
    {
      goto err;
    }

  ret = fuse_init(conn);
  if (ret < 0)
    {
      conn->transport->destroy(conn);
      goto err;
    }

  *handle = conn;
  return 0;

err:
  nxsem_destroy(&conn->initdone);
  fs_heap_free(conn);
  return ret;
}

/****************************************************************************
 * Name: fuse_unbind
 ****************************************************************************/

static int fuse_unbind(FAR void *handle, FAR struct inode **blkdriver,
                       unsigned int flags)
{
  FAR struct fuse_conn_s *conn = handle;
  irqstate_t irqflags;

  irqflags = spin_lock_irqsave(&conn->polllock);
  if (!list_is_empty(&conn->polls))
    {
      spin_unlock_irqrestore(&conn->polllock, irqflags);
      return -EBUSY;
    }

  spin_unlock_irqrestore(&conn->polllock, irqflags);

  /* Send FUSE_DESTROY only if the connection completed its handshake */

  if (conn->initerror >= 0)
    {
      struct fuse_req_s req;

      memset(&req, 0, sizeof(req));

      req.hin.opcode = FUSE_DESTROY;
      req.hin.nodeid = FUSE_ROOT_ID;

      fuse_request(conn, &req, NULL, 0, NULL, 0);
    }

  conn->transport->destroy(conn);
  nxsem_destroy(&conn->initdone);
  fs_heap_free(conn);

  return 0;
}

/****************************************************************************
 * Name: fuse_statfs
 ****************************************************************************/

static int fuse_statfs(FAR struct inode *mountpt, FAR struct statfs *buf)
{
  FAR struct fuse_conn_s *conn = mountpt->i_private;
  struct fuse_statfs_out sout;
  struct fuse_req_s req;
  struct iovec vout[1];
  int ret;

  memset(&req, 0, sizeof(req));

  req.hin.opcode = FUSE_STATFS;
  req.hin.nodeid = FUSE_ROOT_ID;

  vout[0].iov_base = &sout;
  vout[0].iov_len  = sizeof(sout);

  ret = fuse_request(conn, &req, NULL, 0, vout, 1);
  if (ret < 0)
    {
      return ret;
    }

  memset(buf, 0, sizeof(*buf));

  buf->f_type    = FUSEFS_MAGIC; /* "FUSe" */
  buf->f_bsize   = sout.st.bsize;
  buf->f_blocks  = sout.st.blocks;
  buf->f_bfree   = sout.st.bfree;
  buf->f_bavail  = sout.st.bavail;
  buf->f_files   = sout.st.files;
  buf->f_ffree   = sout.st.ffree;
  buf->f_namelen = sout.st.namelen;

  return 0;
}

/****************************************************************************
 * Name: fuse_unlink
 ****************************************************************************/

static int fuse_unlink(FAR struct inode *mountpt, FAR const char *relpath)
{
  FAR struct fuse_conn_s *conn = mountpt->i_private;
  FAR const char *name;
  uint64_t parent;
  int ret;

  ret = fuse_lookup_parent(conn, relpath, &parent, &name);
  if (ret < 0)
    {
      return ret;
    }

  return fuse_unlink_node(conn, parent, name);
}

/****************************************************************************
 * Name: fuse_mkdir
 ****************************************************************************/

static int fuse_mkdir(FAR struct inode *mountpt, FAR const char *relpath,
                      mode_t mode)
{
  FAR struct fuse_conn_s *conn = mountpt->i_private;
  struct fuse_entry_out eout;
  struct fuse_mkdir_in min;
  struct fuse_req_s req;
  struct iovec vout[1];
  struct iovec vin[2];
  FAR const char *name;
  uint64_t parent;
  int ret;

  ret = fuse_lookup_parent(conn, relpath, &parent, &name);
  if (ret < 0)
    {
      return ret;
    }

  memset(&req, 0, sizeof(req));
  memset(&min, 0, sizeof(min));

  req.hin.opcode = FUSE_MKDIR;
  req.hin.nodeid = parent;

  min.mode  = mode;
  min.umask = getumask();

  vin[0].iov_base = &min;
  vin[0].iov_len  = sizeof(min);
  vin[1].iov_base = (FAR void *)name;
  vin[1].iov_len  = strlen(name) + 1;

  vout[0].iov_base = &eout;
  vout[0].iov_len  = sizeof(eout);

  return fuse_request(conn, &req, vin, 2, vout, 1);
}

/****************************************************************************
 * Name: fuse_rmdir
 ****************************************************************************/

static int fuse_rmdir(FAR struct inode *mountpt, FAR const char *relpath)
{
  FAR struct fuse_conn_s *conn = mountpt->i_private;
  struct fuse_req_s req;
  struct iovec vin[1];
  FAR const char *name;
  uint64_t parent;
  int ret;

  ret = fuse_lookup_parent(conn, relpath, &parent, &name);
  if (ret < 0)
    {
      return ret;
    }

  memset(&req, 0, sizeof(req));

  req.hin.opcode = FUSE_RMDIR;
  req.hin.nodeid = parent;

  vin[0].iov_base = (FAR void *)name;
  vin[0].iov_len  = strlen(name) + 1;

  return fuse_request(conn, &req, vin, 1, NULL, 0);
}

/****************************************************************************
 * Name: fuse_rename
 ****************************************************************************/

static int fuse_rename(FAR struct inode *mountpt,
                       FAR const char *oldrelpath,
                       FAR const char *newrelpath)
{
  FAR struct fuse_conn_s *conn = mountpt->i_private;
  struct fuse_rename_in rin;
  struct fuse_req_s req;
  struct iovec vin[3];
  FAR const char *oldname;
  FAR const char *newname;
  uint64_t oldparent;
  uint64_t newparent;
  int ret;

  ret = fuse_lookup_parent(conn, oldrelpath, &oldparent, &oldname);
  if (ret < 0)
    {
      return ret;
    }

  ret = fuse_lookup_parent(conn, newrelpath, &newparent, &newname);
  if (ret < 0)
    {
      return ret;
    }

  memset(&req, 0, sizeof(req));
  memset(&rin, 0, sizeof(rin));

  req.hin.opcode = FUSE_RENAME;
  req.hin.nodeid = oldparent;

  rin.newdir = newparent;

  vin[0].iov_base = &rin;
  vin[0].iov_len  = sizeof(rin);
  vin[1].iov_base = (FAR void *)oldname;
  vin[1].iov_len  = strlen(oldname) + 1;
  vin[2].iov_base = (FAR void *)newname;
  vin[2].iov_len  = strlen(newname) + 1;

  return fuse_request(conn, &req, vin, 3, NULL, 0);
}

/****************************************************************************
 * Name: fuse_stat
 ****************************************************************************/

static int fuse_stat(FAR struct inode *mountpt, FAR const char *relpath,
                     FAR struct stat *buf)
{
  FAR struct fuse_conn_s *conn = mountpt->i_private;
  struct fuse_entry_out entry;
  int ret;

  ret = fuse_lookup_path(conn, relpath, &entry, true);
  if (ret < 0)
    {
      return ret;
    }

  fuse_attr_to_stat(&entry.attr, buf);
  return 0;
}

/****************************************************************************
 * Name: fuse_chstat
 ****************************************************************************/

static int fuse_chstat(FAR struct inode *mountpt, FAR const char *relpath,
                       FAR const struct stat *buf, int flags)
{
  FAR struct fuse_conn_s *conn = mountpt->i_private;
  struct fuse_entry_out entry;
  int ret;

  ret = fuse_lookup_path(conn, relpath, &entry, true);
  if (ret < 0)
    {
      return ret;
    }

  return fuse_setattr(conn, entry.nodeid, NULL, buf, flags);
}

/****************************************************************************
 * Name: fuse_syncfs
 ****************************************************************************/

static int fuse_syncfs(FAR struct inode *mountpt)
{
  FAR struct fuse_conn_s *conn = mountpt->i_private;
  struct fuse_req_s req;

  memset(&req, 0, sizeof(req));

  req.hin.opcode = FUSE_SYNCFS;
  req.hin.nodeid = FUSE_ROOT_ID;

  return fuse_request(conn, &req, NULL, 0, NULL, 0);
}

#ifdef CONFIG_FS_LINKS
/****************************************************************************
 * Name: fuse_link
 ****************************************************************************/

static int fuse_link(FAR struct inode *mountpt, FAR const char *relpath1,
                     FAR const char *relpath2)
{
  FAR struct fuse_conn_s *conn = mountpt->i_private;
  struct fuse_entry_out entry;
  struct fuse_link_in lin;
  struct fuse_req_s req;
  struct iovec vout[1];
  struct iovec vin[2];
  FAR const char *name;
  uint64_t parent;
  int ret;

  /* Lookup the target to get its nodeid */

  ret = fuse_lookup_path(conn, relpath1, &entry, false);
  if (ret < 0)
    {
      return ret;
    }

  /* Lookup the parent of the new link */

  ret = fuse_lookup_parent(conn, relpath2, &parent, &name);
  if (ret < 0)
    {
      return ret;
    }

  memset(&req, 0, sizeof(req));
  memset(&lin, 0, sizeof(lin));

  req.hin.opcode = FUSE_LINK;
  req.hin.nodeid = parent;

  lin.oldnodeid = entry.nodeid;

  vin[0].iov_base = &lin;
  vin[0].iov_len  = sizeof(lin);
  vin[1].iov_base = (FAR void *)name;
  vin[1].iov_len  = strlen(name) + 1;

  vout[0].iov_base = &entry;
  vout[0].iov_len  = sizeof(entry);

  return fuse_request(conn, &req, vin, 2, vout, 1);
}

/****************************************************************************
 * Name: fuse_symlink
 ****************************************************************************/

static int fuse_symlink(FAR struct inode *mountpt, FAR const char *relpath1,
                        FAR const char *relpath2)
{
  FAR struct fuse_conn_s *conn = mountpt->i_private;
  struct fuse_entry_out entry;
  struct fuse_req_s req;
  struct iovec vout[1];
  struct iovec vin[2];
  FAR const char *name;
  uint64_t parent;
  int ret;

  ret = fuse_lookup_parent(conn, relpath2, &parent, &name);
  if (ret < 0)
    {
      return ret;
    }

  memset(&req, 0, sizeof(req));

  req.hin.opcode = FUSE_SYMLINK;
  req.hin.nodeid = parent;

  /* FUSE_SYMLINK: name + '\0' + target + '\0' */

  vin[0].iov_base = (FAR void *)name;
  vin[0].iov_len  = strlen(name) + 1;
  vin[1].iov_base = (FAR void *)relpath1;
  vin[1].iov_len  = strlen(relpath1) + 1;

  vout[0].iov_base = &entry;
  vout[0].iov_len  = sizeof(entry);

  return fuse_request(conn, &req, vin, 2, vout, 1);
}

/****************************************************************************
 * Name: fuse_readlink
 ****************************************************************************/

static ssize_t fuse_readlink(FAR struct inode *mountpt,
                             FAR const char *relpath,
                             FAR char *buf, size_t buflen)
{
  FAR struct fuse_conn_s *conn = mountpt->i_private;
  struct fuse_entry_out entry;
  ssize_t ret;

  ret = fuse_lookup_path(conn, relpath, &entry, false);
  if (ret < 0)
    {
      return ret;
    }

  ret = fuse_readlink_node(conn, entry.nodeid, buf, buflen);
  if (ret > 0 && ret < buflen)
    {
      buf[ret] = '\0';
    }

  return ret;
}

/****************************************************************************
 * Name: fuse_lstat
 ****************************************************************************/

static int fuse_lstat(FAR struct inode *mountpt, FAR const char *relpath,
                      FAR struct stat *buf)
{
  FAR struct fuse_conn_s *conn = mountpt->i_private;
  struct fuse_entry_out entry;
  int ret;

  ret = fuse_lookup_path(conn, relpath, &entry, false);
  if (ret < 0)
    {
      return ret;
    }

  fuse_attr_to_stat(&entry.attr, buf);
  return 0;
}
#endif /* CONFIG_FS_LINKS */

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: fuse_abort
 ****************************************************************************/

void fuse_abort(FAR struct fuse_conn_s *conn)
{
  irqstate_t flags = spin_lock_irqsave_nopreempt(&conn->polllock);
  FAR struct fuse_poll_s *poll;

  list_for_every_entry(&conn->polls, poll, struct fuse_poll_s, node)
    {
      poll_notify(&poll->fds, 1, POLLERR | POLLHUP);
    }

  spin_unlock_irqrestore_nopreempt(&conn->polllock, flags);
}

/****************************************************************************
 * Name: fuse_notify
 ****************************************************************************/

int fuse_notify(FAR struct fuse_conn_s *conn,
                FAR const struct fuse_out_header *hout,
                FAR struct uio *uio)
{
  struct fuse_notify_inval_inode_out inode;
  struct fuse_notify_inval_entry_out entry;
  struct fuse_notify_prune_out prune;
  struct fuse_notify_store_out store;
  struct fuse_notify_delete_out del;

  if (conn->initerror < 0)
    {
      return conn->initerror;
    }

  switch (hout->error)
    {
      case FUSE_NOTIFY_POLL:
        {
          struct fuse_notify_poll_wakeup_out wakeup;

          if (hout->len != sizeof(*hout) + sizeof(wakeup))
            {
              return -EINVAL;
            }

          uio_copyto(uio, 0, &wakeup, sizeof(wakeup));
          uio_advance(uio, sizeof(wakeup));
          fuse_poll_wakeup(conn, wakeup.kh);
          return 0;
        }

      case FUSE_NOTIFY_INVAL_INODE:
        if (hout->len != sizeof(*hout) + sizeof(inode))
          {
            return -EINVAL;
          }

        uio_copyto(uio, 0, &inode, sizeof(inode));
        uio_advance(uio, sizeof(inode));
        return 0;

      case FUSE_NOTIFY_INVAL_ENTRY:
        if (hout->len < sizeof(*hout) + sizeof(entry))
          {
            return -EINVAL;
          }

        uio_copyto(uio, 0, &entry, sizeof(entry));
        uio_advance(uio, sizeof(entry));

        if (hout->len != sizeof(*hout) + sizeof(entry) + entry.namelen + 1)
          {
            return -EINVAL;
          }

        uio_advance(uio, entry.namelen + 1);
        return 0;

      case FUSE_NOTIFY_DELETE:
        if (hout->len < sizeof(*hout) + sizeof(del))
          {
            return -EINVAL;
          }

        uio_copyto(uio, 0, &del, sizeof(del));
        uio_advance(uio, sizeof(del));

        if (hout->len != sizeof(*hout) + sizeof(del) + del.namelen + 1)
          {
            return -EINVAL;
          }

        uio_advance(uio, del.namelen + 1);
        return 0;

      case FUSE_NOTIFY_STORE:
        if (hout->len < sizeof(*hout) + sizeof(store))
          {
            return -EINVAL;
          }

        uio_copyto(uio, 0, &store, sizeof(store));
        uio_advance(uio, sizeof(store));

        if (hout->len != sizeof(*hout) + sizeof(store) + store.size)
          {
            return -EINVAL;
          }

        uio_advance(uio, store.size);
        return 0;

      case FUSE_NOTIFY_INC_EPOCH:
        if (hout->len != sizeof(*hout))
          {
            return -EINVAL;
          }

        return 0;

      case FUSE_NOTIFY_PRUNE:
        if (hout->len < sizeof(*hout) + sizeof(prune))
          {
            return -EINVAL;
          }

        uio_copyto(uio, 0, &prune, sizeof(prune));
        uio_advance(uio, sizeof(prune));

        if (hout->len != sizeof(*hout) + sizeof(prune) +
                         sizeof(uint64_t) * prune.count)
          {
            return -EINVAL;
          }

        uio_advance(uio, sizeof(uint64_t) * prune.count);
        return 0;

      default:
        return -ENOTSUP;
    }
}
