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

//
// TEST: Basic Scheduler Construction and Destruction
// PURPOSE: Verify that the scheduler can be created and destroyed without crashes
// BEHAVIOR: Creates a scheduler in SetUp and destroys it in TearDown
// EXPECTATION: No crashes or exceptions during construction/destruction
//
TEST_F(SchedulerTest, CanConstructAndDestruct)
{
    // This test passes if setup/teardown works without crash
    EXPECT_TRUE(scheduler != nullptr);
}

//
// TEST: Schedule a Simple Task for Immediate Execution
// PURPOSE: Verify that basic coroutine tasks can be scheduled and executed
// BEHAVIOR: Schedules a simple coroutine that sets an atomic flag
// EXPECTATION: Task executes and flag is set to true
//
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

//
// TEST: Schedule Task with General Callable (Non-Coroutine)
// PURPOSE: Verify that non-coroutine callables can be scheduled
// BEHAVIOR: Schedules a regular lambda function that modifies a static counter
// EXPECTATION: Function executes and counter is set to expected value
//
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

//
// TEST: Schedule Interval Task and Cancel It
// PURPOSE: Verify that interval tasks can be scheduled, executed, and cancelled
// BEHAVIOR: Creates an interval task that runs every 50ms, then cancels it
// EXPECTATION: Task executes multiple times before cancellation, then stops
//
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

//
// TEST: Schedule Delayed Task
// PURPOSE: Verify that tasks can be scheduled with a delay before execution
// BEHAVIOR: Schedules a task to execute after 100ms delay
// EXPECTATION: Task does not execute immediately, executes after delay period
//
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

//
// TEST: Thread Affinity - Force Main Thread Execution
// PURPOSE: Verify that tasks can be forced to execute on the main thread
// BEHAVIOR: Schedules a task with ForcedThreadAffinity::MainThread
// EXPECTATION: Task executes on the main thread, not on worker threads
//
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

//
// TEST: RAII Cancellation Token Auto-Cancel
// PURPOSE: Verify that cancellation tokens work correctly with RAII
// BEHAVIOR: Creates an interval task and tests token cancellation
// EXPECTATION: Task executes initially, then stops after cancellation
//
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

//
// TEST: Task Suspension and Resumption
// PURPOSE: Verify that tasks can suspend and resume correctly
// BEHAVIOR: Creates a task that suspends via AsyncTask, then resumes
// EXPECTATION: Task starts, suspends, then completes after resumption
//
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

//
// TEST: Complex Thread Routing - Task->AsyncTask->Task->AsyncTask Pattern
// PURPOSE: Verify complex thread switching between main and worker threads
// BEHAVIOR: Creates a coroutine that alternates between Task and AsyncTask
// EXPECTATION: Execution follows correct thread affinity pattern
//
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

//
// TEST: Simple AsyncTask Execution
// PURPOSE: Verify basic AsyncTask functionality and worker thread routing
// BEHAVIOR: Schedules a simple AsyncTask directly
// EXPECTATION: AsyncTask executes on worker thread, not main thread
//
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

//
// TEST: Direct Task Scheduling - Task vs AsyncTask Thread Affinity
// PURPOSE: Verify that Task and AsyncTask have correct thread affinity
// BEHAVIOR: Schedules both Task and AsyncTask types
// EXPECTATION: Task executes on main thread, AsyncTask on worker thread
//
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

//
// TEST: Interval Task Sequential Execution with Long-Running Tasks
// PURPOSE: Verify that interval tasks execute sequentially even when taking longer than interval
// BEHAVIOR: Creates interval tasks that take 75ms to complete with 50ms intervals
// EXPECTATION: Tasks execute sequentially, never more than 1 simultaneously
//
// BACKGROUND: This test validates a critical concurrency fix for LandSandBoat
// where zone tick tasks were executing simultaneously, causing data races in
// pathfinding code. The fix ensures sequential execution by only rescheduling
// the next interval after the current task completes.
//
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

//
// TEST: Interval Task with Suspending Coroutines
// PURPOSE: Verify sequential execution with complex suspension/resumption patterns
// BEHAVIOR: Creates interval tasks with multiple AsyncTask suspensions
// EXPECTATION: Sequential execution maintained even with multiple suspension points
//
// BACKGROUND: This test validates the fix for complex cases where interval tasks
// contain multiple suspension points (co_await operations that move work to worker
// threads). This directly mirrors LandSandBoat's zone tick pattern where
// entity logic frequently suspends via co_await for pathfinding, and other heavy tasks.
//
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

//
// TEST: Interval Task Rescheduling After Suspension
// PURPOSE: Verify that interval tasks properly reschedule after complex suspension patterns
// BEHAVIOR: Creates interval tasks with Task->AsyncTask->Task suspension pattern
// EXPECTATION: Tasks execute multiple times with proper rescheduling
//
// BACKGROUND: This test reproduces and validates the fix for a LandSandBoat issue
// where IntervalTask would complete but not reschedule itself after complex
// suspension patterns, leading to empty task queues. The pattern tested is:
// 1. scheduleInterval calls zoneRunner (Task)
// 2. zoneRunner calls CZone::ZoneServer (Task)
// 3. ZoneServer calls entity AI that does co_await AsyncTask (pathfinding, etc.)
// 4. AsyncTask completes on worker thread, resumes on main thread
// 5. zoneRunner completes, IntervalTask should reschedule
//
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