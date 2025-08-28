#pragma once

#include <corororo/coroutine/thread_affinity.h>

namespace CoroRoro
{

//
// CoroutineTask
//
//   Forward declaration for the main coroutine task template.
//
template <ThreadAffinity Affinity, typename T>
struct CoroutineTask;

//
// Task
//
//   Type alias for coroutines that execute on the main thread.
//
template <typename T>
using Task = CoroutineTask<ThreadAffinity::MainThread, T>;

//
// AsyncTask
//
//   Type alias for coroutines that execute on worker threads.
//
template <typename T>
using AsyncTask = CoroutineTask<ThreadAffinity::WorkerThread, T>;

} // namespace CoroRoro
