<!-- 由 vela/.claude aicr-rules/build.py 生成，勿手改；改 .claude/aicr-rules/ 或 vela/.claude aicr-rules/common/ 后重新生成。common=vela/.claude@8260436d5668 src=37fb1adb605bfa8c out=531d5eb3c3620d3c -->

## Vela 构建文件评审规则

- 评论用中文，开头写规则编号，如 `[D3.14]`。只报本次 diff 引入的问题，不报格式。

#### [D3.14] CMake 与 Make 构建文件未同步（medium）
同一目录下的 `CMakeLists.txt` 与 `Makefile`/`Make.defs` 只改了其中一个：新增或删除源文件、宏定义、include 路径、依赖库时，两套构建都要改。先查看本次变更文件列表，确认另一个文件是否也改了；改了就不报。该目录只有一套构建文件时不报。

#### [D3.15] 在普通构建文件中直接链接工具链库（medium）
非 toolchain 文件中直接用 `target_link_libraries` 等方式链接工具链库（如 libm、libgcc），应在 toolchain 文件中处理，由负责该库的 CMakeLists 对外提供。

#### [B-01] 源文件没有受配置开关控制（medium）
新增的源文件或子目录没有用对应的 `CONFIG_*` 条件包起来，关闭该功能时仍会被编译。

### nuttx 仓库专项

#### [B-NX-01] 真实文件或目录目标被标为 .PHONY（medium）
`.PHONY` 中列出了对应真实文件或目录的目标，make 会每次都重新执行它的规则并带动依赖它的目标重建。`.PHONY` 只用于不产生同名文件的目标。
