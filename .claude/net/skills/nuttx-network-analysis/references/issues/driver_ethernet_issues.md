# 以太网驱动问题

## 目录索引

| § | 问题特征 | 关键词 |
|---|---------|--------|
| 1 | PHY 链路 down / 频繁 up/down / 速率不匹配 | PHY, link, 无连接 |
| 2 | DMA OWN 位问题（最常见断流原因） | DMA, OWN, 断流 |
| 3 | DMA 描述符数量不足 | DMA, 描述符, 不足 |
| 4 | 描述符/缓冲区对齐问题 | 对齐, alignment, DMA |
| 5 | Tail Pointer 管理不当 | Tail, Pointer, DMA |
| 6 | DMA Burst 长度配置错误 | Burst, DMA, 配置 |
| 7 | 总线错误（Fatal Bus Error） | 总线错误, Fatal, DMA |
| 8 | 4KB 边界问题 | 4KB, 边界, DMA |
| 9 | FIFO/MTL TX Underflow / RX Overflow | FIFO, MTL, Underflow, Overflow |
| 10 | RX DMA 挂死检测与恢复 | RX DMA, 挂死, tail==current |
| 11 | PTP TX 时间戳竞态条件 | PTP, TX, 时间戳, 竞态 |
| 12 | I2C 死锁→Switch 初始化失败→ETH 不通 | I2C, 死锁, Switch |

---

## 1. PHY 链路 down / 频繁 up/down / 速率不匹配

### 1.1 PHY 链路 down

**特征**：`ifconfig` 显示无 `RUNNING` 标志

**排查思路**：
1. 检查 PHY 地址配置是否正确
2. 检查 MDIO 通信（读取 PHY ID 寄存器验证通信正常）
3. 检查 PHY 自协商状态
4. 检查物理连接（网线、连接器、信号完整性）

### 1.2 PHY 链路频繁 up/down

**排查思路**：
1. 检查 PHY 电源稳定性
2. 检查信号质量（SNR）
3. 检查自协商参数是否匹配
4. 检查 EMI 干扰

### 1.3 速率/双工不匹配

**排查思路**：
1. 检查两端自协商结果
2. 检查是否强制了速率/双工模式
3. 检查 PHY 驱动中的速率配置逻辑

---


## 2. DMA OWN 位问题（最常见断流原因）

> 适用于所有带 DMA 引擎的 Ethernet MAC 控制器。具体寄存器名称和位定义因平台而异，分析时需参考对应芯片的数据手册。

**原理**：DMA 引擎通过描述符的 OWN 位确认描述符归属：
- OWN = 1：描述符归 DMA 所有，可以处理
- OWN = 0：描述符归软件所有，DMA 不能使用

当 DMA 连续获取描述符且 OWN=0 时，产生 Buffer Unavailable 中断并进入 SUSPEND 模式。软件必须写 Tail Pointer 寄存器来唤醒 DMA。

**排查思路**：
1. 检查 DMA 状态寄存器的 TBU/RBU 位
2. 检查驱动是否在 TX 完成/RX 处理后及时将 OWN 位置 1 归还给 DMA
3. 检查中断处理是否及时（ISR 延迟可能导致描述符回收不及时）

## 3. DMA 描述符数量不足

**原理**：描述符环至少需要满足硬件最小要求（通常为 4 个）。描述符不足会导致 DMA 频繁进入 SUSPEND 状态。

**排查思路**：
1. 检查 TX/RX 描述符数量配置
2. 高吞吐场景建议增大描述符数量

## 4. 描述符/缓冲区对齐问题

| 要求 | 说明 |
|------|------|
| 地址对齐 | 描述符和数据缓冲区必须满足硬件对齐要求（通常 64-bit） |
| 长度对齐 | 起始地址和长度必须对齐到 burst size |
| 后果 | 未对齐会导致单字传输，严重影响性能 |

## 5. Tail Pointer 管理不当

**原理**：当 DMA 的 Current Descriptor Pointer == Tail Pointer 时，DMA 进入 SUSPEND 模式。软件必须及时更新 Tail Pointer 以保持 DMA 运行。

**排查思路**：
1. 检查驱动在提交新描述符后是否更新了 Tail Pointer
2. 检查 TX/RX 路径中 Tail Pointer 更新的时序

## 6. DMA Burst 长度配置错误

**原理**：Burst 长度必须在硬件支持范围内，超出范围可能导致总线告警或 DMA 停止。具体允许值参考芯片数据手册。

## 7. 总线错误（Fatal Bus Error）

**原理**：AXI 总线错误会导致 DMA 停止所有后续传输并进入 STOP 状态。

**排查思路**：
1. 检查 DMA 状态寄存器值，判断是否有关联bit指示此错误
2. 判断错误方向是读还是写
3. 检查描述符和缓冲区地址是否在有效内存范围内

## 8. 4KB 边界问题

**原理**：AXI 标准要求 burst 不能跨越 4KB 地址边界。部分硬件支持配置更严格的边界限制。

### DMA 状态寄存器关键标志位

| 标志位 | 含义 | 影响 |
|--------|------|------|
| TBU | Transmit Buffer Unavailable | TX DMA 挂起 |
| RBU | Receive Buffer Unavailable | RX DMA 挂起，可能导致 RxQueue 溢出 |
| TPS | Transmit Process Stopped | TX DMA 停止 |
| RPS | Receive Process Stopped | RX DMA 停止 |
| DDE | Descriptor Definition Error | 描述符内容全1，需软件复位恢复 |
| FBE | Fatal Bus Error | DMA 停止，需检查错误方向 |

---

## 9. FIFO/MTL TX Underflow / RX Overflow

### 9.1 TX Underflow（发送下溢）

**原理**：MTL 无法连续提供数据时，MAC 发出 underflow 状态，传输被挂起。

**排查思路**：
1. 检查 TX FIFO 大小配置
2. 检查 DMA burst 长度是否过大
3. 检查总线带宽是否足够

### 9.2 RX Overflow（接收溢出）

**原理**：RX FIFO 溢出导致接收包损坏或丢弃（取决于工作模式：Threshold 模式传输部分包，Store-and-forward 模式丢弃整个包）。

**排查思路**：
1. 检查 RX FIFO 大小配置
2. 检查 DMA RX 描述符回收速度
3. 检查 IOB 分配是否及时

### 9.3 流控阈值配置不当

**原理**：RX Queue 需要足够深度才能有效使用硬件流控。流控激活阈值设置不当会导致反压不及时。

### 9.4 Ethernet MAC 收发断流排查清单

- [ ] 检查描述符 OWN 位：确保软件及时归还描述符给 DMA
- [ ] 检查 Tail Pointer：确保及时更新
- [ ] 检查对齐：描述符和缓冲区都要满足硬件对齐要求
- [ ] 检查 DMA 状态寄存器：确认无异常中断标志
- [ ] 检查 FIFO 填充级别：通过 MTL Debug 寄存器
- [ ] 检查中断状态：TBU/RBU/FBE 等异常中断
- [ ] 检查 DMA Burst 配置：确保在硬件允许范围内
- [ ] 检查 4KB 边界：burst 不能跨越 4KB 地址边界

## 10. RX DMA 挂死检测与恢复

**特征**：ETH 收包断流 + RX DMA tail 指针 == current 指针 + 所有 RX 描述符被占用

**原理**：当所有 RX 描述符都被使用且 tail==current 时，DMA 引擎停止接收，没有自动恢复机制。

**排查思路**：
1. 检查 RX DMA 各通道的 tail_ptr 和 current_ptr 是否相等
2. 检查是否所有 RX 描述符的 OWN 位都已被软件占用
3. 检查是否有周期性 DMA 挂死检测机制
4. 检查挂死恢复函数是否被调用

**修复方向**：
- 实现周期性 DMA 挂死检测（通过低优先级 work queue 周期轮询）
- 当连续检测到 tail==current 且描述符全占用时，触发 DMA 通道重置恢复
- 恢复后重新初始化 RX 描述符环并更新 tail pointer

---


## 11. PTP TX 时间戳竞态条件

**特征**：PTP master 偶发 Sync 报文后无 Follow_up，或 Follow_up 中时间戳为 0/异常值

**原理**：PTP TX 时间戳获取流程中，如果"设置待处理包指针"操作不在锁保护范围内，并发访问者（timeout worker 或 RX poll）可能看到旧值：
- timeout worker 看到 NULL → 不释放新 pkt → 内存泄漏
- RX poll 在时间戳中断到来时看到 NULL → 丢失时间戳 → 无 Follow_up

**典型案例**：VELAFORCAR-15506

**排查思路**：
1. 检查 PTP pending 函数中"包指针赋值"是否在锁保护范围内
2. 检查所有并发路径对该指针的访问是否都持有锁
3. 检查"启动 timeout work"与"设置包指针"的顺序（应先设置再启动）

**修复方向**：
- 将包指针赋值移到锁释放之前
- 或将所有相关操作都放在锁保护范围内

---


## 12. I2C 死锁→Switch 初始化失败→ETH 不通（高频：约 5%）

**特征**：ETH link down + 电流偏低 + 偶发或必现 + 冷启后恢复

**原理（级联故障链）**：
```
I2C 总线死锁（SDA 被拉低）
→ I2C 通信失败
→ Switch 芯片无法初始化
→ Switch 未上电或配置异常
→ ETH PHY link down
→ 所有 ETH 功能不可用
```

**典型案例**：VELAFORCAR-15363 / VELAFORCAR-15371 / VELAFORCAR-15276

**排查思路**：
1. 检查设备电流是否偏低（提示 Switch 未正常上电）
2. 测量 I2C SDA/SCL 波形，确认是否有总线死锁
3. 检查 Switch 使能引脚状态
4. 检查 I2C 初始化代码是否有死锁恢复逻辑（toggle SCL）
5. 检查板级飞线是否影响了 I2C 引脚

**修复方向**：
- I2C 初始化时 toggle SCL 恢复死锁
- 预留 pin 默认配置为 input 而非 output
- 确保复位/刷写后 I2C 总线状态被正确重置

---
