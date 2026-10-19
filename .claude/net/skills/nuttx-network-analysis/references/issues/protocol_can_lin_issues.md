# CAN/LIN 总线协议问题

## 目录索引

| § | 问题特征 | 关键词 |
|---|---------|--------|
| 1 | ctrl+c candump 后再次执行无效 | candump, 资源泄漏, socket |
| 2 | candump 收不到 CAN 信息 | candump, 无数据, 过滤器 |
| 3 | CAN 发送/接收 Error 12 (ENOMEM) | ENOMEM, IOB 不足, can |
| 4 | CAN IOB 池 socket 限值不匹配 | IOB 池, 限值, socket |
| 5 | 休眠唤醒卡在 can oflush | oflush, 阻塞, 休眠 |
| 6 | SIL CAN 虚拟驱动收包数据错误 | SIL, 虚拟驱动, 收包 |

> 路由相关问题（CAN2CAN/CAN2ETH/ETH2CAN）已移至 [routing_issues.md](routing_issues.md)

---

## 1. ctrl+c 终止 candump 后再次执行无效

**特征**：ctrl+c 终止 candump 后再次执行无响应，但其他命令正常，100% 复现

**原理**：进程信号处理函数未正确关闭 socket，导致资源泄漏。后续 bind 同一设备时因资源未释放而失败。

**典型案例**：VELAFORCAR-80334

**排查思路**：
1. 检查 candump 信号处理函数是否正确关闭了 socket
2. 确认 CAN 接口状态是否残留
3. 检查 socket bind 是否因上次未释放而返回 EADDRINUSE

## 2. candump 收不到 CAN 信息

**特征**：candump 100% 收不到对端报文

**原理**：可能是硬件/外部工具配置问题，而非 OS 侧问题。

**典型案例**：VELAFORCAR-82130

**排查思路**：
1. 确认接口 UP 且无错误计数
2. 检查 CAN 收发器是否正确初始化
3. 检查波特率配置是否与对端一致
4. 排除硬件/外部工具配置问题

## 3. CAN 发送/接收 Error 12 (ENOMEM)

**特征**：cansend/candump 均返回 Error 12，100% 复现

**原理**：Error 12 = ENOMEM，通常是 IOB 或 socket buffer 分配失败。

**典型案例**：VELAFORCAR-82188 / 82189

**排查思路**：
1. 检查 IOB 配置值是否过小
2. 查看 IOB 池剩余量
3. 检查是否有其他模块大量占用 IOB

## 4. CAN IOB 池 socket 限值不匹配

**特征**：分核后 CAN 报文匹配的 IOB 池被某路 socket 耗尽，导致其他 CAN 通道也无法分配

**原理**：IOB 按请求大小匹配池。如果单路 socket 的发送缓冲区限值大于 CAN 报文对应的 IOB 池总量，一路未连接的 CAN 通道就可能积压报文吃掉该池所有 IOB。

**典型案例**：VELAFORCAR-66740

**排查思路**：
1. 检查发送缓冲区限值是否大于 CAN 报文对应的 IOB 池总量
2. 修复方案：CAN 初始化时根据对应 IOB 池大小单独设置发送缓冲区限值
3. 检查每路 CAN 的外部连接状态——未连接的通道报文会积压

## 5. 休眠唤醒卡在 can oflush

**特征**：休眠唤醒过程中周期任务阻塞在 CAN flush

**原理**：CAN 设备 flush 操作等待 TX 完成，但如果总线已断开，TX 永远不会完成，没有超时保护则永久阻塞。

**典型案例**：VELAFORCAR-14209

**排查思路**：
1. 检查 flush 操作是否在等待 TX 完成但总线已断开
2. 检查休眠路径上是否有超时保护
3. 检查 CAN 控制器在休眠前是否正确进入 sleep/stop 模式

## 6. SIL CAN 虚拟驱动收包数据错误

**特征**：SIL 环境中 CAN 虚拟驱动收到的数据与实际发送不一致

**原理**：虚拟驱动的 RX 缓冲区拷贝逻辑可能存在字节序或偏移错误，或 CAN FD 帧和标准帧的长度映射不正确。

**典型案例**：VELAFORCAR-56772

**排查思路**：
1. 检查虚拟驱动的 RX 缓冲区拷贝逻辑
2. 检查 CAN FD 帧和标准帧的 DLC/data 长度映射
3. 对比发送端原始数据和接收端输出的十六进制内容
