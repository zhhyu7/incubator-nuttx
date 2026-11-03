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
