#include <atomic>
#include <thread>

#include <corororo/corororo.h>
using namespace CoroRoro;

#include <gtest/gtest.h>

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

TEST_F(BasicSchedulerTest, RunExpiredTasksEmptyQueues)
{
    const auto duration = scheduler_->runExpiredTasks();
    EXPECT_GE(duration.count(), 0);
}

TEST_F(BasicSchedulerTest, BasicTaskCreation)
{
    auto task = []() -> Task<void> {
        co_return;
    }();
    
    SUCCEED();
}

TEST_F(BasicSchedulerTest, BasicTaskExecution)
{
    // For now, just test that we can create and schedule a task
    // The actual execution will be tested once we have proper scheduling working
    auto task = []() -> Task<void> {
        co_return;
    }();
    
    // Schedule the task - this should not crash
    scheduler_->schedule(std::move(task));

    // Run the scheduler - this should not crash
    const auto duration = scheduler_->runExpiredTasks();
    EXPECT_GE(duration.count(), 0);

    // For now, just test that we got here without crashing
    SUCCEED();
}
