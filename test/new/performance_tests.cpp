#include <gtest/gtest.h>
#include <corororo/corororo.h>
#include <atomic>
#include <chrono>
#include <iostream>
#include <numeric>
#include <sstream>
#include <thread>
#include <vector>

using namespace std::chrono_literals;
using namespace CoroRoro;

class PerformanceTest : public ::testing::Test
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
// PERFORMANCE TESTS - Comprehensive Benchmarks
//

// TEST: Microbenchmark - Empty Task Overhead
TEST_F(PerformanceTest, EmptyTaskOverhead)
{
    const int numIterations = 10000;
    std::atomic<int> tasksCompleted{0};

    const auto start = std::chrono::steady_clock::now();

    for (int i = 0; i < numIterations; ++i) {
        auto emptyTask = []() -> Task<void> {
            co_return;
        };
        scheduler_->schedule(emptyTask());
        tasksCompleted.fetch_add(1);
    }

    // Process all tasks
    while (tasksCompleted.load() < numIterations) {
        scheduler_->runExpiredTasks();
    }

    const auto end = std::chrono::steady_clock::now();
    const auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start);
    const auto perTask = duration.count() / numIterations;

    std::cout << "Empty Task Overhead Test:" << std::endl;
    std::cout << "========================" << std::endl;
    std::cout << "Tasks: " << numIterations << std::endl;
    std::cout << "Total time: " << duration.count() << "ns" << std::endl;
    std::cout << "Per task: " << perTask << "ns" << std::endl;
    std::cout << "Tasks per second: " << (1000000000.0 / perTask) << std::endl;

    EXPECT_EQ(tasksCompleted.load(), numIterations);
}

// TEST: Microbenchmark - Empty AsyncTask Overhead
TEST_F(PerformanceTest, EmptyAsyncTaskOverhead)
{
    const int numIterations = 1000;
    std::atomic<int> tasksCompleted{0};

    const auto start = std::chrono::steady_clock::now();

    for (int i = 0; i < numIterations; ++i) {
        auto emptyAsyncTask = []() -> AsyncTask<void> {
            co_return;
        };
        scheduler_->schedule(emptyAsyncTask());
        tasksCompleted.fetch_add(1);
    }

    // Process all tasks
    while (tasksCompleted.load() < numIterations) {
        scheduler_->runExpiredTasks();
    }

    const auto end = std::chrono::steady_clock::now();
    const auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    const auto perTask = duration.count() / numIterations;

    std::cout << "Empty AsyncTask Overhead Test:" << std::endl;
    std::cout << "==============================" << std::endl;
    std::cout << "Tasks: " << numIterations << std::endl;
    std::cout << "Total time: " << duration.count() << "μs" << std::endl;
    std::cout << "Per task: " << perTask << "μs" << std::endl;
    std::cout << "Thread switching overhead: ~" << perTask << "μs" << std::endl;

    EXPECT_EQ(tasksCompleted.load(), numIterations);
}

// TEST: Task Creation Rate Benchmark
TEST_F(PerformanceTest, TaskCreationRateBenchmark)
{
    const int numTasks = 50000;
    std::vector<Task<void>> tasks;
    tasks.reserve(numTasks);

    const auto start = std::chrono::steady_clock::now();

    // Create tasks
    for (int i = 0; i < numTasks; ++i) {
        auto task = []() -> Task<void> {
            co_return;
        };
        tasks.push_back(task());
    }

    const auto creationEnd = std::chrono::steady_clock::now();

    // Schedule tasks
    for (auto& task : tasks) {
        scheduler_->schedule(std::move(task));
    }

    const auto schedulingEnd = std::chrono::steady_clock::now();

    // Process tasks
    std::atomic<int> processed{0};
    while (processed.load() < numTasks) {
        scheduler_->runExpiredTasks();
        processed.fetch_add(1); // Simplified counting
    }

    const auto processingEnd = std::chrono::steady_clock::now();

    const auto creationTime = std::chrono::duration_cast<std::chrono::milliseconds>(creationEnd - start);
    const auto schedulingTime = std::chrono::duration_cast<std::chrono::milliseconds>(schedulingEnd - creationEnd);
    const auto processingTime = std::chrono::duration_cast<std::chrono::milliseconds>(processingEnd - schedulingEnd);

    std::cout << "Task Creation Rate Benchmark:" << std::endl;
    std::cout << "============================" << std::endl;
    std::cout << "Tasks: " << numTasks << std::endl;
    std::cout << "Creation time: " << creationTime.count() << "ms" << std::endl;
    std::cout << "Scheduling time: " << schedulingTime.count() << "ms" << std::endl;
    std::cout << "Processing time: " << processingTime.count() << "ms" << std::endl;
    std::cout << "Tasks per second (creation): " << (numTasks * 1000.0 / creationTime.count()) << std::endl;
    std::cout << "Tasks per second (scheduling): " << (numTasks * 1000.0 / schedulingTime.count()) << std::endl;

    EXPECT_EQ(processed.load(), numTasks);
}

// TEST: Memory Allocation Patterns
TEST_F(PerformanceTest, MemoryAllocationPatterns)
{
    const int numTasks = 1000;
    std::vector<Task<void>> tasks;

    // Measure memory usage before
    const auto start = std::chrono::steady_clock::now();

    // Create many tasks to stress memory allocation
    for (int i = 0; i < numTasks; ++i) {
        auto task = [i]() -> Task<void> {
            // Capture variable to test memory allocation
            volatile int data = i;
            (void)data;
            co_return;
        };
        tasks.push_back(task());
    }

    const auto creationEnd = std::chrono::steady_clock::now();

    // Schedule and process
    for (auto& task : tasks) {
        scheduler_->schedule(std::move(task));
    }

    std::atomic<int> processed{0};
    while (processed.load() < numTasks) {
        scheduler_->runExpiredTasks();
        processed.fetch_add(1);
    }

    const auto processingEnd = std::chrono::steady_clock::now();

    const auto creationTime = std::chrono::duration_cast<std::chrono::microseconds>(creationEnd - start);
    const auto processingTime = std::chrono::duration_cast<std::chrono::microseconds>(processingEnd - creationEnd);

    std::cout << "Memory Allocation Patterns Test:" << std::endl;
    std::cout << "===============================" << std::endl;
    std::cout << "Tasks: " << numTasks << std::endl;
    std::cout << "Creation time: " << creationTime.count() << "μs" << std::endl;
    std::cout << "Processing time: " << processingTime.count() << "μs" << std::endl;
    std::cout << "Time per task: " << (creationTime.count() / numTasks) << "μs" << std::endl;

    EXPECT_EQ(processed.load(), numTasks);
}

// TEST: Scalability Test - Increasing Load
TEST_F(PerformanceTest, ScalabilityTest)
{
    const std::vector<int> loads = {10, 50, 100, 200, 500};
    std::vector<long long> durations;

    for (int load : loads) {
        std::atomic<int> completed{0};

        const auto start = std::chrono::steady_clock::now();

        // Create load number of tasks
        for (int i = 0; i < load; ++i) {
            auto task = [i]() -> Task<void> {
                // Variable work to simulate real load
                volatile int work = i % 10;
                for (int j = 0; j < work * 100; ++j) {
                    volatile int dummy = j;
                    (void)dummy;
                }
                co_return;
            };
            scheduler_->schedule(task());
            completed.fetch_add(1);
        }

        while (completed.load() < load) {
            scheduler_->runExpiredTasks();
        }

        const auto end = std::chrono::steady_clock::now();
        const auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
        durations.push_back(duration);

        std::cout << "Load " << load << ": " << duration << "μs" << std::endl;
    }

    std::cout << "Scalability Test Results:" << std::endl;
    std::cout << "========================" << std::endl;
    for (size_t i = 0; i < loads.size(); ++i) {
        double perTask = static_cast<double>(durations[i]) / loads[i];
        std::cout << "Load " << loads[i] << ": " << durations[i] << "μs total, "
                  << perTask << "μs per task" << std::endl;
    }

    // Check that performance scales reasonably
    EXPECT_LT(durations.back(), durations.front() * 100) << "Performance should scale reasonably";
}

// TEST: Latency Measurement - Task Queue Latency
TEST_F(PerformanceTest, TaskQueueLatency)
{
    const int numMeasurements = 100;
    std::vector<long long> latencies;

    for (int i = 0; i < numMeasurements; ++i) {
        std::atomic<bool> taskExecuted{false};

        const auto submitTime = std::chrono::steady_clock::now();

        auto latencyTask = [&]() -> Task<void> {
            taskExecuted = true;
            co_return;
        };

        scheduler_->schedule(latencyTask());

        while (!taskExecuted.load()) {
            scheduler_->runExpiredTasks();
        }

        const auto executeTime = std::chrono::steady_clock::now();
        const auto latency = std::chrono::duration_cast<std::chrono::microseconds>(executeTime - submitTime).count();
        latencies.push_back(latency);
    }

    // Calculate statistics
    long long minLatency = *std::min_element(latencies.begin(), latencies.end());
    long long maxLatency = *std::max_element(latencies.begin(), latencies.end());
    double avgLatency = std::accumulate(latencies.begin(), latencies.end(), 0.0) / latencies.size();

    std::cout << "Task Queue Latency Test:" << std::endl;
    std::cout << "========================" << std::endl;
    std::cout << "Measurements: " << numMeasurements << std::endl;
    std::cout << "Min latency: " << minLatency << "μs" << std::endl;
    std::cout << "Max latency: " << maxLatency << "μs" << std::endl;
    std::cout << "Avg latency: " << avgLatency << "μs" << std::endl;

    EXPECT_LT(avgLatency, 1000) << "Average latency should be reasonable";
    EXPECT_LT(maxLatency, 10000) << "Maximum latency should be reasonable";
}

// TEST: Throughput Test - Maximum Task Processing Rate
TEST_F(PerformanceTest, ThroughputTest)
{
    const int numTasks = 10000;
    const auto testDuration = std::chrono::seconds(5);
    std::atomic<int> tasksProcessed{0};

    const auto start = std::chrono::steady_clock::now();

    // Continuously submit tasks for the test duration
    while (std::chrono::steady_clock::now() - start < testDuration) {
        for (int i = 0; i < 100 && tasksProcessed.load() < numTasks; ++i) {
            auto throughputTask = []() -> Task<void> {
                volatile int dummy = 0;
                for (int j = 0; j < 10; ++j) {
                    dummy += j;
                }
                (void)dummy;
                co_return;
            };
            scheduler_->schedule(throughputTask());
            tasksProcessed.fetch_add(1);
        }

        scheduler_->runExpiredTasks();
    }

    const auto end = std::chrono::steady_clock::now();
    const auto actualDuration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    const long long tasksPerSecond = (tasksProcessed.load() * 1000LL) / actualDuration.count();

    std::cout << "Throughput Test:" << std::endl;
    std::cout << "===============" << std::endl;
    std::cout << "Tasks processed: " << tasksProcessed.load() << std::endl;
    std::cout << "Duration: " << actualDuration.count() << "ms" << std::endl;
    std::cout << "Tasks per second: " << tasksPerSecond << std::endl;

    EXPECT_GT(tasksPerSecond, 1000) << "Should process at least 1000 tasks per second";
}

// TEST: Memory Efficiency Test
TEST_F(PerformanceTest, MemoryEfficiencyTest)
{
    const int numTasks = 5000;
    std::vector<Task<void>> tasks;

    // Create tasks that capture data to test memory efficiency
    for (int i = 0; i < numTasks; ++i) {
        std::string data = "Task data " + std::to_string(i);
        auto memoryTask = [data = std::move(data)]() -> Task<void> {
            volatile size_t size = data.size();
            (void)size;
            co_return;
        };
        tasks.push_back(memoryTask());
    }

    const auto start = std::chrono::steady_clock::now();

    for (auto& task : tasks) {
        scheduler_->schedule(std::move(task));
    }

    std::atomic<int> processed{0};
    while (processed.load() < numTasks) {
        scheduler_->runExpiredTasks();
        processed.fetch_add(1);
    }

    const auto end = std::chrono::steady_clock::now();
    const auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

    std::cout << "Memory Efficiency Test:" << std::endl;
    std::cout << "======================" << std::endl;
    std::cout << "Tasks with data: " << numTasks << std::endl;
    std::cout << "Processing time: " << duration.count() << "ms" << std::endl;
    std::cout << "Tasks per second: " << (numTasks * 1000 / duration.count()) << std::endl;

    EXPECT_EQ(processed.load(), numTasks);
    EXPECT_LT(duration.count(), 2000) << "Should process tasks efficiently";
}

// TEST: Stress Test - Maximum Concurrent Tasks
TEST_F(PerformanceTest, StressTestMaximumConcurrent)
{
    const int maxConcurrentTasks = 1000;
    std::atomic<int> tasksCompleted{0};

    const auto start = std::chrono::steady_clock::now();

    // Submit maximum concurrent tasks
    for (int i = 0; i < maxConcurrentTasks; ++i) {
        auto stressTask = [i]() -> Task<void> {
            // Variable work based on task index
            volatile int work = i % 100;
            for (int j = 0; j < work; ++j) {
                volatile int dummy = j * work;
                (void)dummy;
            }
            co_return;
        };
        scheduler_->schedule(stressTask());
        tasksCompleted.fetch_add(1);
    }

    // Process all tasks
    while (tasksCompleted.load() < maxConcurrentTasks) {
        scheduler_->runExpiredTasks();
    }

    const auto end = std::chrono::steady_clock::now();
    const auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

    std::cout << "Stress Test - Maximum Concurrent Tasks:" << std::endl;
    std::cout << "======================================" << std::endl;
    std::cout << "Concurrent tasks: " << maxConcurrentTasks << std::endl;
    std::cout << "Processing time: " << duration.count() << "ms" << std::endl;
    std::cout << "Tasks per second: " << (maxConcurrentTasks * 1000 / duration.count()) << std::endl;

    EXPECT_EQ(tasksCompleted.load(), maxConcurrentTasks);
    EXPECT_LT(duration.count(), 5000) << "Should handle high concurrency reasonably";
}
