# NuttX OCU (Output Compare Unit) Driver Pattern

适用于输出比较 lower-half 驱动:把比较阈值写入定时器,当自由运行计数器命中阈值时产生比较匹配(可触发中断 /
自动翻转输出引脚),注册为 `/dev/ocuN`。上层框架名与硬件术语一致,都叫 **OCU**(`CONFIG_OCU`)。

---

## 框架概述

- **上层框架**:`drivers/timers/ocu.c`(`CONFIG_OCU`),提供 `ocu_register()`,把 `/dev/ocuN` 的 ioctl 映射到你实现的 `ocu_ops_s` 回调。你只写 lower-half。
- **lower-half 职责**:配置每通道比较寄存器与比较模式(引脚动作)、启停、读计数器/状态,并在比较匹配中断里回调上层。

## 文件布局(通用)

```
drivers/timers/ocu.c                     # 上层框架(ocu_register, /dev/ocuN)——已存在,勿改
include/nuttx/timers/ocu.h               # 接口:ocu_ops_s / ocu_lowerhalf_s / OCUIOC_* / 枚举 / ocu_callback_t
drivers/timers/<chip>_ocu.c 或 arch/<arch>/src/<chip>/<chip>_ocu.c   # 你要写的 lower-half
```

参考现成实现(优先照抄):
- `drivers/timers/ocu_mcal.c` — 上游唯一的具体 lower-half(AUTOSAR MCAL Ocu 适配层),`ocu_ops_s` 全套回调的完整范例,建议作模板。
- `drivers/timers/ocu.c` / `include/nuttx/timers/ocu.h` — 框架与接口定义(必读)。

## Kconfig(通用)

```kconfig
config OCU
	bool "Output Compare Unit (OCU) driver support"
```

chip/board 的使能项(如 `CONFIG_<CHIP>_TIMx_OCU`)在其上 `select OCU`。

## Lower-half Ops(来自 `include/nuttx/timers/ocu.h`)

```c
struct ocu_ops_s
{
  CODE int (*setup)(FAR struct ocu_lowerhalf_s *lower);      /* open 时:上电/配置 */
  CODE int (*shutdown)(FAR struct ocu_lowerhalf_s *lower);   /* close 时:关闭 */
  CODE int (*start)(FAR struct ocu_lowerhalf_s *lower);      /* 使能比较匹配 */
  CODE int (*stop)(FAR struct ocu_lowerhalf_s *lower);
  CODE int (*getstatus)(FAR struct ocu_lowerhalf_s *lower, ...);
  CODE int (*getcounter)(FAR struct ocu_lowerhalf_s *lower, ...);   /* 读自由运行计数器 */
  CODE int (*setabsthreshold)(FAR struct ocu_lowerhalf_s *lower, ...);      /* 绝对阈值 */
  CODE int (*setrelthreshold)(FAR struct ocu_lowerhalf_s *lower, ...);      /* 相对当前计数 */
  CODE int (*setadvancethreshold)(FAR struct ocu_lowerhalf_s *lower, ...);  /* 相对上次匹配 */
  CODE int (*setpinstate)(FAR struct ocu_lowerhalf_s *lower, ...);   /* 立即设输出电平 */
  CODE int (*setpinaction)(FAR struct ocu_lowerhalf_s *lower, ...);  /* 匹配时自动引脚动作 */
  CODE int (*notification)(FAR struct ocu_lowerhalf_s *lower, ...);  /* 开关比较匹配通知 */
};

struct ocu_lowerhalf_s
{
  FAR const struct ocu_ops_s *ops;   /* 必须第一个成员;后接你的私有状态 */
  /* ... per-channel 私有数据(通道号、寄存器基址、last_threshold、callback...) */
};
```

- 用户态通过 IOCTL 使用:`OCUIOC_START/STOP/GETSTATUS/GETCOUNTER/SETABSTHRESHOLD/SETRELTHRESHOLD/
  SETADVANCETHRESHOLD/SETPINSTATE/SETPINACTION/NOTIFICATION`(见 `ocu.h`)。
- 枚举:`ocu_pinstate_e`(引脚电平)、`ocu_pinaction_e`(匹配动作:disconnect/toggle/clear/set/pulse)、
  `ocu_threshold_ret_e`(阈值是否落在参考区间)。
- 比较匹配回调:`typedef bool (*ocu_callback_t)(FAR uint32_t *next_threshold, ...);`——内核内部驱动可返回
  下一个阈值实现周期性比较(没有 `OCUIOC_SETCALLBACK` ioctl,回调仅供内核态驱动用)。

## 阈值语义

- **绝对**(setabsthreshold):OCR = 给定 tick 值。
- **相对**(setrelthreshold):OCR = 当前计数 + delta。
- **递进**(setadvancethreshold):OCR = 上次匹配点 + delta(用于稳定周期序列,不受中断延迟累积影响)。

## 注册

```c
FAR struct ocu_lowerhalf_s *lower = <分配并填好 ops 与私有状态>;
ret = ocu_register("/dev/ocu0", lower);
```

## 常见陷阱

1. **`ocu.c` 曾漏在 `Make.defs`(只在 CMakeLists)**:上游 OCU 框架 `ocu.c` 的构建条目只加进了 `CMakeLists.txt`,
   `Make.defs` 里没有 `ifeq ($(CONFIG_OCU),y) CSRCS += ocu.c`。用 CMake 构建能过,用 **Make/testbuild.sh**(CI)则
   链接报 `undefined reference to ocu_register`。**启用 OCU 时务必确认两个构建文件都编了 `ocu.c` 及你的 lower-half**,
   并用 Make 验证一次。
2. **通知回调在中断上下文**:比较匹配回调多在 ISR 触发,回调里只做轻量动作;返回下一个阈值时注意加当前计数避免已过点。
3. **设备名 snprintf 缓冲区要够大**:`char path[N]`,N 需容纳 `"/dev/ocu"` + `%d` 最坏 11 位 + NUL(≥20)。

## 测试

- `cmocka_driver_ocu`(`apps/testing/drivers/drivertest/drivertest_ocu.c`)——通用 OCU 驱动单测,覆盖
  getstatus/getcounter/startstop/abs·rel·advance 阈值/pinaction/pinstate/notification(比较匹配中断→回调)/幂等 stop。
  依赖 `TESTING_CMOCKA` → `LIBC_REGEX` → `ALLOW_MIT_COMPONENTS`,且需 `CONFIG_ARCH_SETJMP_H=y`(cmocka 用 setjmp)。
  这些是**测试脚手架**,不进生产 defconfig。
- 注册验证:`ls /dev` 出现 `/dev/ocu0..N`。

## 参考实现

- `nuttx/drivers/timers/ocu.c`,`include/nuttx/timers/ocu.h`(上层框架、`ocu_ops_s`、IOCTL、枚举)
- `nuttx/drivers/timers/ocu_mcal.c`(具体 lower-half:MCAL Ocu 适配,`ocu_ops_s` 全套范例)
- `nuttx/drivers/timers/ocu_imx_gpt.c` —— i.MX GPT 的 OCU lower-half
