---
name: port-thirdparty-lib
description: |
  移植开源三方库到 Vela/NuttX 平台的完整工作流。当用户在 Vela/NuttX 平台上进行以下操作时使用：
  移植/集成第三方开源库、添加 external 代码、解决三方库编译问题、配置构建系统（Kconfig/Makefile/CMake）、
  处理头文件冲突、适配 config.h。即使仅提及 "port"、"third-party"、"external/"、"三方库"、
  "移植库" 等关键词也应触发。覆盖从目录规划（external/ vs apps/）到源码适配的全流程。
---

# 移植三方库到 Vela/NuttX

参考文档：[移植三方代码](https://mi.feishu.cn/wiki/We5CwT7SeisX7nk4w8kcJ90unCc)（小米内网飞书文档，需权限；无法访问时本 skill 已包含完整内容）

## 目录

- [适用范围](#适用范围)
- [放哪里](#放哪里)
- [目录结构](#目录结构)
- [构建文件怎么写](#构建文件怎么写)
- [头文件路径](#头文件路径)
- [config.h 处理](#configh-处理)
- [源码适配](#源码适配)
- [流程和申请](#流程和申请)
- [移植检查清单](#移植检查清单)
- [常见问题排查](#常见问题排查)

## 适用范围

本 skill 适用于在 Vela/NuttX 平台上移植开源三方库。需要具备：
- Vela 代码仓库访问权限
- 向 SCM（Vela 代码配置管理团队）申请外部仓库的权限（或由团队负责人代申请）

## 放哪里

两个选择，取决于是否打算贡献回 NuttX 社区：

- **不 upstream** → `external/<libname>/`（大多数情况）
- **要 upstream** → `apps/` 下对应子目录（如 `apps/testing/`、`apps/fsutils/`）

如何判断？满足以下全部条件才考虑 upstream，否则放 `external/`：
- 库功能对 NuttX 社区有通用价值（如标准协议实现、通用测试工具）
- 库许可证与 NuttX 兼容（Apache 2.0、BSD、MIT 等）
- 团队有意愿长期维护 upstream 版本

不确定时默认放 `external/`，后续再决定是否 upstream。

## 目录结构

外层放构建配置，内层放原始源码，两层分离：

```
external/<libname>/        ← 你创建的（构建配置）
├── Kconfig
├── CMakeLists.txt
├── Make.defs
├── Makefile
└── <libname>/             ← SCM clone 的原始仓库（不要改目录结构）
    └── ...
```

这样分层的原因是：原始仓库由 SCM 管理并保留完整 git history，你的构建适配和它的源码在不同的 git 层级，互不干扰。如果后续库升级版本，只需要 SCM 更新内层目录，外层构建配置通常不用动。

## 构建文件怎么写

四个文件各司其职：

| 文件 | 职责 |
|------|------|
| Kconfig | 功能开关和可裁剪配置项，让不同产品按需开启 |
| Make.defs | 一行判断：CONFIG 开了就把这个目录加入编译。`CONFIGURED_APPS` 是 NuttX 构建系统识别应用目录的变量 |
| Makefile | 具体编译逻辑：哪些 .c 文件、什么 FLAGS、入口在哪。`INCDIR_PREFIX` 是 NuttX 提供的跨平台头文件路径前缀宏 |
| CMakeLists.txt | CMake 构建系统的等价实现（Vela 支持双构建系统）。如果原始库自带 CMakeLists.txt，外层仍需创建自己的，因为 NuttX CMake 构建系统需要 `nuttx_add_library` 等特定 API |

详细模板见 `references/build-templates.md`（首次移植时复制粘贴），真实示例见 `references/examples.md`（遇到问题时对照参考）。

## 头文件路径

头文件分公有和私有，区别对待：

- **公有头文件**（其他模块要 include 的）→ 在 Make.defs 里用 CFLAGS/CXXFLAGS 导出
- **私有头文件**（库内部用的）→ 只在 Makefile 里加 CFLAGS

为什么要区分？不同库可能有同名的内部头文件（`config.h`、`utils.h` 很常见），如果都暴露到全局搜索路径，编译器会 include 到错误的文件，排查起来非常痛苦。

## config.h 处理

有些库用 `config.h` 做平台差异适配。核心原则：无论哪种方式处理 config.h，都不要把 config.h 所在目录加入 CFLAGS 的 `-I` 搜索路径，避免其他模块误 include。

处理步骤：

1. 先在 PC 上 build 原始项目，生成初始 config.h（如果原始库不提供 autoconf 脚本，可以手动创建 config.h 并根据 Vela 能力逐项填写宏定义）
2. 根据 Vela/NuttX 的能力修改（比如关掉不支持的特性）
3. 把 config.h 放在 external 外层目录，不要提交到原始库的 git 里

具体操作示例：

```
external/libfoo/
├── config.h               ← 你生成并修改的配置文件
├── Kconfig
├── Makefile
└── libfoo/                ← 原始仓库
    └── src/
        └── main.c         ← 原始代码中有 #include "config.h"
```

如果原始库源码中已有 `#include "config.h"`，有两种处理方式：

- **方式 A（推荐）**：使用 `-include` 强制预包含
  - 适用场景：原始库使用 autoconf 生成 config.h，或库频繁更新需避免 patch 冲突
  - 在 Makefile 的 CFLAGS 中添加 `-include $(APPDIR)/external/libfoo/config.h`
  - `-include` 是 GCC 编译器参数，会在编译每个源文件前自动包含指定文件，效果等同于在每个 .c 文件开头加了 `#include "config.h"`
  - 优点：不需要修改原始库源码，不污染 `-I` 搜索路径
- **方式 B**：修改源码使用相对路径
  - 适用场景：库长期稳定不更新，且团队可接受维护 patch
  - 将 `#include "config.h"` 改为 `#include "../config.h"`
  - 需要在 vela/external 仓库提交 patch

推荐方式 A，因为不需要修改原始库源码，后续库升级时不会产生冲突。

## 源码适配

Vela 兼容 POSIX 接口，所以大多数基于 POSIX 的三方库可以直接编译通过。遇到问题时：

1. 优先改 Vela 侧代码来兼容，而不是改三方库源码
2. 确实要改库源码的，遵循以下规范：
   - 修改前评估：是否可以通过 Vela 侧适配层避免修改源码
   - 提交要求：修改 patch 直接提交到 Gerrit（提交到库所在的 vela/external 仓库），commit message 中注明修改原因、影响范围、是否已 upstream
   - 每个 patch 保持原子性（一个问题一个 commit），方便后续库升级时 rebase
   - 尽量 upstream 回原始社区，减少长期维护负担
3. NuttX 社区的三方库是"现用现下载"模式（编译时 wget），Vela 用 repo 管理，需要在 Makefile 里适配下载逻辑。搜索参考：
   ```bash
   # 在 NuttX apps 仓库中搜索下载逻辑示例
   # XXX_UNPACK 是 NuttX 构建系统中三方库解压的通用模式名
   git -C apps grep -n "COREMARK_URL\|XXX_UNPACK" -- "*.mk" "Makefile"
   ```

## 流程和申请

1. 向 SCM 申请增加外部 git 仓库到 manifest 文件
   - SCM（Vela 代码配置管理团队）负责 repo manifest 维护和仓库权限
   - 联系方式：在飞书 Vela SCM 群提交申请，或联系 SCM 负责人
   - 申请需提供：原始仓库 URL、license 类型、用途说明、放置路径
   - 审批周期：通常 1-2 个工作日
   - 如果 SCM 不批准独立仓库（比如库太小或仅内部使用），可以将源码直接放在现有仓库的子目录中，跳过 manifest 申请步骤。注意：直接放入现有仓库时仍需评估 license 兼容性，避免合规风险
2. SCM 审批通过后，由 SCM 执行 git clone 原始仓库到内层目录（保留完整 history）
3. 你在外层创建四个构建文件
4. 编译验证（Make 和 CMake 双构建系统都要过），验证方法：
   ```bash
   # Make 构建验证
   make -C <board_dir>
   # CMake 构建验证
   cmake -B <build_dir> && cmake --build <build_dir>
   # 验证双构建系统 CSRCS 一致性（简单对比，可能有误报，需人工确认）
   diff <(grep -oP '\S+\.c' Makefile | sort) <(grep -oP '\S+\.c' CMakeLists.txt | sort)
   ```
5. 功能验证：
   - 如果库自带测试（如 `make test`），直接运行
   - 如果没有，编写简单验证程序：在 NuttX shell 中调用库的核心 API，确认返回值正确
   - 示例：对于 JSON 解析库，写一个解析固定字符串并打印结果的小程序注册为 NuttX 命令
6. 提交 Gerrit review

## 移植检查清单

- [ ] 确定放置位置（external/ 还是 apps/）
- [ ] 验证 license 兼容性（GPL 库禁止链接到 Apache 2.0 项目，LGPL 需动态链接）
- [ ] 评估库的依赖项（可能需要递归移植依赖库）
- [ ] 向 SCM 申请增加仓库到 manifest（或确认放入现有仓库）
- [ ] 初始版本保留完整 git history
- [ ] 外层四个构建文件都创建了（Kconfig / Make.defs / Makefile / CMakeLists.txt）
- [ ] Kconfig 总开关默认 n，endif 后有空行
- [ ] 公有头文件在 Make.defs 导出，私有头文件只在 Makefile
- [ ] 内层目录结构与原始仓库一致
- [ ] config.h 放外层，用 `-include` 预包含或相对路径 include，不加入 `-I` 搜索路径
- [ ] Make 构建通过（`make -C <board_dir>`，确认新增库参与编译无报错）
- [ ] CMake 构建通过（使用 Vela CMake 构建流程验证）
- [ ] Make 和 CMake 的 CSRCS 列表一致
- [ ] 库功能运行正常（运行自带测试或手动验证）
- [ ] 评估二进制大小影响（ROM/RAM footprint），确认在目标平台 flash 分区限制内

## 常见问题排查

| 现象 | 可能原因 | 解决方法 |
|------|---------|---------|
| 头文件找不到 | 私有头文件路径未加入 Makefile 的 CFLAGS | 在 Makefile 中添加 `CFLAGS += ${INCDIR_PREFIX}<path>` |
| 符号未定义（undefined reference） | CSRCS 漏了源文件，或依赖的其他库未开启 | 检查 CSRCS 列表是否完整，检查 Kconfig depends on |
| 其他模块 include 到错误头文件 | 私有头文件被错误地放到了 Make.defs | 将私有路径从 Make.defs 移到 Makefile |
| Kconfig 解析报错 | endif 后缺少空行 | 在最后一个 endif 后加一行空行 |
| Make 编译通过但 CMake 失败 | CMakeLists.txt 中源文件列表与 Makefile 不一致 | 用 diff 对比两边的 CSRCS，检查 CMake 最低版本要求 |
| config.h 冲突 | config.h 所在目录被加入了 `-I` 搜索路径 | 移除 `-I` 路径，改用 `-include` 预包含 |
| config.h 生成失败 | 原始库不提供 autoconf 脚本 | 手动创建 config.h，参考库文档逐项填写宏定义 |
| 库运行时异常（功能不工作/日志无输出） | PRIORITY 过低导致未调度，或 STACKSIZE 不足 | 调高 Kconfig 中的 PRIORITY 值，增大 STACKSIZE |
| 长时间运行后系统变慢 | 库存在内存泄漏 | 开启 `CONFIG_DEBUG_MM=y`，用 `mm_memdump` 排查 |
| 头文件循环依赖 | 库头文件与 NuttX 系统头文件互相引用 | 检查 include 顺序，必要时用前向声明打破循环 |
| 链接时符号顺序错误 | 静态库链接顺序不对 | 调整 Makefile 中库的链接顺序，或使用 `-Wl,--start-group ... -Wl,--end-group` |

## 库版本升级

当原始库发布新版本需要升级时：

1. SCM 更新内层目录到新版本（`git pull` 或重新 clone）
2. 检查外层构建文件是否需要调整（新增/删除的源文件、新的依赖）
3. 如果之前用方式 B 修改过源码，需要 rebase patch 到新版本：
   ```bash
   # 查看之前的修改 patch
   git -C external/<libname>/<libname> log --oneline vela/trunk..HEAD
   # rebase 到新版本，解决冲突
   ```
4. 重新编译验证（Make + CMake 双构建系统）
5. 运行功能测试确认无回归

如果用的是方式 A（`-include`），升级通常很顺利，因为没有修改原始源码。

## 参考

- `references/build-templates.md` — 四个构建文件的详细模板（首次移植时复制粘贴）
- `references/examples.md` — rtos-benchmark 完整真实配置示例（遇到问题时对照参考）
