# Audio Driver 配置与测试验证

本文档是 audio 驱动的 Kconfig 配置说明与测试验证参考，由
[`audio_lowerhalf_pattern.md`](audio_lowerhalf_pattern.md) 路由加载。
覆盖 menuconfig 配置项、示例配置、以及 nxplayer/nxrecorder/nxlooper/CMocka 验证工具。

## 一、Kconfig 配置项说明

打开 `menuconfig`，搜索 `audio`，找到 **Audio Support** 配置项。

### 核心配置项

| 配置项 | 功能 |
|--------|------|
| `CONFIG_AUDIO` | 启用音频设备驱动（除特定模组品类外均需启用） |
| `CONFIG_AUDIO_COMP` | 支持组合节点（`audio_comp`，参见原理说明） |
| `CONFIG_AUDIO_MULTI_SESSION` | 支持多会话（默认通常 disable） |
| `CONFIG_AUDIO_DRIVER_SPECIFIC_BUFFERS` | 支持应用自定义缓冲区大小和数量 |
| `CONFIG_AUDIO_NUM_BUFFERS` | 音频处理缓冲区数量（默认 2） |
| `CONFIG_AUDIO_BUFFER_NUMBYTES` | 每个缓冲区大小（默认 8192） |
| `CONFIG_AUDIO_LARGE_BUFFERS` | 支持大于 65K 样本的缓冲区 |

> **`CONFIG_AUDIO_LARGE_BUFFERS`**：默认缓冲区大小用 `uint16_t` 定义（最大 32K 样本），
> 启用后用 `uint32_t` 定义（最大 65K 样本）：
> ```c
> #ifdef CONFIG_AUDIO_LARGE_BUFFERS
> typedef uint32_t apb_samp_t;
> #else
> typedef uint16_t apb_samp_t;
> #endif
> ```

### 支持格式

| 配置项 | 说明 |
|--------|------|
| `CONFIG_AUDIO_FORMAT_PCM` | 支持 PCM 格式 |
| `CONFIG_AUDIO_FORMAT_MP3` | 支持 MP3 压缩格式（根据设备能力选择其他格式） |

### 其他配置项

| 配置项 | 功能 |
|--------|------|
| `CONFIG_AUDIO_EXCLUDE_*` | 剔除特定音频功能（equalizer、rewind、stop、pause_resume、volume、tone 等），默认关闭 |
| `CONFIG_AUDIO_CUSTOM_DEV_PATH` | 自定义音频设备节点注册路径（默认 `/dev/audio`） |
| `CONFIG_DEBUG_AUDIO` | 控制音频驱动日志输出等级（默认不开启） |

### 调试日志宏

接口定义在 `debug.h`：

```c
#ifdef CONFIG_DEBUG_AUDIO_ERROR
#  define auderr   _err
#else
#  define auderr   _none
#endif

#ifdef CONFIG_DEBUG_AUDIO_WARN
#  define audwarn  _warn
#else
#  define audwarn  _none
#endif

#ifdef CONFIG_DEBUG_AUDIO_INFO
#  define audinfo  _info
#else
#  define audinfo  _none
#endif
```

## 二、示例配置

### 1. Simulator 环境

```makefile
CONFIG_AUDIO=y
CONFIG_AUDIO_NUM_BUFFERS=2
CONFIG_AUDIO_BUFFER_NUMBYTES=8192
CONFIG_AUDIO_FORMAT_PCM=y
CONFIG_AUDIO_FORMAT_MP3=y
CONFIG_AUDIO_EXCLUDE_EQUALIZER=y
CONFIG_AUDIO_EXCLUDE_REWIND=y
```

### 2. 某硬件平台

```makefile
CONFIG_AUDIO=y
CONFIG_AUDIO_NUM_BUFFERS=2
CONFIG_AUDIO_BUFFER_NUMBYTES=8192
CONFIG_AUDIO_DRIVER_SPECIFIC_BUFFERS=y
CONFIG_AUDIO_FORMAT_PCM=y
CONFIG_AUDIO_EXCLUDE_EQUALIZER=y
CONFIG_AUDIO_EXCLUDE_REWIND=y
CONFIG_DEBUG_AUDIO=y
CONFIG_DEBUG_AUDIO_ERROR=y
```

## 三、验证工具

openvela 提供三个内置测试工具（`apps/system/` 下）：`nxplayer`（播放）、`nxrecorder`（录音）、
`nxlooper`（回环）。驱动适配完成后无需编写额外测试代码即可验证核心功能。

### 3.1 nxplayer（播放）

代码位于 `apps/system/nxplayer`。常用命令：

| 命令 | 功能 |
|------|------|
| `device devfile` | 指定音频设备 |
| `play filename` | 播放媒体文件 |
| `playraw filename` | 播放原始数据文件 |
| `tone samplerate duration pitchfreq` | 产生纯音 |
| `pause`/`resume`/`stop` | 暂停/恢复/停止 |
| `volume d%` | 设置音量百分比 |
| `balance d%`/`bass d%`/`treble d%` | 声道平衡/低音/高音 |

**PCM 文件播放**（需指定声道数、采样深度、采样率）：

```bash
nxplayer
nxplayer> device pcm0p
nxplayer> playraw /data/test.pcm 1 16 44100
nxplayer> stop
nxplayer> close
```

**压缩格式播放**（MP3，sim 平台需 `sudo apt install libmad0-dev:i386`）：

```bash
nxplayer
nxplayer> device pcm1p
nxplayer> play /data/audio.mp3
nxplayer> stop
nxplayer> close
```

### 3.2 nxrecorder（录音）

代码位于 `apps/system/nxrecorder`。常用命令：`device`、`recordraw`（录 PCM）、`record`（录媒体文件）、
`pause`/`resume`/`stop`。

**PCM 录音**：

```bash
nxrecorder
nxrecorder> device pcm0c
nxrecorder> recordraw /data/test.pcm 1 16 44100
nxrecorder> stop
nxrecorder> close
```

**压缩格式录音**（MP3，sim 平台需 `sudo apt-get install libmp3lame-dev:i386`）：

```bash
nxrecorder
nxrecorder> device pcm1c
nxrecorder> record /stream/100.mp3 2 16 44100
nxrecorder> stop
nxrecorder> close
```

### 3.3 nxlooper（回环）

代码位于 `apps/system/nxlooper`。`loopback channels bpsamp samprate format chmap` 执行回环测试。

**PCM 回环**：

```bash
nxlooper
nxlooper> device pcm0p
nxlooper> device pcm0c
nxlooper> loopback 2 16 48000
nxlooper> stop
nxlooper> close
```

**压缩格式回环**（MP3，format=8）：

```bash
nxlooper
nxlooper> device pcm1p
nxlooper> device pcm1c
nxlooper> loopback 2 16 44100 8
nxlooper> stop
nxlooper> close
```

`format` 参数定义在 `nuttx/include/nuttx/audio/audio.h`：

```c
#define AUDIO_FMT_UNDEF   0x00
#define AUDIO_FMT_OTHER   0x01
#define AUDIO_FMT_MPEG    0x02
#define AUDIO_FMT_AC3     0x03
#define AUDIO_FMT_WMA     0x04
#define AUDIO_FMT_DTS     0x05
#define AUDIO_FMT_PCM     0x06
#define AUDIO_FMT_WAV     0x07
#define AUDIO_FMT_MP3     0x08
#define AUDIO_FMT_MIDI    0x09
#define AUDIO_FMT_OGG_VORBIS 0x0a
#define AUDIO_FMT_FLAC    0x0b
#define AUDIO_FMT_SBC     0x0c
#define AUDIO_FMT_AAC     0x0d
#define AUDIO_FMT_MSBC    0x0e
#define AUDIO_FMT_CVSD    0x0f
#define AUDIO_FMT_AMR     0x10
#define AUDIO_FMT_OPUS    0x11
```

> `nxlooper` 默认启用，具体配置见各自目录下的 `Kconfig`。

### 3.4 CMocka 单元测试

CMocka 测试代码位于 `apps/testing/drivertest`：

```makefile
ifneq ($(CONFIG_AUDIO),)
MAINSRC  += drivertest_audio.c
PROGNAME += cmocka_driver_audio
endif
```

**配置要求**：

```plaintext
CONFIG_TESTING_CMOCKA=y
CONFIG_AUDIO=y
CONFIG_TESTING_DRIVER_TEST=y
```

**测试程序 `cmocka_driver_audio` 用法**：

```bash
# 1. 两个必需参数：
#    -a 2: 仅播放
#    -a 1: 仅录音
#    -a 3: 先录音后播放
#    -p : 文件名，如 -p /data/1.pcm
#
# 2. 默认设备 /dev/audio/pcm0p, /dev/audio/pcm0c
#    -i 修改设备，如 -i /dev/audio/pcm1c
#
# 3. 默认格式 AUDIO_FMT_PCM
#    -f 修改格式，如 -f mp3
```

## Cross References

- [`audio_lowerhalf_pattern.md`](audio_lowerhalf_pattern.md) — 完整适配框架
- [`audio_lowerhalf_rules.md`](audio_lowerhalf_rules.md) — 实现硬规则
- [`audio_tunnel_pattern.md`](audio_tunnel_pattern.md) — 同核管道驱动
- [`pa_amplifier_pattern.md`](pa_amplifier_pattern.md) — 外置 I2S 智能功放
- [`board_registration.md`](board_registration.md) — Kconfig + 板级注册
