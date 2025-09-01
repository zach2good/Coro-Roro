#pragma once

#include <corororo/scheduler/cancellation_token.h>
#include <corororo/coroutine/task.h>
#include <chrono>
#include <functional>
#include <memory>

namespace CoroRoro
{

//
// ScheduledTask
//
//   Represents a task that has been scheduled for delayed or interval execution.
//   Contains the task itself, timing information, and cancellation support.
//

class ScheduledTask
{
public:
    using time_point = std::chrono::time_point<std::chrono::steady_clock>;
    using milliseconds = std::chrono::milliseconds;

    // Task types
    enum class Type
    {
        Delayed,   // Execute once after delay
        Interval   // Execute repeatedly at intervals
    };

    ScheduledTask(Type type,
                  time_point nextExecutionTime,
                  std::function<Task<void>()> taskFactory,
                  CancellationToken token)
        : type_(type)
        , nextExecutionTime_(nextExecutionTime)
        , taskFactory_(std::move(taskFactory))
        , token_(token)
        , executions_(0)
    {}

    // Check if the task should execute at the given time
    bool shouldExecute(time_point currentTime) const
    {
        return !token_.isCancelled() && currentTime >= nextExecutionTime_;
    }

    // Execute the task and return the next execution time (for intervals)
    time_point execute()
    {
        if (token_.isCancelled())
            return time_point::max(); // Never execute again

        // Record when we actually start executing
        lastExecutionStart_ = std::chrono::steady_clock::now();

        // Create and schedule the task
        auto task = taskFactory_();
        executions_++;

        // For interval tasks, return the next execution time
        if (type_ == Type::Interval)
        {
            return std::chrono::steady_clock::now() + interval_;
        }

        return time_point::max(); // One-time tasks don't repeat
    }

    // Called when an interval task completes - reschedule for next interval
    time_point rescheduleAfterCompletion()
    {
        if (type_ == Type::Interval && !token_.isCancelled())
        {
            auto completionTime = std::chrono::steady_clock::now();
            return completionTime + interval_;
        }
        return time_point::max();
    }

    // Get the last execution start time
    time_point getLastExecutionStart() const { return lastExecutionStart_; }

    // Get the type of task
    Type getType() const { return type_; }

    // Get next execution time
    time_point getNextExecutionTime() const { return nextExecutionTime_; }

    // Check if cancelled
    bool isCancelled() const { return token_.isCancelled(); }

    // Get execution count
    size_t getExecutionCount() const { return executions_; }

    // Set the interval duration for interval tasks
    void setInterval(milliseconds interval) { interval_ = interval; }

    // Get interval duration
    milliseconds getInterval() const { return interval_; }

    // Get the task factory function
    const std::function<Task<void>()>& getTaskFactory() const { return taskFactory_; }

    // Get the cancellation token
    const CancellationToken& getToken() const { return token_; }
    CancellationToken& getToken() { return token_; }

private:
    Type type_;
    time_point nextExecutionTime_;
    std::function<Task<void>()> taskFactory_;
    CancellationToken token_;
    size_t executions_;
    milliseconds interval_{0};
    time_point lastExecutionStart_;
};

} // namespace CoroRoro
