#pragma once

#include "schedulable_task.h"

namespace CoroRoro
{

//
// ForcedAffinityTask
//
//   Wrapper that overrides the natural thread affinity of a task.
//   Used for atomic execution where the entire task graph must run
//   on a specific thread without suspension or thread switching.
//
template <typename TaskType>
class ForcedAffinityTask : public ISchedulableTask
{
public:
    ForcedAffinityTask(ThreadAffinity affinity, TaskType&& task)
    : forcedAffinity_(affinity)
    , task_(std::move(task))
    {
    }

    auto resume() -> TaskState override;

    auto done() const -> bool override;

    auto threadAffinity() const -> ThreadAffinity override;

private:
    ThreadAffinity forcedAffinity_;
    TaskType       task_;
};

//
// Inline members
//

template <typename TaskType>
inline auto ForcedAffinityTask<TaskType>::resume() -> TaskState
{
    return task_.resume();
}

template <typename TaskType>
inline auto ForcedAffinityTask<TaskType>::done() const -> bool
{
    return task_.done();
}

template <typename TaskType>
inline auto ForcedAffinityTask<TaskType>::threadAffinity() const -> ThreadAffinity
{
    // Override the task's natural affinity with the forced affinity
    return forcedAffinity_;
}

} // namespace CoroRoro
