---
name: network-bug-card
description: "Use when 网络 bug(NuttX协议栈/CAN/LIN/以太网驱动/ETH-CAN路由/CarOS应用层通信)已修复后(已有 fix diff)要沉淀经验、或批量回溯存量已修 JIRA 挖网络 bug 规律、或对已存网络经验卡做 A/B 验证。强调事后/修复后从真实 fix 反推根因。Not for: crash 经验(用 experience-generator)、图形经验(用 graphics-bug-card)、正在排查/修复网络 bug 本身(用 nuttx-network-analysis)、未修复无 fix diff 的问题。"
---

# 网络 Bug 经验卡沉淀

把网络 bug 的解决经验，从真实 fix 反推根因，沉淀成经验卡（写入 `.claude/experience-network/units/`），并用 A/B 验证卡是否真有用。与 `graphics-bug-card` 同构，共享元方法，仅领域知识不同。

## Overview

| | |
|---|---|
| **职责** | 从真实 fix 反推网络 bug 根因，沉淀成可 A/B 复用的经验卡 |
| **触发时机** | 修复后（已有 fix diff），不是排查/修复中 |
| **两种模式** | 模式 A 批量回溯（挖存量已修 JIRA） / 模式 B Post-Fix Harvest（刚修完一个被调起） |
| **入库真相源（唯一）** | **任何卡都先落 `candidates/`，A/B 通过才升级进 `units/`** |
| **Agent 边界** | agent 只能写 candidates/，**永不直接写 units/**；正式入库必须人审 |
| **依赖** | `pyyaml`（compile_kb.py 用）；`$GERRIT_USER` 环境变量；JIRA REST 凭据 |

## When to Use

- ✅ 网络 bug 已修复且有 fix diff（commit / gerrit change），要沉淀经验
- ✅ 批量回溯存量已修网络 JIRA，挖共性根因模式
- ✅ 对已存网络经验卡做 A/B 验证（B 组有卡 vs A 组无卡）
- ✅ 由 `nuttx-network-analysis` 在 Step 6 收尾时调起（模式 B 自动入口）

## When NOT to Use

- ❌ **正在排查或修复网络 bug 本身** → 用 `nuttx-network-analysis`
- ❌ crash 经验沉淀（即使栈含网络函数也不在此） → 用 `experience-generator`
- ❌ 图形 bug 经验 → 用 `graphics-bug-card`
- ❌ 未修复 / 没有 fix diff 的问题 — 没有真实 fix 反推不了根因，止步于 outcome
- ❌ systematic-debugging 类调试方法论咨询 — 那是过程指导，不是事后沉淀

## 通用精华（执行时硬约束，自包含）

1. **一个根因机制 = 一张卡**。现象（ping 不通 / ENOBUFS / 丢帧 / link down）是**路由桶**，根因机制才是**卡**。按根因聚类，不按现象。
2. **泛化优先**：卡沉淀的是**模式**，不是单个 bug；patch 的具体位置、`CONFIG_xxx` 值、函数名会变，根因模式不变；具体 case 只是佐证。
3. **卡的"度"判定**：这些 case 是否共享「同一失真层 + 同一排查路径」？是→一张卡（子模式）；否→拆多张。
4. **A/B 当尺子定粒度**：B 组（有卡）若"对域错机制"（到了对的层但选错子机制）→ 卡太宽，拆；若很少命中新 case → 太窄，泛化上去。取"B 组准确率仍高的最宽粒度"。

## 网络专属原则（换个垂域就不成立，必读）

- **"ping 不通 / link down"是万能症状词**：分支同步遗漏、配置宏未使能、PHY 未上电、Switch 固件、协议栈 bug 都黑成不通。**先剔除非软件因素**（电流值 0.15A vs 0.27A、换台架是否复现）再进协议栈。
- **配置类是第一性排查方向**（实测约 27%：分支同步 15% + 配置宏未使能 12%）。非主线分支 / 挪核 / 新板的网络异常，先查 `.config` 差异与 cherry-pick 状态，而不是直接钻协议栈源码。
- **三层栈模型**：
  - CarOS 应用层（仅 CarOS）：`MiRT/SocketMgr/DoIP/EthTsyn/EthSM/SwtMgr/Gateway/SigRoute/E2E`
  - NuttX 协议栈：`tcp_*/udp_*/iob_*/arp_*/icmp_*/devif_*`
  - 驱动层：`enet_*/can_*/lin_*` PHY/DMA/MAC/中断
- **失真层定位铁律**：应用层逻辑**优先于**协议栈判断；应用层调了 NuttX API 时，必须先穿透追踪 API 在协议栈内的完整路径再下结论。
- **设计根因 = "BMP/AMP 范式阻抗"**：AMP 遗留跨核调用 / 未隔离驱动全局变量，在 BMP 共享地址空间下是高频 bug 面（CAN 不外发、中断 work 慢）。
- **IOB 是跨层共享资源**：耗尽（配置/泄漏）与多核 UAF（生命周期）是两类根因，不能合一张卡。

### 已识别根因家族（taxonomy，持续扩充）

| 家族 | 失真层 | 排查路径要点 | 状态 |
|---|---|---|---|
| 分支同步遗漏 | 配置/构建 | dev-system 已修 → 当前分支缺 cherry-pick | 已成卡 `NET-branch-sync-missing-bmp` |
| 配置宏未使能 | Kconfig/defconfig | 多核各核 CONFIG 不一致 | 已成卡 `NET-config-macro-not-enabled` |
| IOB 池耗尽 | 协议栈/驱动配置 | ENOBUFS + navail 单向降 vs 回升 | 已成卡 `NET-iob-pool-exhaustion-config` |
| IOB 多核 UAF | 驱动/协议栈生命周期 | ifdown 时 pending TX 访问已释放 IOB | 已成卡 `NET-iob-multicore-uaf-lifecycle` |
| BMP 全局变量未隔离 | 驱动 | AMP 正常 BMP 异常 + regression | 已成卡 `NET-can-bmp-global-var-not-isolated` |

> 新族判定：先按根因机制聚类（不按现象），用"卡的度判定"决定并入已有卡还是新建。

## 工作流（两种模式，P4 起共用）

### 模式 A — 批量回溯（从存量已修 JIRA 成批挖）

```
P1 取数：按【现象关键词 + 网络组件】全项目检索，过滤 crash/test/feat/perf 与非软件噪声
        ⚠️ 仅纳入"已有 fix diff"的 JIRA；无 fix 的进 outcome 不进卡
P2 反推：gerrit query 拿 repo/ref → git fetch → git show 取 diff → 从"改了什么"反推根因
P3 聚类：按【根因机制】聚类 → ⟨决策门：人确认选哪一族⟩
   选族优先级（卡价值 ∝ 症状歧义度）：优先选**症状歧义、AI 易跑错层**的族
   （如"link down 实为 Switch 未上电"——无卡 AI 会去钻 PHY/协议栈；"分支同步遗漏"——
    无卡 AI 会逐行读协议栈代码）；**自解释症状**族做卡性价比低，可缓做。
```

> ⚠️ **入库门槛与模式 B 统一**：模式 A 在 P3 选好族后，进入共用后半段——P4 定度、**P5 落 candidates/**、P6 A/B、人审通过才升 units/。**绝不直写 units/，绝不在 P5 跑 compile_kb 进路由表**。所有 agent 自动产出走同一道闸门。

### 模式 B — 单沉淀 / Post-Fix Harvest（刚修完一个 bug，由 nuttx-network-analysis 的收尾阶段调起）

> 闭环后半段：`Fix → Feedback → Experience`。被 nuttx-network-analysis Step 6 收尾调起。
> **核心姿势：运行强制、默认不建卡、自动只产候选（草稿+outcome），不自动入正式库。** 真正升级进 `units/` 必须过人审 + A/B 闸门。
> **触发条件：commit 后总是先跑 H0（无条件）；仅当 `root_cause_found 且有 fix diff` 才进 H1 沉淀分支——没定位出根因/没改动的 bug 止于 H0，不建卡。**

```
H0 记 outcome（强制，零摩擦）── 复用 experience-base/append_outcome.py，写全局 _outcomes.jsonl
   填 experience_interaction: cards_injected / cards_adopted / cards_contradicted
      / cards_missed / routing_status —— 这就是 routing_feedback
H1 沉淀判定（默认 no_card）── 按下方决策表，多数单点 fix 到此为止只留 outcome
H2 按机制查重（不按现象！）── 用根因机制（失真层+排查路径）匹配现有卡，不能只看现象命中
H3 起草到 candidates/（不进 units）── 按 _template.draft.md 生成草稿，泛化段/negative/risk/验证留 TODO
   P1' 拿现象+已有 fix diff（跳过取数/聚类）  P2' 从 diff 反推根因 → 填进草稿
H4 移交人审 → 升级闸门 ── 草稿停在 candidates/，人精修后才走共用后半段（P4 定度 → P5 落 candidates/ → P6 A/B → P7 人审升级 units/ + compile_kb）
   ⟨决策门：A/B 通过且人审通过才把卡从 candidates/ 升级进 units/；不通过则退回或丢弃⟩
```

**沉淀判定决策表（H1，默认偏向不建卡）**：

| 修复结果 | 处理 | 落点 |
|---|---|---|
| 命中卡 + 按卡定位成功 | 更新该卡 evidence/案例/验证命令 | candidates/（标 `update:<id>`） |
| 命中卡 + 卡误导了方向（contradicted） | 给该卡加 negative / 陷阱 | candidates/（标 `update:<id>`） |
| router 没命中 + bug 有可泛化新机制 | 新建候选卡 | candidates/ 新草稿 |
| router 没命中 + 一次性 typo/纯配置笔误/外部偶发 | **不建卡**，只留 outcome | 仅全局 _outcomes.jsonl |
| 多张卡冲突 | 记 routing_feedback，提议调权重/边界 | _outcomes.jsonl + candidates/feedback 备注 |

### 共用后半段（A/B 两模式都走，唯一入库真相源）

> 模式 A 挖完进入此段；模式 B 的草稿停在 `candidates/`，**人审后**才走此段。两模式都遵循"**candidates → A/B → 人审 → units**"的同一道闸门，agent 全程不直写 units/。

```
P4 定度：用"卡的度判定" → 先匹配现有卡（experience-network/units）；
        命中→当 case 并入（可留作 A/B hold-out）；不命中→新建草稿
        ⟨决策门：人确认卡边界（并/新建）⟩
P5 起草到 candidates/：按 _template.draft.md 写卡/改卡到 candidates/<id>.draft.md
        ⛔ 此步不跑 compile_kb，不动 units/，路由表暂不收录此卡
P6 A/B 当尺子：留同族 hold-out（不写进卡+剥离泄题）→ A无卡/B有卡 subagent → 对照真实 fix 打分
        通过判据（同时满足）：
          ① B-A 净准确率提升 ≥ +1.0/题（满分 2/题）
          ② B 组绝对准确率 ≥ 50%（精确命中失真层 + 机制方向）
          ③ B 组无"对域错机制"系统性偏差（>20% 命中卡但选错子机制 → 卡太宽，回 P4 拆）
        命中率应对：B 组命中率 < 30%（10 道里命中 ≤ 3）→ 卡太窄，回 P4 泛化上去
P7 人审升级：A/B 通过 + 人审通过 → 把 candidates/<id>.draft.md 升级为 units/<id>.md
        → 跑 compile_kb.py 重新编译路由表
        ⟨决策门：人最终拍板，A/B 不通过则退回精修或丢弃⟩
```

**两模式入库唯一路径**（不可旁路）：

```
agent 产出 → candidates/<id>.draft.md → A/B 验证 → 人审 → units/<id>.md → compile_kb
                                              ↑
                              这是唯一闸门，模式 A 模式 B 都走这条
```

## 依赖与环境

| 依赖 | 用途 | 检查/安装 |
|---|---|---|
| Python 3.6+ | 跑 compile_kb.py / append_outcome.py | `python3 --version` |
| `pyyaml` | compile_kb.py 解析 frontmatter / 写路由表 | `python3 -c "import yaml"`；缺失 → `pip install pyyaml` |
| `$GERRIT_USER` | gerrit query 反推 ref | `echo $GERRIT_USER`；空则需配置 SSH key + 用户名 |
| `$GERRIT_HOST` | 评审主机（默认 `gerrit.pt.mioffice.cn`） | 跨项目时显式 export |
| `$GIT_HOST` | git 主机（默认 `git.mioffice.cn`） | 老项目仓 fetch 用 |
| `$JIRA_CAR` | JIRA 实例（VELAFORCAR-*） | 取数前确认 |
| JIRA REST 凭据 | 拉 issue + 评论 | 跑取数前先 `curl` 验证 200 OK |
| Gerrit SSH 密钥 | `ssh -p 29418 ...` query | `ssh -p 29418 $GERRIT_USER@$GERRIT_HOST gerrit version` 验证 |

> ⚠️ 任一缺失 → 先补齐再进入工作流。模式 A 取数批量阻断在 JIRA/Gerrit 凭据上是常见踩坑点。

## 取数 recipe（内置踩过的坑）

> **环境占位符**：`$GERRIT_USER`=gerrit 用户名；`$GERRIT_HOST`=评审主机（默认 `gerrit.pt.mioffice.cn`）；`$GIT_HOST`=git 主机（默认 `git.mioffice.cn`）；`$JIRA_CAR`=车载 JIRA 实例（VELAFORCAR-*）。

- **现象关键词**（网络）：ping不通/link down/丢帧/断流/ENOBUFS/发送失败/收不到/重传/时延/吞吐低/不外发/Error 12/序列号
- **组件过滤**：`component in (ETH,以太网,网络,CAN,LIN,网络协议栈)` 滤掉 crash/系统噪声
- **非软件因素先剔除**：电流值异常（Switch 未上电）、换台架不复现、回退旧版本正常——这些不进卡（不是软件根因机制）
- **JQL 坑**：`issuetype=Bug`（用英文）；中文字段值加引号
- REST：`GET /rest/api/2/search?jql=...&fields=summary,components,status`；`/issue/{key}?fields=description,comment` 取现象与 gerrit 链接

## 反推 recipe（内置踩过的坑）

- **change→仓库/ref**：`ssh -p 29418 $GERRIT_USER@$GERRIT_HOST "gerrit query --format=JSON --current-patch-set --commit-message change:<n>"`
- **取 diff**：本地仓 `git fetch vela <ref>` → `git show FETCH_HEAD`
- **分支同步类特判**：网络 bug 大头是分支同步遗漏，fix 可能是一条 cherry-pick——反查 dev-system 上的原始 commit 才是真根因，目标分支的 pick commit 只是搬运。
- **必须读真实 diff 反推根因，禁止从 description 猜根因**

## 经验卡格式（experience-network，自包含要点）

- 位置：草稿落 `.claude/experience-network/candidates/<id>.draft.md`；A/B + 人审通过后**升级**到 `.claude/experience-network/units/<id>.md`，再跑 `python3 .claude/experience-network/compile_kb.py` 重新编译路由表。**agent 不直接写 units/，不直接跑 compile_kb。**
- frontmatter：`id/title/type/status/version/created/updated` + `layer`（失真层 app/protocol/driver/config，**仅标注不进强匹配**）+ `routing.triggers`（strong/supporting/negative 加权 tag）+ `contributor` + `source_doc`（挂真实 JIRA/change，可追溯+A/B 真值）
- **tag category 词表**（compile_kb 校验，超出会报错）：`symptom: type: net: config: ctx: func: evidence: behavior: mem: val: layer:`
  - `net:` 协议/载体（tcp/udp/can/lin/eth/arp/icmp/ptp）
  - `config:` 配置特征（macro_disabled/branch_sync_missing/defconfig）
  - `type:` 根因类型（iob_exhaustion/can_send_fail/eth_linkdown/branch_sync_missing）
  - `ctx:` 环境（caros_bmp/amp_multicore/branch_e3u1/multi_netdev）
  - **`layer:` 不当强触发**（那是分析结论）
- **tag 只用可观察项**（从日志/现象/Jira 文字能看到）：`symptom:`/`net:`/`type:`/`evidence:`/`behavior:`
- 正文：模式识别（含设计根因一句）→ 可泛化模式（不绑 CONFIG 值/函数名/行号的判据+根因机制命名）→ 排查路径（含**现象→子模式判别器**：日志关键字 / .config 比对项 / 分支 cherry-pick 检查 / 协议栈穿透路径）→ **修复约束/风险**（改这块别踩什么：兼容性/影响面/不可改的东西）→ **验证命令**（ping/iperf/cansend/candump + ifconfig/proc 计数；无台架环境留"待补"，不编造）→ 陷阱（含 negative）→ 已验证案例（带 JIRA）

## A/B recipe（内置踩过的坑）

- **hold-out 必须同族但不写进卡**，且**剥离泄题**（去 gerrit 链接/comments/JIRA 自带分析；卡里子模式写抽象不泄具体机制），否则 B 组 = 开卷
- A 组（仅现象）/ B 组（现象+卡）各起独立 subagent，**禁联网/禁查 JIRA·Gerrit/禁 git log·show 找现成修复**
- 打分对照真实 fix：精确=2 / 失真层+方向对漏具体=1 / 错=0；净效果=B−A
- 样本荒应对：网络配置类有真实 fix 的 hold-out 稀缺 → 可"写卡 cite 少、hold-out 留多"（如 3+3）
- 报告存 `.claude/experience-network/reports/`；运行存档存 `reports/ab-runs-<date>/`

## 决策门（人介入点，其余自动）

1. P3 选哪一族  2. P4 卡边界（并入/新建）  3. P6 审 A/B 结果（拆/并/通过）
4. **P7 人审升级**：candidates/ 草稿是否精修并过 A/B 后升级进 units/
5. **(模式 B / Post-Fix) H4 移交人审**：与 P7 是同一道闸门——agent 自动产出停在 candidates/，由人决定是否进闸门

> 模式 B 的 H0 记 outcome、H1 沉淀判定、H2 查重、H3 起草草稿**全自动**（默认 no_card）；人在 H4/P7 这道唯一升级门介入——保证 agent 不污染正式库 units/。

## 与 nuttx-network-analysis issues/ 的关系

- `nuttx-network-analysis/references/issues/*.md` 是**老式领域知识库**（150+ 章节，按类型分文件，无 frontmatter）——它是本经验库的**素材来源**：批量回溯（模式 A）可从这些章节反推、迁移成标准卡。
- `nuttx-network-analysis` 是**在线分析消费者**：Step 2 经 experience-router 注入本库命中卡，Step 6 收尾经模式 B 反哺。
- 两者长期目标是把 issues/ 的高价值章节逐步迁入 units/，统一到路由表 + A/B 体系。
