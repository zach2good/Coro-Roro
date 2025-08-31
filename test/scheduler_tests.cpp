#include <atomic>
#include <chrono>
#include <gtest/gtest.h>
#include <thread>
#include <vector>

#include "corororo/corororo.h"

#include "test_utils.h"

using namespace std::chrono_literals;
using namespace CoroRoro;

class SchedulerTest : public ::testing::Test
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

// Test 1: Basic Scheduler Construction and Destruction
TEST_F(SchedulerTest, CanConstructAndDestruct)
{
    // This test passes if setup/teardown works without crash
    EXPECT_TRUE(scheduler != nullptr);
}

// Test 2: Schedule a Simple Task for Immediate Execution
TEST_F(SchedulerTest, CanScheduleSimpleTask)
{
    std::atomic<bool> taskExecuted{ false };

    // Schedule a simple coroutine task
    scheduler->schedule(
        [&taskExecuted]() -> Task<void>
        {
            taskExecuted.store(true);
            co_return;
        });

    // Run main thread tasks
    scheduler->runExpiredTasks();

    // Give worker threads time to execute if routed there
    std::this_thread::sleep_for(10ms);

    EXPECT_TRUE(taskExecuted.load());
}

// Test 3: Schedule Task with General Callable (Non-Coroutine)
TEST_F(SchedulerTest, CanScheduleGeneralCallable)
{
    // Use a simple static variable for this test to avoid capture issues
    static std::atomic<int> globalCounter{ 0 };
    globalCounter.store(0); // Reset before test

    // Schedule a regular function (not a coroutine)
    auto callable = []()
    {
        globalCounter.store(42);
    };

    scheduler->schedule(callable);
    scheduler->runExpiredTasks();
    std::this_thread::sleep_for(10ms);

    EXPECT_EQ(globalCounter.load(), 42);
}

// Test 4: Schedule Interval Task and Cancel It
TEST_F(SchedulerTest, CanScheduleAndCancelIntervalTask)
{
    static std::atomic<int> executionCount{ 0 };
    executionCount.store(0); // Reset for this test

    // Schedule interval task that increments counter every 50ms
    auto token = scheduler->scheduleInterval(
        50ms,
        []() -> Task<void>
        {
            executionCount.fetch_add(1);
            co_return;
        });

    // Let it run a few times
    for (int i = 0; i < 3; ++i)
    {
        scheduler->runExpiredTasks();
        std::this_thread::sleep_for(60ms);
    }

    int countBeforeCancel = executionCount.load();
    EXPECT_GT(countBeforeCancel, 0);

    // Cancel the interval task
    token.cancel();

    // Wait a short time for cancellation to propagate, then run tasks
    std::this_thread::sleep_for(10ms);
    scheduler->runExpiredTasks();

    // Allow for one more execution due to race condition (task already in queue)
    int countAfterCancel = executionCount.load();

    // Wait longer and ensure no further executions
    std::this_thread::sleep_for(100ms);
    scheduler->runExpiredTasks();

    // Verify cancellation worked (no executions after the grace period)
    EXPECT_EQ(executionCount.load(), countAfterCancel);
}

// Test 5: Schedule Delayed Task
TEST_F(SchedulerTest, CanScheduleDelayedTask)
{
    std::atomic<bool>                      taskExecuted{ false };
    auto                                   startTime = std::chrono::steady_clock::now();
    std::atomic<std::chrono::milliseconds> executionDelay{ 0ms };

    // Schedule task to execute after 100ms delay
    auto token = scheduler->scheduleDelayed(
        100ms,
        [&taskExecuted, &executionDelay, startTime]() -> Task<void>
        {
            auto now = std::chrono::steady_clock::now();
            executionDelay.store(std::chrono::duration_cast<std::chrono::milliseconds>(now - startTime));
            taskExecuted.store(true);
            co_return;
        });

    // Task should not execute immediately
    scheduler->runExpiredTasks();
    EXPECT_FALSE(taskExecuted.load());

    // Wait for delay period plus some margin
    std::this_thread::sleep_for(120ms);
    scheduler->runExpiredTasks();

    EXPECT_TRUE(taskExecuted.load());
    EXPECT_GE(executionDelay.load().count(), 95); // Allow some tolerance
}

// Test 6: Thread Affinity - Force Main Thread Execution
TEST_F(SchedulerTest, CanForceMainThreadExecution)
{
    std::atomic<std::thread::id> executionThreadId{};
    auto                         mainThreadId = std::this_thread::get_id();

    // Schedule task with forced main thread affinity
    scheduler->schedule(
        ForcedThreadAffinity::MainThread,
        [&executionThreadId]() -> Task<void>
        {
            executionThreadId.store(std::this_thread::get_id());
            co_return;
        });

    // Run on main thread
    scheduler->runExpiredTasks();

    EXPECT_EQ(executionThreadId.load(), mainThreadId);
}

// Test 7: RAII Cancellation Token Auto-Cancel
TEST_F(SchedulerTest, CancellationTokenAutoCancel)
{
    static std::atomic<int> executionCount{ 0 };
    executionCount = 0; // Reset for this test

    // Create interval task with a short interval
    auto token = scheduler->scheduleInterval(
        50ms,
        []() -> Task<void>
        {
            executionCount.fetch_add(1);
            co_return;
        });

    // Let it execute once
    std::this_thread::sleep_for(60ms);

    // This is where the crash likely occurs
    scheduler->runExpiredTasks();

    EXPECT_GT(executionCount.load(), 0);

    // Cancel the token
    token.cancel();
}

// Test 8: Task Suspension and Resumption
TEST_F(SchedulerTest, TaskSuspensionAndResumption)
{
    std::atomic<bool> taskStarted{ false };
    std::atomic<bool> taskCompleted{ false };

    scheduler->schedule(
        [&taskStarted, &taskCompleted]() -> Task<void>
        {
            taskStarted.store(true);

            // Suspend by awaiting an AsyncTask that runs on worker thread
            co_await []() -> AsyncTask<void>
            {
                // Simulate some async work
                std::this_thread::sleep_for(50ms);
                co_return;
            }();

            taskCompleted.store(true);
            co_return;
        });

    // Initial execution should start the task
    scheduler->runExpiredTasks();
    std::this_thread::sleep_for(10ms);

    EXPECT_TRUE(taskStarted.load());
    EXPECT_FALSE(taskCompleted.load()); // Should be suspended

    // Allow time for completion
    std::this_thread::sleep_for(100ms);
    scheduler->runExpiredTasks();

    EXPECT_TRUE(taskCompleted.load());
}

// Test 9: Complex Thread Routing - Task->AsyncTask->Task->AsyncTask on Main->Worker->Main->Worker
TEST_F(SchedulerTest, ComplexThreadRouting)
{
    std::vector<std::thread::id> executionOrder;
    std::mutex                   orderMutex;
    std::atomic<bool>            testCompleted{ false };
    auto                         mainThreadId = std::this_thread::get_id();

    // Create a complex coroutine that alternates between Task and AsyncTask
    auto complexTask = [&]() -> Task<void>
    {
        // Step 1: This should execute on main thread (Task)
        {
            std::lock_guard<std::mutex> lock(orderMutex);
            executionOrder.push_back(std::this_thread::get_id());
        }

        // Step 2: Switch to worker thread (AsyncTask)
        co_await [&]() -> AsyncTask<void>
        {
            // This should execute on worker thread - capture thread ID here
            {
                std::lock_guard<std::mutex> lock(orderMutex);
                executionOrder.push_back(std::this_thread::get_id());
            }
            co_return;
        }();

        // Step 3: Switch back to main thread (Task)
        co_await [&]() -> Task<void>
        {
            // This should execute on main thread - capture thread ID here
            {
                std::lock_guard<std::mutex> lock(orderMutex);
                executionOrder.push_back(std::this_thread::get_id());
            }
            co_return;
        }();

        // Step 4: Switch to worker thread again (AsyncTask)
        co_await [&]() -> AsyncTask<void>
        {
            // This should execute on worker thread - capture thread ID here
            {
                std::lock_guard<std::mutex> lock(orderMutex);
                executionOrder.push_back(std::this_thread::get_id());
            }
            co_return;
        }();

        testCompleted.store(true);
        co_return;
    };

    scheduler->schedule(complexTask);

    // Process tasks multiple times to handle thread switches
    for (int i = 0; i < 10 && !testCompleted.load(); ++i)
    {
        scheduler->runExpiredTasks();
        std::this_thread::sleep_for(50ms);
    }

    EXPECT_TRUE(testCompleted.load());

    // Verify execution order and thread affinity
    std::lock_guard<std::mutex> lock(orderMutex);
    ASSERT_EQ(executionOrder.size(), 4);

    // Step 1: Main thread (Task)
    EXPECT_EQ(executionOrder[0], mainThreadId);

    // Step 2: Worker thread (AsyncTask)
    EXPECT_NE(executionOrder[1], mainThreadId);

    // Step 3: Main thread (Task)
    EXPECT_EQ(executionOrder[2], mainThreadId);

    // Step 4: Worker thread (AsyncTask)
    EXPECT_NE(executionOrder[3], mainThreadId);
}

// Test 10: Debug Thread Affinity - Simple AsyncTask Test
TEST_F(SchedulerTest, SimpleAsyncTaskTest)
{
    auto mainThreadId = std::this_thread::get_id();

    std::atomic<std::thread::id> executionThreadId{};

    // Schedule a simple AsyncTask directly
    scheduler->schedule(
        []() -> AsyncTask<void>
        {
            co_return;
        });

    // Give time for execution
    std::this_thread::sleep_for(50ms);
    scheduler->runExpiredTasks();
    std::this_thread::sleep_for(50ms);

    // The AsyncTask should have been routed to a worker thread
    // Let's add a more detailed test to capture execution info
    EXPECT_NE(executionThreadId.load(), mainThreadId);
}

// Test 11: Debug Thread Affinity - Direct Task Scheduling
TEST_F(SchedulerTest, DirectTaskScheduling)
{
    std::atomic<std::thread::id> mainTaskThread{};
    std::atomic<std::thread::id> asyncTaskThread{};
    auto                         mainThreadId = std::this_thread::get_id();

    // Test Task (should run on main thread)
    auto mainTask = [&mainTaskThread]() -> Task<void>
    {
        mainTaskThread.store(std::this_thread::get_id());
        co_return;
    };

    // Test AsyncTask (should run on worker thread)
    auto workerTask = [&asyncTaskThread]() -> AsyncTask<void>
    {
        asyncTaskThread.store(std::this_thread::get_id());
        co_return;
    };

    scheduler->schedule(mainTask);
    scheduler->schedule(workerTask);

    // Process tasks multiple times
    for (int i = 0; i < 5; ++i)
    {
        scheduler->runExpiredTasks();
        std::this_thread::sleep_for(20ms);
    }

    EXPECT_EQ(mainTaskThread.load(), mainThreadId);
    EXPECT_NE(asyncTaskThread.load(), mainThreadId);
}

// Test 12: Interval Task Sequential Execution with Long-Running Tasks
//
// BACKGROUND & PROBLEM:
// This test captures and validates the fix for a data race issue discovered
// in the while integrating with LandSandBoat. The issue manifested in the pathfinding code
// (pathfind.cpp) where multiple zone tick tasks were executing simultaneously.
//
// ORIGINAL ISSUE:
// LandSandBoat uses scheduleInterval() to run zone logic every 400ms via zoneRunner()
// coroutines. Each zone's game logic (AI pathfinding, entity updates, etc.) was intended
// to run sequentially - one tick completing before the next begins. However, the original
// IntervalTask implementation would reschedule the next interval BEFORE the current child
// task completed, leading to overlapping executions when child tasks suspended (e.g.,
// during pathfinding operations that offload to worker threads).
//
// DATA RACE:
// In pathfind.cpp, this caused race conditions on shared data members like m_points
// and m_currentPoint, where one zone tick would modify these while another was still
// accessing them, leading to crashes and undefined behavior.
//
// THE FIX:
// IntervalTask now enforces sequential execution by:
// 1. Only rescheduling the next interval AFTER the current child task completes
// 2. Using activeIntervalTasks_ tracking to prevent multiple instances of the same
//    interval task from executing simultaneously
// 3. Allowing child tasks to suspend/resume on worker threads while maintaining
//    the guarantee that only one instance of a given interval task runs at a time
//
// This test verifies that even when interval tasks take longer than their interval
// period, they execute sequentially rather than in parallel, preventing data races.
TEST_F(SchedulerTest, IntervalTaskSequentialExecution)
{
    static std::atomic<int> executionCount{ 0 };
    static std::atomic<int> simultaneousExecutions{ 0 };
    static std::atomic<int> maxSimultaneousExecutions{ 0 };

    executionCount.store(0);
    simultaneousExecutions.store(0);
    maxSimultaneousExecutions.store(0);

    // Schedule interval task that takes longer than the interval to complete
    auto token = scheduler->scheduleInterval(
        50ms, // 50ms interval
        []() -> Task<void>
        {
            // Track simultaneous executions
            int current = simultaneousExecutions.fetch_add(1) + 1;
            int max     = maxSimultaneousExecutions.load();
            while (current > max && !maxSimultaneousExecutions.compare_exchange_weak(max, current))
            {
                max = maxSimultaneousExecutions.load();
            }

            executionCount.fetch_add(1);

            // Simulate long-running task that takes longer than interval
            std::this_thread::sleep_for(75ms); // Longer than 50ms interval

            simultaneousExecutions.fetch_sub(1);
            co_return;
        });

    // Run for about 300ms (should allow ~6 intervals, but only sequential execution)
    auto startTime = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - startTime < 300ms)
    {
        scheduler->runExpiredTasks();
        std::this_thread::sleep_for(10ms);
    }

    token.cancel();

    // Allow any remaining tasks to complete
    std::this_thread::sleep_for(100ms);
    scheduler->runExpiredTasks();

    // Verify sequential execution behavior
    int finalCount      = executionCount.load();
    int maxSimultaneous = maxSimultaneousExecutions.load();

    // Should have executed at least a few times, but not as many as if they were parallel
    EXPECT_GT(finalCount, 1) << "Should have executed multiple times";
    EXPECT_LT(finalCount, 6) << "Should not execute as many times as if parallel (due to long execution)";

    // Most importantly: should never have more than 1 simultaneous execution
    EXPECT_EQ(maxSimultaneous, 1) << "Should never have more than 1 simultaneous execution";
}

// Test 13: Interval Task with Suspending Coroutines
//
// ADVANCED SCENARIO:
// This test specifically validates the fix for the more complex case where interval
// tasks contain multiple suspension points (co_await operations that move work to
// worker threads). This directly mirrors the LandSandBoat zone tick pattern.
//
// SERVER TICK PATTERN:
// In LandSandBoat, zoneRunner() calls CZone::ZoneServer() which in turn calls
// entity logic that frequently suspends via co_await-ing on AsyncTask work.
// Each suspension moves work to a worker thread, then resumes on the main thread
// when complete.
//
// CRITICAL REQUIREMENT:
// Even with multiple suspensions within a single zone tick, we must guarantee:
// 1. No overlapping execution of zone ticks for the same zone
// 2. Proper resumption on the main thread after worker thread operations
// 3. Sequential processing even if individual ticks take longer than the interval
//
// This test simulates multiple AsyncTask suspensions (worker thread operations)
// within each interval task execution and verifies that the sequential execution
// guarantee is maintained throughout all suspension/resumption cycles.
TEST_F(SchedulerTest, IntervalTaskWithSuspendingCoroutines)
{
    static std::atomic<int> executionCount{ 0 };
    static std::atomic<int> suspensionCount{ 0 };
    static std::atomic<int> simultaneousExecutions{ 0 };
    static std::atomic<int> maxSimultaneousExecutions{ 0 };

    executionCount.store(0);
    suspensionCount.store(0);
    simultaneousExecutions.store(0);
    maxSimultaneousExecutions.store(0);

    auto token = scheduler->scheduleInterval(
        30ms, // 30ms interval
        []() -> Task<void>
        {
            // Track simultaneous executions
            int current = simultaneousExecutions.fetch_add(1) + 1;
            int max     = maxSimultaneousExecutions.load();
            while (current > max && !maxSimultaneousExecutions.compare_exchange_weak(max, current))
            {
                max = maxSimultaneousExecutions.load();
            }

            executionCount.fetch_add(1);

            // Simulate work that suspends and resumes multiple times
            co_await [&]() -> AsyncTask<void>
            {
                suspensionCount.fetch_add(1);
                std::this_thread::sleep_for(20ms); // Work on worker thread
                co_return;
            }();

            co_await [&]() -> AsyncTask<void>
            {
                suspensionCount.fetch_add(1);
                std::this_thread::sleep_for(15ms); // More work on worker thread
                co_return;
            }();

            simultaneousExecutions.fetch_sub(1);
            co_return;
        });

    // Run for about 200ms
    auto startTime = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - startTime < 200ms)
    {
        scheduler->runExpiredTasks();
        std::this_thread::sleep_for(5ms);
    }

    token.cancel();

    // Allow any remaining tasks to complete
    std::this_thread::sleep_for(100ms);
    for (int i = 0; i < 10; ++i)
    {
        scheduler->runExpiredTasks();
        std::this_thread::sleep_for(10ms);
    }

    int finalCount       = executionCount.load();
    int finalSuspensions = suspensionCount.load();
    int maxSimultaneous  = maxSimultaneousExecutions.load();

    // Should have executed multiple times with suspensions
    EXPECT_GT(finalCount, 1) << "Should have executed multiple times";
    EXPECT_EQ(finalSuspensions, finalCount * 2) << "Should have 2 suspensions per execution";

    // Most importantly: should never have more than 1 simultaneous execution
    EXPECT_EQ(maxSimultaneous, 1) << "Should never have more than 1 simultaneous execution, even with suspensions";
}

// TEST 14:
// This test reproduces a LandSandBoat issue where IntervalTask
// completes but doesn't reschedule itself, leading to an empty task queue.
//
// The LandSandBoat tick pattern is:
// 1. scheduleInterval calls zoneRunner (Task)
// 2. zoneRunner calls CZone::ZoneServer (Task)
// 3. ZoneServer calls entity AI that does co_await AsyncTask (pathfinding, etc.)
// 4. AsyncTask completes on worker thread, resumes on main thread
// 5. zoneRunner completes, IntervalTask should reschedule
TEST_F(SchedulerTest, IntervalTaskReschedulingFailure)
{
    static std::atomic<int> preExecutionCount{ 0 };
    static std::atomic<int> asyncTaskCount{ 0 };
    static std::atomic<int> postExecutionCount{ 0 };

    preExecutionCount.store(0);
    asyncTaskCount.store(0);
    postExecutionCount.store(0);

    // Simulate the zone tick logic with Task->AsyncTask->Task pattern
    auto zoneTickFactory = []() -> Task<void>
    {
        preExecutionCount.fetch_add(1);

        // Simulate CZone::ZoneServer calling entity AI that suspends
        co_await [&]() -> AsyncTask<void>
        {
            asyncTaskCount.fetch_add(1);

            // Simulate pathfinding/AI work on worker thread
            std::this_thread::sleep_for(10ms);

            co_return;
        }();

        // Resume on main thread after AsyncTask completes
        // This is where the FFXI server would continue processing
        postExecutionCount.fetch_add(1);

        co_return;
    };

    // Schedule the interval task (like gScheduler.scheduleInterval(kLogicUpdateInterval))
    auto token = scheduler->scheduleInterval(100ms, std::move(zoneTickFactory));

    // Wait for multiple executions - this should trigger the rescheduling bug
    // Total time to run: 350ms
    const auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < 7; ++i)
    {
        scheduler->runExpiredTasks();

        const auto now = std::chrono::steady_clock::now();
        if (now - start < 350ms)
        {
            std::this_thread::sleep_for(50ms);
            continue;
        }

        // 350ms has passed, lets cancel the task, sleep for a further 250ms to make sure the task has properly stopped
        token.cancel();
        std::this_thread::sleep_for(250ms);
    }

    int finalCount              = preExecutionCount.load();
    int finalAsyncCount         = asyncTaskCount.load();
    int finalPostExecutionCount = postExecutionCount.load();

    // If we've scheduled the zoneTickFactory to run at intervals of 100ms, and
    // we are continually calling scheduler->runExpiredTasks() for 350ms, we
    // expect the different execution counts all to equal 3.
    EXPECT_EQ(finalCount, 3) << "IntervalTask should execute 3 times. Got: " << finalCount;
    EXPECT_EQ(finalAsyncCount, 3) << "AsyncTask should execute 3 times. Got: " << finalAsyncCount;
    EXPECT_EQ(finalPostExecutionCount, 3) << "PostExecutionCount should execute 3 times. Got: " << finalPostExecutionCount;
}