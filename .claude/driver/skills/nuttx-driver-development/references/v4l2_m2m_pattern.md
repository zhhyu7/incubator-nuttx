# V4L2 M2M 编解码器适配 Pattern

> 本文档面向在 NuttX/Vela 上适配 `V4L2 M2M`（memory-to-memory）视频编解码器驱动的场景，
> 基于 `include/nuttx/video/v4l2_m2m.h` 与 `drivers/video/v4l2_m2m.c` 的框架实现，
> 以 `arch/sim/src/sim/sim_decoder.c`（H264 解码）与 `sim_encoder.c`（H264 编码）为参考驱动。
>
> 硬约束：所有回调签名以 `v4l2_m2m.h` 为准，源码与预期不符处以源码为准并在本文档如实反映。

---

## 架构概览

V4L2 M2M 框架把「应用/FFmpeg/nxcodec → 硬件编解码器」的调用链拆成四层，驱动作者只需实现
最底层的 `codec_ops_s` 回调集合：

```
┌─────────────────────────────────────────────────────────────┐
│ 应用层      FFmpeg / nxcodec / mediatool                     │
│            open() / ioctl(VIDIOC_*) / mmap() / poll()        │
└───────────────────────────┬─────────────────────────────────┘
                            │ POSIX VFS (fs/)
┌───────────────────────────▼─────────────────────────────────┐
│ v4l2_core.c   v4l2_ioctl() 按 cmd 分发到 v4l2_ops_s          │
│               (drivers/video/v4l2_core.c)                    │
└───────────────────────────┬─────────────────────────────────┘
                            │ video_register() 注册的 g_v4l2_fops
┌───────────────────────────▼─────────────────────────────────┐
│ v4l2_m2m.c    通用 M2M 状态机：open/close、REQBUFS/QBUF/DQBUF │
│               buffer 队列管理(video_framebuff)、poll、event   │
│               通过 CODEC_* 宏回调驱动侧的 codec_ops_s          │
└───────────────────────────┬─────────────────────────────────┘
                            │ CODEC_XXX(codec, priv, ...) 宏
┌───────────────────────────▼─────────────────────────────────┐
│ 驱动侧        struct codec_ops_s (本 pattern 要实现的全部)    │
│               openh264 / x264 / 硬件编解码引擎                │
└─────────────────────────────────────────────────────────────┘
```

**数据流方向（decoder 与 encoder 相反）**——V4L2 M2M 术语中，`output` 队列永远是
「驱动要消费的输入数据」，`capture` 队列永远是「驱动产出的结果数据」：

| 类型 | output 队列（输入） | capture 队列（输出） | 方向 |
|------|--------------------|----------------------|------|
| decoder 解码 | 压缩码流（`V4L2_PIX_FMT_H264`） | 原始帧（`V4L2_PIX_FMT_YUV420`） | 压缩 → 原始 |
| encoder 编码 | 原始帧（`V4L2_PIX_FMT_YUV420`） | 压缩码流（`V4L2_PIX_FMT_H264`） | 原始 → 压缩 |

- **decoder**：应用把 H264 码流 `QBUF` 到 output 队列，驱动解码后把 YUV `QBUF` 到 capture 队列。
- **encoder**：应用把 YUV 原始帧 `QBUF` 到 output 队列，驱动编码后把 H264 `QBUF` 到 capture 队列。

两者唯一的本质区别是 `output_enum_fmt` / `capture_enum_fmt`、`output_s_fmt` / `capture_s_fmt`
填写的 `pixelformat` 与 `sizeimage` 算法互换，`codec_ops_s` 骨架完全相同（见 §9）。

---

## 核心结构 codec_ops_s

`include/nuttx/video/v4l2_m2m.h` 中定义，`struct codec_s` 仅含一个 `ops` 指针：

```c
struct codec_s
{
  FAR const struct codec_ops_s *ops;
};

struct codec_ops_s
{
  CODE int (*open)(FAR void *cookie, FAR void **priv);
  CODE int (*close)(FAR void *priv);

  CODE int (*capture_streamon)(FAR void *priv);
  CODE int (*output_streamon)(FAR void *priv);
  CODE int (*capture_streamoff)(FAR void *priv);
  CODE int (*output_streamoff)(FAR void *priv);

  CODE int (*capture_available)(FAR void *priv);
  CODE int (*output_available)(FAR void *priv);

  /* VIDIOC_QUERYCAP handler */

  CODE int (*querycap)(FAR void *priv,
                       FAR struct v4l2_capability *cap);

  /* VIDIOC_ENUM_FMT handlers */

  CODE int (*capture_enum_fmt)(FAR void *priv,
                               FAR struct v4l2_fmtdesc *fmt);

  CODE int (*output_enum_fmt)(FAR void *priv,
                              FAR struct v4l2_fmtdesc *fmt);

  /* VIDIOC_G_FMT handlers */

  CODE int (*capture_g_fmt)(FAR void *priv,
                            FAR struct v4l2_format *fmt);
  CODE int (*output_g_fmt)(FAR void *priv,
                           FAR struct v4l2_format *fmt);

  /* VIDIOC_S_FMT handlers */

  CODE int (*capture_s_fmt)(FAR void *priv,
                            FAR struct v4l2_format *fmt);
  CODE int (*output_s_fmt)(FAR void *priv,
                           FAR struct v4l2_format *fmt);

  /* VIDIOC_TRY_FMT handlers */

  CODE int (*capture_try_fmt)(FAR void *priv,
                              FAR struct v4l2_format *fmt);
  CODE int (*output_try_fmt)(FAR void *priv,
                             FAR struct v4l2_format *fmt);

  /* Buffer handlers  */

  CODE size_t (*capture_g_bufsize)(FAR void *priv);
  CODE size_t (*output_g_bufsize)(FAR void *priv);

  CODE size_t (*capture_g_bufcnt)(FAR void *priv);
  CODE size_t (*output_g_bufcnt)(FAR void *priv);

  CODE void *(*alloc_buf)(FAR void *priv, size_t size);
  CODE void (*free_buf)(FAR void *priv, FAR void *addr);

  /* Memory mode query interface.
  *  If the driver does not implement this interface, the v4l2-m2m
  *  default driver only supports V4L2_MEMORY_MMAP and V4L2_MEMORY_USERPTR.
  */

  CODE int (*capture_try_memory)(FAR void *priv, enum v4l2_memory mem);
  CODE int (*output_try_memory)(FAR void *priv, enum v4l2_memory mem);

  /* Stream type-dependent parameter ioctls */

  CODE int (*capture_g_parm)(FAR void *priv,
                             FAR struct v4l2_streamparm *parm);
  CODE int (*output_g_parm)(FAR void *priv,
                            FAR struct v4l2_streamparm *parm);
  CODE int (*capture_s_parm)(FAR void *priv,
                             FAR struct v4l2_streamparm *parm);
  CODE int (*output_s_parm)(FAR void *priv,
                            FAR struct v4l2_streamparm *parm);

  /* Control handlers */

  CODE int (*g_ext_ctrls)(FAR void *priv,
                          FAR struct v4l2_ext_controls *ctrls);
  CODE int (*s_ext_ctrls)(FAR void *priv,
                          FAR struct v4l2_ext_controls *ctrls);

  /* Crop ioctls */

  CODE int (*capture_g_selection)(FAR void *priv,
                                  FAR struct v4l2_selection *clip);
  CODE int (*output_g_selection)(FAR void *priv,
                                 FAR struct v4l2_selection *clip);
  CODE int (*capture_s_selection)(FAR void *priv,
                                  FAR struct v4l2_selection *clip);
  CODE int (*output_s_selection)(FAR void *priv,
                                 FAR struct v4l2_selection *clip);
  CODE int (*capture_cropcap)(FAR void *priv,
                              FAR struct v4l2_cropcap *cropcap);
  CODE int (*output_cropcap)(FAR void *priv,
                             FAR struct v4l2_cropcap *cropcap);

  /* Event handlers */

  CODE int (*subscribe_event)(FAR void *priv,
                              FAR struct v4l2_event_subscription *sub);

  /* Command handlers */

  CODE int (*decoder_cmd)(FAR void *priv,
                          FAR struct v4l2_decoder_cmd *cmd);
  CODE int (*encoder_cmd)(FAR void *priv,
                          FAR struct v4l2_encoder_cmd *cmd);
};
```

`codec_ops_s` 共 **40** 个函数指针字段。字段职责与调用时机如下（`type` 由框架用
`V4L2_TYPE_IS_OUTPUT(type)` 判定后分派到 `*_output_*` 或 `*_capture_*`）：

| # | 字段 | 职责 | 调用时机 | 对应 ioctl |
|---|------|------|----------|------------|
| 1 | `open` | 分配驱动私有实例，`*priv` 回填；`cookie` 保存供缓冲区 API 使用 | 每次设备 `open()`（支持多实例） | `open()` |
| 2 | `close` | 释放 `open` 分配的私有实例 | 每次设备 `close()` | `close()` |
| 3 | `capture_streamon` | 启动 capture 侧数据流，触发首轮 work | `VIDIOC_STREAMON`，type=capture | `VIDIOC_STREAMON` |
| 4 | `output_streamon` | 启动 output 侧（通常在此做编解码引擎延迟初始化） | `VIDIOC_STREAMON`，type=output | `VIDIOC_STREAMON` |
| 5 | `capture_streamoff` | 停止 capture 侧 | `VIDIOC_STREAMOFF`，type=capture | `VIDIOC_STREAMOFF` |
| 6 | `output_streamoff` | 停止 output 侧（通常置 flush 标志冲刷残留帧） | `VIDIOC_STREAMOFF`，type=output | `VIDIOC_STREAMOFF` |
| 7 | `capture_available` | capture 队列有 buffer 入队时被触发，驱动可再派 work | `VIDIOC_QBUF`，type=capture | `VIDIOC_QBUF` |
| 8 | `output_available` | output 队列有 buffer 入队时被触发 | `VIDIOC_QBUF`，type=output | `VIDIOC_QBUF` |
| 9 | `querycap` | 回填 driver/card/capabilities（`V4L2_CAP_VIDEO_M2M`） | `VIDIOC_QUERYCAP` | `VIDIOC_QUERYCAP` |
| 10 | `capture_enum_fmt` | 按 `fmt->index` 枚举 capture 支持的 pixelformat | `VIDIOC_ENUM_FMT`，type=capture | `VIDIOC_ENUM_FMT` |
| 11 | `output_enum_fmt` | 按 `fmt->index` 枚举 output 支持的 pixelformat | `VIDIOC_ENUM_FMT`，type=output | `VIDIOC_ENUM_FMT` |
| 12 | `capture_g_fmt` | 回读 capture 当前格式 | `VIDIOC_G_FMT`，type=capture | `VIDIOC_G_FMT` |
| 13 | `output_g_fmt` | 回读 output 当前格式 | `VIDIOC_G_FMT`，type=output | `VIDIOC_G_FMT` |
| 14 | `capture_s_fmt` | 设置 capture 格式并算 `sizeimage` | `VIDIOC_S_FMT`，type=capture | `VIDIOC_S_FMT` |
| 15 | `output_s_fmt` | 设置 output 格式并算 `sizeimage` | `VIDIOC_S_FMT`，type=output | `VIDIOC_S_FMT` |
| 16 | `capture_try_fmt` | 校验 capture 格式是否支持 | `VIDIOC_TRY_FMT`，type=capture | `VIDIOC_TRY_FMT` |
| 17 | `output_try_fmt` | 校验 output 格式是否支持 | `VIDIOC_TRY_FMT`，type=output | `VIDIOC_TRY_FMT` |
| 18 | `capture_g_bufsize` | 返回 capture 单 buffer 字节数（`sizeimage`） | `VIDIOC_REQBUFS` / `VIDIOC_QUERYBUF` 内部 | （框架内部） |
| 19 | `output_g_bufsize` | 返回 output 单 buffer 字节数 | `VIDIOC_REQBUFS` / `VIDIOC_QUERYBUF` 内部 | （框架内部） |
| 20 | `capture_g_bufcnt` | 返回 capture 允许的最大 buffer 数 | `VIDIOC_REQBUFS` 内部 | （框架内部） |
| 21 | `output_g_bufcnt` | 返回 output 允许的最大 buffer 数 | `VIDIOC_REQBUFS` 内部 | （框架内部） |
| 22 | `alloc_buf` | 自定义 MMAP 堆分配（NULL 时框架用 `kumm_memalign`） | `VIDIOC_REQBUFS`（MMAP 模式） | （框架内部） |
| 23 | `free_buf` | 释放 `alloc_buf` 分配的堆 | `close()` / `REQBUFS` 重分配 | （框架内部） |
| 24 | `capture_try_memory` | 校验 capture 内存模式（MMAP/USERPTR/DMABUF） | `VIDIOC_REQBUFS` 内部 | （框架内部） |
| 25 | `output_try_memory` | 校验 output 内存模式 | `VIDIOC_REQBUFS` 内部 | （框架内部） |
| 26 | `capture_g_parm` | 回读 capture 流参数（帧率等） | `VIDIOC_G_PARM`，type=capture | `VIDIOC_G_PARM` |
| 27 | `output_g_parm` | 回读 output 流参数 | `VIDIOC_G_PARM`，type=output | `VIDIOC_G_PARM` |
| 28 | `capture_s_parm` | 设置 capture 流参数 | `VIDIOC_S_PARM`，type=capture | `VIDIOC_S_PARM` |
| 29 | `output_s_parm` | 设置 output 流参数 | `VIDIOC_S_PARM`，type=output | `VIDIOC_S_PARM` |
| 30 | `g_ext_ctrls` | 回读扩展控制（B 帧数等） | `VIDIOC_G_EXT_CTRLS` | `VIDIOC_G_EXT_CTRLS` |
| 31 | `s_ext_ctrls` | 设置扩展控制 | `VIDIOC_S_EXT_CTRLS` | `VIDIOC_S_EXT_CTRLS` |
| 32 | `capture_g_selection` | 回读 capture 选区/裁剪 | `VIDIOC_G_SELECTION`，type=capture | `VIDIOC_G_SELECTION` |
| 33 | `output_g_selection` | 回读 output 选区/裁剪 | `VIDIOC_G_SELECTION`，type=output | `VIDIOC_G_SELECTION` |
| 34 | `capture_s_selection` | 设置 capture 选区/裁剪 | `VIDIOC_S_SELECTION`，type=capture | `VIDIOC_S_SELECTION` |
| 35 | `output_s_selection` | 设置 output 选区/裁剪 | `VIDIOC_S_SELECTION`，type=output | `VIDIOC_S_SELECTION` |
| 36 | `capture_cropcap` | 回读 capture 裁剪能力 | `VIDIOC_CROPCAP`，type=capture | `VIDIOC_CROPCAP` |
| 37 | `output_cropcap` | 回读 output 裁剪能力 | `VIDIOC_CROPCAP`，type=output | `VIDIOC_CROPCAP` |
| 38 | `subscribe_event` | 订阅事件（`V4L2_EVENT_EOS`） | `VIDIOC_SUBSCRIBE_EVENT` | `VIDIOC_SUBSCRIBE_EVENT` |
| 39 | `decoder_cmd` | 解码器命令（如 `V4L2_DEC_CMD_STOP`） | `VIDIOC_DECODER_CMD` | `VIDIOC_DECODER_CMD` |
| 40 | `encoder_cmd` | 编码器命令 | `VIDIOC_ENCODER_CMD` | `VIDIOC_ENCODER_CMD` |

> ⚠️ 注意：`v4l2_m2m.h` 定义了 `CODEC_DQEVENT` 宏（引用 `(codec)->ops->dqevent`），但
> `struct codec_ops_s` 中**并不存在 `dqevent` 字段**。`VIDIOC_DQEVENT` 由框架
> `v4l2_m2m.c` 的 `codec_dqevent()` 内部事件队列处理，驱动无需（也无法）实现 dqevent 回调；
> `CODEC_DQEVENT` 宏是全仓库无调用的残留定义，勿在驱动中使用。

每个回调都有对应的 `CODEC_*` 访问宏（`CODEC_OPEN`/`CODEC_CAPTURE_STREAMON`/…），
宏在回调为 NULL 时返回 `-ENOTTY`（`available` 返回 0，`g_bufcnt` 返回
`V4L2_REQBUFS_COUNT_MAX`，`try_memory` 做三种模式默认校验）。框架侧（`v4l2_m2m.c`）
统一通过这些宏间接调用，因此**未实现的回调留 NULL 即可，不必写桩**。

---

## 注册 API

```c
int codec_register(FAR const char *devpath, FAR struct codec_s *codec);
int codec_unregister(FAR const char *devpath);
```

- `codec_register(devpath, codec)`：`devpath` 必须以 `/` 开头（如 `/dev/video1`）。
  内部 `kmm_zalloc` 一个 `codec_mng_s`，固定绑定 `v4l2.vops = &g_codec_vops`、
  `v4l2.fops = &g_codec_fops`，再经 `video_register()` → `register_driver()` 注册字符设备。
- **多实例**：`codec_register` 天然支持多实例——对同一份 `struct codec_s`（或不同 codec）
  用不同 `devpath` 多次调用即可注册多个独立设备节点；每个设备节点被多次 `open()` 时，
  框架为每次 open 分配独立的 `codec_file_s`（含独立的 output/capture 队列），并回调驱动的
  `open` 让驱动分配独立 `priv`。因此驱动私有状态必须全部挂在 `priv` 上，禁止用全局变量存实例状态。
- **调用时机**：通常在驱动 `xxx_initialize()`（板级初始化阶段）调用；卸载时调 `codec_unregister`。

```c
int sim_decoder_initialize(void)
{
  return codec_register(CONFIG_SIM_VIDEO_DECODER_DEV_PATH,
                        &g_sim_codec_decoder);
}
```

---

## 框架缓冲区 API

以下 5 个 API 是驱动在 work 回调里拿/还 buffer 的唯一入口，`cookie` 即 `open` 回调收到的
`cookie` 参数（类型 `struct codec_file_s *`，代表本次 open 的实例）：

```c
FAR struct v4l2_buffer *codec_output_get_buf(FAR void *cookie);
FAR struct v4l2_buffer *codec_capture_get_buf(FAR void *cookie);
int codec_output_put_buf(FAR void *cookie, FAR struct v4l2_buffer *buf);
int codec_capture_put_buf(FAR void *cookie, FAR struct v4l2_buffer *buf);
int codec_queue_event(FAR void *cookie, FAR struct v4l2_event *evt);
```

| API | 语义 |
|-----|------|
| `codec_output_get_buf` | 从 output 队列取一个「应用已入队」的空闲 buffer 容器，无可用时返回 `NULL` |
| `codec_capture_get_buf` | 从 capture 队列取一个「应用已入队」的空闲 buffer 容器，无可用时返回 `NULL` |
| `codec_output_put_buf` | 归还 output buffer（`capture_done`），并 `poll_notify` POLLOUT 通知应用可继续入队 |
| `codec_capture_put_buf` | 归还 capture buffer，`poll_notify` POLLIN 通知应用可 `DQBUF`；带 `V4L2_BUF_FLAG_LAST` 时置 buflast |
| `codec_queue_event` | 入队一个 `struct v4l2_event`（如 `V4L2_EVENT_EOS`），`poll_notify` POLLPRI |

**解码 5 步数据流示例**（`sim_decoder_work` 的核心节奏）：

```c
src_buf = codec_output_get_buf(sim_decoder->cookie);   /* 1. 取 output：压缩码流 */
if (src_buf == NULL && !sim_decoder->flushing)
  return;                                               /* 无输入且非冲刷则直接返回 */

dst_buf = codec_capture_get_buf(sim_decoder->cookie);   /* 2. 取 capture：YUV 目标帧 */
if (dst_buf == NULL)
  return;

ret = sim_decoder_process(sim_decoder, dst_buf, src_buf); /* 3. 硬件解码：
                                                            *    读 src_buf->m.vaddr / bytesused，
                                                            *    写入 dst_buf->m.vaddr，
                                                            *    回填 dst_buf->bytesused / flags / timestamp，
                                                            *    内部调 codec_output_put_buf(4) 与
                                                            *    codec_capture_put_buf(5) */
if (ret > 0)
  work_queue(HPWORK, &sim_decoder->work, sim_decoder_work, sim_decoder, 0);
```

即 5 步为：**get output → get capture → 硬件解码写入 → put output → put capture**。
`codec_output_put_buf` 与 `codec_capture_put_buf` 通常在 `process` 内部按「输入被消费 /
输出已就绪」的时点分别调用，见 §5 参考驱动的 `sim_decoder_process`。

---

## 内存模式（Buffer Type）默认约定

下表是 Vela 侧编解码器适配的**默认约定**，`v4l2_requestbuffers.memory` 与
`v4l2_buffer.memory` 按同一模式设置（两个 ioctl 结构体中的 `memory` 必须一致）：

| 模式 | 输入队列 (output) | 输出队列 (capture) |
|------|------------------|-------------------|
| 解码 decoder | `V4L2_MEMORY_USERPTR` | `V4L2_MEMORY_DMABUF` |
| 编码 encoder | `V4L2_MEMORY_DMABUF` | `V4L2_MEMORY_MMAP` |

三种模式的框架行为（源码 `v4l2_m2m.c` 的 `codec_reqbufs`/`codec_qbuf`/`codec_dqbuf`）：

- **`V4L2_MEMORY_DMABUF`**：需开启 `CONFIG_DMABUF_HEAPS`（`select CONFIG_DMA_SHARED_BUFFER`，
  源码 `drivers/dma-buf/`，头文件 `include/nuttx/dma-heap.h`）。`QBUF` 时框架对
  `buf->m.fd` 执行 `dma_buf_get → dma_buf_attach → dma_buf_map_attachment(DMA_BIDIRECTIONAL)`，
  把映射后的 `dmaaddr` 填入 `container->buf.m.vaddr`；`DQBUF` 时 `codec_release_dmabuf` 释放。
  该路径由 `#ifdef CONFIG_DMA_SHARED_BUFFER` 保护。
- **`V4L2_MEMORY_MMAP`**：沿用框架默认分配——`alloc_buf` 为 NULL 时用
  `kumm_memalign(32, count * bufsize)` 建 `bufheap`，buffer 地址 = `bufheap + bufsize * index`。
  mmap offset 上 output 队列从 0 起、capture 队列从 `CAPTURE_BUF_OFFSET (1<<30)` 起区分。
- **`V4L2_MEMORY_USERPTR`**：由应用管理内存，`QBUF` 时框架把 `buf->m.userptr` 填入 `vaddr`。

> ⚠️ 框架默认校验与实际注释不一致：`v4l2_m2m.h` 的 `CODEC_CAPTURE_TRY_MEMORY` /
> `CODEC_OUTPUT_TRY_MEMORY` 注释称「未实现时仅支持 MMAP 与 USERPTR」，但宏的实际实现
> 对 `V4L2_MEMORY_MMAP / V4L2_MEMORY_USERPTR / V4L2_MEMORY_DMABUF` 三种均放行（返回 0）。
> 若要严格限制队列内存模式，驱动应显式实现 `output_try_memory` / `capture_try_memory`。
> 另：`apps/system/nxcodec` 的示例当前对两队列统一用 `V4L2_MEMORY_MMAP`（最简可跑），
> 上表为产品化默认约定，按实际链路（camera/display 的 DMA-BUF 来源）调整。

---

## 参考驱动锚点

### sim_decoder.c（H264 解码）

- **openh264 延迟初始化推迟到 `output_streamon`**：`openh264_decoder_open()` 仅 `calloc`
  包装结构（`sim_openh264dec.c`），真正的 `WelsCreateDecoder` + `Initialize` 放在
  `openh264_decoder_streamon()`，由 `sim_decoder_output_streamon()` 触发。
  好处：设备 `open()` 不加载重编解码引擎，直到 output 队列真正 `STREAMON` 才初始化。
- **work_queue 触发模式**：`sim_decoder_t` 内嵌 `struct work_s work`，凡
  `capture_streamon` / `output_available` / `capture_available` / `output_streamoff`
  都 `work_queue(HPWORK, &work, sim_decoder_work, sim_decoder, 0)` 派活；`sim_decoder_work`
  尾部在 `ret > 0`（还有待处理帧）时自我续派，形成「消费-产出」循环。
- **flush 标志**：`output_streamoff` 里 `flushing = true` 再派 work；work 中
  `src_buf == NULL && flushing` 时仍继续取 capture buffer，用 `data == NULL` 调
  `DecodeFrame2`/`FlushFrame` 冲刷解码器残帧，`ret == 0 && src_buf == NULL` 时置
  `V4L2_BUF_FLAG_LAST` 并 `codec_queue_event(V4L2_EVENT_EOS)`，随后清 `flushing`。
- **enum/s_fmt 的 format 选择**：capture 固定 `V4L2_PIX_FMT_YUV420`、output 固定
  `V4L2_PIX_FMT_H264`（见 §6 bufsize 算法）。

### sim_encoder.c（H264 编码，方向相反同构）

- `codec_ops_s` 骨架与 decoder 完全同构，仅 format 互换：output=`V4L2_PIX_FMT_YUV420`、
  capture=`V4L2_PIX_FMT_H264`。
- 多了 `capture_s_parm`（从 `parm.parm.capture.timeperframe` 推导 `fps`）与
  `g_ext_ctrls`/`s_ext_ctrls`（`V4L2_CID_MPEG_VIDEO_B_FRAMES` 设 B 帧数）。
- `output_streamon` 把 `output_fmt` 的宽高、`fps`、`bframe` 一次性传给 `x264_wrapper_streamon`。
- flush 语义相同：`output_streamoff` 置 `flushing`，冲刷后置 `V4L2_BUF_FLAG_LAST` + EOS 事件。

> ⚠️ 类型细节：`codec_ops_s` 中 `capture_g_bufsize`/`output_g_bufsize` 声明为返回 `size_t`，
> 但 sim 参考驱动实现为返回 `uint32_t`（`static uint32_t sim_decoder_capture_g_bufsize(...)`）。
> 编译无碍（赋值时隐式转换），新驱动建议直接返回 `size_t` 与头文件一致。

---

## 关键注意事项

1. **bufsize 上限算法**（以 640×480 YUV420 为例）：
   - YUV420 原始帧 = `width * height * 3 / 2` = `640 * 480 * 3 / 2 = 460800` 字节。
   - decoder 的 **capture（YUV）** `sizeimage = w * h * 3 / 2`（sim_decoder_capture_s_fmt）。
   - decoder 的 **output（H264 压缩）** `sizeimage = (w * h * 3 / 2) / 2 + 128`
     （sim_decoder_output_s_fmt），即原始帧的一半再加 128 字节余量：
     640×480 → `460800 / 2 + 128 = 230528`（≈ 230400 + 128）。encoder 方向对称：
     output=YUV 用 `w*h*3/2`，capture=H264 用 `(w*h*3/2)/2 + 128`。
   - `g_bufsize` 必须返回与 `s_fmt` 里 `sizeimage` 一致的值，否则 REQBUFS 会按错误尺寸
     建 `bufheap` 导致越界。

2. **零拷贝默认 vs 可选 alloc_buf/free_buf**：
   - `alloc_buf`/`free_buf` 为 NULL 时，MMAP 模式框架用 `kumm_memalign(32, count * bufsize)`
     分配（零拷贝：驱动直接操作 `buf->m.vaddr`）。
   - 需要特殊对齐/连续物理内存时，实现 `alloc_buf(priv, size)` / `free_buf(priv, addr)`，
     框架在 REQBUFS 与 close 时改走驱动自定义分配。

3. **队列出队顺序**：output/capture 队列由 `video_framebuff_*` 维护，FIFO 顺序；
   `get_buf` 取的是「空闲容器」，`put_buf` 执行 `capture_done` 使其可被应用 `DQBUF`。
   驱动必须成对调用 get/put，否则 buffer 泄漏、`REQBUFS` 计数不对齐。

4. **延迟初始化**：设备 `open` 只做轻量分配；重引擎在 `output_streamon` 初始化
   （见 sim_decoder）。保证多实例 open/close 开销低，且失败可经 STREAMON 返回错误。

5. **flush**：`output_streamoff` 必须冲刷编解码器残留帧——设 flush 标志、用空输入触发引擎
   内部 drain、对末帧置 `V4L2_BUF_FLAG_LAST` 并 `codec_queue_event(V4L2_EVENT_EOS)`。
   否则流尾几帧丢失，应用收不到 EOS。

6. **capture_on 门控**：`output_available`/`capture_available` 里先判 `capture_on`，
   未 STREAMON 时不派 work，避免在 REQBUFS/QBUF 阶段过早启动解码。

---

## Kconfig

| 配置 | 层级 | 作用 |
|------|------|------|
| `CONFIG_VIDEO` | `video/Kconfig` | 总开关，Video 子系统 |
| `CONFIG_DRIVERS_VIDEO` | `drivers/video/Kconfig` | 视频设备驱动支持（menuconfig） |
| `CONFIG_VIDEO_STREAM` | `drivers/video/Kconfig` | Video Stream 支持 |
| `CONFIG_SIM_VIDEO_DECODER` | `arch/sim/Kconfig` | sim 上 H264 解码器，`depends on VIDEO && VIDEOUTILS_OPENH264` |
| `CONFIG_SIM_VIDEO_DECODER_DEV_PATH` | `arch/sim/Kconfig` | 解码器设备路径，默认 `/dev/video1` |
| `CONFIG_SIM_VIDEO_ENCODER` | `arch/sim/Kconfig` | sim 上 H264 编码器，`depends on VIDEO && VIDEOUTILS_LIBX264` |
| `CONFIG_SIM_VIDEO_ENCODER_DEV_PATH` | `arch/sim/Kconfig` | 编码器设备路径，默认 `/dev/video2` |
| `CONFIG_VIDEOUTILS_OPENH264` | `apps/videoutils/openh264/Kconfig` | openh264 库 |
| `CONFIG_VIDEOUTILS_LIBX264` | `apps/videoutils/x264/Kconfig` | x264 库 |
| `CONFIG_DMABUF_HEAPS` | `drivers/dma-buf/Kconfig` | DMA-BUF 用户态堆，`select CONFIG_DMA_SHARED_BUFFER`（DMABUF 模式必需） |

另：`CONFIG_VIDEO_REQBUFS_COUNT_MAX`（默认 3）为 `V4L2_REQBUFS_COUNT_MAX`，
是 `g_bufcnt` 缺省时的队列 buffer 数上限。

---

## 测试验证

- **nxcodec**：`apps/system/nxcodec`，NSH 下
  `nxcodec <devpath> <input> <output> [decode|encode]`，走 V4L2 M2M 全 ioctl 链路，
  是最直接的验证工具（其实现见 `nxcodec_context.c`，默认 `V4L2_MEMORY_MMAP`）。
- **ffmpeg / mediatool**：Vela 集成版 FFmpeg 通过 V4L2 M2M 硬件编解码，meditatool 做
  端到端播控。用真实码流（H264/MP4）decode 到 YUV 或 encode 回 H264 对拍。
- 验证要点：`VIDIOC_ENUM_FMT`/`S_FMT`/`REQBUFS`/`STREAMON`/`QBUF`/`DQBUF` 全链路无
  `-ENOTTY`（说明回调齐全）；EOS 事件在流尾到达；buffer 无泄漏（get/put 成对）。

---

## 骨架示例

最小 H264 decoder 的 `codec_ops_s` 实现（仅实现最小可用集；未实现的回调留 NULL，
框架自动返回 `-ENOTTY`）。命名遵循 NuttX 惯例：小写+下划线、`g_` 前缀全局、`_s` 后缀结构体。

```c
struct my_dec_s
{
  struct v4l2_format output_fmt;   /* 压缩码流格式 */
  struct v4l2_format capture_fmt;  /* YUV 格式 */
  struct work_s      work;
  void              *cookie;
  bool               capture_on;
  bool               flushing;
};

static const struct codec_ops_s g_my_decoder_ops =
{
  .open              = my_dec_open,
  .close             = my_dec_close,
  .capture_streamon  = my_dec_capture_streamon,
  .output_streamon   = my_dec_output_streamon,
  .capture_streamoff = my_dec_capture_streamoff,
  .output_streamoff  = my_dec_output_streamoff,
  .output_available  = my_dec_output_available,
  .capture_available = my_dec_capture_available,
  .querycap          = my_dec_querycap,
  .capture_enum_fmt  = my_dec_capture_enum_fmt,
  .output_enum_fmt   = my_dec_output_enum_fmt,
  .capture_g_fmt     = my_dec_capture_g_fmt,
  .output_g_fmt      = my_dec_output_g_fmt,
  .capture_s_fmt     = my_dec_capture_s_fmt,
  .output_s_fmt      = my_dec_output_s_fmt,
  .capture_try_fmt   = my_dec_capture_try_fmt,
  .output_try_fmt    = my_dec_output_try_fmt,
  .subscribe_event   = my_dec_subscribe_event,
  .capture_g_bufsize = my_dec_capture_g_bufsize,
  .output_g_bufsize  = my_dec_output_g_bufsize,
  .decoder_cmd       = my_dec_decoder_cmd,
};

static struct codec_s g_my_codec_decoder =
{
  .ops = &g_my_decoder_ops,
};

static int my_dec_open(void *cookie, void **priv)
{
  struct my_dec_s *my_dec = kmm_zalloc(sizeof(struct my_dec_s));
  if (my_dec == NULL)
    {
      return -ENOMEM;
    }

  my_dec->cookie = cookie;
  *priv = my_dec;
  return 0;
}

static int my_dec_close(void *priv)
{
  kmm_free(priv);
  return 0;
}

static int my_dec_capture_streamon(void *priv)
{
  struct my_dec_s *my_dec = priv;

  my_dec->capture_on = true;
  work_queue(HPWORK, &my_dec->work, my_dec_work, my_dec, 0);
  return 0;
}

static int my_dec_output_streamon(void *priv)
{
  /* 延迟初始化：此处才创建/初始化硬件解码引擎 */
  return hw_dec_init(priv);
}

static int my_dec_capture_streamoff(void *priv)
{
  struct my_dec_s *my_dec = priv;

  my_dec->capture_on = false;
  return hw_dec_stop(priv);
}

static int my_dec_output_streamoff(void *priv)
{
  struct my_dec_s *my_dec = priv;

  if (!my_dec->capture_on)
    {
      return 0;
    }

  my_dec->flushing = true;
  work_queue(HPWORK, &my_dec->work, my_dec_work, my_dec, 0);
  return 0;
}

static int my_dec_output_available(void *priv)
{
  struct my_dec_s *my_dec = priv;

  if (!my_dec->capture_on)
    {
      return 0;
    }

  work_queue(HPWORK, &my_dec->work, my_dec_work, my_dec, 0);
  return 0;
}

static int my_dec_capture_available(void *priv)
{
  struct my_dec_s *my_dec = priv;

  if (!my_dec->capture_on)
    {
      return 0;
    }

  work_queue(HPWORK, &my_dec->work, my_dec_work, my_dec, 0);
  return 0;
}

static int my_dec_querycap(void *priv, struct v4l2_capability *cap)
{
  strlcpy((char *)cap->driver, "my-h264", sizeof(cap->driver));
  strlcpy((char *)cap->card, "my-h264", sizeof(cap->card));
  cap->capabilities = V4L2_CAP_VIDEO_M2M;
  return 0;
}

static int my_dec_capture_enum_fmt(void *priv, struct v4l2_fmtdesc *f)
{
  if (f->index >= 1)
    {
      return -EINVAL;
    }

  f->pixelformat = V4L2_PIX_FMT_YUV420;
  return 0;
}

static int my_dec_output_enum_fmt(void *priv, struct v4l2_fmtdesc *f)
{
  if (f->index >= 1)
    {
      return -EINVAL;
    }

  f->pixelformat = V4L2_PIX_FMT_H264;
  return 0;
}

static int my_dec_capture_g_fmt(void *priv, struct v4l2_format *fmt)
{
  struct my_dec_s *my_dec = priv;

  *fmt = my_dec->capture_fmt;
  return 0;
}

static int my_dec_output_g_fmt(void *priv, struct v4l2_format *fmt)
{
  struct my_dec_s *my_dec = priv;

  *fmt = my_dec->output_fmt;
  return 0;
}

static int my_dec_capture_s_fmt(void *priv, struct v4l2_format *fmt)
{
  struct my_dec_s *my_dec = priv;

  if (fmt->fmt.pix.pixelformat != V4L2_PIX_FMT_YUV420)
    {
      return -EINVAL;
    }

  my_dec->capture_fmt = *fmt;
  my_dec->capture_fmt.fmt.pix.bytesperline = fmt->fmt.pix.width;
  my_dec->capture_fmt.fmt.pix.sizeimage =
      fmt->fmt.pix.width * fmt->fmt.pix.height * 3 / 2;
  return 0;
}

static int my_dec_output_s_fmt(void *priv, struct v4l2_format *fmt)
{
  struct my_dec_s *my_dec = priv;

  if (fmt->fmt.pix.pixelformat != V4L2_PIX_FMT_H264)
    {
      return -EINVAL;
    }

  my_dec->output_fmt = *fmt;
  my_dec->output_fmt.fmt.pix.sizeimage =
      fmt->fmt.pix.width * fmt->fmt.pix.height * 3 / 2 / 2 + 128;
  return 0;
}

static int my_dec_capture_try_fmt(void *priv, struct v4l2_format *fmt)
{
  return fmt->fmt.pix.pixelformat == V4L2_PIX_FMT_YUV420 ? 0 : -EINVAL;
}

static int my_dec_output_try_fmt(void *priv, struct v4l2_format *fmt)
{
  return fmt->fmt.pix.pixelformat == V4L2_PIX_FMT_H264 ? 0 : -EINVAL;
}

static int my_dec_subscribe_event(void *priv,
                                  struct v4l2_event_subscription *sub)
{
  return sub->type == V4L2_EVENT_EOS ? OK : -EINVAL;
}

static size_t my_dec_capture_g_bufsize(void *priv)
{
  struct my_dec_s *my_dec = priv;

  return my_dec->capture_fmt.fmt.pix.sizeimage;
}

static size_t my_dec_output_g_bufsize(void *priv)
{
  struct my_dec_s *my_dec = priv;

  return my_dec->output_fmt.fmt.pix.sizeimage;
}

static int my_dec_decoder_cmd(void *priv, struct v4l2_decoder_cmd *cmd)
{
  if (cmd->cmd == V4L2_DEC_CMD_STOP)
    {
      /* 触发 drain，末帧置 V4L2_BUF_FLAG_LAST + V4L2_EVENT_EOS */
      return 0;
    }

  return -EINVAL;
}

static void my_dec_work(void *arg)
{
  struct my_dec_s *my_dec = arg;
  struct v4l2_buffer *src_buf;
  struct v4l2_buffer *dst_buf;

  src_buf = codec_output_get_buf(my_dec->cookie);
  if (src_buf == NULL && !my_dec->flushing)
    {
      return;
    }

  dst_buf = codec_capture_get_buf(my_dec->cookie);
  if (dst_buf == NULL)
    {
      return;
    }

  /* 解码：读 src_buf->m.vaddr / bytesused，写 dst_buf->m.vaddr，
   * 回填 dst_buf->bytesused / timestamp；flush 时置 V4L2_BUF_FLAG_LAST 并
   * codec_queue_event(my_dec->cookie, &(struct v4l2_event){.type = V4L2_EVENT_EOS});
   */

  if (src_buf != NULL)
    {
      codec_output_put_buf(my_dec->cookie, src_buf);
    }

  codec_capture_put_buf(my_dec->cookie, dst_buf);
}

int my_decoder_initialize(void)
{
  return codec_register("/dev/video1", &g_my_codec_decoder);
}
```
