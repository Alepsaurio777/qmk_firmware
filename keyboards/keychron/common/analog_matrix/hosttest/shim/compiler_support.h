// Shim de host: sustituye a quantum/compiler_support.h.
#pragma once

#ifndef STATIC_ASSERT
#    define STATIC_ASSERT(cond, msg) _Static_assert(cond, msg)
#endif

#ifndef ARRAY_SIZE
#    define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#endif
