# 平台识别与工具链初始化

> 本文档在分析开始时加载，完成工作目录校验、平台识别和工具链初始化。

---

## 工作目录校验（必须首先执行）

确认当前目录是 NuttX 项目根目录：

```bash
if [ -d nuttx ] && [ -d vendor ]; then
  echo "✅ NuttX 项目目录（含 vendor/）"
elif [ -d nuttx ] && [ ! -d vendor ]; then
  echo "⚠️ NuttX 项目目录（无 vendor/），部分板级分析功能受限"
else
  echo "❌ 错误：当前目录不是 NuttX 项目根目录"
  echo "请切换到正确目录后重试"
fi
```

> 仅校验"是否为 NuttX 项目"，不在此步判断具体平台。

---

## 平台识别

### 核心原则

**以 `.config` 中的 `CONFIG_ARCH` / `CONFIG_ARCH_CHIP` 为唯一判据。**

本仓库是统一仓库（CarOS + Vela 共存），不能用 `build_whole.sh` 或 `envsetup.sh` 的存在性判断平台——它们始终同时存在。

### Step 1：确定 .config 路径

```bash
# 优先从用户提供的 ELF 路径反推（ELF 和 .config 在同一目录）
# 示例：out/build/cmake_out/vendor_micar_zcu_mann_rzcu_tc489_bmp_b0/flat_gcc_bmp/.config

# 搜索所有可用的 .config
find out/build/cmake_out/ -name ".config" -type f 2>/dev/null
```

若有多个 .config，需要用户确认分析目标：
- 用户提供了 ELF 路径 → 取同目录的 .config
- 用户提供了板级名称 → 按名称匹配目录
- 都没有 → 列出可选项让用户选择

### Step 2：从 .config 读取架构信息

```bash
grep "^CONFIG_ARCH=" <.config_path>
grep "^CONFIG_ARCH_CHIP=" <.config_path>
```

### Step 3：确定平台和芯片

| CONFIG_ARCH | CONFIG_ARCH_CHIP | 平台 | 厂商 |
|-------------|-----------------|------|------|
| `tricore` | `tc4x` | CarOS | Infineon |
| `arm` | `stm32f4`, `stm32h7` | Vela/CarOS | STMicro |
| `arm` | `imxrt` | Vela | NXP |
| `arm` | `fc4150`, `fc7300` | CarOS | Flagchip |
| `arm` | `sr6g6` | CarOS | STMicro (Stellar) |
| `arm` | `e3650` | CarOS | Semidrive |
| `risc-v` | — | Vela | — |

### .config 不可用时的降级策略

仅当无法获取 .config 时，按以下优先级推断：

1. **ELF 路径推断**：路径中通常包含板级名称
   ```bash
   # 示例：out/build/cmake_out/vendor_micar_zcu_mann_rzcu_tc489_bmp_b0/flat_gcc_bmp/vela_bmp.elf
   #       → tc489 → Infineon TriCore
   ```
2. **询问用户**：「请提供板级名称或 .config 路径」

不得基于仓库根目录的文件/目录存在性猜测平台。

---

## 编译产物路径

| 构建系统 | ELF 路径模式 | .config 路径模式 |
|---------|-------------|-----------------|
| CMake (CarOS) | `out/build/cmake_out/<board_dir>/<variant>/*.elf` | `out/build/cmake_out/<board_dir>/<variant>/.config` |
| Vela (lunch+m) | `out/<product>/*.elf` 或 `out/<product>/nuttx` | `out/<product>/.config` |

> ⚠️ BMP 架构：多核共用同一个 ELF 文件（`vela_bmp.elf`），所有核的符号都在一个 ELF 里，不需要按核查找不同 ELF。

---

## 工具链初始化

平台识别完成后尝试定位工具链。找到则可用于符号解析，找不到则跳过（后续需要时再向用户索取路径）。

### 查找规则

根据 Step 3 确定的架构选择工具链：

| CONFIG_ARCH | 工具前缀 | 搜索路径 |
|-------------|---------|---------|
| `tricore` | `tricore-elf-` | `prebuilts/gcc/linux-x86_64/tricore-elf/bin/` |
| `arm` | `arm-none-eabi-` | `prebuilts/gcc/linux-x86_64/arm-none-eabi/bin/` |
| `risc-v` | `riscv64-unknown-elf-` | `prebuilts/tools/linux/x86_64/` |

### 执行方式

```bash
# TriCore
find prebuilts/gcc/linux-x86_64/tricore-elf/bin -name "tricore-elf-addr2line" 2>/dev/null

# ARM
find prebuilts/gcc/linux-x86_64/arm-none-eabi/bin -name "arm-none-eabi-addr2line" 2>/dev/null

# RISC-V
find prebuilts/tools/linux/x86_64 -name "riscv64-unknown-elf-addr2line" 2>/dev/null

# 兜底：系统 PATH
which tricore-elf-addr2line arm-none-eabi-addr2line riscv64-unknown-elf-addr2line 2>/dev/null
```

### 找不到时的处理

询问用户：「未找到工具链，请提供 addr2line 的完整路径」

不得跳过符号解析步骤而直接猜测函数名。

### 常用命令

```bash
# 地址转源码
<ADDR2LINE> -e <ELF> -f -C -p <ADDRESS>

# 符号查询
<NM> -n -S <ELF> | grep <SYMBOL>

# 反汇编
<OBJDUMP> -d -S <ELF> | grep -A 50 "<FUNC>:"

# 内存布局（CarOS/TriCore）
grep -E "START|SIZE" out/generated/memory_layout/memory_layout.h

# 源码搜索
find nuttx/net/{tcp,udp,arp,icmp,can,devif,netdev,usrsock} -name "*.c" -exec grep -l "<KEYWORD>" {} \;
```
