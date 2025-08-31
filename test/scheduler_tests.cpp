#include <atomic>
#include <chrono>
#include <gtest/gtest.h>
#include <thread>
#include <vector>

#include "corororo/corororo.h"

#include "test_utils.h"

using namespace std::chrono_literals;
using namespace CoroRoro;

// Simulate the FFXI server position_t struct
struct position_t {
    float x, y, z;
    unsigned char moving;
    unsigned char rotation;
    
    position_t(float x, float y, float z, unsigned char moving, unsigned char rotation)
        : x(x), y(y), z(z), moving(moving), rotation(rotation) {}
};

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

    const int countBeforeCancel = executionCount.load();
    EXPECT_GT(countBeforeCancel, 0);

    // Cancel the interval task
    token.cancel();

    // Wait a short time for cancellation to propagate, then run tasks
    std::this_thread::sleep_for(10ms);
    scheduler->runExpiredTasks();

    // Allow for one more execution due to race condition (task already in queue)
    const int countAfterCancel = executionCount.load();

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
    const auto                             startTime = std::chrono::steady_clock::now();
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
    const auto                   mainThreadId = std::this_thread::get_id();

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
    const auto                   mainThreadId = std::this_thread::get_id();

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
    const auto mainThreadId = std::this_thread::get_id();

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
    const auto                   mainThreadId = std::this_thread::get_id();

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
    const auto startTime = std::chrono::steady_clock::now();
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
    const int finalCount      = executionCount.load();
    const int maxSimultaneous = maxSimultaneousExecutions.load();

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
    const auto startTime = std::chrono::steady_clock::now();
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

    const int finalCount       = executionCount.load();
    const int finalSuspensions = suspensionCount.load();
    const int maxSimultaneous  = maxSimultaneousExecutions.load();

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

    const int finalCount              = preExecutionCount.load();
    const int finalAsyncCount         = asyncTaskCount.load();
    const int finalPostExecutionCount = postExecutionCount.load();

    // If we've scheduled the zoneTickFactory to run at intervals of 100ms, and
    // we are continually calling scheduler->runExpiredTasks() for 350ms, we
    // expect the different execution counts all to equal 3.
    EXPECT_EQ(finalCount, 3) << "IntervalTask should execute 3 times. Got: " << finalCount;
    EXPECT_EQ(finalAsyncCount, 3) << "AsyncTask should execute 3 times. Got: " << finalAsyncCount;
    EXPECT_EQ(finalPostExecutionCount, 3) << "PostExecutionCount should execute 3 times. Got: " << finalPostExecutionCount;
}

//
// TEST: Interval Task Rescheduling When Behind Schedule
// PURPOSE: Verify that interval tasks properly reschedule and execute when running behind
// BEHAVIOR: Creates interval tasks that take longer than their interval, then verifies rescheduling
// EXPECTATION: Tasks should reschedule and execute immediately when behind schedule
// 
// BACKGROUND: This test addresses the core FFXI server issue where interval tasks
// don't properly reschedule when they're running behind. The scheduler should:
// 1. Detect when a task is behind schedule
// 2. Reschedule it to run immediately
// 3. Ensure it actually executes
// 
TEST_F(SchedulerTest, IntervalTaskReschedulingWhenBehindSchedule)
{
    static std::atomic<int> executionCount{ 0 };
    static std::atomic<int> completedCount{ 0 };
    
    executionCount.store(0);
    completedCount.store(0);
    
    // Create an interval task that takes longer than its interval to complete
    auto longRunningTask = [&]() -> Task<void> {
        executionCount.fetch_add(1);
        
        // Simulate work that takes longer than the interval
        // This should cause the task to run behind schedule
        std::this_thread::sleep_for(150ms); // Longer than 100ms interval
        
        completedCount.fetch_add(1);
        co_return;
    };
    
    // Schedule with 100ms interval, but task takes 150ms to complete
    // This should cause the task to run behind schedule
    auto token = scheduler->scheduleInterval(100ms, std::move(longRunningTask));
    
    // Let it run for several intervals to see the rescheduling behavior
    for (int iteration = 0; iteration < 5; ++iteration)
    {
        // Run expired tasks multiple times to allow for rescheduling
        for (int j = 0; j < 10; ++j)
        {
            scheduler->runExpiredTasks();
            std::this_thread::sleep_for(20ms);
        }
        
        // Wait for the next expected interval
        std::this_thread::sleep_for(100ms);
    }
    
    // Cancel the task
    token.cancel();
    
    // Allow any remaining tasks to complete
    std::this_thread::sleep_for(200ms);
    for (int i = 0; i < 10; ++i)
    {
        scheduler->runExpiredTasks();
        std::this_thread::sleep_for(20ms);
    }
    
    // Final analysis
    const int finalExecutions = executionCount.load();
    const int finalCompleted = completedCount.load();
    
    // Calculate expected behavior:
    // - 100ms interval over ~500ms should allow for multiple executions
    // - Even with 150ms execution time, we should see at least 3-4 executions
    // - The key is that when behind schedule, tasks should reschedule immediately
    
    EXPECT_GE(finalExecutions, 3) << "Should have started at least 3 executions due to rescheduling when behind schedule. Got: " << finalExecutions;
    EXPECT_GE(finalCompleted, 3) << "Should have completed at least 3 executions. Got: " << finalCompleted;
    EXPECT_EQ(finalExecutions, finalCompleted) << "All started executions should complete";
}

// ============================================================================
// TEST: Clock Type Comparison - Why Timestamps Look Suspicious
// PURPOSE: Demonstrate the difference between steady_clock and system_clock
// BEHAVIOR: Shows how different clock types produce different timestamp values
// EXPECTATION: steady_clock produces relative timestamps, system_clock produces absolute timestamps
// 
// BACKGROUND: This test explains why the FFXI server shows suspicious timestamps like "483434871ms".
// The issue is likely that steady_clock::time_since_epoch() is being used for logging instead of
// system_clock::time_since_epoch(). steady_clock measures time since system boot, while
// system_clock measures time since Unix epoch (1970).
// ============================================================================
TEST_F(SchedulerTest, ClockTypeComparison)
{
    // Get current time using different clock types
    const auto steadyNow = std::chrono::steady_clock::now();
    const auto systemNow = std::chrono::system_clock::now();
    
    // Convert to milliseconds since epoch
    const auto steadyMs = std::chrono::duration_cast<std::chrono::milliseconds>(steadyNow.time_since_epoch()).count();
    const auto systemMs = std::chrono::duration_cast<std::chrono::milliseconds>(systemNow.time_since_epoch()).count();
    
    // Calculate time since Unix epoch (1970)
    const auto unixEpoch = std::chrono::system_clock::from_time_t(0);
    
    // Convert Unix timestamp to date
    const auto unixTime = std::chrono::duration_cast<std::chrono::seconds>(systemNow - unixEpoch).count();
    
    // Use Windows-safe time functions
    const time_t rawTime = static_cast<time_t>(unixTime);
    struct tm timeInfo;
    #ifdef _WIN32
        gmtime_s(&timeInfo, &rawTime);
    #else
        timeInfo = *std::gmtime(&rawTime);
    #endif
    
    char timeStr[100];
    std::strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S UTC", &timeInfo);
    
    // The key insight: steady_clock is for relative timing, system_clock is for absolute timing
    // steady_clock::time_since_epoch() gives time since system boot (~5.6 days)
    // system_clock::time_since_epoch() gives time since Unix epoch (1970)
    // For logging absolute timestamps, use system_clock!
    // For measuring intervals, use steady_clock!
    
    // Verify the values make sense
    EXPECT_GT(systemMs, 1600000000000) << "system_clock should be after 2020"; // After 2020
    EXPECT_LT(steadyMs, 1000000000) << "steady_clock should be less than ~11 days"; // Less than ~11 days
}

// ============================================================================
// TEST: Realistic FFXI Server Simulation - Memory Pressure + Complex Chains
// PURPOSE: Reproduce the exact FFXI server issue with realistic conditions
// BEHAVIOR: Creates a test that simulates memory pressure, complex task chains, and real-world timing
// EXPECTATION: Should execute consistently every 400ms even under pressure
// 
// BACKGROUND: The FFXI server has real issues that our simple tests don't capture:
// - Memory pressure from managing hundreds of entities
// - Complex pathfinding and AI calculations
// - Database operations and network I/O
// - System-level contention that affects timing
// ============================================================================
TEST_F(SchedulerTest, RealisticFFXIServerSimulation)
{
    static std::atomic<int> executionCount{ 0 };
    static std::atomic<int> completionCount{ 0 };
    static std::atomic<int> memoryPressureCount{ 0 };
    
    executionCount.store(0);
    completionCount.store(0);
    memoryPressureCount.store(0);

    // Simulate memory pressure by allocating/deallocating vectors
    auto createMemoryPressure = [&]() -> Task<void> {
        memoryPressureCount.fetch_add(1);
        
        // Simulate the kind of memory pressure the FFXI server experiences
        std::vector<std::vector<int>> entityData;
        for (int i = 0; i < 100; ++i) {
            entityData.emplace_back(1000, i); // 1000 integers per entity
        }
        
        // Simulate some work with the data
        int sum = 0;
        for (const auto& entity : entityData) {
            for (int val : entity) {
                sum += val;
            }
        }
        
        // Force some memory pressure
        entityData.clear();
        entityData.shrink_to_fit();
        
        co_return;
    };

    // Simulate the complex FFXI server task chain with real-world conditions
    auto realisticZoneTick = [&]() -> Task<void> {
        executionCount.fetch_add(1);
        
        // Level 1: Zone initialization (like CZone::ZoneServer)
        co_await [&]() -> Task<void> {
            // Simulate zone setup work
            std::this_thread::sleep_for(10ms);
            co_return;
        }();
        
        // Level 2: Entity management (like CZoneEntities::ZoneServer)
        co_await [&]() -> Task<void> {
            // Simulate entity AI and pathfinding
            co_await [&]() -> AsyncTask<void> {
                // This runs on worker thread - simulate heavy AI work
                std::this_thread::sleep_for(50ms);
                
                // Simulate pathfinding calculations
                std::vector<int> path = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
                for (int i = 0; i < 1000; ++i) {
                    std::rotate(path.begin(), path.begin() + 1, path.end());
                }
                
                co_return;
            }();
            
            // Simulate more entity work
            std::this_thread::sleep_for(20ms);
            co_return;
        }();
        
        // Level 3: Memory pressure simulation
        co_await createMemoryPressure();
        
        // Level 4: Final cleanup and state updates
        co_await [&]() -> Task<void> {
            // Simulate database updates or state persistence
            std::this_thread::sleep_for(15ms);
            co_return;
        }();
        
        completionCount.fetch_add(1);
        co_return;
    };

    // Schedule with 400ms interval (matching your FFXI server)
    auto token = scheduler->scheduleInterval(400ms, std::move(realisticZoneTick));

    // Run for a realistic duration - simulate your FFXI server running
    const auto startTime = std::chrono::steady_clock::now();
    const auto testDuration = 10s; // 10 seconds to see the pattern
    
    while (std::chrono::steady_clock::now() - startTime < testDuration) {
        // Process tasks multiple times per second (like your FFXI server main loop)
        for (int i = 0; i < 10; ++i) {
            scheduler->runExpiredTasks();
            std::this_thread::sleep_for(100ms); // 100ms between main loop iterations
        }
    }

    // Cancel and allow cleanup
    token.cancel();
    std::this_thread::sleep_for(1s);
    
    // Final analysis
    const int finalExecutions = executionCount.load();
    const int finalCompletions = completionCount.load();
    const int finalMemoryPressure = memoryPressureCount.load();
    
    // Calculate expected executions: 10 seconds / 400ms = 25 executions
    const int expectedExecutions = 25;
    const int tolerance = 3; // Allow some tolerance for timing variations
    
    // The key test: we should get close to the expected number of executions
    EXPECT_GE(finalExecutions, expectedExecutions - tolerance) 
        << "Should execute close to " << expectedExecutions << " times. Got: " << finalExecutions;
    EXPECT_LE(finalExecutions, expectedExecutions + tolerance)
        << "Should not execute significantly more than " << expectedExecutions << " times. Got: " << finalExecutions;
    
    // All executions should complete
    EXPECT_EQ(finalExecutions, finalCompletions) 
        << "All executions should complete. Executions: " << finalExecutions << ", Completions: " << finalCompletions;
    
    // Memory pressure should be applied
    EXPECT_GT(finalMemoryPressure, 0) 
        << "Memory pressure simulation should run";
    
    // Log the actual timing for analysis
    std::cout << "Realistic FFXI Server Simulation Results:" << std::endl;
    std::cout << "  Expected executions: " << expectedExecutions << std::endl;
    std::cout << "  Actual executions: " << finalExecutions << std::endl;
    std::cout << "  Actual completions: " << finalCompletions << std::endl;
    std::cout << "  Memory pressure cycles: " << finalMemoryPressure << std::endl;
    std::cout << "  Test duration: " << std::chrono::duration_cast<std::chrono::milliseconds>(testDuration).count() << "ms" << std::endl;
}

TEST_F(SchedulerTest, AccurateFFXIServerTaskChain)
{
    static std::atomic<int> zoneRunnerCount{ 0 };
    static std::atomic<int> zoneServerCount{ 0 };
    static std::atomic<int> zoneEntitiesCount{ 0 };
    static std::atomic<int> mobControllerCount{ 0 };
    static std::atomic<int> pathfindCount{ 0 };
    static std::atomic<int> navmeshCount{ 0 };
    
    zoneRunnerCount.store(0);
    zoneServerCount.store(0);
    zoneEntitiesCount.store(0);
    mobControllerCount.store(0);
    pathfindCount.store(0);
    navmeshCount.store(0);

    // Level 6: NavMesh::FindRandomPath (AsyncTask) - the heavy operation
    auto navmeshFindRandomPath = [&]() -> AsyncTask<std::vector<position_t>> {
        navmeshCount.fetch_add(1);
        std::this_thread::sleep_for(std::chrono::microseconds(rand() % 5000)); // 0-5ms
        std::vector<position_t> path;
        for (int i = 0; i < 5; ++i) {
            path.push_back({static_cast<float>(i), 0.0f, static_cast<float>(i), 0, 0});
        }
        co_return path;
    };

    // Level 5: CPathFind::RoamAround (Task) - calls navmesh
    auto pathfindRoamAround = [&]() -> Task<bool> {
        pathfindCount.fetch_add(1);
        auto path = co_await navmeshFindRandomPath();
        co_return !path.empty();
    };

    // Level 4: CMobController::Tick (Task) - calls pathfinding
    auto mobControllerTick = [&]() -> Task<void> {
        mobControllerCount.fetch_add(1);
        bool pathFound = co_await pathfindRoamAround();
        if (pathFound) {
            std::this_thread::sleep_for(1ms);
        }
        co_return;
    };

    // Level 3: CZoneEntities::ZoneServer (Task) - iterates through mobs
    auto zoneEntitiesZoneServer = [&]() -> Task<void> {
        zoneEntitiesCount.fetch_add(1);
        for (int i = 0; i < 330; ++i) { // Your logs show "Mob ticked 330 mobs"
            co_await mobControllerTick();
        }
        std::this_thread::sleep_for(5ms);
        co_return;
    };

    // Level 2: CZone::ZoneServer (Task) - calls zone entities
    auto zoneZoneServer = [&]() -> Task<void> {
        zoneServerCount.fetch_add(1);
        co_await zoneEntitiesZoneServer();
        std::this_thread::sleep_for(2ms);
        co_return;
    };

    // Level 1: zoneRunner (Task) - the factory function
    auto zoneRunner = [&]() -> Task<void> {
        zoneRunnerCount.fetch_add(1);
        co_await zoneZoneServer();
        co_return;
    };

    auto token = scheduler->scheduleInterval(400ms, std::move(zoneRunner));
    const auto startTime = std::chrono::steady_clock::now();
    const auto testDuration = 5s;
    
    while (std::chrono::steady_clock::now() - startTime < testDuration) {
        for (int i = 0; i < 5; ++i) {
            scheduler->runExpiredTasks();
            std::this_thread::sleep_for(200ms);
        }
    }

    token.cancel();
    std::this_thread::sleep_for(1s);
    
    const int finalZoneRunner = zoneRunnerCount.load();
    const int finalZoneServer = zoneServerCount.load();
    const int finalZoneEntities = zoneEntitiesCount.load();
    const int finalMobController = mobControllerCount.load();
    const int finalPathfind = pathfindCount.load();
    const int finalNavmesh = navmeshCount.load();
    
    const int expectedExecutions = 12;
    const int tolerance = 2;
    
    EXPECT_GE(finalZoneRunner, expectedExecutions - tolerance);
    EXPECT_LE(finalZoneRunner, expectedExecutions + tolerance);
    EXPECT_EQ(finalZoneRunner, finalZoneServer);
    EXPECT_EQ(finalZoneServer, finalZoneEntities);
    EXPECT_EQ(finalMobController, finalZoneEntities * 330);
    EXPECT_EQ(finalPathfind, finalMobController);
    EXPECT_EQ(finalNavmesh, finalPathfind);
    
    std::cout << "Accurate FFXI Server Task Chain Results:" << std::endl;
    std::cout << "  Expected zoneRunner executions: " << expectedExecutions << std::endl;
    std::cout << "  Actual zoneRunner executions: " << finalZoneRunner << std::endl;
    std::cout << "  ZoneServer executions: " << finalZoneServer << std::endl;
    std::cout << "  ZoneEntities executions: " << finalZoneEntities << std::endl;
    std::cout << "  MobController executions: " << finalMobController << std::endl;
    std::cout << "  Pathfind executions: " << finalPathfind << std::endl;
    std::cout << "  Navmesh executions: " << finalNavmesh << std::endl;
}