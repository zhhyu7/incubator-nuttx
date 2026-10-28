# NuttX ICU (Input Capture Unit) Driver Pattern

> **术语澄清:ICU 就是 NuttX 的 capture 子系统。** 「ICU / Input Capture Unit」是硬件手册和 AUTOSAR 的叫法;
> 在 NuttX 里对应的上层框架名为 **capture**(`drivers/timers/capture.c`、`CONFIG_CAPTURE`、`cap_lowerhalf_s`、
> `cap_register()`、设备节点 `/dev/capN`)。二者指同一件事——找 NuttX 代码请搜 `capture` / `cap_`(不是 icu)。

适用于输入捕获 lower-half 驱动:在输入信号边沿把定时器计数值锁存,据此推导频率 / 占空比 / 边沿计数,注册为 `/dev/capN`。

---

## 框架概述

- **上层框架**:`drivers/timers/capture.c`(`CONFIG_CAPTURE`;通知功能需 `CONFIG_CAPTURE_NOTIFY`),提供 `cap_register()`,
  把 `/dev/capN` 的 open/close/ioctl 映射到你实现的 `cap_ops_s` 回调。你只写 lower-half。
- **lower-half 职责**:配置定时器通道的输入捕获(上升/下降/双沿),在边沿中断里锁存计数值并推导 freq/duty/edges;
  可选地在边沿到来时通过绑定的回调通知上层(`CONFIG_CAPTURE_NOTIFY`)。

## 文件布局(通用)

```
drivers/timers/capture.c                 # 上层框架(cap_register, /dev/capN)——已存在,勿改
include/nuttx/timers/capture.h           # 接口:cap_ops_s / cap_lowerhalf_s / cap_type_e / capture_notify_t
drivers/timers/<chip>_capture.c 或 arch/<arch>/src/<chip>/<chip>_capture_lowerhalf.c   # 你要写的 lower-half(实现 cap_ops_s + cap_register)
```

参考现成实现(**优先照抄这些,而不是从零写**):
- `drivers/timers/fake_capture.c` — 最简 lower-half(用 wdog 定时器模拟边沿),结构最清晰,建议作起点。
- `arch/arm/src/stm32/stm32_capture_lowerhalf.c`(+ `stm32_capture.c`) — 真实芯片定时器输入捕获 lower-half。
- `boards/xtensa/esp32/common/src/esp32_board_mcpwm.c` — `cap_register()` 的板级注册示例。

## Kconfig(通用)

```kconfig
config CAPTURE
	bool "Capture Driver Support"

config CAPTURE_NOTIFY
	bool "Capture upper half Notatication support"
	depends on CAPTURE
```

board/chip 的 `Kconfig` 里再加自己的使能项(如 `CONFIG_<CHIP>_TIMx_CAP`),并在其上 `select CAPTURE`
(需要边沿通知时再 `select CAPTURE_NOTIFY`)。

## Lower-half Ops(来自 `include/nuttx/timers/capture.h`)

```c
struct cap_ops_s
{
  CODE int (*start)(FAR struct cap_lowerhalf_s *lower);            /* 使能/上电通道 */
  CODE int (*stop)(FAR struct cap_lowerhalf_s *lower);            /* 关闭通道 */
  CODE int (*getduty)(FAR struct cap_lowerhalf_s *lower, FAR uint8_t  *duty);   /* 占空比 % */
  CODE int (*getfreq)(FAR struct cap_lowerhalf_s *lower, FAR uint32_t *freq);   /* 频率 Hz */
  CODE int (*getedges)(FAR struct cap_lowerhalf_s *lower, FAR uint32_t *edges); /* 累计边沿数 */
#ifdef CONFIG_CAPTURE_NOTIFY
  CODE int (*bind)(FAR struct cap_lowerhalf_s *lower,
                   enum cap_type_e type, capture_notify_t cb, FAR void *priv);  /* 绑定边沿通知 */
  CODE int (*unbind)(FAR struct cap_lowerhalf_s *lower);
#endif
  CODE int (*start_capture)(FAR struct cap_lowerhalf_s *lower);   /* 开始一次捕获 */
  CODE int (*stop_capture)(FAR struct cap_lowerhalf_s *lower);
  CODE int (*get_input_state)(FAR struct cap_lowerhalf_s *lower, FAR uint8_t *state);
  CODE int (*ioctl)(FAR struct cap_lowerhalf_s *lower, int cmd, unsigned long arg);
};

struct cap_lowerhalf_s
{
  FAR const struct cap_ops_s *ops;   /* 必须是第一个成员;后面挂你自己的私有状态 */
  /* ... 你的 per-channel 私有数据(通道号、寄存器基址、last_edge、period、high_time、edges...) */
};
```

- 边沿类型:`enum cap_type_e { CAP_TYPE_RISING, CAP_TYPE_FALLING, CAP_TYPE_BOTH }`。
- 通知回调:`typedef void (*capture_notify_t)(FAR struct cap_lowerhalf_s *lower, FAR void *priv);`

## 边沿推导(频率 / 占空比 / 边沿数)

lower-half 在捕获中断里读锁存的计数值,维护:
- 相邻两次**同向**边沿(如两次上升沿)间隔 → `period` → `getfreq = clk / period`。
- 一个周期内高电平时长(上升沿到下降沿)→ `high_time` → `getduty = high_time * 100 / period`。
- 每次边沿 `edges++`。
- 第一条边沿还没有完整周期,`freq`/`duty` 先返回 0(见 fake_capture.c / stm32 实现)。
- 计数器回绕(wraparound)要按无符号差值处理。

## 注册

```c
FAR struct cap_lowerhalf_s *lower = <分配并填好 ops 与私有状态>;
ret = cap_register("/dev/cap0", lower);   /* 上层据此建 /dev/cap0 */
```

`/dev/capN` 的 N 由 `cap_register` 的路径决定;board 层为每个捕获通道调一次。

## 常见陷阱

1. **构建系统两处都要加**:新增的 lower-half 源文件要同时进 `CMakeLists.txt` 和 `Make.defs`(CI 常用 Make,
   本地可能用 CMake;只加一个会出现"本地过、CI 链接未定义符号")。
2. **首边沿无周期**:第一条边沿只有时间戳、没有周期,`getfreq`/`getduty` 需先返回 0,避免除零。
3. **计数器回绕**:period/high_time 用无符号差值 `(now - last)` 计算,自然处理回绕。
4. **通知在中断上下文**:`capture_notify_t` 回调多在 ISR 里触发,回调里只做轻量动作(post 信号量 / 唤醒),
   重活交给工作队列或用户线程。

## 测试

- 应用示例 `apps/examples/capture`(`CONFIG_EXAMPLES_CAPTURE`,`cap` 命令):
  `cap -p /dev/cap0 -n 3 -t 300 -w 14` —— 等通知信号,读若干次采样,打印 freq/duty/edges。
- 边沿来源用真实硬件信号(如另一路 PWM 输出接到捕获输入脚),或用 `fake_capture` 先验证软件路径。
- 注册验证:`ls /dev` 出现 `/dev/cap0..N`。

## 参考实现

- `nuttx/drivers/timers/capture.c`,`include/nuttx/timers/capture.h`(上层框架、ops、notify)
- `nuttx/drivers/timers/fake_capture.c`(最简 lower-half 骨架,推荐起点)
- `nuttx/arch/arm/src/stm32/stm32_capture_lowerhalf.c`,`arch/arm/src/stm32/stm32_capture.c`(真实芯片)
- `nuttx/boards/xtensa/esp32/common/src/esp32_board_mcpwm.c`(`cap_register` 注册示例)
