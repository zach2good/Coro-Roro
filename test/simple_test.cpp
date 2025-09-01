#include <corororo/coroutine/task.h>
#include <corororo/scheduler/scheduler.h>
#include <gtest/gtest.h>
#include <thread>

using namespace CoroRoro;

// Test fixture for basic scheduler functionality
class BasicSchedulerTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Create scheduler for each test
        scheduler_ = std::make_unique<Scheduler>();
    }

    void TearDown() override {
        // Cleanup happens automatically via unique_ptr
    }

    std::unique_ptr<Scheduler> scheduler_;
};

TEST_F(BasicSchedulerTest, SchedulerCreation) {
    ASSERT_NE(scheduler_, nullptr);
    EXPECT_TRUE(scheduler_->isRunning());
}

TEST_F(BasicSchedulerTest, WorkerThreadCount) {
    EXPECT_GE(scheduler_->getWorkerThreadCount(), 0);
    // Should have at least 1 less than hardware concurrency for main thread
    EXPECT_LE(scheduler_->getWorkerThreadCount(),
              static_cast<size_t>(std::thread::hardware_concurrency() - 1));
}

TEST_F(BasicSchedulerTest, MainThreadId) {
    auto mainThreadId = scheduler_->getMainThreadId();
    EXPECT_EQ(mainThreadId, std::this_thread::get_id());
}

TEST_F(BasicSchedulerTest, TypeCompilation) {
    // Test that basic coroutine types compile
    using TaskInt = Task<int>;
    using TaskVoid = Task<void>;
    using AsyncTaskInt = AsyncTask<int>;
    using AsyncTaskVoid = AsyncTask<void>;

    // These should compile without errors
    SUCCEED();
}

TEST_F(BasicSchedulerTest, RunExpiredTasks) {
    // Test that runExpiredTasks doesn't crash
    auto duration = scheduler_->runExpiredTasks();
    EXPECT_GE(duration.count(), 0);
}
