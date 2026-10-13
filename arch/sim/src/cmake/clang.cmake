# ##############################################################################
# arch/sim/src/cmake/clang.cmake
#
# SPDX-License-Identifier: Apache-2.0
#
# Licensed to the Apache Software Foundation (ASF) under one or more contributor
# license agreements.  See the NOTICE file distributed with this work for
# additional information regarding copyright ownership.  The ASF licenses this
# file to you under the Apache License, Version 2.0 (the "License"); you may not
# use this file except in compliance with the License.  You may obtain a copy of
# the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
# WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.  See the
# License for the specific language governing permissions and limitations under
# the License.
#
# ##############################################################################

set(CMAKE_C_COMPILER_FORCED TRUE)
set(CMAKE_CXX_COMPILER_FORCED TRUE)

set(CMAKE_ASM_COMPILER clang)
set(CMAKE_C_COMPILER clang)
set(CMAKE_CXX_COMPILER clang++)
set(CMAKE_PREPROCESSOR clang -E -P -x c)
set(CMAKE_STRIP llvm-strip --strip-unneeded)
set(CMAKE_OBJCOPY llvm-objcopy)
set(CMAKE_OBJDUMP llvm-objdump)
set(CMAKE_LINKER ld.lld)
set(CMAKE_LD ld.lld)
set(CMAKE_AR llvm-ar)
set(CMAKE_NM llvm-nm)
set(CMAKE_RANLIB llvm-ranlib)

add_link_options(-no-pie)
add_link_options(-fuse-ld=lld)

if(CONFIG_LTO_THIN)
  add_compile_options(-flto=thin)
elseif(CONFIG_LTO_FULL)
  add_compile_options(-flto)
elseif(CONFIG_LTO_FAT)
  add_compile_options(-flto -ffat-lto-objects)
endif()

if(CONFIG_DEBUG_SYMBOLS)
  # Generate .debug_macro section for GDB macro support. Use DWARF v4 for better
  # readelf compatibility. GCC does not support -fdebug-macro.
  add_compile_options(-gdwarf-4 -fdebug-macro)
endif()

if(CONFIG_COVERAGE_ALL)
  if(CONFIG_COVERAGE_SOURCE)
    add_compile_options(-fprofile-arcs -ftest-coverage -fno-inline)
  elseif(CONFIG_COVERAGE_PGO)
    if(EXISTS "${CMAKE_BINARY_DIR}/default.profdata")
      add_compile_options(-fprofile-use)
    else()
      add_compile_options(-fprofile-generate -fprofile-update=atomic)
    endif()
  endif()
endif()

if(APPLE)
  add_link_options(-Wl,-dead_strip)
else()
  # The text segment base is pinned inside the generated ld script, since lld
  # cannot override SEGMENT_START()'s default from the command line.
  add_link_options(-Wl,--gc-sections)
endif()
