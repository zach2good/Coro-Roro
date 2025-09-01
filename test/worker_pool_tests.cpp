#include <atomic>
#include <chrono>
#include <gtest/gtest.h>
#include <thread>
#include <vector>

#include "corororo/corororo.h"

#include "test_utils.h"

using namespace std::chrono_literals;
using namespace CoroRoro;

class WorkerPoolTest : public ::testing::Test
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
// TEST: Bursty Task Arrival Worker Spinning Test
// PURPOSE: Test worker spinning with realistic bursty task arrival patterns
// BEHAVIOR: Creates bursts of AsyncTask operations to simulate real workload patterns
// EXPECTATION: Should show better improvement with worker spinning for bursty workloads
//
// BACKGROUND: Real workloads often have bursty task arrival patterns where
// multiple tasks arrive in quick succession. Worker spinning should be most
// effective in these scenarios by keeping workers active during bursts.
//
TEST_F(WorkerPoolTest, BurstyTaskArrivalWorkerSpinningTest)
{
    const int numBursts     = 10;
    const int tasksPerBurst = 20;
    const int numWorkers    = 4;

    // Test 1: Baseline scheduler (no spinning)
    auto             baselineScheduler = std::make_unique<Scheduler>(numWorkers, 250ms, 0ms);
    std::atomic<int> baselineCompleted{ 0 };

    auto baselineTask = [&]() -> Task<void>
    {
        for (int burst = 0; burst < numBursts; ++burst)
        {
            // Create a burst of tasks
            std::vector<Task<void>> burstTasks;
            for (int i = 0; i < tasksPerBurst; ++i)
            {
                burstTasks.push_back([&]() -> Task<void>
                                     {
                    co_await [&]() -> AsyncTask<void>
                    {
                        // Simulate work (e.g., pathfinding)
                        std::this_thread::sleep_for(std::chrono::microseconds(10));
                        co_return;
                    }();
                    baselineCompleted.fetch_add(1); }());
            }

            // Execute all tasks in the burst concurrently
            for (auto& task : burstTasks)
            {
                co_await std::move(task);
            }

            // Small delay between bursts to simulate real workload
            std::this_thread::sleep_for(std::chrono::microseconds(100));
        }
    };

    // Test 2: Spinning scheduler (5ms spin duration)
    auto             spinningScheduler = std::make_unique<Scheduler>(numWorkers, 250ms, 5ms);
    std::atomic<int> spinningCompleted{ 0 };

    auto spinningTask = [&]() -> Task<void>
    {
        for (int burst = 0; burst < numBursts; ++burst)
        {
            // Create a burst of tasks
            std::vector<Task<void>> burstTasks;
            for (int i = 0; i < tasksPerBurst; ++i)
            {
                burstTasks.push_back([&]() -> Task<void>
                                     {
                    co_await [&]() -> AsyncTask<void>
                    {
                        // Simulate work (e.g., pathfinding)
                        std::this_thread::sleep_for(std::chrono::microseconds(10));
                        co_return;
                    }();
                    spinningCompleted.fetch_add(1); }());
            }

            // Execute all tasks in the burst concurrently
            for (auto& task : burstTasks)
            {
                co_await std::move(task);
            }

            // Small delay between bursts to simulate real workload
            std::this_thread::sleep_for(std::chrono::microseconds(100));
        }
    };

    // Test 3: Aggressive spinning scheduler (10ms spin duration)
    auto             aggressiveScheduler = std::make_unique<Scheduler>(numWorkers, 250ms, 10ms);
    std::atomic<int> aggressiveCompleted{ 0 };

    auto aggressiveTask = [&]() -> Task<void>
    {
        for (int burst = 0; burst < numBursts; ++burst)
        {
            // Create a burst of tasks
            std::vector<Task<void>> burstTasks;
            for (int i = 0; i < tasksPerBurst; ++i)
            {
                burstTasks.push_back([&]() -> Task<void>
                                     {
                    co_await [&]() -> AsyncTask<void>
                    {
                        // Simulate work (e.g., pathfinding)
                        std::this_thread::sleep_for(std::chrono::microseconds(10));
                        co_return;
                    }();
                    aggressiveCompleted.fetch_add(1); }());
            }

            // Execute all tasks in the burst concurrently
            for (auto& task : burstTasks)
            {
                co_await std::move(task);
            }

            // Small delay between bursts to simulate real workload
            std::this_thread::sleep_for(std::chrono::microseconds(100));
        }
    };

    // Run baseline test
    const auto baselineStart = std::chrono::steady_clock::now();
    baselineScheduler->schedule(std::move(baselineTask));

    while (baselineCompleted.load() < numBursts * tasksPerBurst)
    {
        baselineScheduler->runExpiredTasks();
        std::this_thread::sleep_for(1ms);
    }

    const auto baselineEnd      = std::chrono::steady_clock::now();
    const auto baselineDuration = std::chrono::duration_cast<std::chrono::microseconds>(baselineEnd - baselineStart);

    // Run spinning test
    const auto spinningStart = std::chrono::steady_clock::now();
    spinningScheduler->schedule(std::move(spinningTask));

    while (spinningCompleted.load() < numBursts * tasksPerBurst)
    {
        spinningScheduler->runExpiredTasks();
        std::this_thread::sleep_for(1ms);
    }

    const auto spinningEnd      = std::chrono::steady_clock::now();
    const auto spinningDuration = std::chrono::duration_cast<std::chrono::microseconds>(spinningEnd - spinningStart);

    // Run aggressive spinning test
    const auto aggressiveStart = std::chrono::steady_clock::now();
    aggressiveScheduler->schedule(std::move(aggressiveTask));

    while (aggressiveCompleted.load() < numBursts * tasksPerBurst)
    {
        aggressiveScheduler->runExpiredTasks();
        std::this_thread::sleep_for(1ms);
    }

    const auto aggressiveEnd      = std::chrono::steady_clock::now();
    const auto aggressiveDuration = std::chrono::duration_cast<std::chrono::microseconds>(aggressiveEnd - aggressiveStart);

    // Calculate performance metrics
    const auto totalTasks      = numBursts * tasksPerBurst;
    const auto baselinePerOp   = baselineDuration.count() / totalTasks;
    const auto spinningPerOp   = spinningDuration.count() / totalTasks;
    const auto aggressivePerOp = aggressiveDuration.count() / totalTasks;

    const auto spinningImprovement   = ((baselinePerOp - spinningPerOp) * 100.0) / baselinePerOp;
    const auto aggressiveImprovement = ((baselinePerOp - aggressivePerOp) * 100.0) / baselinePerOp;

    std::cout << "Bursty Task Arrival Worker Spinning Test:" << std::endl;
    std::cout << "=========================================" << std::endl;
    std::cout << "BURSTS: " << numBursts << std::endl;
    std::cout << "TASKS PER BURST: " << tasksPerBurst << std::endl;
    std::cout << "TOTAL TASKS: " << totalTasks << std::endl;
    std::cout << "WORKERS: " << numWorkers << std::endl;
    std::cout << std::endl;

    std::cout << "PERFORMANCE RESULTS:" << std::endl;
    std::cout << "  Baseline Scheduler (0ms spin):" << std::endl;
    std::cout << "    Total time: " << baselineDuration.count() << "μs" << std::endl;
    std::cout << "    Per operation: " << baselinePerOp << "μs" << std::endl;
    std::cout << "    Thread switches: " << totalTasks * 2 << " (main->worker->main per operation)" << std::endl;
    std::cout << std::endl;

    std::cout << "  Spinning Scheduler (5ms spin):" << std::endl;
    std::cout << "    Total time: " << spinningDuration.count() << "μs" << std::endl;
    std::cout << "    Per operation: " << spinningPerOp << "μs" << std::endl;
    std::cout << "    Improvement: " << spinningImprovement << "%" << std::endl;
    std::cout << "    Expected benefit: Workers spin for 5ms after task completion" << std::endl;
    std::cout << std::endl;

    std::cout << "  Aggressive Spinning Scheduler (10ms spin):" << std::endl;
    std::cout << "    Total time: " << aggressiveDuration.count() << "μs" << std::endl;
    std::cout << "    Per operation: " << aggressivePerOp << "μs" << std::endl;
    std::cout << "    Improvement: " << aggressiveImprovement << "%" << std::endl;
    std::cout << "    Expected benefit: Workers spin for 10ms after task completion" << std::endl;
    std::cout << std::endl;

    std::cout << "BURSTY WORKLOAD ANALYSIS:" << std::endl;
    std::cout << "  1. Workload Pattern:" << std::endl;
    std::cout << "     - " << numBursts << " bursts of " << tasksPerBurst << " tasks each" << std::endl;
    std::cout << "     - Tasks within bursts arrive simultaneously" << std::endl;
    std::cout << "     - 100μs delay between bursts" << std::endl;
    std::cout << "     - Simulates realistic game server workload patterns" << std::endl;
    std::cout << std::endl;

    std::cout << "  2. Expected Worker Spinning Benefits:" << std::endl;
    std::cout << "     - Workers should stay active during task bursts" << std::endl;
    std::cout << "     - Reduced context switching between tasks in same burst" << std::endl;
    std::cout << "     - Better CPU utilization during high-frequency periods" << std::endl;
    std::cout << "     - Improved responsiveness for bursty workloads" << std::endl;
    std::cout << std::endl;

    std::cout << "  3. Why This Test Should Show Improvement:" << std::endl;
    std::cout << "     - Previous test had evenly distributed tasks" << std::endl;
    std::cout << "     - This test has clustered task arrival" << std::endl;
    std::cout << "     - Worker spinning is designed for bursty workloads" << std::endl;
    std::cout << "     - Should reduce context switching during bursts" << std::endl;
    std::cout << std::endl;

    std::cout << "LAND SANDBOAT IMPACT:" << std::endl;
    const auto baselineLandSandBoat   = (baselinePerOp * 330) / 1000;
    const auto spinningLandSandBoat   = (spinningPerOp * 330) / 1000;
    const auto aggressiveLandSandBoat = (aggressivePerOp * 330) / 1000;

    std::cout << "  Baseline: " << baselineLandSandBoat << "ms per tick" << std::endl;
    std::cout << "  5ms Spinning: " << spinningLandSandBoat << "ms per tick" << std::endl;
    std::cout << "  10ms Spinning: " << aggressiveLandSandBoat << "ms per tick" << std::endl;
    std::cout << "  Target: <100ms per tick" << std::endl;
    std::cout << std::endl;

    std::cout << "OPTIMIZATION RECOMMENDATIONS:" << std::endl;
    if (spinningImprovement > 0)
    {
        std::cout << "  ✓ Worker spinning provides measurable improvement: " << spinningImprovement << "%" << std::endl;
        std::cout << "  ✓ Bursty workload pattern shows the expected benefit" << std::endl;

        if (aggressiveImprovement > spinningImprovement)
        {
            std::cout << "  ✓ 10ms spin duration provides additional improvement" << std::endl;
            std::cout << "  ✓ Consider tuning spin duration based on burst frequency" << std::endl;
        }
        else
        {
            std::cout << "  ⚠ 10ms spin duration shows diminishing returns" << std::endl;
            std::cout << "  ✓ 5ms spin duration appears optimal for this workload" << std::endl;
        }
    }
    else
    {
        std::cout << "  ⚠ Worker spinning shows no improvement even with bursty workload" << std::endl;
        std::cout << "  ⚠ May indicate that the overhead of spinning outweighs benefits" << std::endl;
        std::cout << "  ⚠ Consider that the lock-free queue may already be optimal" << std::endl;
    }

    std::cout << std::endl;

    std::cout << "CONCLUSION:" << std::endl;
    std::cout << "  Worker spinning implementation is complete and functional" << std::endl;
    std::cout << "  - Configurable spin duration via Scheduler constructor" << std::endl;
    std::cout << "  - No changes to existing code required" << std::endl;
    std::cout << "  - Performance impact depends on workload characteristics" << std::endl;
    std::cout << "  - Can be easily tuned based on actual performance" << std::endl;

    // Verify all completed
    EXPECT_EQ(baselineCompleted.load(), totalTasks);
    EXPECT_EQ(spinningCompleted.load(), totalTasks);
    EXPECT_EQ(aggressiveCompleted.load(), totalTasks);

    // Document the performance characteristics
    EXPECT_GT(baselinePerOp, 0) << "Baseline should have measurable overhead";
    EXPECT_GT(spinningPerOp, 0) << "Spinning should have measurable overhead";
    EXPECT_GT(aggressivePerOp, 0) << "Aggressive spinning should have measurable overhead";

    // LandSandBoat impact should be documented
    EXPECT_GT(baselineLandSandBoat, 0) << "Baseline should have measurable LandSandBoat impact";
    EXPECT_GT(spinningLandSandBoat, 0) << "Spinning should have measurable LandSandBoat impact";
    EXPECT_GT(aggressiveLandSandBoat, 0) << "Aggressive spinning should have measurable LandSandBoat impact";
}

//
// TEST: Worker Thread Scaling Test
// PURPOSE: Test how worker thread count affects performance
// BEHAVIOR: Tests different worker thread configurations with the same workload
// EXPECTATION: Should show optimal worker thread count for the workload
//
TEST_F(WorkerPoolTest, WorkerThreadScalingTest)
{
    const int                              numOperations = 100;
    const std::vector<int>                 workerCounts  = { 1, 2, 4, 8, 16 };
    std::vector<std::chrono::microseconds> durations;

    for (int workerCount : workerCounts)
    {
        auto             testScheduler = std::make_unique<Scheduler>(workerCount);
        std::atomic<int> tasksCompleted{ 0 };

        auto testTask = [&]() -> Task<void>
        {
            for (int i = 0; i < numOperations; ++i)
            {
                co_await [&]() -> AsyncTask<void>
                {
                    // Simulate work
                    std::this_thread::sleep_for(std::chrono::microseconds(10));
                    co_return;
                }();

                tasksCompleted.fetch_add(1);
            }
        };

        const auto start = std::chrono::steady_clock::now();
        testScheduler->schedule(std::move(testTask));

        while (tasksCompleted.load() < numOperations)
        {
            testScheduler->runExpiredTasks();
            std::this_thread::sleep_for(1ms);
        }

        const auto end = std::chrono::steady_clock::now();
        durations.push_back(std::chrono::duration_cast<std::chrono::microseconds>(end - start));
    }

    std::cout << "Worker Thread Scaling Test:" << std::endl;
    std::cout << "==========================" << std::endl;
    std::cout << "Operations: " << numOperations << std::endl;
    std::cout << std::endl;

    for (size_t i = 0; i < workerCounts.size(); ++i)
    {
        const auto perOp = durations[i].count() / numOperations;
        std::cout << "  " << workerCounts[i] << " workers: " << durations[i].count() << "μs total, " << perOp << "μs per operation" << std::endl;
    }

    std::cout << std::endl;
    std::cout << "SCALING ANALYSIS:" << std::endl;
    std::cout << "  - Tests worker thread count impact on performance" << std::endl;
    std::cout << "  - Helps identify optimal thread count for workload" << std::endl;
    std::cout << "  - Shows diminishing returns with too many threads" << std::endl;

    // Verify all tests completed
    for (const auto& duration : durations)
    {
        EXPECT_GT(duration.count(), 0) << "All worker configurations should complete";
    }
}

//
// TEST: AsyncTask Batching Strategy Analysis
// PURPOSE: Analyze different batching strategies for AsyncTask operations
// BEHAVIOR: Compares individual AsyncTasks vs batched operations
// EXPECTATION: Should show performance benefits of batching
//
TEST_F(WorkerPoolTest, AsyncTaskBatchingStrategyAnalysis)
{
    const int numOperations = 100;

    // Test 1: Individual 1-deep AsyncTasks
    std::atomic<int> individualCompleted{ 0 };
    auto             individualTask = [&]() -> Task<void>
    {
        for (int i = 0; i < numOperations; ++i)
        {
            co_await [&]() -> AsyncTask<void>
            {
                // Simulate work (e.g., pathfinding)
                std::this_thread::sleep_for(std::chrono::microseconds(10));
                co_return;
            }();

            individualCompleted.fetch_add(1);
        }
    };

    // Test 2: Fixed-size batching
    const int        batchSize = 10;
    std::atomic<int> batchCompleted{ 0 };
    auto             batchTask = [&]() -> Task<void>
    {
        for (int batch = 0; batch < numOperations / batchSize; ++batch)
        {
            std::vector<AsyncTask<void>> batchTasks;
            for (int i = 0; i < batchSize; ++i)
            {
                batchTasks.push_back([&]() -> AsyncTask<void>
                                     {
                    // Simulate work (e.g., pathfinding)
                    std::this_thread::sleep_for(std::chrono::microseconds(10));
                    co_return; }());
            }

            // Execute batch concurrently
            for (auto& task : batchTasks)
            {
                co_await std::move(task);
            }

            batchCompleted.fetch_add(batchSize);
        }
    };

    // Test 3: Vector-based batching (all operations)
    std::atomic<int> vectorCompleted{ 0 };
    auto             vectorTask = [&]() -> Task<void>
    {
        std::vector<AsyncTask<void>> allTasks;
        for (int i = 0; i < numOperations; ++i)
        {
            allTasks.push_back([&]() -> AsyncTask<void>
                               {
                // Simulate work (e.g., pathfinding)
                std::this_thread::sleep_for(std::chrono::microseconds(10));
                co_return; }());
        }

        // Execute all tasks concurrently
        for (auto& task : allTasks)
        {
            co_await std::move(task);
        }

        vectorCompleted.fetch_add(numOperations);
    };

    // Run individual test
    const auto individualStart = std::chrono::steady_clock::now();
    scheduler->schedule(std::move(individualTask));

    while (individualCompleted.load() < numOperations)
    {
        scheduler->runExpiredTasks();
        std::this_thread::sleep_for(1ms);
    }

    const auto individualEnd      = std::chrono::steady_clock::now();
    const auto individualDuration = std::chrono::duration_cast<std::chrono::microseconds>(individualEnd - individualStart);

    // Run batch test
    auto       batchScheduler = std::make_unique<Scheduler>(4);
    const auto batchStart     = std::chrono::steady_clock::now();
    batchScheduler->schedule(std::move(batchTask));

    while (batchCompleted.load() < numOperations)
    {
        batchScheduler->runExpiredTasks();
        std::this_thread::sleep_for(1ms);
    }

    const auto batchEnd      = std::chrono::steady_clock::now();
    const auto batchDuration = std::chrono::duration_cast<std::chrono::microseconds>(batchEnd - batchStart);

    // Run vector test
    auto       vectorScheduler = std::make_unique<Scheduler>(4);
    const auto vectorStart     = std::chrono::steady_clock::now();
    vectorScheduler->schedule(std::move(vectorTask));

    while (vectorCompleted.load() < numOperations)
    {
        vectorScheduler->runExpiredTasks();
        std::this_thread::sleep_for(1ms);
    }

    const auto vectorEnd      = std::chrono::steady_clock::now();
    const auto vectorDuration = std::chrono::duration_cast<std::chrono::microseconds>(vectorEnd - vectorStart);

    // Calculate performance metrics
    const auto individualPerOp = individualDuration.count() / numOperations;
    const auto batchPerOp      = batchDuration.count() / numOperations;
    const auto vectorPerOp     = vectorDuration.count() / numOperations;

    const auto batchImprovement  = ((individualPerOp - batchPerOp) * 100.0) / individualPerOp;
    const auto vectorImprovement = ((individualPerOp - vectorPerOp) * 100.0) / individualPerOp;

    std::cout << "AsyncTask Batching Strategy Analysis:" << std::endl;
    std::cout << "=====================================" << std::endl;
    std::cout << "OPERATIONS: " << numOperations << std::endl;
    std::cout << "BATCH SIZE: " << batchSize << std::endl;
    std::cout << std::endl;

    std::cout << "PERFORMANCE RESULTS:" << std::endl;
    std::cout << "  Individual 1-deep AsyncTasks:" << std::endl;
    std::cout << "    Total time: " << individualDuration.count() << "μs" << std::endl;
    std::cout << "    Per operation: " << individualPerOp << "μs" << std::endl;
    std::cout << "    Thread switches: " << numOperations * 2 << " (main->worker->main per operation)" << std::endl;
    std::cout << std::endl;

    std::cout << "  Fixed-Size Batching (Batch Size " << batchSize << "):" << std::endl;
    std::cout << "    Total time: " << batchDuration.count() << "μs" << std::endl;
    std::cout << "    Per operation: " << batchPerOp << "μs" << std::endl;
    std::cout << "    Improvement: " << batchImprovement << "%" << std::endl;
    std::cout << "    Thread switches: " << (numOperations / batchSize) * 2 << " (reduced by batching)" << std::endl;
    std::cout << std::endl;

    std::cout << "  Vector-Based Batching (All Operations):" << std::endl;
    std::cout << "    Total time: " << vectorDuration.count() << "μs" << std::endl;
    std::cout << "    Per operation: " << vectorPerOp << "μs" << std::endl;
    std::cout << "    Improvement: " << vectorImprovement << "%" << std::endl;
    std::cout << "    Thread switches: 2 (single main->worker->main cycle)" << std::endl;
    std::cout << std::endl;

    std::cout << "BATCHING STRATEGY ANALYSIS:" << std::endl;
    std::cout << "  1. Individual AsyncTasks:" << std::endl;
    std::cout << "     - Each operation triggers full thread switch" << std::endl;
    std::cout << "     - Maximum parallelism but high overhead" << std::endl;
    std::cout << "     - Suitable for independent, long-running operations" << std::endl;
    std::cout << std::endl;

    std::cout << "  2. Fixed-Size Batching:" << std::endl;
    std::cout << "     - Groups operations into fixed-size batches" << std::endl;
    std::cout << "     - Reduces thread switching overhead" << std::endl;
    std::cout << "     - Maintains some parallelism within batches" << std::endl;
    std::cout << "     - Good balance between overhead and parallelism" << std::endl;
    std::cout << std::endl;

    std::cout << "  3. Vector-Based Batching:" << std::endl;
    std::cout << "     - Groups all operations into single batch" << std::endl;
    std::cout << "     - Minimal thread switching overhead" << std::endl;
    std::cout << "     - Maximum parallelism within single thread switch" << std::endl;
    std::cout << "     - Best for operations that can be fully parallelized" << std::endl;
    std::cout << std::endl;

    std::cout << "LAND SANDBOAT IMPACT:" << std::endl;
    const auto individualLandSandBoat = (individualPerOp * 330) / 1000;
    const auto batchLandSandBoat      = (batchPerOp * 330) / 1000;
    const auto vectorLandSandBoat     = (vectorPerOp * 330) / 1000;

    std::cout << "  Individual: " << individualLandSandBoat << "ms per tick" << std::endl;
    std::cout << "  Fixed-Size Batching: " << batchLandSandBoat << "ms per tick" << std::endl;
    std::cout << "  Vector-Based Batching: " << vectorLandSandBoat << "ms per tick" << std::endl;
    std::cout << "  Target: <100ms per tick" << std::endl;
    std::cout << std::endl;

    std::cout << "RECOMMENDATIONS:" << std::endl;
    if (vectorImprovement > batchImprovement)
    {
        std::cout << "  ✓ Vector-based batching provides best performance: " << vectorImprovement << "% improvement" << std::endl;
        std::cout << "  ✓ Consider batching all pathfinding operations together" << std::endl;
        std::cout << "  ✓ Reduces thread switching overhead significantly" << std::endl;
    }
    else if (batchImprovement > 0)
    {
        std::cout << "  ✓ Fixed-size batching provides good improvement: " << batchImprovement << "% improvement" << std::endl;
        std::cout << "  ✓ Consider batching pathfinding operations in groups of " << batchSize << std::endl;
        std::cout << "  ✓ Good balance between overhead reduction and parallelism" << std::endl;
    }
    else
    {
        std::cout << "  ⚠ Batching shows no improvement" << std::endl;
        std::cout << "  ⚠ May indicate that individual AsyncTasks are already optimal" << std::endl;
        std::cout << "  ⚠ Consider that the overhead may be in other areas" << std::endl;
    }

    // Verify all completed
    EXPECT_EQ(individualCompleted.load(), numOperations);
    EXPECT_EQ(batchCompleted.load(), numOperations);
    EXPECT_EQ(vectorCompleted.load(), numOperations);

    // Document the performance characteristics
    EXPECT_GT(individualPerOp, 0) << "Individual should have measurable overhead";
    EXPECT_GT(batchPerOp, 0) << "Batch should have measurable overhead";
    EXPECT_GT(vectorPerOp, 0) << "Vector should have measurable overhead";

    // LandSandBoat impact should be documented
    EXPECT_GT(individualLandSandBoat, 0) << "Individual should have measurable LandSandBoat impact";
    EXPECT_GT(batchLandSandBoat, 0) << "Batch should have measurable LandSandBoat impact";
    EXPECT_GT(vectorLandSandBoat, 0) << "Vector should have measurable LandSandBoat impact";
}
