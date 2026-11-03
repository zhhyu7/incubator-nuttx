<!-- 由 vela/.claude aicr-rules/build.py 生成，勿手改；改 .claude/aicr-rules/ 或 vela/.claude aicr-rules/common/ 后重新生成。common=vela/.claude@8260436d5668 src=37fb1adb605bfa8c out=531d5eb3c3620d3c -->

## Vela Kconfig 评审规则

- 评论用中文，开头写规则编号，如 `[D3.1]`。只报本次 diff 新增或修改的配置项，写出具体的配置项名。
- 不报格式、缩进和 help 文本措辞。

#### [D3.1] 用 select 强行拉起依赖（medium）
新增的 `select X` 让依赖在本选项开启时被强制打开，应改为 `depends on X`。`choice` 块内的结构性 `select` 除外。

#### [D3.2] 一个大开关打开多个子特性（medium）
新增的单个选项一次启用多个相互独立的子特性，用户无法单独选择。应拆成细粒度选项，大开关通过 `default` 带动子选项。

#### [D3.3] 只用负向架构依赖（medium）
用 `depends on !ARCH_X` 表达适用范围，却没有列出支持的架构，应写成 `depends on ARCH_Y || ARCH_Z`。

#### [D3.4] 通用模块里写死板级参数（medium）
通用驱动或子系统的 Kconfig 里，`default` 写死了板级的值（地址、MTU、引脚、频率）。应在通用 Kconfig 里不给默认值或给中性值，在板级 Kconfig 里用 `default <v> if ARCH_BOARD_XXX` 设置。

#### [D3.5] 选项未按字母序插入（low）
同一组按字母序排列的选项中，新增的选项没有插入到对应位置。原本就不是字母序的列表不报。

#### [D3.7] 循环依赖（high）
新增的 `depends on` 与已有依赖形成环。

### nuttx 仓库专项

#### [K-NX-01] defconfig 不是 savedefconfig 的输出（medium）
适用条件：变更的是 `boards/**/defconfig`。
新增的配置行没有按字母序插入，或新增了等于 Kconfig 默认值的行。defconfig 应由 `make savedefconfig` 生成，不要手工追加在末尾。

#### [K-NX-02] 删除或改名选项后残留引用（medium）
本次删除或改名了 Kconfig 选项，但本次其他变更文件里没有同步修改仍引用旧名称的 defconfig、源码、头文件或构建文件，旧配置会被静默忽略。
