#pragma once

#include <corororo/coroutine/task_state.h>
#include <corororo/coroutine/thread_affinity.h>

namespace CoroRoro
{

//
// ISchedulableTask
//
//   Type-erased interface for schedulable tasks.
//
struct ISchedulableTask
{
    virtual ~ISchedulableTask()                           = default;
    virtual auto resume() -> TaskState                    = 0;
    virtual auto done() const -> bool                     = 0;
    virtual auto threadAffinity() const -> ThreadAffinity = 0;
};

//
// SchedulableTaskWrapper
//
//   Concrete wrapper that implements ISchedulableTask for any task type.
//
template <typename TaskType>
struct SchedulableTaskWrapper : public ISchedulableTask
{
    TaskType task_;

    explicit SchedulableTaskWrapper(TaskType&& task)
    : task_(std::move(task))
    {
    }

    auto resume() -> TaskState override;

    auto done() const -> bool override;

    auto threadAffinity() const -> ThreadAffinity override;
};

//
// Inline members
//

template <typename TaskType>
inline auto SchedulableTaskWrapper<TaskType>::resume() -> TaskState
{
    return task_.resume();
}

template <typename TaskType>
inline auto SchedulableTaskWrapper<TaskType>::done() const -> bool
{
    return task_.done();
}

template <typename TaskType>
inline auto SchedulableTaskWrapper<TaskType>::threadAffinity() const -> ThreadAffinity
{
    return task_.threadAffinity();
}

} // namespace CoroRoro
