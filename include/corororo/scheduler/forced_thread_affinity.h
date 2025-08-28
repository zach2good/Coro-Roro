#pragma once

#include <cstdint>

namespace CoroRoro
{

//
// ForcedThreadAffinity
//
//   Forced thread affinity for overriding task execution location.
//
//   When specified, forces the ENTIRE execution graph to run on the
//   specified thread type with atomic execution (no suspension or
//   thread switching).
//
enum class ForcedThreadAffinity : std::uint8_t
{
    MainThread,   // Force entire execution graph to main thread (atomic execution)
    WorkerThread, // Force entire execution graph to worker thread (atomic execution)
};

} // namespace CoroRoro
