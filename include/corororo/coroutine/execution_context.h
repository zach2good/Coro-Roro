#pragma once

#include <corororo/coroutine/task_state.h>
#include <corororo/coroutine/thread_affinity.h>

#include <coroutine>

namespace CoroRoro
{

// The shared context for a single coroutine execution chain.
struct ExecutionContext final
{
    std::coroutine_handle<> currentlyExecuting_ = nullptr;
    TaskState               activeState_        = TaskState::Suspended;
    ThreadAffinity          activeAffinity_     = ThreadAffinity::MainThread;
    bool                    isChainDone_        = false;
};

} // namespace CoroRoro
