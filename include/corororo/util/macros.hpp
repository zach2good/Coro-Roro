#pragma once

namespace CoroRoro
{

// Simple macro definitions for backward compatibility
#define MOVE_ONLY(ClassName) \
    ClassName(ClassName&&) = default; \
    ClassName& operator=(ClassName&&) = default

#define COPY_ONLY(ClassName) \
    ClassName(const ClassName&) = default; \
    ClassName& operator=(const ClassName&) = default

#define NO_COPY_NO_MOVE(ClassName) \
    ClassName(const ClassName&) = delete; \
    ClassName(ClassName&&) = delete; \
    ClassName& operator=(const ClassName&) = delete; \
    ClassName& operator=(ClassName&&) = delete

} // namespace CoroRoro
