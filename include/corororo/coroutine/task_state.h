#pragma once

#include <cstdint>
#include <string_view>

namespace CoroRoro
{

enum class TaskState : uint8_t
{
    Suspended, // The task is currently suspended.
    Done,      // The task completed successfully.
    Failed,    // The task completed with an exception.
};

// Helper to print the state.
constexpr auto toString(TaskState state) -> std::string_view
{
    switch (state)
    {
        case TaskState::Suspended:
            return "Suspended";
        case TaskState::Done:
            return "Done";
        case TaskState::Failed:
            return "Failed";
    }
    return "Unknown";
}

} // namespace CoroRoro
