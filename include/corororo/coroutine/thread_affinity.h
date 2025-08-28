#pragma once

#include <cstdint>
#include <string_view>

namespace CoroRoro
{

//
// ThreadAffinity
//
//   Specifies which thread a coroutine should execute on.
//
enum class ThreadAffinity : std::uint8_t
{
    MainThread,
    WorkerThread,
};

constexpr auto toString(ThreadAffinity affinity) -> std::string_view
{
    switch (affinity)
    {
        case ThreadAffinity::MainThread:
            return "MainThread";
        case ThreadAffinity::WorkerThread:
            return "WorkerThread";
    }
    return "Unknown";
}

} // namespace CoroRoro
