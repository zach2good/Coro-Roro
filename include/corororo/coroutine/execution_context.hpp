#pragma once

#include <corororo/coroutine/task.hpp>
#include <corororo/coroutine/types.hpp>
#include <coroutine>

namespace CoroRoro
{

// Simple execution context for backward compatibility
class ExecutionContext
{
public:
    ExecutionContext() = default;
    ~ExecutionContext() = default;

    // Currently executing coroutine
    std::coroutine_handle<> currentlyExecuting_ = nullptr;
    
    // Active state
    TaskState activeState_ = TaskState::Suspended;
    
    // Active affinity
    ThreadAffinity activeAffinity_ = ThreadAffinity::Any;
    
    // Chain completion flag
    bool isChainDone_ = false;
};

} // namespace CoroRoro
