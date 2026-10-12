/****************************************************************************
 * libs/libxx/libcxxabi/fundamental_type_info.cpp
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

/* cptc describes every class through its type info object even with RTTI off,
 * so private_typeinfo.cpp has to be built, and the typeid in its can_catch
 * then has nothing to resolve against:
 *
 *   ltc E106: unresolved external: Typeinfo for void
 *             - (private_typeinfo.cpp.o)
 *
 * GCC and Clang emit the objects for every fundamental type in the
 * translation unit that defines ~__fundamental_type_info(); cptc expects them
 * from its own libcp.a, which collides with libc++abi on 61 symbols.
 *
 * Only void and std::nullptr_t are defined, and only so that the link
 * resolves -- can_catch is unreachable while exceptions are off, so nothing
 * reads them.  What a complete set would need is kept under the #if 0 at the
 * end of the file.
 *
 * The layout is the one both tricore-gcc and TASKING libcp.a emit: the vtable
 * pointer at offset 0 with an addend of 8, past the offset-to-top and type
 * info slots, then the name.  Going through a constructor instead would put
 * the object in .data behind a static initialiser, because type_info has no
 * constexpr constructor.
 *
 * This has to be a separate translation unit.  Declaring the objects where
 * typeid is used makes cptc emit its own declaration of the same symbol with
 * a different type, which the C stage rejects:
 *
 *   ctc E289: "_ZTIv" redeclared with a different type
 */

extern "C"
{
  /* The vtable comes from private_typeinfo.cpp, which TASKING always
   * builds.
   */

  extern const void *const _ZTVN10__cxxabiv123__fundamental_type_infoE[];

  struct fundamental_type_info
  {
    const void *vptr;
    const char *name;
  };

/* const at namespace scope has internal linkage, so each object needs an
 * explicit declaration to be exported.  weak matches what the two reference
 * implementations emit.
 */

#define DEFINE_FUNDAMENTAL(code)                                    \
  extern const char _ZTS##code[];                                   \
  extern const fundamental_type_info _ZTI##code;                    \
                                                                    \
  __attribute__((weak)) const char _ZTS##code[] = #code;            \
                                                                    \
  __attribute__((weak))                                             \
  const fundamental_type_info _ZTI##code =                          \
  {                                                                 \
    &_ZTVN10__cxxabiv123__fundamental_type_infoE[2], _ZTS##code     \
  };

  DEFINE_FUNDAMENTAL(v)               /* void                     */
  DEFINE_FUNDAMENTAL(Dn)              /* std::nullptr_t           */

/* The rest of what TASKING libcp.a provides.  GCC emits three objects per
 * fundamental type -- the type, a pointer to it and a pointer to the const
 * form -- so enabling RTTI or exceptions needs the pointer forms as well as
 * the remaining 19 types.
 *
 * Do not enable this without testing.  The layout has been checked against
 * tricore-gcc and libcp.a, but no path in this port reads a type info object
 * at run time, so nothing has exercised typeid comparison, catch matching or
 * dynamic_cast against them.  The manglings are spelled out rather than
 * derived from typeid so that char8_t gets an object at C++14 too, which
 * also means a wrong letter goes unnoticed until link time.
 */

#if 0
  extern const void *const _ZTVN10__cxxabiv119__pointer_type_infoE[];

  struct pointer_type_info
  {
    const void *vptr;
    const char *name;
    unsigned int flags;
    const void *pointee;
  };

/* 0x1 is __pbase_type_info::__const_mask, the only flag a const-qualified
 * pointer to a fundamental type carries.
 */

#  define DEFINE_POINTERS(code)                                     \
  extern const char _ZTSP##code[];                                  \
  extern const char _ZTSPK##code[];                                 \
  extern const pointer_type_info _ZTIP##code;                       \
  extern const pointer_type_info _ZTIPK##code;                      \
                                                                    \
  __attribute__((weak)) const char _ZTSP##code[] = "P" #code;       \
  __attribute__((weak)) const char _ZTSPK##code[] = "PK" #code;     \
                                                                    \
  __attribute__((weak))                                             \
  const pointer_type_info _ZTIP##code =                             \
  {                                                                 \
    &_ZTVN10__cxxabiv119__pointer_type_infoE[2],                    \
    _ZTSP##code, 0, &_ZTI##code                                     \
  };                                                                \
                                                                    \
  __attribute__((weak))                                             \
  const pointer_type_info _ZTIPK##code =                            \
  {                                                                 \
    &_ZTVN10__cxxabiv119__pointer_type_infoE[2],                    \
    _ZTSPK##code, 0x1, &_ZTI##code                                  \
  };

  DEFINE_POINTERS(v)                            /* void             */
  DEFINE_POINTERS(Dn)                           /* std::nullptr_t   */

  DEFINE_FUNDAMENTAL(a)  DEFINE_POINTERS(a)     /* signed char      */
  DEFINE_FUNDAMENTAL(b)  DEFINE_POINTERS(b)     /* bool             */
  DEFINE_FUNDAMENTAL(c)  DEFINE_POINTERS(c)     /* char             */
  DEFINE_FUNDAMENTAL(d)  DEFINE_POINTERS(d)     /* double           */
  DEFINE_FUNDAMENTAL(Di) DEFINE_POINTERS(Di)    /* char32_t         */
  DEFINE_FUNDAMENTAL(Ds) DEFINE_POINTERS(Ds)    /* char16_t         */
  DEFINE_FUNDAMENTAL(Du) DEFINE_POINTERS(Du)    /* char8_t          */
  DEFINE_FUNDAMENTAL(e)  DEFINE_POINTERS(e)     /* long double      */
  DEFINE_FUNDAMENTAL(f)  DEFINE_POINTERS(f)     /* float            */
  DEFINE_FUNDAMENTAL(h)  DEFINE_POINTERS(h)     /* unsigned char    */
  DEFINE_FUNDAMENTAL(i)  DEFINE_POINTERS(i)     /* int              */
  DEFINE_FUNDAMENTAL(j)  DEFINE_POINTERS(j)     /* unsigned int     */
  DEFINE_FUNDAMENTAL(l)  DEFINE_POINTERS(l)     /* long             */
  DEFINE_FUNDAMENTAL(m)  DEFINE_POINTERS(m)     /* unsigned long    */
  DEFINE_FUNDAMENTAL(s)  DEFINE_POINTERS(s)     /* short            */
  DEFINE_FUNDAMENTAL(t)  DEFINE_POINTERS(t)     /* unsigned short   */
  DEFINE_FUNDAMENTAL(w)  DEFINE_POINTERS(w)     /* wchar_t          */
  DEFINE_FUNDAMENTAL(x)  DEFINE_POINTERS(x)     /* long long        */
  DEFINE_FUNDAMENTAL(y)  DEFINE_POINTERS(y)     /* ull              */

#  undef DEFINE_POINTERS
#endif

#undef DEFINE_FUNDAMENTAL
}
