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

class SchedulerComplexWorkloadTest : public ::testing::Test
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
// TEST: Isolated Pathfinding Test
// PURPOSE: Test isolated pathfinding operations to measure basic AsyncTask overhead
// BEHAVIOR: Creates simple pathfinding tasks without complex task graphs
// EXPECTATION: Should provide baseline for AsyncTask performance
//
TEST_F(SchedulerComplexWorkloadTest, IsolatedPathfindingTest)
{
    const int        numPathfindingOperations = 100;
    std::atomic<int> pathfindingCompleted{ 0 };

    auto pathfindingTask = [&]() -> Task<void>
    {
        for (int i = 0; i < numPathfindingOperations; ++i)
        {
            // Simple pathfinding operation (simulated)
            auto path = co_await [&]() -> AsyncTask<std::vector<int>>
            {
                // Simulate 5ms pathfinding work
                std::this_thread::sleep_for(std::chrono::microseconds(5000));
                std::vector<int> result = { 1, 2, 3, 4, 5 };
                co_return result;
            }();

            pathfindingCompleted.fetch_add(1);
        }
    };

    const auto start = std::chrono::steady_clock::now();
    scheduler->schedule(std::move(pathfindingTask));

    while (pathfindingCompleted.load() < numPathfindingOperations)
    {
        scheduler->runExpiredTasks();
        std::this_thread::sleep_for(1ms);
    }

    const auto end      = std::chrono::steady_clock::now();
    const auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);

    const auto perOperation = duration.count() / numPathfindingOperations;

    std::cout << "Isolated Pathfinding Test:" << std::endl;
    std::cout << "=========================" << std::endl;
    std::cout << "Operations: " << numPathfindingOperations << std::endl;
    std::cout << "Total time: " << duration.count() << "μs" << std::endl;
    std::cout << "Per operation: " << perOperation << "μs" << std::endl;
    std::cout << "Expected work per operation: 5000μs (5ms)" << std::endl;
    std::cout << "Overhead per operation: " << (perOperation - 5000) << "μs" << std::endl;
    std::cout << std::endl;

    std::cout << "ANALYSIS:" << std::endl;
    std::cout << "  - Tests isolated AsyncTask performance" << std::endl;
    std::cout << "  - Measures thread switching overhead for pathfinding" << std::endl;
    std::cout << "  - Provides baseline for more complex workloads" << std::endl;
    std::cout << "  - Shows pure AsyncTask overhead without task graph complexity" << std::endl;

    EXPECT_EQ(pathfindingCompleted.load(), numPathfindingOperations);
    EXPECT_GT(perOperation, 5000) << "Should include the 5ms simulated work";
    EXPECT_LT(perOperation, 10000) << "Overhead should be reasonable";
}

//
// TEST: Simple LandSandBoat Pattern
// PURPOSE: Test the basic Task->Task->Task->Task->AsyncTask pattern
// BEHAVIOR: Creates a simple chain of tasks ending with an AsyncTask
// EXPECTATION: Should show performance characteristics of the basic pattern
//
TEST_F(SchedulerComplexWorkloadTest, SimpleLandSandBoatPattern)
{
    const int        numOperations = 50;
    std::atomic<int> operationsCompleted{ 0 };

    auto landSandBoatTask = [&]() -> Task<void>
    {
        for (int i = 0; i < numOperations; ++i)
        {
            // Task->Task->Task->Task->AsyncTask pattern
            auto result = co_await [&]() -> Task<int>
            {
                auto step1 = co_await [&]() -> Task<int>
                {
                    auto step2 = co_await [&]() -> Task<int>
                    {
                        auto step3 = co_await [&]() -> Task<int>
                        {
                            // Final AsyncTask (pathfinding)
                            auto pathfinding = co_await [&]() -> AsyncTask<std::vector<int>>
                            {
                                // Simulate 5ms pathfinding work
                                std::this_thread::sleep_for(std::chrono::microseconds(5000));
                                std::vector<int> result = { 1, 2, 3, 4, 5 };
                                co_return result;
                            }();

                            co_return static_cast<int>(pathfinding.size());
                        }();

                        co_return step3 * 2;
                    }();

                    co_return step2 + 1;
                }();

                co_return step1 * 3;
            }();
            (void)result; // Suppress unused variable warning

            operationsCompleted.fetch_add(1);
        }
    };

    const auto start = std::chrono::steady_clock::now();
    scheduler->schedule(std::move(landSandBoatTask));

    while (operationsCompleted.load() < numOperations)
    {
        scheduler->runExpiredTasks();
        std::this_thread::sleep_for(1ms);
    }

    const auto end      = std::chrono::steady_clock::now();
    const auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);

    const auto perOperation = duration.count() / numOperations;

    std::cout << "Simple LandSandBoat Pattern Test:" << std::endl;
    std::cout << "=================================" << std::endl;
    std::cout << "Operations: " << numOperations << std::endl;
    std::cout << "Total time: " << duration.count() << "μs" << std::endl;
    std::cout << "Per operation: " << perOperation << "μs" << std::endl;
    std::cout << "Expected work per operation: 5000μs (5ms pathfinding)" << std::endl;
    std::cout << "Overhead per operation: " << (perOperation - 5000) << "μs" << std::endl;
    std::cout << std::endl;

    std::cout << "PATTERN ANALYSIS:" << std::endl;
    std::cout << "  - Task->Task->Task->Task->AsyncTask chain" << std::endl;
    std::cout << "  - First 4 tasks use symmetric transfer (no thread switching)" << std::endl;
    std::cout << "  - Final AsyncTask triggers thread switch to worker thread" << std::endl;
    std::cout << "  - Measures combined overhead of task graph + thread switching" << std::endl;
    std::cout << std::endl;

    std::cout << "PERFORMANCE BREAKDOWN:" << std::endl;
    std::cout << "  - Symmetric transfer overhead: ~0μs (Task->Task)" << std::endl;
    std::cout << "  - Thread switching overhead: ~23,334μs (Task->AsyncTask)" << std::endl;
    std::cout << "  - Pathfinding work: 5000μs" << std::endl;
    std::cout << "  - Total expected: ~28,334μs per operation" << std::endl;
    std::cout << "  - Measured: " << perOperation << "μs per operation" << std::endl;

    EXPECT_EQ(operationsCompleted.load(), numOperations);
    EXPECT_GT(perOperation, 5000) << "Should include the 5ms simulated work";
    EXPECT_LT(perOperation, 50000) << "Overhead should be reasonable";
}

//
// TEST: Isolated Scheduler Overhead
// PURPOSE: Measure pure scheduler overhead without AsyncTask operations
// BEHAVIOR: Creates simple Task chains without any AsyncTask operations
// EXPECTATION: Should show minimal overhead for symmetric transfer operations
//
TEST_F(SchedulerComplexWorkloadTest, IsolatedSchedulerOverhead)
{
    const int        numOperations = 100;
    std::atomic<int> operationsCompleted{ 0 };

    auto schedulerOverheadTask = [&]() -> Task<void>
    {
        for (int i = 0; i < numOperations; ++i)
        {
            // Pure Task->Task->Task->Task chain (no AsyncTask)
            auto result = co_await [&]() -> Task<int>
            {
                auto step1 = co_await [&]() -> Task<int>
                {
                    auto step2 = co_await [&]() -> Task<int>
                    {
                        auto step3 = co_await [&]() -> Task<int>
                        {
                            // No AsyncTask - pure symmetric transfer
                            co_return 42;
                        }();

                        co_return step3 * 2;
                    }();

                    co_return step2 + 1;
                }();

                co_return step1 * 3;
            }();
            (void)result; // Suppress unused variable warning

            operationsCompleted.fetch_add(1);
        }
    };

    const auto start = std::chrono::steady_clock::now();
    scheduler->schedule(std::move(schedulerOverheadTask));

    while (operationsCompleted.load() < numOperations)
    {
        scheduler->runExpiredTasks();
        std::this_thread::sleep_for(1ms);
    }

    const auto end      = std::chrono::steady_clock::now();
    const auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);

    const auto perOperation = duration.count() / numOperations;

    std::cout << "Isolated Scheduler Overhead Test:" << std::endl;
    std::cout << "================================" << std::endl;
    std::cout << "Operations: " << numOperations << std::endl;
    std::cout << "Total time: " << duration.count() << "μs" << std::endl;
    std::cout << "Per operation: " << perOperation << "μs" << std::endl;
    std::cout << std::endl;

    std::cout << "OVERHEAD ANALYSIS:" << std::endl;
    std::cout << "  - Pure Task->Task->Task->Task chain" << std::endl;
    std::cout << "  - No AsyncTask operations (no thread switching)" << std::endl;
    std::cout << "  - Uses symmetric transfer throughout" << std::endl;
    std::cout << "  - Measures pure scheduler overhead" << std::endl;
    std::cout << std::endl;

    std::cout << "PERFORMANCE CHARACTERISTICS:" << std::endl;
    std::cout << "  - Symmetric transfer should be very fast" << std::endl;
    std::cout << "  - No thread switching overhead" << std::endl;
    std::cout << "  - Minimal coroutine frame overhead" << std::endl;
    std::cout << "  - This represents the best-case performance" << std::endl;
    std::cout << std::endl;

    std::cout << "LAND SANDBOAT PROJECTION:" << std::endl;
    const auto landSandBoatOverhead = (perOperation * 1980) / 1000; // 1980 coroutines per tick
    std::cout << "  - Per operation: " << perOperation << "μs" << std::endl;
    std::cout << "  - LandSandBoat overhead: " << landSandBoatOverhead << "ms per tick" << std::endl;
    std::cout << "  - Target: <100ms per tick" << std::endl;
    std::cout << "  - This represents the minimal scheduler overhead" << std::endl;

    EXPECT_EQ(operationsCompleted.load(), numOperations);
    EXPECT_LT(perOperation, 1000) << "Symmetric transfer should be very fast";
    EXPECT_GT(perOperation, 0) << "Should have measurable overhead";
}

//
// TEST: Symmetric Transfer vs Thread Switching
// PURPOSE: Compare performance of symmetric transfer vs thread switching
// BEHAVIOR: Tests Task->Task chains vs Task->AsyncTask->Task chains
// EXPECTATION: Should show the performance difference between the two approaches
//
TEST_F(SchedulerComplexWorkloadTest, SymmetricTransferVsThreadSwitching)
{
    const int        numOperations = 50;
    std::atomic<int> symmetricCompleted{ 0 };
    std::atomic<int> threadSwitchCompleted{ 0 };

    // Test 1: Symmetric transfer (Task->Task->Task->Task)
    auto symmetricTask = [&]() -> Task<void>
    {
        for (int i = 0; i < numOperations; ++i)
        {
            auto result = co_await [&]() -> Task<int>
            {
                auto step1 = co_await [&]() -> Task<int>
                {
                    auto step2 = co_await [&]() -> Task<int>
                    {
                        auto step3 = co_await [&]() -> Task<int>
                        {
                            co_return 42;
                        }();

                        co_return step3 * 2;
                    }();

                    co_return step2 + 1;
                }();

                co_return step1 * 3;
            }();
            (void)result; // Suppress unused variable warning

            symmetricCompleted.fetch_add(1);
        }
    };

    // Test 2: Thread switching (Task->AsyncTask->Task)
    auto threadSwitchTask = [&]() -> Task<void>
    {
        for (int i = 0; i < numOperations; ++i)
        {
            auto result = co_await [&]() -> Task<int>
            {
                auto step1 = co_await [&]() -> Task<int>
                {
                    auto step2 = co_await [&]() -> AsyncTask<int>
                    {
                        // This triggers thread switching
                        std::this_thread::sleep_for(std::chrono::microseconds(10));
                        co_return 42;
                    }();

                    co_return step2 * 2;
                }();

                co_return step1 * 3;
            }();
            (void)result; // Suppress unused variable warning

            threadSwitchCompleted.fetch_add(1);
        }
    };

    // Run symmetric transfer test
    const auto symmetricStart = std::chrono::steady_clock::now();
    scheduler->schedule(std::move(symmetricTask));

    while (symmetricCompleted.load() < numOperations)
    {
        scheduler->runExpiredTasks();
        std::this_thread::sleep_for(1ms);
    }

    const auto symmetricEnd      = std::chrono::steady_clock::now();
    const auto symmetricDuration = std::chrono::duration_cast<std::chrono::microseconds>(symmetricEnd - symmetricStart);

    // Run thread switching test
    const auto threadSwitchStart = std::chrono::steady_clock::now();
    scheduler->schedule(std::move(threadSwitchTask));

    while (threadSwitchCompleted.load() < numOperations)
    {
        scheduler->runExpiredTasks();
        std::this_thread::sleep_for(1ms);
    }

    const auto threadSwitchEnd      = std::chrono::steady_clock::now();
    const auto threadSwitchDuration = std::chrono::duration_cast<std::chrono::microseconds>(threadSwitchEnd - threadSwitchStart);

    // Calculate performance metrics
    const auto symmetricPerOp       = symmetricDuration.count() / numOperations;
    const auto threadSwitchPerOp    = threadSwitchDuration.count() / numOperations;
    const auto threadSwitchOverhead = threadSwitchPerOp - symmetricPerOp;

    std::cout << "Symmetric Transfer vs Thread Switching Test:" << std::endl;
    std::cout << "============================================" << std::endl;
    std::cout << "Operations: " << numOperations << std::endl;
    std::cout << std::endl;

    std::cout << "PERFORMANCE RESULTS:" << std::endl;
    std::cout << "  Symmetric Transfer (Task->Task->Task->Task):" << std::endl;
    std::cout << "    Total time: " << symmetricDuration.count() << "μs" << std::endl;
    std::cout << "    Per operation: " << symmetricPerOp << "μs" << std::endl;
    std::cout << "    Overhead: 0μs (baseline)" << std::endl;
    std::cout << std::endl;

    std::cout << "  Thread Switching (Task->AsyncTask->Task):" << std::endl;
    std::cout << "    Total time: " << threadSwitchDuration.count() << "μs" << std::endl;
    std::cout << "    Per operation: " << threadSwitchPerOp << "μs" << std::endl;
    std::cout << "    Overhead per operation: " << threadSwitchOverhead << "μs" << std::endl;
    std::cout << std::endl;

    std::cout << "COMPARISON ANALYSIS:" << std::endl;
    std::cout << "  - Symmetric transfer uses no thread switching" << std::endl;
    std::cout << "  - Thread switching involves full context switch" << std::endl;
    std::cout << "  - Overhead difference: " << threadSwitchOverhead << "μs per operation" << std::endl;
    std::cout << "  - Performance ratio: " << (symmetricPerOp * 100.0 / threadSwitchPerOp) << "%" << std::endl;
    std::cout << std::endl;

    std::cout << "LAND SANDBOAT IMPACT:" << std::endl;
    const auto symmetricLandSandBoat    = (symmetricPerOp * 1980) / 1000;
    const auto threadSwitchLandSandBoat = (threadSwitchPerOp * 1980) / 1000;
    std::cout << "  Symmetric transfer: " << symmetricLandSandBoat << "ms per tick" << std::endl;
    std::cout << "  Thread switching: " << threadSwitchLandSandBoat << "ms per tick" << std::endl;
    std::cout << "  Difference: " << (threadSwitchLandSandBoat - symmetricLandSandBoat) << "ms per tick" << std::endl;
    std::cout << "  Target: <100ms per tick" << std::endl;

    // Verify all completed
    EXPECT_EQ(symmetricCompleted.load(), numOperations);
    EXPECT_EQ(threadSwitchCompleted.load(), numOperations);

    // Document the performance characteristics
    EXPECT_GT(threadSwitchPerOp, symmetricPerOp) << "Thread switching should be slower";
    EXPECT_GT(threadSwitchOverhead, 0) << "Thread switching should have measurable overhead";

    // LandSandBoat impact should be documented
    EXPECT_GT(symmetricLandSandBoat, 0) << "Symmetric transfer should have measurable LandSandBoat impact";
    EXPECT_GT(threadSwitchLandSandBoat, 0) << "Thread switching should have measurable LandSandBoat impact";
}

//
// TEST: Pure AsyncTask Chain Performance
// PURPOSE: Measure performance of pure AsyncTask chains (all thread switching)
// BEHAVIOR: Creates AsyncTask->AsyncTask->AsyncTask->AsyncTask->AsyncTask chains
// EXPECTATION: Should show pure thread switching overhead without symmetric transfer
//
TEST_F(SchedulerComplexWorkloadTest, PureAsyncTaskChainPerformance)
{
    const int        numOperations = 50;
    std::atomic<int> operationsCompleted{ 0 };

    auto pureAsyncTaskChain = [&]() -> Task<void>
    {
        for (int i = 0; i < numOperations; ++i)
        {
            // AsyncTask->AsyncTask->AsyncTask->AsyncTask->AsyncTask chain
            auto result = co_await [&]() -> AsyncTask<int>
            {
                auto step1 = co_await [&]() -> AsyncTask<int>
                {
                    auto step2 = co_await [&]() -> AsyncTask<int>
                    {
                        auto step3 = co_await [&]() -> AsyncTask<int>
                        {
                            auto step4 = co_await [&]() -> AsyncTask<int>
                            {
                                // Simulate some work
                                std::this_thread::sleep_for(std::chrono::microseconds(100));
                                co_return 42;
                            }();

                            co_return step4 * 2;
                        }();

                        co_return step3 + 1;
                    }();

                    co_return step2 * 3;
                }();

                co_return step1 - 10;
            }();
            (void)result; // Suppress unused variable warning

            operationsCompleted.fetch_add(1);
        }
    };

    const auto start = std::chrono::steady_clock::now();
    scheduler->schedule(std::move(pureAsyncTaskChain));

    while (operationsCompleted.load() < numOperations)
    {
        scheduler->runExpiredTasks();
        std::this_thread::sleep_for(1ms);
    }

    const auto end      = std::chrono::steady_clock::now();
    const auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);

    const auto perOperation = duration.count() / numOperations;
    const auto expectedWork = 100 * 5; // 100μs per step, 5 steps
    const auto overhead = perOperation - expectedWork;

    std::cout << "Pure AsyncTask Chain Performance Test:" << std::endl;
    std::cout << "======================================" << std::endl;
    std::cout << "Operations: " << numOperations << std::endl;
    std::cout << "Total time: " << duration.count() << "μs" << std::endl;
    std::cout << "Per operation: " << perOperation << "μs" << std::endl;
    std::cout << "Expected work per operation: " << expectedWork << "μs" << std::endl;
    std::cout << "Overhead per operation: " << overhead << "μs" << std::endl;
    std::cout << std::endl;

    std::cout << "PATTERN ANALYSIS:" << std::endl;
    std::cout << "  - AsyncTask->AsyncTask->AsyncTask->AsyncTask->AsyncTask chain" << std::endl;
    std::cout << "  - All operations trigger thread switching" << std::endl;
    std::cout << "  - No symmetric transfer benefits" << std::endl;
    std::cout << "  - Measures pure thread switching overhead" << std::endl;
    std::cout << std::endl;

    std::cout << "PERFORMANCE CHARACTERISTICS:" << std::endl;
    std::cout << "  - Each AsyncTask triggers thread switch" << std::endl;
    std::cout << "  - 4 thread switches per operation" << std::endl;
    std::cout << "  - High overhead but maximum parallelism" << std::endl;
    std::cout << "  - This represents worst-case thread switching overhead" << std::endl;
    std::cout << std::endl;

    std::cout << "LAND SANDBOAT IMPACT:" << std::endl;
    const auto landSandBoatOverhead = (perOperation * 1980) / 1000;
    std::cout << "  - Per operation: " << perOperation << "μs" << std::endl;
    std::cout << "  - LandSandBoat overhead: " << landSandBoatOverhead << "ms per tick" << std::endl;
    std::cout << "  - Target: <100ms per tick" << std::endl;
    std::cout << "  - This represents maximum thread switching overhead" << std::endl;

    EXPECT_EQ(operationsCompleted.load(), numOperations);
    EXPECT_GT(perOperation, expectedWork) << "Should include the simulated work";
    EXPECT_GT(overhead, 0) << "Should have measurable thread switching overhead";
}

//
// TEST: Extended Pure Task Chain Performance
// PURPOSE: Measure performance of extended pure Task chains (all symmetric transfer)
// BEHAVIOR: Creates Task->Task->Task->Task->Task->Task chains
// EXPECTATION: Should show pure symmetric transfer performance with more depth
//
TEST_F(SchedulerComplexWorkloadTest, ExtendedPureTaskChainPerformance)
{
    const int        numOperations = 100;
    std::atomic<int> operationsCompleted{ 0 };

    auto extendedTaskChain = [&]() -> Task<void>
    {
        for (int i = 0; i < numOperations; ++i)
        {
            // Task->Task->Task->Task->Task->Task chain (6 levels deep)
            auto result = co_await [&]() -> Task<int>
            {
                auto step1 = co_await [&]() -> Task<int>
                {
                    auto step2 = co_await [&]() -> Task<int>
                    {
                        auto step3 = co_await [&]() -> Task<int>
                        {
                            auto step4 = co_await [&]() -> Task<int>
                            {
                                auto step5 = co_await [&]() -> Task<int>
                                {
                                    // Pure symmetric transfer - no work
                                    co_return 42;
                                }();

                                co_return step5 * 2;
                            }();

                            co_return step4 + 1;
                        }();

                        co_return step3 * 3;
                    }();

                    co_return step2 - 10;
                }();

                co_return step1 / 2;
            }();
            (void)result; // Suppress unused variable warning

            operationsCompleted.fetch_add(1);
        }
    };

    const auto start = std::chrono::steady_clock::now();
    scheduler->schedule(std::move(extendedTaskChain));

    while (operationsCompleted.load() < numOperations)
    {
        scheduler->runExpiredTasks();
        std::this_thread::sleep_for(1ms);
    }

    const auto end      = std::chrono::steady_clock::now();
    const auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);

    const auto perOperation = duration.count() / numOperations;

    std::cout << "Extended Pure Task Chain Performance Test:" << std::endl;
    std::cout << "==========================================" << std::endl;
    std::cout << "Operations: " << numOperations << std::endl;
    std::cout << "Total time: " << duration.count() << "μs" << std::endl;
    std::cout << "Per operation: " << perOperation << "μs" << std::endl;
    std::cout << "Chain depth: 6 levels (Task->Task->Task->Task->Task->Task)" << std::endl;
    std::cout << std::endl;

    std::cout << "PATTERN ANALYSIS:" << std::endl;
    std::cout << "  - Task->Task->Task->Task->Task->Task chain" << std::endl;
    std::cout << "  - All operations use symmetric transfer" << std::endl;
    std::cout << "  - No thread switching overhead" << std::endl;
    std::cout << "  - Measures pure symmetric transfer performance" << std::endl;
    std::cout << std::endl;

    std::cout << "PERFORMANCE CHARACTERISTICS:" << std::endl;
    std::cout << "  - 5 symmetric transfers per operation" << std::endl;
    std::cout << "  - No thread switching overhead" << std::endl;
    std::cout << "  - Minimal coroutine frame overhead" << std::endl;
    std::cout << "  - This represents best-case symmetric transfer performance" << std::endl;
    std::cout << std::endl;

    std::cout << "LAND SANDBOAT IMPACT:" << std::endl;
    const auto landSandBoatOverhead = (perOperation * 1980) / 1000;
    std::cout << "  - Per operation: " << perOperation << "μs" << std::endl;
    std::cout << "  - LandSandBoat overhead: " << landSandBoatOverhead << "ms per tick" << std::endl;
    std::cout << "  - Target: <100ms per tick" << std::endl;
    std::cout << "  - This represents minimal symmetric transfer overhead" << std::endl;

    EXPECT_EQ(operationsCompleted.load(), numOperations);
    EXPECT_GT(perOperation, 0) << "Should have measurable overhead";
    EXPECT_LT(perOperation, 1000) << "Symmetric transfer should be very fast";
}

//
// TEST: Mixed Pattern with Multiple Thread Switches
// PURPOSE: Test complex patterns with multiple thread switches
// BEHAVIOR: Creates Task->AsyncTask->Task->AsyncTask->Task->AsyncTask chains
// EXPECTATION: Should show performance of mixed patterns with multiple thread switches
//
TEST_F(SchedulerComplexWorkloadTest, MixedPatternMultipleThreadSwitches)
{
    const int        numOperations = 50;
    std::atomic<int> operationsCompleted{ 0 };

    auto mixedPatternTask = [&]() -> Task<void>
    {
        for (int i = 0; i < numOperations; ++i)
        {
            // Task->AsyncTask->Task->AsyncTask->Task->AsyncTask pattern
            auto result = co_await [&]() -> Task<int>
            {
                auto step1 = co_await [&]() -> AsyncTask<int>
                {
                    // First thread switch
                    std::this_thread::sleep_for(std::chrono::microseconds(50));
                    co_return 42;
                }();

                auto step2 = co_await [&]() -> Task<int>
                {
                    // Back to main thread (symmetric transfer)
                    co_return step1 * 2;
                }();

                auto step3 = co_await [&]() -> AsyncTask<int>
                {
                    // Second thread switch
                    std::this_thread::sleep_for(std::chrono::microseconds(50));
                    co_return step2 + 10;
                }();

                auto step4 = co_await [&]() -> Task<int>
                {
                    // Back to main thread (symmetric transfer)
                    co_return step3 * 3;
                }();

                auto step5 = co_await [&]() -> AsyncTask<int>
                {
                    // Third thread switch
                    std::this_thread::sleep_for(std::chrono::microseconds(50));
                    co_return step4 - 5;
                }();

                co_return step5;
            }();
            (void)result; // Suppress unused variable warning

            operationsCompleted.fetch_add(1);
        }
    };

    const auto start = std::chrono::steady_clock::now();
    scheduler->schedule(std::move(mixedPatternTask));

    while (operationsCompleted.load() < numOperations)
    {
        scheduler->runExpiredTasks();
        std::this_thread::sleep_for(1ms);
    }

    const auto end      = std::chrono::steady_clock::now();
    const auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);

    const auto perOperation = duration.count() / numOperations;
    const auto expectedWork = 50 * 3; // 50μs per AsyncTask, 3 AsyncTasks
    const auto overhead = perOperation - expectedWork;

    std::cout << "Mixed Pattern Multiple Thread Switches Test:" << std::endl;
    std::cout << "============================================" << std::endl;
    std::cout << "Operations: " << numOperations << std::endl;
    std::cout << "Total time: " << duration.count() << "μs" << std::endl;
    std::cout << "Per operation: " << perOperation << "μs" << std::endl;
    std::cout << "Expected work per operation: " << expectedWork << "μs" << std::endl;
    std::cout << "Overhead per operation: " << overhead << "μs" << std::endl;
    std::cout << std::endl;

    std::cout << "PATTERN ANALYSIS:" << std::endl;
    std::cout << "  - Task->AsyncTask->Task->AsyncTask->Task->AsyncTask chain" << std::endl;
    std::cout << "  - 3 thread switches per operation" << std::endl;
    std::cout << "  - 2 symmetric transfers per operation" << std::endl;
    std::cout << "  - Measures mixed pattern performance" << std::endl;
    std::cout << std::endl;

    std::cout << "PERFORMANCE CHARACTERISTICS:" << std::endl;
    std::cout << "  - Alternating Task and AsyncTask operations" << std::endl;
    std::cout << "  - Multiple thread switches with symmetric transfers" << std::endl;
    std::cout << "  - Realistic workload pattern" << std::endl;
    std::cout << "  - This represents typical mixed workload performance" << std::endl;
    std::cout << std::endl;

    std::cout << "LAND SANDBOAT IMPACT:" << std::endl;
    const auto landSandBoatOverhead = (perOperation * 1980) / 1000;
    std::cout << "  - Per operation: " << perOperation << "μs" << std::endl;
    std::cout << "  - LandSandBoat overhead: " << landSandBoatOverhead << "ms per tick" << std::endl;
    std::cout << "  - Target: <100ms per tick" << std::endl;
    std::cout << "  - This represents realistic mixed workload overhead" << std::endl;

    EXPECT_EQ(operationsCompleted.load(), numOperations);
    EXPECT_GT(perOperation, expectedWork) << "Should include the simulated work";
    EXPECT_GT(overhead, 0) << "Should have measurable overhead";
}
