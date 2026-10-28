# NuttX PWM Driver Pattern

## 框架概述

NuttX PWM 子系统采用标准的 upper-half/lower-half 分层架构：

- **Upper-half**（`nuttx/drivers/timers/pwm.c`）：提供字符设备接口（open/close/ioctl），处理 IOCTL 命令分发，管理脉冲计数等待
- **Lower-half**（芯片/板级实现）：实现硬件相关的 PWM 操作（setup/shutdown/start/stop 等）

```
应用层
  │  open("/dev/pwm0") → ioctl(PWMIOC_SETCHARACTERISTICS/START/STOP)
  ▼
Upper-half (nuttx/drivers/timers/pwm.c)
  │  ops->setup / ops->start / ops->stop / ops->shutdown
  ▼
Lower-half (芯片驱动实现 struct pwm_ops_s)
  │  定时器/计数器寄存器操作
  ▼
PWM 硬件（Timer + Compare + Output Pin）
```

### Upper-half 自动行为

| 事件 | Upper-half 行为 |
|------|----------------|
| 首次 `open()` | 调用 `ops->setup()` 初始化硬件 |
| 最后一次 `close()` | 调用 `ops->shutdown()` 释放硬件 |
| `PWMIOC_START` | 调用 `ops->start()`；若 PULSECOUNT 模式且 count>0，阻塞等待 `pwm_expired()` |
| `PWMIOC_STOP` | 调用 `ops->stop()` |
| `PWMIOC_SETCHARACTERISTICS` | 保存 info；若已 started 则重新调用 `ops->start()` 实现动态更新 |

## 关键头文件

```
nuttx/include/nuttx/timers/pwm.h
```

## 关键 Kconfig 配置

| 配置项 | 说明 |
|--------|------|
| `CONFIG_PWM` | 启用 PWM 框架 |
| `CONFIG_PWM_MULTICHAN` | 多通道支持（一个 timer 输出多路 PWM） |
| `CONFIG_PWM_NCHANNELS` | 多通道模式下最大通道数 |
| `CONFIG_PWM_PULSECOUNT` | 脉冲计数模式（输出固定数量脉冲后停止） |
| `CONFIG_PWM_DEADTIME` | 死区时间支持（互补输出） |
| `CONFIG_PWM_OVERWRITE` | 通道输出覆写支持 |

## Lower-half Ops 结构体定义

```c
struct pwm_ops_s
{
  /* 必选回调 */

  CODE int (*setup)(FAR struct pwm_lowerhalf_s *dev);
  CODE int (*shutdown)(FAR struct pwm_lowerhalf_s *dev);

#ifdef CONFIG_PWM_PULSECOUNT
  CODE int (*start)(FAR struct pwm_lowerhalf_s *dev,
                    FAR const struct pwm_info_s *info,
                    FAR void *handle);
#else
  CODE int (*start)(FAR struct pwm_lowerhalf_s *dev,
                    FAR const struct pwm_info_s *info);
#endif

  CODE int (*stop)(FAR struct pwm_lowerhalf_s *dev);
  CODE int (*ioctl)(FAR struct pwm_lowerhalf_s *dev,
                    int cmd, unsigned long arg);

  /* 可选回调 */

  CODE int (*set_output_idle)(FAR struct pwm_lowerhalf_s *dev,
                              int channel);
  CODE int (*get_output_state)(FAR struct pwm_lowerhalf_s *dev,
                               int channel, FAR bool *state);
  CODE int (*enable_notify)(FAR struct pwm_lowerhalf_s *dev,
                            int channel, uint8_t edge);
  CODE int (*disable_notify)(FAR struct pwm_lowerhalf_s *dev,
                             int channel);
  CODE int (*get_power_state)(FAR struct pwm_lowerhalf_s *dev,
                              FAR uint8_t *state);
  CODE int (*set_power_state)(FAR struct pwm_lowerhalf_s *dev,
                              uint8_t state);
};
```

### Ops 回调说明

| Ops | 必选/可选 | 说明 |
|-----|----------|------|
| `setup` | **必选** | 首次 open 时调用，配置 GPIO 引脚为 PWM 功能，初始化定时器，但不输出脉冲 |
| `shutdown` | **必选** | 最后 close 时调用，停止输出、释放引脚、进入最低功耗 |
| `start` | **必选** | 根据 `pwm_info_s` 配置频率/占空比并启动输出。PULSECOUNT 模式额外接收 handle 参数 |
| `stop` | **必选** | 停止脉冲输出，复位定时器 |
| `ioctl` | **必选** | 处理 upper-half 未识别的平台自定义 IOCTL |
| `set_output_idle` | 可选 | 立即将指定通道输出设为空闲电平 |
| `get_output_state` | 可选 | 读取指定通道当前输出电平（HIGH/LOW） |
| `enable_notify` | 可选 | 启用边沿通知中断 |
| `disable_notify` | 可选 | 禁用边沿通知中断 |
| `get_power_state` | 可选 | 获取当前电源状态 |
| `set_power_state` | 可选 | 执行电源状态转换 |

### start 回调关键行为

1. 解析 `info->frequency` 计算定时器分频和周期（TOP 值）
2. 解析 `info->duty`（单通道）或 `info->channels[]`（多通道）计算比较值
3. 配置定时器寄存器并使能输出
4. **PULSECOUNT 模式**：配置中断，在脉冲计数到期时调用 `pwm_expired(handle)`

## Lower-half 状态结构体

```c
struct pwm_lowerhalf_s
{
  FAR const struct pwm_ops_s *ops;  /* 必须是第一个字段 */
};
```

典型的驱动私有结构体扩展：

```c
struct my_pwm_lowerhalf_s
{
  const struct pwm_ops_s *ops;    /* 必须是第一个字段 */
  uint32_t frequency;             /* 当前频率 */
  uint32_t timer_base;            /* 定时器寄存器基地址 */
  uint8_t  timer_id;              /* 定时器编号 */
#ifdef CONFIG_PWM_MULTICHAN
  ub16_t   duty[CONFIG_PWM_NCHANNELS];  /* 各通道占空比 */
  int8_t   pin[CONFIG_PWM_NCHANNELS];   /* 各通道 GPIO */
#else
  ub16_t   duty;                  /* 占空比 */
  int8_t   pin;                   /* 输出 GPIO */
#endif
};
```

## PWM 信息结构体

```c
struct pwm_info_s
{
  pwm_freq_t frequency;           /* 脉冲频率 (Hz) */

#ifdef CONFIG_PWM_MULTICHAN
  struct pwm_chan_s channels[CONFIG_PWM_NCHANNELS];
#else
  ub16_t   duty;                  /* 占空比: 0x0001~0xffff 对应 1/65536~65535/65536 */
#ifdef CONFIG_PWM_DEADTIME
  ub16_t   dead_time_a;           /* 主输出死区 */
  ub16_t   dead_time_b;           /* 互补输出死区 */
#endif
#ifdef CONFIG_PWM_PULSECOUNT
  uint32_t count;                 /* 脉冲数: 0=无限, >0=固定数量 */
#endif
  uint8_t  cpol;                  /* 通道极性: PWM_CPOL_LOW/HIGH */
  uint8_t  dcpol;                 /* 禁用通道极性: PWM_DCPOL_LOW/HIGH */
#endif
  FAR void *arg;                  /* 用户参数 */
};

/* 多通道模式下的单通道描述 */
struct pwm_chan_s
{
  ub16_t  duty;                   /* 占空比 */
#ifdef CONFIG_PWM_DEADTIME
  ub16_t  dead_time_a;
  ub16_t  dead_time_b;
#endif
  uint8_t cpol;                   /* 通道极性 */
  uint8_t dcpol;                  /* 禁用通道极性 */
  int8_t  channel;                /* 通道号: 1~N=活跃, 0=跳过, -1=结束 */
};
```

### 占空比编码

占空比使用 `ub16_t`（unsigned 16.16 定点数的低 16 位）：
- `0x0001` = 1/65536 ≈ 0.0015%
- `0x8000` = 32768/65536 = 50%
- `0xffff` = 65535/65536 ≈ 99.998%

在 start 回调中转换为比较值：`compare = (uint32_t)duty * top / 65536`

## IOCTL 命令表

| IOCTL | 参数 | 说明 |
|-------|------|------|
| `PWMIOC_SETCHARACTERISTICS` | `const struct pwm_info_s *` | 设置频率/占空比，若已启动则动态更新 |
| `PWMIOC_GETCHARACTERISTICS` | `struct pwm_info_s *` | 获取当前配置 |
| `PWMIOC_START` | 无 | 启动脉冲输出（需先 SET） |
| `PWMIOC_STOP` | 无 | 停止脉冲输出 |
| `PWMIOC_SETOUTPUTIDLE` | `int channel` | 设置通道为空闲电平 |
| `PWMIOC_GETOUTPUTSTATE` | `struct pwm_output_state_s *` | 读取通道输出状态 |
| `PWMIOC_ENABLENOTIFY` | `const struct pwm_notify_s *` | 启用边沿通知 |
| `PWMIOC_DISABLENOTIFY` | `int channel` | 禁用边沿通知 |
| `PWMIOC_GETPOWERSTATE` | `uint8_t *` | 获取电源状态 |
| `PWMIOC_SETPOWERSTATE` | `uint8_t state` | 设置电源状态 |

## 设备注册 API

```c
int pwm_register(FAR const char *path, FAR struct pwm_lowerhalf_s *dev);
```

- **path**：设备路径，约定 `/dev/pwm0`、`/dev/pwm1`...
- **dev**：lower-half 实例指针，必须在驱动生命周期内持续有效
- **返回值**：0 成功，负 errno 失败
- **调用时机**：在 `board_late_initialize()` 或板级 bringup 函数中

### PULSECOUNT 模式回调

```c
#ifdef CONFIG_PWM_PULSECOUNT
void pwm_expired(FAR void *handle);
#endif
```

当 lower-half 检测到脉冲计数到期（通常通过中断），必须调用此函数唤醒等待的上层。`handle` 是 `start()` 回调中传入的第三个参数。

## 完整 Lower-half 实现骨架

```c
#include <nuttx/config.h>
#include <nuttx/timers/pwm.h>
#include <nuttx/irq.h>

/* 私有结构体 */

struct mychip_pwm_s
{
  const struct pwm_ops_s *ops;
  uint32_t base;                  /* 寄存器基地址 */
  uint8_t  timer_id;
  uint32_t frequency;
#ifdef CONFIG_PWM_MULTICHAN
  ub16_t   duty[CONFIG_PWM_NCHANNELS];
#else
  ub16_t   duty;
#endif
#ifdef CONFIG_PWM_PULSECOUNT
  FAR void *handle;               /* upper-half handle */
  uint32_t count;                 /* 剩余脉冲数 */
#endif
};

/* 前向声明 */

static int  mychip_pwm_setup(FAR struct pwm_lowerhalf_s *dev);
static int  mychip_pwm_shutdown(FAR struct pwm_lowerhalf_s *dev);
#ifdef CONFIG_PWM_PULSECOUNT
static int  mychip_pwm_start(FAR struct pwm_lowerhalf_s *dev,
                             FAR const struct pwm_info_s *info,
                             FAR void *handle);
#else
static int  mychip_pwm_start(FAR struct pwm_lowerhalf_s *dev,
                             FAR const struct pwm_info_s *info);
#endif
static int  mychip_pwm_stop(FAR struct pwm_lowerhalf_s *dev);
static int  mychip_pwm_ioctl(FAR struct pwm_lowerhalf_s *dev,
                             int cmd, unsigned long arg);

/* Ops 表 */

static const struct pwm_ops_s g_mychip_pwm_ops =
{
  .setup    = mychip_pwm_setup,
  .shutdown = mychip_pwm_shutdown,
  .start    = mychip_pwm_start,
  .stop     = mychip_pwm_stop,
  .ioctl    = mychip_pwm_ioctl,
};

/****************************************************************************
 * setup — 配置引脚和时钟，不输出脉冲
 ****************************************************************************/

static int mychip_pwm_setup(FAR struct pwm_lowerhalf_s *dev)
{
  FAR struct mychip_pwm_s *priv = (FAR struct mychip_pwm_s *)dev;

  /* 1. 使能定时器时钟 */
  /* 2. 配置 GPIO 引脚为 PWM 复用功能 */
  /* 3. 复位定时器到默认状态 */

  return OK;
}

/****************************************************************************
 * shutdown — 停止输出，释放引脚，最低功耗
 ****************************************************************************/

static int mychip_pwm_shutdown(FAR struct pwm_lowerhalf_s *dev)
{
  FAR struct mychip_pwm_s *priv = (FAR struct mychip_pwm_s *)dev;

  /* 1. 停止定时器 */

  mychip_pwm_stop(dev);

  /* 2. 将引脚恢复为 GPIO 输入或低功耗状态 */
  /* 3. 关闭定时器时钟 */

  return OK;
}

/****************************************************************************
 * start — 配置频率/占空比并启动输出
 ****************************************************************************/

#ifdef CONFIG_PWM_PULSECOUNT
static int mychip_pwm_start(FAR struct pwm_lowerhalf_s *dev,
                            FAR const struct pwm_info_s *info,
                            FAR void *handle)
#else
static int mychip_pwm_start(FAR struct pwm_lowerhalf_s *dev,
                            FAR const struct pwm_info_s *info)
#endif
{
  FAR struct mychip_pwm_s *priv = (FAR struct mychip_pwm_s *)dev;

#ifdef CONFIG_PWM_PULSECOUNT
  priv->handle = handle;
  priv->count  = info->count;
#endif

  /* 1. 根据 info->frequency 计算分频器和 TOP/ARR 值 */
  /* 2. 根据 duty 计算比较/匹配值:
   *    compare = (uint32_t)info->duty * top / 65536;
   */
  /* 3. 写入定时器寄存器 */
  /* 4. 使能定时器输出 */

#ifdef CONFIG_PWM_PULSECOUNT
  if (info->count > 0)
    {
      /* 使能更新中断，在 ISR 中递减 count，到 0 时调用 pwm_expired(handle) */
    }
#endif

  return OK;
}

/****************************************************************************
 * stop — 停止脉冲输出
 ****************************************************************************/

static int mychip_pwm_stop(FAR struct pwm_lowerhalf_s *dev)
{
  FAR struct mychip_pwm_s *priv = (FAR struct mychip_pwm_s *)dev;

  /* 1. 禁用定时器输出 */
  /* 2. 禁用中断（如果使用了 PULSECOUNT） */

  return OK;
}

/****************************************************************************
 * ioctl — 处理平台自定义命令
 ****************************************************************************/

static int mychip_pwm_ioctl(FAR struct pwm_lowerhalf_s *dev,
                            int cmd, unsigned long arg)
{
  return -ENOTTY;  /* 无自定义命令时返回 -ENOTTY */
}
```

## 初始化与注册函数

```c
/****************************************************************************
 * arch 层导出函数（供 board 层调用）
 ****************************************************************************/

FAR struct pwm_lowerhalf_s *mychip_pwminitialize(int timer)
{
  FAR struct mychip_pwm_s *priv;

  priv = kmm_zalloc(sizeof(struct mychip_pwm_s));
  if (priv == NULL)
    {
      return NULL;
    }

  priv->ops      = &g_mychip_pwm_ops;
  priv->timer_id = timer;
  priv->base     = get_timer_base(timer);

  return (FAR struct pwm_lowerhalf_s *)priv;
}
```

## Board 层注册

```c
/* boards/<arch>/<chip>/<board>/src/<board>_pwm.c */

#include <nuttx/timers/pwm.h>
#include "mychip_pwm.h"

int board_pwm_setup(void)
{
  FAR struct pwm_lowerhalf_s *pwm;
  int ret;

  pwm = mychip_pwminitialize(1);  /* Timer 1 */
  if (pwm == NULL)
    {
      pwmerr("ERROR: Failed to get PWM lower half\n");
      return -ENODEV;
    }

  ret = pwm_register("/dev/pwm0", pwm);
  if (ret < 0)
    {
      pwmerr("ERROR: pwm_register failed: %d\n", ret);
      return ret;
    }

  return OK;
}
```

## 应用层使用示例

```c
#include <nuttx/timers/pwm.h>
#include <fcntl.h>
#include <sys/ioctl.h>

int pwm_example(void)
{
  struct pwm_info_s info;
  int fd;
  int ret;

  fd = open("/dev/pwm0", O_RDONLY);
  if (fd < 0)
    {
      return -errno;
    }

  /* 配置 1kHz, 50% 占空比 */

  info.frequency = 1000;
#ifdef CONFIG_PWM_MULTICHAN
  info.channels[0].channel = 1;
  info.channels[0].duty    = 0x8000;  /* 50% */
  info.channels[1].channel = -1;      /* 结束标记 */
#else
  info.duty = 0x8000;                 /* 50% */
#endif

  ret = ioctl(fd, PWMIOC_SETCHARACTERISTICS, (unsigned long)&info);
  if (ret < 0)
    {
      goto out;
    }

  ret = ioctl(fd, PWMIOC_START, 0);
  if (ret < 0)
    {
      goto out;
    }

  sleep(5);

  ioctl(fd, PWMIOC_STOP, 0);

out:
  close(fd);
  return ret;
}
```

## 测试工具 pwm

NuttX 内置 `pwm` 命令行工具（`CONFIG_EXAMPLES_PWM`）：

```bash
nsh> pwm -p /dev/pwm0 -f 1000 -d 80 -t 5
# -p: 设备路径
# -f: 频率 (Hz)
# -d: 占空比 (百分比, 1-99)
# -t: 持续时间 (秒)
# -n: 脉冲数 (PULSECOUNT 模式)
```

## 驱动文件布局

```
nuttx/
├── drivers/timers/
│   ├── pwm.c                 # Upper-half（框架提供，不修改）
│   ├── Make.defs             # 已包含 pwm.c
│   └── Kconfig               # CONFIG_PWM 已定义
├── include/nuttx/timers/
│   └── pwm.h                 # 公共头文件（框架提供）
├── arch/<arch>/src/<chip>/
│   ├── <chip>_pwm.c          # Lower-half 实现
│   └── <chip>_pwm.h          # arch 层头文件（导出 initialize 函数）
└── boards/<arch>/<chip>/<board>/src/
    └── <board>_pwm.c         # Board 注册：调用 initialize + pwm_register
```

## 常见陷阱

| 问题 | 原因 | 解决 |
|------|------|------|
| 动态改频率无效 | `start` 中未检查 frequency 是否变化就跳过配置 | 始终根据 info 重新配置，或比较后决定 |
| PULSECOUNT 模式卡死 | ISR 中未调用 `pwm_expired(handle)` | 确保中断处理正确调用 |
| 多通道只有第一路有输出 | 未遍历 `channels[]` 数组到 `channel == -1` | 循环直到 channel < 0 |
| 占空比 100% 无输出 | compare == top 时某些硬件行为不同 | 特殊处理 duty==0xffff 为强制高电平 |
| shutdown 后引脚浮空 | 未将引脚恢复为确定状态 | shutdown 中设置引脚为 GPIO 输出低/高 |

## 参考实现

| 实现 | 路径 | 特点 |
|------|------|------|
| RP2040 | `nuttx/arch/arm/src/rp2040/rp2040_pwm.c` | 简洁单文件，适合入门参考 |
| STM32 | `nuttx/arch/arm/src/stm32/stm32_pwm.c` | 功能完整，支持多通道/脉冲计数/死区 |
| ESP32 LEDC | `nuttx/arch/xtensa/src/esp32/esp32_ledc.c` | LED 控制器作为 PWM 使用 |
| Board 注册 | `nuttx/boards/arm/stm32/nucleo-f446re/src/stm32_pwm.c` | 典型 board 层注册模式 |
