#pragma once

#include <chrono>
#include <memory>

#include "schedulable_task.h"
#include "task_id.h"

namespace CoroRoro
{

//
// Forward declarations
//
class Scheduler;

//
// IntervalTask
//
//   Implements the factory pattern for interval tasks. The factory executes,
//   creates a child task for immediate execution, and reschedules itself
//   for the next interval AFTER the child task completes. This prevents
//   overlapping executions while maintaining interval timing precision.
//
template <typename FactoryType>
class IntervalTask : public ISchedulableTask
{
public:
    using milliseconds = std::chrono::milliseconds;
    using steady_clock = std::chrono::steady_clock;

    IntervalTask(Scheduler& scheduler, TaskId taskId, milliseconds interval, FactoryType&& factory)
    : scheduler_(scheduler)
    , taskId_(taskId)
    , interval_(interval)
    , factory_(std::move(factory))
    , childTask_(nullptr)
    , childTaskStartTime_(steady_clock::now())
    {
    }

    auto resume() -> TaskState override; // Implementation in scheduler.h after Scheduler definition

    auto done() const -> bool override;

    auto threadAffinity() const -> ThreadAffinity override;

private:
    Scheduler&   scheduler_;
    TaskId       taskId_;
    milliseconds interval_;
    FactoryType  factory_;
    
    // Child task management
    std::unique_ptr<ISchedulableTask> childTask_;
    steady_clock::time_point childTaskStartTime_;
};

//
// Inline members
//

template <typename FactoryType>
inline auto IntervalTask<FactoryType>::done() const -> bool
{
    // Task is done when not executing a child task
    return childTask_ == nullptr;
}

template <typename FactoryType>
inline auto IntervalTask<FactoryType>::threadAffinity() const -> ThreadAffinity
{
    return ThreadAffinity::MainThread; // Factory runs on main thread
}

} // namespace CoroRoro
