/****************************************************************************
 * include/nuttx/compiler.h
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.  The
 * ASF licenses this file to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance with the
 * License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.  See the
 * License for the specific language governing permissions and limitations
 * under the License.
 *
 ****************************************************************************/

#ifndef __INCLUDE_NUTTX_COMPILER_H
#define __INCLUDE_NUTTX_COMPILER_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* ISO C11 supports anonymous (unnamed) structures and unions, added in
 * GCC 4.6 (but might be suppressed with -std= option).  ISO C++11 also
 * adds un-named unions, but NOT unnamed structures (although compilers
 * may support them).
 *
 * CAREFUL: This can cause issues for shared data structures shared between
 * C and C++ if the two versions do not support the same features.
 * Structures and unions can lose binary compatibility!
 *
 * NOTE: The NuttX coding standard forbids the use of unnamed structures and
 * unions within the OS.
 */

#undef CONFIG_HAVE_ANONYMOUS_STRUCT
#undef CONFIG_HAVE_ANONYMOUS_UNION

#if (defined(__cplusplus) && __cplusplus >= 201103L) || \
    (defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L)
#  define CONFIG_HAVE_ANONYMOUS_STRUCT 1
#  define CONFIG_HAVE_ANONYMOUS_UNION 1
#endif

/* ISO C99 supports _Bool */

#undef CONFIG_C99_BOOL

#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 199901L
#  define CONFIG_C99_BOOL 1
#endif

/* ISO C99 supports Designated initializers */

#undef CONFIG_DESIGNATED_INITIALIZERS

#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 199901L
#  define CONFIG_DESIGNATED_INITIALIZERS 1
#endif

/* C++ support */

#undef CONFIG_HAVE_CXX14

#if defined(__cplusplus) && __cplusplus >= 201402L
#  define CONFIG_HAVE_CXX14 1
#endif

#if (defined(__cplusplus) && __cplusplus >= 201103L) || \
    (defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L)
#  define CONFIG_HAVE_ZERO_SIZE_ARRAY 1
#endif

/* Keyword about _Atomic */

#if defined(__cplusplus) || defined(__clang__) || \
    !defined(__STDC_VERSION__) || __STDC_VERSION__ < 201112L
#  define __Atomic(t) t
#else
#  define __Atomic(t) _Atomic(t)
#endif

/* Green Hills Software definitions *****************************************/

#if defined(__ghs__)

#  define __extension__
#  define register

#endif

#undef offsetof

/* undef __GNUC__ when __TASKING__ is present */
#if defined(__TASKING__)
#  undef __GNUC__
#  undef __GLIBCXX__  // Do not use libstdc++/libsupc++ when tasking present
#endif

/* GCC-specific definitions *************************************************/

#ifdef __GNUC__

/* In strict C++ mode (-std=c++NN), typeof is not available as a keyword.
 * Map it to __typeof__ which is a GCC built-in that works in all modes.
 */

#  if defined(__cplusplus) && defined(__STRICT_ANSI__)
#    define typeof __typeof__
#  endif

/* Built-ins */
#  if __GNUC__ >= 4 && !defined(__ghs__)
#    define CONFIG_HAVE_BUILTIN_BSWAP16 1
#    define CONFIG_HAVE_BUILTIN_BSWAP32 1
#    define CONFIG_HAVE_BUILTIN_BSWAP64 1
#    define CONFIG_HAVE_BUILTIN_CTZ 1
#    define CONFIG_HAVE_BUILTIN_CLZ 1
#    define CONFIG_HAVE_BUILTIN_POPCOUNT 1
#    define CONFIG_HAVE_BUILTIN_POPCOUNTLL 1
#    define CONFIG_HAVE_BUILTIN_FFS 1
#    define CONFIG_HAVE_BUILTIN_FFSL 1
#    define CONFIG_HAVE_BUILTIN_FFSLL 1
#  endif

#  if CONFIG_FORTIFY_SOURCE > 0
#    if !defined(__OPTIMIZE__) || (__OPTIMIZE__) <= 0
#      warning requires compiling with optimization (-O2 or higher)
#    endif
#    if CONFIG_FORTIFY_SOURCE == 3
#      define fortify_size(__o, type) __builtin_dynamic_object_size(__o, type)
#    else
#      define fortify_size(__o, type) __builtin_object_size(__o, type)
#    endif

#    define fortify_assert(condition) do \
                                        { \
                                          if (!(condition)) \
                                            { \
                                              __builtin_trap(); \
                                            } \
                                        } \
                                      while (0)

#    if !defined(__clang__)
#      define fortify_va_arg_pack __builtin_va_arg_pack
#    endif
#    define fortify_real(fn) __typeof__(fn) __real_##fn __asm__(#fn)
#    define fortify_function(fn) fortify_real(fn); \
                                 extern __inline__ no_builtin(#fn) \
                                 __attribute__((__always_inline__, \
                                                __gnu_inline__, __artificial__))
#  elif defined(__OPTIMIZE__) && __OPTIMIZE__ > 0 && defined(__has_builtin)
#    define builtin_original(fn)  __typeof__(fn)  __orig_##fn __asm__(#fn)
#    define builtin_function(fn)  builtin_original(fn); \
                                  extern __inline__ no_builtin(#fn) \
                                  __attribute__((__always_inline__, \
                                                 __gnu_inline__, __artificial__))
#    define CONFIG_HAVE_BUILTIN 1 /* Use built-in functions */
#  endif

/* Pre-processor */

#  define CONFIG_CPP_HAVE_VARARGS 1 /* Supports variable argument macros */
#  define CONFIG_CPP_HAVE_WARNING 1 /* Supports #warning */

/* Intriniscs.  GCC supports __func__ but provides __FUNCTION__ for backward
 * compatibility with older versions of GCC.
 */

#  define CONFIG_HAVE_FUNCTIONNAME 1 /* Has __FUNCTION__ */
#  define CONFIG_HAVE_FILENAME     1 /* Has __FILE__ */

/* Built-in functions */

#  ifndef __has_builtin
#    define __has_builtin(x) 0
#  endif

#  ifndef __has_feature
#    define __has_feature(x) 0
#  endif

#  ifndef __has_extension
#    define __has_extension(x) 0
#  endif

#  ifndef __has_keyword
#    define __has_keyword(x) 0
#  endif

/* The <stddef.h> header shall define the following macros:
 *
 * offsetof(type, member-designator)
 *   Integer constant expression of type size_t, the value of which is the
 *   offset in bytes to the structure member (member-designator), from the
 *   beginning of its structure (type).
 *
 *     NOTE: This version of offsetof() depends on behaviors that could be
 *     undefined for some compilers.  It would be better to use a builtin
 *     function if one exists.
 *
 * Reference: Opengroup.org
 */

#  define offsetof(a, b) __builtin_offsetof(a, b)
#  define return_address(x) __builtin_return_address(x)

#  define PRAGMA(x)         _Pragma(#x)

#  if defined(__clang__)
#    define unroll_loop(n)  PRAGMA(clang loop unroll_count(n))
#  else
#    define unroll_loop(n)  PRAGMA(GCC unroll n)
#  endif

/* Attributes
 *
 * GCC supports weak symbols which can be used to reduce code size because
 * unnecessary "weak" functions can be excluded from the link.
 */

#  undef CONFIG_HAVE_WEAKFUNCTIONS

#  if !defined(__CYGWIN__) && !defined(CONFIG_ARCH_GNU_NO_WEAKFUNCTIONS)
#    define CONFIG_HAVE_WEAKFUNCTIONS 1
#    define weak_alias(name, aliasname) \
     extern __typeof(name) aliasname __attribute__((weak, alias(#name)));
#    define weak_data __attribute__((weak))
#    define weak_function __attribute__((weak))
#    define weak_const_function __attribute__((weak, __const__))
#  else
#    define weak_alias(name, aliasname)
#    define weak_data
#    define weak_function
#    define weak_const_function
#  endif

/* The noreturn attribute informs GCC that the function will not return.
 * C11 adds _Noreturn keyword (see stdnoreturn.h)
 */

#  define noreturn_function __attribute__((noreturn))

/* If control flow reaches the point of the __builtin_unreachable,
 * the program is undefined. It is useful in situations where
 * the compiler cannot deduce the unreachability of the code.
 */

#define code_unreachable() __builtin_unreachable()

/* The pure function attribute informs the compiler that
 * the function is pure or const.
 */

#  define pure_function  __attribute__((pure))
#  define const_function __attribute__((const))

/* The farcall_function attribute informs GCC that is should use long calls
 * (even though -mlong-calls does not appear in the compilation options)
 */

#  if defined(__clang__)
#    define farcall_function
#  else
#    define farcall_function __attribute__((long_call))
#  endif

/* Branch prediction */

#  define predict_true(x)  __builtin_expect(!!(x), 1)
#  define predict_false(x) __builtin_expect(!!(x), 0)

/* `x` can be evaluated at compile time */

#  define is_constexpr(x) __builtin_constant_p(x)

/* Check if two types are compatible at compile time */

#  if __has_builtin(__is_same)
#    define is_types_compatible(x, type) __is_same(decltype(x), type)
#  else
#    define is_types_compatible(x, type) \
       __builtin_types_compatible_p(typeof(x), type)
#  endif

/* Assume the condition is satisfied.
 * We can use this to perform more aggressive compiler optimizations.
 * Please use this with caution, as it may cause runtime errors if you fail
 * to ensure that the condition is satisfied.
 */

#  if defined(__clang__)
#    define compiler_assume(x) __builtin_assume(x)
#  elif __GNUC__ >= 13 /* The assume(x) is supported since GCC 13. */
#    define compiler_assume(x) __attribute__((assume(x)))
#  elif __has_builtin(__builtin_unreachable)
#    define compiler_assume(x) \
     do { if (!(x)) { __builtin_unreachable(); } } while(0)
#  else
#    define compiler_assume(x)
#  endif

/* Code locate */

#  define locate_code(n) __attribute__((section(n)))

/* Data alignment */

#  define aligned_data(n) __attribute__((aligned(n)))

/* Data location */

#  define locate_data(n) __attribute__((section(n)))

/* The packed attribute informs GCC that the structure elements are packed,
 * ignoring other alignment rules.
 */

#  define begin_packed_struct
#  define end_packed_struct __attribute__((packed))

/* GCC does not support the reentrant attribute */

#  define reentrant_function

/* The naked attribute informs GCC that the programmer will take care of
 * the function prolog and epilog.
 */

#  if !defined(__ghs__) || __GHS_VERSION_NUMBER >= 202354
#    define naked_function __attribute__((naked,no_instrument_function))
#  else
#    define naked_function
#  endif

/* The always_inline_function attribute informs GCC that the function should
 * always be inlined, regardless of the level of optimization.  The
 * noinline_function indicates that the function should never be inlined.
 * Note that if the compiler optimization is disabled, the stack-usage of
 * the inline functions will not be optimized. In this case, force inlining
 * can lead to stack-overflow.
 */

#  ifdef CONFIG_DEBUG_NOOPT
#    define always_inline_function inline
#    define inline_function inline
#  else
#    define always_inline_function __attribute__((always_inline,no_instrument_function)) inline
#    define inline_function __attribute__((always_inline)) inline
#  endif

#  define noinline_function __attribute__((noinline))

/* The noinstrument_function attribute informs GCC don't instrument it */

#  define noinstrument_function __attribute__((no_instrument_function))

/* The no_profile_instrument_function attribute on functions is used to
 * inform the compiler that it should not process any profile feedback
 * based optimization code instrumentation.
 */

#  define noprofile_function __attribute__((no_profile_instrument_function))

/* The nooptimiziation_function attribute no optimize */

#  if defined(__clang__)
#    define nooptimiziation_function __attribute__((optnone))
#  else
#    define nooptimiziation_function __attribute__((optimize("O0")))
#  endif

/* The nosanitize_address attribute informs GCC don't sanitize it */

#  define nosanitize_address __attribute__((no_sanitize_address))

/* the Greenhills compiler do not support the following attributes */

#  if defined(__ghs__)
#    undef nooptimiziation_function
#    define nooptimiziation_function

#    undef nosanitize_address
#    define nosanitize_address
#  endif

#  if defined(__AVR32__)

#    undef nosanitize_address
#    define nosanitize_address

#    undef code_unreachable
#  endif

#  if defined(__TRICORE__)
#    undef nooptimiziation_function
#    define nooptimiziation_function
#  endif

/* The nosanitize_undefined attribute informs GCC don't sanitize it */

#  define nosanitize_undefined __attribute__((no_sanitize("undefined")))

#ifdef CONFIG_ARCH_SAFE_STACK
#  define nosanitize_safestack __attribute__((no_sanitize("safe-stack")))
#else
#  define nosanitize_safestack
#endif

/* The nostackprotect_function attribute disables stack protection in
 * sensitive functions, e.g., stack coloration routines.
 */

#  if defined(__has_attribute)
#    if __has_attribute(no_stack_protector)
#      define nostackprotect_function __attribute__((no_stack_protector))
#    endif
#  endif

/* nostackprotect_function definition for older versions of GCC and Clang.
 * Note that Clang does not support setting per-function optimizations,
 * simply disable the entire optimization pass for the required function.
 */

#  ifndef nostackprotect_function
#    if defined(__clang__)
#      define nostackprotect_function __attribute__((optnone))
#    else
#      define nostackprotect_function __attribute__((__optimize__("-fno-stack-protector")))
#    endif
#  endif

/* The constructor attribute causes the function to be called
 * automatically before execution enters main ().
 * Similarly, the destructor attribute causes the function to
 * be called automatically after main () has completed or
 * exit () has been called. Functions with these attributes are
 * useful for initializing data that will be used implicitly
 * during the execution of the program.
 * See https://gcc.gnu.org/onlinedocs/gcc-4.1.2/gcc/Function-Attributes.html
 */

#  define constructor_fuction __attribute__((constructor))
#  define destructor_function __attribute__((destructor))

/* Use visibility_hidden to hide symbols by default
 * Use visibility_default to make symbols visible
 * See https://gcc.gnu.org/wiki/Visibility
 */

#  define visibility_hidden __attribute__((visibility("hidden")))
#  define visibility_default __attribute__((visibility("default")))

/* The unused code or data */

#  define unused_code __attribute__((unused))
#  define unused_data __attribute__((unused))
#  define used_code __attribute__((used))
#  define used_data __attribute__((used))

/* The allocation function annotations */

#  if __GNUC__ >= 11
#    define fopen_like __attribute__((__malloc__(fclose, 1)))
#    define popen_like __attribute__((__malloc__(pclose, 1)))
#    define malloc_like __attribute__((__malloc__(__builtin_free, 1)))
#    define malloc_like1(a) __attribute__((__malloc__(__builtin_free, 1))) __attribute__((__alloc_size__(a)))
#    define malloc_like2(a, b) __attribute__((__malloc__(__builtin_free, 1))) __attribute__((__alloc_size__(a, b)))
#    define realloc_like(a) __attribute__((__alloc_size__(a)))
#    define realloc_like2(a, b) __attribute__((__alloc_size__(a, b)))
#  else
#    define fopen_like __attribute__((__malloc__))
#    define popen_like __attribute__((__malloc__))
#    define malloc_like __attribute__((__malloc__))
#    define malloc_like1(a) __attribute__((__malloc__)) __attribute__((__alloc_size__(a)))
#    define malloc_like2(a, b) __attribute__((__malloc__)) __attribute__((__alloc_size__(a, b)))
#    define realloc_like(a) __attribute__((__alloc_size__(a)))
#    define realloc_like2(a, b) __attribute__((__alloc_size__(a, b)))
#  endif

/* Some versions of GCC have a separate __syslog__ format.
 * http://mail-index.netbsd.org/source-changes/2015/10/14/msg069435.html
 * Use it if available. Otherwise, assume __printf__ accepts %m.
 */

#  if !defined(__syslog_attribute__)
#    define __syslog__ __printf__
#  endif

#  define format_like(a) __attribute__((__format_arg__(a)))
#  define printf_like(a, b) __attribute__((__format__(__printf__, a, b)))
#  define syslog_like(a, b) __attribute__((__format__(__syslog__, a, b)))
#  define scanf_like(a, b) __attribute__((__format__(__scanf__, a, b)))
#  define strftime_like(a) __attribute__((__format__(__strftime__, a, 0)))
#  define object_size(o, t) __builtin_object_size(o, t)

/* GCC does not use storage classes to qualify addressing */

#  define FAR
#  define NEAR
#  define DSEG
#  define CODE

/* Define these here and allow specific architectures to override as needed */

#  define CONFIG_HAVE_LONG_LONG 1
#  define CONFIG_HAVE_FLOAT 1
#  define CONFIG_HAVE_DOUBLE 1
#  define CONFIG_HAVE_LONG_DOUBLE 1

/* Handle cases where sizeof(int) is 16-bits, sizeof(long) is 32-bits, and
 * pointers are 16-bits.
 */

#  if defined(__m32c__)
/* No I-space access qualifiers */

#    define IOBJ
#    define IPTR

/* Select the small, 16-bit addressing model */

#    define CONFIG_SMALL_MEMORY 1

/* Long and int are not the same size */

#    define CONFIG_LONG_IS_NOT_INT 1

/* Pointers and int are the same size */

#    undef  CONFIG_PTR_IS_NOT_INT

#  elif defined(__AVR__)

#    undef nosanitize_address
#    define nosanitize_address
#    undef code_unreachable

#    if defined(__AVR_2_BYTE_PC__) || defined(__AVR_3_BYTE_PC__)
/* 2-byte 3-byte PC does not support returnaddress */

#      undef return_address
#      define return_address(x) 0

#    endif

#    if defined(CONFIG_AVR_HAS_MEMX_PTR)
/* I-space access qualifiers needed by Harvard architecture */

#      define IOBJ __flash
#      define IPTR __memx

#    else
/* No I-space access qualifiers */

#      define IOBJ
#      define IPTR
#    endif

/* Select the small, 16-bit addressing model (for D-Space) */

#    define CONFIG_SMALL_MEMORY 1

/* Long and int are not the same size */

#    define CONFIG_LONG_IS_NOT_INT 1

/* Pointers and int are the same size */

#    undef  CONFIG_PTR_IS_NOT_INT

/* Uses a 32-bit FAR pointer only from accessing data outside of the 16-bit
 * data space.
 */

#    define CONFIG_HAVE_FARPOINTER 1

#  elif defined(__mc68hc1x__)

/* No I-space access qualifiers */

#    define IOBJ
#    define IPTR

/* Select the small, 16-bit addressing model */

#    define CONFIG_SMALL_MEMORY 1

/* Normally, mc68hc1x code is compiled with the -mshort option
 * which results in a 16-bit integer.  If -mnoshort is defined
 * then an integer is 32-bits.  GCC will defined __INT__ accordingly:
 */

#    if __INT__ == 16
/* int is 16-bits, long is 32-bits */

#      define CONFIG_LONG_IS_NOT_INT 1

/* Pointers and int are the same size (16-bits) */

#      undef  CONFIG_PTR_IS_NOT_INT
#    else
/* int and long are both 32-bits */

#      undef  CONFIG_LONG_IS_NOT_INT

/* Pointers and int are NOT the same size */

#      define CONFIG_PTR_IS_NOT_INT 1
#    endif

#  elif defined(_EZ80ACCLAIM)

/* No I-space access qualifiers */

#    define IOBJ
#    define IPTR

/* Select the large, 24-bit addressing model */

#    undef  CONFIG_SMALL_MEMORY

/* int is 24-bits, long is 32-bits */

#    define CONFIG_LONG_IS_NOT_INT 1

/* pointers are 24-bits too */

#    undef  CONFIG_PTR_IS_NOT_INT

/* the ez80 stdlib doesn't support doubles */

#    undef  CONFIG_HAVE_DOUBLE
#    undef  CONFIG_HAVE_LONG_DOUBLE

#  else

/* No I-space access qualifiers */

#    define IOBJ
#    define IPTR

/* Select the large, 32-bit addressing model */

#    undef  CONFIG_SMALL_MEMORY

/* Long and int are (probably) the same size (32-bits) */

#    undef  CONFIG_LONG_IS_NOT_INT

/* Pointers and int are the same size (32-bits) */

#    undef  CONFIG_PTR_IS_NOT_INT

#  endif

/* Indicate that a local variable is not used */

#  ifndef UNUSED
#    define UNUSED(a) ((void)(1 || &(a)))
#  endif

#  if defined(__clang__)
#    define no_builtin(n) __attribute__((no_builtin(n)))
#  elif (__GNUC__ == 4 && __GNUC_MINOR__ >= 6) || (__GNUC__ > 4)
#    define no_builtin(n) __attribute__((__optimize__("-fno-tree-loop-distribute-patterns")))
#  else
#    define no_builtin(n)
#  endif

/* CMSE extension */

#  ifdef CONFIG_ARCH_HAVE_TRUSTZONE
#    define tz_nonsecure_entry __attribute__((cmse_nonsecure_entry))
#    define tz_nonsecure_call  __attribute__((cmse_nonsecure_call))
#  endif

/* GCC support expression statement, a compound statement enclosed in
 * parentheses may appear as an expression in GNU C.
 */

#  define CONFIG_HAVE_EXPRESSION_STATEMENT 1

/* Warning about usage of deprecated features. */

#  define deprecated_function  __attribute__((deprecated))

/* Memory barrier. */

#  define memory_barrier()  __asm__ __volatile__ ("" : : : "memory")

/* Atomic functions. */

#  ifdef CONFIG_LIBC_ATOMIC_TOOLCHAIN
#    define atomic_store_4(obj, val, memorder)     __atomic_store_n(obj, val, memorder)
#    define atomic_store_8(obj, val, memorder)     __atomic_store_n(obj, val, memorder)
#    define atomic_load_4(obj, memorder)           __atomic_load_n(obj, memorder)
#    define atomic_load_8(obj, memorder)           __atomic_load_n(obj, memorder)
#    define atomic_fetch_add_4(obj, val, memorder) __atomic_fetch_add(obj, val, memorder)
#    define atomic_fetch_add_8(obj, val, memorder) __atomic_fetch_add(obj, val, memorder)
#    define atomic_fetch_sub_4(obj, val, memorder) __atomic_fetch_sub(obj, val, memorder)
#    define atomic_fetch_sub_8(obj, val, memorder) __atomic_fetch_sub(obj, val, memorder)
#    define atomic_fetch_and_4(obj, val, memorder) __atomic_fetch_and(obj, val, memorder)
#    define atomic_fetch_and_8(obj, val, memorder) __atomic_fetch_and(obj, val, memorder)
#    define atomic_fetch_or_4(obj, val, memorder)  __atomic_fetch_or(obj, val, memorder)
#    define atomic_fetch_or_8(obj, val, memorder)  __atomic_fetch_or(obj, val, memorder)
#    define atomic_fetch_xor_4(obj, val, memorder) __atomic_fetch_xor(obj, val, memorder)
#    define atomic_fetch_xor_8(obj, val, memorder) __atomic_fetch_xor(obj, val, memorder)
#    define atomic_exchange_4(obj, val, memorder)  __atomic_exchange_n(obj, val, memorder)
#    define atomic_exchange_8(obj, val, memorder)  __atomic_exchange_n(obj, val, memorder)
#    define atomic_compare_exchange_4(obj, expected, desired, weak, success, failure) \
       __atomic_compare_exchange_n(obj, expected, desired, weak, success, failure)
#    define atomic_compare_exchange_8(obj, expected, desired, weak, success, failure) \
       __atomic_compare_exchange_n(obj, expected, desired, weak, success, failure)
#  endif

/* SDCC-specific definitions ************************************************/

#elif defined(SDCC) || defined(__SDCC)

/* No I-space access qualifiers */

#  define IOBJ
#  define IPTR

/* Pre-processor */

#  define CONFIG_CPP_HAVE_VARARGS 1 /* Supports variable argument macros */
#  define CONFIG_CPP_HAVE_WARNING 1 /* Supports #warning */

/* Intriniscs */

#  define CONFIG_HAVE_FUNCTIONNAME 1 /* Has __FUNCTION__ */
#  define CONFIG_HAVE_FILENAME     1 /* Has __FILE__ */
#  define __FUNCTION__ __func__      /* SDCC supports on __func__ */

/* Pragmas
 *
 * Disable warnings for unused function arguments
 */

# pragma disable_warning 85

/* Attributes
 *
 * SDCC does not support weak symbols
 */

#  undef  CONFIG_HAVE_WEAKFUNCTIONS
#  define weak_alias(name, aliasname)
#  define weak_data
#  define weak_function
#  define weak_const_function
#  define restrict /* REVISIT */

/* SDCC does not support the noreturn or packed attributes */

/* Current SDCC supports noreturn via C11 _Noreturn keyword (see
 * stdnoreturn.h).
 */

#  define noreturn_function
#  define pure_function
#  define const_function
#  define predict_true(x) (x)
#  define predict_false(x) (x)
#  define is_constexpr(x)  (0)
#  define compiler_assume(x)
#  define locate_code(n)
#  define aligned_data(n)
#  define locate_data(n)
#  define begin_packed_struct
#  define end_packed_struct

/* REVISIT: */

#  define farcall_function

/* SDCC does support "naked" functions */

#  define naked_function __naked

/* SDCC does not support forced inlining. */

#  define always_inline_function
#  define inline_function inline
#  define noinline_function
#  define noinstrument_function
#  define noprofile_function
#  define nooptimiziation_function
#  define nosanitize_address
#  define nosanitize_undefined
#  define nostackprotect_function
#  define constructor_fuction
#  define destructor_function

#  define visibility_hidden
#  define visibility_default

#  define unused_code
#  define unused_data
#  define used_code
#  define used_data

#  define fopen_like
#  define popen_like
#  define malloc_like
#  define malloc_like1(a)
#  define malloc_like2(a, b)
#  define realloc_like(a)
#  define realloc_like2(a, b)

#  define format_like(a)
#  define printf_like(a, b)
#  define syslog_like(a, b)
#  define scanf_like(a, b)
#  define strftime_like(a)
#  define object_size(o, t) ((size_t)-1)

/* The reentrant attribute informs SDCC that the function
 * must be reentrant.  In this case, SDCC will store input
 * arguments on the stack to support reentrancy.
 *
 * SDCC functions are always reentrant (except for the mcs51,
 * ds390, hc08 and s08 backends)
 */

#  define reentrant_function __reentrant

/* Indicate that a local variable is not used */

#  ifndef UNUSED
#    define UNUSED(a) ((void)(1 || &(a)))
#  endif

/* It is assumed that the system is build using the small
 * data model with storage defaulting to internal RAM.
 * The NEAR storage class can also be used to address data
 * in internal RAM; FAR can be used to address data in
 * external RAM.
 */

#  if defined(__SDCC_z80) || defined(__SDCC_z180) || defined(__SDCC_gbz80)
#    define FAR
#    define NEAR
#    define CODE
#    define DSEG
#  else
#    define FAR    __xdata
#    define NEAR   __data
#    define CODE   __code
#    if defined(SDCC_MODEL_SMALL)
#      define DSEG __data
#    else
#      define DSEG __xdata
#    endif
#  endif

/* Select small, 16-bit address model */

#  define CONFIG_SMALL_MEMORY 1

/* Long and int are not the same size */

#  define CONFIG_LONG_IS_NOT_INT 1

/* The generic pointer and int are not the same size (for some SDCC
 * architectures).  REVISIT: SDCC now has more backends where pointers are
 * the same size as int than just z80 and z180.
 */

#  if !defined(__z80) && !defined(__gbz80)
#    define CONFIG_PTR_IS_NOT_INT 1
#  endif

/* SDCC does types long long and float, but not types double and long
 * double.
 */

#  define CONFIG_HAVE_LONG_LONG 1
#  define CONFIG_HAVE_FLOAT 1
#  undef  CONFIG_HAVE_DOUBLE
#  undef  CONFIG_HAVE_LONG_DOUBLE

#  define offsetof(a, b) ((size_t)(&(((a *)(0))->b)))
#  define return_address(x) 0

#  define PRAGMA(x)
#  define unroll_loop(n)

#  define no_builtin(n)

/* Warning about usage of deprecated features. */

#  define deprecated_function

/* Memory barrier. */

#  define memory_barrier()

/* Zilog-specific definitions ***********************************************/

#elif defined(__ZILOG__)

/* At present, only the following Zilog compilers are recognized */

#  if !defined(__ZNEO__) && !defined(__EZ8__) && !defined(__EZ80__)
#    warning "Unrecognized Zilog compiler"
#  endif

/* Pre-processor */

#  undef CONFIG_CPP_HAVE_VARARGS /* No variable argument macros */
#  undef CONFIG_CPP_HAVE_WARNING /* Does not support #warning */

/* Intrinsics */

#  define CONFIG_HAVE_FUNCTIONNAME 1 /* Has __FUNCTION__ */
#  define CONFIG_HAVE_FILENAME     1 /* Has __FILE__ */

/* No I-space access qualifiers */

#  define IOBJ
#  define IPTR

/* Attributes
 *
 * The Zilog compiler does not support weak symbols
 */

#  undef  CONFIG_HAVE_WEAKFUNCTIONS
#  define weak_alias(name, aliasname)
#  define weak_data
#  define weak_function
#  define weak_const_function
#  define restrict

/* The Zilog compiler does not support the noreturn, packed, naked
 * attributes.
 */

#  define noreturn_function
#  define pure_function
#  define const_function
#  define predict_true(x) (x)
#  define predict_false(x) (x)
#  define is_constexpr(x)  (0)
#  define compiler_assume(x)
#  define aligned_data(n)
#  define locate_code(n)
#  define locate_data(n)
#  define begin_packed_struct
#  define end_packed_struct
#  define naked_function
#  define always_inline_function
#  define inline_function inline
#  define noinline_function
#  define noinstrument_function
#  define noprofile_function
#  define nooptimiziation_function
#  define nosanitize_address
#  define nosanitize_undefined
#  define nostackprotect_function
#  define constructor_fuction
#  define destructor_function
#  define visibility_hidden
#  define visibility_default
#  define unused_code
#  define unused_data
#  define used_code
#  define used_data
#  define fopen_like
#  define popen_like
#  define malloc_like
#  define malloc_like1(a)
#  define malloc_like2(a, b)
#  define realloc_like(a)
#  define realloc_like2(a, b)
#  define format_like(a)
#  define printf_like(a, b)
#  define syslog_like(a, b)
#  define scanf_like(a, b)
#  define strftime_like(a)
#  define object_size(o, t) ((size_t)-1)

/* REVISIT: */

#  define farcall_function

/* The Zilog compiler does not support the reentrant attribute */

#  define reentrant_function

/* Addressing.
 *
 * Z16F ZNEO:  Far is 24-bits; near is 16-bits of address.
 *             The supported model is (1) all code on ROM, and (2) all data
 *             and stacks in external (far) RAM.
 * Z8Encore!:  Far is 16-bits; near is 8-bits of address.
 *             The supported model is (1) all code on ROM, and (2) all data
 *             and stacks in internal (far) RAM.
 * Z8Acclaim:  In Z80 mode, all pointers are 16-bits.  In ADL mode, all
 *             pointers are 24 bits.
 */

#  if defined(__ZNEO__)
#    define FAR   _Far
#    define NEAR  _Near
#    define DSEG  _Far
#    define CODE  _Erom
#    undef  CONFIG_SMALL_MEMORY       /* Select the large, 32-bit addressing model */
#    undef  CONFIG_LONG_IS_NOT_INT    /* Long and int are the same size */
#    undef  CONFIG_PTR_IS_NOT_INT     /* FAR pointers and int are the same size */
#  elif defined(__EZ8__)
#    define FAR   far
#    define NEAR  near
#    define DSEG  far
#    define CODE  rom
#    define CONFIG_SMALL_MEMORY 1     /* Select small, 16-bit address model */
#    define CONFIG_LONG_IS_NOT_INT 1  /* Long and int are not the same size */
#    undef  CONFIG_PTR_IS_NOT_INT     /* FAR pointers and int are the same size */
#  elif defined(__EZ80__)
#    define FAR
#    define NEAR
#    define DSEG
#    define CODE
#    undef  CONFIG_SMALL_MEMORY       /* Select the large, 32-bit addressing model */
#    define CONFIG_LONG_IS_NOT_INT 1  /* Long and int are not the same size */
#    ifdef CONFIG_EZ80_Z80MODE
#      define CONFIG_PTR_IS_NOT_INT 1 /* Pointers and int are not the same size */
#    else
#      undef  CONFIG_PTR_IS_NOT_INT   /* Pointers and int are the same size */
#    endif
#  endif

/* Indicate that a local variable is not used */

#  ifndef UNUSED
#    define UNUSED(a) ((void)(1 || &(a)))
#  endif

/* Older Zilog compilers support both types double and long long, but the
 * size is 32-bits (same as long and single precision) so it is safer to say
 * that they are not supported.  Later versions are more ANSII compliant and
 * simply do not support long long or double.
 */

#  undef  CONFIG_HAVE_LONG_LONG
#  define CONFIG_HAVE_FLOAT 1
#  undef  CONFIG_HAVE_DOUBLE
#  undef  CONFIG_HAVE_LONG_DOUBLE

#  define offsetof(a, b) ((size_t)(&(((a *)(0))->b)))
#  define return_address(x) 0

#  define PRAGMA(x)
#  define unroll_loop(n)

#  define no_builtin(n)

/* Warning about usage of deprecated features. */

#  define deprecated_function

/* Memory barrier. */

#  define memory_barrier()

/* ICCARM-specific definitions **********************************************/

#elif defined(__ICCARM__)

#  define weak_alias(name, aliasname)
#  define weak_data            __weak
#  define weak_function        __weak
#  define weak_const_function
#  define noreturn_function
#  define farcall_function
#  define pure_function
#  define const_function
#  define predict_true(x) (x)
#  define predict_false(x) (x)
#  define is_constexpr(x)  (0)
#  define compiler_assume(x)
#  define locate_code(n)
#  define aligned_data(n)
#  define locate_data(n)
#  define begin_packed_struct  __packed
#  define end_packed_struct
#  define reentrant_function
#  define naked_function
#  define always_inline_function
#  define inline_function inline
#  define noinline_function
#  define noinstrument_function
#  define noprofile_function
#  define nooptimiziation_function
#  define nosanitize_address
#  define nosanitize_undefined
#  define nostackprotect_function
#  define constructor_fuction
#  define destructor_function
#  define visibility_hidden
#  define visibility_default
#  define unused_code
#  define unused_data
#  define used_code
#  define used_data
#  define fopen_like
#  define popen_like
#  define malloc_like
#  define malloc_like1(a)
#  define malloc_like2(a, b)
#  define realloc_like(a)
#  define realloc_like2(a, b)
#  define format_like(a)
#  define printf_like(a, b)
#  define syslog_like(a, b)
#  define scanf_like(a, b)
#  define strftime_like(a)
#  define object_size(o, t) ((size_t)-1)

#  define FAR
#  define NEAR
#  define DSEG
#  define CODE
#  define IOBJ
#  define IPTR

#  define __asm__       asm
#  define __volatile__  volatile

/* For operatots __sfb() and __sfe() */

#  pragma section = ".bss"
#  pragma section = ".data"
#  pragma section = ".data_init"
#  pragma section = ".text"

/* Indicate that a local variable is not used */

#  ifndef UNUSED
#    define UNUSED(a) ((void)(1 || &(a)))
#  endif

#  define CONFIG_CPP_HAVE_VARARGS 1 /* Supports variable argument macros */
#  define CONFIG_HAVE_FILENAME 1    /* Has __FILE__ */
#  define CONFIG_HAVE_FLOAT 1

#  define offsetof(a, b) ((size_t)(&(((a *)(0))->b)))
#  define return_address(x) 0

#  define PRAGMA(x)
#  define unroll_loop(n)

#  define no_builtin(n)

/* Warning about usage of deprecated features. */

#  define deprecated_function

/* Memory barrier. */

#  define memory_barrier()

/* MSVC(Microsoft Visual C++)-specific definitions **************************/

#elif defined(_MSC_VER)

/* Define these here and allow specific architectures to override as needed */

#  define CONFIG_HAVE_LONG_LONG 1
#  define CONFIG_HAVE_FLOAT 1
#  define CONFIG_HAVE_DOUBLE 1
#  define CONFIG_HAVE_LONG_DOUBLE 1

/* Pre-processor */

#  undef CONFIG_CPP_HAVE_VARARGS /* No variable argument macros */
#  undef CONFIG_HAVE_ZERO_SIZE_ARRAY

/* Intriniscs */

#  define CONFIG_HAVE_FUNCTIONNAME 1 /* Has __FUNCTION__ */
#  define CONFIG_HAVE_FILENAME     1 /* Has __FILE__ */

#  undef  CONFIG_CPP_HAVE_WARNING
#  undef  CONFIG_HAVE_WEAKFUNCTIONS
#  define weak_alias(name, aliasname)
#  define weak_data
#  define weak_function
#  define weak_const_function
#  define restrict
#  define noreturn_function
#  define farcall_function
#  define pure_function
#  define const_function
#  define code_unreachable() __assume(0)
#  define predict_true(x) (x)
#  define predict_false(x) (x)
#  define is_constexpr(x)  (0)
#  define compiler_assume(x) __assume(x)
#  define aligned_data(n)

/* MSVC requires a section to be declared with its attributes before
 * _declspec(allocate) can be place an object into it
 */

#  define locate_code(n)    __declspec(code_seg(n))
#  define locate_data(n)    __pragma(section(n, read, write)) \
                            __declspec(allocate(n))
#  define begin_packed_struct __pragma(pack(push, 1))
#  define end_packed_struct __pragma(pack(pop))
#  define reentrant_function
#  define naked_function
#  define always_inline_function
#  define inline_function __forceinline
#  define noinline_function
#  define noinstrument_function
#  define noprofile_function
#  define nooptimiziation_function
#  define nosanitize_address
#  define nosanitize_undefined
#  define nostackprotect_function
#  define constructor_fuction
#  define destructor_function
#  define visibility_hidden
#  define visibility_default
#  define unused_code
#  define unused_data
#  define used_code
#  define used_data
#  define fopen_like
#  define popen_like
#  define malloc_like
#  define malloc_like1(a)
#  define malloc_like2(a, b)
#  define realloc_like(a)
#  define realloc_like2(a, b)
#  define format_like(a)
#  define printf_like(a, b)
#  define syslog_like(a, b)
#  define scanf_like(a, b)
#  define strftime_like(a)
#  define object_size(o, t) ((size_t)-1)
#  define typeof __typeof__

#  define FAR
#  define NEAR
#  define DSEG
#  define CODE
#  define IOBJ
#  define IPTR

#  undef  CONFIG_SMALL_MEMORY
#  undef  CONFIG_LONG_IS_NOT_INT
#  undef  CONFIG_PTR_IS_NOT_INT

#  ifndef UNUSED
#    define UNUSED(a) ((void)(1 || &(a)))
#  endif

#  define offsetof(a, b) ((size_t)(&(((a *)(0))->b)))
#  define return_address(x) 0

#  define PRAGMA(x)
#  define unroll_loop(n)

#  define no_builtin(n)

/* Warning about usage of deprecated features. */

#  define deprecated_function

/* Memory barrier. */

#  define memory_barrier() _ReadWriteBarrier()

/* Atomic functions. */

#  ifdef CONFIG_LIBC_ATOMIC_TOOLCHAIN
#    define atomic_store_4(obj, val, memorder) \
       _InterlockedExchange((FAR long volatile *)(obj), val)
#    define atomic_store_8(obj, val, memorder) \
       _InterlockedExchange64((FAR long long volatile *)(obj), val)
#    define atomic_load_4(obj, memorder) \
       _InterlockedOr((FAR long volatile *)(obj), 0)
#    define atomic_load_8(obj, memorder) \
       _InterlockedOr64((FAR long long volatile *)(obj), 0)
#    define atomic_fetch_add_4(obj, val, memorder) \
       _InterlockedExchangeAdd((FAR long volatile *)(obj), val)
#    define atomic_fetch_add_8(obj, val, memorder) \
       _InterlockedExchangeAdd64((FAR long long volatile *)(obj), val)
#    define atomic_fetch_sub_4(obj, val, memorder) \
       _InterlockedExchangeAdd((FAR long volatile *)(obj), -val)
#    define atomic_fetch_sub_8(obj, val, memorder) \
       _InterlockedExchangeAdd64((FAR long long volatile *)(obj), -val)
#    define atomic_fetch_and_4(obj, val, memorder) \
       _InterlockedAnd((FAR long volatile *)(obj), val)
#    define atomic_fetch_and_8(obj, val, memorder) \
       _InterlockedAnd64((FAR long long volatile *)(obj), val)
#    define atomic_fetch_or_4(obj, val, memorder) \
       _InterlockedOr((FAR long volatile *)(obj), val)
#    define atomic_fetch_or_8(obj, val, memorder) \
       _InterlockedOr64((FAR long long volatile *)(obj), val)
#    define atomic_fetch_xor_4(obj, val, memorder) \
       _InterlockedXor((FAR long volatile *)(obj), val)
#    define atomic_fetch_xor_8(obj, val, memorder) \
       _InterlockedXor64((FAR long long volatile *)(obj), val)
#    define atomic_exchange_4(obj, val, memorder) \
       _InterlockedExchange((FAR long volatile *)(obj), val)
#    define atomic_exchange_8(obj, val, memorder) \
       _InterlockedExchange64((FAR long long volatile *)(obj), val)
#    define atomic_compare_exchange_4(obj, expect, desired, weak, success, failure) \
       (_InterlockedCompareExchange((FAR long volatile *)(obj), desired, *expect) == *expect)
#    define atomic_compare_exchange_8(obj, expect, desired, weak, success, failure) \
       (_InterlockedCompareExchange64((FAR long long volatile *)(obj), desired, *expect) == *expect)
#  endif

/* TASKING (Infineon AURIX C/C++)-specific definitions **********************/

#elif defined(__TASKING__)

/* TASKING does not predefine __SIZEOF_SIZE_T__, needed by libc++ bitset */

#  define __SIZEOF_SIZE_T__             4

/* Define these here and allow specific architectures to override as needed */

#  define CONFIG_HAVE_LONG_LONG         1
#  define CONFIG_HAVE_FLOAT             1
#  define CONFIG_HAVE_DOUBLE            1
#  define CONFIG_HAVE_LONG_DOUBLE       1

/* Pre-processor */

#  define CONFIG_CPP_HAVE_VARARGS       1 /* Supports variable argument macros */

/* Intriniscs */

#  define CONFIG_HAVE_FUNCTIONNAME      1 /* Has __FUNCTION__ */
#  define CONFIG_HAVE_FILENAME          1 /* Has __FILE__ */

#  undef  CONFIG_CPP_HAVE_WARNING
#  undef  CONFIG_HAVE_WEAKFUNCTIONS
#  undef  code_unreachable
#  define weak_alias(name, aliasname)
#  define weak_data                     __attribute__((weak))
#  define weak_function                 __attribute__((weak))
#  define weak_const_function           __attribute__((weak, __const__))
#  define noreturn_function
#  define farcall_function              __attribute__((long_call))
#  define pure_function
#  define const_function
#  define predict_true(x)               (_Pragma("assume_if true") (x) _Pragma("assume_if restore"))
#  define predict_false(x)              (_Pragma("assume_if false") (x) _Pragma("assume_if restore"))
#  define is_constexpr(x)  (0)
#  define compiler_assume(x)
#  define aligned_data(n)               __attribute__((aligned(n)))
#  define locate_code(n)                __attribute__((section(n)))
#  define locate_data(n)                __attribute__((section(n)))
#  define begin_packed_struct
#  define end_packed_struct             __attribute__((packed))
#  define reentrant_function
#  define naked_function
#  define always_inline_function        __attribute__((always_inline,no_instrument_function)) inline
#  define inline_function               __attribute__((always_inline)) inline
#  define noinline_function             __attribute__((noinline))
#  define noinstrument_function
#  define noprofile_function
#  define nooptimiziation_function      __attribute__((optimize(0)))
#  define nosanitize_address
#  define nosanitize_undefined
#  define nostackprotect_function
#  define constructor_fuction
#  define destructor_function
#  define visibility_hidden
#  define visibility_default
#  define unused_code                   __attribute__((unused))
#  define unused_data                   __attribute__((unused))
#  define used_code                     __attribute__((used))
#  define used_data                     __attribute__((used))
#  define fopen_like
#  define popen_like
#  define malloc_like
#  define malloc_like1(a)
#  define malloc_like2(a, b)
#  define realloc_like(a)
#  define realloc_like2(a, b)
#  define format_like(a)
#  define printf_like(a, b)
#  define syslog_like(a, b)
#  define scanf_like(a, b)
#  define strftime_like(a)
#  define object_size(o, t) ((size_t)-1)

#  define FAR
#  define NEAR
#  define DSEG
#  define CODE
#  define IOBJ
#  define IPTR

#  undef  CONFIG_SMALL_MEMORY
#  undef  CONFIG_LONG_IS_NOT_INT
#  undef  CONFIG_PTR_IS_NOT_INT

#  ifndef UNUSED
#    define UNUSED(a) ((void)(1 || &(a)))
#  endif

#  define offsetof(a, b) ((size_t)((char *)&(((a *)(0))->b)))
#  define return_address(x) __get_return_address()

#  define PRAGMA(x)
#  define unroll_loop(n)

#  define no_builtin(n)

/* Warning about usage of deprecated features. */

#  define deprecated_function

/* Memory barrier. */

#  define memory_barrier()  __asm__ __volatile__ ("" : : : "memory")

/* Atomic functions. */

#  ifdef CONFIG_LIBC_ATOMIC_TOOLCHAIN
#    define atomic_store_4(obj, val, memorder)     __c11_atomic_store(obj, val, memorder)
#    define atomic_store_8(obj, val, memorder)     __c11_atomic_store(obj, val, memorder)
#    define atomic_load_4(obj, memorder)           __c11_atomic_load(obj, memorder)
#    define atomic_load_8(obj, memorder)           __c11_atomic_load(obj, memorder)
#    define atomic_fetch_add_4(obj, val, memorder) __c11_atomic_fetch_add(obj, val, memorder)
#    define atomic_fetch_add_8(obj, val, memorder) __c11_atomic_fetch_add(obj, val, memorder)
#    define atomic_fetch_sub_4(obj, val, memorder) __c11_atomic_fetch_sub(obj, val, memorder)
#    define atomic_fetch_sub_8(obj, val, memorder) __c11_atomic_fetch_sub(obj, val, memorder)
#    define atomic_fetch_and_4(obj, val, memorder) __c11_atomic_fetch_and(obj, val, memorder)
#    define atomic_fetch_and_8(obj, val, memorder) __c11_atomic_fetch_and(obj, val, memorder)
#    define atomic_fetch_or_4(obj, val, memorder)  __c11_atomic_fetch_or(obj, val, memorder)
#    define atomic_fetch_or_8(obj, val, memorder)  __c11_atomic_fetch_or(obj, val, memorder)
#    define atomic_fetch_xor_4(obj, val, memorder) __c11_atomic_fetch_xor(obj, val, memorder)
#    define atomic_fetch_xor_8(obj, val, memorder) __c11_atomic_fetch_xor(obj, val, memorder)
#    define atomic_exchange_4(obj, val, memorder)  __c11_atomic_exchange(obj, val, memorder)
#    define atomic_exchange_8(obj, val, memorder)  __c11_atomic_exchange(obj, val, memorder)
#    define atomic_compare_exchange_4(obj, expected, desired, weak, success, failure) \
       ((weak) ? __c11_atomic_compare_exchange_weak(obj, expected, desired, success, failure) \
               : __c11_atomic_compare_exchange_strong(obj, expected, desired, success, failure))
#    define atomic_compare_exchange_8(obj, expected, desired, weak, success, failure) \
       ((weak) ? __c11_atomic_compare_exchange_weak(obj, expected, desired, success, failure) \
               : __c11_atomic_compare_exchange_strong(obj, expected, desired, success, failure))
#  endif

/* Unknown compiler *********************************************************/

#else

#  undef  CONFIG_CPP_HAVE_VARARGS
#  undef  CONFIG_CPP_HAVE_WARNING
#  undef  CONFIG_HAVE_FUNCTIONNAME
#  undef  CONFIG_HAVE_FILENAME
#  undef  CONFIG_HAVE_WEAKFUNCTIONS
#  define weak_alias(name, aliasname)
#  define weak_data
#  define weak_function
#  define weak_const_function
#  define restrict
#  define noreturn_function
#  define farcall_function
#  define pure_function
#  define const_function
#  define predict_true(x) (x)
#  define predict_false(x) (x)
#  define is_constexpr(x)  (0)
#  define compiler_assume(x)
#  define aligned_data(n)
#  define locate_code(n)
#  define locate_data(n)
#  define begin_packed_struct
#  define end_packed_struct
#  define reentrant_function
#  define naked_function
#  define always_inline_function
#  define inline_function
#  define noinline_function
#  define noinstrument_function
#  define noprofile_function
#  define nooptimiziation_function
#  define nosanitize_address
#  define nosanitize_undefined
#  define nostackprotect_function
#  define constructor_fuction
#  define destructor_function
#  define visibility_hidden
#  define visibility_default
#  define unused_code
#  define unused_data
#  define used_code
#  define used_data
#  define fopen_like
#  define popen_like
#  define malloc_like
#  define malloc_like1(a)
#  define malloc_like2(a, b)
#  define realloc_like(a)
#  define realloc_like2(a, b)
#  define format_like(a)
#  define printf_like(a, b)
#  define syslog_like(a, b)
#  define scanf_like(a, b)
#  define strftime_like(a)
#  define object_size(o, t) ((size_t)-1)

#  define FAR
#  define NEAR
#  define DSEG
#  define CODE
#  define IOBJ
#  define IPTR

#  undef  CONFIG_SMALL_MEMORY
#  undef  CONFIG_LONG_IS_NOT_INT
#  undef  CONFIG_PTR_IS_NOT_INT
#  undef  CONFIG_HAVE_LONG_LONG
#  define CONFIG_HAVE_FLOAT 1
#  undef  CONFIG_HAVE_DOUBLE
#  undef  CONFIG_HAVE_LONG_DOUBLE

#  ifndef UNUSED
#    define UNUSED(a) ((void)(1 || &(a)))
#  endif

#  define offsetof(a, b) ((size_t)(&(((a *)(0))->b)))
#  define return_address(x) 0

#  define PRAGMA(x)
#  define unroll_loop(n)

#  define no_builtin(n)

/* Warning about usage of deprecated features. */

#  define deprecated_function

/* Memory barrier. */

#  define memory_barrier()

#endif

#ifndef is_types_compatible
#  define is_types_compatible(x, type) (0)
#endif

#ifndef CONFIG_HAVE_LONG_LONG
#  undef CONFIG_FS_LARGEFILE
#endif

#ifdef CONFIG_DISABLE_FLOAT
#  undef CONFIG_HAVE_FLOAT
#  undef CONFIG_HAVE_DOUBLE
#  undef CONFIG_HAVE_LONG_DOUBLE
#  define float  int
#  define double long
#endif

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

#ifdef __cplusplus
#define EXTERN extern "C"
extern "C"
{
#else
#define EXTERN extern
#endif

/****************************************************************************
 * Inline Functions
 ****************************************************************************/

#if !defined(__ASSEMBLY__) && !defined(code_unreachable)
noreturn_function static inline void code_unreachable(void)
{
  for (; ; );
}
#endif

#if defined(__TASKING__)

/* TASKING/TriCore intrinsic mappings */

#define CONFIG_HAVE_BUILTIN_CLZ 1
#define CONFIG_HAVE_BUILTIN_POPCOUNT 1
#define CONFIG_HAVE_BUILTIN_POPCOUNTLL 1

#define __builtin_clz(x)             __clz((int)(x))
#define __builtin_clzl(x)            __clz((int)(x))
#define __builtin_clzll(x)                                          \
  ((unsigned long long)(x) >> 32 ?                                  \
   __clz((int)((unsigned long long)(x) >> 32)) :                    \
   32 + __clz((int)(unsigned int)(x)))

#define __builtin_popcount(x)         __popcntw((unsigned int)(x))
#define __builtin_popcountl(x)        __popcntw((unsigned int)(x))
#define __builtin_popcountll(x)                                     \
  (__popcntw((unsigned int)(x)) +                                   \
   __popcntw((unsigned int)((unsigned long long)(x) >> 32)))

#define __builtin_labs(x)             __labs(x)
#define __builtin_fabsf(x)            __fabsf(x)
#define __builtin_fabs(x)             __fabs(x)
#define __builtin_parity(x)           __parity(x)

/* Compiler/language utility mappings to NuttX libc or TASKING equivalents */

#ifndef __ASSEMBLY__

extern void up_exit(int status);
#define __builtin_abort()             up_exit(1)

/* No equivalent exists.  code_unreachable() above already falls back to a
 * static inline with an endless loop when the compiler offers nothing
 * better, so reuse it rather than expand to nothing.
 */

#define __builtin_unreachable()       code_unreachable()

int __builtin_strcmp(const char *s1, const char *s2);
void *__builtin_memchr(const void *s, int c, unsigned int n);
unsigned int __builtin_strlen(const char *s);
int __builtin_memcmp(const void *s1, const void *s2, unsigned int n);
void *__builtin_memmove(void *dest, void *src, unsigned int n);

#define __builtin_memmove(x, y, z) __builtin_memmove(x, (FAR void*)y, z)
#define __builtin_memcpy __builtin_memmove

long long llabs(long long x);
#define __builtin_llabs(x)            llabs(x)

/* isnan carries the width in its name at the call site, so it can be a
 * macro like the plain math functions below.
 */

int _isnan(double x);
#define __builtin_isnan(x)            _isnan(x)
int _isnanf(float x);
#define __builtin_isnanf(x)           _isnanf(x)
int _isnanl(long double x);
#define __builtin_isnanl(x)           _isnanl(x)

/* The remaining classification builtins carry no width in their name: the
 * call site selects them by argument type, so they have to be overloads.
 * Only the libm entry points are declared here; the overloads are in the
 * extern "C++" block at the end of this file.  These are also what the
 * compiler's own isinf(), isnormal() and friends resolve to.
 */

int _isinf(double x);
int _isinff(float x);
int _isinfl(long double x);
int _isfinite(double x);
int _isfinitef(float x);
int _isfinitel(long double x);
int _signbit(double x);
int _signbitf(float x);
int _signbitl(long double x);
int _isnormal(double x);
int _isnormalf(float x);
int _isnormall(long double x);
int _fpclassify(double x);
int _fpclassifyf(float x);
int _fpclassifyl(long double x);
int _isgreater(double x, double y);
int _isgreaterf(float x, float y);
int _isgreaterl(long double x, long double y);
int _isgreaterequal(double x, double y);
int _isgreaterequalf(float x, float y);
int _isgreaterequall(long double x, long double y);
int _isless(double x, double y);
int _islessf(float x, float y);
int _islessl(long double x, long double y);
int _islessequal(double x, double y);
int _islessequalf(float x, float y);
int _islessequall(long double x, long double y);
int _islessgreater(double x, double y);
int _islessgreaterf(float x, float y);
int _islessgreaterl(long double x, long double y);
int _isunordered(double x, double y);
int _isunorderedf(float x, float y);
int _isunorderedl(long double x, long double y);

/* Math builtins mapped to TASKING libm.a
 *
 * TASKING has none of the GCC __builtin_ math names, and declaring
 * them would only move the failure to link time: no TASKING library
 * exports a symbol under a __builtin_ name.  Declare the libm entry
 * point instead and map the builtin onto it.
 */

/* Trigonometric */

float acosf(float x);
#  define __builtin_acosf(x) acosf(x)
double acos(double x);
#  define __builtin_acos(x) acos(x)
long double acosl(long double x);
#  define __builtin_acosl(x) acosl(x)
float asinf(float x);
#  define __builtin_asinf(x) asinf(x)
double asin(double x);
#  define __builtin_asin(x) asin(x)
long double asinl(long double x);
#  define __builtin_asinl(x) asinl(x)
float atanf(float x);
#  define __builtin_atanf(x) atanf(x)
double atan(double x);
#  define __builtin_atan(x) atan(x)
long double atanl(long double x);
#  define __builtin_atanl(x) atanl(x)
float atan2f(float y, float x);
#  define __builtin_atan2f(y, x) atan2f(y, x)
double atan2(double y, double x);
#  define __builtin_atan2(y, x) atan2(y, x)
long double atan2l(long double y, long double x);
#  define __builtin_atan2l(y, x) atan2l(y, x)
float cosf(float x);
#  define __builtin_cosf(x) cosf(x)
double cos(double x);
#  define __builtin_cos(x) cos(x)
long double cosl(long double x);
#  define __builtin_cosl(x) cosl(x)
float sinf(float x);
#  define __builtin_sinf(x) sinf(x)
double sin(double x);
#  define __builtin_sin(x) sin(x)
long double sinl(long double x);
#  define __builtin_sinl(x) sinl(x)
float tanf(float x);
#  define __builtin_tanf(x) tanf(x)
double tan(double x);
#  define __builtin_tan(x) tan(x)
long double tanl(long double x);
#  define __builtin_tanl(x) tanl(x)

/* Hyperbolic */

float acoshf(float x);
#  define __builtin_acoshf(x) acoshf(x)
double acosh(double x);
#  define __builtin_acosh(x) acosh(x)
long double acoshl(long double x);
#  define __builtin_acoshl(x) acoshl(x)
float asinhf(float x);
#  define __builtin_asinhf(x) asinhf(x)
double asinh(double x);
#  define __builtin_asinh(x) asinh(x)
long double asinhl(long double x);
#  define __builtin_asinhl(x) asinhl(x)
float atanhf(float x);
#  define __builtin_atanhf(x) atanhf(x)
double atanh(double x);
#  define __builtin_atanh(x) atanh(x)
long double atanhl(long double x);
#  define __builtin_atanhl(x) atanhl(x)
float coshf(float x);
#  define __builtin_coshf(x) coshf(x)
double cosh(double x);
#  define __builtin_cosh(x) cosh(x)
long double coshl(long double x);
#  define __builtin_coshl(x) coshl(x)
float sinhf(float x);
#  define __builtin_sinhf(x) sinhf(x)
double sinh(double x);
#  define __builtin_sinh(x) sinh(x)
long double sinhl(long double x);
#  define __builtin_sinhl(x) sinhl(x)
float tanhf(float x);
#  define __builtin_tanhf(x) tanhf(x)
double tanh(double x);
#  define __builtin_tanh(x) tanh(x)
long double tanhl(long double x);
#  define __builtin_tanhl(x) tanhl(x)

/* Exponential and logarithmic */

float expf(float x);
#  define __builtin_expf(x) expf(x)
double exp(double x);
#  define __builtin_exp(x) exp(x)
long double expl(long double x);
#  define __builtin_expl(x) expl(x)
float expm1f(float x);
#  define __builtin_expm1f(x) expm1f(x)
double expm1(double x);
#  define __builtin_expm1(x) expm1(x)
long double expm1l(long double x);
#  define __builtin_expm1l(x) expm1l(x)
float frexpf(float x, int *exp);
#  define __builtin_frexpf(x, exp) frexpf(x, exp)
double frexp(double x, int *exp);
#  define __builtin_frexp(x, exp) frexp(x, exp)
long double frexpl(long double x, int *exp);
#  define __builtin_frexpl(x, exp) frexpl(x, exp)
float ldexpf(float x, int exp);
#  define __builtin_ldexpf(x, exp) ldexpf(x, exp)
double ldexp(double x, int exp);
#  define __builtin_ldexp(x, exp) ldexp(x, exp)
long double ldexpl(long double x, int exp);
#  define __builtin_ldexpl(x, exp) ldexpl(x, exp)
float logf(float x);
#  define __builtin_logf(x) logf(x)
double log(double x);
#  define __builtin_log(x) log(x)
long double logl(long double x);
#  define __builtin_logl(x) logl(x)
float log10f(float x);
#  define __builtin_log10f(x) log10f(x)
double log10(double x);
#  define __builtin_log10(x) log10(x)
long double log10l(long double x);
#  define __builtin_log10l(x) log10l(x)
float log2f(float x);
#  define __builtin_log2f(x) log2f(x)
double log2(double x);
#  define __builtin_log2(x) log2(x)
long double log2l(long double x);
#  define __builtin_log2l(x) log2l(x)

/* Power and root */

float powf(float x, float y);
#  define __builtin_powf(x, y) powf(x, y)
double pow(double x, double y);
#  define __builtin_pow(x, y) pow(x, y)
long double powl(long double x, long double y);
#  define __builtin_powl(x, y) powl(x, y)
float sqrtf(float x);
#  define __builtin_sqrtf(x) sqrtf(x)
double sqrt(double x);
#  define __builtin_sqrt(x) sqrt(x)
long double sqrtl(long double x);
#  define __builtin_sqrtl(x) sqrtl(x)

/* Rounding and remainder */

extern float ceilf(float);
#define __builtin_ceilf(x)            ceilf(x)
extern long double ceill(long double);
#define __builtin_ceill(x)            ceill(x)
float floorf(float x);
#  define __builtin_floorf(x) floorf(x)
double floor(double x);
#  define __builtin_floor(x) floor(x)
long double floorl(long double x);
#  define __builtin_floorl(x) floorl(x)
float fmodf(float x, float y);
#  define __builtin_fmodf(x, y) fmodf(x, y)
double fmod(double x, double y);
#  define __builtin_fmod(x, y) fmod(x, y)
long double fmodl(long double x, long double y);
#  define __builtin_fmodl(x, y) fmodl(x, y)
float modff(float x, float *iptr);
#  define __builtin_modff(x, iptr) modff(x, iptr)
double modf(double x, double *iptr);
#  define __builtin_modf(x, iptr) modf(x, iptr)
long double modfl(long double x, long double *iptr);
#  define __builtin_modfl(x, iptr) modfl(x, iptr)
float rintf(float x);
#  define __builtin_rintf(x) rintf(x)
double rint(double x);
#  define __builtin_rint(x) rint(x)
long double rintl(long double x);
#  define __builtin_rintl(x) rintl(x)
float roundf(float x);
#  define __builtin_roundf(x) roundf(x)
double round(double x);
#  define __builtin_round(x) round(x)
long double roundl(long double x);
#  define __builtin_roundl(x) roundl(x)
float truncf(float x);
#  define __builtin_truncf(x) truncf(x)
double trunc(double x);
#  define __builtin_trunc(x) trunc(x)
long double truncl(long double x);
#  define __builtin_truncl(x) truncl(x)
long long llroundf(float x);
#  define __builtin_llroundf(x) llroundf(x)
long long llroundl(long double x);
#  define __builtin_llroundl(x) llroundl(x)
long lroundf(float x);
#  define __builtin_lroundf(x) lroundf(x)
long lroundl(long double x);
#  define __builtin_lroundl(x) lroundl(x)
float scalbnf(float x, int n);
#  define __builtin_scalbnf(x, n) scalbnf(x, n)
double scalbn(double x, int n);
#  define __builtin_scalbn(x, n) scalbn(x, n)
long double scalbnl(long double x, int n);
#  define __builtin_scalbnl(x, n) scalbnl(x, n)

/* Absolute value, sign, min/max */

long double fabsl(long double x);
#  define __builtin_fabsl(x) fabsl(x)
float copysignf(float x, float y);
#  define __builtin_copysignf(x, y) copysignf(x, y)
long double copysignl(long double x, long double y);
#  define __builtin_copysignl(x, y) copysignl(x, y)
double copysign(double x, double y);
#  define __builtin_copysign(x, y) copysign(x, y)
float fmaxf(float x, float y);
#  define __builtin_fmaxf(x, y) fmaxf(x, y)
long double fmaxl(long double x, long double y);
#  define __builtin_fmaxl(x, y) fmaxl(x, y)
double fmax(double x, double y);
#  define __builtin_fmax(x, y) fmax(x, y)
float fminf(float x, float y);
#  define __builtin_fminf(x, y) fminf(x, y)
double fmin(double x, double y);
#  define __builtin_fmin(x, y) fmin(x, y)
long double fminl(long double x, long double y);
#  define __builtin_fminl(x, y) fminl(x, y)

/* Error function */

float erff(float x);
#  define __builtin_erff(x) erff(x)
double erf(double x);
#  define __builtin_erf(x) erf(x)
long double erfl(long double x);
#  define __builtin_erfl(x) erfl(x)
float erfcf(float x);
#  define __builtin_erfcf(x) erfcf(x)
double erfc(double x);
#  define __builtin_erfc(x) erfc(x)
long double erfcl(long double x);
#  define __builtin_erfcl(x) erfcl(x)

/* Added by C99 */

float cbrtf(float x);
#  define __builtin_cbrtf(x) cbrtf(x)
double cbrt(double x);
#  define __builtin_cbrt(x) cbrt(x)
long double cbrtl(long double x);
#  define __builtin_cbrtl(x) cbrtl(x)
float exp2f(float x);
#  define __builtin_exp2f(x) exp2f(x)
double exp2(double x);
#  define __builtin_exp2(x) exp2(x)
long double exp2l(long double x);
#  define __builtin_exp2l(x) exp2l(x)
float fdimf(float x, float y);
#  define __builtin_fdimf(x, y) fdimf(x, y)
double fdim(double x, double y);
#  define __builtin_fdim(x, y) fdim(x, y)
long double fdiml(long double x, long double y);
#  define __builtin_fdiml(x, y) fdiml(x, y)
float fmaf(float x, float y, float z);
#  define __builtin_fmaf(x, y, z) fmaf(x, y, z)
double fma(double x, double y, double z);
#  define __builtin_fma(x, y, z) fma(x, y, z)
long double fmal(long double x, long double y, long double z);
#  define __builtin_fmal(x, y, z) fmal(x, y, z)
float hypotf(float x, float y);
#  define __builtin_hypotf(x, y) hypotf(x, y)
double hypot(double x, double y);
#  define __builtin_hypot(x, y) hypot(x, y)
long double hypotl(long double x, long double y);
#  define __builtin_hypotl(x, y) hypotl(x, y)
int ilogbf(float x);
#  define __builtin_ilogbf(x) ilogbf(x)
int ilogbl(long double x);
#  define __builtin_ilogbl(x) ilogbl(x)
float lgammaf(float x);
#  define __builtin_lgammaf(x) lgammaf(x)
double lgamma(double x);
#  define __builtin_lgamma(x) lgamma(x)
long double lgammal(long double x);
#  define __builtin_lgammal(x) lgammal(x)
long long llrintf(float x);
#  define __builtin_llrintf(x) llrintf(x)
long long llrintl(long double x);
#  define __builtin_llrintl(x) llrintl(x)
float log1pf(float x);
#  define __builtin_log1pf(x) log1pf(x)
double log1p(double x);
#  define __builtin_log1p(x) log1p(x)
long double log1pl(long double x);
#  define __builtin_log1pl(x) log1pl(x)
float logbf(float x);
#  define __builtin_logbf(x) logbf(x)
double logb(double x);
#  define __builtin_logb(x) logb(x)
long double logbl(long double x);
#  define __builtin_logbl(x) logbl(x)
long lrintf(float x);
#  define __builtin_lrintf(x) lrintf(x)
long lrintl(long double x);
#  define __builtin_lrintl(x) lrintl(x)
float nearbyintf(float x);
#  define __builtin_nearbyintf(x) nearbyintf(x)
double nearbyint(double x);
#  define __builtin_nearbyint(x) nearbyint(x)
long double nearbyintl(long double x);
#  define __builtin_nearbyintl(x) nearbyintl(x)
float nextafterf(float x, float y);
#  define __builtin_nextafterf(x, y) nextafterf(x, y)
double nextafter(double x, double y);
#  define __builtin_nextafter(x, y) nextafter(x, y)
long double nextafterl(long double x, long double y);
#  define __builtin_nextafterl(x, y) nextafterl(x, y)
float nexttowardf(float x, long double y);
#  define __builtin_nexttowardf(x, y) nexttowardf(x, y)
double nexttoward(double x, long double y);
#  define __builtin_nexttoward(x, y) nexttoward(x, y)
long double nexttowardl(long double x, long double y);
#  define __builtin_nexttowardl(x, y) nexttowardl(x, y)
float remainderf(float x, float y);
#  define __builtin_remainderf(x, y) remainderf(x, y)
double remainder(double x, double y);
#  define __builtin_remainder(x, y) remainder(x, y)
long double remainderl(long double x, long double y);
#  define __builtin_remainderl(x, y) remainderl(x, y)
float remquof(float x, float y, int *quo);
#  define __builtin_remquof(x, y, quo) remquof(x, y, quo)
double remquo(double x, double y, int *quo);
#  define __builtin_remquo(x, y, quo) remquo(x, y, quo)
long double remquol(long double x, long double y, int *quo);
#  define __builtin_remquol(x, y, quo) remquol(x, y, quo)
float scalblnf(float x, long int n);
#  define __builtin_scalblnf(x, n) scalblnf(x, n)
double scalbln(double x, long int n);
#  define __builtin_scalbln(x, n) scalbln(x, n)
long double scalblnl(long double x, long int n);
#  define __builtin_scalblnl(x, n) scalblnl(x, n)
float tgammaf(float x);
#  define __builtin_tgammaf(x) tgammaf(x)
double tgamma(double x);
#  define __builtin_tgamma(x) tgamma(x)
long double tgammal(long double x);
#  define __builtin_tgammal(x) tgammal(x)

#endif  // __ASSEMBLY__

#endif  // __TASKING__

#undef EXTERN
#ifdef __cplusplus
}
#endif

/* overloaded version of builtin functions c++ needed */

#if defined(__cplusplus) && defined(__TASKING__)

extern "C++"
{
  inline int __builtin_isinf(float x)
  {
    return _isinff(x);
  }

  inline int __builtin_isinf(double x)
  {
    return _isinf(x);
  }

  inline int __builtin_isinf(long double x)
  {
    return _isinfl(x);
  }

  inline int __builtin_isfinite(float x)
  {
    return _isfinitef(x);
  }

  inline int __builtin_isfinite(double x)
  {
    return _isfinite(x);
  }

  inline int __builtin_isfinite(long double x)
  {
    return _isfinitel(x);
  }

  inline bool __builtin_signbit(float x)
  {
    return _signbitf(x) != 0;
  }

  inline bool __builtin_signbit(double x)
  {
    return _signbit(x) != 0;
  }

  inline bool __builtin_signbit(long double x)
  {
    return _signbitl(x) != 0;
  }

  /* __builtin_fpclassify picks whichever of its five code arguments
   * matches the class of x.  _fpclassify already returns the FP_ codes,
   * and those are what libc++ passes in, so hand its result back
   * unchanged.  The codes cannot be compared here because <math.h>
   * defines them and this header cannot include it.
   */

  inline int __builtin_fpclassify(int, int, int, int, int, float x)
  {
    return _fpclassifyf(x);
  }

  inline int __builtin_fpclassify(int, int, int, int, int, double x)
  {
    return _fpclassify(x);
  }

  inline int __builtin_fpclassify(int, int, int, int, int, long double x)
  {
    return _fpclassifyl(x);
  }

  inline bool __builtin_isnormal(float x)
  {
    return _isnormalf(x) != 0;
  }

  inline bool __builtin_isnormal(double x)
  {
    return _isnormal(x) != 0;
  }

  inline bool __builtin_isnormal(long double x)
  {
    return _isnormall(x) != 0;
  }

  /* The ordered comparison builtins take two arguments of the same type,
   * so they are selected by argument type as well.  They must not raise an
   * exception for a NaN operand, which is why libm carries them rather
   * than the plain relational operators being used here.
   */

  inline bool __builtin_isgreater(float x, float y)
  {
    return _isgreaterf(x, y) != 0;
  }

  inline bool __builtin_isgreater(double x, double y)
  {
    return _isgreater(x, y) != 0;
  }

  inline bool __builtin_isgreater(long double x, long double y)
  {
    return _isgreaterl(x, y) != 0;
  }

  inline bool __builtin_isgreaterequal(float x, float y)
  {
    return _isgreaterequalf(x, y) != 0;
  }

  inline bool __builtin_isgreaterequal(double x, double y)
  {
    return _isgreaterequal(x, y) != 0;
  }

  inline bool __builtin_isgreaterequal(long double x, long double y)
  {
    return _isgreaterequall(x, y) != 0;
  }

  inline bool __builtin_isless(float x, float y)
  {
    return _islessf(x, y) != 0;
  }

  inline bool __builtin_isless(double x, double y)
  {
    return _isless(x, y) != 0;
  }

  inline bool __builtin_isless(long double x, long double y)
  {
    return _islessl(x, y) != 0;
  }

  inline bool __builtin_islessequal(float x, float y)
  {
    return _islessequalf(x, y) != 0;
  }

  inline bool __builtin_islessequal(double x, double y)
  {
    return _islessequal(x, y) != 0;
  }

  inline bool __builtin_islessequal(long double x, long double y)
  {
    return _islessequall(x, y) != 0;
  }

  inline bool __builtin_islessgreater(float x, float y)
  {
    return _islessgreaterf(x, y) != 0;
  }

  inline bool __builtin_islessgreater(double x, double y)
  {
    return _islessgreater(x, y) != 0;
  }

  inline bool __builtin_islessgreater(long double x, long double y)
  {
    return _islessgreaterl(x, y) != 0;
  }

  inline bool __builtin_isunordered(float x, float y)
  {
    return _isunorderedf(x, y) != 0;
  }

  inline bool __builtin_isunordered(double x, double y)
  {
    return _isunordered(x, y) != 0;
  }

  inline bool __builtin_isunordered(long double x, long double y)
  {
    return _isunorderedl(x, y) != 0;
  }
}

#endif  // __cplusplus

#endif /* __INCLUDE_NUTTX_COMPILER_H */
