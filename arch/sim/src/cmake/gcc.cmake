# ##############################################################################
# arch/sim/src/cmake/gcc.cmake
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

set(CMAKE_ASM_COMPILER gcc)
set(CMAKE_C_COMPILER gcc)
set(CMAKE_CXX_COMPILER g++)
set(CMAKE_PREPROCESSOR ${CMAKE_C_COMPILER} -E -P -x c)
set(CMAKE_STRIP strip --strip-unneeded)
set(CMAKE_OBJCOPY objcopy)
set(CMAKE_OBJDUMP objdump)
set(CMAKE_LINKER ld)
set(CMAKE_LD ld)
set(CMAKE_AR ar)
set(CMAKE_NM nm)
set(CMAKE_RANLIB ranlib)

if(CONFIG_LTO_FULL)
  add_compile_options(-flto)
elseif(CONFIG_LTO_FAT)
  add_compile_options(-flto -ffat-lto-objects)
endif()

if(CONFIG_COVERAGE_ALL)
  if(CONFIG_COVERAGE_SOURCE)
    add_compile_options(-fprofile-arcs -ftest-coverage -fno-inline)
  elseif(CONFIG_COVERAGE_PGO)
    file(GLOB_RECURSE GCDA_FILES "${CMAKE_BINARY_DIR}/*.gcda")
    if(GCDA_FILES)
      add_compile_options(-fprofile-use)
    else()
      add_compile_options(-fprofile-generate -fprofile-update=atomic)
    endif()
  endif()
endif()

if(APPLE)
  add_link_options(-Wl,-dead_strip)
else()
  add_link_options(-Wl,--gc-sections)
  add_link_options(-Wl,-Ttext-segment=0x40000000)
endif()
