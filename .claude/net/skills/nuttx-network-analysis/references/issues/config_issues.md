# 配置类问题

## 目录索引

| § | 问题特征 | 关键词 |
|---|---------|--------|
| 1 | Ethsm/Socketmgr 应用层配置问题 | Ethsm, Socketmgr, 配置 |
| 2 | 分支同步遗漏（高频：约 15%） | 分支同步, 遗漏, BMP, E3 |
| 3 | 配置宏未使能（高频：约 12%） | 配置宏, 未使能, CONFIG |

---

## 1. Ethsm/Socketmgr 应用层配置问题

### 1.1 Ethsm 设置 no_com 后仍能收包

**特征**：Ethsm 设置 VLAN 为 no_com 状态后，仍能收到该 VLAN 的报文（出现 2 次：16020/15472）

**排查步骤**：
1. 检查 Ethsm 的 no_com 实现是否调用了 ethtool channel 关闭
2. 检查网络设备上层驱动的 channel 控制是否生效
3. 可能是 Ethsm 只关闭了应用层接收但没关闭驱动层

### 1.2 Socketmgr 通信异常

**特征**：Socketmgr 配置的 TCP/UDP 连接无法正常通信或计数异常（15978/15710）

**排查步骤**：
1. 确认 Socketmgr 的 socket 创建和 bind 是否成功
2. 检查 enet_cfg 中的 Socketmgr 配置（IP/端口/类型）
3. 区分是 Socketmgr 应用层问题还是 NuttX 协议栈问题

---


## 2. 分支同步遗漏（高频：约 15% 的以太网问题）

**特征**：BMP/E3U1/E3U2 分支上出现的功能异常，在 dev-system 或 E3 分支上已修复

**典型案例**：
- VELAFORCAR-15349: TCP 重传时间异常 — E3 修改未同步到 BMP
- VELAFORCAR-15350: TCP 端口号超出范围 — E3 修改未同步到 BMP
- VELAFORCAR-15351: TCP window size 不符标准 — E3 修改未同步到 BMP
- VELAFORCAR-15308: PTP 启动方式 + 协议栈 patch 未同步到 BMP

**排查步骤**：
1. 确认问题在 dev-system 分支是否存在
2. 对比 dev-system 和当前分支的 .config 差异（重点关注网络相关配置项）
3. 检查 Gerrit 中是否有对应的 cherry-pick 记录
4. 检查 Jira 评论中是否有"E3修改未同步"、"patch未合入"等关键词

**修复方向**：
- 从 dev-system cherry-pick 对应 patch 到目标分支
- 建议 CI 增加分支间关键配置一致性检查

---

## 3. 配置宏未使能（高频：约 12% 的以太网问题）

**特征**：功能完全不可用 + 必现 + 无 crash + 新板/新分支/挪核后出现

**典型案例**：
- VELAFORCAR-16009: accp 代码中 ETH CONFIG 宏全部关闭 → ifup 不执行
- VELAFORCAR-15641: LZCU BMP 未使能信号路由配置宏 → ETH-LIN 路由不通
- VELAFORCAR-14926: xcp on eth 功能宏开在 core2 但 IP 部署在 core1
- VELAFORCAR-14911: core0 未使能 CAN 总线同步配置宏
- VELAFORCAR-13550: core2 软件路由宏未使能 → CAN 转 ETH 不通

**排查步骤**：
1. 从 .config 中提取所有 ETH/NET 相关配置项
2. 对比功能需求文档中要求使能的配置项
3. 检查多核场景下各核的配置是否一致（特别是挪核后）
4. 检查 defconfig 中是否遗漏了新增的配置项

**修复方向**：
- 在对应核的 defconfig 中使能缺失的 CONFIG_xxx
- 建议维护一份"各核必须使能的 ETH 配置清单"

---

