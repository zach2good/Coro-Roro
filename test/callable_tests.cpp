#include <atomic>
#include <chrono>
#include <thread>

#include <corororo/corororo.h>
using namespace CoroRoro;

#include <gtest/gtest.h>

class CallableTaskSchedulerTests : public ::testing::Test
{
protected:
    void SetUp() override
    {
        scheduler_ = std::make_unique<Scheduler>(4);

        executionCount_ = 0;
    }

    void TearDown() override
    {
        scheduler_.reset();
    }

    std::unique_ptr<Scheduler> scheduler_;

    static std::atomic<size_t> executionCount_;
};

// Initialize static member
std::atomic<size_t> CallableTaskSchedulerTests::executionCount_{ 0 };

TEST_F(CallableTaskSchedulerTests, ScheduleWithLambda)
{
    executionCount_.store(0);

    scheduler_->schedule(
        []() -> Task<void>
        {
            executionCount_.fetch_add(1);
            co_return;
        });
    scheduler_->runExpiredTasks();

    EXPECT_EQ(executionCount_.load(), 1);
}

TEST_F(CallableTaskSchedulerTests, ScheduleWithDirectLambda)
{
    executionCount_.store(0);

    auto taskLambda = []() -> Task<void>
    {
        executionCount_.fetch_add(1);
        co_return;
    };

    // Pass the lambda directly to schedule (not invoking it)
    scheduler_->schedule(taskLambda);
    scheduler_->runExpiredTasks();

    EXPECT_EQ(executionCount_.load(), 1);
}

TEST_F(CallableTaskSchedulerTests, ScheduleWithStdBind)
{
    executionCount_.store(0);

    // Create a function that takes parameters
    auto taskFunction = [](std::atomic<size_t>& count, size_t multiplier) -> Task<void>
    {
        count.fetch_add(multiplier);
        co_return;
    };

    // Use std::bind to create a callable
    auto boundFunction = std::bind(taskFunction, std::ref(executionCount_), 3);

    scheduler_->schedule(boundFunction);
    scheduler_->runExpiredTasks();

    EXPECT_EQ(executionCount_.load(), 3); // Single execution with multiplier of 3
}

TEST_F(CallableTaskSchedulerTests, ScheduleWithVoidLambda)
{
    // Use a static variable to test void lambda functionality
    // Note: void lambdas cannot safely capture 'this' or member variables
    // due to execution context differences in the coroutine wrapper
    static std::atomic<size_t> staticCount{ 0 };
    staticCount.store(0);

    // Schedule a lambda that returns void (non-coroutine)
    scheduler_->schedule(
        []() -> void
        {
            staticCount.fetch_add(1);
        });
    scheduler_->runExpiredTasks();

    EXPECT_EQ(staticCount.load(), 1);
}

TEST_F(CallableTaskSchedulerTests, ScheduleIntervalWithLambda)
{
    executionCount_.store(0);

    // Schedule interval task with lambda
    auto token = scheduler_->scheduleInterval(
        std::chrono::milliseconds(50),
        []() -> Task<void>
        {
            executionCount_.fetch_add(1);
            co_return;
        });

    // Run for 150ms
    auto startTime = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - startTime < std::chrono::milliseconds(170))
    {
        scheduler_->runExpiredTasks();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    token.cancel();
    EXPECT_EQ(executionCount_.load(), 4); // 0ms, 50ms, 100ms, 150ms = 4 executions in 170ms
}

TEST_F(CallableTaskSchedulerTests, ScheduleIntervalWithDirectLambda)
{
    executionCount_.store(0);

    // Create a lambda that returns a Task (not immediately invoked)
    auto taskLambda = []() -> Task<void>
    {
        executionCount_.fetch_add(1);
        co_return;
    };

    // Pass the lambda directly to scheduleInterval (not invoking it)
    auto token = scheduler_->scheduleInterval(
        std::chrono::milliseconds(50),
        taskLambda);

    // Run for 150ms
    auto startTime = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - startTime < std::chrono::milliseconds(170))
    {
        scheduler_->runExpiredTasks();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    token.cancel();
    EXPECT_EQ(executionCount_.load(), 4); // 0ms, 50ms, 100ms, 150ms = 4 executions in 170ms
}

TEST_F(CallableTaskSchedulerTests, ScheduleIntervalWithStdBind)
{
    executionCount_.store(0);

    // Create a function that takes parameters
    auto taskFunction = [](std::atomic<size_t>& count, size_t increment) -> Task<void>
    {
        count.fetch_add(increment);
        co_return;
    };

    // Use std::bind to create a callable for interval task
    auto boundFunction = std::bind(taskFunction, std::ref(executionCount_), 2);

    auto token = scheduler_->scheduleInterval(
        std::chrono::milliseconds(50),
        boundFunction);

    // Run for 150ms
    auto startTime = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - startTime < std::chrono::milliseconds(170))
    {
        scheduler_->runExpiredTasks();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    token.cancel();
    EXPECT_EQ(executionCount_.load(), 8); // 4 executions * 2 increment = 8 total count
}

TEST_F(CallableTaskSchedulerTests, ScheduleDelayedWithFunctionObject)
{
    executionCount_.store(0);

    auto token = scheduler_->scheduleDelayed(
        std::chrono::milliseconds(50),
        []() -> Task<void>
        {
            executionCount_.fetch_add(1);
            co_return;
        });

    // Should not execute immediately
    scheduler_->runExpiredTasks();
    EXPECT_EQ(executionCount_.load(), 0);

    // Wait for delay
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    scheduler_->runExpiredTasks();
    EXPECT_EQ(executionCount_.load(), 1);
}
