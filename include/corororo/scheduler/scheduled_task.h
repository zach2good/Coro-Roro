#pragma once

#include <chrono>
#include <memory>

namespace CoroRoro
{

//
// Forward declarations
//
struct ISchedulableTask;

//
// ScheduledTask
//
//   A task with its scheduled execution time for priority queue ordering.
//   Used internally by the scheduler for time-based task management.
//
struct ScheduledTask
{
    std::chrono::time_point<std::chrono::steady_clock> nextExecution;
    std::unique_ptr<ISchedulableTask>                  task;

    ScheduledTask(std::chrono::time_point<std::chrono::steady_clock> when, std::unique_ptr<ISchedulableTask> taskPtr)
    : nextExecution(when)
    , task(std::move(taskPtr))
    {
    }

    // For priority queue ordering (earlier execution times have higher priority)
    auto operator>(const ScheduledTask& other) const -> bool;

    // Required for std::priority_queue which uses operator< by default
    auto operator<(const ScheduledTask& other) const -> bool;
};

//
// Inline members
//

inline auto ScheduledTask::operator>(const ScheduledTask& other) const -> bool
{
    return nextExecution > other.nextExecution;
}

inline auto ScheduledTask::operator<(const ScheduledTask& other) const -> bool
{
    return nextExecution > other.nextExecution; // Reversed for priority queue (earlier = higher priority)
}

} // namespace CoroRoro
