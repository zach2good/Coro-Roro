#pragma once

#include <corororo/coroutine/types.hpp>

namespace CoroRoro
{

// Simple task state wrapper for backward compatibility
class TaskStateWrapper
{
public:
    TaskStateWrapper() = default;
    explicit TaskStateWrapper(TaskState state) : state_(state) {}
    
    operator TaskState() const { return state_; }
    
    TaskState get() const { return state_; }
    void set(TaskState state) { state_ = state; }

private:
    TaskState state_ = TaskState::Suspended;
};

} // namespace CoroRoro
