# Crash 类已知问题

## 目录索引

| § | 问题特征 | 关键词 |
|---|---------|--------|
| 1 | 网络相关 Crash（协议栈多核竞争/驱动bug/IOB/TCP状态机） | crash, trap, DSTR |
| 2 | 多核网络初始化 Crash | 多核, 初始化, CPU0, per-CPU |
| 3 | ifdown can0 触发 crash | ifdown, can0, netdev |
| 4 | virtio+vlan 系统 crash | virtio, vlan, crash |
| 5 | rpmsg direct 模式负载下 crash | rpmsg, direct, 负载 |
| 6 | SIL 环境高概率软件卡住 | SIL, 卡死, 竞态 |
| 7 | 无 CAN 总线连接时 core0 crash | 无总线, core0, can_txdone |
| 8 | 以太网连接后发送 crash | ETH, 发送, devif_poll |
| 9 | 内核态任务抢占 ETH 中断处理 | 内核态, 抢占, ETH |
| 10 | ETH RX pool 初始化 IOB 不足 crash | IOB, ENOMEM, rx_pool_init, 初始化 |

---

## 1. 网络相关 Crash

**特征**：内存保护异常（如 DSTR.L2_MPE=1）或其他 trap，调用栈涉及网络相关函数

### 1.1 协议栈多核竞争

- 多核同时操作同一 TCP/UDP 连接
- IOB 在核间传递时的竞争条件
- 网络全局锁使用不当（未持锁访问共享状态、中断上下文中使用可睡眠锁）

### 1.2 Ethernet 驱动代码 bug

- DMA 描述符越界访问
- TX/RX 中断处理中的空指针解引用
- PHY 状态变化回调中的竞争

### 1.3 IOB 生命周期管理错误

- IOB 重复释放（double-free）
- IOB 释放后继续使用（use-after-free，特征为 io_offset 被覆写为 mempool free 魔数，需从源码确认当前值）
- IOB 未正确传递导致泄漏

### 1.4 TCP 状态机异常

- 连接关闭时序错误导致 UAF
- TIME_WAIT 状态下的资源释放问题
- RST 处理中的竞争条件

---

## 2. 多核网络初始化 Crash

**特征**：非 CPU0 核心启动阶段崩溃，调用栈涉及网络设备注册 → IOB 可用数查询

### 2.1 IOB/Mempool 初始化顺序

**原理**：IOB mempool 使用 lazy init 机制——首次访问时才初始化自旋锁和内存池。如果非 CPU0 核先于 CPU0 访问 IOB，会触发对未初始化自旋锁的操作。

**排查思路**：
1. 确认 IOB 初始化是否在所有核启动前完成
2. 检查 lazy init 路径中自旋锁的初始化时机
3. 检查多核启动顺序是否保证了共享资源先初始化

### 2.2 网络设备注册时序

**原理**：网络设备注册时会查询 IOB 可用数（CAN 设备还会查询特定 buffer size 的可用数），这会触发 mempool 的 lazy init。多核同时注册网络设备时可能竞争。

**排查思路**：
1. 确认各核网络设备注册的调用顺序
2. 检查是否有同步机制确保初始化完成后再允许注册

### 2.3 per-CPU 变量 BMP 适配问题

**特征**：BMP 多核架构下启动 crash + 堆管理 assert 失败（某核释放了不属于自己堆区的内存）

**原理**：per-CPU 变量宏名拼写错误时，宏替换不生效，所有 CPU 操作同一份数据（偏移为 0 的模板地址）。CPU1 释放 CPU0 堆区内存时触发 assert。

**典型案例**：VELAFORCAR-16242

**排查思路**：
1. 检查 crash 调用栈是否涉及堆管理或 mempool 函数
2. grep per-CPU 变量宏定义，确认拼写是否正确（宏展开是否生效）
3. 检查 BMP 模式下全局变量是否正确使用了 per-CPU 包装
4. 检查静态 pool buffer 声明是否区分了各核

**修复方向**：
- 修复 per-CPU 变量宏名拼写
- 删除 BMP 模式下不需要的全局 pool 别名
- 使静态 pool buffer 声明区分各核

---

## 3. ifdown can0 触发 crash

**特征**：执行 `ifdown canX` 100% 触发 crash

**原理**：网络设备 down 时需要按逆序释放资源（socket → IOB → netdev），如果有其他任务仍持有该设备引用（如后台 candump 进程），或驱动 shutdown 未正确停止 DMA/中断，会导致 UAF。

**排查思路**：
1. 检查设备 ifdown 时的资源释放顺序是否正确
2. 检查是否有其他任务仍持有该设备的引用（后台抓包进程等）
3. 检查驱动 shutdown 回调中是否正确停止了 DMA 和中断
4. 检查 ifdown 与发送/接收路径之间是否有锁保护

**典型案例**：VELAFORCAR-75411

---

## 4. virtio+vlan 系统 crash

**特征**：虚拟化环境下 virtio 网络设备 + VLAN 配置触发 crash

**原理**：VLAN 设备注册时 netdev 结构体初始化可能不完整，或 virtio 驱动的收包路径未正确处理 VLAN tag 剥离/插入。

**排查思路**：
1. 检查 VLAN 设备注册时 netdev 结构体初始化是否完整
2. 检查 virtio 驱动的收包路径是否正确处理了 VLAN tag
3. 用 GDB 分析 crash 调用栈定位具体触发点

**典型案例**：VELAFORCAR-72193

---

## 5. rpmsg direct 模式负载下 crash

**特征**：网络负载下将 rpmsg 改为 direct 收包模式，短时间内必现 crash

**原理**：direct 模式下收包回调在 ISR 上下文执行，ISR 中不能调用可能阻塞的函数（如 IOB 分配可能等待）。同时收包缓冲区在 ISR 和 task 之间存在竞态。

**排查思路**：
1. 检查 direct 模式下收包回调是否在 ISR 上下文执行
2. 确认 ISR 中是否调用了可能阻塞的函数
3. 检查收包缓冲区是否在 ISR 和 task 之间存在竞态
4. 对比 work_queue 模式是否正常——区分 direct 模式特有问题

**典型案例**：VELAFORCAR-13134

---

## 6. SIL 环境高概率软件卡住

**特征**：SIL 环境高概率测试执行时软件卡死，外发报文一段时间后停止

**原理**：SIL 环境的调度时序与真实硬件不同，竞态窗口更大。可能是资源泄漏（IOB/socket/fd）运行一段时间后耗尽，或虚拟驱动的定时器/事件模拟与实际硬件行为不一致。

**排查思路**：
1. 检查虚拟驱动的定时器/事件模拟是否与实际硬件一致
2. 检查是否有资源泄漏导致运行一段时间后耗尽
3. 用 `ps` 查看各任务阻塞点
4. 偶发性问题关注竞态条件——SIL 调度时序与真实硬件不同

**典型案例**：VELAFORCAR-13077

---

## 7. 无 CAN 总线连接时 core0 crash

**特征**：BMP 模式下无外部总线连接，启动后直接 crash

**原理**：CAN 驱动初始化时可能假设总线连接存在。无连接时 TX 立即失败，如果错误处理路径未正确实现（如 bus-off 恢复逻辑在无总线时的行为），会触发异常。

**排查思路**：
1. 检查 CAN 驱动初始化时是否假设总线连接存在
2. 检查 BMP 模式下 CAN 错误处理回调是否正确实现
3. 检查 bus-off 恢复逻辑在无总线时的行为
4. 确保 CAN 驱动在无外部连接时能安全初始化

**典型案例**：VELAFORCAR-13837

---

## 8. 以太网连接后发送 crash

**特征**：刷写固件后以太网连接后首次发送消息直接 crash

**原理**：PHY link up 后上层可能在驱动 open 尚未完成时就开始发送，发送路径中访问未初始化的资源导致空指针。

**排查思路**：
1. 检查 PHY link up 后驱动的初始化完成状态
2. 检查发送路径中是否有空指针——驱动 open 未完成时上层就开始发送
3. 检查 netdev UP 标志与驱动就绪之间的同步机制

**典型案例**：VELAFORCAR-13925

---

## 9. 内核态任务抢占 ETH 中断处理

**特征**：将用户态任务（如 audio 线程）移到内核态后，ETH 出现丢包、收不到包、甚至 crash

**原理**：内核态线程优先级通常高于用户态，可能抢占 ETH 中断下半部（高优先级工作队列），导致 DMA 描述符来不及回收。

**级联故障链**：
```
高优先级任务移到内核态
→ 优先级高于网络中断下半部
→ 中断下半部被延迟执行
→ DMA RX 描述符来不及回收
→ DMA RX ring 满 → 新包被丢弃
→ 严重时 DMA 挂死（tail == current）
```

**典型案例**：VELAFORCAR-13740 / VELAFORCAR-14568 / VELAFORCAR-14438（三条指向同一根因）

**排查思路**：
1. 检查高优先级任务的优先级 vs 网络中断下半部的优先级
2. 检查 ETH 中断处理模式（ISR 直接处理 vs 工作队列延迟处理）
3. 检查 DMA RX 描述符使用率（是否频繁满载）
4. 对比移到内核态前后的 ETH 丢包率

**修复方向**：
- 提高网络中断下半部优先级，确保高于新移入的内核态任务
- 或将 ETH RX 关键路径改为 ISR 直接处理（减少调度延迟）
- 或增大 DMA RX 描述符数量，增加缓冲余量

---

## 10. ETH RX pool 初始化 IOB 不足 crash

**特征**：BMP 多核架构下，非主核（secondary core）在 `board_lateinitialize_phaseB` 阶段 eth 驱动初始化时 DEBUGASSERT 失败，返回 `-ENOMEM`

**原理**：eth 驱动初始化时会为每个启用的 RX channel 预分配 N 个 IOB buffer（N = RX descriptor 数量）。如果该核的 per-CPU IOB 池总量小于预分配需求量，`iob_tryalloc` 返回 NULL，导致初始化失败。

**级联故障链**：
```
per-CPU IOB 池大小 < 启用 channel 数 × RX descriptor 数
→ iob_tryalloc 返回 NULL
→ rx_pool_init 返回 -ENOMEM
→ DEBUGASSERT(ret == OK) 触发 crash
→ 系统未启动到任务执行阶段即崩溃
```

**排查维度**：
1. 确认 crash 核的 per-CPU IOB 池大小配置
2. 确认该核 eth 配置中启用了几个 RX channel（`enable_queues` 中 true 的数量）
3. 确认 RX descriptor 数量（编译宏定义）
4. 计算需求量：启用 channel 数 × descriptor 数，对比 IOB 池大小
5. 附带检查：预分配代码中是否在 NULL 检查前就使用了分配结果（防御性编码缺陷）

**修复方向**：
- 增大对应核的 IOB 池配置，使其 ≥ 预分配需求量（建议留 30% 余量）
- 或减少该核启用的 RX channel / descriptor 数量
- 同时修复代码中 NULL 检查顺序的防御性编码问题

---
