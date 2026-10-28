# kernel 插件（nuttx-mp）

| 项 | 值 |
|---|---|
| 插件 / marketplace | `kernel@nuttx-mp` |
| 宿主仓 | `vela/nuttx` |
| owner | Vela 内核组 |
| 来源 | `vela/.claude` 的 `skills/` 下同名目录，原位置已删除 |

| skill | 调用名 | 作用 |
|---|---|---|
| `nuttx-coding` | `kernel:nuttx-coding` | 同步原语（spinlock / mutex / semaphore / atomic）与异步模式（work_queue 等）选型与评审 |
| `lockfree-queue` | `kernel:lockfree-queue` | 多核无锁队列（SPSC / MPSC / SPMC / MPMC / Ring）选型、性能分析与代码审查 |
| `port-thirdparty-lib` | `kernel:port-thirdparty-lib` | 开源三方库移植到 Vela/NuttX 的完整流程（目录规划、构建系统、头文件适配） |

驱动与子系统开发见同一 marketplace 的 `driver` 插件。

## 启用

`vela/.claude` 入库的 `settings.json` 已装配，`repo sync` 后在工作区根目录启动 Claude Code 即可：

```json
"extraKnownMarketplaces": {
  "nuttx-mp": { "source": { "source": "directory", "path": "./nuttx/.claude" } }
},
"enabledPlugins": { "kernel@nuttx-mp": true }
```

- 临时试用：`claude --plugin-dir nuttx/.claude/kernel`
- 个人关闭：在 `.claude/settings.local.json` 写 `"enabledPlugins": {"kernel@nuttx-mp": false}`，
  不要用 `/plugin` 面板删除（会改写入库的 `settings.json`）
- Kiro / Codex 不识别插件：直接读 `nuttx/.claude/kernel/skills/<skill>/SKILL.md`，
  或在工作区根运行 `python3 .claude/scripts/link_plugin_skills.py` 生成本地链接

skill 不放在 `nuttx/.claude/skills/`：插件 skill 统一放 `kernel/skills/`，避免同名 skill 被发现两次。

## 已知遗留

- 其他团队仓里仍有按平铺名 `nuttx-coding` 引用的地方（如 `frameworks/graphics` 的 `lvgl-coding`），调用仍可用，后续统一改为 `kernel:` 前缀。
- description 都超过 400 B：`nuttx-coding` 775 B、`lockfree-queue` 579 B、`port-thirdparty-lib` 493 B。
