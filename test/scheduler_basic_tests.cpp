#include <atomic>
#include <chrono>
#include <gtest/gtest.h>
#include <thread>
#include <vector>

#include "corororo/corororo.h"

#include "test_utils.h"

using namespace std::chrono_literals;
using namespace CoroRoro;

class SchedulerBasicTest : public ::testing::Test
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
TEST_F(SchedulerBasicTest, CanConstructAndDestruct)
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
TEST_F(SchedulerBasicTest, CanScheduleSimpleTask)
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
// TEST: Schedule Multiple Tasks
// PURPOSE: Verify that multiple tasks can be scheduled and executed
// BEHAVIOR: Schedules multiple coroutines that increment a counter
// EXPECTATION: All tasks execute and counter reaches expected value
//
TEST_F(SchedulerBasicTest, CanScheduleMultipleTasks)
{
    const int        numTasks = 10;
    std::atomic<int> tasksCompleted{ 0 };

    // Schedule multiple tasks
    for (int i = 0; i < numTasks; ++i)
    {
        scheduler->schedule(
            [&tasksCompleted]() -> Task<void>
            {
                tasksCompleted.fetch_add(1);
                co_return;
            });
    }

    // Run main thread tasks
    scheduler->runExpiredTasks();

    // Give worker threads time to execute
    std::this_thread::sleep_for(50ms);

    EXPECT_EQ(tasksCompleted.load(), numTasks);
}

//
// TEST: AsyncTask Execution
// PURPOSE: Verify that AsyncTask can be scheduled and executed on worker threads
// BEHAVIOR: Schedules an AsyncTask that sets an atomic flag
// EXPECTATION: AsyncTask executes on a worker thread and flag is set
//
TEST_F(SchedulerBasicTest, CanScheduleAsyncTask)
{
    std::atomic<bool>            asyncTaskExecuted{ false };
    std::thread::id              mainThreadId = std::this_thread::get_id();
    std::atomic<std::thread::id> asyncTaskThreadId{};

    // Schedule an AsyncTask
    scheduler->schedule(
        [&asyncTaskExecuted, &asyncTaskThreadId, mainThreadId]() -> Task<void>
        {
            co_await [&asyncTaskExecuted, &asyncTaskThreadId, mainThreadId]() -> AsyncTask<void>
            {
                // This should execute on a worker thread
                asyncTaskThreadId.store(std::this_thread::get_id());
                asyncTaskExecuted.store(true);
                co_return;
            }();
        });

    // Run main thread tasks
    scheduler->runExpiredTasks();

    // Give worker threads time to execute
    std::this_thread::sleep_for(50ms);

    EXPECT_TRUE(asyncTaskExecuted.load());
    EXPECT_NE(asyncTaskThreadId.load(), mainThreadId);
}

//
// TEST: Task Return Values
// PURPOSE: Verify that tasks can return values correctly
// BEHAVIOR: Schedules tasks that return different types of values
// EXPECTATION: Return values are correctly propagated
//
TEST_F(SchedulerBasicTest, CanReturnValuesFromTasks)
{
    std::atomic<int> intResult{ 0 };
    std::string      stringResult;
    std::mutex       stringMutex;

    // Schedule task returning int
    scheduler->schedule(
        [&intResult]() -> Task<void>
        {
            auto result = co_await [&intResult]() -> Task<int>
            {
                co_return 42;
            }();

            intResult.store(result);
        });

    // Schedule task returning string
    scheduler->schedule(
        [&stringResult, &stringMutex]() -> Task<void>
        {
            auto result = co_await [&stringResult]() -> Task<std::string>
            {
                co_return "Hello, World!";
            }();

            {
                std::lock_guard<std::mutex> lock(stringMutex);
                stringResult = result;
            }
        });

    // Run main thread tasks
    scheduler->runExpiredTasks();

    // Give worker threads time to execute
    std::this_thread::sleep_for(50ms);

    EXPECT_EQ(intResult.load(), 42);
    {
        std::lock_guard<std::mutex> lock(stringMutex);
        EXPECT_EQ(stringResult, "Hello, World!");
    }
}

//
// TEST: Exception Handling
// PURPOSE: Verify that exceptions in tasks are properly handled
// BEHAVIOR: Schedules a task that throws an exception
// EXPECTATION: Exception is caught and task state reflects failure
//
TEST_F(SchedulerBasicTest, CanHandleExceptions)
{
    std::atomic<bool> exceptionCaught{ false };

    // Schedule task that throws an exception
    scheduler->schedule(
        [&exceptionCaught]() -> Task<void>
        {
            try
            {
                auto throwingTask = [&exceptionCaught]() -> Task<void>
                {
                    throw std::runtime_error("Test exception");
                    co_return;
                }();

                // This should not be reached due to exception
                co_await throwingTask;
            }
            catch (const std::runtime_error& e)
            {
                exceptionCaught.store(true);
                EXPECT_STREQ(e.what(), "Test exception");
            }
        });

    // Run main thread tasks
    scheduler->runExpiredTasks();

    // Give worker threads time to execute
    std::this_thread::sleep_for(50ms);

    EXPECT_TRUE(exceptionCaught.load());
}

//
// TEST: Complex Nested Task Patterns
// PURPOSE: Verify that complex nested task patterns work correctly
// BEHAVIOR: Creates deeply nested Task->Task->Task->AsyncTask patterns
// EXPECTATION: All tasks execute correctly in the nested pattern
//
TEST_F(SchedulerBasicTest, CanHandleComplexNestedPatterns)
{
    std::atomic<int> taskCallCount{ 0 };
    std::atomic<int> affinityChangeCount{ 0 };

    // Create a complex nested pattern: Task->Task->Task->AsyncTask
    scheduler->schedule(
        [&taskCallCount, &affinityChangeCount]() -> Task<void>
        {
            // Level 1: Task
            ++taskCallCount;

            co_await [&taskCallCount]() -> Task<void>
            {
                // Level 2: Task
                ++taskCallCount;

                co_await [&taskCallCount]() -> Task<void>
                {
                    // Level 3: Task
                    ++taskCallCount;

                    co_await [&taskCallCount, &affinityChangeCount]() -> AsyncTask<void>
                    {
                        // Level 4: AsyncTask (should change thread affinity)
                        ++taskCallCount;
                        ++affinityChangeCount; // This represents the thread switch
                        co_return;
                    }();
                }();
            }();
        });

    // Run main thread tasks
    scheduler->runExpiredTasks();

    // Give worker threads time to execute
    std::this_thread::sleep_for(50ms);

    EXPECT_EQ(taskCallCount.load(), 4);
    EXPECT_EQ(affinityChangeCount.load(), 1);
}

//
// TEST: Long Alternating Chain
// PURPOSE: Verify that long alternating Task->AsyncTask->Task->AsyncTask chains work
// BEHAVIOR: Creates a long chain of alternating Task and AsyncTask calls
// EXPECTATION: All tasks execute correctly with proper thread switching
//
TEST_F(SchedulerBasicTest, CanHandleLongAlternatingChain)
{
    std::atomic<int> taskCallCount{ 0 };
    std::atomic<int> affinityChangeCount{ 0 };

    // Create a long alternating chain: Task->AsyncTask->Task->AsyncTask->Task->AsyncTask
    scheduler->schedule(
        [&taskCallCount, &affinityChangeCount]() -> Task<void>
        {
            // Level 1: Task
            ++taskCallCount;

            co_await [&taskCallCount, &affinityChangeCount]() -> AsyncTask<void>
            {
                // Level 2: AsyncTask
                ++taskCallCount;
                ++affinityChangeCount;

                co_await [&taskCallCount]() -> Task<void>
                {
                    // Level 3: Task
                    ++taskCallCount;

                    co_await [&taskCallCount, &affinityChangeCount]() -> AsyncTask<void>
                    {
                        // Level 4: AsyncTask
                        ++taskCallCount;
                        ++affinityChangeCount;

                        co_await [&taskCallCount]() -> Task<void>
                        {
                            // Level 5: Task
                            ++taskCallCount;

                            co_await [&taskCallCount, &affinityChangeCount]() -> AsyncTask<void>
                            {
                                // Level 6: AsyncTask
                                ++taskCallCount;
                                ++affinityChangeCount;
                                co_return;
                            }();
                        }();
                    }();
                }();
            }();
        });

    // Run main thread tasks
    scheduler->runExpiredTasks();

    // Give worker threads time to execute
    std::this_thread::sleep_for(50ms);

    EXPECT_EQ(taskCallCount.load(), 6);
    EXPECT_EQ(affinityChangeCount.load(), 3);
}

//
// TEST: Strides Pattern
// PURPOSE: Verify that "strides" pattern (3 Tasks, 3 AsyncTasks, 3 Tasks, 3 AsyncTasks) works
// BEHAVIOR: Creates a pattern with groups of 3 Tasks followed by groups of 3 AsyncTasks
// EXPECTATION: All tasks execute correctly with proper thread switching between groups
//
TEST_F(SchedulerBasicTest, CanHandleStridesPattern)
{
    std::atomic<int> taskCallCount{ 0 };
    std::atomic<int> affinityChangeCount{ 0 };

    // Create "strides" pattern: 3 Tasks, 3 AsyncTasks, 3 Tasks, 3 AsyncTasks
    scheduler->schedule(
        [&taskCallCount, &affinityChangeCount]() -> Task<void>
        {
            // First stride: 3 Tasks
            co_await [&taskCallCount]() -> Task<void>
            {
                ++taskCallCount;
                co_await [&taskCallCount]() -> Task<void>
                {
                    ++taskCallCount;
                    co_await [&taskCallCount]() -> Task<void>
                    {
                        ++taskCallCount;
                        co_return;
                    }();
                }();
            }();

            // Second stride: 3 AsyncTasks
            co_await [&taskCallCount, &affinityChangeCount]() -> AsyncTask<void>
            {
                ++taskCallCount;
                ++affinityChangeCount;
                co_await [&taskCallCount, &affinityChangeCount]() -> AsyncTask<void>
                {
                    ++taskCallCount;
                    ++affinityChangeCount;
                    co_await [&taskCallCount, &affinityChangeCount]() -> AsyncTask<void>
                    {
                        ++taskCallCount;
                        ++affinityChangeCount;
                        co_return;
                    }();
                }();
            }();

            // Third stride: 3 Tasks
            co_await [&taskCallCount]() -> Task<void>
            {
                ++taskCallCount;
                co_await [&taskCallCount]() -> Task<void>
                {
                    ++taskCallCount;
                    co_await [&taskCallCount]() -> Task<void>
                    {
                        ++taskCallCount;
                        co_return;
                    }();
                }();
            }();

            // Fourth stride: 3 AsyncTasks
            co_await [&taskCallCount, &affinityChangeCount]() -> AsyncTask<void>
            {
                ++taskCallCount;
                ++affinityChangeCount;
                co_await [&taskCallCount, &affinityChangeCount]() -> AsyncTask<void>
                {
                    ++taskCallCount;
                    ++affinityChangeCount;
                    co_await [&taskCallCount, &affinityChangeCount]() -> AsyncTask<void>
                    {
                        ++taskCallCount;
                        ++affinityChangeCount;
                        co_return;
                    }();
                }();
            }();
        });

    // Run main thread tasks
    scheduler->runExpiredTasks();

    // Give worker threads time to execute
    std::this_thread::sleep_for(50ms);

    EXPECT_EQ(taskCallCount.load(), 12);
    EXPECT_EQ(affinityChangeCount.load(), 6); // 2 groups of 3 AsyncTasks = 6 thread switches
}
