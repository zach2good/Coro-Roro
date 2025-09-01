#pragma once

#include <cstdint>

namespace CoroRoro
{

// Thread affinity types for backward compatibility
enum class ThreadAffinity : uint32_t
{
    Any = 0,
    Main = 1,
    Worker = 2,
    Forced = 3
};

// Task state types for backward compatibility
enum class TaskState : uint32_t
{
    Suspended = 0,
    Running = 1,
    Done = 2,
    Failed = 3
};

// Priority levels for scheduling
enum class Priority : uint32_t
{
    Low = 0,
    Normal = 1,
    High = 2
};

} // namespace CoroRoro
