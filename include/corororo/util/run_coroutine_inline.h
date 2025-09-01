#pragma once

#include <corororo/coroutine/task.h>

namespace CoroRoro
{

// Simple utility to run a coroutine inline
template <typename T>
T runCoroutineInline(Task<T>&& task)
{
    while (!task.await_ready())
    {
        // Resume the coroutine until it's done
        task.resume();
    }
    return task.await_resume();
}

// Specialization for void tasks
template <>
void runCoroutineInline(Task<void>&& task)
{
    while (!task.await_ready())
    {
        // Resume the coroutine until it's done
        task.resume();
    }
    task.await_resume();
}

} // namespace CoroRoro
