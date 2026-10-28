# 真实示例：rtos-benchmark

> `external/rtos-benchmark` 的完整配置，可以直接对照参考。

## 目录结构

```
external/rtos-benchmark/
├── Kconfig
├── CMakeLists.txt
├── Make.defs
├── Makefile
└── rtos-benchmark/          ← 原始仓库
    ├── h/
    │   ├── bench_api.h
    │   └── bench_utils.h
    ├── src/
    │   ├── common/          ← 通用测试代码
    │   ├── nuttx/           ← NuttX 适配层
    │   ├── freertos/
    │   ├── zephyr/
    │   └── rtems/
    ├── LICENSE
    └── README.md
```

## Kconfig

```kconfig
config RTOS_BENCHMARK
        tristate "RTOS benchmark"
        depends on PTHREAD_MUTEX_TYPES && (!SMP || CANCELLATION_POINTS)
        default n

if RTOS_BENCHMARK

config RTOS_BENCHMARK_PROGNAME
        string "Program Name"
        default "rtosbenchmark"

config RTOS_BENCHMARK_STACKSIZE
        int "rtos_benchmark stack size"
        default DEFAULT_TASK_STACKSIZE

config RTOS_BENCHMARK_PRIORITY
        int "Task PRIORITY"
        default 100

config RTOS_BENCHMARK_ITERATIONS
        int "ITERATIONS"
        default 1000

config RTOS_BENCHMARK_MAXTHREADS
        int "Max Threads"
        default 10

config RTOS_BENCHMARK_MAXSEMAPHORES
        int "Max Semaphores"
        default 5

config RTOS_BENCHMARK_MAXMUTEXES
        int "Max Mutexes"
        default 5

endif

```

注意：
- `depends on` 行声明了库的实际依赖：POSIX mutex 类型支持，SMP 模式下需要 CANCELLATION_POINTS
- `endif` 后有一行空行（Kconfig 解析器要求）

## Make.defs

```makefile
ifneq ($(CONFIG_RTOS_BENCHMARK),)
CONFIGURED_APPS += $(APPDIR)/external/rtos-benchmark
endif
```

## Makefile

```makefile
include $(APPDIR)/Make.defs

PROGNAME  = $(CONFIG_RTOS_BENCHMARK_PROGNAME)
PRIORITY  = $(CONFIG_RTOS_BENCHMARK_PRIORITY)
STACKSIZE = $(CONFIG_RTOS_BENCHMARK_STACKSIZE)
MODULE    = $(CONFIG_RTOS_BENCHMARK)

CFLAGS   += ${INCDIR_PREFIX}rtos-benchmark/h

CSRCS    += rtos-benchmark/src/common/bench_interrupt_latency_test.c    \
            rtos-benchmark/src/common/bench_mutex_lock_unlock_test.c    \
            rtos-benchmark/src/common/bench_sem_context_switch_test.c   \
            rtos-benchmark/src/common/bench_sem_signal_release_test.c   \
            rtos-benchmark/src/common/bench_thread_switch_yield_test.c  \
            rtos-benchmark/src/common/bench_thread_test.c               \
            rtos-benchmark/src/common/bench_malloc_free_test.c          \
            rtos-benchmark/src/common/bench_utils.c                     \
            rtos-benchmark/src/nuttx/bench_porting_layer_nuttx.c        \
            rtos-benchmark/src/nuttx/timer/timer.c

MAINSRC   = rtos-benchmark/src/common/bench_all.c

include $(APPDIR)/Application.mk
```

注意：CSRCS 只编译 `common/`（通用代码）和 `nuttx/`（NuttX 适配层），不编译 freertos/zephyr/rtems 目录。这是移植多平台库时的常见模式——只选择目标平台的适配代码。

## CMakeLists.txt

```cmake
if(CONFIG_RTOS_BENCHMARK)

  set(CSRCS
      rtos-benchmark/src/common/bench_interrupt_latency_test.c
      rtos-benchmark/src/common/bench_mutex_lock_unlock_test.c
      rtos-benchmark/src/common/bench_sem_context_switch_test.c
      rtos-benchmark/src/common/bench_sem_signal_release_test.c
      rtos-benchmark/src/common/bench_thread_switch_yield_test.c
      rtos-benchmark/src/common/bench_thread_test.c
      rtos-benchmark/src/common/bench_malloc_free_test.c
      rtos-benchmark/src/common/bench_utils.c
      rtos-benchmark/src/nuttx/bench_porting_layer_nuttx.c
      rtos-benchmark/src/nuttx/timer/timer.c)

  set(INCDIR rtos-benchmark/h)

  nuttx_add_library(rtos-benchmark STATIC)
  target_sources(rtos-benchmark PRIVATE ${CSRCS})
  target_include_directories(rtos-benchmark PRIVATE ${INCDIR})

  nuttx_add_application(
    NAME      ${CONFIG_RTOS_BENCHMARK_PROGNAME}
    STACKSIZE ${CONFIG_RTOS_BENCHMARK_STACKSIZE}
    PRIORITY  ${CONFIG_RTOS_BENCHMARK_PRIORITY}
    SRCS      rtos-benchmark/src/common/bench_all.c
    INCLUDE_DIRECTORIES ${INCDIR}
    DEPENDS   rtos-benchmark)

endif()
```

注意 CMakeLists.txt 的 CSRCS 列表与 Makefile 保持一致——双构建系统必须编译相同的源文件。
