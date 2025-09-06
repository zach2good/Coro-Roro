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

        executionCount_            = 0;
        firstExecution_            = false;
        task1Count_                = 0;
        task2Count_                = 0;
        simultaneousExecutions_    = 0;
        maxSimultaneousExecutions_ = 0;
        executed_                  = false;
    }

    void TearDown() override
    {
        scheduler_.reset();
    }

    std::unique_ptr<Scheduler> scheduler_;

    std::atomic<size_t>      executionCount_{ 0 };
    std::atomic<bool>        firstExecution_{ false };
    std::atomic<size_t>      task1Count_{ 0 };
    std::atomic<size_t>      task2Count_{ 0 };
    std::atomic<size_t>      simultaneousExecutions_{ 0 };
    std::atomic<size_t>      maxSimultaneousExecutions_{ 0 };
    static std::atomic<bool> executed_;
};

// Initialize static member
std::atomic<bool> IntervalTaskSchedulerTests::executed_{ false };

//
// Interval Task Factory Tests
//

TEST_F(IntervalTaskSchedulerTests, ScheduleIntervalBasic)
{
    executionCount_.store(0);

    // Schedule an interval task that executes every 50ms
    auto token = scheduler_->scheduleInterval(
        std::chrono::milliseconds(50),
        [this]() -> Task<void>
        {
            executionCount_.fetch_add(1);
            co_return;
        });

    // Verify token is valid
    EXPECT_TRUE(token.valid());

    // Run for 220ms (should execute ~4 times)
    auto startTime = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - startTime < std::chrono::milliseconds(220))
    {
        scheduler_->runExpiredTasks();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    // Should have executed multiple times (allow for timing variations)
    EXPECT_GE(executionCount_.load(), 4);

    // Cancel the task
    token.cancel();
    EXPECT_FALSE(token.valid());

    // Token will cancel itself when it goes out of scope.
    // While it does nothing, double-cancel is safe.
}

TEST_F(IntervalTaskSchedulerTests, ScheduleIntervalImmediateExecution)
{
    executionCount_.store(0);
    firstExecution_.store(false);

    auto token = scheduler_->scheduleInterval(
        std::chrono::milliseconds(100),
        [this]() -> Task<void>
        {
            if (executionCount_.load() == 0)
            {
                firstExecution_.store(true);
            }
            executionCount_.fetch_add(1);
            co_return;
        });

    // First execution should happen immediately
    scheduler_->runExpiredTasks();
    EXPECT_TRUE(firstExecution_.load());
    EXPECT_EQ(executionCount_.load(), 1);
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
    executed_.store(false);

    auto token = scheduler_->scheduleDelayed(
        std::chrono::milliseconds(100),
        [this]() -> Task<void>
        {
            executed_.store(true);
            co_return;
        });

    // Should not execute immediately
    scheduler_->runExpiredTasks();
    EXPECT_FALSE(executed_.load());

    // Wait for delay
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    scheduler_->runExpiredTasks();
    EXPECT_TRUE(executed_.load());
}

TEST_F(IntervalTaskSchedulerTests, ScheduleAtBasic)
{
    executed_.store(false);

    // Schedule at a specific time point (now + 100ms)
    auto executionTime = std::chrono::steady_clock::now() + std::chrono::milliseconds(100);

    auto token = scheduler_->scheduleAt(
        executionTime,
        [this]() -> Task<void>
        {
            executed_.store(true);
            co_return;
        });

    // Should not execute immediately
    scheduler_->runExpiredTasks();
    EXPECT_FALSE(executed_.load());

    // Wait for the scheduled time
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    scheduler_->runExpiredTasks();
    EXPECT_TRUE(executed_.load());
}

//
// Cancellation Token Tests
//

TEST_F(IntervalTaskSchedulerTests, CancellationTokenConstruction)
{
    executionCount_.store(0);

    // A cancellation token should be default-constructed as invalid
    // You should then be able to overwrite it with a valid token
    // from the scheduler
    CancellationToken token;
    EXPECT_FALSE(token.valid());

    token = scheduler_->scheduleInterval(
        std::chrono::milliseconds(50),
        [this]() -> Task<void>
        {
            executionCount_.fetch_add(1);
            co_return;
        });
    EXPECT_TRUE(token.valid());

    // Let it execute once
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    scheduler_->runExpiredTasks();
    size_t countAfterFirst = executionCount_.load();
    EXPECT_GT(countAfterFirst, 0);

    // Cancel the task
    token.cancel();
    EXPECT_FALSE(token.valid());
}

TEST_F(IntervalTaskSchedulerTests, CancellationTokenBasic)
{
    executionCount_.store(0);

    auto token = scheduler_->scheduleInterval(
        std::chrono::milliseconds(50),
        [this]() -> Task<void>
        {
            executionCount_.fetch_add(1);
            co_return;
        });

    // Let it execute once
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    scheduler_->runExpiredTasks();
    size_t countAfterFirst = executionCount_.load();
    EXPECT_GT(countAfterFirst, 0);

    // Cancel the task
    token.cancel();
    EXPECT_FALSE(token.valid());

    // Wait and run again - should not execute more
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    scheduler_->runExpiredTasks();
    EXPECT_EQ(executionCount_.load(), countAfterFirst);
}

TEST_F(IntervalTaskSchedulerTests, CancellationTokenDestruction)
{
    executionCount_.store(0);
    size_t countAfterFirst = 0;

    {
        auto token = scheduler_->scheduleInterval(
            std::chrono::milliseconds(50),
            [this]() -> Task<void>
            {
                executionCount_.fetch_add(1);
                co_return;
            });

        // Let it execute a few times
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        scheduler_->runExpiredTasks();
        countAfterFirst = executionCount_.load();
        EXPECT_GT(countAfterFirst, 0);

        // Token goes out of scope - should cancel the task
    }

    // Wait and run again - should not execute more
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    scheduler_->runExpiredTasks();
    size_t finalCount = executionCount_.load();
    EXPECT_EQ(finalCount, countAfterFirst); // Should not have executed more after token destruction
}

//
// Single Execution Guarantee Tests
//

TEST_F(IntervalTaskSchedulerTests, SingleExecutionGuarantee)
{
    executionCount_.store(0);
    simultaneousExecutions_.store(0);
    maxSimultaneousExecutions_.store(0);

    auto token = scheduler_->scheduleInterval(
        std::chrono::milliseconds(50),
        [this]() -> Task<void>
        {
            // Track simultaneous executions
            size_t current = simultaneousExecutions_.fetch_add(1) + 1;
            size_t max     = maxSimultaneousExecutions_.load();
            while (current > max && !maxSimultaneousExecutions_.compare_exchange_weak(max, current))
            {
                max = maxSimultaneousExecutions_.load();
            }

            executionCount_.fetch_add(1);

            // Simulate long-running task that takes longer than interval
            std::this_thread::sleep_for(std::chrono::milliseconds(75)); // Longer than 50ms interval

            simultaneousExecutions_.fetch_sub(1);
            co_return;
        });

    // Run for 300ms
    auto startTime = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - startTime < std::chrono::milliseconds(300))
    {
        scheduler_->runExpiredTasks();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    token.cancel();

    // Should never have more than 1 simultaneous execution
    EXPECT_EQ(maxSimultaneousExecutions_.load(), 1);
    EXPECT_GT(executionCount_.load(), 1);
}

//
// Suspending Coroutine Tests
//

TEST_F(IntervalTaskSchedulerTests, IntervalTaskWithSuspendingCoroutines)
{
    executionCount_.store(0);

    auto token = scheduler_->scheduleInterval(
        std::chrono::milliseconds(100),
        [this]() -> Task<void>
        {
            executionCount_.fetch_add(1);

            // Suspend on worker thread
            co_await []() -> AsyncTask<void>
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
                co_return;
            }();

            // Should resume on main thread
            co_return;
        });

    // Run for 400ms
    auto startTime = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - startTime < std::chrono::milliseconds(400))
    {
        scheduler_->runExpiredTasks();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    token.cancel();

    // Should have executed multiple times with proper suspension/resumption
    EXPECT_GT(executionCount_.load(), 2);
}

//
// Multiple Interval Tasks Tests
//

// Helper function objects to avoid lambda capture issues
class Task1Functor
{
public:
    Task1Functor(std::atomic<size_t>* count)
    : count_(count)
    {
    }

    Task<void> operator()() const
    {
        count_->fetch_add(1);
        co_return;
    }

private:
    std::atomic<size_t>* count_;
};

class Task2Functor
{
public:
    Task2Functor(std::atomic<size_t>* count)
    : count_(count)
    {
    }

    Task<void> operator()() const
    {
        count_->fetch_add(1);
        co_return;
    }

private:
    std::atomic<size_t>* count_;
};

TEST_F(IntervalTaskSchedulerTests, MultipleIntervalTasks)
{
    task1Count_.store(0);
    task2Count_.store(0);

    // Create separate function objects
    Task1Functor task1_func(&task1Count_);
    Task2Functor task2_func(&task2Count_);

    auto token1 = scheduler_->scheduleInterval(
        std::chrono::milliseconds(100),
        std::move(task1_func));

    auto token2 = scheduler_->scheduleInterval(
        std::chrono::milliseconds(150),
        std::move(task2_func));

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
    // Task 1 (100ms interval): ~3 executions in 300ms
    // Task 2 (150ms interval): ~2 executions in 300ms
    EXPECT_GE(task1Count_.load(), 2);
    EXPECT_GE(task2Count_.load(), 1);
    EXPECT_GE(task1Count_.load() + task2Count_.load(), 4);
}

TEST_F(IntervalTaskSchedulerTests, MultipleIntervalTasksDifferentIntervals)
{
    task1Count_.store(0);
    task2Count_.store(0);

    // Test the user's specific scenario: 2400ms and 1-minute intervals
    auto token1 = scheduler_->scheduleInterval(
        std::chrono::milliseconds(2400), // 2.4 seconds
        [this]() -> Task<void>
        {
            task1Count_.fetch_add(1);
            co_return;
        });

    auto token2 = scheduler_->scheduleInterval(
        std::chrono::minutes(1), // 60 seconds
        [this]() -> Task<void>
        {
            task2Count_.fetch_add(1);
            co_return;
        });

    // Run for 5 seconds - this should allow the 2400ms task to execute ~2 times
    // The 1-minute task should execute once (immediately)
    auto startTime = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - startTime < std::chrono::seconds(5))
    {
        scheduler_->runExpiredTasks();
        std::this_thread::sleep_for(std::chrono::milliseconds(200)); // User's calling pattern
    }

    token1.cancel();
    token2.cancel();

    // The 2400ms task should execute at least once (immediately) and potentially twice
    EXPECT_GE(task1Count_.load(), 1);

    // The 1-minute task should execute once (immediately, then not again for 60 seconds)
    EXPECT_EQ(task2Count_.load(), 1);
}

TEST_F(IntervalTaskSchedulerTests, MultipleIntervalTasksImmediateExecution)
{
    task1Count_.store(0);
    task2Count_.store(0);

    // Test that both tasks execute immediately when scheduled with the same timing
    auto token1 = scheduler_->scheduleInterval(
        std::chrono::milliseconds(1000),
        [this]() -> Task<void>
        {
            task1Count_.fetch_add(1);
            co_return;
        });

    auto token2 = scheduler_->scheduleInterval(
        std::chrono::milliseconds(2000),
        [this]() -> Task<void>
        {
            task2Count_.fetch_add(1);
            co_return;
        });

    // Call runExpiredTasks once - both should execute immediately
    scheduler_->runExpiredTasks();

    token1.cancel();
    token2.cancel();

    // Both tasks should have executed once immediately
    EXPECT_EQ(task1Count_.load(), 1);
    EXPECT_EQ(task2Count_.load(), 1);
}

//
// Error Handling Tests
//

TEST_F(IntervalTaskSchedulerTests, IntervalTaskWithException)
{
    executionCount_.store(0);

    auto token = scheduler_->scheduleInterval(
        std::chrono::milliseconds(50),
        [this]() -> Task<void>
        {
            executionCount_.fetch_add(1);
            if (executionCount_.load() == 2)
            {
                throw std::runtime_error("Test exception");
            }
            co_return;
        });

    // Run for 200ms
    auto startTime = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - startTime < std::chrono::milliseconds(200))
    {
        scheduler_->runExpiredTasks();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    token.cancel();

    // Should have executed at least once before exception
    EXPECT_GE(executionCount_.load(), 1);
}

//
// Performance Tests
//

TEST_F(IntervalTaskSchedulerTests, HighFrequencyIntervalTasks)
{
    executionCount_.store(0);

    auto token = scheduler_->scheduleInterval(
        std::chrono::milliseconds(10), // Very frequent
        [this]() -> Task<void>
        {
            executionCount_.fetch_add(1);
            co_return;
        });

    // Run for 100ms
    auto startTime = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - startTime < std::chrono::milliseconds(100))
    {
        scheduler_->runExpiredTasks();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    token.cancel();

    // Should have executed many times
    EXPECT_GT(executionCount_.load(), 5);
}
