#include <gtest/gtest.h>
#include <corororo/corororo.h>
#include <atomic>
#include <chrono>
#include <thread>
#include <vector>
#include <condition_variable>
#include <mutex>

using namespace std::chrono_literals;
using namespace CoroRoro;

class IntervalTest : public ::testing::Test
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
// INTERVAL/DELAYED TESTS - Manual Implementation Patterns
//

// TEST: Manual Interval Task Implementation
TEST_F(IntervalTest, ManualIntervalTaskImplementation)
{
    const int expectedExecutions = 5;
    const auto interval = std::chrono::milliseconds(100);
    std::atomic<int> executionCount{0};
    std::atomic<bool> keepRunning{true};

    // Manual interval implementation
    auto intervalTask = [&]() -> Task<void> {
        while (keepRunning.load()) {
            // Execute the task
            executionCount.fetch_add(1);

            // Wait for next interval
            std::this_thread::sleep_for(interval);

            // Small yield to prevent busy waiting
            co_await std::suspend_always{};
        }
    };

    const auto start = std::chrono::steady_clock::now();
    scheduler_->schedule(intervalTask());

    // Let it run for expected duration
    auto expectedDuration = interval * expectedExecutions;
    std::this_thread::sleep_for(expectedDuration + std::chrono::milliseconds(50));

    // Stop the interval
    keepRunning = false;

    // Process any remaining tasks
    for (int i = 0; i < 10; ++i) {
        scheduler_->runExpiredTasks();
    }

    const auto end = std::chrono::steady_clock::now();
    const auto actualDuration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

    std::cout << "Manual Interval Task Implementation Test:" << std::endl;
    std::cout << "=========================================" << std::endl;
    std::cout << "Expected executions: " << expectedExecutions << std::endl;
    std::cout << "Actual executions: " << executionCount.load() << std::endl;
    std::cout << "Interval: " << interval.count() << "ms" << std::endl;
    std::cout << "Expected duration: " << expectedDuration.count() << "ms" << std::endl;
    std::cout << "Actual duration: " << actualDuration.count() << "ms" << std::endl;

    EXPECT_GE(executionCount.load(), expectedExecutions - 1);
    EXPECT_LE(executionCount.load(), expectedExecutions + 1);
}

// TEST: Manual Delayed Task Implementation
TEST_F(IntervalTest, ManualDelayedTaskImplementation)
{
    std::atomic<bool> taskExecuted{false};
    const auto delay = std::chrono::milliseconds(200);

    auto delayedTask = [&]() -> Task<void> {
        std::this_thread::sleep_for(delay);
        taskExecuted = true;
        co_return;
    };

    const auto start = std::chrono::steady_clock::now();
    scheduler_->schedule(delayedTask());

    // Wait for task to complete
    while (!taskExecuted.load()) {
        scheduler_->runExpiredTasks();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    const auto end = std::chrono::steady_clock::now();
    const auto actualDelay = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

    std::cout << "Manual Delayed Task Implementation Test:" << std::endl;
    std::cout << "=========================================" << std::endl;
    std::cout << "Requested delay: " << delay.count() << "ms" << std::endl;
    std::cout << "Actual delay: " << actualDelay.count() << "ms" << std::endl;
    std::cout << "Accuracy: " << (actualDelay.count() - delay.count()) << "ms difference" << std::endl;

    EXPECT_TRUE(taskExecuted.load());
    EXPECT_GE(actualDelay.count(), delay.count() - 50); // Allow some tolerance
    EXPECT_LE(actualDelay.count(), delay.count() + 100);
}

// TEST: Recurring Task with External Timer
TEST_F(IntervalTest, RecurringTaskWithExternalTimer)
{
    const int maxExecutions = 8;
    const auto interval = std::chrono::milliseconds(150);
    std::atomic<int> executionCount{0};
    std::atomic<bool> timerRunning{true};

    // External timer thread
    std::thread timerThread([&]() {
        for (int i = 0; i < maxExecutions && timerRunning.load(); ++i) {
            std::this_thread::sleep_for(interval);

            // Schedule task on main thread
            auto recurringTask = [&]() -> Task<void> {
                executionCount.fetch_add(1);
                std::cout << "Execution #" << executionCount.load() << std::endl;
                co_return;
            };

            scheduler_->schedule(recurringTask());
        }
    });

    const auto start = std::chrono::steady_clock::now();

    // Process tasks for the duration
    auto expectedDuration = interval * maxExecutions;
    auto timeout = start + expectedDuration + std::chrono::milliseconds(200);

    while (std::chrono::steady_clock::now() < timeout && executionCount.load() < maxExecutions) {
        scheduler_->runExpiredTasks();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    timerRunning = false;
    timerThread.join();

    const auto end = std::chrono::steady_clock::now();
    const auto actualDuration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

    std::cout << "Recurring Task with External Timer Test:" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "Max executions: " << maxExecutions << std::endl;
    std::cout << "Actual executions: " << executionCount.load() << std::endl;
    std::cout << "Interval: " << interval.count() << "ms" << std::endl;
    std::cout << "Total duration: " << actualDuration.count() << "ms" << std::endl;

    EXPECT_GE(executionCount.load(), maxExecutions - 2); // Allow some tolerance
    EXPECT_LE(executionCount.load(), maxExecutions + 1);
}

// TEST: Delayed Task with Cancellation Pattern
TEST_F(IntervalTest, DelayedTaskWithCancellationPattern)
{
    std::atomic<bool> taskExecuted{false};
    std::atomic<bool> taskCancelled{false};
    const auto delay = std::chrono::milliseconds(300);

    // Delayed task
    auto delayedTask = [&]() -> Task<void> {
        std::this_thread::sleep_for(delay);

        if (!taskCancelled.load()) {
            taskExecuted = true;
        }

        co_return;
    };

    // Cancellation mechanism (external to scheduler)
    auto cancellationTask = [&]() -> Task<void> {
        std::this_thread::sleep_for(std::chrono::milliseconds(150)); // Cancel before delay expires
        taskCancelled = true;
        co_return;
    };

    const auto start = std::chrono::steady_clock::now();

    // Schedule both tasks
    scheduler_->schedule(delayedTask());
    scheduler_->schedule(cancellationTask());

    // Wait for completion
    auto timeout = start + std::chrono::milliseconds(500);
    while (std::chrono::steady_clock::now() < timeout &&
           (!taskExecuted.load() && !taskCancelled.load())) {
        scheduler_->runExpiredTasks();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    const auto end = std::chrono::steady_clock::now();
    const auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

    std::cout << "Delayed Task with Cancellation Pattern Test:" << std::endl;
    std::cout << "=============================================" << std::endl;
    std::cout << "Delay: " << delay.count() << "ms" << std::endl;
    std::cout << "Cancellation time: 150ms" << std::endl;
    std::cout << "Task executed: " << (taskExecuted.load() ? "YES" : "NO") << std::endl;
    std::cout << "Task cancelled: " << (taskCancelled.load() ? "YES" : "NO") << std::endl;
    std::cout << "Total duration: " << duration.count() << "ms" << std::endl;

    EXPECT_TRUE(taskCancelled.load()); // Should be cancelled before execution
    EXPECT_FALSE(taskExecuted.load()); // Should not execute due to cancellation
}

// TEST: Multiple Interval Tasks with Different Frequencies
TEST_F(IntervalTest, MultipleIntervalTasksDifferentFrequencies)
{
    const int durationSeconds = 3;
    std::atomic<int> fastTaskCount{0};
    std::atomic<int> slowTaskCount{0};
    std::atomic<bool> running{true};

    // Fast interval task (every 200ms)
    auto fastIntervalTask = [&]() -> Task<void> {
        while (running.load()) {
            fastTaskCount.fetch_add(1);
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            co_await std::suspend_always{};
        }
    };

    // Slow interval task (every 500ms)
    auto slowIntervalTask = [&]() -> Task<void> {
        while (running.load()) {
            slowTaskCount.fetch_add(1);
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            co_await std::suspend_always{};
        }
    };

    const auto start = std::chrono::steady_clock::now();

    // Schedule both interval tasks
    scheduler_->schedule(fastIntervalTask());
    scheduler_->schedule(slowIntervalTask());

    // Let them run for the test duration
    std::this_thread::sleep_for(std::chrono::seconds(durationSeconds));

    running = false;

    // Process any remaining tasks
    for (int i = 0; i < 20; ++i) {
        scheduler_->runExpiredTasks();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    const auto end = std::chrono::steady_clock::now();
    const auto actualDuration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

    int expectedFastTasks = durationSeconds * 1000 / 200; // ~15
    int expectedSlowTasks = durationSeconds * 1000 / 500;  // ~6

    std::cout << "Multiple Interval Tasks with Different Frequencies Test:" << std::endl;
    std::cout << "======================================================" << std::endl;
    std::cout << "Test duration: " << durationSeconds << " seconds" << std::endl;
    std::cout << "Fast task interval: 200ms" << std::endl;
    std::cout << "Slow task interval: 500ms" << std::endl;
    std::cout << "Fast task executions: " << fastTaskCount.load() << " (expected: ~" << expectedFastTasks << ")" << std::endl;
    std::cout << "Slow task executions: " << slowTaskCount.load() << " (expected: ~" << expectedSlowTasks << ")" << std::endl;
    std::cout << "Actual duration: " << actualDuration.count() << "ms" << std::endl;

    EXPECT_GE(fastTaskCount.load(), expectedFastTasks - 2);
    EXPECT_LE(fastTaskCount.load(), expectedFastTasks + 2);
    EXPECT_GE(slowTaskCount.load(), expectedSlowTasks - 1);
    EXPECT_LE(slowTaskCount.load(), expectedSlowTasks + 1);
}

// TEST: Interval Task with Variable Execution Time
TEST_F(IntervalTest, IntervalTaskWithVariableExecutionTime)
{
    const int numExecutions = 5;
    const auto baseInterval = std::chrono::milliseconds(300);
    std::atomic<int> executionCount{0};
    std::vector<long long> executionTimes;

    auto variableTimeTask = [&]() -> Task<void> {
        for (int i = 0; i < numExecutions; ++i) {
            const auto executionStart = std::chrono::steady_clock::now();

            // Variable execution time (increases with each execution)
            auto executionTime = std::chrono::milliseconds(50 + i * 20);
            std::this_thread::sleep_for(executionTime);

            const auto executionEnd = std::chrono::steady_clock::now();
            const auto actualExecutionTime = std::chrono::duration_cast<std::chrono::milliseconds>(executionEnd - executionStart);

            executionTimes.push_back(actualExecutionTime.count());
            executionCount.fetch_add(1);

            // Fixed interval between executions
            std::this_thread::sleep_for(baseInterval - executionTime);
            co_await std::suspend_always{};
        }
    };

    const auto start = std::chrono::steady_clock::now();
    scheduler_->schedule(variableTimeTask());

    // Wait for all executions to complete
    while (executionCount.load() < numExecutions) {
        scheduler_->runExpiredTasks();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    const auto end = std::chrono::steady_clock::now();
    const auto totalDuration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

    std::cout << "Interval Task with Variable Execution Time Test:" << std::endl;
    std::cout << "===============================================" << std::endl;
    std::cout << "Executions: " << numExecutions << std::endl;
    std::cout << "Base interval: " << baseInterval.count() << "ms" << std::endl;
    std::cout << "Total duration: " << totalDuration.count() << "ms" << std::endl;

    for (size_t i = 0; i < executionTimes.size(); ++i) {
        std::cout << "Execution " << (i + 1) << " time: " << executionTimes[i] << "ms" << std::endl;
    }

    EXPECT_EQ(executionCount.load(), numExecutions);
    EXPECT_EQ(executionTimes.size(), numExecutions);
    EXPECT_GT(totalDuration.count(), baseInterval.count() * numExecutions * 0.9); // Should be close to expected
}

// TEST: Delayed Task Chain (Sequential Dependencies)
TEST_F(IntervalTest, DelayedTaskChain)
{
    const int chainLength = 5;
    const auto baseDelay = std::chrono::milliseconds(100);
    std::atomic<int> completedTasks{0};
    std::vector<std::chrono::milliseconds> completionTimes;

    // Create a chain of delayed tasks
    auto createDelayedTask = [&](int taskId) -> Task<void> {
        return [&, taskId]() -> Task<void> {
            const auto start = std::chrono::steady_clock::now();

            // Variable delay based on task ID
            auto delay = baseDelay * (taskId + 1);
            std::this_thread::sleep_for(delay);

            const auto end = std::chrono::steady_clock::now();
            const auto executionTime = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

            {
                std::lock_guard<std::mutex> lock(completionTimesMutex_);
                completionTimes.push_back(executionTime);
            }

            completedTasks.fetch_add(1);
            co_return;
        }();
    };

    const auto chainStart = std::chrono::steady_clock::now();

    // Schedule all tasks in the chain
    for (int i = 0; i < chainLength; ++i) {
        scheduler_->schedule(createDelayedTask(i));
    }

    // Wait for all tasks to complete
    while (completedTasks.load() < chainLength) {
        scheduler_->runExpiredTasks();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    const auto chainEnd = std::chrono::steady_clock::now();
    const auto totalDuration = std::chrono::duration_cast<std::chrono::milliseconds>(chainEnd - chainStart);

    std::cout << "Delayed Task Chain Test:" << std::endl;
    std::cout << "=======================" << std::endl;
    std::cout << "Chain length: " << chainLength << std::endl;
    std::cout << "Base delay: " << baseDelay.count() << "ms" << std::endl;
    std::cout << "Total duration: " << totalDuration.count() << "ms" << std::endl;

    for (size_t i = 0; i < completionTimes.size(); ++i) {
        std::cout << "Task " << (i + 1) << " completion time: " << completionTimes[i].count() << "ms" << std::endl;
    }

    EXPECT_EQ(completedTasks.load(), chainLength);
    EXPECT_EQ(completionTimes.size(), chainLength);

private:
    std::mutex completionTimesMutex_;
};
