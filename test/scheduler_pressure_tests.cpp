#include <atomic>
#include <chrono>
#include <gtest/gtest.h>
#include <thread>
#include <vector>

#include "corororo/corororo.h"

#include "landsandboat_simulation.h"
#include "test_utils.h"


using namespace std::chrono_literals;
using namespace CoroRoro;
using namespace LandSandBoatSimulation;

class SchedulerPressureTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        // Create scheduler with 4 worker threads for testing
        scheduler = std::make_unique<Scheduler>(4);
    }

    void TearDown() override
    {
        scheduler.reset();
    }

    std::unique_ptr<Scheduler> scheduler;
};

//
// TEST: Realistic Workload Scaling Test
// PURPOSE: Test scheduler performance under realistic LandSandBoat-like workloads
// BEHAVIOR: Creates workloads of increasing complexity and size
// EXPECTATION: Should show how performance scales with workload size
//
TEST_F(SchedulerPressureTest, RealisticWorkloadScalingTest)
{
    const std::vector<int>                 workloadSizes = { 10, 50, 100, 200, 500 };
    std::vector<std::chrono::microseconds> durations;

    for (int workloadSize : workloadSizes)
    {
        std::atomic<int> tasksCompleted{ 0 };

        auto workloadTask = [&]() -> Task<void>
        {
            for (int i = 0; i < workloadSize; ++i)
            {
                // Simulate realistic LandSandBoat workload
                auto result = co_await [&]() -> Task<int>
                {
                    // AI decision making
                    auto aiDecision = co_await [&]() -> Task<int>
                    {
                        // Entity movement calculation
                        auto movement = co_await [&]() -> Task<int>
                        {
                            // Database update simulation
                            auto dbUpdate = co_await [&]() -> Task<int>
                            {
                                // Network packet processing
                                auto networkPacket = co_await [&]() -> Task<int>
                                {
                                    // Pathfinding (AsyncTask)
                                    auto pathfinding = co_await [&]() -> AsyncTask<std::vector<position_t>>
                                    {
                                        // Simulate 5ms pathfinding work
                                        std::this_thread::sleep_for(std::chrono::microseconds(5000));
                                        std::vector<position_t> path = { { 1.0f, 2.0f, 3.0f }, { 4.0f, 5.0f, 6.0f } };
                                        co_return path;
                                    }();

                                    co_return static_cast<int>(pathfinding.size());
                                }();

                                co_return networkPacket * 2;
                            }();

                            co_return dbUpdate + 1;
                        }();

                        co_return movement * 3;
                    }();

                    co_return aiDecision * 4;
                }();
                (void)result; // Suppress unused variable warning

                tasksCompleted.fetch_add(1);
            }
        };

        const auto start = std::chrono::steady_clock::now();
        scheduler->schedule(std::move(workloadTask));

        while (tasksCompleted.load() < workloadSize)
        {
            scheduler->runExpiredTasks();
            std::this_thread::sleep_for(1ms);
        }

        const auto end = std::chrono::steady_clock::now();
        durations.push_back(std::chrono::duration_cast<std::chrono::microseconds>(end - start));
    }

    std::cout << "Realistic Workload Scaling Test:" << std::endl;
    std::cout << "===============================" << std::endl;
    std::cout << std::endl;

    for (size_t i = 0; i < workloadSizes.size(); ++i)
    {
        const auto perTask = durations[i].count() / workloadSizes[i];
        std::cout << "  " << workloadSizes[i] << " tasks: " << durations[i].count() << "μs total, " << perTask << "μs per task" << std::endl;
    }

    std::cout << std::endl;
    std::cout << "SCALING ANALYSIS:" << std::endl;
    std::cout << "  - Tests realistic LandSandBoat workload patterns" << std::endl;
    std::cout << "  - Each task includes AI, movement, DB, network, and pathfinding" << std::endl;
    std::cout << "  - Shows how performance scales with workload size" << std::endl;
    std::cout << "  - Helps identify optimal workload sizes" << std::endl;

    // Verify all tests completed
    for (const auto& duration : durations)
    {
        EXPECT_GT(duration.count(), 0) << "All workload sizes should complete";
    }
}

//
// TEST: High Concurrency Stress Test
// PURPOSE: Test scheduler under high concurrency conditions
// BEHAVIOR: Creates many concurrent tasks to stress the scheduler
// EXPECTATION: Should maintain performance under high load
//
TEST_F(SchedulerPressureTest, HighConcurrencyStressTest)
{
    const int        numConcurrentTasks = 1000;
    const int        numWorkers         = 4;
    std::atomic<int> tasksCompleted{ 0 };

    // Create many concurrent tasks
    std::vector<Task<void>> concurrentTasks;
    for (int i = 0; i < numConcurrentTasks; ++i)
    {
        concurrentTasks.push_back([&]() -> Task<void>
                                  {
            // Simulate some work
            auto result = co_await [&]() -> AsyncTask<int>
            {
                // Simulate 1ms work
                std::this_thread::sleep_for(std::chrono::microseconds(1000));
                co_return 42;
            }();
            (void)result; // Suppress unused variable warning
            
            tasksCompleted.fetch_add(1); }());
    }

    const auto start = std::chrono::steady_clock::now();

    // Schedule all tasks
    for (auto& task : concurrentTasks)
    {
        scheduler->schedule(std::move(task));
    }

    // Wait for all tasks to complete
    while (tasksCompleted.load() < numConcurrentTasks)
    {
        scheduler->runExpiredTasks();
        std::this_thread::sleep_for(1ms);
    }

    const auto end      = std::chrono::steady_clock::now();
    const auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);

    const auto perTask = duration.count() / numConcurrentTasks;

    std::cout << "High Concurrency Stress Test:" << std::endl;
    std::cout << "=============================" << std::endl;
    std::cout << "Concurrent tasks: " << numConcurrentTasks << std::endl;
    std::cout << "Workers: " << numWorkers << std::endl;
    std::cout << "Total time: " << duration.count() << "μs" << std::endl;
    std::cout << "Per task: " << perTask << "μs" << std::endl;
    std::cout << "Expected work per task: 1000μs (1ms)" << std::endl;
    std::cout << "Overhead per task: " << (perTask - 1000) << "μs" << std::endl;
    std::cout << std::endl;

    std::cout << "STRESS TEST ANALYSIS:" << std::endl;
    std::cout << "  - Tests scheduler under high concurrency" << std::endl;
    std::cout << "  - Measures performance with many concurrent tasks" << std::endl;
    std::cout << "  - Shows how well the scheduler handles load" << std::endl;
    std::cout << "  - Helps identify concurrency bottlenecks" << std::endl;

    EXPECT_EQ(tasksCompleted.load(), numConcurrentTasks);
    EXPECT_GT(perTask, 1000) << "Should include the 1ms simulated work";
    EXPECT_LT(perTask, 10000) << "Overhead should be reasonable under stress";
}

//
// TEST: Memory Pressure Test
// PURPOSE: Test scheduler under memory pressure conditions
// BEHAVIOR: Creates tasks that allocate and deallocate memory
// EXPECTATION: Should maintain performance under memory pressure
//
TEST_F(SchedulerPressureTest, MemoryPressureTest)
{
    const int        numMemoryTasks = 500;
    std::atomic<int> tasksCompleted{ 0 };

    auto memoryTask = [&]() -> Task<void>
    {
        for (int i = 0; i < numMemoryTasks; ++i)
        {
            // Allocate memory in AsyncTask
            auto memoryResult = co_await [&]() -> AsyncTask<std::vector<int>>
            {
                // Allocate a large vector
                std::vector<int> largeVector(10000);
                for (int j = 0; j < 10000; ++j)
                {
                    largeVector[j] = j;
                }

                // Simulate some work
                std::this_thread::sleep_for(std::chrono::microseconds(100));

                co_return largeVector;
            }();
            (void)memoryResult; // Suppress unused variable warning

            tasksCompleted.fetch_add(1);
        }
    };

    const auto start = std::chrono::steady_clock::now();
    scheduler->schedule(std::move(memoryTask));

    while (tasksCompleted.load() < numMemoryTasks)
    {
        scheduler->runExpiredTasks();
        std::this_thread::sleep_for(1ms);
    }

    const auto end      = std::chrono::steady_clock::now();
    const auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);

    const auto perTask = duration.count() / numMemoryTasks;

    std::cout << "Memory Pressure Test:" << std::endl;
    std::cout << "====================" << std::endl;
    std::cout << "Memory tasks: " << numMemoryTasks << std::endl;
    std::cout << "Total time: " << duration.count() << "μs" << std::endl;
    std::cout << "Per task: " << perTask << "μs" << std::endl;
    std::cout << "Expected work per task: 100μs" << std::endl;
    std::cout << "Overhead per task: " << (perTask - 100) << "μs" << std::endl;
    std::cout << std::endl;

    std::cout << "MEMORY PRESSURE ANALYSIS:" << std::endl;
    std::cout << "  - Tests scheduler under memory allocation pressure" << std::endl;
    std::cout << "  - Each task allocates 40KB of memory" << std::endl;
    std::cout << "  - Measures performance impact of memory operations" << std::endl;
    std::cout << "  - Shows how well the scheduler handles memory pressure" << std::endl;

    EXPECT_EQ(tasksCompleted.load(), numMemoryTasks);
    EXPECT_GT(perTask, 100) << "Should include the 100μs simulated work";
    EXPECT_LT(perTask, 5000) << "Overhead should be reasonable under memory pressure";
}

//
// TEST: Mixed Workload Pressure Test
// PURPOSE: Test scheduler with mixed workload types
// BEHAVIOR: Creates a mix of different task types and complexities
// EXPECTATION: Should handle mixed workloads efficiently
//
TEST_F(SchedulerPressureTest, MixedWorkloadPressureTest)
{
    const int        numSimpleTasks  = 200;
    const int        numComplexTasks = 100;
    const int        numAsyncTasks   = 150;
    std::atomic<int> simpleCompleted{ 0 };
    std::atomic<int> complexCompleted{ 0 };
    std::atomic<int> asyncCompleted{ 0 };

    // Simple tasks (symmetric transfer only)
    auto simpleTask = [&]() -> Task<void>
    {
        for (int i = 0; i < numSimpleTasks; ++i)
        {
            auto result = co_await [&]() -> Task<int>
            {
                co_return 42;
            }();
            (void)result; // Suppress unused variable warning

            simpleCompleted.fetch_add(1);
        }
    };

    // Complex tasks (mixed Task and AsyncTask)
    auto complexTask = [&]() -> Task<void>
    {
        for (int i = 0; i < numComplexTasks; ++i)
        {
            auto result = co_await [&]() -> Task<int>
            {
                auto step1 = co_await [&]() -> Task<int>
                {
                    auto step2 = co_await [&]() -> AsyncTask<int>
                    {
                        std::this_thread::sleep_for(std::chrono::microseconds(500));
                        co_return 42;
                    }();

                    co_return step2 * 2;
                }();

                co_return step1 * 3;
            }();
            (void)result; // Suppress unused variable warning

            complexCompleted.fetch_add(1);
        }
    };

    // Pure AsyncTask operations
    auto asyncTask = [&]() -> Task<void>
    {
        for (int i = 0; i < numAsyncTasks; ++i)
        {
            auto result = co_await [&]() -> AsyncTask<int>
            {
                std::this_thread::sleep_for(std::chrono::microseconds(1000));
                co_return 42;
            }();
            (void)result; // Suppress unused variable warning

            asyncCompleted.fetch_add(1);
        }
    };

    const auto start = std::chrono::steady_clock::now();

    // Schedule all task types
    scheduler->schedule(std::move(simpleTask));
    scheduler->schedule(std::move(complexTask));
    scheduler->schedule(std::move(asyncTask));

    // Wait for all tasks to complete
    while (simpleCompleted.load() < numSimpleTasks ||
           complexCompleted.load() < numComplexTasks ||
           asyncCompleted.load() < numAsyncTasks)
    {
        scheduler->runExpiredTasks();
        std::this_thread::sleep_for(1ms);
    }

    const auto end      = std::chrono::steady_clock::now();
    const auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);

    const auto totalTasks = numSimpleTasks + numComplexTasks + numAsyncTasks;
    const auto perTask    = duration.count() / totalTasks;

    std::cout << "Mixed Workload Pressure Test:" << std::endl;
    std::cout << "============================" << std::endl;
    std::cout << "Simple tasks: " << numSimpleTasks << std::endl;
    std::cout << "Complex tasks: " << numComplexTasks << std::endl;
    std::cout << "Async tasks: " << numAsyncTasks << std::endl;
    std::cout << "Total tasks: " << totalTasks << std::endl;
    std::cout << "Total time: " << duration.count() << "μs" << std::endl;
    std::cout << "Per task: " << perTask << "μs" << std::endl;
    std::cout << std::endl;

    std::cout << "MIXED WORKLOAD ANALYSIS:" << std::endl;
    std::cout << "  - Tests scheduler with mixed task types" << std::endl;
    std::cout << "  - Simple tasks: pure symmetric transfer" << std::endl;
    std::cout << "  - Complex tasks: mixed Task and AsyncTask" << std::endl;
    std::cout << "  - Async tasks: pure AsyncTask operations" << std::endl;
    std::cout << "  - Shows how well the scheduler handles diverse workloads" << std::endl;

    EXPECT_EQ(simpleCompleted.load(), numSimpleTasks);
    EXPECT_EQ(complexCompleted.load(), numComplexTasks);
    EXPECT_EQ(asyncCompleted.load(), numAsyncTasks);
    EXPECT_GT(perTask, 0) << "Should have measurable performance";
    EXPECT_LT(perTask, 10000) << "Overhead should be reasonable for mixed workload";
}
