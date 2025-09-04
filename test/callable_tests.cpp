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
    }

    void TearDown() override
    {
        scheduler_.reset();
    }

    std::unique_ptr<Scheduler> scheduler_;
};

TEST_F(CallableTaskSchedulerTests, ScheduleWithLambda)
{
    std::atomic<int> executionCount{ 0 };

    // Schedule a lambda that returns a Task
    scheduler_->schedule([&executionCount]() -> Task<void>
                         {
        executionCount.fetch_add(1);
        co_return; });

    scheduler_->runExpiredTasks();
    EXPECT_EQ(executionCount.load(), 1);
}

TEST_F(CallableTaskSchedulerTests, ScheduleWithDirectLambda)
{
    std::atomic<int> executionCount{ 0 };

    // Create a lambda that returns a Task (not immediately invoked)
    auto taskLambda = [&executionCount]() -> Task<void>
    {
        executionCount.fetch_add(1);
        co_return;
    };

    // Pass the lambda directly to schedule (not invoking it)
    scheduler_->schedule(taskLambda);

    scheduler_->runExpiredTasks();
    EXPECT_EQ(executionCount.load(), 1);
}

TEST_F(CallableTaskSchedulerTests, ScheduleWithStdBind)
{
    std::atomic<int> executionCount{ 0 };

    // Create a function that takes parameters
    auto taskFunction = [](std::atomic<int>& count, int multiplier) -> Task<void>
    {
        count.fetch_add(multiplier);
        co_return;
    };

    // Use std::bind to create a callable
    auto boundFunction = std::bind(taskFunction, std::ref(executionCount), 3);

    scheduler_->schedule(boundFunction);
    scheduler_->runExpiredTasks();
    EXPECT_EQ(executionCount.load(), 3);
}

TEST_F(CallableTaskSchedulerTests, ScheduleIntervalWithLambda)
{
    std::atomic<int> executionCount{ 0 };

    // Schedule interval task with lambda
    auto token = scheduler_->scheduleInterval(
        std::chrono::milliseconds(50),
        [&executionCount]() -> Task<void>
        {
            executionCount.fetch_add(1);
            co_return;
        });

    // Run for 150ms
    auto startTime = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - startTime < std::chrono::milliseconds(150))
    {
        scheduler_->runExpiredTasks();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    token.cancel();
    EXPECT_GT(executionCount.load(), 2);
}

TEST_F(CallableTaskSchedulerTests, ScheduleIntervalWithDirectLambda)
{
    std::atomic<int> executionCount{ 0 };

    // Create a lambda that returns a Task (not immediately invoked)
    auto taskLambda = [&executionCount]() -> Task<void>
    {
        executionCount.fetch_add(1);
        co_return;
    };

    // Pass the lambda directly to scheduleInterval (not invoking it)
    auto token = scheduler_->scheduleInterval(
        std::chrono::milliseconds(50),
        taskLambda);

    // Run for 150ms
    auto startTime = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - startTime < std::chrono::milliseconds(150))
    {
        scheduler_->runExpiredTasks();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    token.cancel();
    EXPECT_GT(executionCount.load(), 2);
}

TEST_F(CallableTaskSchedulerTests, ScheduleIntervalWithStdBind)
{
    std::atomic<int> executionCount{ 0 };

    // Create a function that takes parameters
    auto taskFunction = [](std::atomic<int>& count, int increment) -> Task<void>
    {
        count.fetch_add(increment);
        co_return;
    };

    // Use std::bind to create a callable for interval task
    auto boundFunction = std::bind(taskFunction, std::ref(executionCount), 2);

    auto token = scheduler_->scheduleInterval(
        std::chrono::milliseconds(50),
        boundFunction);

    // Run for 150ms
    auto startTime = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - startTime < std::chrono::milliseconds(150))
    {
        scheduler_->runExpiredTasks();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    token.cancel();
    EXPECT_GT(executionCount.load(), 4); // Should be at least 4 (2 * 2+ executions)
}

TEST_F(CallableTaskSchedulerTests, ScheduleDelayedWithFunctionObject)
{
    std::atomic<int> executionCount{ 0 };

    // Create a function object
    struct TaskFunctor
    {
        std::atomic<int>& count_;

        TaskFunctor(std::atomic<int>& count)
        : count_(count)
        {
        }

        Task<void> operator()() const
        {
            count_.fetch_add(1);
            co_return;
        }
    };

    TaskFunctor functor(executionCount);

    auto token = scheduler_->scheduleDelayed(
        std::chrono::milliseconds(50),
        functor);

    // Should not execute immediately
    scheduler_->runExpiredTasks();
    EXPECT_EQ(executionCount.load(), 0);

    // Wait for delay
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    scheduler_->runExpiredTasks();
    EXPECT_EQ(executionCount.load(), 1);
}
