==============================
FUSE (Filesystem in Userspace)
==============================

NuttX provides a transport-independent FUSE client core under ``fs/fuse``.
The core implements the NuttX ``mountpt_operations`` interface on top of the
standard FUSE wire protocol (kernel ABI 7.45, see ``include/nuttx/fuse.h``)
and dispatches each request through a pluggable transport.

Architecture
============

::

  Application (open/read/write/opendir ...)
        |
        v
      NuttX VFS
        |
        v
  fs/fuse core  (g_fuse_operations + FUSE encode/decode + node walk)
        |  conn->transport->request()
        v
  fuse_transport_ops  --------+------------------------+
        |                     |                        |
        v                     v                        v
  /dev/fuse transport    virtio-fs transport      (other transports)
  (CONFIG_FS_FUSE_DEV)   (CONFIG_FS_VIRTIOFS)

The core owns the filesystem semantics and the request life cycle.  A
transport only moves the FUSE request/reply bytes between the core and a
server.  ``fuse_request()`` computes the request ``unique`` and ``len``,
submits the request through ``conn->transport->request()``. Requests may
use a completion callback; synchronous requests use a semaphore-backed
callback.
The core submits ``FUSE_INIT`` asynchronously during bind, and subsequent
requests wait for its completion before proceeding.
The INIT reply must contain exactly one complete ``fuse_init_out`` with
the expected protocol major version.  The completion callback releases the
INIT request after publishing the negotiated maximum write size.

IOCTL forwarding
================

Ioctls not handled by the FUSE core are sent to the server as ``FUSE_IOCTL``
requests.  The client uses ``ioctl_arglen(cmd, arg)`` to determine the
argument size.  For a known size, it sets both ``in_size`` and ``out_size`` to
that value.  For an unknown size, it sets ``FUSE_IOCTL_UNRESTRICTED`` and both
sizes to zero so the server can provide input and/or output iovecs.

A retry response is followed only while the current ``out_size`` is zero,
regardless of ``in_size``.  The server may provide input and/or output iovecs;
the client uses them for the next request.  If that request returns another
retry while its ``out_size`` is non-zero, the client rejects it.  Zero-length
output iovec plans may therefore be retried repeatedly.  Retry iovecs use the
protocol-defined 64-bit layout and their counts are limited by the client's
scatter/gather segment arrays.

Server notifications
====================

The ``/dev/fuse`` transport accepts one notification per ``write()`` or
``writev()``.  Notifications use ``fuse_out_header.unique == 0`` and a
positive notification code in ``error``.  The transport separates them from
ordinary replies and calls ``fuse_notify()`` with the payload.  The declared
message length must fit within the supplied input and match the notification
layout, including the name length, data size or node count where applicable.
Invalid lengths return ``-EINVAL``.  Extra input beyond the declared message
is not consumed; a successful write returns the number of bytes actually
consumed. Notifications do not complete an outstanding filesystem request and
return ``-ENOTCONN`` before the FUSE_INIT handshake completes or after
disconnect.

The client currently has no directory-entry, attribute, readdir or file-data
cache.  Valid ``FUSE_NOTIFY_INVAL_INODE``, ``FUSE_NOTIFY_INVAL_ENTRY``,
``FUSE_NOTIFY_DELETE``, ``FUSE_NOTIFY_STORE``, ``FUSE_NOTIFY_INC_EPOCH`` and
``FUSE_NOTIFY_PRUNE`` notifications therefore succeed without changing local
state.  Pruning does
not release active file handles or manufacture ``FUSE_FORGET`` requests.
Payload contents, including names and entry flags, are not validated or
acted upon.
STORE data is consumed and discarded; it is not written to the server or
retained locally.

``FUSE_NOTIFY_RETRIEVE`` and ``FUSE_NOTIFY_RESEND`` remain unsupported and
return ``-ENOTSUP``.  RESEND concerns outstanding requests, not cache
invalidation.

The ``/dev/fuse`` transport currently delivers server notifications.
Virtio-fs does not yet negotiate ``VIRTIO_FS_F_NOTIFICATION`` and continues
to use queue 0 for hiprio and queue 1 for requests.

File polling
============

Each poll setup sends a synchronous ``FUSE_POLL`` request containing the file
handle, requested events and ``FUSE_POLL_SCHEDULE_NOTIFY``.  The initial
reply's ``revents`` are reported to the application.  Each registration
holds a file reference and receives a handle generated from an
atomic 32-bit counter and carried in the 64-bit ``kh`` protocol field.

``FUSE_NOTIFY_POLL`` carries only ``kh``.  After validating and consuming
its payload, the client reports the matching registration's requested
``POLLIN`` and ``POLLOUT`` events without issuing another query.  Unknown
or expired handles are ignored.  Every transport is expected to deliver
these notifications; virtio-fs notification reception remains to be
implemented.

Teardown removes the registration before releasing the file reference and
poll storage.  Disconnect wakes registered polls with``POLLERR | POLLHUP``
and aborts outstanding transport requests.  Poll requests use the synchronous
FUSE request mechanism and can therefore wait beyond the poll timeout if the
server does not reply.  Unmount returns ``-EBUSY`` while registrations remain.
An ``-ENOSYS`` reply is returned as an error; there is no local-readiness
fallback.

Concurrency
===========

Each request carries a completion callback and its argument.  Synchronous
callers use a stack ``sem_t`` until completion; asynchronous callers provide
their own callback.  Request IDs use an atomic counter, and the connection's
``initdone`` gates requests while ``initerror`` is ``-EINPROGRESS``.
The completion callback publishes success or the initialization error and
wakes waiting requests.  Unbind does not wait for an unfinished handshake
and does not send ``FUSE_DESTROY`` while initialization is pending.
The local transport uses its device lock to protect request queues.
Multiple VFS calls may therefore be in flight concurrently without holding
a connection lock for the full round trip.

The local transport holds its device lock during notification processing
to keep the connection alive.  A separate ``polllock`` protects poll
registrations in a doubly linked list and event delivery.  Lock ordering
is device lock before poll lock; FUSE requests are issued without the poll
lock held.

Transports
==========

The mount type selects the transport through the mount data options:

============  ==================  =================================
Mount type    Option              Transport
============  ==================  =================================
``fuse``      ``fd=<n>``          ``/dev/fuse`` (``CONFIG_FS_FUSE_DEV``)
``virtiofs``  ``tag=<name>``      virtio-fs (``CONFIG_FS_VIRTIOFS``)
============  ==================  =================================

The ``fuse`` mount type is registered by the core.  Enabling the core alone
(``CONFIG_FS_FUSE``) provides no transport, so a mount only succeeds once at
least one transport is configured.

Configuration
=============

``CONFIG_FS_FUSE``
  Enable the transport-independent FUSE core and register the ``fuse`` mount
  type.

See also ``CONFIG_FS_FUSE_DEV`` for the local ``/dev/fuse`` transport and
``CONFIG_FS_VIRTIOFS`` for the virtio-fs transport.
