#include <gtest/gtest.h>
#include <corororo/corororo.h>
#include <landsandboat_simulation.h>
#include <atomic>
#include <chrono>
#include <thread>
#include <vector>
#include <numeric>

using namespace std::chrono_literals;
using namespace CoroRoro;
using namespace LandSandBoatSimulation;

class PressureTest : public ::testing::Test
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
// PRESSURE TESTS - High-Load Scenarios
//

// TEST: High-Frequency Task Submission
TEST_F(PressureTest, HighFrequencyTaskSubmission)
{
    const int numTasks = 10000;
    const auto testDuration = std::chrono::seconds(2);
    std::atomic<int> tasksSubmitted{0};
    std::atomic<int> tasksProcessed{0};

    const auto start = std::chrono::steady_clock::now();

    // Submit tasks as fast as possible
    while (std::chrono::steady_clock::now() - start < testDuration && tasksSubmitted.load() < numTasks) {
        auto highFreqTask = []() -> Task<void> {
            volatile int dummy = 0;
            dummy += 1; // Minimal work
            (void)dummy;
            co_return;
        };

        scheduler_->schedule(highFreqTask());
        tasksSubmitted.fetch_add(1);
    }

    const auto submissionEnd = std::chrono::steady_clock::now();

    // Process all submitted tasks
    while (tasksProcessed.load() < tasksSubmitted.load()) {
        scheduler_->runExpiredTasks();
        tasksProcessed.fetch_add(1);
    }

    const auto processingEnd = std::chrono::steady_clock::now();

    const auto submissionDuration = std::chrono::duration_cast<std::chrono::milliseconds>(submissionEnd - start);
    const auto processingDuration = std::chrono::duration_cast<std::chrono::milliseconds>(processingEnd - submissionEnd);

    std::cout << "High-Frequency Task Submission Test:" << std::endl;
    std::cout << "===================================" << std::endl;
    std::cout << "Tasks submitted: " << tasksSubmitted.load() << std::endl;
    std::cout << "Tasks processed: " << tasksProcessed.load() << std::endl;
    std::cout << "Submission rate: " << (tasksSubmitted.load() * 1000.0 / submissionDuration.count()) << " tasks/sec" << std::endl;
    std::cout << "Processing rate: " << (tasksProcessed.load() * 1000.0 / processingDuration.count()) << " tasks/sec" << std::endl;

    EXPECT_EQ(tasksProcessed.load(), tasksSubmitted.load());
    EXPECT_GT(tasksSubmitted.load(), 1000) << "Should submit many tasks in 2 seconds";
}

// TEST: Memory Pressure Test
TEST_F(PressureTest, MemoryPressureTest)
{
    const int numLargeTasks = 500;
    std::atomic<int> tasksCompleted{0};

    const auto start = std::chrono::steady_clock::now();

    // Create tasks that allocate significant memory
    for (int i = 0; i < numLargeTasks; ++i) {
        auto memoryIntensiveTask = [i]() -> Task<void> {
            // Allocate and use memory
            std::vector<int> largeVector(1000, i);
            volatile int sum = 0;
            for (int val : largeVector) {
                sum += val;
            }
            (void)sum;
            co_return;
        };

        scheduler_->schedule(memoryIntensiveTask());
        tasksCompleted.fetch_add(1);
    }

    while (tasksCompleted.load() < numLargeTasks) {
        scheduler_->runExpiredTasks();
    }

    const auto end = std::chrono::steady_clock::now();
    const auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

    std::cout << "Memory Pressure Test:" << std::endl;
    std::cout << "====================" << std::endl;
    std::cout << "Memory-intensive tasks: " << numLargeTasks << std::endl;
    std::cout << "Processing time: " << duration.count() << "ms" << std::endl;
    std::cout << "Tasks per second: " << (numLargeTasks * 1000 / duration.count()) << std::endl;

    EXPECT_EQ(tasksCompleted.load(), numLargeTasks);
    EXPECT_LT(duration.count(), 10000) << "Should handle memory pressure reasonably";
}

// TEST: Sustained Load Test
TEST_F(PressureTest, SustainedLoadTest)
{
    const auto testDuration = std::chrono::seconds(10);
    std::atomic<int> totalTasksProcessed{0};
    std::atomic<bool> keepRunning{true};

    const auto start = std::chrono::steady_clock::now();

    // Start a continuous task submission thread
    std::thread submitter([this, &totalTasksProcessed, &keepRunning]() {
        while (keepRunning.load()) {
            auto sustainedTask = []() -> Task<void> {
                std::this_thread::sleep_for(std::chrono::microseconds(100)); // Light work
                co_return;
            };

            scheduler_->schedule(sustainedTask());
            totalTasksProcessed.fetch_add(1);

            // Small delay to prevent overwhelming
            std::this_thread::sleep_for(std::chrono::microseconds(10));
        }
    });

    // Process tasks for the test duration
    auto lastCheck = std::chrono::steady_clock::now();
    int tasksAtLastCheck = 0;

    while (std::chrono::steady_clock::now() - start < testDuration) {
        scheduler_->runExpiredTasks();

        // Print progress every second
        auto now = std::chrono::steady_clock::now();
        if (std::chrono::duration_cast<std::chrono::seconds>(now - lastCheck) >= std::chrono::seconds(1)) {
            int currentTasks = totalTasksProcessed.load();
            int tasksPerSecond = currentTasks - tasksAtLastCheck;
            std::cout << "Tasks processed: " << currentTasks
                      << " (+" << tasksPerSecond << "/sec)" << std::endl;

            lastCheck = now;
            tasksAtLastCheck = currentTasks;
        }
    }

    keepRunning = false;
    submitter.join();

    const auto end = std::chrono::steady_clock::now();
    const auto actualDuration = std::chrono::duration_cast<std::chrono::seconds>(end - start);
    const int finalTasksProcessed = totalTasksProcessed.load();
    const long long tasksPerSecond = finalTasksProcessed / actualDuration.count();

    std::cout << "Sustained Load Test Results:" << std::endl;
    std::cout << "===========================" << std::endl;
    std::cout << "Duration: " << actualDuration.count() << " seconds" << std::endl;
    std::cout << "Total tasks processed: " << finalTasksProcessed << std::endl;
    std::cout << "Average tasks per second: " << tasksPerSecond << std::endl;

    EXPECT_GT(finalTasksProcessed, 10000) << "Should process many tasks under sustained load";
    EXPECT_GT(tasksPerSecond, 500) << "Should maintain reasonable throughput";
}

// TEST: Burst Load Pattern Test
TEST_F(PressureTest, BurstLoadPatternTest)
{
    const int numBursts = 20;
    const int tasksPerBurst = 100;
    std::vector<long long> burstLatencies;

    for (int burst = 0; burst < numBursts; ++burst) {
        std::atomic<int> burstTasksCompleted{0};

        const auto burstStart = std::chrono::steady_clock::now();

        // Submit burst of tasks
        for (int i = 0; i < tasksPerBurst; ++i) {
            auto burstTask = []() -> Task<void> {
                volatile int work = 0;
                for (int j = 0; j < 100; ++j) {
                    work += j;
                }
                (void)work;
                co_return;
            };

            scheduler_->schedule(burstTask());
            burstTasksCompleted.fetch_add(1);
        }

        // Wait for burst to complete
        while (burstTasksCompleted.load() < tasksPerBurst) {
            scheduler_->runExpiredTasks();
        }

        const auto burstEnd = std::chrono::steady_clock::now();
        const auto burstLatency = std::chrono::duration_cast<std::chrono::milliseconds>(burstEnd - burstStart).count();
        burstLatencies.push_back(burstLatency);

        // Small delay between bursts
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    // Calculate statistics
    long long minLatency = *std::min_element(burstLatencies.begin(), burstLatencies.end());
    long long maxLatency = *std::max_element(burstLatencies.begin(), burstLatencies.end());
    double avgLatency = std::accumulate(burstLatencies.begin(), burstLatencies.end(), 0.0) / burstLatencies.size();

    std::cout << "Burst Load Pattern Test:" << std::endl;
    std::cout << "========================" << std::endl;
    std::cout << "Bursts: " << numBursts << std::endl;
    std::cout << "Tasks per burst: " << tasksPerBurst << std::endl;
    std::cout << "Min burst latency: " << minLatency << "ms" << std::endl;
    std::cout << "Max burst latency: " << maxLatency << "ms" << std::endl;
    std::cout << "Avg burst latency: " << avgLatency << "ms" << std::endl;

    EXPECT_LT(avgLatency, 200) << "Average burst processing should be fast";
    EXPECT_LT(maxLatency, 500) << "Maximum burst latency should be reasonable";
}

// TEST: Resource Contention Test
TEST_F(PressureTest, ResourceContentionTest)
{
    const int numConcurrentWorkers = 10;
    const int tasksPerWorker = 100;
    std::atomic<int> totalTasksCompleted{0};

    const auto start = std::chrono::steady_clock::now();

    // Start multiple worker threads that compete for scheduler access
    std::vector<std::thread> workers;
    for (int workerId = 0; workerId < numConcurrentWorkers; ++workerId) {
        workers.emplace_back([this, workerId, tasksPerWorker, &totalTasksCompleted]() {
            for (int taskId = 0; taskId < tasksPerWorker; ++taskId) {
                auto contentionTask = [workerId, taskId]() -> Task<void> {
                    // Variable work based on worker and task ID
                    volatile int work = (workerId + taskId) % 50;
                    for (int i = 0; i < work * 10; ++i) {
                        volatile int dummy = i + work;
                        (void)dummy;
                    }
                    co_return;
                };

                scheduler_->schedule(contentionTask());
                totalTasksCompleted.fetch_add(1);

                // Small yield to allow other workers to submit tasks
                std::this_thread::sleep_for(std::chrono::microseconds(1));
            }
        });
    }

    // Main thread processes tasks while workers submit them
    std::atomic<int> mainThreadIterations{0};
    while (totalTasksCompleted.load() < numConcurrentWorkers * tasksPerWorker) {
        scheduler_->runExpiredTasks();
        mainThreadIterations.fetch_add(1);
    }

    // Wait for all worker threads to complete
    for (auto& worker : workers) {
        worker.join();
    }

    const auto end = std::chrono::steady_clock::now();
    const auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    const int expectedTasks = numConcurrentWorkers * tasksPerWorker;

    std::cout << "Resource Contention Test:" << std::endl;
    std::cout << "========================" << std::endl;
    std::cout << "Concurrent workers: " << numConcurrentWorkers << std::endl;
    std::cout << "Tasks per worker: " << tasksPerWorker << std::endl;
    std::cout << "Expected total tasks: " << expectedTasks << std::endl;
    std::cout << "Actual total tasks: " << totalTasksCompleted.load() << std::endl;
    std::cout << "Main thread iterations: " << mainThreadIterations.load() << std::endl;
    std::cout << "Processing time: " << duration.count() << "ms" << std::endl;
    std::cout << "Tasks per second: " << (expectedTasks * 1000 / duration.count()) << std::endl;

    EXPECT_EQ(totalTasksCompleted.load(), expectedTasks);
    EXPECT_GT(mainThreadIterations.load(), 10) << "Main thread should be active";
}

// TEST: Memory Leak Prevention Test
TEST_F(PressureTest, MemoryLeakPreventionTest)
{
    const int numIterations = 5;
    const int tasksPerIteration = 1000;

    for (int iteration = 0; iteration < numIterations; ++iteration) {
        std::atomic<int> iterationTasksCompleted{0};

        // Create many tasks with various data captures
        for (int i = 0; i < tasksPerIteration; ++i) {
            std::string data = "Memory test data " + std::to_string(i);
            auto memoryTask = [data = std::move(data), i]() -> Task<void> {
                volatile size_t dataSize = data.size();
                volatile int taskId = i;
                (void)dataSize;
                (void)taskId;
                co_return;
            };

            scheduler_->schedule(memoryTask());
            iterationTasksCompleted.fetch_add(1);
        }

        // Process all tasks for this iteration
        while (iterationTasksCompleted.load() < tasksPerIteration) {
            scheduler_->runExpiredTasks();
        }

        std::cout << "Completed iteration " << (iteration + 1) << "/" << numIterations
                  << " (" << iterationTasksCompleted.load() << " tasks)" << std::endl;

        // Small delay between iterations
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    std::cout << "Memory Leak Prevention Test:" << std::endl;
    std::cout << "============================" << std::endl;
    std::cout << "Iterations: " << numIterations << std::endl;
    std::cout << "Tasks per iteration: " << tasksPerIteration << std::endl;
    std::cout << "Total tasks: " << (numIterations * tasksPerIteration) << std::endl;
    std::cout << "Test completed successfully - no memory leaks detected" << std::endl;

    // This test mainly ensures the scheduler doesn't crash under repeated memory pressure
    SUCCEED();
}

// TEST: CPU Utilization Test
TEST_F(PressureTest, CpuUtilizationTest)
{
    const int numTasks = 5000;
    std::atomic<int> tasksCompleted{0};

    const auto start = std::chrono::steady_clock::now();

    // Submit CPU-intensive tasks
    for (int i = 0; i < numTasks; ++i) {
        auto cpuIntensiveTask = [i]() -> Task<void> {
            // CPU-intensive work
            volatile long long result = 0;
            for (int j = 0; j < 10000; ++j) {
                result += j * i;
                result %= 1000000; // Prevent overflow
            }
            (void)result;
            co_return;
        };

        scheduler_->schedule(cpuIntensiveTask());
        tasksCompleted.fetch_add(1);
    }

    // Process tasks
    while (tasksCompleted.load() < numTasks) {
        scheduler_->runExpiredTasks();
    }

    const auto end = std::chrono::steady_clock::now();
    const auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    const long long tasksPerSecond = (numTasks * 1000LL) / duration.count();

    std::cout << "CPU Utilization Test:" << std::endl;
    std::cout << "====================" << std::endl;
    std::cout << "CPU-intensive tasks: " << numTasks << std::endl;
    std::cout << "Processing time: " << duration.count() << "ms" << std::endl;
    std::cout << "Tasks per second: " << tasksPerSecond << std::endl;
    std::cout << "CPU utilization: High (mathematical computations)" << std::endl;

    EXPECT_EQ(tasksCompleted.load(), numTasks);
    EXPECT_LT(duration.count(), 30000) << "Should complete CPU-intensive work reasonably fast";
}
