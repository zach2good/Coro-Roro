#pragma once

//
// unreachable
//
//   Platform-specific unreachable code marker (will assert and cause a crash at runtime).
//   TODO: If we upgrade to C++23, use std::unreachable
//
#ifdef __GNUC__ // GCC 4.8+, Clang, Intel and other compilers compatible with GCC (-std=c++0x or above)
[[noreturn]] inline __attribute__((always_inline)) void unreachable()
{
    __builtin_unreachable();
}
#elif defined(_MSC_VER) // MSVC
[[noreturn]] __forceinline void unreachable()
{
    __assume(false);
}
#else
inline void unreachable()
{
}
#endif

//
// ASSERT_UNREACHABLE
//
//   Marks code paths that should never be reached (will assert and cause a crash at runtime).
//
#define ASSERT_UNREACHABLE() unreachable()

//
// MOVE_ONLY
//
//   Macro for declaring move-only semantics (disables copy and move).
//
#define MOVE_ONLY(ClassName)                         \
    ClassName(const ClassName&)            = delete; \
    ClassName& operator=(const ClassName&) = delete; \
    ClassName(ClassName&&)                 = delete; \
    ClassName& operator=(ClassName&&)      = delete

//
// NO_MOVE_NO_COPY
//
//   Macro for classes that should not be moved or copied.
//
#define NO_MOVE_NO_COPY(ClassName)                   \
    ClassName(const ClassName&)            = delete; \
    ClassName& operator=(const ClassName&) = delete; \
    ClassName(ClassName&&)                 = delete; \
    ClassName& operator=(ClassName&&)      = delete
