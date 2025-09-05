#pragma once

//
// Macros
//

#if defined(__GNUC__) || defined(__clang__)
#define FORCE_INLINE inline __attribute__((always_inline))
#define HOT_PATH     __attribute__((hot))
#define COLD_PATH    __attribute__((cold))
#else
#define FORCE_INLINE inline
#define HOT_PATH
#define COLD_PATH
#endif

#define LIKELY   [[likely]]
#define UNLIKELY [[unlikely]]

#define CACHE_ALIGN alignas(std::hardware_destructive_interference_size)

#define NO_DISCARD [[nodiscard]]

#define ASSERT_UNREACHABLE() std::abort()
