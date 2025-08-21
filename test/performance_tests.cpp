#include <corororo/corororo.h>
using namespace CoroRoro;

#include <iostream>

#include <spdlog/spdlog.h>
#include <tracy/Tracy.hpp>

//
// Utils
//

auto getThreadIdString() -> std::string
{
    std::stringstream ss;
    ss << std::this_thread::get_id();
    return fmt::format("Thread ID: {}", ss.str());
}

//
// Example Application Code
//

auto calculateMagicDamage() -> Task<int>
{
    ZoneScoped;
    spdlog::info("Executing: calculate_magic_damage ({})", getThreadIdString());

    co_return 42;
}

auto calculateTotalDamage() -> Task<int>
{
    ZoneScoped;
    spdlog::info("Executing: calculate_total_damage ({})", getThreadIdString());

    const auto a = co_await calculateMagicDamage();
    const auto b = co_await calculateMagicDamage();
    const auto c = co_await calculateMagicDamage();

    co_return a + b + c;
}

auto blockingNavmesh() -> AsyncTask<int>
{
    ZoneScoped;
    spdlog::info("Executing: blocking_navmesh");

    std::this_thread::sleep_for(std::chrono::milliseconds(1));

    // Should suspend here
    co_return co_await calculateTotalDamage();
}

auto blockingSql() -> AsyncTask<int>
{
    ZoneScoped;
    spdlog::info("Executing: blocking_sql ({})", getThreadIdString());

    std::this_thread::sleep_for(std::chrono::milliseconds(1));

    co_return 100;
}

auto sendPackets() -> Task<void>
{
    ZoneScoped;
    spdlog::info("Executing: send_packets ({})", getThreadIdString());

    co_return;
}

auto parentTask() -> Task<int>
{
    // NOTE: If you use ZoneScoped in the "parent" task that's defining child tasks,
    // it will create a new zone for each child task, which may not be what you want.

    spdlog::info("Executing: parent_task");

    co_await sendPackets();
    const auto a = co_await calculateTotalDamage();
    co_await sendPackets();

    // Should suspend here
    const auto b = co_await blockingNavmesh();
    // Should suspend here

    co_await sendPackets();

    // Should suspend here
    const auto c = co_await blockingSql();
    // Should suspend here

    co_await sendPackets();

    co_return a + b + c;
}

//
// Benchmarking
//

auto benchmarkInlineScheduler(size_t numTicks, size_t numTasks) -> double
{
    ZoneScoped;
    spdlog::info("Benchmarking CoroRoro Direct Execution");

    auto start = std::chrono::steady_clock::now();

    auto prevLevel = spdlog::get_level();
    spdlog::set_level(spdlog::level::off);

    for (int j = 0; j < numTicks; ++j) // tick
    {
        FrameMark;
        ZoneScopedN("tick");

        for (int i = 0; i < numTasks; ++i)
        {
            auto task = parentTask();
            while (!task.done())
            {
                task.resume();
            }
        }
    }

    spdlog::set_level(prevLevel);

    auto end            = std::chrono::steady_clock::now();
    auto ms             = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    auto tasksPerSecond = numTicks * numTasks * 1000.0 / std::max<int64_t>(1, ms);

    return tasksPerSecond;
}

auto benchmarkCoroutineScheduler(size_t numTicks, size_t numTasks) -> double
{
    ZoneScoped;

    spdlog::info("Benchmarking CoroRoro with AsyncTask");

    auto start = std::chrono::steady_clock::now();

    auto prevLevel = spdlog::get_level();
    spdlog::set_level(spdlog::level::off);

    for (int j = 0; j < numTicks; ++j) // tick
    {
        FrameMark;
        ZoneScopedN("tick");

        for (int i = 0; i < numTasks; ++i) // zones
        {
            // Use AsyncTask version for cross-thread performance testing
            auto asyncParentTask = []() -> AsyncTask<int>
            {
                ZoneScoped;
                spdlog::info("Executing: async_parent_task ({})", getThreadIdString());
                const auto totalDamage = co_await calculateTotalDamage();
                const auto navmeshData = co_await blockingNavmesh();
                co_return totalDamage + navmeshData;
            };
            
            auto task = asyncParentTask();
            while (!task.done())
            {
                task.resume();
            }
        }
    }
    spdlog::set_level(prevLevel);

    auto end            = std::chrono::steady_clock::now();
    auto ms             = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    auto tasksPerSecond = numTicks * numTasks * 1000.0 / std::max<int64_t>(1, ms);

    return tasksPerSecond;
}

//
// Runners
//

void runBenchmarks()
{
    ZoneScoped;
    spdlog::info("=== Running benchmarks (Main Thread: {}) ===", getThreadIdString());

    const size_t numTicks = 3;
    const size_t numTasks = 100;

    const auto inlineTasksPerSecond    = benchmarkInlineScheduler(numTicks, numTasks);
    const auto schedulerTasksPerSecond = benchmarkCoroutineScheduler(numTicks, numTasks);

    spdlog::info(fmt::format("Inline Single-Thread Scheduler: {:.2f} tasks/s", inlineTasksPerSecond));
    spdlog::info(fmt::format("Coroutine Scheduler: {:.2f} tasks/s", schedulerTasksPerSecond));

    const auto speedupFactor = schedulerTasksPerSecond / inlineTasksPerSecond;
    spdlog::info(fmt::format("Speedup Factor: {:.2f}x", speedupFactor));
}

auto main() -> int
{
    spdlog::info("=== Running performance/Tracy tests ===");

    runBenchmarks();

    spdlog::info("=== All tests completed ===");

    return 0;
}
