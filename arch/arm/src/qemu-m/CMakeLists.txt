# ##############################################################################
# arch/arm/src/qemu-m/CMakeLists.txt
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

# Only the qemu-m chip's own sources are registered here. The reused arm_m core
# layer is added by arch/arm/src/CMakeLists.txt via ${ARCH_SUBDIR}, so
# arch/arm/src/arm_m stays zero-diff (REQ-6.5 / NFR-4).

set(SRCS qemu_m_start.c qemu_m_serial.c qemu_m_irq.c qemu_m_timer.c
         qemu_m_allocateheap.c qemu_m_cpuindex.c)

if(CONFIG_RTC)
  list(APPEND SRCS qemu_m_rtc.c)
endif()

target_sources(arch PRIVATE ${SRCS})
