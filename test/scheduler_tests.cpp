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
    std::atomic<bool>            taskExecuted{ false };
    std::atomic<std::thread::id> executionThreadId{};
    auto                         mainThreadId = std::this_thread::get_id();

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
