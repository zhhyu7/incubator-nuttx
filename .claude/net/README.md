# net 插件（net-mp）

| 项 | 值 |
|---|---|
| 插件 / marketplace | `net@net-mp` |
| 宿主仓 | `vela/nuttx` |
| 来源 | `vela/.claude` 的 `skills/nuttx-network-analysis`、`skills/network-bug-card`，原位置已删除 |

| skill | 调用名 | 作用 |
|---|---|---|
| `nuttx-network-analysis` | `net:nuttx-network-analysis` | NuttX 网络全栈（协议栈/CAN/LIN/以太网/USRsock-RPMSG）分析 |
| `network-bug-card` | `net:network-bug-card` | 网络 bug 修复后经验沉淀 |

## 启用

`vela/.claude` 入库的 `settings.json` 已装配，`repo sync` 后在工作区根目录启动 Claude Code 即可
（首次启动只注册 marketplace，第二次启动起 `net:` skill 才出现）：

```json
"extraKnownMarketplaces": {
  "net-mp": { "source": { "source": "directory", "path": "./nuttx/.claude" } }
},
"enabledPlugins": { "net@net-mp": true }
```

- 临时试用：`claude --plugin-dir nuttx/.claude/net`
- 个人关闭：在 `.claude/settings.local.json` 写 `"enabledPlugins": {"net@net-mp": false}`，
  不要用 `/plugin` 面板删除（会改写入库的 `settings.json`）
- Kiro / Codex 不识别插件：直接读
  `nuttx/.claude/net/skills/nuttx-network-analysis/SKILL.md`

skill 不放在 `.claude/skills/`：那里会被 Claude Code 当作项目 skill 再发现一次，出现重名。
