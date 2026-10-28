# NuttX I2C Master 驱动框架 Pattern

将一个 I2C 控制器适配到 NuttX I2C master upper-half / lower-half 框架。本文档覆盖框架分层、必选/可选 ops、设备注册、数据流模型与最小骨架，适用于任意 I2C master lower-half 驱动开发。

> 适用范围：NuttX in-tree I2C master 驱动开发（寄存器级 / DMA / 中断实现均适用）。任何需要向 NuttX 暴露 `struct i2c_master_s` 的 lower-half 实现都可参考本文档。

---

## 1. 框架概述

NuttX I2C 采用 upper-half / lower-half 分层：

```
应用层 / 上层设备驱动  (i2ctool / 传感器等)
  │  I2C_TRANSFER() / i2c_write() / i2c_read() / i2c_writeread()
  ▼
Upper-half 通用层  (nuttx/drivers/i2c/)
  │  - i2c_driver.c   : /dev/i2cN 字符设备 (I2CIOC_TRANSFER / I2CIOC_RESET)
  │  - i2c_write.c / i2c_read.c / i2c_writeread.c : 便捷封装
  │  ops->transfer(dev, msgs, count)
  ▼
Lower-half 硬件驱动  (struct i2c_ops_s 实现)
  │  寄存器访问 / DMA / 中断
  ▼
硬件 I2C 控制器
```

- **Lower-half** 只需实现 `struct i2c_ops_s`，并把 `struct i2c_master_s`（含 `ops` vtable）作为私有结构体的**首成员**嵌入。
- **Upper-half** 通过 `i2c_register()` 暴露 `/dev/i2cN` 字符设备，或直接由上层设备驱动持有 `struct i2c_master_s *` 并调用 `I2C_TRANSFER()`。
- 传输语义：一次 `transfer(msgs, count)` 是**原子序列**——序列内首个 msg 发 START、末个 msg 发 STOP，中间由 `I2C_M_NOSTART` / `I2C_M_NOSTOP` 控制 repeated-start。整个序列对总线加锁串行化。

---

## 2. 关键头文件

| 文件 | 内容 |
|------|------|
| `nuttx/include/nuttx/i2c/i2c_master.h` | lower-half ops 结构体、`struct i2c_master_s`、`struct i2c_msg_s`、`struct i2c_config_s`、`i2c_register()`、访问宏、`I2C_M_*` flags、速率宏、IOCTL |
| `nuttx/drivers/i2c/i2c_driver.c` | `/dev/i2cN` 字符设备 upper-half（`i2c_register`） |
| `nuttx/drivers/i2c/i2c_write.c` / `i2c_read.c` / `i2c_writeread.c` | 便捷 API 封装到 `ops->transfer` |

### lower-half ops 结构体

```c
struct i2c_ops_s
{
  CODE int (*transfer)(FAR struct i2c_master_s *dev,
                       FAR struct i2c_msg_s *msgs, int count);
#ifdef CONFIG_I2C_RESET
  CODE int (*reset)(FAR struct i2c_master_s *dev);
#endif
  CODE int (*setup)(FAR struct i2c_master_s *dev);
  CODE int (*shutdown)(FAR struct i2c_master_s *dev);
  CODE uint8_t (*status)(FAR struct i2c_master_s *dev);
  CODE int (*cancel)(FAR struct i2c_master_s *dev, FAR void *cookie);
};
```

### 私有结构体嵌入方式

```c
struct <chip>_i2c_dev_s
{
  struct i2c_master_s dev;   /* 必须是首成员，用于 (dev*) <-> (priv*) 转换 */
  /* ... 硬件私有字段：寄存器基址、频率、锁、DMA 句柄、中断号等 ... */
};
```

`(struct <chip>_i2c_dev_s *)dev` 与 `&priv->dev` 之间可直接强转，因 `dev` 是首成员。

### struct i2c_msg_s 关键字段

| 字段 | 说明 |
|------|------|
| `frequency` | 本次传输的 I2C 频率 |
| `addr` | 从机地址（7/10 bit） |
| `flags` | `I2C_M_READ`（读）、`I2C_M_TEN`（10-bit）、`I2C_M_NOSTART`、`I2C_M_NOSTOP` |
| `buffer` / `length` | 数据缓冲区与字节数 |
| `callback` / `arg` | 非 NULL 时为**异步传输**，完成后回调；lower-half 在中断里解锁并回调 |
| `cookie` | 异步传输时 lower-half 回填的句柄，供 `I2C_CANCEL()` 取消 |

---

## 3. 必选 ops 回调

| ops | 是否必选 | 说明 |
|-----|---------|------|
| `transfer(dev, msgs, count)` | **必选** | 执行一个 I2C 消息序列。返回 `OK`(≥0) 或负 errno。是驱动的核心。 |

`transfer` 契约：
- 遍历 `count` 个 `msgs`，按 `flags` 决定读/写、是否 START/STOP。
- 序列内保证原子（对总线加锁串行化）。
- `callback != NULL` → 异步提交并回填 `cookie`；否则同步阻塞到完成。
- 成功返回 `OK` 或已完成的传输数；失败返回负 errno（如 `-EIO`、`-ENXIO`、`-ETIMEDOUT`）。

---

## 4. 可选 ops 回调

| ops | 触发条件 | 说明 |
|-----|---------|------|
| `reset(dev)` | `CONFIG_I2C_RESET` | 总线复位：通过 toggle SCL 释放卡死的从机（典型 9~20 个时钟脉冲）。 |
| `setup(dev)` | — | master 初始化（上电 / 时钟使能）。多数静态初始化驱动可留空或返回 `OK`。 |
| `shutdown(dev)` | — | master 反初始化（关时钟 / 断电）。 |
| `status(dev)` | — | 返回状态位集（`I2C_STATUS_PRESENT` / `I2C_STATUS_BUSY`）。未实现时框架默认返回 `I2C_STATUS_PRESENT`。 |
| `cancel(dev, cookie)` | — | 取消挂起的异步传输；`cookie=NULL` 取消全部。 |

> 最小可用驱动只需实现 `transfer`（+ 视需要 `reset`）。`setup` / `shutdown` / `status` / `cancel` 按硬件能力选实现。

### 地址与速率辅助宏

| 宏 | 用途 |
|----|------|
| `I2C_ADDR8(a)` / `I2C_READADDR8(a)` / `I2C_WRITEADDR8(a)` | 7-bit 地址转 8-bit 读写地址 |
| `I2C_ADDR10H(a)` / `I2C_ADDR10L(a)` | 10-bit 地址高低字节 |
| `I2C_SPEED_STANDARD` (100k) / `I2C_SPEED_FAST` (400k) / `I2C_SPEED_FAST_PLUS` (1M) / `I2C_SPEED_HIGH` (3.4M) | 标准速率常量 |

---

## 5. 设备注册 API

```c
#ifdef CONFIG_I2C_DRIVER
int i2c_register(FAR struct i2c_master_s *i2c, int bus);
#endif
```

- 注册为 `/dev/i2cN`（N = bus minor）。
- 依赖 `CONFIG_I2C_DRIVER`。
- 典型注册流程：
  1. `priv = kmm_zalloc(sizeof(*priv));`
  2. `priv->dev.ops = &g_i2c_ops;`
  3. 硬件初始化（时钟、引脚、寄存器、中断）。
  4. `i2c_register(&priv->dev, bus);`，失败则 `kmm_free(priv)`。

---

## 6. 数据流模型

### 同步写

```
上层 I2C_TRANSFER(msgs, 1)  (flags=0, buffer, length)
  → ops->transfer
      → 硬件发 START + addr(W) + data + STOP，阻塞到完成
  ← 返回 OK / 负 errno
```

### 写后读（repeated start）

```
msgs[0]: 写寄存器地址, flags=I2C_M_NOSTOP
msgs[1]: 读数据,       flags=I2C_M_READ (隐含 repeated START)
I2C_TRANSFER(msgs, 2)
  → ops->transfer 逐条处理，msg[0] 不发 STOP，msg[1] 发 repeated START 后读
```

### 异步传输

```
msg.callback != NULL
  → ops->transfer 提交到硬件队列 / DMA，回填 msg.cookie，立即返回
  → 传输完成中断 → lower-half 回调 msg.callback(arg) + 解锁
  → 需要取消时 I2C_CANCEL(dev, cookie)
```

---

## 7. 实现要点

- **加锁串行化**：`transfer` 内对总线上锁（`mutex` / `sem`），保证多个上层调用者不会交错。异步模式下在完成中断里解锁。
- **START/STOP 处理**：严格按 `I2C_M_NOSTART` / `I2C_M_NOSTOP` 生成时序，正确实现 repeated-start，否则写后读会失败。
- **超时保护**：同步等待硬件完成时加超时（返回 `-ETIMEDOUT`），避免总线异常时永久阻塞。
- **NACK / 仲裁丢失**：从机无应答返回 `-ENXIO`，总线仲裁丢失 / 错误返回 `-EIO`。
- **频率切换**：每个 `msg.frequency` 可能不同，`transfer` 应按需重配时钟分频。
- **`reset` 实现**：将 SCL 配成推挽输出并 toggle 若干个时钟释放卡死从机，恢复后切回开漏。

---

## 8. 参考实现

| 类型 | 路径 | 说明 |
|------|------|------|
| 框架头文件 | `nuttx/include/nuttx/i2c/i2c_master.h` | ops / 结构体 / 注册 API 权威定义 |
| Upper-half 字符设备 | `nuttx/drivers/i2c/i2c_driver.c` | `/dev/i2cN` 实现，`i2c_register` |
| lower-half 骨架（ARM） | `nuttx/arch/arm/src/at32/at32_i2c.c` | 标准寄存器级 I2C master 实现 |
| lower-half 骨架（RISC-V） | `nuttx/arch/risc-v/src/bl602/bl602_i2c.c` | 另一寄存器级参考 |
| lower-half 骨架（sim） | `nuttx/arch/sim/src/sim/sim_i2c.c` | 仿真环境参考 |

---

## 附：最小 lower-half 骨架

```c
static const struct i2c_ops_s g_i2c_ops =
{
  .transfer = <chip>_i2c_transfer,
#ifdef CONFIG_I2C_RESET
  .reset    = <chip>_i2c_reset,
#endif
};

static int <chip>_i2c_transfer(FAR struct i2c_master_s *dev,
                               FAR struct i2c_msg_s *msgs, int count)
{
  FAR struct <chip>_i2c_dev_s *priv = (FAR struct <chip>_i2c_dev_s *)dev;
  int ret = OK;

  /* 上锁串行化整个序列 */

  for (int i = 0; i < count; i++)
    {
      /* 按 msgs[i].flags 决定读/写与 START/STOP，操作硬件寄存器/DMA */
    }

  /* 解锁 */

  return ret;
}
```
