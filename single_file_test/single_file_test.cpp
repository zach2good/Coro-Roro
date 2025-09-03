// MSVC: /std:c++20 /W4 /Wall /permissive-
// GCC/Clang: -std=c++20 -Wall -Wextra

#include <corororo/corororo.h>
using namespace CoroRoro;

std::atomic<size_t> mainThreadTaskCounter{ 0 };
std::atomic<size_t> workerTaskCounter{ 0 };
std::atomic<size_t> totalTasksFinishedCounter{ 0 };

auto innermostTask() -> AsyncTask<int>
{
    // Useful to confirm the worker tasks are running on worker threads
    // coro_log("Running innermostTask on worker thread");

    ++workerTaskCounter;
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    co_return 100;
}

auto innerTask() -> Task<int>
{
    // Useful to confirm the main tasks are running on main thread
    // coro_log("Running innerTask on main thread");

    ++mainThreadTaskCounter;
    co_await innermostTask();
    co_return co_await innermostTask();
}

auto middleTask() -> Task<void>
{
    ++mainThreadTaskCounter;
    co_await innerTask();
    co_return;
}

auto outerTask() -> Task<void>
{
    ++mainThreadTaskCounter;
    co_await middleTask();
    co_await innerTask();
    co_return;
}

auto outermostTask(size_t numSubTasks) -> Task<int>
{
    // coro_log("Starting outermostTask on main thread");

    co_await outerTask();

    int value = 0;
    for (size_t i = 0; i < numSubTasks; ++i)
    {
        value = co_await innerTask();
    }

    ++totalTasksFinishedCounter;

    // coro_log("Finishing outermostTask on main thread");

    co_return value;
}

auto main() -> int
{
    {
#if !defined(RUNNING_ON_GODBOLT)
        const auto numThreads  = 16;
        const auto numTasks    = 10;
        const auto numSubTasks = 300;
#else
        const auto numThreads  = 2;
        const auto numTasks    = 10;
        const auto numSubTasks = 20;
#endif

        Scheduler scheduler(numThreads);

        coro_log("Scheduling outer tasks...");
        for (int i = 0; i < numTasks; ++i)
        {
            scheduler.schedule(outermostTask(numSubTasks));
        }

        coro_log("Running tasks...");
        try
        {
            auto duration = scheduler.runExpiredTasks();
            coro_log("-> Scheduler ran for " + std::to_string(duration.count()) + "ms");
        }
        catch (const std::exception& e)
        {
            coro_log("Error occurred while running tasks: " + std::string(e.what()));
            return 1;
        }

        coro_log("Number of threads: " + std::to_string(numThreads));
        coro_log("Total tasks scheduled: " + std::to_string(numTasks));
        coro_log("Total main thread coroutines executed: " + std::to_string(mainThreadTaskCounter.load()));
        coro_log("Total worker coroutines executed: " + std::to_string(workerTaskCounter.load()));
        coro_log("Total tasks finished: " + std::to_string(totalTasksFinishedCounter.load()));
    }

    coro_log("Test completed successfully!");
    return 0;
}
