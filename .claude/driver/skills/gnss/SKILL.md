---
name: gnss
description: |
  Vela/NuttX GNSS 子系统开发与调试。覆盖 GNSS 框架（gnss_uorb.c upper-half driver）、
  fakesensor GNSS 仿真（CSV 回放 + KVDB 运行时切换）、板级初始化、芯片适配。
  Use when: GNSS, GPS, 定位, gnss_uorb, fakesensor gnss, board_gnss_init,
  persist.fakesensor.gnss, NMEA, GGA, RMC, ttyGNSS, sensortest gnss,
  CC025X, AG3335M, SYN4778, 卫星定位, gnss_register, gnss_lowerhalf,
  GNSS 无数据, GNSS 初始化失败, fakesensor 切换, 室内测试 GNSS.
  Not for: BLE 定位(非卫星), WiFi 定位, 通用 sensor 驱动开发(use driver:nuttx-driver-development).
---

# GNSS Skill

## 1. 架构概览

```
┌─────────────────────────────────────────────────────────┐
│                    Application Layer                      │
│         (sensortest / location service / app)            │
├─────────────────────────────────────────────────────────┤
│                   uORB Sensor Framework                   │
│              /dev/uorb/sensor_gnss0 等                    │
├─────────────────────────────────────────────────────────┤
│              GNSS Upper-Half Driver                       │
│         nuttx/drivers/sensors/gnss_uorb.c                │
│   (NMEA 解析 / 环形缓冲区 / 多星座支持 / file_ops)       │
├─────────────────────────────────────────────────────────┤
│              GNSS Lower-Half Driver                       │
│   ┌──────────────┐    ┌──────────────────────────┐      │
│   │  Real Chip   │    │   Fakesensor (CSV回放)    │      │
│   │ CC025X/AG335 │    │ fakesensor_uorb.c         │      │
│   │ /SYN4778     │    │ 数据源: /resource/misc/   │      │
│   └──────────────┘    │         csv/gps.csv       │      │
│                        └──────────────────────────┘      │
├─────────────────────────────────────────────────────────┤
│              Board Init (board_gnss_init.c)               │
│   KVDB 运行时选择: persist.fakesensor.gnss → fakesensor  │
│                    ro.factory.gnss.chip → real chip       │
└─────────────────────────────────────────────────────────┘
```

## 2. 核心文件路径

| 文件 | 作用 |
|------|------|
| `nuttx/drivers/sensors/gnss_uorb.c` | Upper-half GNSS 驱动框架，NMEA 解析，uORB 集成 |
| `nuttx/include/nuttx/sensors/gnss.h` | GNSS 驱动接口定义（gnss_lowerhalf_s / gnss_register） |
| `nuttx/drivers/sensors/fakesensor_uorb.c` | Fakesensor 驱动，CSV 数据回放 |
| `nuttx/include/nuttx/sensors/fakesensor.h` | Fakesensor 接口（fakesensor_init） |
| `vendor/bes/boards/best1700_ep/p62/src/bsp/gnss/board_gnss_init.c` | P62 板级 GNSS 初始化 |

## 3. GNSS 框架原理（gnss_uorb.c）

### 3.1 Upper/Lower Half 分层

- **Upper-half**（`gnss_uorb.c`）：提供统一的字符设备 `/dev/ttyGNSS%d` 和 uORB sensor 节点，负责 NMEA 协议解析、环形缓冲区管理、多星座数据分发
- **Lower-half**（芯片驱动或 fakesensor）：实现 `gnss_lowerhalf_s` 的 ops（activate / set_interval / control / inject），向 upper-half push 原始 NMEA 数据

### 3.2 注册流程

```c
// Lower-half 驱动调用 gnss_register 注册到框架
int gnss_register(FAR struct gnss_lowerhalf_s *dev, int devno,
                  FAR uint32_t *nbuffer, int count);
```

注册后自动创建：
- `/dev/ttyGNSS<devno>` — 字符设备（读写 NMEA 原始数据）
- `/dev/uorb/sensor_gnss<devno>` — uORB sensor 节点（结构化定位数据）

### 3.3 NMEA 解析

Upper-half 内置 minmea 库解析以下语句：
- **GGA** — 定位数据（经纬度、海拔、卫星数、HDOP）
- **RMC** — 推荐最小定位信息（速度、航向、日期）
- **GSV** — 可见卫星信息
- **GSA** — DOP 和活跃卫星

定位有效条件：GGA + RMC 标志位同时置位。

### 3.4 多星座支持

| 前缀 | 星座 |
|------|------|
| GP | GPS |
| GL | GLONASS |
| GQ | QZSS |
| GB / BD | BeiDou |
| GA | Galileo |

## 4. Fakesensor GNSS 功能

### 4.1 原理

Fakesensor GNSS 通过 CSV 文件回放预录的 NMEA 数据，模拟真实 GNSS 芯片输出。适用于：
- 室内开发调试（无卫星信号环境）
- CI/CT 自动化测试
- 定位功能验证（无需真实硬件）

工作流程：
1. `fakesensor_init(SENSOR_TYPE_GNSS, csv_path, devno, batch)` 创建 fakesensor 实例
2. 内部调用 `gnss_register()` 注册到 GNSS upper-half 框架
3. 独立内核线程循环读取 CSV 文件，按 interval 周期 push NMEA 数据
4. Upper-half 解析 NMEA 并通过 uORB 发布结构化定位数据

### 4.2 CSV 文件格式

```
interval:1000
nmea
$GNGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,47.0,M,,*47
$GNRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W*6A
...
```

- 第一行：`interval:<ms>` 数据推送间隔（毫秒）
- 第二行：CSV header（对 GNSS 类型为 `nmea`）
- 后续行：NMEA 语句，循环回放

默认数据文件路径：`/resource/misc/csv/gps.csv`

### 4.3 KVDB 运行时切换

板级初始化 `board_gnss_initialize()` 通过 KVDB 属性实现运行时选择，**无需重新编译**：

```c
// board_gnss_init.c 核心逻辑
if (property_get_bool("persist.fakesensor.gnss", 0)) {
    // 使用 fakesensor CSV 回放
    fakesensor_init(SENSOR_TYPE_GNSS, "/resource/misc/csv/gps.csv", 0, 50);
} else {
    // 读取 ro.factory.gnss.chip 选择真实芯片驱动
    // CC025X / AG3335M / SYN4778
}
```

**切换命令**：

```bash
# 开启 fakesensor GNSS
setprop persist.fakesensor.gnss true
reboot

# 恢复真实 GNSS
setprop persist.fakesensor.gnss false
reboot
```

### 4.4 真实芯片选择逻辑

当 fakesensor 未启用时，通过 `ro.factory.gnss.chip` KVDB 键选择芯片驱动：

| KVDB 值 | 芯片 | 初始化函数 |
|---------|------|-----------|
| `CC0257B` | CC025X | `board_cc025x_initialize()` |
| `AG3335M` | AG3335M | `board_ag3335m_initialize()` |
| `SYN4778` | SYN4778 | `board_syn4778_initialize()` |

## 5. Kconfig 配置

### 5.1 必需配置项

```kconfig
# GNSS 框架（upper-half）
CONFIG_SENSORS_GNSS=y

# Fakesensor 支持
CONFIG_SENSORS_FAKESENSOR=y

# KVDB（运行时属性读取）
CONFIG_KVDB=y

# 板级 GNSS 驱动总开关
CONFIG_MIWEAR_DRIVERS_GNSS=y

# 真实芯片驱动（按需选一）
CONFIG_MIWEAR_DRIVERS_GNSS_CC025X=y
# CONFIG_MIWEAR_DRIVERS_GNSS_AG3335M=y
# CONFIG_MIWEAR_DRIVERS_GNSS_SYN4778=y
```

### 5.2 Fakesensor 独立使用（不依赖真实芯片）

只需：
```kconfig
CONFIG_SENSORS_GNSS=y
CONFIG_SENSORS_FAKESENSOR=y
CONFIG_KVDB=y
CONFIG_MIWEAR_DRIVERS_GNSS=y
```

无需开启任何 `CONFIG_MIWEAR_DRIVERS_GNSS_*` 芯片驱动。

## 6. 测试与验证

### 6.1 验证 fakesensor 注册成功

```bash
# 检查设备节点
ls /dev/ttyGNSS0
ls /dev/uorb/sensor_gnss0

# 查看启动日志
dmesg | grep GNSS
# 期望输出: [GNSS] fakesensor registered via KVDB
```

### 6.2 读取定位数据

```bash
# 通过 sensortest 读取 uORB 数据
sensortest -n 10 gnss0

# 通过字符设备读取原始 NMEA
cat /dev/ttyGNSS0
```

### 6.3 确认 KVDB 属性

```bash
# 查看当前 fakesensor 开关状态
getprop persist.fakesensor.gnss

# 查看当前芯片类型
getprop ro.factory.gnss.chip
```

## 7. 常见问题排查

| 现象 | 可能原因 | 排查方法 |
|------|---------|---------|
| `/dev/ttyGNSS0` 不存在 | GNSS 驱动未注册 | 检查 `CONFIG_SENSORS_GNSS=y`，查 dmesg |
| fakesensor 无数据输出 | CSV 文件不存在或格式错误 | 确认 `/resource/misc/csv/gps.csv` 存在且格式正确 |
| `setprop` 后未生效 | 未 reboot | KVDB persist 属性需重启后在 board_init 阶段读取 |
| fakesensor init failed 回退真实 GNSS | CSV 路径错误或内存不足 | 查 syslog `[GNSS] fakesensor init failed` |
| 真实 GNSS 无定位 | 室内无信号 / 芯片未上电 | 切换 fakesensor 验证框架正常，排除硬件问题 |
| gnss_register 返回错误 | 重复注册或 nbuffer 配置异常 | 确认 devno 唯一，检查 batch_number |

## 8. 开发指南

### 8.1 新增芯片适配

1. 在 `vendor/bes/boards/best1700_ep/p62/src/bsp/gnss/` 下新增芯片初始化文件
2. 实现 `gnss_lowerhalf_s` 的 ops 接口
3. 调用 `gnss_register()` 注册到框架
4. 在 `board_gnss_init.c` 的 `gnss_init()` 中添加 KVDB 分支
5. 新增对应 Kconfig 选项 `CONFIG_MIWEAR_DRIVERS_GNSS_<CHIP>`

### 8.2 自定义 fakesensor 数据

1. 准备 NMEA 数据文件（可从真实设备录制）
2. 添加 `interval:<ms>` 头和 `nmea` header
3. 放置到设备文件系统（如 `/resource/misc/csv/custom_gnss.csv`）
4. 修改 `board_gnss_init.c` 中的 CSV 路径，或通过新增 KVDB 键动态指定
