# V4L2 Capture（摄像头）适配 Pattern

本文描述在 NuttX/Vela 上适配一个摄像头（V4L2 Capture）驱动的方法。核心结论：**Vela 把 Linux V4L2 的"驱动适配"简化为两个 C 结构体的实现**——`struct imgdata_s`（平台主控 / 图像数据通路）与 `struct imgsensor_s`（具体 Sensor 芯片），其余（VFS 字符设备、ioctl 分发、vbuffer 队列、DMABUF/MMAP 缓冲管理、场景参数）全部由框架 `drivers/video/v4l2_cap.c` 完成。

所有回调签名以 `include/nuttx/video/imgdata.h` 与 `include/nuttx/video/imgsensor.h` 为准。本文字段表逐项与源码核对。

---

## 架构概览

```
┌─────────────────────────────────────────────────────────────┐
│  应用层（nxcamera / openvela camera framework）              │
│  open()/ioctl()/mmap()/close()                               │
└──────────────────────────┬──────────────────────────────────┘
                           │ VFS（字符设备，video_register 注册）
┌──────────────────────────▼──────────────────────────────────┐
│  v4l2_cap.c 框架（v4l2_ops_s / file_operations / 状态机）     │
│  - ioctl 分发、vbuffer 队列、场景参数、MMAP/DMABUF 缓冲管理     │
│  - get_connected_imgsensor() 选 Sensor                        │
└───────┬───────────────────────────────┬──────────────────────┘
        │ IMGDATA_* 宏调用               │ IMGSENSOR_* 宏调用
┌───────▼──────────────┐        ┌───────▼──────────────────────┐
│ struct imgdata_s      │        │ struct imgsensor_s            │
│ (imgdata_ops_s)       │        │ (imgsensor_ops_s + 能力三表)  │
│ 平台主控：中断/时钟/    │        │ 具体 Sensor：I2C 寄存器读写     │
│ 引脚/DMA/存储区        │        │ 屏蔽芯片差异                   │
└───────┬──────────────┘        └───────┬──────────────────────┘
        │                              │
┌───────▼──────────────────────────────▼──────────────────────┐
│  硬件：CISIF/DMA 主控 + I2C 总线 + CMOS Sensor(+FPGA)        │
└─────────────────────────────────────────────────────────────┘
```

与 Linux V4L2 的对比：

| 维度 | Linux V4L2 驱动 | Vela `v4l2_cap` |
|------|----------------|-----------------|
| 注册 | `video_register_device()` + 实现整套 `v4l2_ioctl_ops` | `capture_register()` + 实现 `imgdata_ops_s`/`imgsensor_ops_s` |
| ioctl 实现 | 驱动逐个实现 `VIDIOC_*` | 框架已实现，驱动只回填两个结构体 + 三表 |
| 缓冲管理 | 驱动实现 `videobuf2` ops | 框架内置 vbuffer 队列 + MMAP/DMABUF |
| Sensor 控制 | 通常混在同一个驱动里 | 拆成 `imgsensor_s`，一个主控可挂多个 Sensor |

**简化驱动适配 = 实现 `imgdata_s`（平台主控）+ `imgsensor_s`（Sensor），框架负责其余全部 V4L2 协议面。**

---

## 核心解耦：imgdata vs imgsensor

| 职责 | `struct imgdata_s`（平台主控） | `struct imgsensor_s`（Sensor 芯片） |
|------|-------------------------------|-----------------------------------|
| 定位 | 平台通用（CISIF/DMA/时钟/引脚），一个主控可对接多个 Sensor | 具体 Sensor 的寄存器读写，屏蔽芯片差异 |
| 数量关系 | 1 个 imgdata 对应多个 imgsensor | 多 Sensor 并存时由 `is_available()` 探测选择 |
| 关心的格式 | `imgdata_format_t`（含 `IMGDATA_PIX_FMT_*`） | `imgsensor_format_t`（含 `IMGSENSOR_PIX_FMT_*`） |
| 典型内容 | 中断处理、DMA 存储区地址、时钟/引脚使能 | I2C `getreg/putreg/putreglist`、寄存器表、状态机 |
| 数据流回调 | 通过 `imgdata_capture_t` 回调上报帧完成 | 只负责"出流/停流"，不产生数据回调 |
| 参考实现 | `cxd56_cisif.c`（CISIF）、`sim_camera.c` 的 data 部分 | `isx012.c`、`isx019.c`（Sensor+FPGA）、`sim_camera.c` 的 sensor 部分 |

框架在 `validate_frame_setting` / `start_capture` 中**同时**调用两者（先 `IMGSENSOR_*` 后 `IMGDATA_*`，或按起流顺序），只有两者都返回 `OK` 才算成功。

---

## 结构 imgdata_ops_s

定义于 `include/nuttx/video/imgdata.h`。`struct imgdata_s` 仅含一个 `FAR const struct imgdata_ops_s *ops` 成员；驱动可在其后追加私有成员（通常做法是把 `struct imgdata_s` 作为私有结构体首成员，用 `container_of` 或直接转型取私有数据）。

| # | 字段 | 签名（以源码为准） | 职责 | 调用时机 |
|---|------|-------------------|------|---------|
| 1 | `init` | `CODE int (*init)(FAR struct imgdata_s *data)` | 平台主控初始化：引脚配置、时钟使能、中断 attach/enable | `open()` 首次打开（`capture_open` → `IMGDATA_INIT`，在 `IMGSENSOR_INIT` 之后） |
| 2 | `uninit` | `CODE int (*uninit)(FAR struct imgdata_s *data)` | 反初始化：关中断/时钟/引脚 | `close()` 最后一次关闭（`capture_close` → `IMGDATA_UNINIT`） |
| 3 | `set_buf` | `CODE int (*set_buf)(FAR struct imgdata_s *data, uint8_t nr_datafmts, FAR imgdata_format_t *datafmts, uint8_t *addr, uint32_t size)` | 设置下一帧捕获的存储缓冲区地址；**`addr==NULL`（`size==0`）进入丢帧模式**——继续消费硬件帧但不存数据，回调以 `size=0` 上报，上层有缓冲后再调 `set_buf` | ①起流时（框架 `start_capture` 先 `IMGDATA_SET_BUF` 再 `IMGDATA_START_CAPTURE`）②每帧 `complete_capture` 完成后的 `set_next_buf` 路径（重填下一帧缓冲） |
| 4 | `validate_frame_setting` | `CODE int (*validate_frame_setting)(FAR struct imgdata_s *data, uint8_t nr_datafmts, FAR imgdata_format_t *datafmts, FAR imgdata_interval_t *interval)` | 校验主控能否支持该格式/分辨率/帧间隔（如 YUV/JPG 尺寸上限） | `VIDIOC_S_FMT`/`VIDIOC_TRY_FMT`/`VIDIOC_S_PARM`（框架先校验 imgsensor，再校验 imgdata） |
| 5 | `start_capture` | `CODE int (*start_capture)(FAR struct imgdata_s *data, uint8_t nr_datafmts, FAR imgdata_format_t *datafmts, FAR imgdata_interval_t *interval, FAR imgdata_capture_t callback, FAR void *arg)` | 配置主控 mode 并启动采集，保存 `callback/arg`，此后每帧完成时调用 `callback(result, size, ts, arg)` | `STREAMON` 后首个 QBUF/DQBUF 触发状态机进入 `CAPTURE_STATE_CAPTURE` 时 |
| 6 | `stop_capture` | `CODE int (*stop_capture)(FAR struct imgdata_s *data)` | 停止采集，禁用 DMA/中断，清空回调 | `STREAMOFF`、拍完（`remaining_capnum==0`）、资源清理 |
| 7 | `alloc` | `CODE void *(*alloc)(FAR struct imgdata_s *data, uint32_t align_size, uint32_t size)` | **用户自定义帧内存分配**（如 uncached/连续物理内存）；`alloc`+`free` 都为 `NULL` 时框架用系统内存 `kumm_memalign(32, size)` | `VIDIOC_REQBUFS` 且 `memory == V4L2_MEMORY_MMAP` 时，框架调 `alloc(imgdata, 32, count*bufsize)` |
| 8 | `free` | `CODE void (*free)(FAR struct imgdata_s *data, void *addr)` | 对应 `alloc` 的释放；`NULL` 时框架用 `kumm_free` | `REQBUFS` 重分配 / 流资源清理时 |

补充类型（`imgdata.h` 内定义，回调参数直接使用）：

```c
typedef struct imgdata_format_s
{
  uint16_t width;
  uint16_t height;
  uint32_t pixelformat;   /* IMGDATA_PIX_FMT_* */
} imgdata_format_t;

typedef struct imgdata_interval_s
{
  uint32_t numerator;     /* 帧间隔：numerator/denominator 秒/帧 */
  uint32_t denominator;
} imgdata_interval_t;

typedef int (*imgdata_capture_t)(uint8_t result, uint32_t size,
                                 FAR const struct timeval *ts,
                                 FAR void *arg);
```

`pixelformat` 取值（框架会把 V4L2 fourcc 转成 `IMGDATA_PIX_FMT_*` 后再传给驱动）：`IMGDATA_PIX_FMT_UYVY(0)`、`IMGDATA_PIX_FMT_RGB565(1)`、`IMGDATA_PIX_FMT_JPEG(2)`、`IMGDATA_PIX_FMT_JPEG_WITH_SUBIMG(3)`、`IMGDATA_PIX_FMT_SUBIMG_UYVY(4)`、`IMGDATA_PIX_FMT_SUBIMG_RGB565(5)`、`IMGDATA_PIX_FMT_YUYV(6)`、`IMGDATA_PIX_FMT_YUV420P(7)`、`IMGDATA_PIX_FMT_NV12(8)`、`IMGDATA_PIX_FMT_ENTROPY(9)`；`IMGDATA_FMT_MAX=2`、`IMGDATA_FMT_MAIN=0`、`IMGDATA_FMT_SUB=1`（JPEG+缩略图场景下 `nr_datafmts==2`）。

**`set_buf` 丢帧语义（重要）**：`addr==NULL && size==0` 是合法输入，表示"丢帧模式"——硬件继续出帧但驱动不落盘，并以 `size=0` 调用 capture 回调通知上层补缓冲。回调参数 `size==0` 即丢帧事件，框架的 `complete_capture` 会跳过该帧处理直接进入 `set_next_buf`。

---

## 结构 imgsensor_ops_s

定义于 `include/nuttx/video/imgsensor.h`。`struct imgsensor_s` 除 `ops` 外还携带**静态能力三表**（见下节）。

| # | 字段 | 签名（以源码为准） | 职责 | 调用时机 | 对应 ioctl |
|---|------|-------------------|------|---------|-----------|
| 1 | `is_available` | `CODE bool (*is_available)(FAR struct imgsensor_s *sensor)` | 探测硬件是否存在（上电 + 读 I2C 芯片寄存器/chipid 后判断），探测完需下电 | `capture_register` → `get_connected_imgsensor`（遍历 sensor 数组选第一个返回 `true` 的） | （注册期，非 ioctl） |
| 2 | `init` | `CODE int (*init)(FAR struct imgsensor_s *sensor)` | Sensor 上电 + 初始化寄存器表 | `open()` 首次（`IMGSENSOR_INIT`，先于 `IMGDATA_INIT`） | open |
| 3 | `uninit` | `CODE int (*uninit)(FAR struct imgsensor_s *sensor)` | 下电、释放 I2C | `close()` 最后 | close |
| 4 | `get_driver_name` | `CODE const char * (*get_driver_name)(FAR struct imgsensor_s *sensor)` | 返回驱动名（填入 capability/info） | querycap/enum_input | `VIDIOC_QUERYCAP`、`VIDIOC_ENUMINPUT` |
| 5 | `validate_frame_setting` | `CODE int (*validate_frame_setting)(FAR struct imgsensor_s *sensor, imgsensor_stream_type_t type, uint8_t nr_datafmts, FAR imgsensor_format_t *datafmts, FAR imgsensor_interval_t *interval)` | 校验 Sensor 能否支持该格式/分辨率/帧间隔（按 `type` 区分 VIDEO/STILL） | S_FMT/TRY_FMT/S_PARM | `VIDIOC_S_FMT`、`VIDIOC_TRY_FMT`、`VIDIOC_S_PARM` |
| 6 | `start_capture` | `CODE int (*start_capture)(FAR struct imgsensor_s *sensor, imgsensor_stream_type_t type, uint8_t nr_datafmts, FAR imgsensor_format_t *datafmts, FAR imgsensor_interval_t *interval)` | 写寄存器启动 Sensor 出流（模式/格式/fps/clip） | STREAMON 状态机进入 CAPTURE；拍仍 STILL | `VIDIOC_STREAMON`、`VIDIOC_TAKEPICT_START` |
| 7 | `stop_capture` | `CODE int (*stop_capture)(FAR struct imgsensor_s *sensor, imgsensor_stream_type_t type)` | 停止 Sensor 出流 | STREAMOFF/拍完 | `VIDIOC_STREAMOFF`、`VIDIOC_TAKEPICT_STOP` |
| 8 | `get_frame_interval` | `CODE int (*get_frame_interval)(FAR struct imgsensor_s *sensor, imgsensor_stream_type_t type, FAR imgsensor_interval_t *interval)` | 查询当前实际帧间隔（含 Sensor 自动帧扩展/抽帧后折算） | G_PARM 且正在 capture 时（否则返回框架缓存值） | `VIDIOC_G_PARM` |
| 9 | `get_supported_value` | `CODE int (*get_supported_value)(FAR struct imgsensor_s *sensor, uint32_t id, FAR imgsensor_supported_value_t *value)` | 查询某控件能力（range/discrete/elems + 默认值） | queryctrl/querymenu/默认值初始化 | `VIDIOC_QUERYCTRL`、`VIDIOC_QUERY_EXT_CTRL`、`VIDIOC_QUERYMENU` |
| 10 | `get_value` | `CODE int (*get_value)(FAR struct imgsensor_s *sensor, uint32_t id, uint32_t size, FAR imgsensor_value_t *value)` | 读控件当前值（I2C 读寄存器换算） | G_CTRL/G_EXT_CTRLS | `VIDIOC_G_CTRL`、`VIDIOC_G_EXT_CTRLS` |
| 11 | `set_value` | `CODE int (*set_value)(FAR struct imgsensor_s *sensor, uint32_t id, uint32_t size, imgsensor_value_t value)` | 写控件（I2C 写寄存器，注意换算） | S_CTRL/S_EXT_CTRLS、S_SELECTION(clip) | `VIDIOC_S_CTRL`、`VIDIOC_S_EXT_CTRLS`、`VIDIOC_S_SELECTION` |

补充类型：

```c
typedef struct imgsensor_format_s
{
  uint16_t width;
  uint16_t height;
  uint32_t pixelformat;    /* IMGSENSOR_PIX_FMT_*（与 IMGDATA_PIX_FMT_* 同值） */
} imgsensor_format_t;

typedef struct imgsensor_interval_s
{
  uint32_t numerator;
  uint32_t denominator;
} imgsensor_interval_t;

typedef enum imgsensor_stream_type_e
{
  IMGSENSOR_STREAM_TYPE_VIDEO = 0,
  IMGSENSOR_STREAM_TYPE_STILL = 1,
} imgsensor_stream_type_t;
```

控件能力值（`get_supported_value` 的 `value` 参数，`imgsensor_supported_value_t`）按 `type` 选择 union 成员：

| `type`（`imgsensor_ctrl_type_t`） | 使用成员 | 说明 |
|------|---------|------|
| `IMGSENSOR_CTRL_TYPE_INTEGER`/`BOOLEAN`/`INTEGER64`/`BITMASK`/`U8FIXEDPOINT_Q7`/`U16FIXEDPOINT_Q8`/`INTEGER_TIMES_3` | `u.range`（`minimum/maximum/step/default_value`） | 连续范围 |
| `IMGSENSOR_CTRL_TYPE_INTEGER_MENU` | `u.discrete`（`nr_values/values[]/default_value`） | 枚举菜单 |
| `IMGSENSOR_CTRL_TYPE_U8`/`U16`/`U32` | `u.elems`（`nr_elems/minimum/maximum/step`） | 数组型（如 gamma 曲线、3A 参数、clip） |

`get_value`/`set_value` 的 `value` 参数是 `imgsensor_value_t` union：`value32` / `value64` / `p_u8` / `p_u16` / `p_u32`；按控件类型选对应成员。

`id` 取值即 `IMGSENSOR_ID_*` 宏（映射到 `V4L2_CID_*`，如 `IMGSENSOR_ID_BRIGHTNESS == V4L2_CID_BRIGHTNESS`），另有两个 Vela 扩展 clip id：`IMGSENSOR_ID_CLIP_VIDEO(0xFFFF0000)` / `IMGSENSOR_ID_CLIP_STILL(0xFFFF0001)`。

---

## 静态能力三表

`struct imgsensor_s` 内三个表指针 + 数量成员（源码逐字段）：

```c
struct imgsensor_s
{
  FAR const struct imgsensor_ops_s *ops;
  size_t fmtdescs_num;
  FAR const struct v4l2_fmtdesc *fmtdescs;
  size_t frmsizes_num;
  FAR const struct v4l2_frmsizeenum *frmsizes;
  size_t frmintervals_num;
  FAR const struct v4l2_frmivalenum *frmintervals;
};
```

三表类型直接复用 V4L2 标准结构：

- `fmtdescs`：`struct v4l2_fmtdesc`（`pixelformat` + `description[]`）→ 对应 `VIDIOC_ENUM_FMT`
- `frmsizes`：`struct v4l2_frmsizeenum`（`type` + `discrete`/`stepwise`）→ 对应 `VIDIOC_ENUM_FRAMESIZES`
- `frmintervals`：`struct v4l2_frmivalenum`（`type` + `discrete`/`stepwise`）→ 对应 `VIDIOC_ENUM_FRAMEINTERVALS`

**「第 0 项 = 推荐默认」约定（关键）**：框架 `initialize_frame_setting()` 在流资源初始化时取各表**第 0 项**作为默认帧设置：

| 表 | 取第 0 项的方式（源码 `initialize_frame_setting`） |
|----|---------------------------------------------------|
| `frmsizes[0]` | `DISCRETE` → `discrete.width/height`；`STEPWISE` → `stepwise.min_width/min_height` |
| `fmtdescs[0]` | `pixelformat`（默认像素格式） |
| `frmintervals[0]` | `DISCRETE` → `discrete.numerator/denominator`；`STEPWISE` → `stepwise.min` |

因此适配时必须把**最推荐/最常用**的分辨率、像素格式、帧间隔放在各表第 0 项（框架默认参数即来自这里；表为空时框架回退 QVGA UYVY 15FPS）。

---

## 注册 API

### 新版多实例接口（**必须使用**）

```c
/* include/nuttx/video/v4l2_cap.h */
int capture_register(FAR const char *devpath,
                     FAR struct imgdata_s *data,
                     FAR struct imgsensor_s **sensors,
                     size_t sensor_num);

int capture_unregister(FAR const char *devpath);
```

- `capture_register`：注册一个 `/dev/videoX` 设备；每次调用分配独立的 `capture_mng_t` 实例（`kmm_zalloc`），**支持多实例**（不同 `devpath` 互不干扰）。
- `sensors` 为 `imgsensor_s*` 指针数组，`sensor_num` 为数组长度；框架内部 `get_connected_imgsensor()` 遍历数组，选第一个 `IMGSENSOR_IS_AVAILABLE()` 返回 `true` 的 Sensor 挂到该实例上——一个 imgdata 可配多个候选 Sensor。
- `capture_unregister`：`unregister_driver(devpath)`。

### 旧版单实例接口（**禁用**）

以下四个接口仍存在于源码中，但**新驱动禁止使用**：

| 接口 | 实现（v4l2_cap.c） | 禁用原因 |
|------|-------------------|---------|
| `int imgsensor_register(FAR struct imgsensor_s *sensor)` | `kmm_realloc` 追加到全局 `g_capture_registered_sensor` 数组 | 依赖全局变量 |
| `void imgdata_register(FAR struct imgdata_s *data)` | 赋值给全局 `g_capture_data` | 依赖全局变量，**全局只能注册 1 个 imgdata** |
| `int capture_initialize(FAR const char *devpath)` | 转发 `capture_register(devpath, g_capture_data, g_capture_registered_sensor, g_capture_registered_sensor_num)` | 依赖上述两个全局变量 |
| `int capture_uninitialize(FAR const char *devpath)` | 转发 `capture_unregister(devpath)` | 配套旧式初始化 |

**禁用原因**：整套旧接口围绕两个全局变量 `g_capture_data`（单 imgdata）与 `g_capture_registered_sensor`（全局 sensor 数组）展开，存在**单实例限制**与**跨设备全局状态污染**问题，多实例/多 Sensor 场景无法正确隔离。新驱动应直接调用 `capture_register()` 显式传入 `imgdata`/`imgsensor` 数组。

> 注意：现有参考驱动 `sim_camera.c`（`imgsensor_register`+`imgdata_register`）、`cxd56_cisif.c`（`imgdata_register`）、`isx012.c`/`isx019.c`（`imgsensor_register`）仍沿用旧接口，属历史实现；新适配请勿照搬其注册方式，改用 `capture_register()`。

---

## 应用层 ioctl 流程

### 标准 V4L2 流程

```
open("/dev/video0")
  → VIDIOC_QUERYCAP                 （capture_querycap：driver 名 + 能力）
  → VIDIOC_ENUM_FMT / ENUM_FRAMESIZES / ENUM_FRAMEINTERVALS   （枚举能力三表）
  → VIDIOC_S_FMT                    （capture_s_fmt → validate_frame_setting）
  → VIDIOC_S_PARM                   （capture_s_parm：帧间隔）
  → VIDIOC_REQBUFS                  （capture_reqbufs：分配 buffer；MMAP 走 alloc，DMABUF 走 fd 导入）
  → VIDIOC_QUERYBUF                 （capture_querybuf：取 offset/length）
  → mmap() 或 传入 dmabuf fd
  → VIDIOC_QBUF                     （capture_qbuf：入队）
  → VIDIOC_STREAMON                 （capture_streamon：状态机 STREAMOFF→STREAMON→CAPTURE）
  → VIDIOC_DQBUF                    （capture_dqbuf：阻塞等帧 / O_NONBLOCK 返回 -EAGAIN）
  → （可选 poll() 等待 POLLIN）
  → VIDIOC_STREAMOFF                （capture_streamoff）
  → munmap() → close()
```

关键状态机（`capture_mng_t` 内）：`CAPTURE_STATE_STREAMOFF → STREAMON → CAPTURE`；真正 `start_capture`（调 imgdata+imgsensor 的 start）发生在 `STREAMON` 之后的首个 `QBUF`/`DQBUF` 时机。

### openvela 专用 ioctl（定义于 `include/sys/videoio.h`）

| 宏 | 取值 | 用途 | 框架入口 |
|----|------|------|---------|
| `VIDIOC_CANCEL_DQBUF` | `_VIDIOC(0x00c1)` | 取消阻塞中的 DQBUF（唤醒并以 `-ECANCELED` 返回） | `capture_cancel_dqbuf` |
| `VIDIOC_DO_HALFPUSH` | `_VIDIOC(0x00c2)` | 半按（3A lock + 单次 AF），转成 S_EXT_CTRLS | `capture_do_halfpush` |
| `VIDIOC_TAKEPICT_START` | `_VIDIOC(0x00c3)` | 拍照开始（STILL 流，`capture_num` 控制张数/无限） | `capture_takepict_start` |
| `VIDIOC_TAKEPICT_STOP` | `_VIDIOC(0x00c4)` | 拍照结束 | `capture_takepict_stop` |
| `V4SIOC_QUERY_EXT_CTRL_SCENE` | `_VIDIOC(0x00c5)` | 按场景查询控件能力 | `capture_query_ext_ctrl_scene` |
| `V4SIOC_QUERYMENU_SCENE` | `_VIDIOC(0x00c6)` | 按场景查询菜单 | `capture_querymenu_scene` |
| `V4SIOC_G_EXT_CTRLS_SCENE` | `_VIDIOC(0x00c7)` | 读某场景下控件组 | `capture_g_ext_ctrls_scene` |
| `V4SIOC_S_EXT_CTRLS_SCENE` | `_VIDIOC(0x00c8)` | 写某场景下控件组（存入场景参数表） | `capture_s_ext_ctrls_scene` |

场景（Scene Mode，`V4L2_CID_SCENE_MODE`）由框架维护 `capture_scene_param[]`（`V4L2_SCENE_MODE_NONE` + 各 `CONFIG_VIDEO_SCENE_*` 场景），每个场景保存一套控件值，`reflect_scene_parameter` 批量下发；驱动只需实现 `get_value/set_value/get_supported_value` 即可被场景机制复用。

---

## 内存模式（Buffer Type）选择 + 适配确认点

框架 `capture_reqbufs` 支持两种内存模式：

| 场景 | 内存模式 | 说明 |
|------|---------|------|
| 使用上层 camera framework | **必须 `V4L2_MEMORY_DMABUF`** | framework 通过 dmabuf fd 传递缓冲区；QBUF 时用 `dma_buf_get`/`dma_buf_attach`/`dma_buf_map_attachment` 导入 |
| 不使用 camera framework | `V4L2_MEMORY_MMAP` | 框架在 REQBUFS 时调 `imgdata->ops->alloc(imgdata, 32, count*bufsize)`（无 alloc 则 `kumm_memalign(32, ...)`）；mmap 由 `capture_mmap` 映射 `bufheap` |
| 默认 | `V4L2_MEMORY_DMABUF`（需开启 `CONFIG_DMABUF_HEAPS`） | DMABUF 需 `CONFIG_DMABUF_HEAPS`（其 `select DMA_SHARED_BUFFER`）；MMAP 需 `CONFIG_FS_FILEMAP` 供 mmap 系统调用 |

实现要点（源码 `capture_reqbufs`/`capture_qbuf`/`capture_expbuf`）：

- `V4L2_MEMORY_MMAP`：`REQBUFS` 分配连续 `bufheap`（`imgdata->ops->alloc` 或 `kumm_memalign(32, …)`）；`QUERYBUF` 返回 `m.offset = length * index`；`QBUF` 时框架按 index 计算 `m.userptr` 指向 `bufheap` 内偏移。
- `V4L2_MEMORY_DMABUF`（`#ifdef CONFIG_DMA_SHARED_BUFFER`）：`REQBUFS` 不分配堆，缓冲由上层通过 `QBUF` 的 `m.fd` 导入；`VIDIOC_EXPBUF`（`capture_expbuf`）可将 `bufheap` 导出为 dmabuf fd。
- DMABUF 模式下框架维护 `dma_buf_ops`（attach/detach/map/unmap/release/begin_cpu_access/end_cpu_access/mmap），其中 `begin/end_cpu_access` 做 dcache `up_invalidate_dcache`/`up_clean_dcache`。

### 适配交互确认点（必做）

适配开始前**必须**与开发者确认：**目标是否使用上层 camera framework？**

- 使用 framework → 内存模式定为 **`V4L2_MEMORY_DMABUF`**，需在 defconfig 开启 `CONFIG_DMABUF_HEAPS`（自动带出 `CONFIG_DMA_SHARED_BUFFER`）。
- 不使用 framework → 用 **`V4L2_MEMORY_MMAP`**（若需非 cache 内存，实现 `imgdata_ops.alloc/free` 返回 uncached 内存，否则留 NULL 走 `kumm_memalign`）。

未确认该点就动手，会选错缓冲模式导致 framework 拿不到 dmabuf fd 或驱动侧地址不对齐。

---

## 参考驱动锚点

| 文件 | 角色 | 要点 |
|------|------|------|
| `arch/sim/src/sim/sim_camera.c` | sim 参考（空 imgsensor + 静态能力 + imgdata 封装 host V4L2） | 一个 `sim_camera_priv_t` 内嵌 `imgdata_s`+`imgsensor_s`；`imgsensor_ops` 大多空实现（`validate/start/stop` 直接返回 0，`is_available` 返回 true），静态能力 `g_frmsizes[]`/`g_fmts[]` 供 `sensor` 引用；`imgdata_ops` 干实事——`data_init` 调 `host_video_init(CONFIG_HOST_CAMERA_DEV_PATH)`，`start_capture` 调 `host_video_set_fmt/start_capture` + `work_queue` 周期回调 `complete_capture`；注册仍用旧式 `imgsensor_register`/`imgdata_register` |
| `arch/arm/src/cxd56xx/cxd56_cisif.c` | imgdata 平台侧（中断/时钟/引脚） | `init` 做 `CXD56_PIN_CONFIGS(PINCONFS_IS)`+`cxd56_img_cisif_clock_enable()`+`irq_attach/up_enable_irq`；`start_capture` 按 `IMGDATA_PIX_FMT_*` 选 `MODE_YUV/JPG/INTLEV_TRS_EN`；`set_buf` 配 `CISIF_*_START_ADDR`（`CXD56_PHYSADDR(addr)`，要求 32 字节对齐）；中断 `cisif_intc_handler` 分发到各子中断后调 `imgdata_capture_t` 回调上报 size |
| `drivers/video/isx012.c` | imgsensor 设备侧（I2C 寄存器读写） | 私有结构 `isx012_dev_t` 首成员 `struct imgsensor_s sensor`；`getreg/putreg/putreglist` 走 I2C；`init` 用寄存器表 `g_isx012_def_init` 初始化 + shading；`validate_frame_setting` 用 `isx012_get_maximum_fps` 校验；`start_capture` → `isx012_set_mode_param` 写 fps/fmt/sensmode/hsize/vsize 寄存器；`get_supported_value` 返回大量 `IMGSENSOR_ID_*` 的 range/discrete |
| `drivers/video/isx019.c` | imgsensor 设备侧（sensor + FPGA 双 I2C） | 双 I2C：`isx019_i2c_read/write`（带 cmd/校验和的 sensor 协议）+ `fpga_i2c_read/write`（FPGA 寄存器）；实现 `get_frame_interval`（读 V_TIME 折算自动帧扩展 + FPGA 抽帧）；`set_value` 用 `isx019_reginfo_t` 表 + `convert_t` 换算函数映射控件到寄存器；JPEG 质量通过写 FPGA DQT 表实现 |
| 解耦精髓 | — | cxd56_cisif（imgdata）与 isx012/isx019（imgsensor）分离：主控负责"数据进哪块内存 + 何时完成"，Sensor 负责"以什么格式/分辨率/帧率出流"，两者通过框架的 `validate_frame_setting`/`start_capture` 串起来 |
| uncached 自定义 alloc/free | — | `imgdata_ops.alloc/free` 由驱动实现（返回 uncached/连续物理内存），框架在 `V4L2_MEMORY_MMAP` 的 `REQBUFS` 时调用；两者为 NULL 则回退 `kumm_memalign(32, …)`（源码 `capture_reqbufs` 分支） |

---

## Kconfig

| 配置项 | 位置 | 作用 |
|--------|------|------|
| `CONFIG_VIDEO` | `video/Kconfig` | Video 子系统总开关（menu 入口） |
| `CONFIG_VIDEO_STREAM` | `drivers/video/Kconfig` | 视频流支持（v4l2_cap 框架） |
| `CONFIG_VIDEO_FB` | `drivers/video/Kconfig` | 帧缓冲（framebuffer）支持 |
| `CONFIG_VIDEO_ISX012` / `CONFIG_VIDEO_ISX019` | `drivers/video/Kconfig` | 编入 isx012 / isx019 参考 sensor 驱动 |
| `CONFIG_HOST_CAMERA_DEV_PATH` | `arch/sim/Kconfig` | sim 下 host 摄像头设备路径（如 `/dev/video0`） |
| `CONFIG_SIM_CAMERA_DEV_PATH` | `arch/sim/Kconfig` | sim 摄像头注册的设备路径 |
| `CONFIG_SIM_CAMERA` / `CONFIG_SIM_CAMERA_V4L2` / `CONFIG_SIM_CAMERA_LOOP_INTERVAL` | `arch/sim/Kconfig` | sim 摄像头驱动开关 / V4L2 模式 / 出帧周期 |
| `CONFIG_DMABUF_HEAPS` | `drivers/dma-buf/Kconfig` | 启用 DMA-BUF 用户态内存堆（`select DMA_SHARED_BUFFER`），DMABUF 内存模式必需 |
| `CONFIG_FS_FILEMAP` | `fs` | 启用 mmap 系统调用（`V4L2_MEMORY_MMAP` 需要 `capture_mmap`） |

适配 checklist（按内存模式）：
- 走 DMABUF：`CONFIG_VIDEO` + `CONFIG_VIDEO_STREAM` + `CONFIG_DMABUF_HEAPS`（带出 `CONFIG_DMA_SHARED_BUFFER`）。
- 走 MMAP：额外需要 `CONFIG_FS_FILEMAP`（mmap 映射 `bufheap`）。

---

## 测试验证

用 `apps/system/nxcamera`（仓库内 `apps/system/nxcamera/`）做端到端验证：打开 `/dev/videoX`，走标准 V4L2 流程（QUERYCAP → S_FMT → S_PARM → REQBUFS → QUERYBUF → QBUF → STREAMON → DQBUF → STREAMOFF）抓帧，校验分辨率/像素格式/帧率是否与静态能力三表第 0 项一致，并抽查 `get_value`/`set_value` 控件（如 `IMGSENSOR_ID_BRIGHTNESS`）与 `VIDIOC_TAKEPICT_START/STOP` 拍照路径。

---

## 骨架示例

最小 `imgdata` + `imgsensor` 实现，含三表静态定义与多实例 `capture_register` 注册：

```c
/* 静态能力三表：第 0 项 = 推荐默认（框架 initialize_frame_setting 取第 0 项） */
static const struct v4l2_fmtdesc g_fmtdescs[] =
{
  {
    .pixelformat = V4L2_PIX_FMT_YUYV,
    .description = "YUYV 4:2:2",
  },
  {
    .pixelformat = V4L2_PIX_FMT_JPEG,
    .description = "JPEG",
  },
};

static const struct v4l2_frmsizeenum g_frmsizes[] =
{
  {
    .type = V4L2_FRMSIZE_TYPE_DISCRETE,
    .discrete = { .width = 640, .height = 480 },
  },
  {
    .type = V4L2_FRMSIZE_TYPE_DISCRETE,
    .discrete = { .width = 1280, .height = 720 },
  },
};

static const struct v4l2_frmivalenum g_frmintervals[] =
{
  {
    .type = V4L2_FRMIVAL_TYPE_DISCRETE,
    .discrete = { .numerator = 1, .denominator = 30 },  /* 30 FPS */
  },
  {
    .type = V4L2_FRMIVAL_TYPE_DISCRETE,
    .discrete = { .numerator = 1, .denominator = 15 },  /* 15 FPS */
  },
};

/* 私有结构：imgdata_s 为首成员（可直接转型取私有数据）；
 * imgsensor_s 为第二成员（其回调需 container_of 取私有数据） */
struct mycam_priv_s
{
  struct imgdata_s   data;     /* 首成员：container_of / 直接转型 */
  struct imgsensor_s sensor;
  /* ... 硬件私有字段：i2c、中断号、缓冲区指针等 ... */
};

/* ================= imgsensor ops ================= */
static const struct imgsensor_ops_s g_mycam_sensor_ops =
{
  .is_available           = mycam_is_available,            /* 探测：上电读 chipid 后下电 */
  .init                   = mycam_sensor_init,             /* 上电 + 寄存器表初始化 */
  .uninit                 = mycam_sensor_uninit,           /* 下电 */
  .get_driver_name        = mycam_get_driver_name,         /* 返回 "MyCam" */
  .validate_frame_setting = mycam_validate_frame_setting,  /* 校验 Sensor 支持的格式/帧率 */
  .start_capture          = mycam_start_capture,           /* 写寄存器启动出流 */
  .stop_capture           = mycam_stop_capture,            /* 停止出流 */
  .get_frame_interval     = mycam_get_frame_interval,      /* 可选：NULL 则返回框架缓存值 */
  .get_supported_value    = mycam_get_supported_value,     /* 控件能力 range/discrete */
  .get_value              = mycam_get_value,               /* 读控件 */
  .set_value              = mycam_set_value,               /* 写控件 */
};

/* ================= imgdata ops ================= */
static const struct imgdata_ops_s g_mycam_data_ops =
{
  .init                   = mycam_data_init,               /* 引脚/时钟/中断 attach */
  .uninit                 = mycam_data_uninit,             /* 关中断/时钟/引脚 */
  .set_buf                = mycam_data_set_buf,            /* 设下一帧存储区；addr=NULL 丢帧 */
  .validate_frame_setting = mycam_data_validate_frame_setting, /* 主控格式/尺寸校验 */
  .start_capture          = mycam_data_start_capture,      /* 配 mode + 启动 DMA，注册回调 */
  .stop_capture           = mycam_data_stop_capture,       /* 停 DMA/中断，清回调 */
  .alloc                  = mycam_data_alloc,              /* 可选：NULL 则 kumm_memalign */
  .free                   = mycam_data_free,               /* 可选 */
};

/* ================= 实例 + 多实例注册 ================= */
static struct mycam_priv_s g_mycam =
{
  .data   = { .ops = &g_mycam_data_ops },
  .sensor =
  {
    .ops               = &g_mycam_sensor_ops,
    .fmtdescs_num      = nitems(g_fmtdescs),
    .fmtdescs          = g_fmtdescs,
    .frmsizes_num      = nitems(g_frmsizes),
    .frmsizes          = g_frmsizes,
    .frmintervals_num  = nitems(g_frmintervals),
    .frmintervals      = g_frmintervals,
  },
};

int mycam_initialize(void)
{
  FAR struct imgsensor_s *sensors[] = { &g_mycam.sensor };

  /* 新版多实例接口：一个 devpath 一个独立实例 */
  return capture_register("/dev/video0", &g_mycam.data, sensors, 1);
}
```

骨架要点：
- 三表第 0 项 = 推荐默认（640x480 YUYV 30FPS）。
- 取私有数据方式因内嵌位置而异：`imgdata`（首成员）直接转型 `(struct mycam_priv_s *)data`；`imgsensor`（第二成员）用 `container_of(&sensor, struct mycam_priv_s, sensor)`（等价于 `sim_camera.c` 的 `offsetof` 回退，或用 `container_of` 宏）。
- 注册用 `capture_register`（多实例），**不要**用旧式 `imgsensor_register`/`imgdata_register`/`capture_initialize`。
- 若不需要平台主控（纯软件/SIM），`imgdata_ops` 可仿 `sim_camera.c` 用 work_queue 周期回调模拟出帧。
