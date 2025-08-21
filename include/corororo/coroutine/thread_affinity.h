#pragma once

#include <cstdint>
#include <string_view>

namespace CoroRoro
{

enum class ThreadAffinity : uint8_t
{
    MainThread,
    WorkerThread,
};

// Helper to print the affinity.
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
