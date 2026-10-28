# Audio Lower-Half Driver Pattern — openvela 音频下半部分驱动适配框架

本文档是 openvela 音频驱动子系统（`audio`）的架构规范与 Lower-Half 驱动适配参考。
由主 SKILL.md 的 Driver Type Dispatch Table 自动路由加载。

> 本文档基于 openvela 官方《Audio Driver 原理说明》《Audio Driver 适配说明》整理，
> 覆盖**完整 audio lower-half 驱动适配**（单一节点 + 组合节点 + 内置组件 + compress）。
>
> **适用范围辨析**（决定读哪个文档）：
> - **完整 audio lower-half 驱动**（实现 `audio_ops_s`，注册 PCM 设备节点，处理 PCM 数据流）→ 本文档。
> - **外置 I2S 智能功放**（PA/Power Amplifier，`audio_comp` companion device，不处理 PCM 数据流）→
>   [`pa_amplifier_pattern.md`](pa_amplifier_pattern.md)。
> - **同核进程间音频管道**（audio_tunnel）→ [`audio_tunnel_pattern.md`](audio_tunnel_pattern.md)。
> - **Lower-Half 实现的锁/中断/buffer 生命周期硬规则** → [`audio_lowerhalf_rules.md`](audio_lowerhalf_rules.md)。
> - **Kconfig 配置 + nxplayer/nxrecorder/nxlooper/CMocka 验证** → [`audio_config_test.md`](audio_config_test.md)。

## 一、架构分层

openvela 音频驱动采用分层架构，应用通过标准 `open`/`close`/`ioctl` 系统调用与驱动交互：

```
apps (nxplayer/nxrecorder)
    │ open/close/ioctl
    ▼
VFS (/dev/audio/...)
    │
    ▼
audio upper-half driver   ← 框架提供，统一接口 + 部分 ioctl 默认实现（与硬件无关）
    │ audio_ops_s 回调
    ▼
audio lower-half driver   ← 芯片厂商实现，封装具体硬件（与硬件相关）
    │
    ▼
硬件 (I2S / DMA / Codec / PA)
```

调用链：`apps → vfs → audio upperhalf → audio lowerhalf → 硬件`。
前三部分与芯片/产品无关，**只有 lower-half 是芯片特有**，甚至同一芯片不同产品也可能存在差异（采样率/声道数能力、DMA 通道分配、是否外置 PA）。

## 二、代码目录

| 位置 | 内容 |
|------|------|
| `nuttx/audio/` | 音频 upper-half 核心：`audio.c`、`audio_comp.c`、`Kconfig`、`Makefile` |
| `nuttx/drivers/audio/` | 内置 lower-half 驱动：`audio_dma.c`、`audio_i2s.c`、`audio_tunnel.c` |
| `nuttx/include/nuttx/audio/audio.h` | `audio_ops_s`、`audio_lowerhalf_s`、`audio_caps_s`、`AUDIOIOC_*` 等公共接口 |
| `nuttx/include/nuttx/audio/i2s.h` | `i2s_ops_s`、`i2s_dev_s`（I2S 硬件抽象） |
| `arch/<arch>/src/<chip>/` | 平台相关 lower-half（如 `arch/sim/src/sim/posix/sim_alsa.c`） |
| `boards/<arch>/<chip>/<board>/src/` | 板级注册（`audio_register` 调用点） |

## 三、核心数据结构

### 3.1 audio_lowerhalf_s（Lower-Half 核心）

```c
struct audio_lowerhalf_s
{
  /* 第一个字段必须是指向回调结构的指针 */
  FAR const struct audio_ops_s *ops;

  /* 绑定到 upper-half 的回调函数（用于 DEQUEUE buffer / 异步事件 / 错误上报） */
  FAR audio_callback_t upper;

  /* 回调时传给 upper-layer 的私有不透明指针 */
  FAR void *priv;

  /* 自定义设备状态结构可在回调结构指针之后追加字段 */
};
```

> **硬规则**：`audio_lowerhalf_s` 必须是驱动私有结构体的**第一个成员**，
> 以支持 `struct xx_dev_s` 与 `struct audio_lowerhalf_s` 之间的自由转换。

### 3.2 audio_upperhalf_s（Upper-Half 状态，框架内部）

```c
struct audio_upperhalf_s
{
  uint8_t           crefs;     /* 设备被打开的次数 */
  volatile bool     started;   /* true: 播放中 */
  mutex_t           lock;      /* 互斥锁 */
  FAR struct audio_lowerhalf_s *dev;  /* lower-half 状态 */
  struct file      *usermq;    /* 用户态应用的消息队列 */
};
```

### 3.3 audio_ops_s（必须实现的操作接口）

定义在 `include/nuttx/audio/audio.h`，含控制接口 + 数据接口：

```c
struct audio_ops_s
{
  CODE int (*getcaps)(FAR struct audio_lowerhalf_s *dev, int type,
                      FAR struct audio_caps_s *caps);   /* 获取设备能力 */
  CODE int (*configure)(FAR struct audio_lowerhalf_s *dev,
                        FAR const struct audio_caps_s *caps); /* 设置设备参数 */
  CODE int (*shutdown)(FAR struct audio_lowerhalf_s *dev);  /* 关闭设备 */
  CODE int (*start)(FAR struct audio_lowerhalf_s *dev);     /* 开始播放/录音 */
  CODE int (*stop)(FAR struct audio_lowerhalf_s *dev);      /* 结束播放/录音 */
  CODE int (*pause)(FAR struct audio_lowerhalf_s *dev);     /* 暂停 */
  CODE int (*resume)(FAR struct audio_lowerhalf_s *dev);    /* 恢复 */
  CODE int (*allocbuffer)(FAR struct audio_lowerhalf_s *dev,
                          FAR struct audio_buf_desc_s *apb);  /* 分配 buffer */
  CODE int (*freebuffer)(FAR struct audio_lowerhalf_s *dev,
                         FAR struct audio_buf_desc_s *apb);   /* 释放 buffer */
  CODE int (*enqueuebuffer)(FAR struct audio_lowerhalf_s *dev,
                            FAR struct ap_buffer_s *apb);     /* 送 buffer 给驱动 */
  CODE int (*ioctl)(FAR struct audio_lowerhalf_s *dev,
                    int cmd, unsigned long arg);              /* 扩展 ioctl */
  CODE int (*reserve)(FAR struct audio_lowerhalf_s *dev);     /* 保留设备 */
  CODE int (*release)(FAR struct audio_lowerhalf_s *dev);     /* 释放设备 */
};
```

> **可选字段**：完整 `audio_ops_s` 还包含 `cancelbuffer`（取消已入队 buffer）、`read`、`write`
> 字段。多数 lower-half 将这三者实现为 `NULL`（只有 `audio_comp` 组合设备才实现
> `cancelbuffer`/`read`/`write`）。上面列出的 12 个字段是适配 lower-half 时的**最小功能集**。
>
> **opts 表条件编译**：
> - `.stop` 受 `CONFIG_AUDIO_EXCLUDE_STOP` 保护
> - `.pause`/`.resume` 受 `CONFIG_AUDIO_EXCLUDE_PAUSE_RESUME` 保护
> - 所有 ops 回调**必须支持 `CONFIG_AUDIO_MULTI_SESSION` 编译分支**（开启后函数指针签名增加 `session` 参数）

### 3.4 数据交互结构体

| 结构体 | 作用 | 关键字段 |
|--------|------|---------|
| `ap_buffer_s` | 音频数据载体 | `samp`(数据存储位置)、`nmaxbytes`(最大容量)、`nbytes`(实际数据大小)、`curbytes`(当前处理偏移)、`nsamples`(样本数) |
| `audio_buf_desc_s` | `ap_buffer_s` 的描述体 | 作为各 ioctl 的参数 |
| `audio_caps_s` | 能力/参数描述 union | `ac_type`、`ac_subtype`、`ac_controls`(union)、`ac_channels`、`ac_format` |

## 四、设备节点注册

### 4.1 audio_register（单一节点）

```c
int audio_register(FAR const char *name, FAR struct audio_lowerhalf_s *dev);
```

- `name`：设备节点名（如 `"pcm0p"`），注册到 `/dev/audio/` 下。
- `dev`：lower-half 驱动结构指针。
- 返回值：`0` 成功，负值失败。

实现要点（框架内部完成，lower-half 无需关心）：
1. 分配并清零 `audio_upperhalf_s`，`nxmutex_init`。
2. `upper->dev = dev`。
3. `dev->upper = audio_callback`（绑定回调），`dev->priv = upper`。
4. `register_driver(path, &g_audioops, 0666, upper)`。

`g_audioops`（upper-half 的 VFS 文件操作）：
```c
static const struct file_operations g_audioops =
{
  audio_open,  audio_close, audio_read, audio_write,
  NULL /*seek*/, audio_ioctl /*ioctl*/
};
```

### 4.2 audio_comp_initialize（组合节点）

```c
FAR struct audio_lowerhalf_s *audio_comp_initialize(FAR const char *name, ...);
```

功能类似 Linux ASoC 的 `platform/dai/codec` 组合，但 openvela 把所有音频驱动统一抽象为
`audio_lowerhalf`，**不再区分 platform/dai/codec**。

- **不定长参数**：多个 `audio_lowerhalf *` 驱动组合注册为一个 PCM 设备（`NULL` 结尾）。
- **适用场景**：`i2s lowerhalf` 由芯片平台固化，`PA lowerhalf` 按项目适配，通过组合节点提高灵活性。

内部状态 `audio_comp_priv_s`：
```c
struct audio_comp_priv_s
{
  struct audio_lowerhalf_s export;   /* 出口，与 upper-half 对接（必须第一成员） */
  FAR struct audio_lowerhalf_s **lower;  /* 包含的低级设备数组 */
  int count;                          /* 低级设备数量 */
};
```

流程：分配 `priv` → `priv->export.ops = &g_audio_comp_ops` → 遍历 va_arg 统计 lower 数量 →
分配 lower 数组 → 每个 lower 设 `tmp->upper = audio_comp_callback`、`tmp->priv = priv` →
`audio_register(name, &priv->export)`。

`g_audio_comp_ops` 将各操作（configure/start/stop/pause/resume/allocbuffer/…）**依次转发**给每个
lower-half。例如 `audio_comp_configure` 会遍历调用每个 `lower[i]->ops->configure`。

### 4.3 何时注册

设备节点注册发生在系统初始化阶段，调用栈：

```
nx_start() → up_initialize() → audio_register(...)
```

Lower-Half 驱动的注册通常在板级初始化（如 `board_early_initialize`）完成：

```c
audio_register("pcm0p", xx_audio_initialize(...));
audio_register("pcm0c", xx_audio_initialize(...));
```

> **单一节点 vs 组合节点**：
> - 单一节点：一个 `audio_lowerhalf` 完整实现所有控制 + 数据接口。
> - 组合节点：一个较复杂设备实现完整接口，其余简单设备（如独立 PA）只实现部分接口。

## 五、AUDIOIOC 对外接口

应用通过 `ioctl` 系统调用使用音频功能，`AUDIOIOC_*` 定义在 `include/nuttx/audio/audio.h`：

```c
AUDIOIOC_GETCAPS(1)      AUDIOIOC_RESERVE(2)      AUDIOIOC_RELEASE(3)
AUDIOIOC_CONFIGURE(4)    AUDIOIOC_SHUTDOWN(5)     AUDIOIOC_START(6)
AUDIOIOC_STOP(7)         AUDIOIOC_PAUSE(8)        AUDIOIOC_RESUME(9)
AUDIOIOC_GETBUFFERINFO(10) AUDIOIOC_ALLOCBUFFER(11) AUDIOIOC_FREEBUFFER(12)
AUDIOIOC_ENQUEUEBUFFER(13) AUDIOIOC_REGISTERMQ(14) AUDIOIOC_UNREGISTERMQ(15)
AUDIOIOC_HWRESET(16)     AUDIOIOC_SETBUFFERINFO(17) AUDIOIOC_SETPARAMTER(18)
AUDIOIOC_GETLATENCY(19)  AUDIOIOC_FLUSH(20)
```

> lower-half 驱动通常需要支持的 `ioctl`：`AUDIOIOC_SETBUFFERINFO`（设置 buffer 信息）、
> `AUDIOIOC_GETBUFFERINFO`（获取 buffer 信息）、`AUDIOIOC_SETPARAMTER`（`key=value` 格式设置参数）、
> `AUDIOIOC_GETLATENCY`（获取驱动播放延迟）。

## 六、Lower-Half 驱动实现步骤

### 步骤 1：定义私有数据结构

`audio_lowerhalf_s` 必须是第一个成员：

```c
struct xx_audio_dev_s
{
  struct audio_lowerhalf_s dev;   /* 必须第一成员，支持类型转换 */

  /* 私有变量：硬件寄存器基地址、工作线程 ID、状态标志等 */
  pthread_t threadid;
  bool paused;
  /* ... */
};
```

### 步骤 2：实现 initialize 初始化函数

分配私有结构体，并关联 `audio_ops_s` 操作集：

```c
struct audio_lowerhalf_s *xx_audio_initialize(...)
{
  struct xx_audio_dev_s *priv;

  priv = kmm_zalloc(sizeof(struct xx_audio_dev_s));
  if (!priv)
    {
      return NULL;
    }

  priv->dev.ops = &g_xx_audio_ops;   /* 关键：绑定 ops */

  /* 其他初始化 ... */

  return &priv->dev;
}
```

### 步骤 3：注册设备节点

```c
audio_register("pcm0c", xx_audio_initialize(...));
audio_register("pcm0p", xx_audio_initialize(...));
```

### 步骤 4：定义 audio_ops_s 操作集

```c
static const struct audio_ops_s g_xx_audio_ops =
{
  .getcaps       = xx_audio_getcaps,
  .configure     = xx_audio_configure,
  .shutdown      = xx_audio_shutdown,
  .start         = xx_audio_start,
#ifndef CONFIG_AUDIO_EXCLUDE_STOP
  .stop          = xx_audio_stop,
#endif
#ifndef CONFIG_AUDIO_EXCLUDE_PAUSE_RESUME
  .pause         = xx_audio_pause,
  .resume        = xx_audio_resume,
#endif
  .allocbuffer   = xx_audio_allocbuffer,
  .freebuffer    = xx_audio_freebuffer,
  .enqueuebuffer = xx_audio_enqueuebuffer,
  .ioctl         = xx_audio_ioctl,
  .reserve       = xx_audio_reserve,
  .release       = xx_audio_release,
};
```

### 步骤 5：实现各接口函数

#### getcaps — 获取设备能力（分阶段查询）

上层分三阶段查询，驱动需在 `caps` 中填对应信息：

| 阶段 | 上层查询 | 驱动返回 |
|------|---------|---------|
| 1 类型+主格式 | `ac_type=AUDIO_TYPE_QUERY`, `ac_subtype=AUDIO_TYPE_QUERY` | `ac_controls.b[0]`=设备类型(INPUT/OUTPUT)，`ac_format.hw`=主格式(如 `(1<<(AUDIO_FMT_PCM-1))`) |
| 2 指定类型详情 | `ac_type=AUDIO_TYPE_OUTPUT/INPUT`, `ac_subtype=AUDIO_TYPE_QUERY` | `ac_channels`=声道数，`ac_controls.hw[0]`=采样率位掩码 |
| 3 子格式 | `ac_type=AUDIO_TYPE_QUERY`, `ac_subtype`=主格式(如 `AUDIO_FMT_PCM`) | 具体子格式(如 `AUDIO_SUBFMT_PCM_S16_LE`)，`AUDIO_SUBFMT_END` 结尾 |

**ac_channels 编码规则**（关键，易错）：
- 低 4 位 = 最大通道数，高 4 位 = 最小通道数（无限制设 0）。
- 示例：1~2 通道 `0x12`；仅 2 通道 `0x22`；仅 1 通道 `0x11`。

完整 getcaps 实现见下方「关键实现细节」。

#### configure — 配置音频参数

从 `caps` 提取采样率/采样精度/通道数并配置硬件：

```c
static int xx_audio_configure(FAR struct audio_lowerhalf_s *dev,
                              FAR const struct audio_caps_s *caps)
{
  FAR struct xx_dev_s *priv = (FAR struct xx_dev_s *)dev;
  switch (caps->ac_type)
    {
      case AUDIO_TYPE_OUTPUT:
        priv->samprate  = caps->ac_controls.hw[0];
        priv->nchannels = caps->ac_channels;
        priv->bpsamp    = caps->ac_controls.b[2];

        xx_setmclkfrequency(priv);
        xx_settxchannels(priv);
        xx_setdatawidth(priv);
        xx_setbitrate(priv);
        break;
      /* ... AUDIO_TYPE_INPUT 等 */
    }
}
```

#### ioctl — 扩展控制命令

建议支持 `AUDIOIOC_SETBUFFERINFO`/`AUDIOIOC_GETBUFFERINFO`（数量 + 大小）和 `AUDIOIOC_GETLATENCY`。
驱动应根据硬件能力（如 DMA 描述符限制）决定 buffer 参数是否可动态调整。未实现命令返回 `-ENOTTY`。

#### allocbuffer / freebuffer — 分配/释放缓冲区（按需）

DMA 场景需要 lower-half 自行分配（`audio_dma` 分配连续地址直接对应 DMA 源/目标地址，
应用数据可直接写入 DMA 缓冲区，省一次拷贝）。默认 `apb_alloc` 一次性分配
`sizeof(ap_buffer_s) + numbytes`（`apb->samp` 与结构体连续），会多一次拷贝。

#### enqueuebuffer — 入队缓冲区

- 播放：应用将含数据的 buffer 传给驱动，驱动交给硬件播放。
- 录音：应用传空 buffer，驱动填充后传回。

推荐实现：`struct dq_queue_s pendq` 队列保存 buffer + work thread + 消息队列异步处理：

```c
static int xx_audio_enqueuebuffer(FAR struct audio_lowerhalf_s *dev,
                                  FAR struct ap_buffer_s *apb)
{
  FAR struct xx_dev_s *priv = (FAR struct xx_dev_s *)dev;
  struct audio_msg_s term_msg;

  apb_reference(apb);                     /* 增加引用计数，防提前释放 */
  apb->flags |= AUDIO_APB_OUTPUT_ENQUEUED;
  /* 入队到共享 pendq；实际实现须用 spin_lock_irqsave 保护，见 audio_lowerhalf_rules.md 规则 2 */
  dq_addlast(&apb->dq_entry, &priv->pendq);

  if (priv->mq.f_inode != NULL)           /* 通知 work thread */
    {
      term_msg.msg_id = AUDIO_MSG_ENQUEUE;
      term_msg.u.data = 0;
      file_mq_send(&priv->mq, (FAR const char *)&term_msg,
                   sizeof(term_msg), CONFIG_XX_MSG_PRIO);
    }
  return OK;
}
```

#### start — 启动音频流

推荐流程：① 创建消息队列 → ② 创建工作线程（处理实际传输）→ ③ 使能 DMA/I2S 外设。
工作线程循环监听 `AUDIO_MSG_ENQUEUE`/`AUDIO_MSG_STOP` 等消息，从 `pendq` 取 buffer 处理。

#### stop — 优雅停止

发送 `AUDIO_MSG_STOP`，工作线程**等所有已缓冲数据播放完后**再停止，`pthread_join` 等待线程退出。
返回所有在途 buffer（`AUDIO_CALLBACK_DEQUEUE`），最后 `AUDIO_CALLBACK_COMPLETE` 通知上层。

> **注意**：`AUDIO_MSG_STOP` 不是立即停止，而是等缓存数据播放完再停。

#### pause / resume — 暂停/恢复

方式一：向 work thread 发 `AUDIO_MSG_PAUSE`/`AUDIO_MSG_RESUME` 消息。
方式二：用变量同步状态（`priv->paused = true/false`）。

> **注意**：暂停期间**不得**通过 `AUDIO_CALLBACK_DEQUEUE` 回调向上层返回 buffer。
> resume 后恢复回调，向上层发送 DEQUEUE 消息返回 buffer。

#### reserve / release / shutdown

- `reserve`：与硬件无关，保留并实现该接口。
- `release`：播放/录制完成后，释放相关资源。
- `shutdown`：驱动模块卸载时调用，最终资源清理（归还所有 buffer、停止硬件）。

## 七、关键实现细节

### 7.1 getcaps 完整实现参考

```c
static int bes_rpmsg_aud_svr_getcaps(FAR struct audio_lowerhalf_s *dev,
                                     int type, FAR struct audio_caps_s *caps)
{
  DEBUGASSERT(caps->ac_len >= sizeof(struct audio_caps_s));
  caps->ac_format.hw = 0;
  caps->ac_controls.w = 0;

  switch (caps->ac_type)
    {
    case AUDIO_TYPE_QUERY:                     /* 查询支持的类型 */
      switch (caps->ac_subtype)
        {
        case AUDIO_TYPE_QUERY:
          caps->ac_controls.b[0] =
            (isCaptureDev(dev) ? AUDIO_TYPE_INPUT : AUDIO_TYPE_OUTPUT) |
            AUDIO_TYPE_FEATURE | AUDIO_TYPE_PROCESSING;
          caps->ac_format.hw = (1 << (AUDIO_FMT_PCM - 1));
          break;
        case AUDIO_FMT_MIDI:
          caps->ac_controls.b[0] = AUDIO_SUBFMT_END;
          break;
        case AUDIO_FMT_PCM:
          caps->ac_controls.b[0] = AUDIO_SUBFMT_PCM_S16_LE;
          caps->ac_controls.b[1] = AUDIO_SUBFMT_END;
          break;
        default:
          caps->ac_controls.b[0] = AUDIO_SUBFMT_END;
          break;
        }
      break;

    case AUDIO_TYPE_OUTPUT:                    /* OUTPUT 能力 */
      switch (caps->ac_subtype)
        {
        case AUDIO_TYPE_QUERY:
          caps->ac_channels = CONFIG_AUDIO_BES_OUTPUT_CHANNELS;
          caps->ac_controls.hw[0] = AUDIO_SAMP_RATE_8K | AUDIO_SAMP_RATE_11K |
                                     AUDIO_SAMP_RATE_16K | AUDIO_SAMP_RATE_22K |
                                     AUDIO_SAMP_RATE_32K | AUDIO_SAMP_RATE_44K |
                                     AUDIO_SAMP_RATE_48K;
          break;
        /* ... */
        }
      break;

    case AUDIO_TYPE_FEATURE:                   /* 音量/低音/高音等 */
      if (caps->ac_subtype == AUDIO_FU_UNDEF)
        {
          caps->ac_controls.b[0] = AUDIO_FU_VOLUME | AUDIO_FU_BASS | AUDIO_FU_TREBLE;
          caps->ac_controls.b[1] = AUDIO_FU_BALANCE >> 8;
        }
      break;

    case AUDIO_TYPE_PROCESSING:                /* 处理单元 */
      if (caps->ac_subtype == AUDIO_PU_UNDEF)
        caps->ac_controls.b[0] = AUDIO_PU_STEREO_EXTENDER;
      break;

    default:
      caps->ac_subtype = 0;
      caps->ac_channels = 0;
      break;
    }

  return caps->ac_len;
}
```

### 7.2 AUDIOIOC_SETPARAMTER

通用参数设置接口，`arg` 为 `char *` 字符串，格式严格 `"key=value"`。用于传递非标准、平台特定配置：

- `"scenario=phone"` — 通话场景
- `"scenario=music"` — 音乐场景

## 八、内置可复用组件

适配前先评估复用这些通用 lower-half 驱动，避免重复实现：

### 8.1 audio_dma（`drivers/audio/audio_dma.c`）

通用 DMA 封装，完整实现数据接口，控制接口下沉到 DMA 模块（`dma_ops_s`）。

- `configure` 最终调用 `DMA_CONFIG(chan, cfg)`（`chan->ops->config`）。
- `allocbuffer` 分配连续地址直接对应 DMA 源/目标地址（`apb->samp`），应用数据直接写入 DMA 缓冲区，**省一次拷贝**。
- 处理机制：`audio_dma_callback`（DMA 中断）或高优先级工作线程（等待 I2S/DMA 完成回调唤醒）。
- **复用方式**：复用 `audio_dma` lower-half 时，只需重新实现 `dma_ops_s` 操作集。

### 8.2 audio_i2s（`drivers/audio/audio_i2s.c`）

通用 I2S 封装，完整实现数据接口，控制接口下沉到 I2S 模块（`i2s_ops_s`）。

`i2s_ops_s` 接口（硬件厂商实现）：
- 接收：`i2s_rxchannels`/`i2s_rxsamplerate`/`i2s_rxdatawidth`/`i2s_receive`
- 发送：`i2s_txchannels`/`i2s_txsamplerate`/`i2s_txdatawidth`/`i2s_send`
- 主时钟：`i2s_mclkfrequency`
- 控制：`i2s_ioctl`

`audio_i2s_configure` 中按 `ac_type` 调用 `I2S_TXCHANNELS`/`I2S_TXDATAWIDTH`/`I2S_TXSAMPLERATE`
（或 RX 版本）。`enqueuebuffer` 中播放走 `I2S_SEND`，录音走 `I2S_RECEIVE`。

> 厂商实现 `i2s_send()`/`i2s_receive()` 时，PCM 传输完成需调用 `audio_i2s_callback`
> （触发 `AUDIO_CALLBACK_DEQUEUE` 回调归还 buffer）。

### 8.3 audio_comp（组合节点，见 §4.2）

### 8.4 sim_alsa（`arch/sim/src/sim/posix/sim_alsa.c`）

Simulator 平台桥接主机 ALSA 能力，作为基础参考。`sim_audio_initialize(true/false)` 注册 `pcm0p`/`pcm0c`。

## 九、Compress 能力

类似 ALSA 的 `compress` 节点，用于压缩音频数据的解码/输出（如独立 DSP 编解码场景）：

- 应用通过 `ENQUEUE` 将压缩数据传给 compress 节点。
- 驱动内部通过 RPC 与 DSP 通信完成解码/播放。
- sim 平台注册 `pcm1p`/`pcm1c` 模拟 compress，借助主机 `libmad`/`lame` 完成 MP3 编解码。

## 十、适配前分析清单

编写代码前必须完成：

1. **硬件能力**：音频接口（I2S/PCM）控制逻辑与数据格式、DMA 控制器用法、Codec/PA 控制方式（I2C/SPI）、
   支持的采样率/位深/声道数范围。
2. **理解框架**：完整测试应用（nxplayer 等）、统一 `audio_ops_s` 接口、标准化调用流程、松耦合组合节点、
   内置可复用组件、丰富参考示例（`sim_alsa.c`、song-u1 开源驱动）。
3. **明确适配目标**：
   - 目标 1：从零实现完整音频节点（同时实现数据流 I2S/DMA + 控制流 I2C/SPI）。
   - 目标 2：替换/添加独立 Lower-Half（如更新 PA，仅实现控制接口）。

## 十一、注意事项（详见 audio_lowerhalf_rules.md）

| 注意项 | 要求 |
|--------|------|
| 接口耗时 | 所有 `audio_ops_s` 接口避免长时间阻塞，控制在 **10ms 以内**（过长如 `start` 40ms 会导致录音 overflow） |
| 缓冲区管理 | **绝不能丢失** `enqueuebuffer` 传入的 `ap_buffer_s`（丢失指针 = 内存泄漏），所有 buffer 必须通过 `upper()` 回调返回 |
| 回调锁 | 调用 `dev->upper()` 回调前**必须释放 lower-half 所有锁**（否则 AB-BA 死锁） |

> 完整实现硬规则（锁设计 / 中断上下文 / buffer 生命周期 / 错误处理 / 检查清单）见
> [`audio_lowerhalf_rules.md`](audio_lowerhalf_rules.md)。

## Cross References

- [`audio_lowerhalf_rules.md`](audio_lowerhalf_rules.md) — 锁/中断/buffer/错误处理硬规则 + 7 维检查清单
- [`audio_tunnel_pattern.md`](audio_tunnel_pattern.md) — 同核进程间音频管道驱动（audio_tunnel）
- [`audio_config_test.md`](audio_config_test.md) — Kconfig 配置 + nxplayer/nxrecorder/nxlooper/CMocka 验证
- [`pa_amplifier_pattern.md`](pa_amplifier_pattern.md) — 外置 I2S 智能功放（PA，audio_comp companion）
- [`bus_access.md`](bus_access.md) — I2C/SPI 寄存器访问（Codec/PA 控制接口用）
- [`coding_rules.md`](coding_rules.md) — 编码规范、内核 API、中断规则、同步原语
- [`board_registration.md`](board_registration.md) — 板级注册（audio_register 调用点）
