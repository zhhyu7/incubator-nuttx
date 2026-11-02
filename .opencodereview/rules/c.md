<!-- 由 vela-tool-service/ci/aicr/rules/c/build.py 生成，勿手改；修改该目录下的 src/ 后重新生成 -->

## Vela C 代码评审规则

- 评论用中文，开头写规则编号，如 `[P-01]`；API 名、变量名保持原文。
- 只报有证据的问题：写出文件、行号和具体的变量或调用路径。上下文不足以确认时不报，误报比漏报代价更大。
- 只评本次 diff 新增或修改的代码；已有代码里的问题不报。
- 不报：格式、缩进、命名风格、注释措辞（由 nxstyle 等工具负责）；NuttX 里正常的写法，如 `FAR`、`CODE`、`aligned_data`、`locate_data`、GCC `__attribute__`、`nitems()`。
- severity 按每条规则括号里的级别填写。同一函数里同一类问题只报一次，并列出所有出现位置。
- 标注了「适用条件」的规则，只在条件成立时检查。

### 内存安全

#### [P-01] 分配结果未判空（critical）
`kmm_malloc`/`kmm_zalloc`/`kmm_calloc`/`zalloc`/`malloc` 的返回值未判 NULL，就解引用或传给 `memcpy`/`memset`/`strcpy`。
```c
buf = kmm_malloc(size);
memcpy(buf, src, size);            /* Bad：buf 可能为 NULL */

buf = kmm_malloc(size);
if (buf == NULL)
  {
    return -ENOMEM;                /* Good */
  }
```

#### [P-02] 释放后仍可被访问（critical）
结构体在 close/stop/destroy 路径被释放，但仍有路径能访问它：挂着未取消的 work、timer、回调，或释放后同一函数继续使用该指针。work 和 timer 的具体情形见 P-21、P-22。

#### [P-03] 错误路径重复释放（critical）
错误分支里已经 `kmm_free(buf)`，又 `goto` 到统一清理标签再释放一次。只保留一个释放点。

#### [P-04] realloc 覆盖原指针（high）
`buf = kmm_realloc(buf, n)` / `p = realloc(p, n)` 直接赋回原指针：失败时原内存泄漏，指针变 NULL。应先用临时变量接收并判空。
```c
tmp = kmm_realloc(buf, newsize);
if (tmp == NULL)
  {
    kmm_free(buf);
    return -ENOMEM;
  }
buf = tmp;
```

#### [P-05] 队列节点摘下未释放（high）
`sq_remfirst`/`sq_remlast`/`dq_remfirst` 的返回值被丢弃，而节点是堆分配的，造成泄漏。

#### [P-06] 释放后未置空（medium）
`kmm_free(priv->buf)` 之后没有 `priv->buf = NULL`，后面又用 `if (priv->buf)` 判断，会解引用悬挂指针。只在后续确实还会判断或使用该字段时报。

#### [P-07] 未初始化变量被读取（high）
局部变量、栈数组或栈结构体存在未初始化就被读取的路径。常见情形：把栈结构体地址传给可能失败、不填字段的函数，调用方不检查返回值就读字段。
```c
struct sensor_event_s event;
ret = sensor_read(fd, &event, sizeof(event));
ts = event.timestamp;              /* Bad：ret < 0 时 event 未初始化 */
```

#### [P-08] sizeof 或长度参数错误（high）
`memset`/`memcpy`/`memmove`/`strncpy` 的长度和目标大小不一致，如 `sizeof(ptr)` 当作缓冲区大小、元素个数当作字节数。
```c
memset(buf, 0, sizeof(buf));       /* Bad：buf 是 char *，sizeof 只是指针大小 */
```

#### [C-01] 类型混淆（high）
同一块内存被当作不兼容的类型使用，如把 `struct a_s *` 强转为 `struct b_s *` 访问；`container_of` 的成员名或类型不对应。

#### [C-02] 指针算术越界（high）
指针加偏移后超出所属缓冲区，偏移量来自长度字段且没有和缓冲区剩余长度比较。

#### [C-03] memcpy 区域重叠（medium）
源和目标可能重叠（同一缓冲区内搬移数据）却用了 `memcpy`，应使用 `memmove`。

#### [C-04] 非对齐访问（high）
把字节缓冲区强转为 `uint32_t *`、`uint16_t *` 或结构体指针后直接读写，偏移未对齐时在部分架构上会触发 fault。应使用 `memcpy` 或逐字节组装。

### 并发与线程安全

#### [P-09] 共享变量无锁访问（high）
全局或文件级变量在多个任务可达的入口（`_open`/`_close`/`_read`/`_write`、worker 回调）中读写，但没有 `nxmutex_lock`/`nxsem_wait`/`enter_critical_section` 保护。只在能指出两条并发路径时报。

#### [P-10] ISR 中调用可睡眠或带锁的函数（critical）
从中断处理函数（`irq_attach` 注册的 handler，以及它调用的函数）可达的代码中调用了 `nxmutex_lock`、`nxsem_wait`、`kmm_malloc`、`usleep`、`sleep`、`printf`/`syslog`、`fopen`/`fread`/`fwrite`。ISR 中只能用自旋锁或临界区，耗时工作交给 `work_queue`。
```c
static int sensor_isr(int irq, FAR void *context, FAR void *arg)
{
  nxmutex_lock(&priv->lock);       /* Bad：ISR 不能睡眠 */
  ...
}
```

#### [P-11] 加锁顺序不一致（high）
同一模块内两条路径以相反顺序获取同一组锁（A→B 与 B→A），可能死锁。必须指出两条路径的位置。

#### [C-05] 持锁时再次加同一把锁（high）
持有非递归锁时，调用的函数内部又获取同一把锁，导致自死锁。

#### [P-12] nxsem_wait 返回值未处理（high）
`nxsem_wait`/`nxsem_timedwait` 的返回值被丢弃，或没有处理 `-ECANCELED`/`-EINTR`，等待失败后仍按已获得信号量继续执行。
```c
ret = nxsem_wait(&priv->sem);
if (ret < 0)
  {
    return ret;
  }
```

#### [P-13] 寄存器读改写未保护（high）
`getreg32` → 修改 → `putreg32` 的序列没有包在 `enter_critical_section`/`leave_critical_section` 中，而该寄存器也会被 ISR 或其他核写。可以使用 `modifyreg32`。

#### [P-14] 共享标志缺少 volatile、原子操作或内存屏障（high）
ISR 与任务之间、或多核之间共享的标志用普通变量读写；或者先写数据再写 `ready` 标志，两次写之间没有 `SP_DMB()`/`atomic_*`。只在能指出共享双方时报。

#### [P-15] 用二值信号量当互斥锁（medium）
用 `nxsem_init(&sem, 0, 1)` 做互斥锁，没有优先级继承，会导致优先级反转。应使用 `nxmutex_t`。

#### [C-06] 条件变量用法错误（high）
`pthread_cond_wait` 没有放在检查条件的 `while` 循环里，或在没有持有关联互斥锁时调用、修改条件。

#### [C-07] 线程资源未回收（medium）
`pthread_create` 创建的线程既没有 `pthread_join` 也没有 detach；线程退出路径没有释放线程专属资源（包括 TLS 中分配的内存）。

#### [C-08] 信号处理函数不安全（high）
信号处理函数中调用了非异步信号安全的函数（`malloc`、`printf`、`syslog`、加锁），或修改了没有声明为 `volatile sig_atomic_t` 的共享变量。

#### [C-09] 定时器回调无条件创建新定时器（medium）
`wd_start` 等定时器回调每次触发都无条件创建新的定时器，数量无限增长。应复用同一个定时器重新启动，或加终止条件。

#### [C-10] 阻塞调用放错上下文（medium）
在高优先级工作队列（`HPWORK`）、持有自旋锁或临界区期间，执行阻塞 IO、`usleep` 或长时间轮询。

### 资源管理

#### [P-16] 文件描述符在错误路径泄漏（high）
`open()`/`file_open()` 成功后，后续出错直接返回，没有 `close()`/`file_close()`；或者 fd 没有检查是否小于 0 就传给 `ioctl`/`read`/`write`。
```c
fd = open("/dev/sensor0", O_RDONLY);
ret = ioctl(fd, SNIOC_SET_INTERVAL, &interval);
if (ret < 0)
  {
    return ret;                    /* Bad：fd 泄漏，且 fd 未判断 < 0 */
  }
```

#### [P-17] 互斥锁或信号量未销毁（medium）
`_register`/`_init` 里调用了 `nxmutex_init`/`nxsem_init`，但对应的 `_unregister`/`_uninit` 或初始化失败路径没有 `nxmutex_destroy`/`nxsem_destroy`。

#### [P-18] pm_stay 与 pm_relax 不配对（high）
`pm_stay` 之后的错误路径或 stop 路径没有对同一 domain、同一 state 调用 `pm_relax`，导致系统无法进入低功耗。

#### [P-19] socket 在失败路径未关闭（high）
`socket()` 成功后，`connect`/`bind`/`send`/`recv` 失败直接返回，没有 `close`。

#### [P-20] 错误或退出路径资源释放不完整（high）
函数申请了内存或多个资源（如 `kmm_zalloc` → `nxmutex_init` → `register_driver`），中途失败时没有按逆序释放已获取的部分。
```c
priv = kmm_zalloc(sizeof(*priv));
nxmutex_init(&priv->lock);
ret = register_driver(path, &g_ops, 0666, priv);
if (ret < 0)
  {
    kmm_free(priv);                /* Bad：锁没有 destroy */
    return ret;
  }
```

#### [P-21] 释放结构体前未取消 work（critical）
`_close`/`_stop`/`_unregister`/`_destroy` 路径释放的结构体里含有已入队的 `struct work_s`，释放前没有 `work_cancel`。
```c
work_cancel(HPWORK, &priv->work);  /* Good：先取消，再释放 */
kmm_free(priv);
```

#### [P-22] 释放结构体前未取消定时器（critical）
`wd_start` 启动的定时器持有结构体指针，结构体释放前没有 `wd_cancel`。

#### [C-11] 成对资源未配对释放（high）
以下资源在所有路径（包括错误路径）都要配对释放：`opendir`/`closedir`、`fopen`/`fclose`、`mmap`/`munmap`、`media_player_open`/`media_player_close`、`up_disable_irq`/`up_enable_irq`、`enter_critical_section`/`leave_critical_section`（每个提前 return 都要恢复）。

#### [C-12] fd 或句柄有效性判断错误（high）
用 `if (fd)`、`fd > 0` 或 `fd != 0` 判断 fd 是否有效（fd 为 0 是合法的），应使用 `fd >= 0`/`fd < 0`；或者把无效值初始化为 0 而不是 -1。

### 错误处理

#### [P-30] errno 被中间调用覆盖（medium）
调用失败后，先调用了 `syslog`/`close`/`printf` 等可能改写 errno 的函数，再读取 errno。应在失败后立即保存 errno。日志里把返回值当 errno 打印、或把 errno 当返回值打印，也按本条报。
```c
ret = read(fd, buf, len);
if (ret < 0)
  {
    int errcode = errno;           /* Good：立即保存 */
    syslog(LOG_ERR, "read failed: %d\n", errcode);
    return -errcode;
  }
```

#### [P-31] 用断言处理运行时错误（high）
`DEBUGASSERT`/`ASSERT`/`PANIC` 用来检查运行时可能失败的情况，如内存分配失败、用户或 ioctl 输入非法、设备错误。断言只能用于编程错误。

#### [P-32] 失败后状态未回滚（high）
先把状态、标志或计数改成「已完成」，再调用实际执行操作的函数；该函数失败时直接返回，没有回滚，状态与实际不一致。应在成功之后再更新状态。

#### [P-33] 负的 errno 被转成正数返回（medium）
NuttX 内核 API（如 `nxsem_wait`、`file_*`）失败时返回负的 errno，调用方却 `return -ret`，把错误变成正数，上层可能当作成功处理。

#### [C-13] 错误被吞掉（high）
函数内部调用失败，最终却返回 `OK`；或者 `ret` 在判断前被后续调用覆盖；或者关键调用失败后仍继续执行后续步骤（写设备、提交状态）。

#### [C-14] 关键返回值未检查（medium）
`read`/`write`/`ioctl`/`register_driver`/`file_open`/`nxsem_init`/`pthread_create` 等调用，后续逻辑依赖它们成功，却没有检查返回值。返回值确实不影响后续逻辑的调用（如日志、`close`）不报。

#### [C-15] 内核与用户态错误码约定混用（medium）
适用条件：nuttx 内核代码（`drivers/`、`fs/`、`sched/`、`mm/`、`net/`、`arch/`）。内核内部函数应返回负的 errno，不应设置 errno 或返回 -1；内核内部应调用 `file_*`/`nx_*` 系列接口，而不是会设置 errno 的用户态接口。

### 整数安全

#### [P-39] size_t 减法下溢（high）
两个无符号数相减而结果可能为负，回绕成很大的值，再被用作 `memcpy` 长度、循环上限或分配大小。相减前要比较大小。

#### [P-40] 有符号与无符号比较（high）
有符号值（`int offset`、`ssize_t ret`）直接和 `size_t`/`uint32_t` 比较做边界检查，负数会被提升成很大的正数而绕过检查。
```c
if (offset < bufsize)              /* Bad：offset 为 -1 时绕过检查 */

if (offset < 0 || (size_t)offset >= bufsize)
  {
    return -EINVAL;                /* Good */
  }
```

#### [P-41] 乘法溢出导致分配过小（critical）
`count * size` 直接传给 `kmm_malloc`/`malloc`，没有溢出检查，之后又按 `count` 逐个写入。应先判断 `count > SIZE_MAX / size`，或使用 `kmm_calloc`。

#### [P-42] 窄化截断（medium）
`uint32_t`/`size_t`/`ssize_t` 赋给 `uint16_t`/`uint8_t`/`int` 之前没有范围检查，高位被静默丢弃，且截断后的值用于长度或协议字段。

#### [P-43] 移位越界（medium）
移位位数可能大于等于类型位宽，或可能为负，这是未定义行为。`1 << bit` 中的 `1` 是 int，`bit >= 31` 就出问题，应使用 `UINT32_C(1) << bit` 并检查 `bit` 的范围。

#### [C-16] 除零（high）
除数或取模的右操作数来自外部输入、配置或计算结果，可能为 0，却没有检查。

#### [C-17] 无符号循环变量导致死循环（high）
无符号循环变量配合 `>= 0` 条件，如 `for (size_t i = n - 1; i >= 0; i--)`，或循环变量在循环体内被修改，导致循环无法终止。

#### [C-18] 外部枚举值或整数未校验就使用（high）
来自外部（ioctl、报文、配置）的枚举值或整数，没有校验是否在定义范围内，就用作数组下标或 switch 分发。

### 输入校验与字符串

#### [P-23] strcpy、strcat、sprintf 写入固定缓冲区（critical）
`strcpy`/`strcat`/`sprintf` 写入固定大小的缓冲区，源串或格式化参数可能超出缓冲区大小。应使用 `strlcpy` 或 `snprintf` 并检查返回值。

#### [P-24] snprintf 截断被忽略（medium）
`snprintf` 的返回值被丢弃或只检查 `< 0`，截断后的字符串仍被当作完整内容使用，如作为 AT 命令或文件路径。
```c
ret = snprintf(cmd, sizeof(cmd), "AT+CMD=%s,%d", param, val);
if (ret >= sizeof(cmd))
  {
    return -EOVERFLOW;             /* Good */
  }
```

#### [P-25] ioctl 参数未校验（high）
ioctl 处理函数把 `arg` 转换为数值或指针后直接使用（保存、解引用、作为循环上限），没有校验范围、长度和指针是否合法。
```c
case SNIOC_SET_INTERVAL:
  if (arg < MIN_INTERVAL || arg > MAX_INTERVAL)
    {
      return -EINVAL;              /* Good */
    }
  priv->interval = arg;
  break;
```

#### [P-26] 栈数组大小不安全（critical）
VLA 的长度来自外部输入或未检查的参数；或者在非 ISR 路径声明了超过约 1KB 的栈数组或栈结构体。应改用堆分配并设置上限。

#### [P-27] 边界差一（high）
遍历从 0 开始的数组时用了 `<=`；构造字符串时没有给结尾的 `\0` 留位置。

#### [P-28] 环形缓冲区未回绕（high）
环形缓冲区的 head/tail 用 `+ 1` 前进，没有 `% size` 或掩码回绕。

#### [P-29] 报文字段未校验就当下标或长度（critical）
来自网络、BLE、RPMSG、USB 报文的字段，未做边界检查就用作数组下标、分发选择或缓冲区长度。
```c
if (pkt->type >= nitems(g_handlers))
  {
    return -EINVAL;                /* Good */
  }
handler = g_handlers[pkt->type];
```

#### [P-34] 外部字符串作为格式串（critical）
外部输入（用户、网络、配置、IPC）作为 `printf`/`syslog`/`dprintf` 的格式串参数。应写成 `syslog(LOG_INFO, "%s", msg)`。

#### [P-35] system、popen 拼接外部输入（critical）
外部字符串拼进命令行，再交给 `system()`/`popen()` 执行。应改用直接的 API，或对输入做白名单校验。

#### [P-36] 路径穿越（high）
用户提供的文件名拼接到基础目录后直接打开，没有过滤 `..`、`/` 和绝对路径。

#### [P-37] switch 缺少 default 或贯穿未标注（medium）
ioctl 等分发型 `switch` 没有 `default:`，未知命令会返回 `OK` 或未初始化的 `ret`；非空的 `case` 贯穿到下一个 `case` 却没有注释说明是有意为之。

#### [P-38] strncpy 后未保证以 NUL 结尾（high）
`strncpy(dst, src, sizeof(dst))` 之后把 `dst` 当 C 字符串使用，源串过长时 `dst` 没有结尾的 `\0`。应使用 `strlcpy`。

#### [C-19] 解析结果未检查（medium）
`sscanf` 的返回值没有和期望字段数比较；`strtol`/`strtoul` 没有检查 `endptr` 和范围；`getenv` 的结果没有判 NULL 就使用。

#### [C-20] 不安全或不可重入的函数（high）
使用 `gets`；在多线程环境使用 `strtok`、`localtime`、`gmtime`、`ctime`、`asctime`，应改用对应的 `_r` 版本。

#### [C-21] strncmp 长度错误（medium）
`strncmp` 的长度参数和期望比较的前缀长度不一致，如用 `sizeof(ptr)`，或长度少算导致只比较了一部分。

#### [C-22] 未处理字节序（medium）
适用条件：报文、文件格式或跨核结构体的字段读写。多字节字段直接读写，没有用 `be16toh`/`htole32` 等转换，隐含了主机字节序。

#### [C-23] 指针与整数互转（medium）
指针强转为 `int`/`uint32_t`/`long` 保存，或反过来，在 64 位平台（sim）上会截断。应使用 `uintptr_t`。

### 嵌入式专项

适用条件：代码涉及 DMA、cache、RPMSG、共享内存、ISR、PM 回调、硬件寄存器。不涉及时跳过本节。

#### [P-44] DMA 缓冲区未按 cache line 对齐（high）
传给 DMA 的缓冲区是普通栈数组，或没有 cache line 对齐和大小保证。DMA 写入时可能破坏同一 cache line 上的相邻数据。
```c
uint8_t dma_buf[256];                                    /* Bad */
static uint8_t dma_buf[256] aligned_data(ARMV7M_DCACHE_LINESIZE);  /* Good */
```

#### [P-45] DMA 前后缺少 cache 维护（high）
DMA 发送前写入缓冲区后没有 `up_clean_dcache`；DMA 接收完成后读取缓冲区前没有 `up_invalidate_dcache`。

#### [P-46] RPMSG 消息超过 MTU（critical）
传给 `rpmsg_send` 的结构体或负载可能超过端点的 MTU（通常为 496 或 512 字节）。应使用 `_Static_assert` 或运行时检查限制大小。

#### [P-47] ISR 中调用堆分配或睡眠（critical）
ISR 可达的函数（`int xxx(int irq, FAR void *context, FAR void *arg)`，或名称以 `_isr`/`_interrupt` 结尾）调用了 `kmm_malloc`、`kmm_zalloc`、`usleep`、`nanosleep`。

#### [P-48] 写 1 清除的寄存器做了读改写（high）
写 1 清除（W1C）的状态寄存器用 `getreg32` → 或运算 → `putreg32` 的方式清标志，会把读到的所有置位标志一并清掉。应只写要清除的位。
```c
putreg32(INT_DONE, REG_INT_STATUS);  /* Good：只清 INT_DONE */
```

#### [P-49] ISR 中栈上分配过大（high）
ISR 可达的函数声明了超过约 256 字节的栈变量。中断栈通常只有 1~2KB。

#### [P-50] 多核共享内存写后未刷 cache（high）
写完给对端核使用的共享内存后，通知对端之前没有 `up_clean_dcache`，需要时还应加 `SP_DMB()`。

#### [P-51] resume 未恢复寄存器配置（high）
驱动运行时配置过硬件寄存器，但 `pm_resume` 回调在深睡唤醒后没有从 shadow 副本恢复这些寄存器。

#### [P-52] RPMSG 结构体只改了一侧（critical）
RPMSG 负载结构体只在一端的源码里定义或修改，没有放进两端共用的头文件（如 `include/nuttx/...`），两端的结构体布局会不一致。结构体跨核或跨平台传输时，还要注意填充与对齐差异，应显式使用 `begin_packed_struct`/定宽类型。

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

### 宏、头文件与控制流

#### [D3.8] 同一文件内条件编译风格不统一（low）
已有文件中新增的条件编译，和该文件现有的写法（`#ifdef CONFIG_X` 或 `#if defined(CONFIG_X)`）不一致。新文件不报。

#### [D3.12] 依赖配置的 include 缺少保护（medium）
新增的 `#include` 只有在某个 `CONFIG_*` 开启时才存在或才可用，但没有用对应的 `#ifdef` 包起来。

#### [D3.13] 重复定义项目已有的宏（medium）
新定义的宏和项目已有的公共宏语义相同，如自己定义 `ARRAY_SIZE`、`MY_MAX`、`ALIGN`，而项目已有 `nitems`、`MAX`、`ALIGN_UP`、`container_of`。

#### [D3.16] 头文件缺少 include guard（medium）
新增的头文件没有 `#ifndef __XXX_H` 保护或 `#pragma once`。故意需要多次展开的 X-macro 文件（`*.def`、`*_inc.h`）除外。

#### [D3.17] 头文件 include 了 .c 文件（medium）
头文件 `#include` 了 `.c`/`.cpp` 实现文件。需要复用的实现应拆成独立的 `.c`，或改为 `.inc`/`.def`。

#### [D8.1] goto 用作普通跳转（low）
新增的 `goto` 跳到的不是清理标签（如 `errout`、`cleanup`、`out`、`fail`），而是业务流程中间。

#### [D8.3] goto 向后跳（low）
新增的 `goto` 跳向它前面的标签，等于手写循环。紧邻 `EINTR`/`EAGAIN` 判断的短重试除外。

### nuttx 仓库专项

「本次其他变更文件」指 prompt 中的 other_changed_files 列表；「commit message」指 Requirement Background 中的内容。

#### [NX-01] 自旋锁或调度锁在提前退出路径未释放（critical）
`spin_lock_irqsave`/`spin_lock`/`sched_lock`/`nxmutex_lock` 之后，保护区内的 `break`、`continue`、`return`、`goto` 路径没有调用对应的 `spin_unlock_irqrestore`/`spin_unlock`/`sched_unlock`/`nxmutex_unlock`。循环体内加锁时要逐个检查每个出口。`enter_critical_section` 的同类问题按 C-11 报，不重复报。

#### [NX-02] 临界区被 SMP 条件编译掉（high）
保护调度器状态、任务列表或被中断和线程共享的数据的临界区，放在了 `#ifdef CONFIG_SMP` 内。单核上中断嵌套同样会产生竞争，应无条件进入临界区。只保护 SMP 专属数据（如每 CPU 状态）时除外。

#### [NX-03] 改动已有 ioctl 编号（critical）
适用条件：变更涉及 `include/nuttx/fs/ioctl.h` 或各子系统的 `_xxxIOC(nr)` 命令定义。
修改了已有命令的 `nr`、在已有命令中间插入新命令使后续编号顺延，或修改了其他子系统的 `_xxxIOCBASE`。这会破坏已编译应用和对端核的 ABI。新命令应追加在本子系统已有编号之后，留出空号也不要重排。

#### [NX-04] 格式符与参数类型不一致（high）
`printf`/`syslog`/`snprintf`/`_info`/`_err` 等的格式符与参数类型不匹配：`size_t`/`ssize_t` 没用 `%zu`/`%zd`，`off_t` 没用 `PRIdOFF`，`uint64_t`/`int64_t` 没用 `PRIu64`/`PRId64`，`uintptr_t` 没用 `PRIxPTR`；或修正长度修饰时改变了原有进制（如把 `%x` 改成 `%zu`）。

#### [NX-05] 改为栈变量或部分赋值后未清零（high）
把 `kmm_zalloc`/`zalloc` 分配的结构体改成栈变量后没有 `memset` 清零；或栈上的结构体、union 只赋值了部分字段就整体传出（拷贝给用户态、作为 RPMSG 消息发送、`memcpy` 到共享内存），未赋值字段和填充字节会带出栈上残留数据。

#### [NX-06] goto 跳过了错误检查（high）
`goto` 的目标标签位于错误检查或部分清理代码之后，跳转后返回值没有被检查、已获取的资源没有释放；或在给 `ret` 赋值之前就跳到返回 `ret` 的标签。

#### [NX-07] 遗留调试代码或被注释掉的代码（medium）
新增被注释掉的代码（`/* foo(); */`、`#if 0` 块）、临时调试打印（追踪用的 `printf`/`_alert`/`syslog(LOG_EMERG, ...)`）或 `TODO`/`FIXME`/`XXX` 标记。禁用的代码应连同因此变得无用的变量和声明一起删除。

#### [NX-08] 公共结构体新增字段没有配置保护（medium）
适用条件：修改 `include/nuttx/` 下被大量实例化的结构体，如 `struct tcb_s`、`struct file`、`struct inode`、`struct net_driver_s`、`struct iob_s`。
新增字段只服务于某个可选特性，却没有用对应的 `CONFIG_*` 包起来，关闭该特性的配置也要承担内存开销。

#### [NX-09] 唤醒或通知无条件调用（medium）
`nxsem_post`、`poll_notify`、`work_queue` 等唤醒或通知调用，没有放在真正需要它的条件下（有等待者、状态确实发生变化），导致虚假唤醒或信号量计数持续增长。

#### [NX-10] IPv4 与 IPv6 路径未同步修改（medium）
适用条件：变更位于 `net/`，且修改了只属于一个协议族的文件或分支（`ipv4_*`/`ipv6_*`、`icmp/`/`icmpv6/`、`#ifdef CONFIG_NET_IPv4`/`CONFIG_NET_IPv6` 分支）。
修复只改了一侧，同一 diff 和本次其他变更文件里都没有另一侧的对应修改，commit message 也没有说明另一侧不需要改。

#### [NX-11] 只修复了 switch 中的一处贯穿（medium）
本次 diff 为某个 `case` 补上了 `break` 或 `return`，但同一个 switch 里还有其他 case 存在同样未标注的贯穿。

#### [NX-12] 头文件不自包含（medium）
适用条件：新增或修改 `include/` 下的头文件。
头文件里新使用了某个类型或宏（`size_t`、`bool`、`FAR`、`CONFIG_*`），却没有 include 定义它的头文件（`<nuttx/config.h>`、`<stddef.h>`、`<stdbool.h>`、`<nuttx/compiler.h>`），依赖使用方先 include。

#### [NX-13] 提交内容与 commit message 不符（medium）
适用条件：Requirement Background 中给出了 commit message。
diff 中有 commit message 没有提到、且与本次目的无关的改动（无关的重构或格式调整、另一个 bug 的修复、在 `boards/` 的 defconfig 里顺带打开新功能），或 commit message 声称的修改在 diff 中找不到。在第一处无关改动上报一次，列出所有无关改动的位置。
