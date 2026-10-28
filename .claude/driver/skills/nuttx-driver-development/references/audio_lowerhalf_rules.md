# Audio Lower-Half 驱动实现注意事项 — 锁 / 中断 / Buffer 生命周期硬规则

本文档是 audio lower-half 驱动实现的**硬约束规则**参考，由
[`audio_lowerhalf_pattern.md`](audio_lowerhalf_pattern.md) 的「注意事项」章节路由加载。

> 背景：Vela multimedia 更新 upper-half 后，曾多次因 lower-half 不适配导致问题
> （如 VELAPLATFO-82594 死锁）。本文档沉淀了这些事故的共性规则，是**写代码和 review 时的强制检查项**，
> 不因"看起来简单"而跳过。

## 一、典型数据流与锁嵌套

### 下行路径（用户 → 硬件）：Enqueue Buffer

```c
用户空间 write/ioctl
  ↓
audio_enqueuebuffer()              // [Upper Half]
  ├─ spin_lock_irqsave(&upper->spinlock)
  ├─ 更新队列指针
  ├─ audio_try_enqueue()
  │   └─ lower->ops->enqueuebuffer()  // ⬇️ 调用 lower-half
  │       ├─ spin_lock_irqsave(&priv->lock)
  │       ├─ 操作驱动队列
  │       ├─ 启动 DMA 传输
  │       └─ spin_unlock_irqrestore(&priv->lock)
  └─ spin_unlock_irqrestore(&upper->spinlock)
```

锁嵌套：`upper->spinlock` → `priv->lock`（单向嵌套，安全）。

### 上行路径（硬件 → 用户）：Dequeue Buffer

```c
硬件中断 / DMA 完成
  ↓
lowerhalf_irq_handler()           // [Lower Half 中断处理]
  ├─ spin_lock_irqsave(&priv->lock)
  ├─ 获取完成的 buffer
  ├─ 更新驱动状态
  ├─ spin_unlock_irqrestore(&priv->lock)  // ⚠️ 必须先释放锁!
  │
  └─ dev->upper(AUDIO_CALLBACK_DEQUEUE)   // ⬆️ 回调 upper-half
      ↓
    audio_callback()               // [Upper Half]
      └─ audio_dequeuebuffer()
          ├─ spin_lock_irqsave(&upper->spinlock)
          ├─ 更新队列
          └─ spin_unlock_irqrestore(&upper->spinlock)
```

**关键**：回调前必须释放 `priv->lock`，否则形成 AB-BA 死锁。

## 二、核心规则（逐条）

> ⚠️ **核心规则**：在调用 `dev->upper()` 之前，永远不要持有任何 lower-half 私有锁。

### 规则 1：回调必须无锁（Critical: Callback Must Be Lock-Free）

调用 `dev->upper()` 回调前，必须释放 lower-half 的所有锁。

```c
// ❌ 错误 — 持有锁时回调（死锁风险!）
static void my_driver_irq_handler(...)
{
  flags = spin_lock_irqsave(&priv->lock);
  process_completed_buffer();
  dev->upper(dev->priv, AUDIO_CALLBACK_DEQUEUE, apb, OK);  // 危险!
  spin_unlock_irqrestore(&priv->lock, flags);
}

// ✅ 正确 — 无锁回调
static void my_driver_irq_handler(...)
{
  flags = spin_lock_irqsave(&priv->lock);   // 1. 锁保护下完成必要数据操作
  buf = get_completed_buffer();
  update_driver_state();
  spin_unlock_irqrestore(&priv->lock, flags);

  dev->upper(dev->priv, AUDIO_CALLBACK_DEQUEUE, buf, OK);  // 2. 释放锁后回调
}
```

**原因**：upper-half 回调会获取 `upper->spinlock`；若 lower-half 持有 `priv->lock` 再回调，
形成嵌套锁 A→B；同时用户线程可能持 `upper->spinlock` 调 lower-half，形成 B→A → **AB-BA 死锁**。

### 规则 2：锁粒度最小化（Fine-Grained Locking）

Lower-half 私有锁只保护驱动私有数据，不跨模块。

```c
struct my_audio_driver_s
{
  struct audio_lowerhalf_s dev;  // 必须是第一个成员

  spinlock_t lock;               // 私有锁，仅保护以下字段:
  uint32_t hw_position;          //   - DMA 位置
  struct list_node pending_bufs; //   - 待处理队列
  uint32_t buffer_count;         //   - 计数器
  bool is_running;               //   - 状态标志

  // 无需锁保护的字段分开放置
  uint32_t sample_rate;          // 配置参数（只在 configure 时修改）
  uint8_t channels;
};

// ✅ 只锁保护关键区域，无锁状态下操作硬件
static int my_enqueuebuffer(FAR struct audio_lowerhalf_s *dev,
                            FAR struct ap_buffer_s *apb)
{
  FAR struct my_audio_driver_s *priv = (FAR struct my_audio_driver_s *)dev;

  flags = spin_lock_irqsave(&priv->lock);
  list_add_tail(&priv->pending_bufs, apb);
  priv->buffer_count++;
  spin_unlock_irqrestore(&priv->lock, flags);

  program_dma_transfer(apb);   // 无锁操作硬件
  return OK;
}
```

**避免事项**：
- ❌ 持有锁时调用可能睡眠的函数（`nxsem_wait`、`nxmutex_lock`）
- ❌ 持有锁时调用 upper 层函数
- ❌ 持有锁时访问其他子系统的共享数据
- ❌ 锁内做复杂计算或长时间循环

### 规则 3：中断上下文规范处理（IRQ Context Best Practices）

中断处理要快速完成，最小化锁持有时间。

```c
static int my_driver_dma_irq(int irq, FAR void *context)
{
  FAR struct my_audio_driver_s *priv = context;
  FAR struct ap_buffer_s *completed_buf;
  bool should_notify;

  // 锁内只做最少必要工作
  flags = spin_lock_irqsave(&priv->lock);
  completed_buf = get_completed_dma_buffer();
  priv->completed_count++;
  should_notify = check_underrun_condition();
  spin_unlock_irqrestore(&priv->lock, flags);

  // 无锁状态下回调
  if (completed_buf)
    {
      priv->dev.upper(priv->dev.priv, AUDIO_CALLBACK_DEQUEUE, completed_buf, OK);
    }

  if (should_notify)
    {
      priv->dev.upper(priv->dev.priv, AUDIO_CALLBACK_UNDERRUN, NULL, OK);
    }

  return OK;
}
```

**中断上下文禁止操作**：
- ❌ 调用 `nxsem_wait` / `nxmutex_lock`
- ❌ `kmm_malloc` / `kmm_free`（除非使用 IRQ-safe 版本）
- ❌ 访问用户空间内存
- ❌ 打印大量日志（影响实时性）
- ❌ 复杂算法或长循环

**必须用 `spin_lock_irqsave`**（禁用本地中断），中断和用户线程都可能访问的数据：

```c
// ✅ 正确 — 禁用本地中断
flags = spin_lock_irqsave(&priv->lock);
spin_unlock_irqrestore(&priv->lock, flags);

// ❌ 错误 — 用户线程持锁时发生中断 → 死锁!
spin_lock(&priv->lock);
```

### 规则 4：Buffer 生命周期严格管理

Buffer 所有权转移流程：

```
用户提交 → enqueuebuffer(驱动接管) → 硬件处理 → 中断完成 →
上报回调(归还所有权) → upper half → 返回用户
```

```c
// enqueuebuffer: 接管 buffer 所有权
static int my_enqueuebuffer(FAR struct audio_lowerhalf_s *dev,
                            FAR struct ap_buffer_s *apb)
{
  FAR struct my_audio_driver_s *priv = (FAR struct my_audio_driver_s *)dev;

  if (apb == NULL || apb->nbytes == 0)   // 1. 验证
    return -EINVAL;

  flags = spin_lock_irqsave(&priv->lock); // 2. 入队（取得所有权）
  list_add_tail(&priv->pending, apb);
  priv->pending_count++;
  spin_unlock_irqrestore(&priv->lock, flags);

  start_dma_if_needed(priv);              // 3. 触发硬件处理
  return OK;
}

// 中断处理: 归还 buffer 所有权
static int my_dma_irq(int irq, FAR void *context)
{
  FAR struct my_audio_driver_s *priv = context;
  FAR struct ap_buffer_s *buf;

  flags = spin_lock_irqsave(&priv->lock);   // 4. 取出完成的 buffer
  buf = list_remove_head(&priv->pending);
  priv->pending_count--;
  spin_unlock_irqrestore(&priv->lock, flags);

  if (buf)                                   // 5. 归还给 upper half
    {
      buf->curbyte = buf->nbytes;            // 更新已处理字节数
      priv->dev.upper(priv->dev.priv, AUDIO_CALLBACK_DEQUEUE, buf, OK);
    }
  return OK;
}
```

**关键约定**：
- ✅ `enqueuebuffer` 成功返回后，驱动拥有 buffer
- ✅ 处理完毕后，必须通过回调归还
- ✅ 归还时设置正确的 `curbyte` 和状态
- ❌ 不能重复归还同一个 buffer
- ❌ 不能访问已归还的 buffer
- ❌ 不能修改 buffer 元数据（除了 `curbyte`）

### 规则 5：完善的错误处理与资源清理

**ioctl 未实现命令返回 `-ENOTTY`**：

```c
static int my_audio_ioctl(FAR struct audio_lowerhalf_s *dev, int cmd,
                          unsigned long arg)
{
  int ret = OK;
  switch (cmd)
    {
    /* 处理上层传来的 ioctl */
    default:
      ret = -ENOTTY;
      break;
    }
  return ret;
}
```

**shutdown 必须清理所有资源**（归还 buffer 用错误状态，且在锁外回调）：

```c
static int my_shutdown(FAR struct audio_lowerhalf_s *dev)
{
  FAR struct my_audio_driver_s *priv = (FAR struct my_audio_driver_s *)dev;
  FAR struct ap_buffer_s *buf;

  disable_dma(priv);     // 1. 停止硬件（防新中断）
  disable_codec(priv);

  while (true)           // 2. 归还所有未完成 buffer
    {
      flags = spin_lock_irqsave(&priv->lock);
      buf = list_remove_head(&priv->pending);
      spin_unlock_irqrestore(&priv->lock, flags);
      if (buf == NULL)
        break;
      priv->dev.upper(priv->dev.priv, AUDIO_CALLBACK_DEQUEUE, buf, -ECANCELED);
    }

  flags = spin_lock_irqsave(&priv->lock); // 3. 重置驱动状态
  priv->is_running = false;
  priv->pending_count = 0;
  spin_unlock_irqrestore(&priv->lock, flags);
  return OK;
}
```

**start 错误路径处理**（回退已配置的状态）：

```c
static int my_start(FAR struct audio_lowerhalf_s *dev)
{
  ret = configure_codec(priv);
  if (ret < 0)
    goto errout;

  ret = enable_dma(priv);
  if (ret < 0)
    goto errout_codec;   // 回退到已配置的状态

  flags = spin_lock_irqsave(&priv->lock);
  priv->is_running = true;
  spin_unlock_irqrestore(&priv->lock, flags);
  return OK;

errout_codec:
  disable_codec(priv);
errout:
  return ret;
}
```

**错误通知**（硬件错误通过 `AUDIO_CALLBACK_IOERR` 上报）：

```c
if (dma_has_error())
  {
    auderr("DMA error: 0x%08x\n", get_dma_error());
    flags = spin_lock_irqsave(&priv->lock);
    priv->error_count++;
    priv->is_running = false;
    spin_unlock_irqrestore(&priv->lock, flags);
    priv->dev.upper(priv->dev.priv, AUDIO_CALLBACK_IOERR, NULL, -EIO);
  }
```

**资源清理检查清单**：
- ✅ 停止所有硬件活动（DMA、定时器、中断）
- ✅ 归还所有持有的 buffer
- ✅ 释放动态分配的内存
- ✅ 重置驱动内部状态
- ✅ 所有回调在锁外调用
- ✅ 使用适当的错误码（-ECANCELED、-EIO 等）

## 三、Lower-Half 实现检查清单

开发或审查 audio lower-half 驱动时，逐项检查：

### 1. 锁的设计
- [ ] 使用 `spinlock_t` 而非 `mutex`（中断上下文不能睡眠）
- [ ] 私有锁只保护驱动私有数据
- [ ] 永远不在持有锁时调用 `dev->upper()` 回调
- [ ] 中断和线程访问共享数据时使用 `spin_lock_irqsave`
- [ ] 锁的临界区尽可能小

### 2. 中断处理
- [ ] 中断处理函数快速返回
- [ ] 在释放锁后调用回调
- [ ] 正确保存和恢复中断状态（`irqstate_t`）
- [ ] 不在中断中调用可能睡眠的函数
- [ ] 最小化锁持有时间

### 3. Buffer 管理
- [ ] `enqueuebuffer` 成功返回后拥有 buffer 所有权
- [ ] 处理完成后必须通过回调归还 buffer
- [ ] 正确设置 `apb->curbyte` 表示已处理字节
- [ ] `shutdown` 时返还所有未完成的 buffer
- [ ] 不重复归还或访问已归还的 buffer

### 4. 并发安全
- [ ] 识别所有可能并发的路径（用户/中断/work）
- [ ] 共享数据必须加锁保护
- [ ] 避免在锁内做耗时操作
- [ ] 考虑多核并发场景

### 5. 错误处理
- [ ] 所有错误路径都正确清理资源
- [ ] 硬件错误通过 `AUDIO_CALLBACK_IOERR` 通知
- [ ] 使用标准错误码（`-ENOMEM`、`-EIO`、`-ECANCELED`）
- [ ] `start/stop/shutdown` 实现正确的状态转换
- [ ] 错误发生时停止硬件避免持续中断

### 6. 接口实现
- [ ] 实现所有必需的 `audio_ops_s` 接口
- [ ] `getcaps` 返回正确的硬件能力
- [ ] `configure` 验证参数并配置硬件
- [ ] `allocbuffer` 考虑 DMA 对齐要求（如有）
- [ ] `ioctl` 处理特定的控制命令

### 7. 代码质量
- [ ] 添加适当的日志和错误信息
- [ ] 使用 `audinfo/audwarn/auderr` 宏
- [ ] 关键路径添加 `sched_note_printf` 用于追踪
- [ ] 代码注释清晰，尤其是锁的使用

## Cross References

- [`audio_lowerhalf_pattern.md`](audio_lowerhalf_pattern.md) — 完整适配框架（数据结构、注册、实现步骤）
- [`coding_rules.md`](coding_rules.md) — 同步原语、中断模式、错误清理的通用编码规则
- [`driver_review_checklist.md`](driver_review_checklist.md) — 6 维度代码审查清单
