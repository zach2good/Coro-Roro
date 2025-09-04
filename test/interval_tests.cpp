#include <atomic>
#include <chrono>
#include <thread>

#include <corororo/corororo.h>
using namespace CoroRoro;

#include <gtest/gtest.h>

class IntervalTaskSchedulerTests : public ::testing::Test
{
protected:
    void SetUp() override
    {
        scheduler_ = std::make_unique<Scheduler>(4);
    }

    void TearDown() override
    {
        scheduler_.reset();
    }

    std::unique_ptr<Scheduler> scheduler_;
};

//
// Interval Task Factory Tests
//

TEST_F(IntervalTaskSchedulerTests, ScheduleIntervalBasic)
{
    std::atomic<int> executionCount{0};
    
    // Schedule an interval task that executes every 50ms
    auto token = scheduler_->scheduleInterval(
        std::chrono::milliseconds(50),
        [&executionCount]() -> Task<void> {
            executionCount.fetch_add(1);
            co_return;
        }
    );
    
    // Verify token is valid
    EXPECT_TRUE(token.valid());
    
    // Run for 220ms (should execute ~4 times)
    auto startTime = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - startTime < std::chrono::milliseconds(220))
    {
        scheduler_->runExpiredTasks();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    
    // Should have executed multiple times
    EXPECT_EQ(executionCount.load(), 4);
    
    // Cancel the task
    token.cancel();
    EXPECT_FALSE(token.valid());

    // Token will cancel itself when it goes out of scope.
    // While it does nothing, double-cancel is safe.
}

TEST_F(IntervalTaskSchedulerTests, ScheduleIntervalImmediateExecution)
{
    std::atomic<int> executionCount{0};
    std::atomic<bool> firstExecution{false};
    
    auto token = scheduler_->scheduleInterval(
        std::chrono::milliseconds(100),
        [&executionCount, &firstExecution]() -> Task<void> {
            if (executionCount.load() == 0)
            {
                firstExecution.store(true);
            }
            executionCount.fetch_add(1);
            co_return;
        }
    );
    
    // First execution should happen immediately
    scheduler_->runExpiredTasks();
    EXPECT_TRUE(firstExecution.load());
    EXPECT_EQ(executionCount.load(), 1);
}

// NOTE: You shouldn't be able to schedule an interval task with a return value.
// The task completes in the scheduler and the return value will be discarded.
// We need to change the scheduler to only allow tasks with void return type.
/*
TEST_F(IntervalTaskSchedulerTests, ScheduleIntervalWithReturnValue)
{
    std::atomic<int> executionCount{0};
    
    auto token = scheduler_->scheduleInterval(
        std::chrono::milliseconds(50),
        [&executionCount]() -> Task<int> {
            executionCount.fetch_add(1);
            co_return executionCount.load();
        }
    );
    
    // Run for 150ms
    auto startTime = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - startTime < std::chrono::milliseconds(150))
    {
        scheduler_->runExpiredTasks();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    
    EXPECT_GT(executionCount.load(), 2);
}
*/

//
// Delayed Task Tests
//

TEST_F(IntervalTaskSchedulerTests, ScheduleDelayedBasic)
{
    std::atomic<bool> executed{false};
    
    auto token = scheduler_->scheduleDelayed(
        std::chrono::milliseconds(100),
        [&executed]() -> Task<void> {
            executed.store(true);
            co_return;
        }
    );
    
    // Should not execute immediately
    scheduler_->runExpiredTasks();
    EXPECT_FALSE(executed.load());
    
    // Wait for delay
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    scheduler_->runExpiredTasks();
    EXPECT_TRUE(executed.load());
}

//
// Cancellation Token Tests
//

TEST_F(IntervalTaskSchedulerTests, CancellationTokenBasic)
{
    std::atomic<int> executionCount{0};
    
    auto token = scheduler_->scheduleInterval(
        std::chrono::milliseconds(50),
        [&executionCount]() -> Task<void> {
            executionCount.fetch_add(1);
            co_return;
        }
    );
    
    // Let it execute once
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    scheduler_->runExpiredTasks();
    int countAfterFirst = executionCount.load();
    EXPECT_GT(countAfterFirst, 0);
    
    // Cancel the task
    token.cancel();
    EXPECT_TRUE(token.isCancelled());
    
    // Wait and run again - should not execute more
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    scheduler_->runExpiredTasks();
    EXPECT_EQ(executionCount.load(), countAfterFirst);
}

TEST_F(IntervalTaskSchedulerTests, CancellationTokenDestruction)
{
    std::atomic<int> executionCount{0};
    
    {
        auto token = scheduler_->scheduleInterval(
            std::chrono::milliseconds(50),
            [&executionCount]() -> Task<void> {
                executionCount.fetch_add(1);
                co_return;
            }
        );
        
        // Let it execute once
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        scheduler_->runExpiredTasks();
        int countAfterFirst = executionCount.load();
        EXPECT_GT(countAfterFirst, 0);
        
        // Token goes out of scope - should cancel the task
    }
    
    // Wait and run again - should not execute more
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    scheduler_->runExpiredTasks();
    int finalCount = executionCount.load();
    EXPECT_EQ(finalCount, 1); // Should only have executed once
}

//
// Single Execution Guarantee Tests
//

TEST_F(IntervalTaskSchedulerTests, SingleExecutionGuarantee)
{
    std::atomic<int> executionCount{0};
    std::atomic<int> simultaneousExecutions{0};
    std::atomic<int> maxSimultaneousExecutions{0};
    
    auto token = scheduler_->scheduleInterval(
        std::chrono::milliseconds(50),
        [&executionCount, &simultaneousExecutions, &maxSimultaneousExecutions]() -> Task<void> {
            // Track simultaneous executions
            int current = simultaneousExecutions.fetch_add(1) + 1;
            int max = maxSimultaneousExecutions.load();
            while (current > max && !maxSimultaneousExecutions.compare_exchange_weak(max, current))
            {
                max = maxSimultaneousExecutions.load();
            }
            
            executionCount.fetch_add(1);
            
            // Simulate long-running task that takes longer than interval
            std::this_thread::sleep_for(std::chrono::milliseconds(75)); // Longer than 50ms interval
            
            simultaneousExecutions.fetch_sub(1);
            co_return;
        }
    );
    
    // Run for 300ms
    auto startTime = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - startTime < std::chrono::milliseconds(300))
    {
        scheduler_->runExpiredTasks();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    
    token.cancel();
    
    // Should never have more than 1 simultaneous execution
    EXPECT_EQ(maxSimultaneousExecutions.load(), 1);
    EXPECT_GT(executionCount.load(), 1);
}

//
// Suspending Coroutine Tests
//

TEST_F(IntervalTaskSchedulerTests, IntervalTaskWithSuspendingCoroutines)
{
    std::atomic<int> executionCount{0};
    
    auto token = scheduler_->scheduleInterval(
        std::chrono::milliseconds(100),
        [&executionCount]() -> Task<void> {
            executionCount.fetch_add(1);
            
            // Suspend on worker thread
            co_await []() -> AsyncTask<void> {
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
                co_return;
            }();
            
            // Should resume on main thread
            co_return;
        }
    );
    
    // Run for 400ms
    auto startTime = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - startTime < std::chrono::milliseconds(400))
    {
        scheduler_->runExpiredTasks();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    
    token.cancel();
    
    // Should have executed multiple times with proper suspension/resumption
    EXPECT_GT(executionCount.load(), 2);
}

//
// Multiple Interval Tasks Tests
//

TEST_F(IntervalTaskSchedulerTests, MultipleIntervalTasks)
{
    std::atomic<int> task1Count{0};
    std::atomic<int> task2Count{0};
    
    auto token1 = scheduler_->scheduleInterval(
        std::chrono::milliseconds(50),
        [&task1Count]() -> Task<void> {
            task1Count.fetch_add(1);
            co_return;
        }
    );
    
    auto token2 = scheduler_->scheduleInterval(
        std::chrono::milliseconds(75),
        [&task2Count]() -> Task<void> {
            task2Count.fetch_add(1);
            co_return;
        }
    );
    
    // Run for 300ms
    auto startTime = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - startTime < std::chrono::milliseconds(300))
    {
        scheduler_->runExpiredTasks();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    
    token1.cancel();
    token2.cancel();
    
    // Both tasks should have executed multiple times
    EXPECT_GT(task1Count.load(), 4);
    EXPECT_GT(task2Count.load(), 2);
}

//
// Error Handling Tests
//

TEST_F(IntervalTaskSchedulerTests, IntervalTaskWithException)
{
    std::atomic<int> executionCount{0};
    
    auto token = scheduler_->scheduleInterval(
        std::chrono::milliseconds(50),
        [&executionCount]() -> Task<void> {
            executionCount.fetch_add(1);
            if (executionCount.load() == 2)
            {
                throw std::runtime_error("Test exception");
            }
            co_return;
        }
    );
    
    // Run for 200ms
    auto startTime = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - startTime < std::chrono::milliseconds(200))
    {
        scheduler_->runExpiredTasks();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    
    token.cancel();
    
    // Should have executed at least once before exception
    EXPECT_GE(executionCount.load(), 1);
}

//
// Performance Tests
//

TEST_F(IntervalTaskSchedulerTests, HighFrequencyIntervalTasks)
{
    std::atomic<int> executionCount{0};
    
    auto token = scheduler_->scheduleInterval(
        std::chrono::milliseconds(10), // Very frequent
        [&executionCount]() -> Task<void> {
            executionCount.fetch_add(1);
            co_return;
        }
    );
    
    // Run for 100ms
    auto startTime = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - startTime < std::chrono::milliseconds(100))
    {
        scheduler_->runExpiredTasks();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    
    token.cancel();
    
    // Should have executed many times
    EXPECT_GT(executionCount.load(), 5);
}
