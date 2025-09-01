#include <atomic>
#include <chrono>
#include <gtest/gtest.h>
#include <thread>
#include <vector>

#include "corororo/corororo.h"

#include "test_utils.h"

using namespace std::chrono_literals;
using namespace CoroRoro;

class SchedulerIntervalTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        // Create scheduler with 4 worker threads for testing
        scheduler = std::make_unique<Scheduler>(4);
    }

    void TearDown() override
    {
        scheduler.reset();
    }

    std::unique_ptr<Scheduler> scheduler;
};

//
// TEST: Basic Interval Task Execution
// PURPOSE: Verify that interval tasks execute at the correct intervals
// BEHAVIOR: Creates an interval task that increments a counter
// EXPECTATION: Task executes at the specified interval
//
TEST_F(SchedulerIntervalTest, BasicIntervalTaskExecution)
{
    std::atomic<int> executionCount{ 0 };
    const int        expectedExecutions = 5;
    const auto       interval           = 50ms;

    auto token = scheduler->scheduleInterval(
        interval,
        [&executionCount]() -> Task<void>
        {
            executionCount.fetch_add(1);
            co_return;
        });

    // Wait for expected number of executions
    auto start   = std::chrono::steady_clock::now();
    auto timeout = start + std::chrono::seconds(1);

    while (executionCount.load() < expectedExecutions && std::chrono::steady_clock::now() < timeout)
    {
        scheduler->runExpiredTasks();
        std::this_thread::sleep_for(10ms);
    }

    token.cancel();

    // Allow cleanup
    std::this_thread::sleep_for(50ms);
    scheduler->runExpiredTasks();

    EXPECT_GE(executionCount.load(), expectedExecutions);

    // Verify timing is reasonable (should be close to interval * executions)
    auto end              = std::chrono::steady_clock::now();
    auto duration         = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    auto expectedDuration = interval * expectedExecutions;

    EXPECT_GE(duration, expectedDuration * 0.8) << "Should take at least 80% of expected time";
    EXPECT_LE(duration, expectedDuration * 2.0) << "Should not take more than 2x expected time";
}

//
// TEST: Interval Task Cancellation
// PURPOSE: Verify that interval tasks can be cancelled
// BEHAVIOR: Creates an interval task and cancels it after a few executions
// EXPECTATION: Task stops executing after cancellation
//
TEST_F(SchedulerIntervalTest, IntervalTaskCancellation)
{
    std::atomic<int> executionCount{ 0 };
    const int        executionsBeforeCancel = 3;
    const auto       interval               = 30ms;

    auto token = scheduler->scheduleInterval(
        interval,
        [&executionCount]() -> Task<void>
        {
            executionCount.fetch_add(1);
            co_return;
        });

    // Wait for executions before cancellation
    auto start   = std::chrono::steady_clock::now();
    auto timeout = start + std::chrono::seconds(1);

    while (executionCount.load() < executionsBeforeCancel && std::chrono::steady_clock::now() < timeout)
    {
        scheduler->runExpiredTasks();
        std::this_thread::sleep_for(10ms);
    }

    // Cancel the task
    token.cancel();

    // Wait a bit more to ensure no more executions
    std::this_thread::sleep_for(100ms);
    scheduler->runExpiredTasks();

    auto finalCount = executionCount.load();

    EXPECT_GE(finalCount, executionsBeforeCancel) << "Should have executed at least the expected number of times";
    EXPECT_LE(finalCount, executionsBeforeCancel + 1) << "Should not execute many more times after cancellation";
}

//
// TEST: Multiple Interval Tasks
// PURPOSE: Verify that multiple interval tasks can run simultaneously
// BEHAVIOR: Creates multiple interval tasks with different intervals
// EXPECTATION: All tasks execute at their respective intervals
//
TEST_F(SchedulerIntervalTest, MultipleIntervalTasks)
{
    std::atomic<int> fastTaskCount{ 0 };
    std::atomic<int> slowTaskCount{ 0 };

    const auto fastInterval = 20ms;
    const auto slowInterval = 60ms;

    auto fastToken = scheduler->scheduleInterval(
        fastInterval,
        [&fastTaskCount]() -> Task<void>
        {
            fastTaskCount.fetch_add(1);
            co_return;
        });

    auto slowToken = scheduler->scheduleInterval(
        slowInterval,
        [&slowTaskCount]() -> Task<void>
        {
            slowTaskCount.fetch_add(1);
            co_return;
        });

    // Run for a fixed duration
    auto start       = std::chrono::steady_clock::now();
    auto runDuration = 200ms;
    auto endTime     = start + runDuration;

    while (std::chrono::steady_clock::now() < endTime)
    {
        scheduler->runExpiredTasks();
        std::this_thread::sleep_for(5ms);
    }

    fastToken.cancel();
    slowToken.cancel();

    // Allow cleanup
    std::this_thread::sleep_for(50ms);
    scheduler->runExpiredTasks();

    // Fast task should execute more times than slow task
    EXPECT_GT(fastTaskCount.load(), 0) << "Fast task should have executed";
    EXPECT_GT(slowTaskCount.load(), 0) << "Slow task should have executed";
    EXPECT_GT(fastTaskCount.load(), slowTaskCount.load()) << "Fast task should execute more frequently";

    // Verify reasonable execution counts based on intervals
    auto expectedFastExecutions = runDuration / fastInterval;
    auto expectedSlowExecutions = runDuration / slowInterval;

    EXPECT_GE(fastTaskCount.load(), expectedFastExecutions * 0.5) << "Fast task should execute at least half expected times";
    EXPECT_GE(slowTaskCount.load(), expectedSlowExecutions * 0.5) << "Slow task should execute at least half expected times";
}

//
// TEST: Interval Task with Delayed Start
// PURPOSE: Verify that interval tasks can have a delayed first execution
// BEHAVIOR: Creates an interval task with a delay before first execution
// EXPECTATION: Task starts after the delay and then executes at regular intervals
//
TEST_F(SchedulerIntervalTest, IntervalTaskWithDelayedStart)
{
    std::atomic<int> executionCount{ 0 };
    const auto       initialDelay       = 100ms;
    const auto       interval           = 50ms;
    const int        expectedExecutions = 3;

    auto startTime = std::chrono::steady_clock::now();

    auto token = scheduler->scheduleInterval(
        interval,
        [&executionCount]() -> Task<void>
        {
            executionCount.fetch_add(1);
            co_return;
        },
        initialDelay);

    // Wait for expected executions
    auto timeout = startTime + std::chrono::seconds(1);

    while (executionCount.load() < expectedExecutions && std::chrono::steady_clock::now() < timeout)
    {
        scheduler->runExpiredTasks();
        std::this_thread::sleep_for(10ms);
    }

    token.cancel();

    // Allow cleanup
    std::this_thread::sleep_for(50ms);
    scheduler->runExpiredTasks();

    auto endTime       = std::chrono::steady_clock::now();
    auto totalDuration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);

    EXPECT_GE(executionCount.load(), expectedExecutions) << "Should have executed expected number of times";

    // Total time should be at least initial delay + interval * (executions - 1)
    auto expectedMinDuration = initialDelay + interval * (expectedExecutions - 1);
    EXPECT_GE(totalDuration, expectedMinDuration * 0.8) << "Should respect initial delay and intervals";
}

//
// TEST: Interval Task with Complex Work
// PURPOSE: Verify that interval tasks can perform complex work
// BEHAVIOR: Creates an interval task that performs multiple operations
// EXPECTATION: Complex work executes correctly at intervals
//
TEST_F(SchedulerIntervalTest, IntervalTaskWithComplexWork)
{
    std::atomic<int> executionCount{ 0 };
    std::atomic<int> totalWorkDone{ 0 };
    const auto       interval           = 40ms;
    const int        expectedExecutions = 4;

    auto token = scheduler->scheduleInterval(
        interval,
        [&executionCount, &totalWorkDone]() -> Task<void>
        {
            executionCount.fetch_add(1);

            // Perform some complex work
            auto result1 = co_await [&]() -> Task<int>
            {
                co_return 42;
            }();

            auto result2 = co_await [&]() -> AsyncTask<int>
            {
                std::this_thread::sleep_for(1ms);
                co_return 100;
            }();

            totalWorkDone.fetch_add(result1 + result2);
            co_return;
        });

    // Wait for expected executions
    auto start   = std::chrono::steady_clock::now();
    auto timeout = start + std::chrono::seconds(1);

    while (executionCount.load() < expectedExecutions && std::chrono::steady_clock::now() < timeout)
    {
        scheduler->runExpiredTasks();
        std::this_thread::sleep_for(10ms);
    }

    token.cancel();

    // Allow cleanup
    std::this_thread::sleep_for(100ms);
    scheduler->runExpiredTasks();

    EXPECT_GE(executionCount.load(), expectedExecutions) << "Should have executed expected number of times";
    EXPECT_EQ(totalWorkDone.load(), executionCount.load() * 142) << "Should have done correct amount of work (42 + 100 per execution)";
}

//
// TEST: Interval Task Factory Pattern
// PURPOSE: Verify that interval tasks can use factory functions
// BEHAVIOR: Creates an interval task using a factory function
// EXPECTATION: Factory-created tasks execute correctly
//
TEST_F(SchedulerIntervalTest, IntervalTaskFactoryPattern)
{
    std::atomic<int> executionCount{ 0 };
    const auto       interval           = 30ms;
    const int        expectedExecutions = 5;

    // Factory function that creates tasks
    auto taskFactory = [&executionCount]() -> Task<void>
    {
        executionCount.fetch_add(1);

        // Factory can create different types of work
        auto workResult = co_await [&]() -> Task<int>
        {
            co_return executionCount.load() * 10; // Different work each time
        }();

        (void)workResult; // Suppress unused variable warning
        co_return;
    };

    auto token = scheduler->scheduleInterval(interval, std::move(taskFactory));

    // Wait for expected executions
    auto start   = std::chrono::steady_clock::now();
    auto timeout = start + std::chrono::seconds(1);

    while (executionCount.load() < expectedExecutions && std::chrono::steady_clock::now() < timeout)
    {
        scheduler->runExpiredTasks();
        std::this_thread::sleep_for(10ms);
    }

    token.cancel();

    // Allow cleanup
    std::this_thread::sleep_for(50ms);
    scheduler->runExpiredTasks();

    EXPECT_GE(executionCount.load(), expectedExecutions) << "Factory-created tasks should execute expected number of times";
}

//
// TEST: Interval Task Thread Affinity (Not Yet Implemented)
// PURPOSE: Verify that interval tasks respect thread affinity
// BEHAVIOR: Creates interval tasks with different thread affinities
// EXPECTATION: Tasks execute on the correct threads
//
// NOTE: This test is disabled because scheduleInterval with ForcedThreadAffinity
// is not yet implemented in the scheduler.
//
TEST_F(SchedulerIntervalTest, IntervalTaskThreadAffinity)
{
    // This test is disabled until scheduleInterval with ForcedThreadAffinity is implemented
    GTEST_SKIP() << "scheduleInterval with ForcedThreadAffinity is not implemented yet";

    // TODO: Re-enable this test when the feature is implemented
    // Expected behavior:
    // - Main thread interval tasks should execute on the main thread
    // - Worker thread interval tasks should execute on worker threads
    // - Both should respect their specified thread affinity
}

//
// TEST: Interval Task Performance
// PURPOSE: Measure performance of interval task scheduling and execution
// BEHAVIOR: Creates many interval tasks and measures overhead
// EXPECTATION: Should provide performance metrics for interval task operations
//
TEST_F(SchedulerIntervalTest, IntervalTaskPerformance)
{
    const int        numTasks = 50;
    std::atomic<int> totalExecutions{ 0 };
    const auto       interval                  = 20ms;
    const int        expectedExecutionsPerTask = 10;

    std::vector<CancellationToken> tokens;
    tokens.reserve(numTasks);

    auto start = std::chrono::steady_clock::now();

    // Create many interval tasks
    for (int i = 0; i < numTasks; ++i)
    {
        auto token = scheduler->scheduleInterval(
            interval,
            [&totalExecutions]() -> Task<void>
            {
                totalExecutions.fetch_add(1);
                co_return;
            });

        tokens.push_back(std::move(token));
    }

    // Wait for expected executions
    auto timeout = start + std::chrono::seconds(1);

    while (totalExecutions.load() < numTasks * expectedExecutionsPerTask &&
           std::chrono::steady_clock::now() < timeout)
    {
        scheduler->runExpiredTasks();
        std::this_thread::sleep_for(5ms);
    }

    auto end      = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);

    // Cancel all tasks
    for (auto& token : tokens)
    {
        token.cancel();
    }

    // Allow cleanup
    std::this_thread::sleep_for(50ms);
    scheduler->runExpiredTasks();

    auto totalExecutionsCount = totalExecutions.load();
    auto perTask              = duration.count() / totalExecutionsCount;

    std::cout << "Interval Task Performance Test:" << std::endl;
    std::cout << "==============================" << std::endl;
    std::cout << "Tasks: " << numTasks << std::endl;
    std::cout << "Total executions: " << totalExecutionsCount << std::endl;
    std::cout << "Total time: " << duration.count() << "μs" << std::endl;
    std::cout << "Per execution: " << perTask << "μs" << std::endl;
    std::cout << "Executions per second: " << (totalExecutionsCount * 1000000.0 / duration.count()) << std::endl;
    std::cout << std::endl;

    EXPECT_GE(totalExecutionsCount, numTasks * expectedExecutionsPerTask * 0.8) << "Should execute most expected times";
    EXPECT_LT(perTask, 1000) << "Per execution overhead should be reasonable";
}

//
// TEST: Interval Task Stress Test
// PURPOSE: Test interval tasks under stress conditions
// BEHAVIOR: Creates many interval tasks with different intervals
// EXPECTATION: Should handle stress conditions gracefully
//
TEST_F(SchedulerIntervalTest, IntervalTaskStressTest)
{
    const int                      numTasks = 100;
    std::atomic<int>               totalExecutions{ 0 };
    std::vector<CancellationToken> tokens;
    tokens.reserve(numTasks);

    // Create tasks with varying intervals
    for (int i = 0; i < numTasks; ++i)
    {
        auto interval = std::chrono::milliseconds(10 + (i % 20)); // 10-30ms intervals

        auto token = scheduler->scheduleInterval(
            interval,
            [&totalExecutions]() -> Task<void>
            {
                totalExecutions.fetch_add(1);
                co_return;
            });

        tokens.push_back(std::move(token));
    }

    // Run for a fixed duration
    auto start       = std::chrono::steady_clock::now();
    auto runDuration = 500ms;
    auto endTime     = start + runDuration;

    while (std::chrono::steady_clock::now() < endTime)
    {
        scheduler->runExpiredTasks();
        std::this_thread::sleep_for(5ms);
    }

    // Cancel all tasks
    for (auto& token : tokens)
    {
        token.cancel();
    }

    // Allow cleanup
    std::this_thread::sleep_for(100ms);
    scheduler->runExpiredTasks();

    auto totalExecutionsCount = totalExecutions.load();

    std::cout << "Interval Task Stress Test:" << std::endl;
    std::cout << "========================" << std::endl;
    std::cout << "Tasks: " << numTasks << std::endl;
    std::cout << "Total executions: " << totalExecutionsCount << std::endl;
    std::cout << "Run duration: " << runDuration.count() << "ms" << std::endl;
    std::cout << "Executions per second: " << (totalExecutionsCount * 1000.0 / runDuration.count()) << std::endl;
    std::cout << std::endl;

    EXPECT_GT(totalExecutionsCount, 0) << "Should have some executions under stress";
    EXPECT_LT(totalExecutionsCount, 10000) << "Should not have excessive executions";
}

//
// TEST: Interval Task Cancellation Performance
// PURPOSE: Measure performance of interval task cancellation
// BEHAVIOR: Creates many interval tasks and measures cancellation time
// EXPECTATION: Should provide performance metrics for cancellation operations
//
TEST_F(SchedulerIntervalTest, IntervalTaskCancellationPerformance)
{
    const int                      numTasks = 200;
    std::vector<CancellationToken> tokens;
    tokens.reserve(numTasks);

    // Create many interval tasks
    for (int i = 0; i < numTasks; ++i)
    {
        auto token = scheduler->scheduleInterval(
            100ms, // Long interval to prevent execution during test
            []() -> Task<void>
            {
                co_return;
            });

        tokens.push_back(std::move(token));
    }

    // Measure cancellation performance
    auto cancelStart = std::chrono::steady_clock::now();

    for (auto& token : tokens)
    {
        token.cancel();
    }

    auto cancelEnd      = std::chrono::steady_clock::now();
    auto cancelDuration = std::chrono::duration_cast<std::chrono::microseconds>(cancelEnd - cancelStart);

    // Allow cleanup
    std::this_thread::sleep_for(50ms);
    scheduler->runExpiredTasks();

    auto perCancellation = cancelDuration.count() / numTasks;

    std::cout << "Interval Task Cancellation Performance:" << std::endl;
    std::cout << "======================================" << std::endl;
    std::cout << "Tasks: " << numTasks << std::endl;
    std::cout << "Total cancellation time: " << cancelDuration.count() << "μs" << std::endl;
    std::cout << "Per cancellation: " << perCancellation << "μs" << std::endl;
    std::cout << "Cancellations per second: " << (numTasks * 1000000.0 / cancelDuration.count()) << std::endl;
    std::cout << std::endl;

    EXPECT_LT(perCancellation, 100) << "Per cancellation overhead should be reasonable";
    // Note: Cancellation is so fast it may not be measurable, which is good for performance
    EXPECT_GE(perCancellation, 0) << "Cancellation overhead should be non-negative";
}
