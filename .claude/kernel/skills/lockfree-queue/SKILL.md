---
name: lockfree-queue
description: "NuttX 嵌入式多核系统（TriCore/ARM/RISC-V, 2-4 cores）无锁队列选型、性能分析和代码审查指南。涵盖 SPSC/MPSC/SPMC/MPMC/Ring 五种实现，包含原子操作成本分析、tail-wait 缓解策略、代码风格规范。Use when: 选择队列类型, 分析性能瓶颈, 编写/修改队列头文件, 审查队列代码正确性, 比较 lock-free 队列 tradeoffs, 调试队列问题, or any mention of lock-free, lockfree, 无锁队列, mpsc, mpmc, spsc, spmc, ring buffer, CAS, Vyukov, Michael-Scott, RTS, tail-wait, intrusive queue, 生产者消费者."
---

# Lock-Free Queue — Analysis & Development Guide

Context for working with lock-free concurrent queue implementations in this NuttX-based embedded multi-core codebase (TriCore, ARM, RISC-V targets, 2–4 cores, low contention).

## Quick Reference

Performance numbers are TriCore TC3xx @ 300MHz reference values. Re-benchmark on ARM/RISC-V targets before production use.

| Your scenario | Choose | Hot path cost (atomic ops/round-trip) |
|---------------|--------|---------------------------------------|
| 1P 1C | SPSC (`spsc.h`) | 2 atomics (1 release-store + 1 acquire-load), wait-free both sides |
| NP 1C | MPSC (`mpsc.h`) | ~3 atomics + irq_save (enqueue wait-free, dequeue lock-free) |
| 1P NC | SPMC (`spmc.h`) | 5 atomics (2 enqueue + 3 dequeue), enqueue wait-free, dequeue lock-free |
| NP NC, bounded | Ring (`ring.h`) | 12 atomics (4 CAS) |
| NP NC, unbounded | MPMC (`mpmc.h`) | ~6 atomics (2 CAS, no recycle) |

## Reference Files

Read relevant files before implementation, modification, or debugging tasks. For conceptual discussions or queue selection, reference this skill directly.

- `nuttx/include/nuttx/mpmc.h` — MPMC Michael-Scott intrusive linked list, no stub recycle
- `nuttx/include/nuttx/mpsc.h` — MPSC Vyukov intrusive linked list with stub recycle
- `nuttx/include/nuttx/spsc.h` — SPSC intrusive linked list, stub-sentinel (no recycle)
- `nuttx/include/nuttx/spmc.h` — SPMC intrusive linked list, SPSC enqueue + Michael-Scott consumer (no recycle)
- `nuttx/include/nuttx/ring.h` — Unified bounded ring (DPDK RTS algorithm, all four modes)
- `nuttx/include/nuttx/atomic.h` — Atomic operations abstraction layer

## NuttX Code Style Rules

When writing or modifying lock-free queue headers:

- Use `static inline_function` (not `static inline`) for all inline functions
- Use `FAR` pointer qualifier on all pointer parameters and local pointer variables
- Use NuttX section comment banners (`/***...*/`) to separate Included Files, Pre-processor Definitions, Public Type Definitions, Public Inline Functions
- Struct naming: `struct xxx_queue_s`, `struct xxx_node_s` (suffix `_s`)
- Guard macro naming: `__INCLUDE_NUTTX_XXX_H` (double underscore, path-based)
- Wrap C++ extern: `#undef EXTERN` / `extern "C"` block at start and end of public declarations
- Brace style: opening brace on next line, indented 2 spaces from parent; code inside indented 2 more
- Use `container_of()` for intrusive node-to-data conversion; never allocate separate queue entries
- Prefer `_release` / `_acquire` memory ordering over `seq_cst` for producer-consumer patterns. Verify semantics via `nuttx/include/nuttx/atomic.h` (Cortex-M uses DMB, RISC-V uses fence). TriCore has strong TSO ordering; for multi-cluster setups verify CCI-400 snoop filter configuration.
- Ring buffer capacity must be power-of-2 (enforced by `static_assert` in `RING_DEFINE`; for dynamic `init`, caller must validate capacity before calling)

## Algorithm Taxonomy

| Queue | Type | Producers | Consumers | Progress | Key Primitive |
|-------|------|-----------|-----------|----------|---------------|
| SPSC (`spsc.h`) | Intrusive linked list, stub-sentinel | 1 | 1 | Wait-free both sides | release-store / acquire-load on `prev->next` |
| SPMC (`spmc.h`) | Intrusive linked list, stub-sentinel, no recycle | 1 | N | Enqueue wait-free, dequeue lock-free | release-store on `prev->next` (producer); release CAS on `q->head` (consumers) |
| MPSC (`mpsc.h`) | Intrusive linked list, stub-sentinel with recycle | N | 1 | Enqueue wait-free, dequeue lock-free | xchg on `q->tail` (producers); plain reads + occasional re-enqueue of stub (consumer) |
| MPMC (`mpmc.h`) | Intrusive linked list, stub-sentinel, no recycle | N | N | Lock-free both sides | release CAS on `tail->next` + cooperative tail advance (producers); release CAS on `q->head` (consumers) |
| Ring (`ring.h`) | Bounded ring, DPDK RTS | N | N | Lock-free | CAS on packed {cnt:8, idx:24} on `prod_head/tail` and `cons_head/tail` |

## Atomic Operation Cost Per Operation

Benchmark values from TriCore TC3xx @ 300MHz with non-inlined atomics; re-benchmark on your target platform. To check: `grep CONFIG_LIBC_ATOMIC_TOOLCHAIN .config` — if not set, atomics are extern function calls (~30 cycles each). Enabling it lets the compiler inline `__atomic_*` builtins.

| Operation | SPSC | SPMC | MPSC Vyukov | MPMC M-S | Ring (RTS) |
|-----------|------|------|-------------|----------|------------|
| Enqueue | 1 (set_release on prev->next) | 2 (1 plain store on q->tail + 1 set_release on prev->next) | 2 (xchg + set_release) + irq_save/restore | 4 (2 read + 1 link CAS + 1 best-effort tail CAS) | 6 (2 read + 2 CAS) |
| Dequeue | 1 (read_acquire on head->next) | 3 (1 read + 1 acquire read + 1 release CAS on q->head) | 1–2 (read_acquire only; near-empty path adds xchg + set_release to recycle stub) | 3 (1 read + 1 acquire read + 1 release CAS on q->head) | 6 (2 read + 2 CAS) |
| Empty check | 1 (acquire-load) | 2 (1 read + 1 acquire-load on head->next) | 1 | 2 (1 read + 1 acquire-load on head->next) | 2 |

## Queue Selection Rules

```
1 producer, 1 consumer?
  → SPSC (spsc.h): wait-free, zero CAS, intrusive linked list
    Stub-sentinel; never recycled (consumer never writes producer state).
    Caller must defer freeing a returned node until q->head advances past
    it — either remember the previous node and free it after the next
    successful dequeue, or poll spsc_node_reclaimable(q, node).
    If multiple threads may send on same link, add external mutex.

N producers, 1 consumer?
  → MPSC Vyukov (mpsc.h): enqueue wait-free (single xchg), dequeue ≈ 0 CAS
    Best for: per-pool data queues, multiple CPUs send, one CPU drains.
    Three dequeue variants:
      mpsc_dequeue       — lock-free, returns NULL on transient/empty
      mpsc_dequeue_spin  — busy-spin through transient, NULL only on empty
      mpsc_dequeue_yield — sched_yield() through transient, NULL only on empty
    Use mpsc_empty() to distinguish "really empty" from "producer in flight".
    For block-until-arrived, combine mpsc_dequeue + mpsc_empty + a sem the
    producer posts after enqueue (do not put blocking inside the queue API).

1 producer, N consumers?
  → SPMC (spmc.h): wait-free enqueue (no CAS, single producer), lock-free
    multi-consumer dequeue (release CAS on q->head). Same no-recycle
    lifetime model as spsc.h / mpmc.h: the returned node becomes the
    new head sentinel and must not be freed or re-enqueued until
    spmc_node_reclaimable(q, node) returns true. With multiple consumers
    each must hold its own `prev` and poll spmc_node_reclaimable() in a
    short loop (sched_yield()) before reclaiming. Single-consumer or
    serial-drain callers can free unconditionally on the next successful
    dequeue. ABA safety follows from the same delayed-reclaim discipline
    as MPMC — no DCAS / tagged pointer / hazard pointer needed.

N producers, N consumers?
  → Bounded/pre-allocated needed? → Ring (ring.h) with RING_F_MPMC
    Warning: 4 CAS per round-trip, expensive with non-inlined atomics.
    Tail-wait mitigation: uses RTS (Relaxed Tail Sync), no IRQ disabling needed.
  → Unbounded OK? → MPMC Michael-Scott (mpmc.h)
    Lock-free both sides; intrusive linked list. Same node-lifetime rule
    as SPSC: returned node remains as new head sentinel until the next
    successful dequeue advances past it. Use mpmc_node_reclaimable()
    or defer-free-prev pattern. ABA is avoided by this delayed-reclaim
    discipline (no DCAS / tagged pointer needed).
    Warning: every dequeue does ~3 loads + 1 release-acquire CAS;
    helping CAS adds 1 release CAS when tail observed lagging.
```

## Performance Insights

These are derived from real hardware benchmarks on this codebase (TriCore TC3xx @ 300MHz, non-inlined atomics). Verify on your target platform:

1. MPMC and SPMC both use the no-recycle / delayed-reclaim design (mirroring `spsc.h`): the returned node remains as the new head sentinel, and the caller defers freeing it via `*_node_reclaimable()`. There is no stub recycle path; the stub is consumed once on the first dequeue and never re-inserted. The trade-off is the lifetime constraint on returned nodes (delayed-reclaim) in exchange for ABA safety without DCAS / hazard pointers. Note: MPSC (`mpsc.h`) is the only intrusive queue that *does* recycle the stub — its dequeue may re-enqueue the stub when the queue is about to drain.

2. Ring RTS requires double-CAS per operation (head CAS + tail CAS). That is 4 CAS per message round-trip.

3. MPSC Vyukov enqueue uses `up_irq_save()`/`up_irq_restore()` to protect the xchg+set_release window. On TriCore this is ~10 cycles (`disable`/`enable` instructions), much cheaper than a CAS function call (~30 cycles).

4. Intrusive linked lists (mpmc.h, mpsc.h, spmc.h) have no capacity limit and no ring buffer allocation, but require a per-node `atomic_ptr_t next` field. Bounded rings (spsc.h, ring.h) need pre-allocated buffers but have better cache locality for batch operations.

5. A spinlock-protected SPSC can have lower latency than a lock-free MPMC when the actual access pattern is confirmed single-producer single-consumer. The reason: MPMC pays CAS + stub recycle overhead even when only one thread is producing. Compare total atomic operation count on the actual hot path, not just the locking model.

## Tail-Wait Problem and Mitigation Strategies

The tail-wait problem occurs in multi-producer/multi-consumer bounded ring queues that use a two-phase commit (head CAS to reserve slots, then tail update to publish). Between these two phases, a thread holds reserved-but-uncommitted slots. If that thread is preempted, all other threads waiting on the tail are blocked — causing unbounded priority inversion in an RTOS.

Two mitigation strategies are used in this codebase:

**RTS (Relaxed Tail Sync) — used by `ring.h`**: Packs an 8-bit thread counter and 24-bit index into a single `atomic_t`. The tail CAS increments the counter and conditionally advances the index only when all in-flight threads have completed (counter matches). No IRQ disabling needed. Trade-off: requires 2 CAS per operation (head + tail), and the packed format limits index to 24 bits.

**IRQ-save around xchg — used by `mpsc.h`**: Disables interrupts only around the 2 atomic operations (xchg + store-release) in enqueue (the window where a preemption would leave the linked list in an inconsistent state). The IRQ-disabled window is ~10 cycles on TriCore. This is not a general tail-wait solution for ring buffers; it is specific to the Vyukov MPSC linked-list algorithm where the xchg swaps the head pointer and the subsequent store links the old head to the new node.

## Common Pitfalls

- Using MPMC when MPSC suffices. The consumer side is often single (e.g., one CPU's IRQ handler). MPSC dequeue is dramatically cheaper.
- Forgetting the no-recycle node-lifetime constraint shared by `spsc.h`, `spmc.h`, and `mpmc.h`. A node returned by `*_dequeue()` becomes the new head sentinel and **must not** be freed or re-enqueued until `*_node_reclaimable(q, node)` returns true (or the standard defer-free-prev pattern advances past it). For SPSC the next successful dequeue alone is enough; for SPMC and MPMC each consumer must hold its own `prev` and may need to poll `*_node_reclaimable()` because a peer consumer can still be holding the stale value as a CAS expectation. MPSC (`mpsc.h`) is the exception — it recycles the stub internally and returns a fully detached node, so the caller may free immediately.
- Using `ring.h` with runtime flags from `r->flags`. The compiler cannot eliminate dead SP/SC code paths. Use `ring_enqueue_tf` / `ring_dequeue_tf` macros with compile-time constant flags for dead-code elimination.
- Holding IRQ lock across the entire MPSC enqueue including unrelated work. The `mpsc.h` enqueue only needs IRQ protection around the 2 atomic operations (xchg + store-release). Do not extend the IRQ-disabled window to cover caller logic:
  ```c
  /* ✗ Bad: IRQ disabled too long */
  flags = up_irq_save();
  prepare_data(node);          /* unrelated work under IRQ lock */
  mpsc_enqueue(q, &node->mnode);
  up_irq_restore(flags);

  /* ✓ Good: IRQ protection is inside mpsc_enqueue only */
  prepare_data(node);
  mpsc_enqueue(q, &node->mnode);  /* irq_save/restore handled internally */
  ```

## Development Rules

1. Queue descriptors and nodes in multi-core scenarios must reside in shared memory (uncached). Verify memory region attributes via board linker script (`.shared` section) or `up_addrenv_*` APIs:
   ```c
   DEBUGASSERT(up_addrenv_coherent(&my_queue, sizeof(my_queue)));
   ```
   Incorrect placement causes silent data corruption.
2. One core must complete queue init (init + fill) before other cores use it. Place a memory barrier (DMB/DSB) after init and use a synchronization mechanism (atomic flag, IPI) to signal readiness.
3. Use `container_of()` / intrusive design. Never allocate separate queue entry wrappers.
4. Benchmark with realistic workloads. Single-producer ping-pong latency and multi-producer contention exercise different code paths. A queue that wins one may lose the other.
5. When analyzing performance, count every atomic operation on the hot path. Check disassembly for `call atomic_*` instructions, CAS retry loops, and unexpected stub recycle paths.
6. Consider the actual access pattern. A queue configured as MPMC but used as SPSC in practice (e.g., rpmsg ping single-link test) has different characteristics than under real multi-producer load.

## Error Handling Patterns

- Bounded queues (SPSC, Ring): enqueue/dequeue return actual count transferred. Always check return value — partial operations are possible in batch mode, and 0 means queue full/empty.
- Intrusive linked lists (MPSC, SPMC, MPMC): dequeue returns `NULL` on empty queue — this is expected, not an error. Do not assert on NULL dequeue. For SPMC and MPMC the publication point is the link CAS on `prev->next`, so a NULL return means the queue is genuinely empty (no transient-empty window). MPSC is different: there is a transient-empty window between a producer's xchg on `q->tail` and its subsequent `prev->next` store, so a NULL return can mean either empty or a producer in flight — use `mpsc_dequeue_spin` / `mpsc_dequeue_yield` if the caller cannot tolerate transient NULL.
- Always drain the queue before shutdown to avoid leaking nodes. For intrusive queues, orphaned nodes mean the owning data structures are leaked.
- Ring `init` with non-power-of-2 capacity triggers DEBUGASSERT in debug builds and causes incorrect index wrapping in release builds (use-after-free risk). Always validate with `DEBUGASSERT((cap & (cap - 1)) == 0)` before calling `xxx_init()`.

## Design Conventions for New Queue Implementations

These conventions apply when creating new queue variants (based on DPDK rte_ring unified design):

- Descriptor struct contains a `void *ring` (or typed `type *buffer`) pointer field, plus capacity, mask, and index fields. Descriptor and ring buffer are always separate objects.
- `DEFINE` macro expands to two separate declarations: a static array and a struct initializer using C designated initializers. Not a composite struct. No init call needed for static definitions.
- `init` function signature: `xxx_init(struct xxx_queue_s *q, void *ring, uint32_t cap, uint32_t elem_size)` — assigns ring pointer into descriptor. Used only for manual ring specification (shared memory, dynamic allocation).
- Unified API: `xxx_enqueue(q, items, count)` / `xxx_dequeue(q, items, count)`. Count=1 for single, count>1 for batch. Returns `uint32_t` actual count (not bool). No separate `_ext` variants.
- Four-index pattern: `prod_head`, `prod_tail`, `cons_head`, `cons_tail`. For SP/SC sides, the private index is plain `uint32_t`; for MP/MC sides, the shared index is `atomic_t`.
- Cache-line padding between producer and consumer index groups to avoid false sharing.

Example usage:
```c
/* Static definition — no init call needed */
SPSC_DEFINE(my_queue, struct data_s *, 32);

/* Dynamic init — for shared memory or runtime allocation */
struct ring_queue_s q;
ring_init(&q, shared_mem_buf, 64, sizeof(void *));
```
