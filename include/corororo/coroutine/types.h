#pragma once

#include <cstdint>

namespace CoroRoro
{

// Thread affinity types
enum class ThreadAffinity : uint32_t
{
    Main = 1,
    Worker = 2
};

// Compiler-specific optimization macros for cross-platform compatibility
#if defined(__GNUC__) || defined(__clang__)
    #define CORO_LIKELY(expr) __builtin_expect(!!(expr), 1)
    #define CORO_UNLIKELY(expr) __builtin_expect(!!(expr), 0)
    #define CORO_HOT __attribute__((hot))
    #define CORO_COLD __attribute__((cold))
    #define CORO_INLINE __attribute__((always_inline))
#elif defined(_MSC_VER)
    #define CORO_LIKELY(expr) (expr)
    #define CORO_UNLIKELY(expr) (expr)
    #define CORO_HOT
    #define CORO_COLD
    #define CORO_INLINE __forceinline
#else
    #define CORO_LIKELY(expr) (expr)
    #define CORO_UNLIKELY(expr) (expr)
    #define CORO_HOT
    #define CORO_COLD
    #define CORO_INLINE inline
#endif

// Task and AsyncTask are now aliases defined in task.h

} // namespace CoroRoro