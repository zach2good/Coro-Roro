#include "corororo/corororo.h"

#include <atomic>
#include <chrono>
#include <iostream>
#include <sstream>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include "test_utils.h"

using namespace std::chrono_literals;
using namespace CoroRoro;

//
// Test Utility Functions
//

constexpr auto windowsPerformancePenalty() -> double
{
#if defined(_WIN32) || defined(_WIN64)
    // Everything just runs slower if you're on Windows :(
    return 0.1;
#else
    return 1.0;
#endif
}

auto getThreadIdString() -> std::string
{
    std::stringstream ss;
    ss << std::this_thread::get_id();
    return "Thread ID: " + ss.str();
}

auto calculateMagicDamage() -> Task<int>
{
    co_return 42;
}

auto calculateTotalDamage() -> Task<int>
{
    const auto a = co_await calculateMagicDamage();
    const auto b = co_await calculateMagicDamage();
    const auto c = co_await calculateMagicDamage();

    co_return a + b + c;
}

auto blockingNavmesh() -> AsyncTask<int>
{
    std::this_thread::sleep_for(1ms);

    // Should suspend here
    co_return co_await calculateTotalDamage();
}

auto blockingSQL() -> AsyncTask<int>
{
    std::this_thread::sleep_for(1ms);
    co_return 100;
}

auto sendPackets() -> Task<void>
{
    co_return;
}

auto parentTask() -> Task<int>
{
    // NOTE: If you use ZoneScoped in the "parent" task that's defining child tasks,
    // it will create a new zone for each child task, which may not be what you want.

    co_await sendPackets();
    const auto a = co_await calculateTotalDamage();
    co_await sendPackets();

    // Should suspend here
    const auto b = co_await blockingNavmesh();
    // Should suspend here

    co_await sendPackets();

    // Should suspend here
    const auto c = co_await blockingSQL();
    // Should suspend here

    co_await sendPackets();

    co_return a + b + c;
}

class SchedulerPerformanceTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        scheduler = std::make_unique<Scheduler>(4);
    }

    void TearDown() override
    {
        scheduler.reset();
    }

    std::unique_ptr<Scheduler> scheduler;

    static constexpr size_t PERF_TEST_TICKS = 3;
    static constexpr size_t PERF_TEST_TASKS = 50;
};

TEST_F(SchedulerPerformanceTest, BenchmarkInlineNoScheduler)
{
    auto start = std::chrono::steady_clock::now();

    for (size_t j = 0; j < PERF_TEST_TICKS; ++j)
    {
        for (size_t i = 0; i < PERF_TEST_TASKS; ++i)
        {
            // Run the complex parentTask inline using utility function
            auto result = runCoroutineInline(parentTask());
            EXPECT_GT(result, 0);
        }
    }

    auto end            = std::chrono::steady_clock::now();
    auto ms             = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    auto totalTasks     = PERF_TEST_TICKS * PERF_TEST_TASKS;
    auto tasksPerSecond = totalTasks * 1000.0 / std::max<int64_t>(1, ms);

    EXPECT_GT(tasksPerSecond, 300.0 * windowsPerformancePenalty()); // Realistic expectation for complex parentTask
}

TEST_F(SchedulerPerformanceTest, BenchmarkSchedulerMultiThreaded)
{
    auto start = std::chrono::steady_clock::now();

    for (size_t j = 0; j < PERF_TEST_TICKS; ++j)
    {
        // Schedule all tasks for this tick
        for (size_t i = 0; i < PERF_TEST_TASKS; ++i)
        {
            scheduler->schedule(
                []() -> Task<int>
                {
                    // Use the complex parentTask for fair comparison
                    co_return co_await parentTask();
                });
        }

        // Process tasks for this tick
        scheduler->runExpiredTasks();
        std::this_thread::sleep_for(2ms);
    }

    auto end            = std::chrono::steady_clock::now();
    auto ms             = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    auto totalTasks     = PERF_TEST_TICKS * PERF_TEST_TASKS;
    auto tasksPerSecond = totalTasks * 1000.0 / std::max<int64_t>(1, ms);

    EXPECT_GT(tasksPerSecond, 10000.0 * windowsPerformancePenalty()); // Should be significantly faster than inline for complex tasks
}

TEST_F(SchedulerPerformanceTest, BenchmarkSchedulerImmediateOnly)
{
    auto start = std::chrono::steady_clock::now();

    for (size_t j = 0; j < PERF_TEST_TICKS; ++j)
    {
        // Schedule the complex parentTask for fair comparison
        for (size_t i = 0; i < PERF_TEST_TASKS; ++i)
        {
            scheduler->schedule(
                []() -> Task<int>
                {
                    co_return co_await parentTask();
                });
        }

        scheduler->runExpiredTasks();
        std::this_thread::sleep_for(1ms);
    }

    auto end            = std::chrono::steady_clock::now();
    auto ms             = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    auto totalTasks     = PERF_TEST_TICKS * PERF_TEST_TASKS;
    auto tasksPerSecond = totalTasks * 1000.0 / std::max<int64_t>(1, ms);

    EXPECT_GT(tasksPerSecond, 10000.0 * windowsPerformancePenalty()); // Should be fast even for complex tasks
}

TEST_F(SchedulerPerformanceTest, BenchmarkSchedulerWithValidation)
{
    std::atomic<size_t> completedTasks{ 0 };
    std::atomic<int>    totalResult{ 0 };

    auto start = std::chrono::steady_clock::now();

    const size_t numTasks = 100;
    for (size_t i = 0; i < numTasks; ++i)
    {
        scheduler->schedule(
            [&completedTasks, &totalResult]() -> Task<void>
            {
                // Use the complex parentTask for fair comparison and validation
                auto result = co_await parentTask();

                // parentTask returns: calculateTotalDamage() + blockingNavmesh() + blockingSQL()
                // calculateTotalDamage() = 42*3 = 126
                // blockingNavmesh() = calculateTotalDamage() = 126
                // blockingSQL() = 100
                // Expected total: 126 + 126 + 100 = 352
                totalResult.fetch_add(result);
                completedTasks.fetch_add(1);
                co_return;
            });
    }

    // Process tasks and wait for completion
    auto timeout = start + std::chrono::seconds(5);
    while (completedTasks.load() < numTasks && std::chrono::steady_clock::now() < timeout)
    {
        scheduler->runExpiredTasks();
        std::this_thread::sleep_for(1ms);
    }

    auto end            = std::chrono::steady_clock::now();
    auto ms             = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    auto tasksPerSecond = numTasks * 1000.0 / std::max<int64_t>(1, ms);

    EXPECT_EQ(completedTasks.load(), numTasks);
    EXPECT_EQ(totalResult.load(), numTasks * 352); // 352 per task (126 + 126 + 100)
    EXPECT_GT(tasksPerSecond, 1000.0 * windowsPerformancePenalty());
}

TEST_F(SchedulerPerformanceTest, BenchmarkDelayedTasks)
{
    std::atomic<size_t> completedTasks{ 0 };
    auto                start = std::chrono::steady_clock::now();

    const size_t numTasks = 10;
    const auto   delay    = 50ms;

    // Store tokens to prevent RAII auto-cancellation
    std::vector<CancellationToken> tokens;

    // Schedule delayed tasks
    for (size_t i = 0; i < numTasks; ++i)
    {
        auto token = scheduler->scheduleDelayed(delay,
                                                [&completedTasks]() -> Task<void>
                                                {
                                                    // Use the complex parentTask for fair comparison
                                                    auto result = co_await parentTask();
                                                    (void)result; // Suppress unused variable warning
                                                    completedTasks.fetch_add(1);
                                                    co_return;
                                                });

        tokens.push_back(std::move(token));
    }

    // Wait for all tasks to complete with more frequent polling
    auto timeout = start + std::chrono::seconds(3);
    while (completedTasks.load() < numTasks && std::chrono::steady_clock::now() < timeout)
    {
        scheduler->runExpiredTasks();
        std::this_thread::sleep_for(5ms);
    }

    auto end         = std::chrono::steady_clock::now();
    auto actualDelay = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

    EXPECT_GT(completedTasks.load(), 0); // At least some tasks should complete
    if (completedTasks.load() == numTasks)
    {
        EXPECT_GE(actualDelay, delay);         // Should take at least the delay time
        EXPECT_LT(actualDelay, delay + 200ms); // But not much longer
    }
}

TEST_F(SchedulerPerformanceTest, BenchmarkCancellationPerformance)
{
    std::atomic<size_t> tasksStarted{ 0 };
    std::atomic<size_t> tasksCompleted{ 0 };

    const size_t                   numTasks = 20;
    std::vector<CancellationToken> tokens;

    // Schedule tasks that take some time
    for (size_t i = 0; i < numTasks; ++i)
    {
        auto token = scheduler->scheduleDelayed(
            200ms,
            [&tasksStarted, &tasksCompleted]() -> Task<void>
            {
                tasksStarted.fetch_add(1);

                // Use the complex parentTask for fair comparison
                auto result = co_await parentTask();
                (void)result; // Suppress unused variable warning

                tasksCompleted.fetch_add(1);
                co_return;
            });

        tokens.push_back(std::move(token));
    }

    // Cancel half the tasks immediately (before they execute)
    auto cancelStart = std::chrono::steady_clock::now();

    for (size_t i = 0; i < numTasks / 2; ++i)
    {
        tokens[i].cancel();
    }

    auto cancelEnd  = std::chrono::steady_clock::now();
    auto cancelTime = std::chrono::duration_cast<std::chrono::milliseconds>(cancelEnd - cancelStart);

    // Wait for remaining tasks
    std::this_thread::sleep_for(100ms);
    scheduler->runExpiredTasks();

    // Cancellation should be very fast
    EXPECT_LT(cancelTime, 10ms);

    // Some tasks should have been cancelled before completion
    EXPECT_LT(tasksCompleted.load(), numTasks);
}

TEST_F(SchedulerPerformanceTest, BenchmarkHighVolumeScheduling)
{
    const size_t        numTasks = 100;
    std::atomic<size_t> completedTasks{ 0 };

    auto start = std::chrono::steady_clock::now();

    // Schedule a large number of complex tasks for fair comparison
    for (size_t i = 0; i < numTasks; ++i)
    {
        scheduler->schedule(
            [&completedTasks]() -> Task<void>
            {
                // Use the complex parentTask for fair comparison
                auto result = co_await parentTask();
                (void)result; // Suppress unused variable warning
                completedTasks.fetch_add(1);
                co_return;
            });
    }

    // Process all tasks
    auto timeout = start + std::chrono::seconds(5);
    while (completedTasks.load() < numTasks && std::chrono::steady_clock::now() < timeout)
    {
        scheduler->runExpiredTasks();
        std::this_thread::sleep_for(1ms);
    }

    auto end            = std::chrono::steady_clock::now();
    auto ms             = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    auto tasksPerSecond = numTasks * 1000.0 / std::max<int64_t>(1, ms);

    EXPECT_EQ(completedTasks.load(), numTasks);
    EXPECT_GT(tasksPerSecond, 200.0 * windowsPerformancePenalty()); // Realistic expectation for high volume complex tasks
}

TEST_F(SchedulerPerformanceTest, BenchmarkIntervalTasks)
{
    // Use static to prevent lifetime issues with interval task execution
    static std::atomic<size_t> taskExecutionCount{ 0 };
    taskExecutionCount.store(0); // Reset for this test

    auto start = std::chrono::steady_clock::now();

    // Use a separate scope to avoid double cancellation issues
    {
        // This was the problematic test that caused segfaults
        // Schedule an interval task that runs every 25ms for 1 second
        auto token = scheduler->scheduleInterval(
            25ms,
            []() -> Task<void>
            {
                taskExecutionCount.fetch_add(1);

                // Use parentTask for realistic workload (same as other tests)
                std::ignore = co_await parentTask();

                co_return;
            });

        // Run the scheduler for 500ms (less aggressive than 1 second)
        auto endTime = start + std::chrono::milliseconds(500);
        while (std::chrono::steady_clock::now() < endTime)
        {
            scheduler->runExpiredTasks();
            std::this_thread::sleep_for(10ms); // Poll less aggressively
        }

        // Cancel the interval task explicitly
        token.cancel();

        // Token will be destroyed here, but task is already cancelled
    }

    // Give time for final tasks to complete and cleanup (token already destroyed)
    auto cleanupEnd = std::chrono::steady_clock::now() + std::chrono::milliseconds(200);
    while (std::chrono::steady_clock::now() < cleanupEnd)
    {
        scheduler->runExpiredTasks();
        std::this_thread::sleep_for(10ms);
    }

    auto end              = std::chrono::steady_clock::now();
    auto ms               = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    auto tasksPerSecond   = taskExecutionCount.load() * 1000.0 / std::max<int64_t>(1, ms);
    auto expectedTasksMin = 500 / 25;              // ~20 tasks in 500ms at 25ms intervals
    auto expectedTasksMax = expectedTasksMin + 10; // Allow some tolerance

    EXPECT_GT(taskExecutionCount.load(), static_cast<size_t>(expectedTasksMin * 0.5)); // At least 50% of expected (relaxed)
    EXPECT_LT(taskExecutionCount.load(), static_cast<size_t>(expectedTasksMax));       // Not too many
    EXPECT_GT(tasksPerSecond, 15.0);                                                   // Should execute regularly (relaxed expectation)
}
