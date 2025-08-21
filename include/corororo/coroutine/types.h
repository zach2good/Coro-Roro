#pragma once

#include <corororo/coroutine/thread_affinity.h>

namespace CoroRoro
{

template <ThreadAffinity Affinity, typename T>
struct CoroutineTask;

template <typename T>
using Task = CoroutineTask<ThreadAffinity::MainThread, T>;

template <typename T>
using AsyncTask = CoroutineTask<ThreadAffinity::WorkerThread, T>;

} // namespace CoroRoro
