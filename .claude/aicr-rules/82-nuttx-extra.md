### nuttx 仓库专项

「本次其他变更文件」指 prompt 中的 other_changed_files 列表；「commit message」指 Requirement Background 中的内容。

#### [NX-01] 自旋锁或调度锁在提前退出路径未释放（critical）
`spin_lock_irqsave`/`spin_lock`/`sched_lock`/`nxmutex_lock` 之后，保护区内的 `break`、`continue`、`return`、`goto` 路径没有调用对应的 `spin_unlock_irqrestore`/`spin_unlock`/`sched_unlock`/`nxmutex_unlock`。循环体内加锁时要逐个检查每个出口。`enter_critical_section` 的同类问题按 C-11 报，不重复报。

#### [NX-02] 临界区被 SMP 条件编译掉（high）
保护调度器状态、任务列表或被中断和线程共享的数据的临界区，放在了 `#ifdef CONFIG_SMP` 内。单核上中断嵌套同样会产生竞争，应无条件进入临界区。只保护 SMP 专属数据（如每 CPU 状态）时除外。

#### [NX-03] 改动已有 ioctl 编号（critical）
适用条件：变更涉及 `include/nuttx/fs/ioctl.h` 或各子系统的 `_xxxIOC(nr)` 命令定义。
修改了已有命令的 `nr`、在已有命令中间插入新命令使后续编号顺延，或修改了其他子系统的 `_xxxIOCBASE`。这会破坏已编译应用和对端核的 ABI。新命令应追加在本子系统已有编号之后，留出空号也不要重排。

#### [NX-04] 格式符与参数类型不一致（high）
`printf`/`syslog`/`snprintf`/`_info`/`_err` 等的格式符与参数类型不匹配：`size_t`/`ssize_t` 没用 `%zu`/`%zd`，`off_t` 没用 `PRIdOFF`，`uint64_t`/`int64_t` 没用 `PRIu64`/`PRId64`，`uintptr_t` 没用 `PRIxPTR`；或修正长度修饰时改变了原有进制（如把 `%x` 改成 `%zu`）。

#### [NX-05] 改为栈变量或部分赋值后未清零（high）
把 `kmm_zalloc`/`zalloc` 分配的结构体改成栈变量后没有 `memset` 清零；或栈上的结构体、union 只赋值了部分字段就整体传出（拷贝给用户态、作为 RPMSG 消息发送、`memcpy` 到共享内存），未赋值字段和填充字节会带出栈上残留数据。

#### [NX-06] goto 跳过了错误检查（high）
`goto` 的目标标签位于错误检查或部分清理代码之后，跳转后返回值没有被检查、已获取的资源没有释放；或在给 `ret` 赋值之前就跳到返回 `ret` 的标签。

#### [NX-07] 遗留调试代码或被注释掉的代码（medium）
新增被注释掉的代码（`/* foo(); */`、`#if 0` 块）、临时调试打印（追踪用的 `printf`/`_alert`/`syslog(LOG_EMERG, ...)`）或 `TODO`/`FIXME`/`XXX` 标记。禁用的代码应连同因此变得无用的变量和声明一起删除。

#### [NX-08] 公共结构体新增字段没有配置保护（medium）
适用条件：修改 `include/nuttx/` 下被大量实例化的结构体，如 `struct tcb_s`、`struct file`、`struct inode`、`struct net_driver_s`、`struct iob_s`。
新增字段只服务于某个可选特性，却没有用对应的 `CONFIG_*` 包起来，关闭该特性的配置也要承担内存开销。

#### [NX-09] 唤醒或通知无条件调用（medium）
`nxsem_post`、`poll_notify`、`work_queue` 等唤醒或通知调用，没有放在真正需要它的条件下（有等待者、状态确实发生变化），导致虚假唤醒或信号量计数持续增长。

#### [NX-10] IPv4 与 IPv6 路径未同步修改（medium）
适用条件：变更位于 `net/`，且修改了只属于一个协议族的文件或分支（`ipv4_*`/`ipv6_*`、`icmp/`/`icmpv6/`、`#ifdef CONFIG_NET_IPv4`/`CONFIG_NET_IPv6` 分支）。
修复只改了一侧，同一 diff 和本次其他变更文件里都没有另一侧的对应修改，commit message 也没有说明另一侧不需要改。

#### [NX-11] 只修复了 switch 中的一处贯穿（medium）
本次 diff 为某个 `case` 补上了 `break` 或 `return`，但同一个 switch 里还有其他 case 存在同样未标注的贯穿。

#### [NX-12] 头文件不自包含（medium）
适用条件：新增或修改 `include/` 下的头文件。
头文件里新使用了某个类型或宏（`size_t`、`bool`、`FAR`、`CONFIG_*`），却没有 include 定义它的头文件（`<nuttx/config.h>`、`<stddef.h>`、`<stdbool.h>`、`<nuttx/compiler.h>`），依赖使用方先 include。

#### [NX-13] 提交内容与 commit message 不符（medium）
适用条件：Requirement Background 中给出了 commit message。
diff 中有 commit message 没有提到、且与本次目的无关的改动（无关的重构或格式调整、另一个 bug 的修复、在 `boards/` 的 defconfig 里顺带打开新功能），或 commit message 声称的修改在 diff 中找不到。在第一处无关改动上报一次，列出所有无关改动的位置。
