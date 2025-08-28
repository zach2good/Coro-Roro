#pragma once

#include <corororo/coroutine/task.h>

namespace CoroRoro
{

//
// runCoroutineInline
//
//   A helper for if you just want to run the entirety of a coroutine inline.
//
template <typename Coroutine>
auto runCoroutineInline(Coroutine&& coro)
{
    while (!coro.done())
    {
        coro.resume();
    }

    if constexpr (!std::is_void_v<decltype(coro.result())>)
    {
        return coro.result();
    }
}

} // namespace CoroRoro
