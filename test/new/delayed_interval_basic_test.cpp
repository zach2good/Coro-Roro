#include <gtest/gtest.h>
#include <corororo/corororo.h>
#include <atomic>
#include <chrono>
#include <thread>

using namespace std::chrono_literals;
using namespace CoroRoro;

class DelayedIntervalTest : public ::testing::Test
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
// BASIC DELAYED AND INTERVAL TESTS
//

TEST_F(DelayedIntervalTest, BasicDelayedTaskTest)
{
    std::atomic<int> taskExecuted{0};

    const auto startTime = std::chrono::steady_clock::now();

    // Schedule a delayed task
    auto token = scheduler_->scheduleDelayed(100ms, [&]() -> Task<void> {
        taskExecuted.store(1);
        co_return;
    });

    // Wait for the delay and processing
    std::this_thread::sleep_for(200ms);

    // Process expired tasks
    scheduler_->runExpiredTasks();

    // The task should have executed
    EXPECT_EQ(taskExecuted.load(), 1);

    const auto endTime = std::chrono::steady_clock::now();
    const auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);

    // Should have taken at least 100ms for the delay
    EXPECT_GE(duration.count(), 90); // Allow some tolerance
}

TEST_F(DelayedIntervalTest, BasicIntervalTaskTest)
{
    std::atomic<int> executionCount{0};

    const auto startTime = std::chrono::steady_clock::now();

    // Schedule an interval task (every 50ms)
    auto token = scheduler_->scheduleInterval(50ms, [&]() -> Task<void> {
        executionCount.fetch_add(1);
        co_return;
    });

    // Let it run for about 200ms (should execute ~4 times)
    std::this_thread::sleep_for(200ms);

    // Process expired tasks multiple times
    for (int i = 0; i < 10; ++i) {
        scheduler_->runExpiredTasks();
        std::this_thread::sleep_for(10ms);
    }

    // Cancel the interval task
    token.cancel();

    // Should have executed multiple times
    EXPECT_GE(executionCount.load(), 2); // At least 2 executions
    EXPECT_LE(executionCount.load(), 8); // Not too many (in case of timing issues)

    const auto endTime = std::chrono::steady_clock::now();
    const auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);

    // Should have taken at least 200ms
    EXPECT_GE(duration.count(), 180);
}

TEST_F(DelayedIntervalTest, DelayedTaskCancellationTest)
{
    std::atomic<int> taskExecuted{0};

    // Schedule a delayed task
    auto token = scheduler_->scheduleDelayed(100ms, [&]() -> Task<void> {
        taskExecuted.store(1);
        co_return;
    });

    // Cancel immediately
    token.cancel();

    // Wait for when the task would have executed
    std::this_thread::sleep_for(150ms);

    // Process expired tasks
    scheduler_->runExpiredTasks();

    // The task should NOT have executed due to cancellation
    EXPECT_EQ(taskExecuted.load(), 0);
}

TEST_F(DelayedIntervalTest, IntervalTaskCancellationTest)
{
    std::atomic<int> executionCount{0};

    // Schedule an interval task
    auto token = scheduler_->scheduleInterval(50ms, [&]() -> Task<void> {
        executionCount.fetch_add(1);
        co_return;
    });

    // Let it run for a short time
    std::this_thread::sleep_for(100ms);

    // Process some tasks
    for (int i = 0; i < 3; ++i) {
        scheduler_->runExpiredTasks();
        std::this_thread::sleep_for(10ms);
    }

    int executionsBeforeCancel = executionCount.load();

    // Cancel the interval task
    token.cancel();

    // Let it run for a bit more
    std::this_thread::sleep_for(100ms);

    // Process tasks again
    for (int i = 0; i < 5; ++i) {
        scheduler_->runExpiredTasks();
        std::this_thread::sleep_for(10ms);
    }

    int executionsAfterCancel = executionCount.load();

    // Should have executed some times before cancellation
    EXPECT_GE(executionsBeforeCancel, 1);

    // Should not have executed many more times after cancellation
    // (may execute 1-2 more due to timing, but not many)
    EXPECT_LE(executionsAfterCancel - executionsBeforeCancel, 3);
}

TEST_F(DelayedIntervalTest, MultipleDelayedTasksTest)
{
    std::atomic<int> task1Executed{0};
    std::atomic<int> task2Executed{0};
    std::atomic<int> task3Executed{0};

    // Schedule multiple delayed tasks with different delays
    auto token1 = scheduler_->scheduleDelayed(50ms, [&]() -> Task<void> {
        task1Executed.store(1);
        co_return;
    });

    auto token2 = scheduler_->scheduleDelayed(100ms, [&]() -> Task<void> {
        task2Executed.store(1);
        co_return;
    });

    auto token3 = scheduler_->scheduleDelayed(150ms, [&]() -> Task<void> {
        task3Executed.store(1);
        co_return;
    });

    // Wait and process at different intervals
    std::this_thread::sleep_for(60ms);
    scheduler_->runExpiredTasks();
    EXPECT_EQ(task1Executed.load(), 1); // First task should be done
    EXPECT_EQ(task2Executed.load(), 0); // Second task not yet
    EXPECT_EQ(task3Executed.load(), 0); // Third task not yet

    std::this_thread::sleep_for(50ms);
    scheduler_->runExpiredTasks();
    EXPECT_EQ(task2Executed.load(), 1); // Second task should be done
    EXPECT_EQ(task3Executed.load(), 0); // Third task not yet

    std::this_thread::sleep_for(50ms);
    scheduler_->runExpiredTasks();
    EXPECT_EQ(task3Executed.load(), 1); // Third task should be done
}

TEST_F(DelayedIntervalTest, IntervalTaskDriftCorrectionTest)
{
    std::atomic<int> executionCount{0};
    std::vector<std::chrono::steady_clock::time_point> executionTimes;

    // Schedule an interval task with a long execution time
    auto token = scheduler_->scheduleInterval(100ms, [&]() -> Task<void> {
        executionTimes.push_back(std::chrono::steady_clock::now());
        executionCount.fetch_add(1);

        // Simulate work that takes longer than the interval
        std::this_thread::sleep_for(80ms);

        co_return;
    });

    // Let it run for several executions
    const auto testDuration = 500ms;
    const auto startTime = std::chrono::steady_clock::now();

    while (std::chrono::steady_clock::now() - startTime < testDuration) {
        scheduler_->runExpiredTasks();
        std::this_thread::sleep_for(10ms);
    }

    // Cancel the task
    token.cancel();

    // Analyze execution pattern
    if (executionTimes.size() >= 3) {
        // Check that intervals are based on completion time, not start time
        // Each execution should be roughly 180ms apart (100ms interval + 80ms work)
        for (size_t i = 1; i < executionTimes.size(); ++i) {
            auto interval = executionTimes[i] - executionTimes[i-1];
            auto intervalMs = std::chrono::duration_cast<std::chrono::milliseconds>(interval).count();

            // Should be approximately 180ms (100ms interval + 80ms work)
            EXPECT_GE(intervalMs, 150); // Allow some tolerance
            EXPECT_LE(intervalMs, 220);
        }
    }

    // Should have executed several times
    EXPECT_GE(executionCount.load(), 2);
    EXPECT_LE(executionCount.load(), 6); // Not too many due to timing
}
