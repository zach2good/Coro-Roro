#include <atomic>
#include <thread>

#include <corororo/corororo.h>
using namespace CoroRoro;

#include <gtest/gtest.h>

class BasicSchedulerTests : public ::testing::Test
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

TEST_F(BasicSchedulerTests, SchedulerCreation)
{
    ASSERT_NE(scheduler_, nullptr);
}

TEST_F(BasicSchedulerTests, RunExpiredTasksEmptyQueues)
{
    const auto duration = scheduler_->runExpiredTasks();
    EXPECT_GE(duration.count(), 0);
}

TEST_F(BasicSchedulerTests, BasicTaskCreation)
{
    auto task = []() -> Task<void>
    {
        co_return;
    }();

    SUCCEED();
}

TEST_F(BasicSchedulerTests, BasicTaskExecution)
{
    const auto mainThreadId = std::this_thread::get_id();

    std::atomic<bool> taskExecuted{ false };

    auto task = [&]() -> Task<void>
    {
        EXPECT_EQ(std::this_thread::get_id(), mainThreadId);
        taskExecuted = true;
        co_return;
    }();

    scheduler_->schedule(std::move(task));

    const auto duration = scheduler_->runExpiredTasks();
    EXPECT_GE(duration.count(), 0);

    EXPECT_TRUE(taskExecuted);
}

TEST_F(BasicSchedulerTests, BasicAsyncTaskExecution)
{
    const auto mainThreadId = std::this_thread::get_id();

    std::atomic<bool> taskExecuted{ false };

    auto task = [&]() -> AsyncTask<void>
    {
        EXPECT_NE(std::this_thread::get_id(), mainThreadId);
        taskExecuted = true;
        co_return;
    }();

    scheduler_->schedule(std::move(task));

    const auto duration = scheduler_->runExpiredTasks();
    EXPECT_GE(duration.count(), 0);

    EXPECT_TRUE(taskExecuted);
}

TEST_F(BasicSchedulerTests, BasicNestedTasksExecution)
{
    const auto mainThreadId = std::this_thread::get_id();

    std::atomic<bool> taskExecuted{ false };

    auto innerTask = [&]() -> AsyncTask<void>
    {
        EXPECT_NE(std::this_thread::get_id(), mainThreadId);
        taskExecuted = true;
        co_return;
    };

    auto task = [&]() -> Task<void>
    {
        EXPECT_EQ(std::this_thread::get_id(), mainThreadId);
        co_await innerTask();
        co_return;
    }();

    scheduler_->schedule(std::move(task));

    const auto duration = scheduler_->runExpiredTasks();
    EXPECT_GE(duration.count(), 0);

    EXPECT_TRUE(taskExecuted);
}

TEST_F(BasicSchedulerTests, BasicAlternatingNestedTasksExecution)
{
    const auto mainThreadId = std::this_thread::get_id();

    std::vector<std::thread::id> taskThreadOrder{};

    auto innerTask = [&]() -> AsyncTask<void>
    {
        taskThreadOrder.push_back(std::this_thread::get_id());
        co_return;
    };

    auto task = [&]() -> Task<void>
    {
        taskThreadOrder.push_back(std::this_thread::get_id());
        co_await innerTask();
        taskThreadOrder.push_back(std::this_thread::get_id());
        co_await innerTask();
        taskThreadOrder.push_back(std::this_thread::get_id());
        co_await innerTask();
        taskThreadOrder.push_back(std::this_thread::get_id());
        co_return;
    }();

    scheduler_->schedule(std::move(task));

    const auto duration = scheduler_->runExpiredTasks();
    EXPECT_GE(duration.count(), 0);

    EXPECT_EQ(taskThreadOrder.size(), 7);
    EXPECT_EQ(taskThreadOrder[0], mainThreadId);
    EXPECT_NE(taskThreadOrder[1], mainThreadId);
    EXPECT_EQ(taskThreadOrder[2], mainThreadId);
    EXPECT_NE(taskThreadOrder[3], mainThreadId);
    EXPECT_EQ(taskThreadOrder[4], mainThreadId);
    EXPECT_NE(taskThreadOrder[5], mainThreadId);
    EXPECT_EQ(taskThreadOrder[6], mainThreadId);
}

TEST_F(BasicSchedulerTests, RunBasicCoroutineWithoutScheduler)
{
    std::atomic<bool> taskExecuted{ false };

    auto task = [&]() -> Task<void>
    {
        taskExecuted = true;
        co_return;
    };

    // Will block and run coroutine inline to completion.
    // Does not require a scheduler.
    // Completely ignores affinity.
    runCoroutineInline(task());

    EXPECT_TRUE(taskExecuted);
}

TEST_F(BasicSchedulerTests, RunSimpleCoroutineChainWithoutScheduler)
{
    std::atomic<size_t> tasksExecuted{ 0 };

    auto innerTask1 = [&]() -> Task<void>
    {
        tasksExecuted.fetch_add(1, std::memory_order_relaxed);
        co_return;
    };

    auto innerTask2 = [&]() -> Task<void>
    {
        tasksExecuted.fetch_add(1, std::memory_order_relaxed);
        co_await innerTask1();
        co_return;
    };

    auto innerTask3 = [&]() -> Task<void>
    {
        tasksExecuted.fetch_add(1, std::memory_order_relaxed);
        co_await innerTask2();
        co_return;
    };

    auto task = [&]() -> Task<void>
    {
        tasksExecuted.fetch_add(1, std::memory_order_relaxed);
        co_await innerTask3();
        co_return;
    };

    // Will block and run coroutine inline to completion.
    // Does not require a scheduler.
    // Completely ignores affinity.
    runCoroutineInline(task());

    EXPECT_EQ(tasksExecuted.load(), 4);
}

TEST_F(BasicSchedulerTests, RunCoroutineWithReturnValue)
{
    // Test coroutine that returns an integer
    auto intTask = []() -> Task<int>
    {
        co_return 42;
    };

    // Run the coroutine and get the result
    int result = runCoroutineInline(intTask());

    EXPECT_EQ(result, 42);
}

TEST_F(BasicSchedulerTests, RunCoroutineWithStringReturnValue)
{
    // Test coroutine that returns a string
    auto stringTask = []() -> Task<std::string>
    {
        co_return std::string("Hello, World!");
    };

    // Run the coroutine and get the result
    std::string result = runCoroutineInline(stringTask());

    EXPECT_EQ(result, "Hello, World!");
}

TEST_F(BasicSchedulerTests, RunCoroutineWithComplexReturnValue)
{
    // Test coroutine that returns a complex object
    auto complexTask = []() -> Task<std::vector<int>>
    {
        std::vector<int> data = { 1, 2, 3, 4, 5 };
        co_return data;
    };

    // Run the coroutine and get the result
    std::vector<int> result = runCoroutineInline(complexTask());

    EXPECT_EQ(result.size(), 5);
    EXPECT_EQ(result[0], 1);
    EXPECT_EQ(result[4], 5);
}
