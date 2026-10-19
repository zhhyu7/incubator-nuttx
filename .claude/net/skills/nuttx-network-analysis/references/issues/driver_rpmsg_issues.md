# RPMSG/USRsock 驱动问题

## 目录索引

| § | 问题特征 | 关键词 |
|---|---------|--------|
| 1 | USRsock/RPMSG 连接失败 | usrsock, rpmsg, 连接失败 |
| 2 | USRsock/RPMSG 数据传输超时 | usrsock, 超时, 传输 |
| 3 | USRsock 拦截范围错误（BMP 多核级联故障） | USRsock, 拦截, 多核, 级联 |
| 4 | USRsock/RPMSG 关键配置 | CONFIG, RPMSG, 缓冲区 |
| 5 | USRsock 跨核发包阻塞调度 | USRsock, 阻塞, 调度 |

---

## 1. USRsock/RPMSG 连接失败

**特征**：`connect()` 返回错误

**排查思路**：
1. 检查远端 CPU 是否已启动
2. 检查 RPMSG 服务是否已注册
3. 检查 RPMSG 通道名称是否匹配
4. 检查共享内存配置

## 2. USRsock/RPMSG 数据传输超时

**排查思路**：
1. 检查 RPMSG 缓冲区是否耗尽
2. 检查跨核中断（IPI）是否正常
3. 检查远端处理是否阻塞
4. 检查零拷贝模式配置

## 3. USRsock 拦截范围错误（BMP 多核级联故障）

**特征**：BMP 多核架构下多个核的以太网同时不通，IOB 耗尽，某个核的应用层模块获取到了错误核的驱动信息

**原理**：USRsock 默认拦截所有 socket 类型（包括控制 socket），将 ioctl 转发到用户空间守护进程。在 BMP 多核架构下，用户空间守护进程可能运行在不同核上，导致 ioctl 路由到错误核的驱动实例。

**级联故障链**：
```
控制 socket 被 USRsock 拦截 → ioctl 路由到错误核的驱动
→ 应用层模块拿到错误通道信息 → 报文发送失败
→ 不断重试耗尽 IOB → 其他核也无法收发 → 全核不通
```

**排查思路**：
1. 检查 USRsock 是否与本地 ENET 驱动共存
2. 检查控制 socket 是否走 USRsock 卸载的配置项
3. 确认哪些 socket 类型被 USRsock 拦截
4. 检查 IOB 使用情况，确认是否因某个模块重试导致 IOB 耗尽

**修复方向**：
- 新增配置项控制控制 socket 是否走 USRsock 卸载
- 在 BMP 多核场景下关闭控制 socket 的 USRsock 卸载

**关键经验**：
- IOB 是全局共享资源，一个核的发送异常可以耗尽所有 IOB，影响其他核
- USRsock + 本地驱动共存时，必须明确哪些 socket 类型走 USRsock、哪些走本地内核栈
- "全核不通"的症状往往是级联故障，需要找到最初的单点故障

## 4. USRsock/RPMSG 关键配置维度

分析 RPMSG/USRsock 问题时需关注以下配置维度（具体配置项名称从 .config 中确认）：

- RPMSG 网络驱动使能
- 任务优先级和栈大小
- 服务端/客户端模式
- 零拷贝传输模式
- 控制 socket 卸载开关（BMP 多核建议关闭）

---


## 5. USRsock 跨核发包阻塞调度

**特征**：USRsock 跨核发包阻塞低优先级调度任务

**原理**：USRsock 通过 RPMSG 通道跨核发包时，如果 RPMSG 通道缓冲区满，发送方会同步阻塞等待。高优先级任务中如果包含 USRsock 发包操作，该任务会阻塞。

**典型案例**：VELAFORCAR-13547 / 13518

**排查思路**：
1. 检查 USRsock RPMSG 发送是否为同步阻塞调用
2. 检查 RPMSG 通道的缓冲区大小和流控机制
3. 解决方向：异步发送、增大 RPMSG 缓冲区、或将网络发包移到低优先级任务

---
