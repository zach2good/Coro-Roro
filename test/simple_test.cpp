#include <atomic>
#include <corororo/coroutine/task.h>
#include <corororo/scheduler/scheduler.h>
#include <gtest/gtest.h>
#include <thread>

using namespace CoroRoro;

class BasicSchedulerTest : public ::testing::Test
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

TEST_F(BasicSchedulerTest, SchedulerCreation)
{
    ASSERT_NE(scheduler_, nullptr);
}

TEST_F(BasicSchedulerTest, MainThreadId)
{
    const auto mainThreadId = scheduler_->getMainThreadId();
    EXPECT_EQ(mainThreadId, std::this_thread::get_id());
}

TEST_F(BasicSchedulerTest, RunExpiredTasksEmpty)
{
    // Test that runExpiredTasks doesn't crash
    const auto duration = scheduler_->runExpiredTasks();
    EXPECT_GE(duration.count(), 0);
}

TEST_F(BasicSchedulerTest, BasicTaskCreation)
{
    std::atomic<bool> taskExecuted{ false };

    const auto simpleTask = [&]() -> Task<void>
    {
        taskExecuted = true;
        co_return;
    };

    scheduler_->schedule(simpleTask());

    EXPECT_TRUE(taskExecuted);
}
