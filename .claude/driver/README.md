# driver 插件（nuttx-mp）

| 项 | 值 |
|---|---|
| 插件 / marketplace | `driver@nuttx-mp` |
| 宿主仓 | `vela/nuttx` |
| owner | Vela 内核组（`gnss` 原归传感组，联系人 wangjianyu3） |
| 来源 | `vela/.claude` 的 `skills/` 下同名目录，原位置已删除 |

| skill | 调用名 | 作用 |
|---|---|---|
| `nuttx-driver-development` | `driver:nuttx-driver-development` | NuttX 驱动的创建、修改、评审：upper/lower-half 架构，按 Driver Type Dispatch Table 加载各子系统 pattern |
| `gnss` | `driver:gnss` | GNSS 子系统：`gnss_uorb` 框架、fakesensor 回放、板级初始化与芯片适配 |

`nuttx-driver-development` 的分步工作流由共享仓的 `driver-workflow` agent 驱动，状态与校验脚本在
`nuttx/.claude/driver/skills/nuttx-driver-development/scripts/`（从工作区根执行）。
驱动代码评审是 `quality-ops:driver-code-reviewer`，它按需读取本插件的 `references/`。

## 启用

`vela/.claude` 入库的 `settings.json` 已装配，`repo sync` 后在工作区根目录启动 Claude Code 即可：

```json
"extraKnownMarketplaces": {
  "nuttx-mp": { "source": { "source": "directory", "path": "./nuttx/.claude" } }
},
"enabledPlugins": { "driver@nuttx-mp": true }
```

- 临时试用：`claude --plugin-dir nuttx/.claude/driver`
- 个人关闭：在 `.claude/settings.local.json` 写 `"enabledPlugins": {"driver@nuttx-mp": false}`，
  不要用 `/plugin` 面板删除（会改写入库的 `settings.json`）
- Kiro / Codex 不识别插件：直接读 `nuttx/.claude/driver/skills/<skill>/SKILL.md`，
  或在工作区根运行 `python3 .claude/scripts/link_plugin_skills.py` 生成本地链接

## 迁移后必做

- `references/verification_checklist.md` 里给出的 hook 示例已改为新路径。已经照旧示例把
  `.claude/skills/nuttx-driver-development/scripts/...` 写进个人 hook 配置的，需要改成新路径。

## 已知遗留

- 其他团队仓里仍有按平铺名 `nuttx-driver-development` 引用的地方（如 `frameworks/graphics`），调用仍可用，后续统一改为 `driver:` 前缀。
- description 都超过 400 B：`nuttx-driver-development` 1130 B、`gnss` 596 B。
