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
