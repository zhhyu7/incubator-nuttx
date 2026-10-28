# 构建文件模板参考

> 移植三方库时需要在外层目录创建的四个构建文件模板。
> 以 `<libname>` 代表库名，`LIB_XXX` 代表 Kconfig 宏前缀，实际使用时替换。

## 目录

- [Kconfig](#kconfig)
- [Make.defs](#makedefs)
- [Makefile](#makefile)
- [CMakeLists.txt](#cmakeliststxt)
- [公有头文件导出](#公有头文件导出)

---

## Kconfig

```kconfig
config LIB_XXX
        tristate "XXX library"
        default n

if LIB_XXX

config LIB_XXX_PROGNAME
        string "Program Name"
        default "xxx"

config LIB_XXX_STACKSIZE
        int "Stack size"
        default DEFAULT_TASK_STACKSIZE

config LIB_XXX_PRIORITY
        int "Task priority"
        default 100

endif

```

要点：
- 总开关默认 `n`，因为三方库通常不是所有产品都需要，默认关闭避免增大 image
- 优先级 100 是 NuttX 的常规应用优先级
- 栈大小用 `DEFAULT_TASK_STACKSIZE`（通常 2048），适用于轻量级 CLI 工具；IO 密集型库（如 file parser、网络协议栈）可能需要 8192 或更高
- `endif` 后必须有一行空行（如上模板所示，endif 和代码块结束符之间有空行），否则 Kconfig 解析器会报错

---

## Make.defs

```makefile
ifneq ($(CONFIG_LIB_XXX),)
CONFIGURED_APPS += $(APPDIR)/external/<libname>
endif
```

这个文件的唯一作用是告诉上层构建系统"当 CONFIG_LIB_XXX 开启时，把这个目录加入编译"。它必须被上层目录的 Make.defs include 进去才能生效。

---

## Makefile

```makefile
include $(APPDIR)/Make.defs

PROGNAME  = $(CONFIG_LIB_XXX_PROGNAME)
PRIORITY  = $(CONFIG_LIB_XXX_PRIORITY)
STACKSIZE = $(CONFIG_LIB_XXX_STACKSIZE)
MODULE    = $(CONFIG_LIB_XXX)

# 私有头文件路径
CFLAGS   += ${INCDIR_PREFIX}<libname>/include

# 源文件列表
CSRCS    += <libname>/src/file1.c \
            <libname>/src/file2.c

# 主函数入口（如果库有可执行程序）
MAINSRC   = <libname>/src/main.c

include $(APPDIR)/Application.mk
```

如果库是纯静态库没有 main 入口，去掉 PROGNAME/PRIORITY/STACKSIZE/MAINSRC，只保留 CSRCS。

---

## CMakeLists.txt

```cmake
if(CONFIG_LIB_XXX)

  set(CSRCS
      <libname>/src/file1.c
      <libname>/src/file2.c)

  set(INCDIR <libname>/include)

  # 静态库
  nuttx_add_library(<libname> STATIC)
  target_sources(<libname> PRIVATE ${CSRCS})
  target_include_directories(<libname> PRIVATE ${INCDIR})

  # 可执行应用（如果有 main 入口）
  nuttx_add_application(
    NAME      ${CONFIG_LIB_XXX_PROGNAME}
    STACKSIZE ${CONFIG_LIB_XXX_STACKSIZE}
    PRIORITY  ${CONFIG_LIB_XXX_PRIORITY}
    SRCS      <libname>/src/main.c
    INCLUDE_DIRECTORIES ${INCDIR}
    DEPENDS   <libname>)

endif()
```

如果原始库自带 CMakeLists.txt，外层仍需创建自己的——NuttX CMake 构建系统需要 `nuttx_add_library` 等特定 API 来注册库和应用。

---

## 公有头文件导出

如果库的头文件需要被其他模块 include，在 Make.defs 中添加：

```makefile
CFLAGS   += ${INCDIR_PREFIX}$(APPDIR)/external/<libname>/<libname>/include
CXXFLAGS += ${INCDIR_PREFIX}$(APPDIR)/external/<libname>/<libname>/include
```

私有头文件只在 Makefile 中添加，原因是避免头文件名冲突——不同库可能有同名的内部头文件（如 `config.h`、`utils.h`），如果都暴露到全局搜索路径会导致编译时 include 到错误的文件。
