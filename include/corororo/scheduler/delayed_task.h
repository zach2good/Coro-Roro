#pragma once

#include "schedulable_task.h"
#include "task_id.h"

namespace CoroRoro
{

//
// Forward declarations
//
class Scheduler;

//
// DelayedTask
//
//   Wrapper for one-time scheduled task execution with cancellation support.
//   Executes the wrapped task directly while checking for cancellation.
//
template <typename TaskType>
class DelayedTask : public ISchedulableTask
{
public:
    DelayedTask(Scheduler& scheduler, TaskId taskId, TaskType&& task)
    : scheduler_(scheduler)
    , taskId_(taskId)
    , task_(std::move(task))
    {
    }

    auto resume() -> TaskState override; // Implementation in scheduler.h after Scheduler definition

    auto done() const -> bool override;

    auto threadAffinity() const -> ThreadAffinity override;

private:
    Scheduler& scheduler_;
    TaskId     taskId_;
    TaskType   task_;
};

//
// Inline members
//

template <typename TaskType>
inline auto DelayedTask<TaskType>::done() const -> bool
{
    return task_.done();
}

template <typename TaskType>
inline auto DelayedTask<TaskType>::threadAffinity() const -> ThreadAffinity
{
    return task_.threadAffinity();
}

} // namespace CoroRoro
