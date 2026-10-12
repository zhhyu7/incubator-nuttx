=========
Core Dump
=========

Overview
========

.. image:: image/coredump-overview.png

How to use
-----------

1. Enable NuttX Core dump

Enable Kconfig

    .. code-block:: console

      CONFIG_COREDUMP=y                   /* Enable Coredump */

      CONFIG_BOARD_COREDUMP_SYSLOG=y      /* Enable Board Coredump, if exceptions and assertions occur, */

      CONFIG_SYSTEM_COREDUMP=y            /* Enable coredump in user command, which can capture the current
                                             state of one or all threads when the system is running, the
                                             output can be redirect to console or file */

      CONFIG_BOARD_COREDUMP_COMPRESSION=y /* Default y, enable Coredump compression to
                                             reduce the size of the original core image */

      CONFIG_BOARD_COREDUMP_FULL=y        /* Default y, save all task information,
                                             see "Core dump levels" below */

2. Run Coredump on nsh (CONFIG_SYSTEM_COREDUMP=y)

Parameters of coredump tool

    .. code-block:: console

      $ coredump <pid>        /* If pid is specified, coredump will only capture the thread with the
                                 specified pid, otherwise all threads will be captured */

      $ coredump <filename>   /* If filename is specified, then coredump will be output to the specified
                                 file by default, otherwise it will be redirect in stdout stream */

3. Capture coredump from stdout

Save the print of the red frame part in the figure as file

    .. image:: image/coredump-hexdump.png

    .. code-block:: console

      $ cat elf.dump
      [CPU0] [ 6] 5A5601013D03FF077F454C4601010100C0000304002800C00D003420036000070400053400200008200A4000000420030034C024200001D8092004E00200601A
      ...
      [CPU0] [ 6] 401B018D37814720005A5601000800090100006000010000

4. Convert the dump file

If the core file is post-processed by lzf compress and hexdump stream, execute the coredump script (`tools/coredump.py
<https://github.com/apache/nuttx/blob/master/tools/coredump.py>`_) to convert hex to binary and lzf decompression, If the -o parameter is not added in commandline, the output of <original file name>.core will be automatically generated:

    .. code-block:: console

      $ ./nuttx/tools/coredump.py elf.dump
      Core file conversion completed: elf.core

5. Analysis by gdb

After generating elf.core, combined with compiled nuttx.elf, you can view the call stack and related register information of all threads directly through gdb:

(NOTE: Toolchain version must be newer than 11.3)

    .. code-block:: console

      $ prebuilts/gcc/linux/arm/bin/arm-none-eabi-gdb -c elf.core nuttx

    .. image:: image/coredump-gdb.png

Core dump levels
----------------

On flash constrained targets the board memory regions dominate the core dump
size. The "Core Dump content" choice trades content away for a smaller core
file.  The levels below describe the automatic dump taken on an assertion or
exception:

======= ============= ==============================================
Level   Threads       Memory regions
======= ============= ==============================================
CURRENT crashing only tagged ``PF_COREDUMP_CURRENT``
THREADS all           tagged ``PF_COREDUMP_THREADS`` or ``_CURRENT``
FULL    all           all, whatever their tag
======= ============= ==============================================

Each level is a ``CONFIG_BOARD_COREDUMP_<LEVEL>`` symbol. The current CPU
register context and the interrupt stack are emitted at every level, so thread
backtraces stay recoverable from the smallest core file.

A memory region declares the lowest level that still emits it through the
``PF_COREDUMP_*`` bits of its ELF segment flags. The encoding descends, so a
region with no tag reaches the full dump only:

.. code-block:: c

  /* Runtime registration, kept in every level */

  coredump_add_memory_region(ptr, size, PF_R | PF_COREDUMP_CURRENT);

Static board regions carry the same bits in the third field of each triple.
``0x6`` is ``PF_R | PF_W`` and reaches the full dump only, while ``0x400004``
adds ``PF_COREDUMP_CURRENT`` (``0x400000``) and survives every level:

.. code-block:: console

  CONFIG_BOARD_MEMORY_RANGE="{0x20000000,0x20040000,0x6},{0x40001000,0x40001100,0x400004}"

The tag is written into the ``p_flags`` of the emitted ``PT_LOAD`` segment, so
the level of each region is recoverable from the core file itself.

Runtime control
---------------

With ``CONFIG_FS_PROCFS_REGISTER`` the facility exposes ``/proc/coredump/ctl``:

.. code-block:: console

  $ cat /proc/coredump/ctl
  enable: on
  level : current
  usage : echo on|off > /proc/coredump/ctl
          echo level <current|threads|full> > /proc/coredump/ctl
          echo <path> <nslot> > /proc/coredump/ctl

  $ echo "level full" > /proc/coredump/ctl     /* Widen the next dump */
  $ echo "level current" > /proc/coredump/ctl  /* Back to the small dump */
  $ echo off > /proc/coredump/ctl              /* Disable the facility */
  $ echo on > /proc/coredump/ctl               /* Re-enable it */

The "Core Dump content" choice only selects the level the target boots with.
Switching the level at run time moves both the thread and the region scope of
the automatic dump, so a target can keep a minidump in production and widen it
for an investigation without a rebuild.  The ``coredump`` command takes its
thread scope from ``pid`` as described above; the level narrows only the memory
regions it captures.

Stored dumps on a block or MTD device can be scanned and exposed under
``/proc/coredump`` by writing ``"<path> <nslot>"`` to the same file.

Netpoll coredump
----------------

Netpoll coredump sends the crash dump through a netpoll network device.
Ethernet devices use UDP. CAN devices use ISO-TP over CAN netpoll.

Enable the netpoll backend with:

.. code-block:: console

  CONFIG_COREDUMP=y
  CONFIG_BOARD_COREDUMP_NETPOLL=y
  CONFIG_NET_NETPOLL=y

For UDP, configure ``CONFIG_BOARD_COREDUMP_NETPOLL_INTF`` and
``CONFIG_BOARD_COREDUMP_NETPOLL_PORT``.
``CONFIG_BOARD_COREDUMP_NETPOLL_IPADDR`` and
``CONFIG_BOARD_COREDUMP_NETPOLL_MAC`` select the destination address.

For CAN, configure ``CONFIG_BOARD_COREDUMP_NETPOLL_INTF`` and
``CONFIG_BOARD_COREDUMP_NETPOLL_PORT``. The port value is the CAN ID used by
NuttX. The host replies on the next CAN ID.

Capture UDP coredumps on the host with:

.. code-block:: console

  $ ./tools/coredump.py --decompress -o elf.core udp 6677

Capture CAN coredumps on the host with:

.. code-block:: console

  $ ./tools/coredump.py --decompress -o elf.core isotp can0 1800

The CAN capture mode uses Linux CAN ISO-TP sockets.

Start the capture tool before triggering the crash. The netpoll stream contains
only the coredump bytes; the host tool writes the core file after an idle
timeout. Use ``--timeout`` if the target or transport is slow.
