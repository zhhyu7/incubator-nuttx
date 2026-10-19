# 网络协议栈分析详细工作流程

> **占位符说明**：本文档中 `<.config_path>` 等占位符，由 SKILL.md 中的"配置数据源规则"和
> [platform_detection.md](platform_detection.md) 中的编译产物路径规则确定实际值。
> 典型路径格式：`out/build/cmake_out/<board_dir>/<build_variant>/.config`

## 目录

- [流程 A: Crash 分析](#流程-a-crash-分析)
- [载体识别（流程 B/C 共享前置步骤）](#载体识别流程-bc-共享前置步骤)
- [流程 B: 协议栈问题](#流程-b-协议栈问题)
- [流程 C: 驱动/硬件问题](#流程-c-驱动硬件问题)
- [流程 D: 性能分析](#流程-d-性能分析)
- [流程 E: 配置排查](#流程-e-配置排查)
- [流程 M: 多核竞态分析](#流程-m-多核竞态分析)

---

## 流程 A: Crash 分析

适用于：系统崩溃、trap、assert 等异常情况，且调用栈涉及网络相关函数。

### Step A1: 提取关键寄存器

从 crash 日志中提取平台对应的关键寄存器：

**TriCore (AURIX)**:

| 寄存器 | 含义 | 关键位 |
|--------|------|--------|
| **CPUDSTR** | Data Synchronous Trap Register | CRE(6), L2_MPE(8), L2_CSA(9), ALN(24) |
| **CPUDEADD** | Data Error Address | 触发错误的内存地址 |
| **PC** | Program Counter | 崩溃时的指令地址 |

**ARM Cortex-M**:

| 寄存器 | 含义 | 关键位 |
|--------|------|--------|
| **CFSR** | Configurable Fault Status Register | MMFSR / BFSR / UFSR |
| **BFAR** | Bus Fault Address Register | 触发错误的内存地址 |
| **PC** | Program Counter | 崩溃时的指令地址 |

### Step A2: 符号地址定位

> ⚠️ **BMP 架构**：多核共用同一个 ELF（`vela_bmp.elf`），任何核的 crash 地址都用这一个 ELF 解析，不需要按核查找不同 ELF。

```bash
# 搜索 ELF 文件
find out/ -name "*.elf" -o -name "nuttx" -type f 2>/dev/null

# 地址转源码行号（按平台选择工具链）
# TriCore:
tricore-elf-addr2line -e <ELF> -f -C <地址>
# ARM:
arm-none-eabi-addr2line -e <ELF> -f -C <地址>
```

### Step A3: 网络上下文判断

根据调用栈中的函数名判断问题所在层次：

| 函数前缀 | 所属层次 | 下一步 |
|----------|----------|--------|
| `tcp_*` | TCP 传输层 | 检查 TCP 连接状态机 |
| `udp_*` | UDP 传输层 | 检查 UDP 端口绑定和缓冲区 |
| `arp_*` | ARP 层 | 检查 ARP 表和解析流程 |
| `icmp_*` | ICMP 层 | 检查 ICMP 配置和路由 |
| `net_*`/`netdev_*` | 网络设备层 | 检查设备注册和状态 |
| `devif_*` | 设备接口层 | 检查回调和 poll 机制 |
| `*_enet_*`/`*_eth_*` | Ethernet 驱动层 | 检查 DMA/PHY/MAC |
| `rpmsg_*` | RPMSG 跨核层 | 检查通道状态和缓冲区 |
| `iob_*`/`mempool_*` | IOB 缓冲区管理 | 检查 IOB 生命周期和多核竞争 |

### Step A4: 内存保护分析

如果 Trap/Fault 涉及内存保护违规：

1. 通过 vela-knowledge-router 按平台关键词加载内存布局和 MPU 配置相关文档
2. 从 ELF/MAP 确认 IOB buffer 符号的地址范围（方法见知识库 `mempool.md`）

**关键判断**：
- 若错误地址在 IOB buffer 范围内 → IOB use-after-free（优先排查多核竞争）
- 若错误地址在外设空间 → 寄存器访问权限问题
- 若错误地址在其他核的私有内存区域 → 跨核内存访问违规

### Step A5: 生成 Crash 分析报告

```markdown
## 网络 Crash 分析报告

### 错误类型
- Trap 类型: DSE / DAE / MPX / ...
- 错误标志: L2_MPE=1, CRE=0, ALN=0, ...

### 错误地址
- 地址: 0x........
- 符号: <符号名>+<偏移>
- 所属区域: <内存区域名>

### 网络上下文
- 涉及协议层: TCP / UDP / ARP / Ethernet / RPMSG
- 涉及函数: <函数列表>
- IOB 状态: <是否涉及 IOB 操作>

### 直接原因
<一句话描述崩溃的直接原因>

### 调用路径
<简化的调用栈>

### 建议排查方向
- [ ] 检查 ...
- [ ] 验证 ...
```

→ **CP1：等待用户确认**（确认后进入问题沉淀流程，参见 SKILL.md）

---

## 载体识别（流程 B/C 共享前置步骤）

载体识别必须基于 `.config` 中的**芯片级驱动配置**，而非仅看通用网络层配置。

**判断顺序**：

1. **先查本地 Ethernet 驱动**：platform_detection 已识别芯片平台 → 确认该平台的 Ethernet 驱动是否使能。命中 → 本地 Ethernet
2. **再查 RPMSG 网络设备**：确认 RPMSG 网络驱动是否使能 → 从 rpmsgdrv 源码确认注册类型，不同类型对应不同角色（见下方 RPMSG 角色判断）
3. **两者共存时**：本地驱动优先。eth0 通常是本地驱动，rpmsg 用于其他虚拟设备
4. **USRsock 判断**：确认 USRsock 是否使能，同时确认是否有本地 TCP 栈。有 USRsock 且无本地 TCP 栈 → 纯 USRsock 转发；有本地 TCP 栈 → USRsock 仅作为 server 为其他核服务

**RPMSG 网络设备角色判断**：

RPMSG 网络设备可以注册为不同类型的网络设备，类型决定角色。分析时从 rpmsgdrv 源码确认设备类型判断逻辑和注册方式。

USRsock 与 rpmsgdrv 是独立并行的两套机制：
- rpmsgdrv 工作在网络设备层（注册 netdev），报文通过 RPMSG 通道传递到远端核
- usrsock_rpmsg 工作在 socket 系统调用层，将 socket 操作代理到远端核协议栈执行，不注册网卡
- 同一核上可同时存在两者，各自服务不同场景

**载体与分析方向映射**：

| 网络载体 | 问题特征 | 分析方向 |
|----------|---------|---------|
| Ethernet | enet/geth/PHY/MDIO/DMA/eth0/iperf/TCP/UDP/ARP | 协议栈 + Ethernet 驱动 |
| CAN | mcmcan/socketcan/can_sendmsg/can0/CAN 总线 | 协议栈 CAN/LIN 章节 + CAN 驱动 |
| WiFi | wifi/wlan/SSID/AP/sta | 仅协议栈层（TCP/UDP/ARP over WiFi），PHY 层路由到专用 WiFi skill |
| USRsock | usrsock/rpmsg/跨核 | 跨核网络章节 |

**设备归属判断逻辑**：

| 条件 | 结论 |
|------|------|
| .config 中存在芯片级 Ethernet 驱动配置 | eth0 = 本地硬件驱动，TX/RX 走 DMA 中断 |
| .config 中存在 RPMSG 网络驱动配置，但无芯片级 Ethernet 驱动 | eth0 = rpmsg 转发到远端核 |
| 两者共存 | eth0 = 本地驱动，rpmsg 用于其他设备 |

**判断失败兜底**：当 .config 中未找到匹配的网络驱动配置时，要求研发直接告知网络载体类型，不得猜测。

---

## 流程 B: 协议栈问题

适用于：TCP/UDP/ARP/ICMP/CAN/LIN 通信异常、路由转发异常等无 crash 的网络问题。

> **前置步骤**：先完成上方"载体识别"，再进入对应协议层分析。

### Step B1: 确定问题所在层次

根据问题涉及的协议类型，选择对应的分层模型：

**以太网协议栈**：
```
应用层      → socket(), connect(), send(), recv(), bind()
            ↓
传输层      → nuttx/net/tcp/ 或 nuttx/net/udp/
            ↓
网络层      → nuttx/net/arp/, nuttx/net/icmp/, nuttx/net/route/
            ↓
设备接口层  → nuttx/net/devif/
            ↓
网络设备层  → nuttx/net/netdev/, nuttx/drivers/net/netdev_upperhalf.c
            ↓
驱动层      → 平台 Ethernet 驱动（按项目确定）
            ↓
硬件        → Ethernet MAC + PHY
```

**CAN/LIN 协议栈**：
```
应用层      → socket(), bind(), sendmsg(), recvmsg()
            ↓
Socket 层   → nuttx/net/can/can_*.c
            ↓
网络设备层  → nuttx/net/netdev/
            ↓
驱动适配层  → 芯片特定（如 *_mcmcan_net.c / *_lin_net.c）
            ↓
硬件驱动层  → 芯片特定 CAN/LIN 底层驱动
```

**跨核网络（USRsock）**：
```
应用层      → socket() (非网络核)
            ↓
USRsock     → usrsock_rpmsg.c → RPMSG 通道 → usrsock_rpmsg_server.c
            ↓
NuttX 协议栈 → 网络核真实 socket 操作
```

**路由转发（CAN2ETH/ETH2CAN/CAN2CAN）**：
```
入口接收    → ETH RX / CAN RX
            ↓
路由任务    → ieee1722acf 解包/封包 / cangw 规则匹配
            ↓
出口发送    → CAN TX / ETH TX
```

### Step B2: 关键数据结构

| 结构体 | 作用 | 关键字段 |
|--------|------|----------|
| `tcp_conn_s` | TCP 连接上下文 | `state`, `sndseq`, `rcvseq`, `mss`, `rto`, `sa`, `sv` |
| `udp_conn_s` | UDP 连接上下文 | `lport`, `rport`, `domain`, `readahead` |
| `arp_entry_s` | ARP 缓存条目 | `at_ipaddr`, `at_ethaddr`, `at_time` |
| `net_driver_s` | 网络设备结构 | `d_ifup`, `d_ifdown`, `d_txavail`, `d_buf`, `d_len` |
| `netdev_lowerhalf_s` | 网络设备下半部 | `netdev`, `ops`, `quota` |
| `can_conn_s` | CAN 连接上下文 | `dev`, `filters`, `readahead`, `sconn` |
| `can_frame` | CAN 帧格式 | `can_id`, `can_dlc`, `data[]` |
| `canfd_frame` | CAN FD 帧格式 | `can_id`, `len`, `flags`, `data[]` |

### Step B3: TCP 关键代码路径

**TCP 连接建立 (3-way handshake)**:
```
connect() → tcp_connect() → tcp_sendsynack()
    → [SYN sent] → tcp_input() 收到 SYN-ACK
    → tcp_synack() → [ESTABLISHED]
```

**TCP TX 路径**:
```
psock_send() → tcp_send_buffered() / tcp_send_unbuffered()
    → tcp_sendfile() → devif_send()
    → netdev_txnotify() → driver.d_txavail()
    → [平台驱动] transmit() → DMA TX
```

**TCP RX 路径**:
```
[平台驱动] rx_interrupt() → netdev_lower_rxready()
    → netdev_input() → ipv4_input() → tcp_input()
    → tcp_callback() → tcp_data_event()
    → recvfrom() 返回数据
```

**TCP 重传机制**:
```
tcp_timer() → tcp_rexmit_timeout()
    → 指数退避: RTO = RTO * 2 (最大 120s)
    → 超过最大重传次数（从 config_reference.md 确认配置项名称）→ 连接超时
```

### Step B4: UDP 关键代码路径

**UDP TX 路径**:
```
psock_sendto() → udp_sendto_buffered() / udp_sendto_unbuffered()
    → udp_send() → devif_send()
    → netdev_txnotify() → driver.d_txavail()
```

**UDP RX 路径**:
```
netdev_input() → ipv4_input() → udp_input()
    → udp_callback() → udp_datahandler()
    → recvfrom() 返回数据
```

### Step B5: ARP 关键代码路径

**ARP 解析流程**:
```
arp_send() → 检查 ARP 缓存
    → 缓存命中 → 直接返回 MAC 地址
    → 缓存未命中 → arp_request() 发送 ARP 请求
    → 等待 arp_input() 收到 ARP 响应
    → arp_update() 更新缓存
    → 超时 → 返回 ETIMEDOUT
```

### Step B6: CAN/LIN 关键代码路径

**CAN TX 路径** (sendmsg):
```
can_sendmsg() → can_send() → netdev_txnotify()
    → driver.d_txavail() → 驱动 txpoll() → 驱动 transmit()
```

**CAN RX 路径** (recvmsg):
```
驱动 interrupt() → 驱动 receive() → can_input()
    → can_callback() → can_recvmsg() → 应用
```

**LIN 与 CAN 的关系**：

LIN 上层复用 SocketCAN 协议栈（`nuttx/net/can/`），TX/RX 代码路径与 CAN 相同（`can_sendmsg`/`can_input`），但有以下 LIN 特定差异：

- **帧 ID 范围**：LIN 帧 ID 为 0x00-0x3F（6-bit），通过 `can_id` 字段承载
- **DLC 规则**：LIN 帧长度固定由 ID 决定（ID 0x00-0x1F → 2/4 字节，0x20-0x2F → 4/6 字节，0x30-0x3F → 8 字节）
- **主/从模式**：LIN master 发送 header（break + sync + ID），slave 填充 response
- **驱动层独立**：LIN 硬件驱动（`*_lin.c` / `*_lin_sock.c`）独立于 CAN 驱动，注册为独立的 netdev

排查 LIN 问题时：协议栈层走 CAN 的代码路径分析，驱动层走独立的 LIN 驱动（通过 Step C1 的 find 命令定位）。

### Step B7: 路由关键代码路径

**ETH2CAN 路径**：
```
ETH RX interrupt → hpwork → netdev_input() → ip_input()
    → UDP/Raw socket → 应用层路由任务（MiRT/SigRoute/Gateway）
    → ieee1722acf 解包 → CAN frame
    → can_sendmsg() → CAN 驱动 transmit() → CAN 总线
```

**CAN2ETH 路径**：
```
CAN RX interrupt → can_input() → 应用层路由任务
    → ieee1722acf 封包（AVTP header + ACF CAN header）
    → sequence_num 累加
    → UDP/Raw send() → ETH 驱动 → 以太网
```

**CAN2CAN 路径（netforward/cangw）**：
```
CAN_A RX interrupt → can_input() → netforward/cangw 路由规则匹配
    → 转发到 CAN_B → can_sendmsg() → CAN_B 驱动 transmit()
```

### Step B7a: 路由问题排查流程（ETH-CAN / CAN-CAN）

当问题涉及跨总线路由时，按以下步骤排查：

**1. 确定路由方向**：
- ETH→CAN（ieee1722acf 解包）
- CAN→ETH（ieee1722acf 封包）
- CAN→CAN（netforward/cangw 规则匹配）

**2. 检查路由配置**：
- CAN forward 总开关是否使能（从 config_reference.md 确认配置项名称，再从 .config grep 确认）
- CAN 路由规则是否正确配置

**3. 检查路由链路各环节**：

| 环节 | 检查点 |
|------|--------|
| 入口接收 | RX 中断是否触发？IOB 是否分配成功？ |
| 协议解析 | ieee1722acf header 解析是否正确？CAN ID 匹配？ |
| 转发决策 | cangw 规则是否命中？目标设备是否 up？ |
| 出口发送 | TX mailbox 是否满？发送是否成功？ |

**4. 关注关键字段**：
- `sequence_num` 累加是否连续（丢帧检测）
- ACF 字段填充是否正确（AVTP subtype、acf_msg_type）
- IOB 在路由链路中的流转和释放

**5. 性能指标**：
- 路由时延（入口 RX 到出口 TX 的时间差）
- 丢帧率（sequence_num 不连续的比例）
- 满载行为（TX FIFO 满时的处理策略）

**6. 参考已知问题**：issues/routing_issues.md、issues/protocol_can_lin_issues.md（路由相关章节）

→ **CP2：等待用户确认**（确认后进入问题沉淀流程，参见 SKILL.md）

### Step B8: 常见问题排查表

| 问题 | 检查点 |
|------|--------|
| **TCP 连接超时** | 1) ARP 解析成功? 2) 路由正确? 3) PHY link up? 4) SYN 重传次数? |
| **TCP 重传过多** | 1) RTT 估算合理? 2) 拥塞窗口? 3) 网络丢包率? 4) MTU 匹配? |
| **UDP 丢包** | 1) IOB 可用数? 2) 接收缓冲区大小? 3) 发送速率? 4) 中断处理及时? |
| **ARP 超时** | 1) 同一子网? 2) 网络接口 up? 3) VLAN 配置? 4) 物理连接? |
| **ping 不通** | 1) ICMP 启用? 2) 路由正确? 3) IP 过滤规则? 4) 防火墙? |
| **DHCP 失败** | 1) UDP 广播启用? 2) 网络接口 up? 3) DHCP 服务器可达? |
| **CAN bind 失败** | 1) 设备名正确? 2) ifconfig 显示 can 设备? 3) 设备已 up? |
| **CAN 发送返回 -1** | 1) errno? 2) TX buffer 满? 3) 总线错误? 4) IOB 池耗尽? |
| **CAN 收不到数据** | 1) 过滤器? 2) bind 正确? 3) RX 中断? 4) IOB 不足? |
| **CAN Error 12** | 1) IOB 池大小配置（从知识库确认配置项名称）? 2) 其他模块占用? |
| **ETH2CAN 丢帧** | 1) CAN TX mailbox 满? 2) IOB 池不足? 3) 路由任务优先级? |
| **路由时延超标** | 1) 优先级反转? 2) IOB 分配等待? 3) 中断响应慢? |
| **USRsock 连接失败** | 1) RPMSG 通道建立? 2) 远端服务就绪? 3) 拦截范围? |

→ **CP2：等待用户确认**（确认后进入问题沉淀流程，参见 SKILL.md）

---

## 流程 C: 驱动/硬件问题

适用于：Ethernet 链路异常、PHY 问题、CAN/LIN 驱动问题、USRsock/RPMSG 网络驱动问题。

> **前置步骤**：先完成上方"载体识别"，明确当前 eth0/can0 背后是本地硬件驱动还是 RPMSG 转发，再进入对应驱动排查。

### Step C1: 定位驱动源文件

板卡识别完成后，按驱动类型搜索对应源文件：

```bash
# Ethernet 驱动
find vendor/ nuttx/arch/ nuttx/drivers/net/ -name "*enet*" -o -name "*eth*" | grep -v test

# CAN 驱动
find vendor/ nuttx/arch/ nuttx/drivers/can/ -name "*can*" -o -name "*mcmcan*" -o -name "*flexcan*" | grep -v test

# LIN 驱动
find vendor/ nuttx/arch/ -name "*lin*" | grep -E "lin_|_lin\." | grep -v test

# RPMSG 网络驱动
find nuttx/drivers/net/ -name "*rpmsg*"
```

> 所有驱动文件（含通用和平台特定）由 find 命令在运行时确定，不在此处硬编码。
> 驱动架构知识文档通过 vela-knowledge-router 按关键词加载。

### Step C2: Ethernet 驱动操作接口

所有 Ethernet 驱动实现网络设备操作接口（从源码 grep 驱动的 ops 结构体确认具体回调函数）。关键回调包括：设备启动、设备停止、发送帧、接收帧、TX 通知、设备控制。

### Step C2.5: 驱动源码竞态审查（偶发/驱动类问题必须执行）

> **"偶发"判断标准**：问题不是 100% 必现，而是在特定时序/负载/多核并发条件下才触发。
> 典型特征：同一操作有时成功有时失败、重启后恢复、高负载时出现、仅特定核触发。

> 注意：此步骤仅覆盖**驱动层特有的**竞态模式（DMA 描述符、中断/工作队列、硬件寄存器访问）。
> 涉及协议栈层面的多核竞态（IOB 生命周期、net_lock 死锁、跨核资源竞争）请参见 [流程 M: 多核竞态分析](#流程-m-多核竞态分析)。

对于偶发问题或驱动层问题，在匹配知识库之外，必须主动审查相关驱动源码中的竞态条件和资源生命周期。

**驱动层竞态审查清单**：

| 审查项 | 检查方法 | 典型问题 |
|--------|---------|---------|
| DMA 描述符生命周期 | OWN bit 检查、描述符释放时机 | 提前释放 IOB 导致 DMA 访问已回收内存 |
| 中断/工作队列竞争 | ISR 与 HPWORK 是否并发访问同一资源 | timeout worker 与 RX poll 竞争 ptp_pkt |
| 中断优先级抢占 | 内核态高优先级任务是否抢占 ETH/CAN 中断下半部 | audio 内核化后抢占 ETH RX |
| 跨核 MMIO 访问 | 非部署核是否有 MMIO 映射和锁保护 | PTP 跨核读取失败 |
| 多核 CAN 初始化 | 多核同时 ifup 同一 CAN 模块的不同通道 | MRAM 区域重叠 |

**执行方法**：
```bash
# 1. 定位驱动源文件
find vendor/ nuttx/arch/ -name "*enet*" -o -name "*eth*" -o -name "*can*" | grep -v test

# 2. 从驱动 priv 结构体中识别共享资源字段，搜索所有访问点
grep -n "priv-><共享资源字段>" <driver_file>

# 3. 检查每个访问点是否在锁保护范围内
```

**判断标准**：
- 共享变量在锁外被读写 → 竞态条件（高置信度）
- ISR 和工作队列访问同一资源无原子保护 → 竞态条件
- DMA 描述符 OWN bit 未检查就提交 → 驱动 bug

### Step C3: Ethernet 硬件调试要点（通用）

| 检查项 | 方法 |
|--------|------|
| **PHY 链路状态** | 读取 PHY 状态寄存器（MDIO），检查 link/speed/duplex |
| **MAC 统计** | 读取 MAC 统计计数器（TX/RX 帧数、错误帧、丢帧） |
| **DMA 描述符** | 检查 TX/RX 描述符 ring 状态（OWN bit、缓冲区指针） |
| **中断状态** | 检查 DMA 中断状态寄存器、中断使能配置 |
| **时钟配置** | 检查 MAC 时钟源、分频配置 |
| **PTP 状态** | 检查 PTP 时钟同步状态（若启用） |

> AURIX 平台额外检查 QBV/TSN 调度表配置。

### Step C4: RPMSG 网络驱动调试

| 检查项 | 方法 |
|--------|------|
| **通道状态** | 检查 RPMSG endpoint 是否已创建 |
| **远端状态** | 确认远端 CPU 已启动并注册 RPMSG 服务 |
| **缓冲区** | 检查 RPMSG 共享内存配置和可用缓冲区 |
| **零拷贝** | 检查 RPMSG 网络驱动零拷贝配置是否启用（从 config_reference.md 确认配置项名称） |
| **中断** | 检查跨核中断（IPI）是否正常触发 |

### Step C5: CAN 驱动调试

**关键文件**（按板卡识别）：
```bash
find vendor/ nuttx/arch/ nuttx/drivers/can/ -name "*can*" | grep -v test
```

| 检查项 | 方法 |
|--------|------|
| **时钟/波特率** | 检查波特率配置、时序参数（采样点、SJW） |
| **Message RAM** | 检查 TX/RX buffer 配置、FIFO 大小、地址对齐 |
| **中断状态** | 读取中断状态寄存器、检查中断使能/清除（寄存器名从驱动知识库确认） |
| **总线状态** | 检查协议状态寄存器、Error Counter（TEC/REC） |
| **Bus-off 恢复** | 检查自动恢复配置、恢复时间 |
| **TX FIFO/Queue** | 检查 TX FIFO 状态寄存器、Put/Get Index |

### Step C6: LIN 驱动调试

| 检查项 | 方法 |
|--------|------|
| **主/从模式** | 检查 LIN 控制器模式配置 |
| **调度表** | 检查调度表条目、周期配置 |
| **波特率** | 检查 LIN 波特率配置 |
| **校验和** | 检查 classic/enhanced checksum 配置 |

→ **CP3：等待用户确认**（确认后进入问题沉淀流程，参见 SKILL.md）

---

## 流程 D: 性能分析

适用于：网络吞吐量低、延迟高、中断风暴等性能问题。

> 配置读取统一使用 `.config`（编译产物），参见 SKILL.md "配置数据源规则"。

### Step D1: 吞吐量瓶颈定位

从 `config_reference.md` 确认以下维度的配置项名称，再从 .config 中 grep 确认实际值：

| 检查维度 | 说明 |
|---------|------|
| MTU / 包大小限制 | 过小会导致分片，影响吞吐 |
| IOB 池大小（各级） | 不足会导致分配等待 |
| TCP 窗口大小 | 过小会限制在途数据量 |
| DMA 描述符数量 | 过少会导致 DMA 频繁 SUSPEND |

### Step D2: 延迟分析

关键延迟点：
1. **中断响应延迟**：从硬件中断到 ISR 执行
2. **协议栈处理延迟**：从 ISR 到应用层数据可用
3. **上下文切换开销**：ISR → 工作队列 → 应用线程

- 检查 Ethernet 驱动是否使用 ISR 工作队列模式（从 .config 中搜索 ENET 和 ISR/WQUEUE 相关配置确认）

### Step D3: 校验和卸载

- 检查硬件校验和卸载是否启用（从 .config 中搜索 CSUM/CHECKSUMS 相关配置确认）

→ **CP2：等待用户确认**（确认后进入问题沉淀流程，参见 SKILL.md）

---

## 流程 E: 配置排查

适用于：网络功能不工作、配置错误等问题。

### Step E1: 核心 Kconfig 检查

根据问题涉及的协议类型，参考对应架构文档中的配置章节：

| 协议类型 | 配置参考文档（vela-knowledge 知识库） | 章节 |
|---------|------------|------|
| 以太网 (TCP/UDP/ARP/ICMP) | `domain_knowledge/kernel/network/` | Configuration Options |
| CAN/LIN + 路由 | `domain_knowledge/kernel/network/` | Configuration Options / 关键配置 |
| 跨核网络 (USRsock/RPMSG) | `domain_knowledge/kernel/network/` | 相关配置 |
| IOB 缓冲区 | `domain_knowledge/kernel/mempool.md` | IOB 配置 |

**通用检查步骤**：

1. 从 `config_reference.md` 确认问题涉及的配置项名称
2. 从 .config 中 grep 确认关键总开关是否使能
3. 按问题类型检查对应配置（参考上述文档中的配置章节）

**快速排除规则**：
- 功能完全不可用 + 必现 → 优先检查对应模块的总开关是否 `=y`
- IOB 相关错误（ENOBUFS/Error 12）→ 检查 IOB 池大小和 send_bufsize 限值
- 路由不工作 → 检查 CAN forward 总开关和 IEEE1722ACF 配置是否使能

### Step E2: 运行时诊断命令

```bash
# 完整网络状态检查
ifconfig                    # 接口状态
route                       # 路由表
arp -a                      # ARP 表
netstat -s                  # 协议统计
netstat -t                  # TCP 连接
netstat -u                  # UDP 端口
cat /proc/iob               # IOB 状态
cat /proc/net/dev           # 设备统计
```

→ **CP2：等待用户确认**（确认后进入问题沉淀流程，参见 SKILL.md）

---

## 流程 M: 多核竞态分析

适用于：任何流程中发现多核竞争证据时自动进入。横跨协议栈层和驱动层的公共分析流程。

**进入条件**：参见 SKILL.md「多核竞态分析（公共模块）」章节中的进入条件定义，此处不重复。

### Step M1: 确定竞态类型

| 类型 | 特征 | 涉及层次 |
|------|------|---------|
| IOB 生命周期竞态 | mempool free 魔数覆写、double-free、UAF | 协议栈 + 驱动 |
| net_lock 死锁 | 任务互相等待、调度停止 | 协议栈 |
| 跨核资源竞争 | 同一全局变量/结构体多核并发访问 | 协议栈 + 驱动 |
| DMA 与 CPU 竞争 | DMA 描述符所有权混乱、cache 不一致 | 驱动 |
| 锁保护范围不足 | 共享变量在锁外被读写 | 协议栈 + 驱动 |

### Step M2: IOB 生命周期分析

参考：vela-knowledge `domain_knowledge/kernel/mempool.md`

```bash
# 日志中搜索 UAF 特征（mempool free 魔数值需从源码确认）
grep -E "already freed|double free|io_offset" <日志>

# IOB 状态（L4 环境）
cat /proc/iob
```

**排查路径**：
1. 确定 IOB 的分配点（哪个函数 `iob_alloc`）
2. 确定 IOB 的释放点（哪个函数 `iob_free`）
3. 检查分配和释放之间是否有多核并发访问
4. 检查 IOB 是否在释放后仍被引用（UAF）
5. 检查引用计数是否正确维护

### Step M3: net_lock 死锁分析

参考：[issues/multicore_race_issues.md](issues/multicore_race_issues.md)

```bash
# 日志中搜索锁相关
grep -E "spin_lock|net_lock|NET_LOCK|nxmutex" <日志>
```

**排查路径**：
1. 确定死锁涉及的任务（哪些任务互相等待）
2. 分析锁获取顺序（是否存在 A→B 和 B→A 的交叉）
3. 检查是否在中断上下文中调用了 net_lock（禁止）
4. 检查是否持锁后调用了可能阻塞的函数
5. 检查 BMP 模式下 per-CPU 变量是否正确隔离

### Step M4: 跨核资源竞争分析

```bash
# 搜索全局变量的多核访问
grep -rn "g_.*\|priv->" <相关源文件> | grep -v "static "

# 检查 spin_lock 保护
grep -B5 -A5 "spin_lock\|spin_unlock" <相关源文件>
```

**排查路径**：
1. 确定被竞争的资源（全局变量/结构体字段）
2. 检查所有访问点是否都在同一把锁保护下
3. 检查 BMP 模式下全局变量是否需要 per-core 隔离
4. 检查中断处理函数中的资源访问是否有原子保护

### Step M5: 证据汇总与结论

**高置信度证据**：
- 同一 IOB 地址在两个核的调用栈中同时出现
- io_offset 被覆写为 mempool free 魔数，且调用栈显示释放后仍有访问
- 两个任务分别持有对方需要的锁（死锁环）
- 共享变量在 unlock 之后被赋值（竞态窗口）

**中置信度证据**：
- 偶发 crash + 多核环境 + IOB 相关函数
- 锁获取顺序在不同代码路径中不一致
- per-CPU 变量宏名使用错误

→ **CP1（如从 Crash 进入）或 CP2（如从协议栈/配置进入）：等待用户确认**（确认后进入问题沉淀流程，参见 SKILL.md）

> 诊断完成后的代码修复（CP4）和 Gerrit 提交流程，参见 SKILL.md 的"代码修复（CP4）"章节。
