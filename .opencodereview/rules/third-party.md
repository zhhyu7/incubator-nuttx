<!-- 由 vela/.claude aicr-rules/build.py 生成，勿手改；改 .claude/aicr-rules/ 或 vela/.claude aicr-rules/common/ 后重新生成。common=vela/.claude@8260436d5668 src=37fb1adb605bfa8c out=531d5eb3c3620d3c -->

## 第三方代码评审规则

本目录是第三方代码，不评审代码质量，只检查下面三条。评论用中文，开头写规则编号。

#### [D2.1] 修改了第三方已有代码（medium）
修改了第三方目录下已有的源文件，而本次提交不是单独的版本升级。应向上游提交修改，或者以本地 fork 的方式单独提交并说明原因。文档和许可证文件（`*.md`、`*.txt`、`README*`、`LICENSE*`）的修改，以及新增文件，不报。

#### [D2.2] 本地 fork 的修改缺少说明（medium）
修改处没有 `local-fork:` 注释说明原因，注释里也没有上游 PR 或 issue 的链接。

#### [D2.3] 版本升级和功能修改混在一起（medium）
同一次提交既修改了第三方库的版本号（`VERSION`、`version.h` 等），又包含业务逻辑改动。版本升级应单独提交。
