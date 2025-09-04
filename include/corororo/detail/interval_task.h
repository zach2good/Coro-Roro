#pragma once

#include "enums.h"
#include "forward_declarations.h"
#include "macros.h"
#include "task_base.h"

#include <atomic>
#include <chrono>
#include <functional>
#include <optional>

// Forward declarations to avoid circular dependencies
namespace CoroRoro
{
class Scheduler;
class CancellationToken;
} // namespace CoroRoro

namespace CoroRoro
{

//
// IntervalTask
//
// Represents a recurring task that executes at regular intervals.
// Uses factory pattern to create fresh task instances on each execution.
//

class IntervalTask final
{
public:
    //
    // Constructor & Destructor
    //

    IntervalTask(Scheduler*                                                                        scheduler,
                 std::function<CoroRoro::detail::TaskBase<CoroRoro::ThreadAffinity::Main, void>()> factory,
                 std::chrono::milliseconds                                                         interval,
                 bool                                                                              isOneTime = false);

    ~IntervalTask();

    IntervalTask(const IntervalTask&)            = delete;
    IntervalTask& operator=(const IntervalTask&) = delete;
    IntervalTask(IntervalTask&&)                 = delete;
    IntervalTask& operator=(IntervalTask&&)      = delete;

    //
    // Execution Control
    //

    // Try to create a tracked task from the factory
    // Returns nullopt if a child task is already in flight (single-execution guarantee)
    auto createTrackedTask() -> std::optional<CoroRoro::detail::TaskBase<CoroRoro::ThreadAffinity::Main, void>>;

    // Execute the task (called by scheduler when timer expires)
    void execute();

    // Mark as cancelled
    void markCancelled();

    //
    // Status and Information
    //

    auto isCancelled() const -> bool;
    auto getNextExecution() const -> std::chrono::steady_clock::time_point;
    void updateNextExecution();
    void setNextExecution(std::chrono::steady_clock::time_point time);
    auto isOneTime() const -> bool;

    //
    // Cancellation Token Management
    //

    void setToken(class CancellationToken* token);
    void clearTokenPointer();

private:
    //
    // Internal Methods
    //

    CoroRoro::detail::TaskBase<CoroRoro::ThreadAffinity::Main, void> createTrackedWrapper(CoroRoro::detail::TaskBase<CoroRoro::ThreadAffinity::Main, void> originalTask);

    //
    // Member Variables
    //

    std::function<CoroRoro::detail::TaskBase<CoroRoro::ThreadAffinity::Main, void>()> factory_;
    std::chrono::steady_clock::time_point                                             nextExecution_;
    std::chrono::milliseconds                                                         interval_;
    [[maybe_unused]] Scheduler*                                                       scheduler_;
    class CancellationToken*                                                          token_{ nullptr };
    std::atomic<bool>                                                                 cancelled_{ false };
    std::atomic<bool>                                                                 hasActiveChild_{ false };
    [[maybe_unused]] bool                                                             isOneTime_{ false };
};

//
// Comparison operators for priority queue (moved after IntervalTask definition)
//

inline bool operator<(const IntervalTask& lhs, const IntervalTask& rhs)
{
    return lhs.getNextExecution() > rhs.getNextExecution(); // Min-heap
}

} // namespace CoroRoro
