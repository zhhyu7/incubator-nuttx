# Audio Tunnel Lower-Half Driver Pattern — 同核进程间音频管道

本文档是 audio_tunnel 同核音频管道驱动的架构规范参考，由
[`audio_lowerhalf_pattern.md`](audio_lowerhalf_pattern.md) 路由加载。
基于 `nuttx/drivers/audio/audio_tunnel.c` 源码分析。

> **定位**：audio_tunnel 是纯本地的**同核**音频管道，用于同一 CPU 内两个应用/线程间的
> PCM 数据传输（如播放器 → 解码器）。与跨核的 `audio_rpmsg` 相对。

## 一、配置与注册

### 1.1 Kconfig

| 配置项 | 类型 | 依赖 | 说明 |
|--------|------|------|------|
| `CONFIG_AUDIO_TUNNEL` | bool | `AUDIO` | 启用 Tunnel 音频设备驱动 |

### 1.2 注册 API

```c
int audio_tunnel_initialize(const char *prefix);
```

| 参数 | 类型 | 说明 |
|------|------|------|
| `prefix` | `const char *` | 设备名前缀（如 `"a2dp"`） |

注册后生成一对设备：
- `/dev/audio/<prefix>p` — Producer 端（数据写入方）
- `/dev/audio/<prefix>c` — Consumer 端（数据读取方）

```c
/* 板级初始化代码 */
audio_tunnel_initialize("a2dp");   /* → /dev/audio/a2dpp + /dev/audio/a2dpc */

/* 多通道示例 */
audio_tunnel_initialize("a2dp");
audio_tunnel_initialize("wtp");    /* → /dev/audio/wtpp + /dev/audio/wtpc */
```

### 1.3 与 audio_rpmsg 的区别

| 维度 | audio_tunnel | audio_rpmsg |
|------|-------------|-------------|
| 通信范围 | **同核**（同一 CPU 内两个应用/线程间） | **跨核**（两个 CPU 之间） |
| 数据传输 | 直接 `memcpy`（本地内存） | 通过 RPMSG endpoint（共享内存或 payload 拷贝） |
| 依赖 | 仅 `CONFIG_AUDIO` | `CONFIG_AUDIO + CONFIG_RPMSG` |
| 典型场景 | 同核内进程间音频管道 | 跨核音频数据透传 |

## 二、原理

### 2.1 整体架构

```
┌──────────────────── 同一 CPU ────────────────────┐
│  App A (播放器/编码器)    App B (解码器/输出)       │
│       │                        ▲                  │
│  audio upper-half           audio upper-half      │
│  (/dev/audio/<prefix>p)     (/dev/audio/<prefix>c)│
│       │                        ▲                  │
│  ┌─ audio_peer_s[0] ───┐  ┌─ audio_peer_s[1] ───┐ │
│  │    (PRODUCER)        │  │    (CONSUMER)        │ │
│  │    pendq             │  │    pendq             │ │
│  └────────┬─────────────┘  └────────┬─────────────┘ │
│           └───── audio_tunnel_s ─────┘               │
│                  (共享 mutex/info/binfo)              │
│                        ▼                              │
│             audio_tunnel_deliver_buffer()             │
│             memcpy(dst->samp, src->samp)              │
└──────────────────────────────────────────────────────┘
```

Producer 和 Consumer 共享同一个 `audio_tunnel_s` 结构体，数据交换通过直接 `memcpy` 完成。

### 2.2 ioctl 处理

所有 ioctl 均为纯本地处理，不涉及远端通信：

| ioctl | 行为 | 说明 |
|-------|------|------|
| `AUDIOIOC_GETBUFFERINFO` | 读取 `tunnel->binfo` | 返回 buffer 大小和数量 |
| `AUDIOIOC_SETBUFFERINFO` | 写入 `tunnel->binfo` | 设置 buffer 参数 |
| `AUDIOIOC_GETAUDIOINFO` | 读取 `tunnel->info` | 返回音频格式信息 |
| `AUDIOIOC_HWRESET` | 向对端发送 `AUDIO_MSG_IOERR` | 通知对端发生错误 |
| 其他 | 返回 `-ENOTTY` | 不支持 |

`configure` 操作直接更新共享的 `tunnel->info`（format/samplerate/channels/subformat），两端可见。

### 2.3 数据传输模型

#### 数据交换流程

```
Producer App              audio_tunnel                Consumer App
     │  enqueuebuffer(src)   │  enqueuebuffer(dst)       │
     │──────────────────────►│◄──────────────────────────│
     │                       │                            │
     │              deliver_buffer()                      │
     │         ┌─ mutex lock                             │
     │         │  src = pendq[producer].dequeue          │
     │         │  dst = pendq[consumer].dequeue          │
     │         │  mutex unlock                           │
     │         │  memcpy(dst->samp, src->samp, nbytes)   │
     │         │                                         │
     │◄────────── callback(DEQUEUE, src)                 │
     │                       │ callback(DEQUEUE, dst) ──►│
```

#### 触发时机

`audio_tunnel_deliver_buffer()` 在以下时机被调用：

| 触发点 | 函数 | 说明 |
|--------|------|------|
| buffer 入队 | `enqueuebuffer()` | 任一端 enqueue 后立即尝试配对 |
| 开始播放 | `start()` | 配对可能已有等待的 buffer |
| 恢复播放 | `resume()` | 暂停期间可能累积了 buffer |

#### 配对条件

数据交换的前提是 **Producer 和 Consumer 的 pendq 都非空**：
- 任一端队列为空 → `deliver_buffer()` 直接返回，不做任何操作。
- 一次调用仅配对一对 buffer（src + dst），不循环。

#### 消息传递

控制消息（start/stop/pause/resume/ioerr）通过 `audio_tunnel_deliver_message()` 传递给**对端**：

```
Producer 调用 start() → Consumer 收到 AUDIO_MSG_START
Consumer 调用 stop()  → Producer 收到 AUDIO_MSG_STOP
任一端调用 HWRESET   → 对端收到 AUDIO_MSG_IOERR
```

### 2.4 线程模型

| 场景 | 执行上下文 | 说明 |
|------|-----------|------|
| enqueuebuffer | 调用者线程 | 加入 pendq 后直接调用 `deliver_buffer()` |
| deliver_buffer | 调用者线程 | 谁触发谁执行（memcpy + callback 在同一线程） |
| callback(DEQUEUE) | 调用者线程 | 直接在 `deliver_buffer()` 中回调上层 |
| start/stop/pause/resume | 调用者线程 | 直接在当前线程投递消息给对端 |

**并发保护**：
- `tunnel->mutex` 保护 `pendq`（两端队列）、`info`、`binfo`。
- **注意**：`memcpy` 和 `callback` 在 mutex 外执行，避免持锁时间过长。
- 两端可能在不同线程同时 `enqueuebuffer()`，mutex 保证队列操作原子性。

### 2.5 停止与清理时序

```
任一端调用 stop()
     ├─ deliver_message(MSG_STOP) → 对端收到停止通知
     ├─ mutex lock
     │   └─ 清空本端 pendq：逐个 dequeue → callback(DEQUEUE, apb)
     ├─ mutex unlock
     └─ callback(COMPLETE) → 通知上层停止完成
```

**要点**：
- `stop()` 只清理**自己端**的 pendq，不清理对端。
- 对端需自行调用 `stop()` 清理自己的 pendq。
- 只有一端 stop，另一端的 pending buffer 保持不动。

## 三、测试

### 3.1 基本功能测试

```bash
nsh> ls /dev/audio/
/dev/audio/a2dpp
/dev/audio/a2dpc
```

### 3.2 数据环回测试

同时打开 producer 和 consumer 设备，producer 写入数据后 consumer 应立即收到相同数据：

```bash
# 终端 1：Consumer 端录音
nsh> nxrecorder
nxrecorder> device /dev/audio/a2dpc
nxrecorder> recordraw /data/tunnel_out.pcm 2 16000 16

# 终端 2：Producer 端播放
nsh> nxplayer
nxplayer> device /dev/audio/a2dpp
nxplayer> play /data/test.pcm
```

验证 `/data/tunnel_out.pcm` 与 `/data/test.pcm` 内容一致。

### 3.3 关键验证点

| 测试项 | 验证方法 | 预期 |
|--------|---------|------|
| 设备注册 | `ls /dev/audio/` | 出现 `<prefix>p` 和 `<prefix>c` |
| 数据完整性 | 比对 Producer 输入和 Consumer 输出 | 字节级一致 |
| 流控 | Consumer 不 enqueue → Producer buffer 堆积 | Producer 的 pendq 持续增长，不丢数据 |
| 消息传递 | Producer start → Consumer 收到 MSG_START | 通过日志确认 |
| 停止清理 | stop 后检查 pendq | 本端 pendq 清空，所有 apb 归还 |
| 并发安全 | 两端高频 enqueue | 无 crash、无数据错乱 |
| FINAL 标志 | Producer 最后一个 apb 带 AUDIO_APB_FINAL | 两端都收到 COMPLETE 回调 |

### 3.4 调试日志

启用 `CONFIG_DEBUG_AUDIO_INFO` 后打印关键信息：

```plaintext
audio tunnel not ready.              # 一端队列为空，无法配对
ac_type: 1                           # configure 调用
cmd=1 arg=...                        # ioctl 调用
```

### 3.5 常见问题排查

| 现象 | 可能原因 | 排查方向 |
|------|---------|---------|
| Consumer 收不到数据 | Consumer 未 enqueue 空 buffer | 检查 Consumer 是否先提交了接收 buffer |
| 只有第一帧有数据 | deliver_buffer 仅配对一次 | 确认上层在 DEQUEUE 回调中重新 enqueue |
| 数据截断 | dst buffer 比 src 小 | 确认两端 buffer_size 一致（通过 SETBUFFERINFO） |
| stop 后对端无感知 | 对端未处理 MSG_STOP | 检查对端 upper callback 是否正确处理 message |

## Cross References

- [`audio_lowerhalf_pattern.md`](audio_lowerhalf_pattern.md) — 完整 audio lower-half 适配框架
- [`audio_lowerhalf_rules.md`](audio_lowerhalf_rules.md) — 锁/中断/buffer 生命周期硬规则
- [`audio_config_test.md`](audio_config_test.md) — Kconfig 配置 + 测试工具
