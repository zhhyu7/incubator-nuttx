---
name: nuttx-network-analysis
description: |
  NuttX 网络全栈分析（Ethernet/CAN/LIN/USRsock-RPMSG）。

  Use when:
  - crash 调用栈含网络函数 (tcp_*/udp_*/enet_*/iob_*/arp_*/icmp_*/usrsock_*/netdev_*/devif_*/can_*/lin_*)
  - TCP/UDP/ARP/ICMP/CAN/LIN 通信故障（丢包、超时、连接失败、发送失败）
  - IOB 错误 (ENOBUFS)、IOB UAF、net_lock 死锁、网络相关多核竞态
  - Ethernet/CAN/LIN/RPMSG 网络驱动层异常（PHY/DMA/中断/总线相关）
  - RPMSG 网络 (rpmsgdrv/usrsock_rpmsg) 跨核收发异常
  - 网络吞吐/延迟劣化（需 iperf/netstat/tcpdump 数据支撑）
  - 网络 Kconfig 配置导致的功能异常（IOB 池不足、协议未使能等）

  Not for:
  - non-NuttX 系统
  - pure WiFi/BLE driver firmware（协议栈之上的 WiFi 问题仍由本技能处理）
  - netlink_* 问题
  - 通用性能 profiling（无网络症状）→ nuttx-profiling
  - 通用 Kconfig 修改（无网络故障现象）→ vela-config
  - 通用 crash（调用栈无网络函数）→ crash-analysis
---

# NuttX 网络全栈分析

NuttX 网络协议栈系统专家技能。覆盖从应用层 Socket API 到底层硬件驱动的完整协议栈，支持多芯片平台、多总线类型。

支持的输入：crash 日志、coredump、GDB 调用栈、网络问题描述、tcpdump 抓包、netstat/ifconfig/iperf 输出、/proc/iob、candump 输出

## 技能覆盖范围

| 层次 | 覆盖内容 |
|------|----------|
| **协议栈层** | TCP/UDP、ARP/ICMP、IPv4/IPv6、CAN/LIN 总线协议、跨核网络(USRsock)协议逻辑 |
| **驱动层** | Ethernet(多芯片)、CAN(多芯片)、LIN(多芯片)、RPMSG 网络驱动 |
| **应用层** | CarOS: MiRT/SocketMgr/DoIP/EthTsyn/EthSM/SwtMgr/Gateway/SigRoute/E2E |
| **公共模块** | 多核竞态分析(IOB/net_lock/跨核竞争)、Crash 分析、配置排查 |

### 不覆盖的领域

| 领域 | 路由到 |
|------|--------|
| WiFi/BLE 驱动固件 | 专用 WiFi skill（本技能仅处理 WiFi 之上的协议栈部分） |
| Netlink 协议 | 不在本技能范围 |
| 通用 crash（无网络函数） | crash-analysis skill |

---

## 使用前提

### 平台识别与工具链（必须首先执行）

分析开始时，读取 [platform_detection.md](references/platform_detection.md) 完成：
1. 工作目录校验（确认在 NuttX 项目根目录）
2. 平台识别（CarOS / Vela ARM / Vela RISC-V）
3. 板卡识别（芯片平台 + 交叉验证）
4. 工具链初始化（addr2line / nm / objdump 路径确定）


### 环境检测

分析开始时自动检测当前环境，决定可用能力：

| 环境级别 | 特征检测 | 可用能力 |
|---------|---------|---------|
| **L1: 纯静态** | 仅有 crash 日志/log 文本，无 ELF，无设备 | 日志解析、模式匹配、知识库检索 |
| **L2: 静态+ELF** | 有 ELF 文件，无设备连接 | L1 + addr2line、nm、objdump、反汇编 |
| **L3: 静态+源码** | 在 NuttX 项目目录中，有源码 | L2 + 源码追踪、配置检查、patch 生成 |
| **L4: 运行时调试** | 有设备连接（GDB/OpenOCD/JLink/串口） | L3 + 实时寄存器读取、断点、内存 dump |
| **L5: 完全无证据** | 无日志、无 ELF、无设备、问题描述无具体现象（仅"网络不通"之类） | 无法定位根因；主动向研发索要证据（见 [triage_and_relay.md §3.2](references/triage_and_relay.md)），不硬猜 |

**检测方法**：依次执行以下检查，取最高匹配级别：

```bash
# 定位项目根目录（按优先级尝试多种方式）
PROJECT_ROOT=$(
  # 1. Git 仓库
  git rev-parse --show-toplevel 2>/dev/null ||
  # 2. Google repo 管理（.repo 在顶层）
  (d="$PWD"; while [ "$d" != "/" ]; do [ -d "$d/.repo" ] && echo "$d" && break; d=$(dirname "$d"); done) ||
  # 3. NuttX 特征文件向上搜索（适用于非 VCS 管理的源码目录）
  (d="$PWD"; while [ "$d" != "/" ]; do [ -f "$d/nuttx/Kconfig" ] && echo "$d" && break; d=$(dirname "$d"); done) ||
  # 4. 兜底：当前目录（标注为不可靠）
  pwd
)

# L3 检测：源码目录存在（从项目根目录检查）
test -d "$PROJECT_ROOT/nuttx/net" || test -d "$PROJECT_ROOT/nuttx/drivers" || test -f "$PROJECT_ROOT/nuttx/Kconfig"

# L2 检测：ELF 文件存在（从项目根目录搜索，列出所有候选）
ELF_LIST=$(find "$PROJECT_ROOT/out" -name "*.elf" -type f 2>/dev/null)
ELF_COUNT=$(echo "$ELF_LIST" | grep -c .)
# 单个 → 直接使用；多个 → 根据分析上下文（核名/板卡）选择，无法判断时询问研发

# L4 检测：设备连接（仅在用户提及或需要时执行）
pgrep -f "openocd\|JLinkGDBServer" 2>/dev/null
ls /dev/ttyACM* /dev/ttyUSB* 2>/dev/null | head -1
```

> 如果 PROJECT_ROOT 仅由兜底 `pwd` 命中且 L3 检测失败，应提示研发确认项目根目录路径，不可假设当前目录即为项目根。

> 注意：L3 包含 L2 能力（源码目录通常也有 ELF 产物）。如果在 L3 环境但未找到 ELF，仍为 L3（可源码分析但无法 addr2line）。

**环境不足时的处理策略**：

| 情况 | 处理 |
|------|------|
| 需要 addr2line 但无 ELF | 提示研发提供 ELF 文件路径 |
| 需要读寄存器但无设备 | 基于日志/文档推断，标注"[未实际验证]" |
| 需要执行 NSH 命令但无串口 | 给出命令让研发手动执行并反馈结果 |
| 运行时调试可用 | 主动提议实时读取，CP3 可直接执行 GDB 命令 |

**L4 操作权限分级**：

| 权限 | 操作类型 | 示例 | 是否需确认 |
|------|---------|------|-----------|
| 只读 | GDB 查看命令、寄存器读取、内存 dump 读取 | `bt`、`info`、`x`、`p` | 否，可自主执行 |
| 连接 | 首次建立 OpenOCD/JLink/GDB 连接 | `target remote` | 是（仅首次） |
| 写入 | 修改设备状态 | `set`、`load`、`restore`、`reboot`、`ifdown` | 是，每次确认 |
| 刷写 | 固件下载 | flash/download | 是，每次确认 |

**L4 环境建立与调试执行**：

两种平台 AI 均可自主完成 L4 只读调试（执行 GDB 只读命令 + 获取结果），无需用户手动操作 GDB。首次连接需用户确认，连接建立后的只读操作无需再次确认。

| 平台 | 路由到 | AI 自主调试流程 | 参考文档 |
|------|--------|----------------|---------|
| CarOS（TriCore） | `caros-board` skill | 1. 按 openocd-debug.md 完成前置检查并启动 OpenOCD（首次连接需用户确认）<br>2. tmux 中启动 `tricore-gdb` + `target remote :<port>`<br>3. 通过 Bash 工具向 tmux 发送 GDB 命令并读取输出 | `caros-board/references/openocd-debug.md` |
| 通用 NuttX/Vela | `gdb-start` skill | 1. 按 gdb-start 流程启动 GDB + gdbrpc<br>2. 通过 gdbmcp MCP 工具发送 GDB 命令 | `gdb-start/SKILL.md` |

两条路径的底层通道不同（Bash/tmux vs gdbmcp MCP），但对本 skill 而言能力等价——都能执行 GDB 命令并拿到结果用于网络分析。连接建立后的只读 GDB 操作无需再次确认。


### 配置数据源规则

| 数据源 | 用途 | 说明 |
|--------|------|------|
| `.config`（编译产物或测试版本包） | 所有分析步骤的配置读取 | Kconfig 展开后的完整配置 |
| `defconfig`（源码） | 仅用于对比"用户主动修改了哪些配置" | 只记录与默认值不同的项 |

全流程统一使用 `.config` 作为配置分析的唯一数据源。

.config 缺失时的降级策略：
1. 优先从 Jira 测试版本包提取
2. 非 Jira 场景下本地编译产物的 .config 可直接使用
3. 都没有 → 要求研发提供，不得猜测配置值

### 信息确认规则

分析过程中涉及的具体技术信息（配置项名称、函数位置、寄存器定义、硬件参数等），必须从知识库文档、源码、.config 中确认。**如果以上途径均无法确认，禁止自行推测，必须向研发确认后再继续。**

**来源标注要求**：在分析报告中引用具体技术信息时，必须内联标注信息来源。标注格式：

| 标注 | 含义 | 示例 |
|------|------|------|
| `[源码]` | 从源码 grep/read 确认 | `IFXGETH_MAX_RX_DESCRIPTORS=12` [源码] |
| `[.config]` | 从 .config 中 grep 确认 | `CONFIG_IOB_NBUFFERS1_CPU2=10` [.config] |
| `[知识库]` | 从 vela-knowledge 文档确认 | IOB 使用 mempool 管理 [知识库] |
| `[ELF]` | 从 ELF 反汇编/nm 确认 | 函数地址 0x80012340 [ELF] |
| `[日志]` | 从用户提供的 crash 日志/截图中读取 | `core_id = 2` [日志] |
| `[需确认]` | 无法从以上途径确认，需研发提供 | PHY 地址配置 [需确认] |

**执行规则**：
- CP1/CP2/CP3 的结论输出中，每条关键判据必须带标注
- 出现 `[需确认]` 标注时，必须暂停分析并向研发提问，不可带着未确认信息继续推导结论
- 无标注的技术信息视为违规——如果发现自己输出了无标注的具体值，立即补标注或标记 `[需确认]`

### 知识库缺失发现规则

分析过程中如果发现 vela-knowledge 知识库文档存在缺失或过时（如缺少关键配置项说明、源码路径已变更、新增平台未覆盖等），应在 CP 结论中附带告知研发，格式：「知识库发现：`<文档路径>` 缺少/过时 `<具体内容>`，建议补充。」不阻塞当前分析流程。

### 敏感信息处理

脱敏规则（分析报告输出时执行）：
- 内网 IP（`10.x.x.x`、`172.16-31.x.x`、`192.168.x.x`）→ `[内网IP]`
- 设备 SN（连续 12+ 位字母数字串）→ `[设备SN]`
- 车辆 VIN（17 位字母数字）→ `[VIN]`
- MAC 地址 → 保留前 3 字节，后 3 字节替换为 `xx:xx:xx`
- 私钥/证书文件（`.pem`、`.key`、`.crt`、`.p12`）→ 不读取文件内容，仅引用文件路径
- 169.254.x.x（link-local）和 VLAN IP 可保留用于分析

### 交互风格

- **语言**：默认使用中文
- **称呼**：称呼用户为"研发"
- **技术文档/报告**：使用标准技术写作风格（不带称呼）

### 模式选择

| 执行模式 | 行为 | 触发条件 |
|---------|------|---------|
| 标准模式 | 所有 CP 正常交互 | 默认模式；用户无特殊指定时使用 |
| 快速模式 | 分析仍完整执行，合并所有中间 CP 到 CP3 点一次性展示（含根因+排查方向+修复建议），用户可选择继续进入 CP4 或结束 | 用户说"快速看一下"/"简单分析"/"帮我快速定位" |
| 仅分析模式 | 到 CP1/CP2/CP3 结束，不进入 CP4 | 用户说"只分析不修"/"帮我看看原因"/"不需要修复" |

标准模式下 CP4 不可跳过。快速模式下用户可在合并展示点选择结束或继续进入 CP4。仅分析模式不进入 CP4。


---

## 参考文档与读取策略

**知识库优先规则**：当 workflow_details.md 中的代码路径、数据结构、配置项名称等技术细节与 vela-knowledge 知识库文档内容冲突时，**以知识库为准**。

**vela-knowledge-router 路由未命中时的兜底策略**：

所有通过 vela-knowledge-router 加载文档的场景（驱动文档、协议栈文档、应用层文档、平台特定文档）共享此兜底规则。

**触发条件**（任一满足即进入降级）：
- 路由未命中任何文档（关键词无匹配）
- 路由调用超时或返回错误（skill 执行异常、MCP 连接失败等）
- 返回的文档路径不存在或文件为空

**降级策略**：

| 优先级 | 降级方式 | 说明 |
|--------|---------|------|
| 1 | 缩短关键词重试 | 去掉芯片/平台特定词，仅用通用类型关键词重试一次（仅适用于未命中场景，超时/错误不重试） |
| 2 | 源码直接分析 | 从源码中 grep 相关函数/结构体，直接阅读代码 |
| 3 | 标注并继续 | 在分析结论中标注"[无知识库文档覆盖]"，基于源码分析继续，不阻塞流程 |

禁止因知识库未命中或调用异常而中断分析。未命中同时触发"知识库缺失发现规则"，在 CP 结论中附带告知研发。

### 公共文档

| 文档 | 规模 | 读取方式 | 状态 |
|------|------|----------|------|
| [platform_detection.md](references/platform_detection.md) | <500 行 | 平台识别 + 板卡识别 + 工具链初始化（分析开始时加载） | ✅ |
| [workflow_details.md](references/workflow_details.md) | <800 行 | 按需读取对应章节（含载体识别规则） | ✅ |
| [triage_and_relay.md](references/triage_and_relay.md) | <300 行 | 进决策树前读「前置分诊」；被 bug-analyze 接力时读「接力模式」；信息不足时读「能力边界」 | ✅ |

### 平台特定文档

平台特定的 Crash 参考、工具链文档等，按板卡识别结果通过 vela-knowledge-router 动态加载（与驱动文档相同机制），不在此处列举具体路径。

典型场景：Crash 分析时按平台关键词（如 DSTR、DEADD、CSA）命中对应 Crash 参考文档；符号解析时按工具链关键词（如 tricore-elf-addr2line）命中对应工具文档。

### 协议栈文档

**加载策略**：先读 `overview.md` 建立全局上下文（架构分层 + 数据流 + 目录索引），再按问题类型从其目录表选取对应子文件读取。

| 步骤 | 操作 |
|------|------|
| 1 | 进入流程 B/C/D/E 时，读取 `domain_knowledge/kernel/network/overview.md` 的架构图和目录结构章节 |
| 2 | 根据问题涉及的协议类型，从 overview.md 的目录表选取子文件读取 |
| 3 | 跨域知识（如 mempool）通过 vela-knowledge-router 路由加载 |

| 文档 | 规模 | 读取方式 | 状态 |
|------|------|----------|------|
| vela-knowledge: `domain_knowledge/kernel/network/` | 多个文件，各 <350 行 | overview.md 为锚点，按需读取子文件 | ✅ |

### 驱动文档

驱动层知识文档按**板卡识别结果**动态加载，不硬编码具体路径。

**加载时机**：进入流程 C（驱动/硬件问题）或流程 A 中 Step A3 判定为驱动层时。

**加载规则**：

| 步骤 | 操作 |
|------|------|
| 1 | 板卡识别确定平台（参见"板卡识别"章节） |
| 2 | 确定驱动类型（Ethernet / CAN / LIN / RPMSG，由决策树路由结果决定） |
| 3 | 组合「平台 + 驱动类型」关键词，通过 vela-knowledge-router 路由表匹配加载对应文档 |

**关键词构建规则**：

组合公式：`<板卡识别出的芯片/平台关键词>` + `<驱动类型关键词>`

| 驱动类型 | 通用关键词 | 说明 |
|---------|-----------|------|
| Ethernet | Ethernet, PHY, DMA, MAC, PTP, netdev_lower | 板卡识别后追加芯片特定词（如 GETH / stm32_eth） |
| CAN | CAN 驱动, Message RAM, 寄存器, 波特率 | 板卡识别后追加芯片特定词（如 MCMCAN / FlexCAN） |
| CAN (SIL) | SIL Kit CAN, QEMU CAN, 虚拟 CAN | 仅 SIL/QEMU 环境 |
| LIN | LIN 驱动, 调度表, LIN master/slave | — |
| RPMSG 网络 | rpmsgdrv, RPMSG 网络, 跨核网络 | — |
| 应用层 | MiRT, DoIP, EthTsyn, Gateway, SigRoute | 仅 CarOS 平台 |

**示例**：板卡识别为 Infineon TriCore + 驱动类型为 CAN → 查询关键词 "MCMCAN CAN 驱动架构" → 路由表命中对应文档。

> 新芯片/新驱动的文档只需添加到 vela-knowledge 知识库并在 vela-knowledge-router 路由表中注册，本 SKILL.md 无需修改。

### 应用层文档

应用层知识文档按**平台识别结果**加载。本 skill 常被作为 sub-agent 调起，而
sub-agent 上下文内只能看到 skill 名字列表（无 description），无法靠 description
做自动路由（约束 R8）。因此应用层加载采用**确定性两步**：先过条件门，命中后
直接 Read 固定锚点，再以 vela-knowledge-router 动态匹配作兜底。

**Step 1 — 条件门**：当**平台 = CarOS** 且问题描述/调用栈命中以下任一应用模块时，
直接进入 Step 2（不依赖 description 自动路由）：

> MiRT / DoIP / EthTsyn / EthSM / SwtMgr / Gateway / SigRoute / E2E / SocketMgr

**Step 2 — 固定锚点 Read**（确定性，命中条件门即读）：

```
vela-knowledge/project_knowledge/caros/net/network_app_layer.md
```

> 该锚点为应用层知识的**单一来源**，不在本 skill 的 references/ 下复制内容。

**兜底（条件门未命中 / 非 CarOS 平台 / 新增模块）**：组合「平台 + 应用模块」关键词，
通过 vela-knowledge-router 路由表动态匹配加载对应文档。

| 步骤 | 操作 |
|------|------|
| 1 | 平台识别确定平台（如 CarOS / Vela ARM） |
| 2 | 确定应用模块名（由问题描述或调用栈中的模块关键词决定） |
| 3 | 组合「平台 + 应用模块」关键词，通过 vela-knowledge-router 路由表匹配加载对应文档 |

> 新平台/新应用模块的文档只需添加到 vela-knowledge 知识库并在 vela-knowledge-router
> 路由表中注册；若属 CarOS 应用层，归并进上述固定锚点 `network_app_layer.md` 即可，
> 本 SKILL.md 无需修改。

### 已知问题库（issues/）

| 文档 | 覆盖范围 |
|------|----------|
| [issues/crash_issues.md](references/issues/crash_issues.md) | Crash 类已知问题 |
| [issues/protocol_tcp_udp_issues.md](references/issues/protocol_tcp_udp_issues.md) | TCP/UDP 协议问题 |
| [issues/protocol_arp_icmp_issues.md](references/issues/protocol_arp_icmp_issues.md) | ARP/ICMP 问题 |
| [issues/protocol_can_lin_issues.md](references/issues/protocol_can_lin_issues.md) | CAN/LIN 总线协议问题 |
| [issues/driver_ethernet_issues.md](references/issues/driver_ethernet_issues.md) | 以太网驱动问题 |
| [issues/driver_can_issues.md](references/issues/driver_can_issues.md) | CAN 驱动问题 |
| [issues/driver_lin_issues.md](references/issues/driver_lin_issues.md) | LIN 驱动问题 |
| [issues/driver_rpmsg_issues.md](references/issues/driver_rpmsg_issues.md) | RPMSG/USRsock 驱动问题 |
| [issues/multicore_race_issues.md](references/issues/multicore_race_issues.md) | 多核竞态问题 |
| [issues/config_issues.md](references/issues/config_issues.md) | 配置类问题 |
| [issues/performance_issues.md](references/issues/performance_issues.md) | 性能问题 |
| [issues/ptp_issues.md](references/issues/ptp_issues.md) | PTP 时钟同步问题 |
| [issues/sleep_wake_issues.md](references/issues/sleep_wake_issues.md) | 休眠唤醒问题 |
| [issues/routing_issues.md](references/issues/routing_issues.md) | CAN2CAN/CAN2ETH/ETH2CAN 路由问题 |

**读取策略统一规则**：
- <500 行：可直接全文读取
- 500-1500 行：先读目录（前 30 行）定位章节，再按需读取
- \>1500 行：先读目录（前 100 行），用 start_line/end_line 分页读取，grep 定位后前后扩展 20 行

**issues 文件使用方式**：每个 issues 文件前 30 行为目录索引，分析时先按问题特征匹配目录，命中则读取对应章节。


---

## 流程路由与执行

**优先级：Crash → Config → ProtocolStack → Driver → Performance**

### 全局阶段编号（Gate 锚点 · 单一事实源）

本 skill 内部采用**分流程编号**（流程 A/B/C/D/E/M 的 Step A1、B3、C1… 见
[workflow_details.md](references/workflow_details.md)）+ **Checkpoint 编号**（CP1-CP4）。
两道 always-loaded 网络 gate（`rules/network-evidence-gate.md`、
`rules/network-root-cause-gate.md`）用一套**全局阶段编号**锚定阻断时机。二者的对应关系
以下表为唯一事实源——gate 文档中出现的 `Step N` 一律按本表解析，**不要**与
[platform_detection.md](references/platform_detection.md) 内部的 `Step 1/2/3`（平台识别专用）混淆。

| 全局阶段 | 含义 | 对应本 skill 的实际锚点 |
|---------|------|------------------------|
| **Step 1** | 平台/环境准备 | platform_detection 全流程（工作目录校验→平台识别→工具链）+ 环境分级 L1-L5 |
| **Step 2** | 前置分诊 + 证据收集 | 「前置分诊」节：Step 2.1 = §1.1 非 bug 识别；Step 2.2 = §1.2 分支同步检查 + `.config` 比对（gate 前证据收集，不算源码定位） |
| **Step 2.1.1** | 经验前置匹配（证据门 Gate 1） | 「执行顺序」步骤 1 之前，按 `network-evidence-gate.md` 完成经验库路由并输出 `## Network Experience Routing` 块 |
| **Step 3** | 知识库匹配（源码定位起点） | 「执行顺序」步骤 1/1a：issues 库匹配 + 案例验证（首次读/搜源码定位即从此开始） |
| **Step 4** | 分析结论确认 | 各流程的分析产出 + 到达 **CP1（Crash）/ CP2（协议栈·配置·性能）/ CP3（驱动）** |
| **Step 5** | 代码修复 | 「代码修复（CP4）」章节 = **CP4** |
| **Step 7** | 流程内源码深度定位 | workflow_details 各流程读源码的代码路径步骤（如 Step B3-B7 / C1-C2.5 / M2-M4）——与 Step 3 同属"源码定位"，受同一证据门约束 |

> 证据门在 **Step 3 / Step 7 之前**（任何源码定位动作之前）必须清关；根因门在
> **Step 4 根因结论 / Step 5 修复之前**必须清关。两门独立、互不豁免（含 RELAY 模式）。

### 前置分诊（进决策树之前）

分析一个网络 Jira / 问题描述时，先做两项快速分诊（详见
[triage_and_relay.md §一](references/triage_and_relay.md)），再进决策树：

1. **非 bug 快速识别**：约 11% 以太网 Jira 是需求/适配/同步任务而非 bug，命中
   特征（`[ETH]` 开头无异常、含"适配/合入/bringup/整改"且无失败现象）则直接告知
   并退出，不进入分析。
2. **分支同步检查**：非主线分支（BMP/E3U1 等）issue 必做，约 15% 问题由 patch 漏
   cherry-pick 导致。属 gate 前证据收集，不受证据门阻断。

> 被 `bug-analyze` 以 `[RELAY MODE]` 接力进入时，前置分诊跳过（上游已判定为网络
> bug），改读 [triage_and_relay.md §二 接力模式](references/triage_and_relay.md) 的
> 输入/回传契约。信息不足、需判断能力边界时读 §三。

### 决策树

```
┌─ 有 crash/trap/assert？
│   ├─ YES → CrashAnalysis
│   │         ├─ 调用栈含 tcp_*/udp_*/enet_*/iob_* → 以太网协议栈方向
│   │         ├─ 调用栈含 can_*/lin_* → CAN/LIN 总线方向
│   │         ├─ 调用栈含 usrsock_*/rpmsg_* → 跨核网络方向
│   │         └─ 涉及 IOB + 多核 → 多核竞态分析模块
│   │
│   └─ NO → 配置检查（最先排除）
│            ├─ Kconfig 缺失/错误/未使能 → Config 流程 → CP2 → 结束或 CP4
│            └─ 配置正常 → 继续判断
│                 ├─ TCP/UDP/ARP/ICMP 异常 → ProtocolStack (以太网)
│                 ├─ CAN/LIN 发送/接收异常 → ProtocolStack (CAN/LIN)
│                 ├─ ETH-CAN/CAN-CAN 路由问题 → 路由分析
│                 ├─ 跨核通信异常 → ProtocolStack (跨核网络)
│                 ├─ 驱动/硬件问题 → Driver
│                 │    ├─ 识别驱动类型 (Ethernet/CAN/LIN/RPMSG)
│                 │    └─ 识别板卡芯片 (Infineon/STM32/NXP/...)
│                 └─ 性能问题 → Performance
```

### 流程接口定义

路由完成后，按下表确定流程的输入、输出、Checkpoint 和详细步骤位置：

| 流程 | 输入 | 输出 | CP | 详细步骤 |
|------|------|------|----|---------| 
| CrashAnalysis | crash 日志/coredump | 根因报告（错误类型+地址+调用路径+排查方向） | CP1 | [§流程 A](references/workflow_details.md#流程-a-crash-分析) |
| Config | .config + 运行时状态 | 配置问题清单或"配置正常" | CP2 | [§流程 E](references/workflow_details.md#流程-e-配置排查) |
| ProtocolStack (以太网) | 问题描述 + 日志/抓包 | 协议层根因定位 | CP2 | [§流程 B](references/workflow_details.md#流程-b-协议栈问题) |
| ProtocolStack (CAN/LIN) | 问题描述 + candump/日志 | 协议层根因定位 | CP2 | [§流程 B Step B6](references/workflow_details.md#step-b6-canlin-关键代码路径) |
| ProtocolStack (跨核网络) | 问题描述 + 日志 | USRsock/RPMSG 根因定位 | CP2 | [§流程 B](references/workflow_details.md#流程-b-协议栈问题) |
| 路由分析 (ETH-CAN/CAN-CAN) | 路由方向 + 配置 + 日志 | 路由链路根因定位 | CP2 | [§流程 B Step B7a](references/workflow_details.md#step-b7a-路由问题排查流程eth-can--can-can) |
| Driver | 驱动类型+板卡芯片 | 驱动/硬件根因定位 | CP3 | [§流程 C](references/workflow_details.md#流程-c-驱动硬件问题) |
| Performance | 性能数据 | 瓶颈定位 | CP2 | [§流程 D](references/workflow_details.md#流程-d-性能分析) |
| 多核竞态 | 多核竞争证据 | 竞态根因定位 | CP1/CP2 | [§流程 M](references/workflow_details.md#流程-m-多核竞态分析) |

> **多核竞态**有两种进入方式：① 决策树直接路由（crash 涉及 IOB + 多核）；② 其他流程执行中发现多核证据时横向切入。详见下方独立章节。

> 各流程的详细执行步骤（代码路径、数据结构、排查表、命令）参见 [workflow_details.md](references/workflow_details.md)。

### 执行顺序

路由选定后，实际执行顺序为：

1. **知识库匹配**（必须先行，不可跳过）：
   - 根据路由结果选择对应的 issues 文件
   - 读取该文件目录（前 30 行）
   - 用问题关键特征匹配章节标题
   - 命中已知案例 → 进入步骤 1a
   - 未命中 → 跳到步骤 2
   
   **1a. 案例验证**（命中后执行）：
   - issues 文件提供**问题模式 + 原理 + 排查维度**
   - 排查步骤中涉及的具体信息，分两步动态获取：
     - **配置项名称** → 从 `domain_knowledge/kernel/network/config_reference.md` 查阅（网络相关配置速查表），或通过 vela-knowledge-router 查阅其他领域的关联知识库文档
     - **配置项实际值** → 从 .config 中 grep 确认
     - **代码位置** → 从源码中 grep 功能关键词定位当前版本的实际路径；跨核网络相关可参考 `domain_knowledge/kernel/network/usrsock_rpmsg.md` 中的源码位置索引
   - 按排查维度逐项验证 → 验证通过则直接到 CP

2. **未命中** → 进入对应流程的标准分析步骤（workflow_details.md）。
   **进入流程 B（协议栈）或流程 C（驱动/硬件）前，必须先完成「载体识别」**（判断 eth0/can0 背后是本地硬件驱动还是 RPMSG 转发），见 [workflow_details.md §载体识别](references/workflow_details.md#载体识别流程-bc-共享前置步骤)。载体识别属分析动作，位于证据门清关之后。
3. **分析完成** → 到达对应 Checkpoint，等待用户确认
4. **问题沉淀**（**不可跳过**，分析结束时必须执行，无论从哪个 CP 退出）：如果本次分析定位了 issues 库中不存在的新问题模式，询问研发是否需要将其沉淀到对应的 issues 文件中。沉淀内容限于：问题特征、原理、排查维度、修复方向（概括性指导）。不写入具体的配置项值、函数名、文件路径等可能过时的信息。

> 知识库匹配是成本最低、收益最高的分析路径——已知问题库沉淀了历史排查经验，优先复用可避免重复分析。

### 流程冲突处理

- crash + 驱动异常 → CrashAnalysis（驱动状态作为子任务检查）
- IOB 耗尽 + 协议超时 → CrashAnalysis（协议超时作为 IOB 耗尽的症状）
- 多个非 crash 症状并存 → 先 Config 排除配置问题，再按优先级选最高流程
- ETH-CAN 路由问题 → 先排查路由配置，再按入口/出口分别检查协议栈和驱动

---

## 多核竞态分析（公共模块）

横跨协议栈和驱动层的公共分析模块。与其他流程不同，它有两种进入方式：

1. **决策树直接路由**：crash 调用栈涉及 IOB + 多核特征 → 直接进入
2. **横向切入**：在执行流程 A/B/C/D/E 过程中，发现下列任一证据时自动切入本模块

**进入条件**（任一满足即进入）：
- IOB 的 io_offset 被覆写为 mempool free 魔数（UAF 特征，具体值需从源码确认）
- 同一 IOB/资源地址被多核访问
- net_lock 死锁（任务互相等待）
- 锁获取顺序不一致
- per-CPU 变量宏名错误
- DMA 与 CPU 竞争描述符
- 内存保护异常且错误地址落在 IOB buffer 范围内（IOB buffer 符号名需从源码确认）

> 注意：内存保护异常但错误地址不在 IOB 范围内时，应走 Step A4 内存保护分析，而非本模块。

**详细执行步骤**：参见 [workflow_details.md §流程 M](references/workflow_details.md#流程-m-多核竞态分析)

**参考文档**：
- IOB 生命周期详解：vela-knowledge `domain_knowledge/kernel/mempool.md`
- 已知多核竞态案例：[issues/multicore_race_issues.md](references/issues/multicore_race_issues.md)

---

## 代码修复（CP4）

所有流程确认后进入修复阶段：

1. 根因定位 → 代码定位
2. 修改逻辑设计（说明为什么这样改）
3. 生成代码 Diff：
   - ≤300 行 → 直接展示
   - \>300 行 → 摘要 + 前 50 行，完整 diff 保存到 `/tmp/network_fix_<timestamp>.patch`
4. 询问是否需要自动验证 patch：
   ```
   研发，是否需要自动验证此 patch？
   - 🔨 编译验证 → 执行增量编译，确认无编译错误
   - 🧪 编译+风格检查 → 编译 + checkpatch.sh 检查
   - ⏭️ 跳过验证 → 直接进入确认
   ```
   **验证执行方式**：

   | 验证项 | 激活 Skill | 适用平台 |
   |--------|-----------|---------|
   | 编译验证 | `vela-build` | 通用 NuttX/Vela |
   | 编译+刷写+调试验证 | `caros-board` | CarOS 平台（平台识别结果为 `CONFIG_ARCH=tricore`） |
   | 风格检查 | `code-style` | 所有平台 |

   > CarOS 平台优先使用 `caros-board`（覆盖编译、刷写、调试全流程），非 CarOS 平台使用 `vela-build`。
   > CarOS 平台验证前需询问用户板卡类型（如 tc4d9 EVB / tc489 EVB / lzcu / rzcu / mzcu 等），`caros-board` skill 依赖板卡信息选择正确的编译目标和刷写方式。

   - 验证失败时自动修正并重新展示 Diff，直到通过或用户选择跳过
   - 连续 2 次修正仍失败 → 停止自动修正，展示错误让用户决定
5. **CP4: 用户确认修复（不可跳过）** — 验证通过（或跳过验证）后，展示最终 Diff 等待用户确认
6. 可选提交：使用 `git-commit` 技能提交到 Gerrit

### 临时文件管理

patch 文件仅保存到 `/tmp/network_fix_*.patch`。

分析结束后统一清理：
```bash
for f in /tmp/network_fix_*.patch; do
  [ -f "$f" ] && realpath "$f" | grep -q '^/tmp/' && rm -f "$f"
done
```

禁止 rm 任何非 `/tmp/` 前缀的路径。

---

## Checkpoint 交互格式

统一确认模板：

```
研发，{操作描述}。确认吗？
- ✅ 确认 → {下一步}
- 📋 详情 → 显示更多细节
- ✏️ 调整 → 告诉我哪里需要改
- 🔄 返回 → 回到{上一步}
- ❌ 放弃 → 结束
```

| Checkpoint | 触发时机 | 额外选项 |
|------------|----------|----------|
| CP1 | Crash 分析完成 | "仅查看（结束）" |
| CP2 | 协议栈/配置/性能分析完成 | "补充信息"、"仅查看（结束）" |
| CP3 | 驱动/硬件诊断完成 | "生成 GDB 命令"（仅 L4 环境可执行）、"仅查看（结束）" |
| CP4 | 代码修复完成 | "显示完整 diff"（不可跳过） |
| 问题沉淀 | 任何 CP 退出时（结束前） | "沉淀到 issues"、"无需沉淀（结束）" |

### CP 状态转换

| 从 | 到 | 条件 |
|----|----|----|
| CP1 | CP4 | 用户确认根因，继续修复 |
| CP1 | 问题沉淀 | 用户选择"仅查看" |
| CP2 | CP4 | 用户确认，继续修复 |
| CP2 | CP3 | 需要深入驱动层 |
| CP2 | 问题沉淀 | 用户选择"仅查看" |
| CP3 | CP4 | 用户确认，继续修复 |
| CP3 | 问题沉淀 | 用户选择"仅查看" |
| CP4 | 问题沉淀 | 用户确认修复 |
| CP4 | CP1/2/3 | 用户选择"返回诊断" |
| 问题沉淀 | 结束 | 沉淀完成或研发确认无需沉淀 |

### 问题沉淀（所有退出路径的终止门）

无论从哪个 CP 退出，在输出最终结论后、结束对话前，**必须**输出以下模板之一。此模板是对话结束的**唯一合法出口**——未输出此模板的对话视为未完成。

**情况 A：本次问题命中已知案例（知识库匹配阶段命中）**

输出：
```
本次问题已命中已知案例 §X，无需新增沉淀。分析结束。
```

**情况 B：本次分析定位了新问题模式（issues 库未命中）**

输出：
```
研发，本次分析定位了一个 issues 库中不存在的新问题模式：
「{一句话描述问题模式}」

是否需要沉淀到 issues 库？
- 📝 沉淀 → 告诉我目标文件（如 crash_issues.md）
- ⏭️ 无需沉淀 → 结束
```

**情况 C：本次分析未能定位根因（信息不足等）**

输出：
```
本次分析未定位到明确根因，无新问题模式可沉淀。分析结束。
```

---

## ELF 版本不匹配处理

1. 用 addr2line 获取 ELF 中的源文件路径和行号
2. 对比仓库代码对应行的内容
3. 如果行号/内容不匹配：
   - 询问用户：「ELF 的构建 commit ID 是？」
   - 用户能提供 → 切换到对应版本分析
   - 用户无法提供 → 以 ELF 反汇编为准，标注"基于 ELF 反汇编分析，可能与仓库代码不一致"
4. 分析结论中必须标注 ELF 版本与仓库代码是否一致

---

## 排查优先级原则

> 本节定义的是**流程内部**的排查方向优先级——当进入某个流程（如 ProtocolStack）后，面对多个可能的排查方向时，按此优先级选择先查什么。与决策树的流程路由优先级（Crash → Config → ...）是不同层面的概念。

| 优先级 | 排查方向 |
|--------|----------|
| ✅ 高 | 配置缺失/错误（最先排除） |
| ✅ 高 | IOB 耗尽/泄漏、net_lock 死锁、多核竞态 |
| ✅ 高 | 驱动硬件配置、DMA/PHY/总线状态、中断处理 bug |
| ✅ 高 | TCP 重传/拥塞控制、CAN TX 失败、跨核通信超时 |
| ✅ 中 | ARP 缓存/超时、路由表错误、CAN 过滤器配置 |
| ✅ 中 | RPMSG 通道状态、跨核同步问题 |
| ❌ 低 | CPU 权限级别、虚拟化/Hypervisor 限制 |

### CAN/LIN 通信问题专项优先级

当决策树路由到 CAN/LIN 相关流程时，按以下顺序排查：

1. **IOB 资源** — IOB资源是否充足且匹配；单路 socket send_bufsize 是否超过 CAN 使用的 IOB 池总量
2. **驱动初始化** — ifup 时序、CAN control/trcv 同步点、BMP 模式全局变量适配
3. **中断/调度** — 中断线程负载是否过高、hpwork 是否被阻塞、net_lock 死锁
4. **路由链路** — ETH-CAN/CAN-CAN 路由的发送鲁棒性、rpmsg direct 模式稳定性、sequence_num 累加逻辑
5. **休眠唤醒** — CAN oflush 阻塞、唤醒后 LIN/CAN 通道恢复
6. **BMP 特有** — BMP 模式下全局 IOB 共享、跨核 rpmsg 零拷贝适配、无总线连接时的防护

### 换方向规则

当选定的分析流程执行 2 轮仍无实质进展时，回到决策树重新路由，按以下优先级尝试下一个方向：

1. 配置检查（Kconfig/运行时）
2. 协议栈纯逻辑（信号量/定时器/回调）
3. 驱动交互（DMA/PHY/中断/总线）
4. 硬件/环境问题

---

## 与其他技能的关系

| 优先级 | 条件 | 选择的 Skill |
|--------|------|-------------|
| 1 | crash 调用栈含网络函数（tcp_*/udp_*/enet_*/iob_*/can_*/lin_*/usrsock_*） | 本技能 |
| 2 | crash 调用栈无网络函数 | crash-analysis |
| 3 | WiFi 驱动固件问题 | 专用 WiFi skill |

| 协作技能 | 调用场景 |
|---------|---------|
| gdb-start | 运行时调试（L4 环境），GDB 会话管理 |
| crash-analysis | 通用 crash 方法论参考 |
| vela-build | CP4 patch 验证：增量编译确认无编译错误（非 CarOS 平台） |
| caros-board | CP4 patch 验证：CarOS 平台编译、刷写、调试全流程验证 |
| code-style | CP4 patch 验证：checkpatch.sh 风格检查 |
| git-commit | 修复确认后提交到 Gerrit |
| vela-config | 配置修改（Kconfig 变更） |

---

## Changelog

> **规则**：任何对本 SKILL.md 或 references/ 下文件的修改，必须在此表中追加一条记录。

| 日期 | 变更 |
|------|------|
| 2025-05-22 | 初始版本：合并 network-stack-analysis + socketcan 为全栈分析；去硬编码；动态路由加载；新增信息确认规则和问题沉淀步骤 |
| 2026-05-22 | 流程强化：问题沉淀结构化为终止门、来源标注机制、知识库缺失发现规则。去硬编码：结构优化：common/ 扁平化、流程 M 去重、CP 出口补充沉淀提示。新增 crash_issues §10 |
| 2026-06-23 | 应用层文档加载改为确定性两步（条件门 + 固定锚点 Read network_app_layer.md），动态路由降级为兜底，适配 sub-agent 上下文 R8 约束 |
| 2026-06-30 | 删壳收尾：network-analysis agent 已废弃（Gerrit 9135022），本 skill 成为网络分析主入口（直接调用 / module-dev 驱动 / bug-analyze 接力）。新增 references/triage_and_relay.md，回收原 agent 的前置分诊（非 bug 识别 + 分支同步检查）、接力模式契约、能力边界自评；决策树前增「前置分诊」节。同步修正 network-evidence-gate.md / network-root-cause-gate.md 中指向已删 agent 的悬空引用 |
| 2026-07-09 | Gate 锚点对齐：新增「全局阶段编号（Gate 锚点·单一事实源）」表，把两道 gate 引用的 Step 2.1.1/3/4/5/7 与本 skill 的分流程 Step + CP1-CP4 显式绑定，消除与 platform_detection 内部 Step 1/2/3 的编号撞车；环境分级补 L5（完全无证据）；执行顺序显式点名流程 B/C 前置「载体识别」。配套修正 network-evidence-gate.md（compile_kb 缺依赖归入 soft-fail）与两道 gate 的 Step 引用注释 |
