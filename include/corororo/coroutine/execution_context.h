#pragma once

#include <corororo/coroutine/task.h>
#include <corororo/coroutine/types.h>
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
