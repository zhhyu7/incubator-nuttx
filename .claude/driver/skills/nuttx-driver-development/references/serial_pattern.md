# Serial (UART) Driver Pattern — NuttX 串口驱动框架

本文档是 serial（UART）驱动子系统的完整参考，由主 SKILL.md 的 Driver Type Dispatch Table 自动路由加载。

## Table of Contents

1. [一、框架概述](#一框架概述) — 上下半区模型、数据流、Console 特殊处理
2. [二、开发准备：代码与配置](#二开发准备代码与配置) — 关键文件位置、Kconfig、驱动文件布局
3. [三、关键数据结构](#三关键数据结构) — `uart_dev_s`、`uart_ops_s`、`uart_buffer_s`、私有结构体
4. [四、核心 API 与驱动操作集](#四核心-api-与驱动操作集) — `uart_ops_s` 回调详解、上半区辅助函数
5. [五、数据传输模式](#五数据传输模式) — 中断逐字节模式、批量 recvbuf/sendbuf 模式、DMA 模式
6. [六、Console 与 Early Serial](#六console-与-early-serial) — up_putc、earlyserialinit、serialinit
7. [七、TERMIOS 支持](#七termios-支持) — 波特率/数据位/停止位/奇偶校验动态配置
8. [八、流控支持](#八流控支持) — 硬件流控（RTS/CTS）、软件流控
9. [九、DMA 集成](#九dma-集成) — TX DMA、RX DMA 回调与缓冲区管理
10. [十、完整实现模板](#十完整实现模板) — 生产级 lower-half 骨架代码
11. [十一、常见问题与陷阱](#十一常见问题与陷阱) — 典型 bug 模式（含 MCAL/多核 panic 经验）
12. [十二、Cross References](#十二cross-references) — 关联文档索引

---

## 一、框架概述

NuttX serial 子系统采用经典的 **Upper-Half / Lower-Half** 架构。上半区（`drivers/serial/serial.c`）实现 VFS 字符设备接口（open/read/write/ioctl/poll/close），管理环形缓冲区和进程同步。下半区（arch-specific 或 driver-specific）实现硬件相关的 `uart_ops_s` 回调。

### 上下半区模型

```
Application (read/write/ioctl/poll/tcsetattr...)
    │
    ▼
VFS: /dev/ttyS0, /dev/console
    │
    ▼
Upper-half: serial.c + serial_io.c + serial_dma.c
    │  ← 管理 RX/TX 环形缓冲区、信号量、poll 唤醒
    │  ← 调用 lower-half ops 回调
    ▼
Lower-half: YOUR driver (implements uart_ops_s)
    │  ← 配置硬件、管理中断、收发字节
    ▼
UART Hardware (寄存器访问)
```

### 数据流

**TX 路径**（应用写入 → 硬件发送）:
1. 应用调用 `write()` → 上半区将数据写入 `xmit` 环形缓冲区
2. 上半区调用 `txint(dev, true)` 使能 TX 中断
3. TX 中断触发 → 上半区调用 `uart_xmitchars()` → 调用 `send()` 或 `sendbuf()` 逐字节/批量发送
4. 缓冲区空 → 上半区调用 `txint(dev, false)` 关闭 TX 中断

**RX 路径**（硬件接收 → 应用读取）:
1. RX 中断触发 → lower-half ISR 调用 `uart_recvchars(dev)`
2. `uart_recvchars()` 循环调用 `receive()` 或 `recvbuf()` 读取硬件数据到 `recv` 环形缓冲区
3. 缓冲区有数据 → 唤醒阻塞在 `read()` 的应用线程

### Console 特殊处理

- Console 设备在 `earlyserialinit()` 阶段仅调用 `setup()`（配置波特率等），**不** attach 中断
- 早期阶段 `up_putc()` 通过 polling 方式逐字符输出（不依赖中断）
- `serialinit()` 阶段才注册所有 `/dev/ttyS*` 和 `/dev/console`，此时 Console 也进入中断驱动模式
- Console 设备的 `shutdown()` **永远不会被调用**（即使所有 fd 关闭）

---

## 二、开发准备：代码与配置

### 关键文件位置

**框架核心**:
- `nuttx/drivers/serial/serial.c` — 上半区：VFS file_operations 实现（open/read/write/close/poll/ioctl）
- `nuttx/drivers/serial/serial_io.c` — 上半区：`uart_xmitchars()` / `uart_recvchars()` 数据搬运
- `nuttx/drivers/serial/serial_dma.c` — 上半区：DMA 辅助函数
- `nuttx/include/nuttx/serial/serial.h` — 核心头文件：`uart_dev_s`、`uart_ops_s` 定义

**参考实现**（按复杂度排序）:
- `nuttx/drivers/serial/uart_pl011.c` — 可移植 PL011 驱动（最简洁的参考骨架）
- `nuttx/drivers/serial/serial_cmsdk.c` — ARM CMSDK UART（极简）
- `nuttx/arch/arm/src/stm32f7/stm32_serial.c` — STM32F7（含 DMA、RS485、流控，最完整）
- `nuttx/drivers/serial/uart_16550.c` — 16550 兼容 UART（经典通用）

**Lower-half 驱动位置选择**:
- 架构/芯片特定驱动 → `nuttx/arch/<arch>/src/<chip>/xxx_serial.c`
- 通用 IP 核驱动（PL011、16550 等） → `nuttx/drivers/serial/uart_xxx.c`

### 驱动文件布局

```
# 方式 A：架构特定驱动（推荐用于 SoC 内置 UART）
vendor/<manufacturer>/<chip>/
├── <chip>_serial.c           # Lower-half 实现
├── <chip>_serial.h           # 内部头文件（寄存器定义等）
└── Kconfig                   # 配置入口

# 方式 B：通用驱动（推荐用于标准 IP 核）
nuttx/drivers/serial/
├── uart_myip.c               # Lower-half 实现
├── Make.defs                 # 添加编译
├── CMakeLists.txt            # 添加编译
└── Kconfig-myip              # 子 Kconfig（被主 Kconfig source）

nuttx/include/nuttx/serial/
└── uart_myip.h               # 公共头文件：earlyserialinit/serialinit 原型
```

### Kconfig 配置

```kconfig
# 典型串口 Kconfig 结构

config UART_MYIP
	bool "MyIP UART support"
	default n
	select ARCH_HAVE_SERIAL_TERMIOS
	---help---
		Enable driver support for MyIP UART controller.

if UART_MYIP

config UART0_MYIP
	bool "UART0"
	default n

config UART1_MYIP
	bool "UART1"
	default n

endif # UART_MYIP
```

---

## 三、关键数据结构

### uart_dev_s — 设备实例

每个 UART 端口对应一个 `uart_dev_s` 实例，由 lower-half 静态分配并初始化。

```c
struct uart_dev_s
{
  /* 调用者必须初始化的字段 */
  bool                 isconsole;   /* true: 此设备是串口 console */
  struct uart_buffer_s xmit;       /* TX 环形缓冲区 */
  struct uart_buffer_s recv;       /* RX 环形缓冲区 */
  FAR const struct uart_ops_s *ops; /* Lower-half 操作集 */
  FAR void            *priv;       /* Lower-half 私有数据 */

  /* 上半区管理的字段（初始化为 0 即可） */
  uint16_t             open_count;
  sem_t                xmitsem;    /* TX 空间等待信号量 */
  sem_t                recvsem;    /* RX 数据等待信号量 */
  mutex_t              closelock;
  rspinlock_t          lock;       /* ISR 与线程之间的 spinlock */
  /* ... 其他字段 ... */
};
```

### uart_buffer_s — 环形缓冲区

```c
struct uart_buffer_s
{
  mutex_t              lock;       /* 缓冲区独占访问（上半区管理） */
  volatile sbuf_size_t head;       /* 写入索引 [IN] */
  volatile sbuf_size_t tail;       /* 读取索引 [OUT] */
  sbuf_size_t          size;       /* 缓冲区大小 */
  FAR char            *buffer;     /* 缓冲区指针 */
};
```

**调用者初始化要求**：`size` 和 `buffer` 必须设置，`head`/`tail` 初始化为 0。

### uart_ops_s — Lower-Half 操作集

```c
struct uart_ops_s
{
  /* 必须实现 */
  CODE int   (*setup)(FAR struct uart_dev_s *dev);
  CODE void  (*shutdown)(FAR struct uart_dev_s *dev);
  CODE int   (*attach)(FAR struct uart_dev_s *dev);
  CODE void  (*detach)(FAR struct uart_dev_s *dev);
  CODE int   (*ioctl)(FAR struct file *filep, int cmd, unsigned long arg);
  CODE int   (*receive)(FAR struct uart_dev_s *dev, FAR unsigned int *status);
  CODE void  (*rxint)(FAR struct uart_dev_s *dev, bool enable);
  CODE bool  (*rxavailable)(FAR struct uart_dev_s *dev);
  CODE void  (*send)(FAR struct uart_dev_s *dev, int ch);
  CODE void  (*txint)(FAR struct uart_dev_s *dev, bool enable);
  CODE bool  (*txready)(FAR struct uart_dev_s *dev);
  CODE bool  (*txempty)(FAR struct uart_dev_s *dev);

  /* 可选 */
  CODE bool  (*rxflowcontrol)(...);  /* CONFIG_SERIAL_IFLOWCONTROL */
  CODE void  (*dmasend)(...);        /* CONFIG_SERIAL_TXDMA */
  CODE void  (*dmareceive)(...);     /* CONFIG_SERIAL_RXDMA */
  CODE void  (*dmarxfree)(...);      /* CONFIG_SERIAL_RXDMA */
  CODE void  (*dmatxavail)(...);     /* CONFIG_SERIAL_TXDMA */
  CODE int   (*release)(...);        /* 设备注销时的清理 */
  CODE ssize_t (*recvbuf)(...);      /* 批量接收（替代逐字节 receive） */
  CODE ssize_t (*sendbuf)(...);      /* 批量发送（替代逐字节 send） */
};
```

### Lower-Half 私有结构体（典型模板）

```c
struct myuart_priv_s
{
  /* 硬件配置（通常从 board.h 或 Kconfig 获取） */
  uint32_t         base;         /* 寄存器基地址 */
  uint32_t         clk_freq;     /* 输入时钟频率 */
  uint32_t         baud;         /* 当前波特率 */
  int              irq;          /* 中断号 */

  /* 运行时状态 */
  uint8_t          parity;       /* 0:none, 1:odd, 2:even */
  uint8_t          bits;         /* 数据位: 5-9 */
  uint8_t          stopbits2;    /* true: 2 stop bits */
  bool             flowc;        /* true: 硬件流控使能 */

#ifdef CONFIG_SERIAL_TXDMA
  DMA_HANDLE       txdma;        /* TX DMA 通道句柄 */
#endif
#ifdef CONFIG_SERIAL_RXDMA
  DMA_HANDLE       rxdma;        /* RX DMA 通道句柄 */
  char             rxdmabuf[RXDMA_BUFFER_SIZE]; /* RX DMA 缓冲区 */
#endif

  spinlock_t       lock;         /* 硬件寄存器访问保护 */
};
```

---

## 四、核心 API 与驱动操作集

### uart_ops_s 回调详解

#### setup(dev) — 硬件初始化

**调用时机**: 第一次 `open()` 串口时，以及 `earlyserialinit()` 期间对 console 调用。

**职责**:
- 配置 UART 波特率、数据位、停止位、奇偶校验
- 使能 UART 时钟（如果 SoC 有时钟门控）
- 配置 GPIO 引脚为 UART 功能
- 使能 UART 收发器（TX/RX enable）
- 配置 FIFO（如果有）
- **不要** attach 或 enable 中断（那是 `attach()` 的职责）

```c
static int myuart_setup(FAR struct uart_dev_s *dev)
{
  FAR struct myuart_priv_s *priv = dev->priv;

  /* 使能时钟 */
  myuart_clock_enable(priv->base);

  /* 配置 GPIO */
  myuart_configure_pins(priv->base);

  /* 设置波特率 */
  myuart_set_baudrate(priv->base, priv->clk_freq, priv->baud);

  /* 配置 8N1 默认格式 */
  myuart_set_format(priv->base, priv->bits, priv->parity, priv->stopbits2);

  /* 使能 FIFO */
  myuart_enable_fifo(priv->base);

  /* 使能 UART（TX + RX） */
  myuart_enable(priv->base);

  return OK;
}
```

#### shutdown(dev) — 硬件关闭

**调用时机**: 最后一个 fd `close()` 时。**Console 设备永远不会被调用 shutdown**。

**职责**:
- 禁用 UART 收发器
- 关闭时钟门控（省电）
- 将 GPIO 恢复为默认状态

#### attach(dev) — 中断 attach

**调用时机**: `open()` 时在 `setup()` 之后调用。

**职责**:
- 调用 `irq_attach()` 注册 ISR
- 调用 `up_enable_irq()` 使能中断
- **不要** 在这里使能 RX/TX 中断位（那是 `rxint()`/`txint()` 的职责）

```c
static int myuart_attach(FAR struct uart_dev_s *dev)
{
  FAR struct myuart_priv_s *priv = dev->priv;
  int ret;

  ret = irq_attach(priv->irq, myuart_interrupt, dev);
  if (ret == OK)
    {
      up_enable_irq(priv->irq);
    }

  return ret;
}
```

#### detach(dev) — 中断 detach

**调用时机**: `close()` 时在 `shutdown()` 之前调用。

```c
static void myuart_detach(FAR struct uart_dev_s *dev)
{
  FAR struct myuart_priv_s *priv = dev->priv;

  up_disable_irq(priv->irq);
  irq_detach(priv->irq);
}
```

#### receive(dev, status) — 接收单字节

**调用上下文**: ISR 中（通过 `uart_recvchars()`）。

**返回**: 接收到的字符（int），通过 `*status` 返回错误标志。

```c
static int myuart_receive(FAR struct uart_dev_s *dev, FAR unsigned int *status)
{
  FAR struct myuart_priv_s *priv = dev->priv;

  *status = getreg32(priv->base + MYUART_RSR_OFFSET);  /* 读错误状态 */
  return getreg32(priv->base + MYUART_DR_OFFSET) & 0xFF;
}
```

#### rxavailable(dev) — RX FIFO 非空？

**调用上下文**: ISR 中。`uart_recvchars()` 循环调用直到返回 false。

```c
static bool myuart_rxavailable(FAR struct uart_dev_s *dev)
{
  FAR struct myuart_priv_s *priv = dev->priv;
  return !(getreg32(priv->base + MYUART_FR_OFFSET) & MYUART_FR_RXFE);
}
```

#### rxint(dev, enable) — 使能/禁用 RX 中断

**调用上下文**: 线程上下文（上半区调用）。

```c
static void myuart_rxint(FAR struct uart_dev_s *dev, bool enable)
{
  FAR struct myuart_priv_s *priv = dev->priv;
  irqstate_t flags = spin_lock_irqsave(&priv->lock);

  if (enable)
    {
      myuart_set_imsc(priv->base, MYUART_IMSC_RXIM);
    }
  else
    {
      myuart_clear_imsc(priv->base, MYUART_IMSC_RXIM);
    }

  spin_unlock_irqrestore(&priv->lock, flags);
}
```

#### send(dev, ch) — 发送单字节

**调用上下文**: ISR 中（通过 `uart_xmitchars()`）。

```c
static void myuart_send(FAR struct uart_dev_s *dev, int ch)
{
  FAR struct myuart_priv_s *priv = dev->priv;
  putreg32((uint32_t)ch, priv->base + MYUART_DR_OFFSET);
}
```

#### txready(dev) — TX FIFO 有空间？

**调用上下文**: ISR 中。`uart_xmitchars()` 循环调用直到返回 false。

```c
static bool myuart_txready(FAR struct uart_dev_s *dev)
{
  FAR struct myuart_priv_s *priv = dev->priv;
  return !(getreg32(priv->base + MYUART_FR_OFFSET) & MYUART_FR_TXFF);
}
```

#### txempty(dev) — TX 完全空？（FIFO + 移位寄存器）

**调用上下文**: 线程上下文（`tcdrain()` 等待所有数据物理发送完毕）。

```c
static bool myuart_txempty(FAR struct uart_dev_s *dev)
{
  FAR struct myuart_priv_s *priv = dev->priv;
  return (getreg32(priv->base + MYUART_FR_OFFSET) & MYUART_FR_TXFE) != 0;
}
```

> [!WARNING] `txready` vs `txempty` 区别
>
> - `txready`: TX FIFO 还有空间可以写入新字节 → 用于 ISR 中判断是否可以继续 `send()`
> - `txempty`: TX FIFO **和**移位寄存器都已清空，所有字节已物理发送 → 用于 `tcdrain` / RS485 方向切换

#### txint(dev, enable) — 使能/禁用 TX 中断

**调用上下文**: 可能在线程上下文或 ISR 中调用。

```c
static void myuart_txint(FAR struct uart_dev_s *dev, bool enable)
{
  FAR struct myuart_priv_s *priv = dev->priv;
  irqstate_t flags = spin_lock_irqsave(&priv->lock);

  if (enable)
    {
      myuart_set_imsc(priv->base, MYUART_IMSC_TXIM);

      /* 触发首次发送：在使能 TX 中断后立即调用 uart_xmitchars
       * 因为 FIFO 可能已经为空但不会再触发中断
       */
      uart_xmitchars(dev);
    }
  else
    {
      myuart_clear_imsc(priv->base, MYUART_IMSC_TXIM);
    }

  spin_unlock_irqrestore(&priv->lock, flags);
}
```

> [!IMPORTANT] txint(enable=true) 中调用 uart_xmitchars
>
> 许多实现在 `txint(dev, true)` 时会立即调用 `uart_xmitchars(dev)` 来"启动"发送。
> 原因：如果 TX FIFO 已经空了，硬件不会再产生 TX empty 中断，需要软件触发首次填充。
> 注意此时调用上下文可能是线程，确保 `uart_xmitchars()` 内部的 spinlock 正确保护。

#### ioctl(filep, cmd, arg) — ioctl 处理

主要处理 `TCGETS` / `TCSETS` / `TIOCM*` 等终端控制命令。详见 [七、TERMIOS 支持](#七termios-支持)。

### 上半区辅助函数（Lower-half 在 ISR 或回调中调用）

| 函数 | 由谁调用 | 作用 |
|------|---------|------|
| `uart_recvchars(dev)` | Lower-half RX ISR | 从硬件读取数据到 recv 缓冲区 |
| `uart_xmitchars(dev)` | Lower-half TX ISR | 从 xmit 缓冲区取数据写入硬件 |
| `uart_xmitchars_dma(dev)` | Lower-half（`dmatxavail` 或 DMA 完成回调） | 设置 dmatx 并调用 `ops->dmasend()` |
| `uart_xmitchars_done(dev)` | Lower-half TX DMA 完成回调 | 更新 xmit 缓冲区索引，唤醒 write() |
| `uart_recvchars_dma(dev)` | Lower-half `dmarxfree()` 或首次启动时 | 设置 dmarx 并调用 `ops->dmareceive()` |
| `uart_recvchars_done(dev)` | Lower-half RX DMA/数据到达回调 | 更新 recv.head，唤醒 read()（不自动重启 DMA） |
| `uart_connected(dev, bool)` | Lower-half 热插拔事件 | 通知上半区设备连接/断开（仅 `CONFIG_SERIAL_REMOVABLE`） |

---

## 五、数据传输模式

### 模式 1：中断逐字节（最基础）

实现 `receive()` + `send()` + `rxavailable()` + `txready()`。

ISR 示例：

```c
static int myuart_interrupt(int irq, FAR void *context, FAR void *arg)
{
  FAR struct uart_dev_s *dev = (FAR struct uart_dev_s *)arg;
  FAR struct myuart_priv_s *priv = dev->priv;
  uint32_t status = getreg32(priv->base + MYUART_ISR_OFFSET);

  /* RX 中断 */
  if (status & MYUART_ISR_RXNE)
    {
      uart_recvchars(dev);
    }

  /* TX 中断 */
  if (status & MYUART_ISR_TXE)
    {
      uart_xmitchars(dev);
    }

  /* 清中断 */
  putreg32(status, priv->base + MYUART_ICR_OFFSET);

  return OK;
}
```

### 模式 2：批量 recvbuf/sendbuf（高吞吐）

当硬件有大 FIFO 时，实现 `recvbuf()` / `sendbuf()` 可以减少单字节调用开销。
上半区会优先使用 `recvbuf`/`sendbuf`（如果 ops 中非 NULL）。

```c
static ssize_t myuart_recvbuf(FAR struct uart_dev_s *dev,
                              FAR void *buf, size_t len)
{
  FAR struct myuart_priv_s *priv = dev->priv;
  FAR char *p = (FAR char *)buf;
  size_t count = 0;

  while (count < len && myuart_rxavailable_hw(priv))
    {
      p[count++] = (char)(getreg32(priv->base + MYUART_DR_OFFSET) & 0xFF);
    }

  return count > 0 ? (ssize_t)count : -EAGAIN;
}

static ssize_t myuart_sendbuf(FAR struct uart_dev_s *dev,
                              FAR const void *buf, size_t len)
{
  FAR struct myuart_priv_s *priv = dev->priv;
  FAR const char *p = (FAR const char *)buf;
  size_t count = 0;

  while (count < len && myuart_txready_hw(priv))
    {
      putreg32((uint32_t)p[count++], priv->base + MYUART_DR_OFFSET);
    }

  return count > 0 ? (ssize_t)count : -EAGAIN;
}
```

> [!NOTE] recvbuf/sendbuf 与 receive/send 的关系
>
> - 如果 ops 中设置了 `recvbuf`，`uart_recvchars()` 优先调用它
> - 如果 ops 中设置了 `sendbuf`，`uart_xmitchars()` 优先调用它
> - 可以同时提供 `receive`/`send`（作为 fallback）或只提供一种

### 模式 3：DMA 模式

详见 [九、DMA 集成](#九dma-集成)。

---

## 六、Console 与 Early Serial

### syslog 输出的两条路径

NuttX syslog 系统根据运行阶段选择不同的输出路径：

**路径一：正常运行时（通过 /dev/console 设备）**
```
syslog(priority, fmt, ...)
  └─ nx_vsyslog() → syslog_write() → syslog_dev_write()
       └─ file_write(&sl_file, buffer, buflen)  // 写入 /dev/console
            └─ uart_write()                     // serial.c 上半区
                 ├─ uart_putxmitchar()          // 放入 TX 环形缓冲区
                 └─ uart_enabletxint(dev)       // 使能 TX 中断触发发送
                      └─ uart_xmitchars()       // ISR 中逐字节调用 ops->send()
```

**路径二：早期启动 / 中断上下文 / panic（polling 直写硬件）**
```
syslog(priority, fmt, ...)
  └─ nx_vsyslog() → ... → up_putc(ch)          // 直接调用 lower-half 提供的函数
                              └─ 等待 TX FIFO 空 → 写数据寄存器 → 等待发送完成
```

> [!IMPORTANT] up_putc 的重要性
>
> `up_putc()` 是系统最底层的字符输出函数，在以下场景中是**唯一的输出通道**：
>
> - **earlyserialinit() 之前**：系统启动最早期，中断和调度器都未就绪
> - **中断上下文中**：无法使用基于信号量/缓冲区的正常 write 路径
> - **panic / assert / hardfault**：系统已崩溃，只能用 polling 方式输出
> - **调度器未启动时的 syslog**：如 `board_early_initialize()` 阶段
>
> 如果 `up_putc()` 不正确或未实现，上述场景中将**完全没有调试输出**。

### 初始化时序

```
board_early_initialize()
    └── <chip>_earlyserialinit()    ← setup() console，不 attach 中断
          └── CONSOLE_DEV.isconsole = true
          └── uart_setup(&CONSOLE_DEV)

board_late_initialize() / nx_start()
    └── <chip>_serialinit()         ← uart_register() 所有端口
          └── uart_register("/dev/console", &CONSOLE_DEV)
          └── uart_register("/dev/ttyS0", &TTYS0_DEV)
          └── uart_register("/dev/ttyS1", &TTYS1_DEV)
          ...
```

### up_putc / up_lowputc — Polling 输出实现

`up_putc()` 必须是**纯 polling** 的、**不依赖中断**、**不依赖信号量**的独立实现。它直接操作硬件寄存器完成字符发送。

实现要求：
1. 等待 TX FIFO 有空间（busy-wait）
2. 写入数据寄存器
3. 等待数据完全发送出去（确保物理输出）
4. **不能**调用任何可能阻塞或依赖调度器的函数

```c
#ifdef HAVE_MYUART_CONSOLE
void up_putc(int ch)
{
  FAR struct myuart_priv_s *priv = CONSOLE_DEV.priv;

  /* 等待 TX FIFO 有空间（polling） */

  while (!(myuart_getreg(priv, MYUART_SR) & MYUART_SR_TXE));

  /* 写入数据 */

  myuart_putreg(priv, MYUART_DR, (uint32_t)ch);

  /* 等待发送完成（确保字符物理输出）
   * 对于 FIFO UART，要等到 FIFO 完全空 + 移位寄存器空
   */

  while (!(myuart_getreg(priv, MYUART_SR) & MYUART_SR_TXFE));
}

/* up_lowputc 通常实现相同，CONFIG_ARCH_LOWPUTC 使能时使用 */

#ifdef CONFIG_ARCH_LOWPUTC
void up_lowputc(int ch)
{
  up_putc(ch);
}
#endif
#endif /* HAVE_MYUART_CONSOLE */
```

> [!WARNING] up_putc 常见错误
>
> | 错误 | 后果 |
> |------|------|
> | 使用中断/DMA 发送 | panic 时无输出（中断已关闭） |
> | 没等 TXFE 就返回 | 后续字符可能覆盖前一个，输出乱码 |
> | 使用了 spinlock/信号量 | 早期启动阶段死锁 |
> | 波特率配置与 earlyserialinit 不一致 | 输出乱码 |
> | 没有处理 '\n' → '\r\n' 转换 | 终端显示不换行（视 termios 配置而定，通常上层处理） |

### earlyserialinit / serialinit 导出

```c
/* 公共头文件中声明 */
void myuart_earlyserialinit(void);
void myuart_serialinit(void);
```

---

## 七、TERMIOS 支持

当 `CONFIG_SERIAL_TERMIOS` 使能时，上半区在 `ioctl()` 中处理 `TCGETS`/`TCSETS` 命令，但实际波特率/格式修改由 lower-half 的 `ioctl()` 完成。

### ioctl 中处理 TCGETS/TCSETS

> [!IMPORTANT] Upper-half 与 Lower-half 的职责划分
>
> 上半区（serial.c）的 `uart_ioctl()` 执行顺序：
> 1. **先调用** lower-half 的 `ops->ioctl(filep, cmd, arg)`
> 2. 如果返回 `-ENOTTY`，上半区自行处理 `FIONREAD`/`TCFLSH`/`TCDRN` 等
> 3. 最后，**无论** lower-half 返回 OK 还是 -ENOTTY，上半区都会对 TCGETS/TCSETS **追加** `c_iflag`/`c_oflag`/`c_lflag`/`c_cc` 等 line discipline 相关字段
>
> 因此 lower-half 的 ioctl **只负责硬件相关配置**：
> - TCGETS: 只填 `c_cflag`（数据位/停止位/奇偶/流控）和波特率
> - TCSETS: 只解析 `c_cflag` 和波特率，应用到硬件寄存器
> - **不要** 处理 `c_iflag`/`c_oflag`/`c_lflag`（上半区自动管理）

```c
static int myuart_ioctl(FAR struct file *filep, int cmd, unsigned long arg)
{
  FAR struct inode *inode = filep->f_inode;
  FAR struct uart_dev_s *dev = inode->i_private;
  FAR struct myuart_priv_s *priv = dev->priv;
  int ret = OK;

  switch (cmd)
    {
#ifdef CONFIG_SERIAL_TERMIOS
      case TCGETS:
        {
          FAR struct termios *termiosp = (FAR struct termios *)arg;
          if (!termiosp)
            {
              ret = -EINVAL;
              break;
            }

          /* Lower-half 只填 c_cflag 和波特率 */

          termiosp->c_cflag =
            ((priv->parity != 0) ? PARENB : 0) |
            ((priv->parity == 1) ? PARODD : 0) |
            ((priv->stopbits2) ? CSTOPB : 0) |
#ifdef CONFIG_SERIAL_OFLOWCONTROL
            ((priv->oflow) ? CCTS_OFLOW : 0) |
#endif
#ifdef CONFIG_SERIAL_IFLOWCONTROL
            ((priv->iflow) ? CRTS_IFLOW : 0) |
#endif
            CS8;

          cfsetispeed(termiosp, priv->baud);
          /* 注意：不要填 c_iflag/c_oflag/c_lflag，上半区会追加 */
        }
        break;

      case TCSETS:
        {
          FAR struct termios *termiosp = (FAR struct termios *)arg;
          if (!termiosp)
            {
              ret = -EINVAL;
              break;
            }

          /* 可选：sanity check，拒绝不支持的配置 */

          if ((termiosp->c_cflag & CSIZE) != CS8)
            {
              ret = -EINVAL;
              break;
            }

          /* 解析 c_cflag → 更新 priv 中的硬件配置参数 */

          if (termiosp->c_cflag & PARENB)
            {
              priv->parity = (termiosp->c_cflag & PARODD) ? 1 : 2;
            }
          else
            {
              priv->parity = 0;
            }

          priv->stopbits2 = (termiosp->c_cflag & CSTOPB) != 0;

#ifdef CONFIG_SERIAL_OFLOWCONTROL
          priv->oflow = (termiosp->c_cflag & CCTS_OFLOW) != 0;
#endif
#ifdef CONFIG_SERIAL_IFLOWCONTROL
          priv->iflow = (termiosp->c_cflag & CRTS_IFLOW) != 0;
#endif

          /* 解析波特率 */

          priv->baud = cfgetispeed(termiosp);

          /* 立即应用到硬件（不实现 TCSADRAIN/TCSAFLUSH） */

          myuart_set_format(dev);
        }
        break;
#endif /* CONFIG_SERIAL_TERMIOS */

      case TIOCSBRK:  /* Turn break on */
      case TIOCCBRK:  /* Turn break off */
      default:
        ret = -ENOTTY;
        break;
    }

  return ret;
}
```

---

## 八、流控支持

### 硬件流控（RTS/CTS）

当 `CONFIG_SERIAL_IFLOWCONTROL` 使能时，实现 `rxflowcontrol` 回调：

```c
#ifdef CONFIG_SERIAL_IFLOWCONTROL
static bool myuart_rxflowcontrol(FAR struct uart_dev_s *dev,
                                 unsigned int nbuffered, bool upper)
{
  FAR struct myuart_priv_s *priv = dev->priv;

  if (upper)
    {
      /* 缓冲区水位到达上限，拉高 RTS（阻止对方发送） */
      myuart_rts_deassert(priv->base);

      /* 关闭 RX 中断，阻止更多数据进入 */
      myuart_rxint_disable(priv->base);
      return true;  /* true = 流控已激活 */
    }
  else
    {
      /* 缓冲区水位降到下限，拉低 RTS（允许对方发送） */
      myuart_rts_assert(priv->base);

      /* 重新使能 RX 中断 */
      myuart_rxint_enable(priv->base);
      return false;
    }
}
#endif
```

---

## 九、DMA 集成

### TX DMA 完整调用链

```
上半区 write() 将数据写入 xmit 缓冲区
    │
    ▼
上半区调用 ops->dmatxavail(dev)  ← Lower-half 回调
    │
    ▼
Lower-half dmatxavail(): 检查 DMA 是否空闲
    │  如果 DMA 空闲:
    ▼
Lower-half 调用 uart_xmitchars_dma(dev)  ← 上半区辅助函数
    │  设置 dev->dmatx (buffer/length/nbuffer/nlength)
    ▼
uart_xmitchars_dma() 内部调用 ops->dmasend(dev)  ← Lower-half 回调
    │
    ▼
Lower-half dmasend(): 启动硬件 DMA 传输
    │
    ▼
DMA 完成中断
    │
    ▼
Lower-half 调用 uart_xmitchars_done(dev)  ← 上半区辅助函数
    │  更新 xmit.tail，唤醒阻塞的 write()
    ▼
(如果 xmit 缓冲区仍有数据，循环继续)
```

**关键点**：
- `uart_xmitchars_dma()` 由 **lower-half** 调用（不是上半区直接调用），常见调用位置有：
  - `dmatxavail()` 回调：上半区 write() 后通知有新数据，lower-half 检查 DMA 空闲后调用
  - DMA 完成回调：前一次传输完成后，检查是否还有剩余数据继续链式发送
  - `attach()` / 初始化：首次打开端口时启动发送循环
- `ops->dmasend()` 是由 `uart_xmitchars_dma()` 内部调用的（即上半区辅助函数回调 lower-half）
- Lower-half 需要实现两个回调：`dmatxavail`（判断能否启动 DMA）和 `dmasend`（真正启动 DMA）

### TX DMA 实现

```c
#ifdef CONFIG_SERIAL_TXDMA
static void myuart_dmatxavail(FAR struct uart_dev_s *dev)
{
  FAR struct myuart_priv_s *priv = dev->priv;

  /* 仅在 DMA 空闲时启动新传输 */

  if (myuart_dma_is_idle(priv->txdma))
    {
      uart_xmitchars_dma(dev);
    }
}

static void myuart_dmasend(FAR struct uart_dev_s *dev)
{
  FAR struct myuart_priv_s *priv = dev->priv;
  FAR struct uart_dmaxfer_s *xfer = &dev->dmatx;

  /* 启动 DMA: 从 xfer->buffer 发送 xfer->length 字节 */
  /* 注意：如果 xfer->nlength > 0，表示数据跨越环形缓冲区边界，
   * 简单实现可以先只发 xfer->length 部分，在完成回调中再发 nlength 部分；
   * 或者用 scatter-gather DMA 一次性发两段。
   */

  myuart_dma_start_tx(priv->txdma, xfer->buffer, xfer->length);
}

/* DMA TX 完成回调（从 DMA ISR 调用） */

static void myuart_dma_tx_callback(DMA_HANDLE handle, void *arg, int result)
{
  FAR struct uart_dev_s *dev = (FAR struct uart_dev_s *)arg;

  /* 报告已传输字节数 */

  dev->dmatx.nbytes = dev->dmatx.length;
  uart_xmitchars_done(dev);

  /* 链式发送：如果 xmit 缓冲区还有数据，继续启动下一次 DMA */

  uart_xmitchars_dma(dev);
}
#endif
```

### RX DMA 完整调用链

NuttX serial 框架提供了两种 RX DMA 模式，lower-half 可以选择其中一种：

#### 模式 A：循环 DMA（STM32 等 SoC 内置 UART 常用）

Lower-half 在 `setup()` 中自行配置循环 DMA 到私有 FIFO 缓冲区，上半区 `uart_recvchars_dma()` / `ops->dmareceive` 机制不参与。

```
setup() 中:
  配置循环 DMA → 硬件持续接收到 priv->rxfifo
  启动 DMA（DMA_SCR_CIRC 模式，半满+完成回调）

DMA 半满/完成回调:
  Lower-half 计算新到达的字节数
  复制到上半区 recv 缓冲区（或通过 up_dma_receive ops 让上半区逐字节取）
  调用 uart_datareceived(dev) 唤醒读者
```

这种模式下 ops 中 **不设** `dmareceive`/`dmarxfree`，而是用自定义的 `receive`/`rxavailable` 从 DMA 循环缓冲区读数据。参考：STM32F7 `up_dma_setup()`。

#### 模式 B：按需 DMA（uart_rpmsg、16550、hostfs、虚拟串口常用）

使用上半区 `serial_dma.c` 提供的 `uart_recvchars_dma()` / `uart_recvchars_done()` 机制。

```
上半区 read() 消费数据后:
    调用 ops->dmarxfree(dev)  ← 通知 lower-half "recv 缓冲区有空间了"
        │
        ▼
Lower-half dmarxfree():
    调用 uart_recvchars_dma(dev)  ← 上半区辅助函数
        │  计算 recv 缓冲区空闲空间，设置 dev->dmarx (buffer/length)
        ▼
    uart_recvchars_dma() 内部调用 ops->dmareceive(dev)  ← Lower-half 回调
        │
        ▼
Lower-half dmareceive(): 启动硬件 DMA 接收
        │
        ▼
数据到达（DMA 完成、IDLE 中断、或 lower-half 轮询检测到数据）:
    Lower-half 设置 dev->dmarx.nbytes = 实际接收字节数
    调用 uart_recvchars_done(dev)  ← 上半区辅助函数
        │  更新 recv.head，唤醒阻塞的 read()
        │  注意：uart_recvchars_done() 不会自动重启 DMA
        ▼
下一次 read() 消费数据 → 上半区再次调用 ops->dmarxfree() → 循环继续
```

**关键点**：
- `uart_recvchars_done()` **不会**自动调用 `dmareceive` 重启 DMA（与 TX 侧不同）
- RX DMA 的重新启动依赖于 `dmarxfree()` 被上半区调用（当 read() 消费了数据后）
- Lower-half 的 `dmarxfree()` 回调中通常调用 `uart_recvchars_dma(dev)` 来重启接收
- RX DMA 的**首次启动**位置由 lower-half 自行决定：可以在 `setup()`、`attach()`、`rxint(enable=true)` 或任何合适的时机调用 `uart_recvchars_dma(dev)`
- 没有强制要求必须在某个特定回调中启动

### RX DMA 实现（模式 B：按需 DMA）

```c
#ifdef CONFIG_SERIAL_RXDMA

/* 首次启动 RX DMA —— 可以放在 attach()、rxint(enable=true) 或 setup() 中，
 * 由 lower-half 根据硬件特性自行决定。
 * 核心：确保在上半区第一次调用 dmarxfree() 之前 DMA 已就绪即可。
 * 最简单的做法：在 dmarxfree() 中统一处理（无论首次还是后续）。
 */

static void myuart_dmareceive(FAR struct uart_dev_s *dev)
{
  FAR struct myuart_priv_s *priv = dev->priv;
  FAR struct uart_dmaxfer_s *xfer = &dev->dmarx;

  /* 启动 DMA 接收到 xfer->buffer，长度 xfer->length
   * 注意：buffer 和 length 由上半区 uart_recvchars_dma() 设置好
   */

  myuart_dma_start_rx(priv->rxdma, xfer->buffer, xfer->length);
}

static void myuart_dmarxfree(FAR struct uart_dev_s *dev)
{
  /* 上半区在 read() 消费数据后调用此回调，表示 recv 缓冲区有空间了。
   * lower-half 在此重启 RX DMA 接收。
   */

  FAR struct myuart_priv_s *priv = dev->priv;

  if (myuart_dma_is_idle(priv->rxdma))
    {
      uart_recvchars_dma(dev);  /* → 设置 dmarx → 调用 ops->dmareceive() */
    }
}

/* DMA RX 完成/IDLE 回调（从 DMA ISR 或轮询中调用） */

static void myuart_dma_rx_callback(DMA_HANDLE handle, void *arg, int result)
{
  FAR struct uart_dev_s *dev = (FAR struct uart_dev_s *)arg;
  FAR struct myuart_priv_s *priv = dev->priv;

  /* 报告实际接收的字节数 */

  dev->dmarx.nbytes = dev->dmarx.length - myuart_dma_residual(priv->rxdma);

  /* 通知上半区搬数据到 recv 缓冲区、唤醒 read()
   * 注意：uart_recvchars_done() 不会自动重启 DMA，
   * 重启在下一次 read() 消费数据后触发 dmarxfree() 时发生。
   */

  uart_recvchars_done(dev);
}
#endif
```

### TX DMA ops 表

```c
static const struct uart_ops_s g_myuart_dma_ops =
{
  .setup       = myuart_setup,
  .shutdown    = myuart_shutdown,
  .attach      = myuart_attach,
  .detach      = myuart_detach,
  .ioctl       = myuart_ioctl,
  .receive     = myuart_receive,
  .rxint       = myuart_rxint,
  .rxavailable = myuart_rxavailable,
  .send        = myuart_send,
  .txint       = myuart_txint,       /* DMA 模式下可为空操作 */
  .txready     = myuart_txready,
  .txempty     = myuart_txempty,
#ifdef CONFIG_SERIAL_TXDMA
  .dmatxavail  = myuart_dmatxavail,
  .dmasend     = myuart_dmasend,
#endif
#ifdef CONFIG_SERIAL_RXDMA
  .dmareceive  = myuart_dmareceive,
  .dmarxfree   = myuart_dmarxfree,
#endif
};
```

---

## 十、完整实现模板

以下是一个生产级 serial lower-half 骨架，可直接复制修改。

```c
/****************************************************************************
 * arch/<arch>/src/<chip>/<chip>_serial.c
 ****************************************************************************/

#include <nuttx/config.h>
#include <nuttx/irq.h>
#include <nuttx/arch.h>
#include <nuttx/serial/serial.h>
#include <nuttx/spinlock.h>

#ifdef CONFIG_SERIAL_TERMIOS
#include <termios.h>
#endif

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define MYUART_NPORTS  2

/* Register offsets - 根据你的硬件填写 */
#define MYUART_DR      0x00   /* Data register */
#define MYUART_SR      0x04   /* Status register */
#define MYUART_CR      0x08   /* Control register */
#define MYUART_BRR     0x0C   /* Baud rate register */
#define MYUART_IER     0x10   /* Interrupt enable register */
#define MYUART_ISR     0x14   /* Interrupt status register */
#define MYUART_ICR     0x18   /* Interrupt clear register */

/* Status register bits */
#define MYUART_SR_RXNE   (1 << 0)  /* RX not empty */
#define MYUART_SR_TXE    (1 << 1)  /* TX empty (FIFO has space) */
#define MYUART_SR_TXFE   (1 << 2)  /* TX FIFO fully empty */

/* Interrupt bits */
#define MYUART_IER_RXNE  (1 << 0)
#define MYUART_IER_TXE   (1 << 1)

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct myuart_priv_s
{
  uint32_t     base;       /* Register base address */
  uint32_t     clk_freq;   /* Input clock frequency */
  uint32_t     baud;       /* Current baud rate */
  int          irq;        /* IRQ number */
  uint8_t      parity;     /* 0=none, 1=odd, 2=even */
  uint8_t      bits;       /* Data bits: 5-9 */
  bool         stopbits2;  /* true = 2 stop bits */
  spinlock_t   lock;       /* Register access lock */
};

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static int  myuart_setup(FAR struct uart_dev_s *dev);
static void myuart_shutdown(FAR struct uart_dev_s *dev);
static int  myuart_attach(FAR struct uart_dev_s *dev);
static void myuart_detach(FAR struct uart_dev_s *dev);
static int  myuart_ioctl(FAR struct file *filep, int cmd, unsigned long arg);
static int  myuart_receive(FAR struct uart_dev_s *dev, FAR unsigned int *status);
static void myuart_rxint(FAR struct uart_dev_s *dev, bool enable);
static bool myuart_rxavailable(FAR struct uart_dev_s *dev);
static void myuart_send(FAR struct uart_dev_s *dev, int ch);
static void myuart_txint(FAR struct uart_dev_s *dev, bool enable);
static bool myuart_txready(FAR struct uart_dev_s *dev);
static bool myuart_txempty(FAR struct uart_dev_s *dev);

/****************************************************************************
 * Private Data
 ****************************************************************************/

static const struct uart_ops_s g_myuart_ops =
{
  .setup       = myuart_setup,
  .shutdown    = myuart_shutdown,
  .attach      = myuart_attach,
  .detach      = myuart_detach,
  .ioctl       = myuart_ioctl,
  .receive     = myuart_receive,
  .rxint       = myuart_rxint,
  .rxavailable = myuart_rxavailable,
#ifdef CONFIG_SERIAL_IFLOWCONTROL
  .rxflowcontrol = NULL,
#endif
  .send        = myuart_send,
  .txint       = myuart_txint,
  .txready     = myuart_txready,
  .txempty     = myuart_txempty,
};

/* --- UART0 --- */

#ifdef CONFIG_MYUART_UART0
static char g_uart0_rxbuf[CONFIG_UART0_RXBUFSIZE];
static char g_uart0_txbuf[CONFIG_UART0_TXBUFSIZE];

static struct myuart_priv_s g_uart0_priv =
{
  .base     = CONFIG_MYUART0_BASE,
  .clk_freq = CONFIG_MYUART0_CLK_FREQ,
  .baud     = CONFIG_UART0_BAUD,
  .irq      = CONFIG_MYUART0_IRQ,
  .parity   = 0,
  .bits     = 8,
  .stopbits2 = false,
  .lock     = SP_UNLOCKED,
};

static struct uart_dev_s g_uart0_dev =
{
  .recv =
    {
      .size   = CONFIG_UART0_RXBUFSIZE,
      .buffer = g_uart0_rxbuf,
    },
  .xmit =
    {
      .size   = CONFIG_UART0_TXBUFSIZE,
      .buffer = g_uart0_txbuf,
    },
  .ops  = &g_myuart_ops,
  .priv = &g_uart0_priv,
};
#endif

/* --- UART1 --- */

#ifdef CONFIG_MYUART_UART1
static char g_uart1_rxbuf[CONFIG_UART1_RXBUFSIZE];
static char g_uart1_txbuf[CONFIG_UART1_TXBUFSIZE];

static struct myuart_priv_s g_uart1_priv =
{
  .base     = CONFIG_MYUART1_BASE,
  .clk_freq = CONFIG_MYUART1_CLK_FREQ,
  .baud     = CONFIG_UART1_BAUD,
  .irq      = CONFIG_MYUART1_IRQ,
  .parity   = 0,
  .bits     = 8,
  .stopbits2 = false,
  .lock     = SP_UNLOCKED,
};

static struct uart_dev_s g_uart1_dev =
{
  .recv =
    {
      .size   = CONFIG_UART1_RXBUFSIZE,
      .buffer = g_uart1_rxbuf,
    },
  .xmit =
    {
      .size   = CONFIG_UART1_TXBUFSIZE,
      .buffer = g_uart1_txbuf,
    },
  .ops  = &g_myuart_ops,
  .priv = &g_uart1_priv,
};
#endif

/* Console selection */

#if defined(CONFIG_UART0_SERIAL_CONSOLE)
#  define CONSOLE_DEV  g_uart0_dev
#elif defined(CONFIG_UART1_SERIAL_CONSOLE)
#  define CONSOLE_DEV  g_uart1_dev
#endif

#ifdef CONFIG_MYUART_UART0
#  define TTYS0_DEV    g_uart0_dev
#endif
#ifdef CONFIG_MYUART_UART1
#  define TTYS1_DEV    g_uart1_dev
#endif

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static inline uint32_t myuart_getreg(FAR struct myuart_priv_s *priv,
                                     uint32_t offset)
{
  return getreg32(priv->base + offset);
}

static inline void myuart_putreg(FAR struct myuart_priv_s *priv,
                                 uint32_t offset, uint32_t value)
{
  putreg32(value, priv->base + offset);
}

static int myuart_setup(FAR struct uart_dev_s *dev)
{
  FAR struct myuart_priv_s *priv = dev->priv;

  /* TODO: 使能时钟、配置 GPIO、设置波特率、配置格式、使能 UART */
  /* myuart_clock_enable(priv->base); */
  /* myuart_set_baudrate(priv->base, priv->clk_freq, priv->baud); */
  /* myuart_set_format(priv->base, priv->bits, priv->parity, priv->stopbits2); */
  /* myuart_enable(priv->base); */

  return OK;
}

static void myuart_shutdown(FAR struct uart_dev_s *dev)
{
  FAR struct myuart_priv_s *priv = dev->priv;

  /* TODO: 禁用 UART、关闭时钟 */
  /* myuart_disable(priv->base); */
  /* myuart_clock_disable(priv->base); */

  UNUSED(priv);
}

static int myuart_attach(FAR struct uart_dev_s *dev)
{
  FAR struct myuart_priv_s *priv = dev->priv;
  int ret;

  ret = irq_attach(priv->irq, myuart_interrupt, dev);
  if (ret == OK)
    {
      up_enable_irq(priv->irq);
    }

  return ret;
}

static void myuart_detach(FAR struct uart_dev_s *dev)
{
  FAR struct myuart_priv_s *priv = dev->priv;

  up_disable_irq(priv->irq);
  irq_detach(priv->irq);
}

static int myuart_ioctl(FAR struct file *filep, int cmd, unsigned long arg)
{
#ifdef CONFIG_SERIAL_TERMIOS
  FAR struct inode *inode = filep->f_inode;
  FAR struct uart_dev_s *dev = inode->i_private;
  FAR struct myuart_priv_s *priv = dev->priv;

  switch (cmd)
    {
      case TCGETS:
        {
          FAR struct termios *termiosp = (FAR struct termios *)arg;
          if (!termiosp)
            return -EINVAL;

          /* Only fill c_cflag and baud rate (upper-half handles the rest) */

          termiosp->c_cflag =
            ((priv->parity != 0) ? PARENB : 0) |
            ((priv->parity == 1) ? PARODD : 0) |
            ((priv->stopbits2) ? CSTOPB : 0) |
            CS8;

          cfsetispeed(termiosp, priv->baud);
        }
        break;

      case TCSETS:
        {
          FAR struct termios *termiosp = (FAR struct termios *)arg;
          if (!termiosp)
            return -EINVAL;

          if ((termiosp->c_cflag & CSIZE) != CS8)
            return -EINVAL;

          /* Parse c_cflag */

          if (termiosp->c_cflag & PARENB)
            priv->parity = (termiosp->c_cflag & PARODD) ? 1 : 2;
          else
            priv->parity = 0;

          priv->stopbits2 = (termiosp->c_cflag & CSTOPB) != 0;
          priv->baud = cfgetispeed(termiosp);

          /* Apply to hardware */

          /* TODO: myuart_set_format(dev); */
        }
        break;

      default:
        return -ENOTTY;
    }

  return OK;
#else
  return -ENOTTY;
#endif
}

static int myuart_receive(FAR struct uart_dev_s *dev, FAR unsigned int *status)
{
  FAR struct myuart_priv_s *priv = dev->priv;

  *status = myuart_getreg(priv, MYUART_SR);
  return (int)(myuart_getreg(priv, MYUART_DR) & 0xFF);
}

static void myuart_rxint(FAR struct uart_dev_s *dev, bool enable)
{
  FAR struct myuart_priv_s *priv = dev->priv;
  irqstate_t flags = spin_lock_irqsave(&priv->lock);
  uint32_t ier = myuart_getreg(priv, MYUART_IER);

  if (enable)
    {
      ier |= MYUART_IER_RXNE;
    }
  else
    {
      ier &= ~MYUART_IER_RXNE;
    }

  myuart_putreg(priv, MYUART_IER, ier);
  spin_unlock_irqrestore(&priv->lock, flags);
}

static bool myuart_rxavailable(FAR struct uart_dev_s *dev)
{
  FAR struct myuart_priv_s *priv = dev->priv;
  return (myuart_getreg(priv, MYUART_SR) & MYUART_SR_RXNE) != 0;
}

static void myuart_send(FAR struct uart_dev_s *dev, int ch)
{
  FAR struct myuart_priv_s *priv = dev->priv;
  myuart_putreg(priv, MYUART_DR, (uint32_t)ch);
}

static void myuart_txint(FAR struct uart_dev_s *dev, bool enable)
{
  FAR struct myuart_priv_s *priv = dev->priv;
  irqstate_t flags = spin_lock_irqsave(&priv->lock);
  uint32_t ier = myuart_getreg(priv, MYUART_IER);

  if (enable)
    {
      ier |= MYUART_IER_TXE;
      myuart_putreg(priv, MYUART_IER, ier);

      /* Kick-start TX: 填充 FIFO */
      uart_xmitchars(dev);
    }
  else
    {
      ier &= ~MYUART_IER_TXE;
      myuart_putreg(priv, MYUART_IER, ier);
    }

  spin_unlock_irqrestore(&priv->lock, flags);
}

static bool myuart_txready(FAR struct uart_dev_s *dev)
{
  FAR struct myuart_priv_s *priv = dev->priv;
  return (myuart_getreg(priv, MYUART_SR) & MYUART_SR_TXE) != 0;
}

static bool myuart_txempty(FAR struct uart_dev_s *dev)
{
  FAR struct myuart_priv_s *priv = dev->priv;
  return (myuart_getreg(priv, MYUART_SR) & MYUART_SR_TXFE) != 0;
}

static int myuart_interrupt(int irq, FAR void *context, FAR void *arg)
{
  FAR struct uart_dev_s *dev = (FAR struct uart_dev_s *)arg;
  FAR struct myuart_priv_s *priv = dev->priv;
  uint32_t status = myuart_getreg(priv, MYUART_ISR);

  if (status & MYUART_IER_RXNE)
    {
      uart_recvchars(dev);
    }

  if (status & MYUART_IER_TXE)
    {
      uart_xmitchars(dev);
    }

  /* Clear interrupts */
  myuart_putreg(priv, MYUART_ICR, status);

  return OK;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

void myuart_earlyserialinit(void)
{
#ifdef CONSOLE_DEV
  CONSOLE_DEV.isconsole = true;
  myuart_setup(&CONSOLE_DEV);
#endif
}

void myuart_serialinit(void)
{
#ifdef CONSOLE_DEV
  uart_register("/dev/console", &CONSOLE_DEV);
#endif
#ifdef TTYS0_DEV
  uart_register("/dev/ttyS0", &TTYS0_DEV);
#endif
#ifdef TTYS1_DEV
  uart_register("/dev/ttyS1", &TTYS1_DEV);
#endif
}

#ifdef HAVE_MYUART_CONSOLE
void up_putc(int ch)
{
  FAR struct myuart_priv_s *priv = CONSOLE_DEV.priv;

  /* Wait for TX ready (polling) */
  while (!(myuart_getreg(priv, MYUART_SR) & MYUART_SR_TXE));

  myuart_putreg(priv, MYUART_DR, (uint32_t)ch);

  /* Wait for TX complete */
  while (!(myuart_getreg(priv, MYUART_SR) & MYUART_SR_TXFE));
}
#endif
```

---

## 十一、常见问题与陷阱

### 1. txint(enable=true) 不触发首次发送

**症状**: `write()` 数据写入缓冲区后无输出。

**原因**: 使能 TX 中断时 FIFO 已经空了，不会再产生 TX empty 中断。

**修复**: 在 `txint(dev, true)` 中调用 `uart_xmitchars(dev)` 启动首次传输。

### 2. Console 早期输出乱码

**症状**: 启动早期 `syslog` 输出乱码。

**原因**: `earlyserialinit()` 中未正确配置波特率或时钟频率参数错误。

**检查**: 确认 `priv->clk_freq` 与硬件实际时钟频率一致。

### 3. RX 数据丢失

**症状**: 高波特率下接收数据不完整。

**原因**:
- `rxavailable()` 判断条件不准确
- ISR 中处理太慢（建议用 `recvbuf` 批量收取）
- 环形缓冲区太小（增大 `CONFIG_UART*_RXBUFSIZE`）

### 4. shutdown 不应清理 console

**规则**: `shutdown()` 中需判断 `dev->isconsole`，如果是 console 则**不要**真正关闭硬件。或者依赖上半区保证不对 console 调用 shutdown（事实如此）。

### 5. spinlock 使用

**规则**: `rxint()`/`txint()` 修改中断使能寄存器时必须用 `spin_lock_irqsave()`，因为 ISR 也可能同时读写这些寄存器。

### 6. DMA 缓冲区对齐

使用 DMA 时，缓冲区必须按 D-Cache line size 对齐（通常 32 或 64 字节）。否则 cache invalidate 会破坏相邻数据。

### 7. uart_register 路径命名

- Console: `"/dev/console"`
- TTY 设备: `"/dev/ttyS0"`, `"/dev/ttyS1"`, ...
- 一个物理端口可以同时注册为 console 和 ttyS*（共用同一个 `uart_dev_s`）

### 8. Panic 路径必须独立于正常驱动栈

**症状**: 触发 panic（如 Ctrl+?）后，crashlog 无输出或只输出少量内容后卡死。

**原因**: `send()` 在 panic 时仍走异步 TX 路径（依赖中断/DMA/回调完成传输），
但 panic 时中断已禁用，TX 永远完不成。

**规则**:
- panic 状态时走 `up_putc()` polling, 不能直接调用 `send()`

### 9. clearAllFlags 破坏 RX 状态

**症状**: lowputc 正常工作后，正常模式下 RX 接收断了（streaming 不触发回调）。

**原因**: lowputc 中使用 `clearAllFlags()` 往 FLAGSCLEAR 写 0xFFFFFFFF，
把 RX 相关的标志也清掉了，破坏了 MCAL/驱动 RX 状态机。

**规则**: Polling TX 路径**只清 TX 相关标志位**，不碰 RX/ERR 标志：

```c
/* ✅ 只清 TX 标志 */
clear_tx_fifo_flag(base);
clear_tx_complete_flag(base);

/* ❌ 不要用 clear_all */
// clear_all_flags(base);
```

### 10. 中断处理方式（ISR_DIRECT vs ISR_WQUEUE）影响 Panic 流程

**症状**: CPU0 panic 后 CPU1 crashlog 只输出几行就被整芯片复位打断。

**原因**: UART IRQ 使用 work queue 方式处理时，CPU1 的 `reboot_notifier_call_chain`
触发了 CPU0 的 IRQ work，CPU0 二次进入 assert 后直接复位。

**触发链**:
```
Ctrl+? → CPU0 wq irq → CPU0 assert → CPU1 assert
       → CPU1 reboot_notifier → 触发 CPU0 中断 work
       → CPU0 二次 assert → 整芯片复位
```

**规则**:
- 涉及 console/panic 路径的 UART，优先使用 `ISR_DIRECT` 而非 `ISR_WQUEUE`
- 如果必须用 wqueue，需要在 panic 路径中屏蔽 wqueue 调度

---

## 十二、Cross References

| Reference | Description |
|-----------|-------------|
| `nuttx/include/nuttx/serial/serial.h` | 框架头文件：所有数据结构和 API 声明 |
| `nuttx/drivers/serial/serial.c` | 上半区：VFS 接口实现 |
| `nuttx/drivers/serial/serial_io.c` | 上半区：uart_xmitchars / uart_recvchars |
| `nuttx/drivers/serial/serial_dma.c` | 上半区：DMA 辅助函数 |
| `nuttx/drivers/serial/uart_pl011.c` | 参考实现：PL011（最简洁的骨架参考） |
| `nuttx/arch/arm/src/stm32f7/stm32_serial.c` | 参考实现：STM32F7（最完整，含 DMA/RS485） |
| `nuttx/drivers/serial/uart_16550.c` | 参考实现：16550 兼容（经典通用 IP） |
| `references/coding_rules.md` | NuttX 编码规范和内核 API 使用规则 |
| `references/mcal_autosar_pattern.md` | MCAL UART 适配模式（当 lower-half 对接 AUTOSAR MCAL 时） |
