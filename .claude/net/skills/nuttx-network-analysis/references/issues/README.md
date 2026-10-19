# 已知问题库

本目录按问题类型独立拆分，每个文件前 30 行为目录索引。

## 文件列表与来源

| 文件 | 内容 |
|------|------|
| crash_issues.md | Crash 类已知问题 |
| protocol_tcp_udp_issues.md | TCP/UDP 协议问题 |
| protocol_arp_icmp_issues.md | ARP/ICMP 问题 |
| protocol_can_lin_issues.md | CAN/LIN 总线协议问题 |
| driver_ethernet_issues.md | 以太网驱动问题 |
| driver_can_issues.md | CAN 驱动问题 |
| driver_lin_issues.md | LIN 驱动问题 |
| driver_rpmsg_issues.md | RPMSG/USRsock 驱动问题 |
| multicore_race_issues.md | 多核竞态问题 |
| config_issues.md | 配置类问题 |
| performance_issues.md | 性能问题 |
| ptp_issues.md | PTP 时钟同步问题 |
| sleep_wake_issues.md | 休眠唤醒问题 |
| routing_issues.md | CAN2CAN/CAN2ETH/ETH2CAN 路由问题 |

## 使用方式

1. 根据问题类型选择对应文件
2. 读取前 30 行（目录索引）
3. 按问题特征匹配章节标题
4. 命中 → 读取对应章节详细内容
5. 未命中 → 进入标准分析流程

## 新增已知问题

解决新问题后，将排查经验添加到对应的 issues 文件中：
1. 在目录索引中添加条目
2. 在文件末尾添加新章节（格式参考已有章节）
3. 包含：问题特征、原理、排查维度、修复方向（概括性指导，不写入具体配置项值、函数名、文件路径等可能过时的信息）
