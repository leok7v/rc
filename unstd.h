#ifndef unstd_included // unified `unstandard` (non-standard) std header
#define unstd_included

// Copyright (c) 2024, "Leo" Dmitry Kuznetsov
// This code and the accompanying materials are made available under the terms
// of BSD-3 license, which accompanies this distribution. The full text of the
// license may be found at https://opensource.org/license/bsd-3-clause

#include "rt.h"

#ifndef UNSTD_NO_RT_IMPLEMENTATION
#define rt_implementation
#include "rt.h" // implement all functions in header file
#endif

#ifndef UNSTD_NO_SHORTHAND

#undef assert
#undef countof
#undef max
#undef min
#undef printf
#undef println
#undef swap

#ifdef UNSTD_ASSERTS_IN_RELEASE
#define assert  rt_swear
#else
#define assert  rt_assert
#endif

#define countof rt_countof
#define max     rt_max
#define min     rt_min
#define printf  rt_printf
#define println rt_println
#define swap    rt_swap

#endif

#endif
