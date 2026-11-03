### 架构与分层（nuttx）

分层约定：`drivers/` 只使用 NuttX 公共 API 和公共的 arch 接口，不访问板级内部实现或其他驱动的私有实现；`arch/*/src/` 可以直接访问寄存器和芯片内部；`boards/` 负责板级初始化；`include/nuttx/` 是稳定的公共接口，不放私有结构、寄存器细节和实现状态。
豁免：当前层本来就是正确的层（如 `arch/` 里访问寄存器、`boards/` 里做板级初始化），或者 commit 说明了设计取舍。

#### [DP-01] 绕过抽象层（high）
上层代码直接使用下层的寄存器、私有实现或内部接口，而相应的公共抽象已经存在，如通用驱动直接访问板级私有符号，或绕过 VFS、sensor upper-half 等子系统接口。

#### [DP-02] 跨层 include（high）
新增的 include 依赖了跨层的内部头文件，如驱动 include 板级内部头文件或其他驱动的私有头文件。

#### [DP-03] 公共头文件泄露实现细节（medium）
`include/nuttx/` 等公共头文件里暴露了私有结构体、私有枚举、寄存器定义或内部辅助函数声明。

#### [DP-04] 内部符号缺少 static（medium）
只在本文件使用的函数和变量没有声明为 `static`；或者通过临时的 `extern` 声明引用其他模块的内部符号，而不是通过公共头文件。

#### [DP-05] 通用代码里写了板级或产品逻辑（high）
通用的 `drivers/`、`fs/`、`sched/` 代码里出现 `CONFIG_ARCH_BOARD_*` 等板级条件，或者写死了 GPIO、总线号、片选、寄存器地址、板级延时。应通过 board 配置、platform data、ops 回调或 Kconfig 注入。

#### [DP-06] 重复保存状态（high）
新增的状态和已有子系统的权威状态重复，如驱动自己维护 `is_suspended`、`opened_count`、`registered`，而 PM、VFS 或子系统已经在管理这些状态。

#### [DP-07] 重复造轮子（medium）
自己实现 worker 线程、定时器队列、重试循环、引用计数或链表，而 `work_queue`、`wdog`、`nxmutex`/`nxsem`、`sq`/`dq` 或子系统基础设施已经能满足需求。

#### [DP-08] 绕过标准注册或回调路径（high）
绕过 `file_operations`、`sensor_ops_s`、`spi_ops_s`、`i2c_ops_s`、`register_driver` 等标准 ops 和注册机制，直接调用具体实现或用 `extern` 调用其他驱动的内部函数。
