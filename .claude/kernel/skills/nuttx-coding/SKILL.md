---
name: nuttx-coding
description: "NuttX/Vela kernel and application coding guide. Covers three areas: (1) Synchronization — spinlock vs mutex vs semaphore vs atomic selection, lock correctness review, deadlock/race condition debugging; (2) Async patterns — work_queue top-half/bottom-half, IRQ-semaphore-thread signaling, message queue; (3) Task & pthread — task vs pthread lifecycle, safe thread exit patterns, why pthread_cancel is unsafe. Use when: choosing locks, reviewing lock usage, writing ISR handlers, using work_queue, creating threads, exiting threads, or any mention of spin_lock, mutex, semaphore, critical_section, atomic, work_queue, HPWORK, LPWORK, pthread, task_create, pthread_cancel, pthread_kill, force_exit, 锁, 同步, 死锁, 竞态, 中断下半部, 线程退出, 线程安全."
---

# NuttX Coding Guide

# NuttX Coding Guide

---

# Part 1: Synchronization

## Decision Table — Pick the Right Primitive

| Scenario | Primitive | Key Constraint |
|----------|-----------|---------------|
| Simple integer (refcount etc.) | `atomic_xxx` (`<stdatomic.h>`) | Lowest overhead, no lock needed |
| Short-term, IRQ-safe, **driver code** | `spin_lock_irqsave` / `spin_unlock_irqrestore` | **NO scheduling APIs inside** |
| Short-term, need post-type wakeup | `spin_lock_irqsave_nopreempt` | post-type OK, **wait-type still NO** |
| Kernel internal, need sleep/wake inside | `enter_critical_section` / `leave_critical_section` | Both wait & post OK; **avoid in drivers** |
| Long-term hold, thread context only | `nxmutex_lock` / `nxmutex_unlock` | Cannot use in IRQ context |
| Long-term hold, recursive needed | `nxrmutex_lock` / `nxrmutex_unlock` | Cannot use in IRQ context |
| Notification (not mutual exclusion) | `nxsem_post` / `nxsem_wait` | Semaphore is for signaling, not locking |
| User-space only | `pthread_mutex_lock` / `pthread_mutex_unlock` | Kernel code must NOT use pthread APIs |

## Scheduling API Classification

Two categories of APIs that cause scheduling:

- **wait-type** (current task may sleep): `sleep`, `usleep`, `nxsem_wait`, `nxmutex_lock`, `poll`, `read`(blocking), etc.
- **post-type** (may wake a higher-priority task): `nxsem_post`, `nxmutex_unlock`, `work_queue`, etc.

### What You Can Call While Holding Each Lock

```
                          wait-type    post-type
                          (sleep/lock) (sem_post/wakeup)
spin_lock_irqsave            ✗            ✗
spin_lock_irqsave_nopreempt  ✗            ✓
enter_critical_section       ✓            ✓
nxmutex_lock                 ✓            ✓
```

## Hard Rules (Violations = Bugs)

1. **spin_lock_irqsave**: NO scheduling APIs of any kind inside. No sleep, no sem_wait, no sem_post.
2. **spin_lock_irqsave_nopreempt**: Only post-type APIs allowed. Still NO wait-type.
3. **enter_critical_section**: Both types OK, but **do NOT use in driver code** — use `spin_lock_irqsave` instead. `enter_critical_section` uses a global kernel lock which is too coarse for drivers.
4. **Kernel code** (`nuttx/`, `vendor/chip/`): Use atomic → spinlock → critical_section → nxmutex (items 0-5 in overhead order). **Never use pthread APIs.**
5. **Application code** (everything else): Use atomic and pthread APIs only. **Never use nxmutex/nxsem/spinlock directly.**
6. **IRQ handlers and idle thread**: Cannot use mutex or semaphore.
7. **Never use `sched_lock`/`sched_unlock` alone for synchronization** — it fails on SMP.
8. **Never use bare `spin_lock`/`spin_unlock` in driver code** — always use `spin_lock_irqsave`/`spin_unlock_irqrestore` instead. Reasons:
   - On single-core (no `CONFIG_SPINLOCK`): `spin_lock` is a **no-op** (`#define spin_lock(lock) ((void)(lock))`), provides zero protection. `spin_lock_irqsave` degrades to `irq_save()` which still disables interrupts and protects the critical section.
   - On SMP: if the same lock is also taken in an IRQ handler, the IRQ can preempt the lock holder and deadlock trying to acquire the same lock.
   - Bare `spin_lock` only makes sense for pure thread-to-thread protection where the lock is **never** taken in IRQ context — this is extremely rare in driver code.

## Per-Thread Interrupt State

Vela kernel tracks interrupt disable state **per thread** (in TCB). This has a critical implication:

After `enter_critical_section` / `irq_save`, if a context switch happens (via sem_post waking a higher-priority thread, or sem_wait sleeping), the **new thread's own interrupt state** takes effect — NOT the caller's. When the original thread is scheduled back, its saved interrupt state is restored.

This means: inside `enter_critical_section`, calling sem_post may immediately switch to another thread that runs with interrupts **enabled**, even though the caller had interrupts disabled. The caller's critical section resumes (with interrupts re-disabled) only when it gets scheduled back.

## Sync Code Review Checklist

- [ ] Is the primitive appropriate for the hold duration? (short → spinlock, long → mutex)
- [ ] If holding spinlock: are there ANY function calls that might sleep or wake tasks?
- [ ] If using `spin_lock_irqsave_nopreempt`: verify only post-type APIs are called, no wait-type
- [ ] If using `enter_critical_section` in a driver: suggest switching to `spin_lock_irqsave`
- [ ] Is the lock scope as narrow as possible?
- [ ] On SMP: is the shared resource actually protected across CPUs? (local irq disable alone is insufficient)
- [ ] Recursive locking: is `nxrmutex` or `rspinlock` used instead of plain variants?
- [ ] Lock ordering: if multiple locks are acquired, is the order consistent to prevent deadlock?

## Common Sync Patterns

### Driver: Protect shared state accessed from IRQ and thread

```c
/* In driver private struct */
spinlock_t lock;

/* Thread context */
irqstate_t flags = spin_lock_irqsave(&priv->lock);
/* ... access shared data ... */
spin_unlock_irqrestore(&priv->lock, flags);

/* IRQ handler — same lock */
irqstate_t flags = spin_lock_irqsave(&priv->lock);
/* ... access shared data ... */
spin_unlock_irqrestore(&priv->lock, flags);
```

### Driver: IRQ handler needs to wake a worker thread

```c
/* IRQ handler — use nopreempt variant to allow sem_post */
irqstate_t flags = spin_lock_irqsave_nopreempt(&priv->lock);
/* ... update state ... */
nxsem_post(&priv->waitsem);  /* post-type: OK */
spin_unlock_irqrestore_nopreempt(&priv->lock, flags);
```

### Atomic refcount

```c
#include <stdatomic.h>

atomic_int refs;

if (atomic_fetch_add(&refs, 1) == 0)
  {
    /* First reference — initialize resource */
  }

if (atomic_fetch_sub(&refs, 1) == 1)
  {
    /* Last reference — cleanup resource */
  }
```

---

# Part 2: Async Patterns

## Pattern Selection — When to Use What

| Need | Pattern | Key Trait |
|------|---------|-----------|
| Defer work from IRQ (no data to pass) | work_queue (HPWORK) | Fire-and-forget, no allocation |
| IRQ notifies thread, thread processes | semaphore signaling | Lowest overhead for simple wake |
| Pass structured data between tasks | message queue (mqueue) | Buffered, priority-aware |
| Periodic background task | work_queue (LPWORK) + re-queue | Timer-like, no dedicated thread |

## Anti-Patterns (Common Bugs)

- **Busy-wait in thread**: Polling a flag in a loop wastes CPU. Use semaphore wait instead.
- **Loop-sleep polling**: `while (!ready) usleep(1000)` adds latency and wastes power. Use semaphore.
- **Heavy processing in ISR**: Loops, printf, blocking calls in IRQ handler. Defer to work_queue.
- **Creating threads unnecessarily**: On single-core, more threads = more stack + more context switches, not more parallelism. Prefer work_queue when possible.

## Pattern 1: Top-Half / Bottom-Half (work_queue)

The standard pattern for interrupt handling. ISR (top-half) does minimal work, defers processing to a worker thread (bottom-half).

```c
/* In device private struct — work_s must be persistent, NOT on stack */
struct my_dev_s {
  struct work_s work;
  /* ... */
};

/* Top-half: ISR — keep it minimal */
static int my_interrupt(int irq, FAR void *context, FAR void *arg)
{
  FAR struct my_dev_s *priv = arg;

  /* Only: read status, clear IRQ flag, disable IRQ if needed */

  if (work_available(&priv->work))
    {
      work_queue(HPWORK, &priv->work, my_worker, priv, 0);
    }

  return OK;
}

/* Bottom-half: runs in worker thread context — safe to do heavy work */
static void my_worker(FAR void *arg)
{
  FAR struct my_dev_s *priv = arg;

  /* Safe to: loop, lock mutex, call blocking APIs, process data */

  /* Re-enable IRQ after processing */
  up_enable_irq(priv->irq);
}
```

Rules:
- `struct work_s` must be in persistent storage (device struct / global), never a local variable
- Check `work_available()` before queueing to avoid double-queue
- Use `HPWORK` for time-sensitive interrupt bottom-halves
- Use `LPWORK` for background / non-urgent tasks
- Use `work_cancel_sync()` in driver teardown to ensure worker has finished

## Pattern 2: IRQ-Semaphore-Thread (signaling)

For drivers where a dedicated thread processes data on each interrupt.

```c
struct my_dev_s {
  sem_t waitsem;
  spinlock_t lock;
};

/* Init */
nxsem_init(&priv->waitsem, 0, 0);  /* Initial count 0 */

/* Worker thread — blocks until IRQ signals */
static int my_thread(int argc, FAR char *argv[])
{
  while (1)
    {
      nxsem_wait_uninterruptible(&priv->waitsem);
      /* Process data */
    }
}

/* ISR — signal the thread */
static int my_interrupt(int irq, FAR void *context, FAR void *arg)
{
  FAR struct my_dev_s *priv = arg;
  int semcount;

  nxsem_get_value(&priv->waitsem, &semcount);
  if (semcount < 1)
    {
      nxsem_post(&priv->waitsem);  /* Wake the thread */
    }

  return OK;
}
```

Rules:
- Use `nxsem_wait_uninterruptible` — survives signals without spurious wakeup
- Check `semcount < 1` before post to prevent unbounded count growth
- Semaphore is for **notification**, not mutual exclusion — don't use it as a lock

## Pattern 3: Message Queue (structured data passing)

For passing typed messages between tasks with priority support.

```c
/* Open queue */
struct file mqd;
struct mq_attr attr = {
  .mq_maxmsg  = 8,           /* Queue depth */
  .mq_msgsize = sizeof(msg), /* Message size */
};
file_mq_open(&mqd, "/my_queue", O_RDWR | O_CREAT, 0666, &attr);

/* Producer: send */
file_mq_send(&mqd, (FAR const char *)&msg, sizeof(msg), priority);

/* Consumer: receive (blocks if empty) */
file_mq_receive(&mqd, (FAR char *)&msg, sizeof(msg), NULL);

/* Cleanup */
file_mq_close(&mqd);
```

Rules:
- Set `mq_maxmsg` and `mq_msgsize` carefully — pre-allocated, wastes RAM if oversized
- Messages are copied, not zero-copy — for large data, send a pointer instead
- Supports priority ordering — higher priority messages dequeued first

## Communication Overhead Comparison

```
                    RAM Cost          Latency     Data Passing
work_queue          ~0 (reuse pool)   Low         Via arg pointer only
semaphore           ~32 bytes         Lowest      None (signal only)
message queue       mq_maxmsg×size    Medium      Copied, priority-aware
```

## Async Code Review Checklist

- [ ] ISR does minimal work? No loops, no printf, no blocking calls?
- [ ] `struct work_s` is persistent (not stack-local)?
- [ ] `work_available()` checked before `work_queue()` to prevent double-queue?
- [ ] `work_cancel_sync()` called in driver cleanup/teardown?
- [ ] Semaphore count bounded? (check semcount before post)
- [ ] Using `nxsem_wait_uninterruptible` instead of `nxsem_wait`?
- [ ] Message queue sizes reasonable for the use case?
- [ ] Thread creation justified? Could work_queue replace it?

---

# Part 3: Task & Pthread

## Task vs Pthread — Key Differences

NuttX 的 task 和 pthread 类似 Linux 的进程和线程：

| | Task (`task_create`) | Pthread (`pthread_create`) |
|---|---|---|
| 类比 | Linux 进程 | Linux 线程 |
| 地址空间 | FLAT 模式下共享，PROTECTED 模式下独立 | 共享父 task 的地址空间 |
| 文件描述符 | 独立副本（fork 语义） | 共享父 task 的文件描述符 |
| 生命周期 | 独立，父 task 退出不影响子 task | **依附于父 task，父 task 退出则 pthread 被强制终止** |
| 退出方式 | `exit()` / `return` from main | `pthread_exit()` / `return` from thread func |
| 等待 | `waitpid()` | `pthread_join()` |
| 信号 | 独立信号处理 | 共享 task group 的信号处理 |

## Hard Rules (Violations = Bugs)

1. **主 task 退出前，必须确保所有子 pthread 已退出。** 主 task exit 会触发 task group 清理，强制终止所有未退出的 pthread，导致资源泄漏、数据损坏。
2. **禁止使用 `pthread_cancel()`。** 原因：
   - 异步取消（`PTHREAD_CANCEL_ASYNCHRONOUS`）可能在任意指令处中断线程，导致锁未释放、内存泄漏、数据不一致。
   - 延迟取消（`PTHREAD_CANCEL_DEFERRED`）依赖 cancellation points，但很多代码路径没有正确处理取消后的清理。
   - 需要 `CONFIG_CANCELLATION_POINTS` 才能正常工作，很多板级配置未开启。
   - 实际代码中极难保证所有资源都通过 `pthread_cleanup_push/pop` 正确注册。
3. **子 pthread 退出必须是自愿的** — 由子线程自己决定何时退出、如何清理资源，主线程只负责通知。
4. **Signal handler 里禁止调用系统 API。** 信号处理函数运行在被中断的上下文中，只能做简单赋值运算（设置 flag），**严禁调用** `printf`、`syslog`、`malloc`、`free`、`sem_post` 等非 async-signal-safe 函数。违反会导致死锁（如 printf 持有 stdout 锁时被信号中断，handler 里再调 printf 死锁）或数据损坏。
5. **珍爱生命，远离 signal。** Signal 机制复杂、易出错、难调试。能用标志位解决的不要用 signal，能用 sem_post 唤醒的不要用 signal。只在必须打断阻塞系统调用时才用 signal，且 handler 保持为空函数或仅设一个 flag。

## 安全退出模式

### 模式 A：全局标志位（推荐，最简单）

主线程设置标志位，子线程轮询检查后自行清理退出。

```c
struct app_context_s
{
  volatile bool exit_flag;    /* 退出标志 */
  pthread_t     worker;       /* 子线程 */
  /* ... other shared state ... */
};

/* 子线程：循环中检查退出标志 */
static FAR void *worker_thread(FAR void *arg)
{
  FAR struct app_context_s *ctx = arg;

  while (!ctx->exit_flag)
    {
      /* 正常工作 ... */
      do_some_work(ctx);
    }

  /* 自行清理资源 */
  cleanup_resources(ctx);
  return NULL;
}

/* 主线程：通知退出并等待 */
static void app_shutdown(FAR struct app_context_s *ctx)
{
  ctx->exit_flag = true;           /* 1. 设置标志 */
  pthread_join(ctx->worker, NULL); /* 2. 等待子线程自行退出 */
}
```

适用场景：子线程有短周期循环，能及时检查标志位。

局限：如果子线程阻塞在 `read()` / `poll()` / `sleep()` 等系统调用上，无法及时看到标志位变化。

### 模式 B：信号 + 标志位（推荐，适合有阻塞调用的场景）

主线程发送信号打断阻塞调用，子线程在信号处理函数中设置退出标志，然后自行清理退出。

参考实现：`apps/system/cu/cu_main.c`

```c
struct app_context_s
{
  bool       force_exit;   /* 退出标志 */
  pthread_t  listener;     /* 子线程 */
  int        devfd;        /* 可能阻塞的 fd */
};

/* 子线程的信号处理函数 — 仅打断阻塞，不做复杂操作 */
static void sigusr1_handler(int sig)
{
  /* 空函数即可 — 信号本身会打断 read/poll/sleep 等阻塞调用，
   * 使其返回 -1 并设置 errno = EINTR */
}

/* 子线程 */
static FAR void *listener_thread(FAR void *arg)
{
  FAR struct app_context_s *ctx = arg;
  struct sigaction sa;

  /* 注册信号处理 */
  memset(&sa, 0, sizeof(sa));
  sa.sa_handler = sigusr1_handler;
  sigaction(SIGUSR1, &sa, NULL);

  while (!ctx->force_exit)
    {
      char buf[256];
      int rc = read(ctx->devfd, buf, sizeof(buf));  /* 可能阻塞 */
      if (rc <= 0)
        {
          /* read 被信号打断返回 -1/EINTR，或 fd 关闭返回 0 */
          break;
        }

      process_data(buf, rc);
    }

  cleanup_resources(ctx);
  return NULL;
}

/* 主线程退出流程 */
static void app_shutdown(FAR struct app_context_s *ctx)
{
  ctx->force_exit = true;                    /* 1. 设置标志 */
  pthread_kill(ctx->listener, SIGUSR1);      /* 2. 发信号打断阻塞 */
  pthread_join(ctx->listener, NULL);         /* 3. 等待子线程退出 */
}
```

关键点：
- 信号处理函数可以是空函数 — 目的只是打断阻塞的系统调用（使其返回 `EINTR`）
- 也可以在 `sigaction` 中设置退出标志（如 `cu_exit` 用 `sa.sa_sigaction` + `si_user` 传递上下文）
- **handler 里严禁调用 printf/syslog/malloc 等系统 API** — 只做 `flag = true` 这类简单赋值
- `pthread_kill` 只发送信号给指定线程，不影响其他线程
- 必须在 `pthread_kill` 之后调用 `pthread_join` 等待子线程真正退出
- 能不用 signal 就不用 — 优先考虑标志位（模式 A）或 sem_post 唤醒

参考实现：`apps/netutils/dhcpc/dhcpc.c` (`dhcpc_cancel` 函数)

```c
/* dhcpc 的退出模式：flag + signal + join */
void dhcpc_cancel(FAR void *handle)
{
  struct dhcpc_state_s *pdhcpc = handle;

  pdhcpc->cancel = true;                          /* 1. 设置标志 */
  pthread_kill(pdhcpc->thread, SIGQUIT);           /* 2. 打断 sleep() */
  pthread_join(pdhcpc->thread, NULL);              /* 3. 等待退出 */
}

/* dhcpc 子线程在安全点检查标志 */
static void *dhcpc_run(void *args)
{
  while (1)
    {
      ret = dhcpc_request(pdhcpc, &result);        /* 网络操作 */
      if (pdhcpc->cancel) return NULL;             /* 检查点 1 */

      result.lease_time = sleep(result.lease_time); /* 可被信号打断 */
      if (pdhcpc->cancel) return NULL;             /* 检查点 2 */
    }
}
```

### 模式选择

| 场景 | 推荐模式 |
|------|---------|
| 子线程有短周期循环，无长时间阻塞 | 模式 A：全局标志位 |
| 子线程阻塞在 read/poll/sleep | 模式 B：信号 + 标志位 |
| 子线程阻塞在 sem_wait | 用 `nxsem_post` 唤醒 + 标志位 |
| 需要兼容 `CONFIG_DISABLE_SIGNALS` | 标志位 + `pthread_cancel` 作为 fallback |

## Thread Code Review Checklist

- [ ] 主 task 退出前是否 `pthread_join` 了所有子 pthread？
- [ ] 是否使用了 `pthread_cancel`？如果是，建议改为标志位 + 信号模式
- [ ] 子线程有阻塞调用时，是否有信号机制打断阻塞？
- [ ] 退出标志是否声明为 `volatile` 或使用 `atomic`？
- [ ] 子线程退出前是否正确释放了所有资源（fd、malloc、锁）？
- [ ] `pthread_kill` 之后是否紧跟 `pthread_join`？
- [ ] 信号处理函数是否足够简单（只设标志或空函数）？严禁 printf/syslog/malloc
- [ ] 是否真的需要 signal？能否用标志位或 sem_post 替代？

---

# API Reference — Where to Find Current Headers

| Primitive | Header to read |
|-----------|---------------|
| spinlock | `nuttx/include/nuttx/spinlock.h` |
| mutex | `nuttx/include/nuttx/mutex.h` |
| semaphore | `nuttx/include/nuttx/semaphore.h` |
| atomic | `<stdatomic.h>` (toolchain) |
| irq save/restore | `nuttx/include/nuttx/irq.h` + arch-specific `irq.h` |
| critical section | `nuttx/include/nuttx/spinlock.h` (enter/leave_critical_section) |
| sched lock | `nuttx/include/nuttx/sched.h` |
| work_queue | `nuttx/include/nuttx/wqueue.h` |
| message queue | `nuttx/include/nuttx/mqueue.h` |
| kthread_create | `nuttx/include/nuttx/kthread.h` |
| pthread | `nuttx/include/pthread.h` |
| signal | `nuttx/include/signal.h` |
