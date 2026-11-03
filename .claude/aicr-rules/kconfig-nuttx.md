### nuttx 仓库专项

#### [K-NX-01] defconfig 不是 savedefconfig 的输出（medium）
适用条件：变更的是 `boards/**/defconfig`。
新增的配置行没有按字母序插入，或新增了等于 Kconfig 默认值的行。defconfig 应由 `make savedefconfig` 生成，不要手工追加在末尾。

#### [K-NX-02] 删除或改名选项后残留引用（medium）
本次删除或改名了 Kconfig 选项，但本次其他变更文件里没有同步修改仍引用旧名称的 defconfig、源码、头文件或构建文件，旧配置会被静默忽略。
