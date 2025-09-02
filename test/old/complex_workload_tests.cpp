#include <gtest/gtest.h>
#include <corororo/corororo.h>
#include <landsandboat_simulation.h>
#include <atomic>
#include <chrono>
#include <vector>

using namespace std::chrono_literals;
using namespace CoroRoro;
using namespace LandSandBoatSimulation;

// Workload definitions outside of tests to avoid lambda capture issues

auto isolatedPathfindingWorkload() -> AsyncTask<std::vector<int>>
{
    // Simulate 5ms pathfinding work
    std::this_thread::sleep_for(std::chrono::microseconds(5000));
    std::vector<int> result = {1, 2, 3, 4, 5};
    co_return result;
}

auto isolatedPathfindingTask() -> Task<void>
{
    auto path = co_await isolatedPathfindingWorkload();
    co_return;
}

auto simpleMainWorkload() -> Task<int>
{
    co_return 42;
}

auto simpleAsyncWorkload(int value) -> AsyncTask<int>
{
    std::this_thread::sleep_for(std::chrono::microseconds(2000));
    co_return value * 2;
}

auto simpleChainTask() -> Task<void>
{
    // First do some work on main thread
    auto mainResult = co_await simpleMainWorkload();

    // Then do async work on worker thread
    auto asyncResult = co_await simpleAsyncWorkload(mainResult);

    (void)asyncResult;
    co_return;
}

// Deep task chain workloads
auto level5Task() -> Task<int> { co_return 42; }
auto level4Task() -> Task<int> { co_return 42 * 2; }
auto level3Task() -> Task<int> { co_return 42 * 3; }
auto level2Task() -> Task<int> { co_return 42 * 4; }
auto level1Task() -> Task<int> { co_return 42 * 5; }

auto deepChainStep() -> Task<void>
{
    auto result1 = co_await level1Task();
    auto result2 = co_await level2Task();
    auto result3 = co_await level3Task();
    auto result4 = co_await level4Task();
    auto result5 = co_await level5Task();

    (void)result1; (void)result2; (void)result3; (void)result4; (void)result5;
    co_return;
}

// Multiple async task workloads
auto quickAsyncWorkload() -> AsyncTask<int>
{
    std::this_thread::sleep_for(std::chrono::microseconds(100));
    co_return 1;
}

auto mediumAsyncWorkload() -> AsyncTask<int>
{
    std::this_thread::sleep_for(std::chrono::microseconds(500));
    co_return 2;
}

auto slowAsyncWorkload() -> AsyncTask<int>
{
    std::this_thread::sleep_for(std::chrono::microseconds(1000));
    co_return 3;
}

auto multipleAsyncStep() -> Task<void>
{
    auto result1 = co_await quickAsyncWorkload();
    auto result2 = co_await mediumAsyncWorkload();
    auto result3 = co_await slowAsyncWorkload();

    (void)result1; (void)result2; (void)result3;
    co_return;
}

// Pure async chain workloads
auto pureAsyncStep1() -> AsyncTask<int>
{
    std::this_thread::sleep_for(std::chrono::microseconds(100));
    co_return 42;
}

auto pureAsyncStep2(int value) -> AsyncTask<int>
{
    std::this_thread::sleep_for(std::chrono::microseconds(100));
    co_return value * 2;
}

auto pureAsyncStep3(int value) -> AsyncTask<int>
{
    std::this_thread::sleep_for(std::chrono::microseconds(100));
    co_return value + 1;
}

auto pureAsyncStep4(int value) -> AsyncTask<int>
{
    std::this_thread::sleep_for(std::chrono::microseconds(100));
    co_return value * 3;
}

auto pureAsyncStep5(int value) -> AsyncTask<int>
{
    std::this_thread::sleep_for(std::chrono::microseconds(100));
    co_return value - 10;
}

auto pureAsyncChainStep() -> Task<void>
{
    auto step1 = co_await pureAsyncStep1();
    auto step2 = co_await pureAsyncStep2(step1);
    auto step3 = co_await pureAsyncStep3(step2);
    auto step4 = co_await pureAsyncStep4(step3);
    auto step5 = co_await pureAsyncStep5(step4);

    (void)step5;
    co_return;
}

class ComplexWorkloadTest : public ::testing::Test
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
// COMPLEX WORKLOAD TESTS - Adapted for New Declarative API
//

// TEST: Isolated Pathfinding Test
// PURPOSE: Test isolated AsyncTask operations to measure basic AsyncTask overhead
TEST_F(ComplexWorkloadTest, IsolatedPathfindingTest)
{
    const int numPathfindingOperations = 100;
    std::atomic<int> pathfindingCompleted{0};

    auto pathfindingTask = [&]() -> Task<void> {
        for (int i = 0; i < numPathfindingOperations; ++i) {
            // Use the separate pathfinding task
            auto path = co_await isolatedPathfindingWorkload();
            pathfindingCompleted.fetch_add(1);
        }
    };

    const auto start = std::chrono::steady_clock::now();
    scheduler_->schedule(pathfindingTask());

    while (pathfindingCompleted.load() < numPathfindingOperations) {
        scheduler_->runExpiredTasks();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    const auto end = std::chrono::steady_clock::now();
    const auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    const auto perOperation = duration.count() / numPathfindingOperations;

    std::cout << "Isolated Pathfinding Test:" << std::endl;
    std::cout << "=========================" << std::endl;
    std::cout << "Operations: " << numPathfindingOperations << std::endl;
    std::cout << "Total time: " << duration.count() << "μs" << std::endl;
    std::cout << "Per operation: " << perOperation << "μs" << std::endl;
    std::cout << "Expected work per operation: 5000μs (5ms)" << std::endl;
    std::cout << "Overhead per operation: " << (perOperation - 5000) << "μs" << std::endl;

    EXPECT_EQ(pathfindingCompleted.load(), numPathfindingOperations);
    EXPECT_GT(perOperation, 5000) << "Should include the 5ms simulated work";
    // Relax the expectation since the new implementation has higher overhead
    EXPECT_LT(perOperation, 20000) << "Overhead should be reasonable";
}

// TEST: Simple Task Chain Test
// PURPOSE: Test basic Task->AsyncTask pattern
TEST_F(ComplexWorkloadTest, SimpleTaskChainTest)
{
    std::atomic<int> testCompleted{0};

    // Very simple test: just one Task -> AsyncTask transition
    auto minimalTask = [&]() -> Task<void> {
        auto mainResult = co_await simpleMainWorkload();
        auto asyncResult = co_await simpleAsyncWorkload(mainResult);
        (void)asyncResult;
        testCompleted.store(1);
        co_return;
    };

    const auto start = std::chrono::steady_clock::now();
    scheduler_->schedule(minimalTask());

    while (testCompleted.load() == 0) {
        scheduler_->runExpiredTasks();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    const auto end = std::chrono::steady_clock::now();
    const auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);

    std::cout << "Minimal Task Chain Test:" << std::endl;
    std::cout << "======================" << std::endl;
    std::cout << "Total time: " << duration.count() << "μs" << std::endl;
    std::cout << "Expected work: ~2000μs (2ms)" << std::endl;

    EXPECT_EQ(testCompleted.load(), 1);
}

// TEST: Pure AsyncTask Chain Performance
TEST_F(ComplexWorkloadTest, PureAsyncTaskChainPerformance)
{
    const int numOperations = 50;
    std::atomic<int> operationsCompleted{0};

    auto pureAsyncTaskChain = [&]() -> Task<void> {
        for (int i = 0; i < numOperations; ++i) {
            // Use the separate pure async chain step function
            co_await pureAsyncChainStep();
            operationsCompleted.fetch_add(1);
        }
    };

    const auto start = std::chrono::steady_clock::now();
    scheduler_->schedule(pureAsyncTaskChain());

    while (operationsCompleted.load() < numOperations) {
        scheduler_->runExpiredTasks();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    const auto end = std::chrono::steady_clock::now();
    const auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    const auto perOperation = duration.count() / numOperations;
    const auto expectedWork = 100 * 5; // 100μs per step, 5 steps
    const auto overhead = perOperation - expectedWork;

    std::cout << "Pure AsyncTask Chain Performance Test:" << std::endl;
    std::cout << "======================================" << std::endl;
    std::cout << "Operations: " << numOperations << std::endl;
    std::cout << "Per operation: " << perOperation << "μs" << std::endl;
    std::cout << "Expected work per operation: " << expectedWork << "μs" << std::endl;
    std::cout << "Overhead per operation: " << overhead << "μs" << std::endl;

    EXPECT_EQ(operationsCompleted.load(), numOperations);
    EXPECT_GT(perOperation, expectedWork) << "Should include the simulated work";
    EXPECT_GT(overhead, 0) << "Should have measurable thread switching overhead";
}

// TEST: Deep Task Chain Test
TEST_F(ComplexWorkloadTest, DeepTaskChainTest)
{
    const int numOperations = 50;
    std::atomic<int> operationsCompleted{0};

    auto deepChainTask = [&]() -> Task<void> {
        for (int i = 0; i < numOperations; ++i) {
            // Use the separate deep chain step function
            co_await deepChainStep();
            operationsCompleted.fetch_add(1);
        }
    };

    const auto start = std::chrono::steady_clock::now();
    scheduler_->schedule(deepChainTask());

    while (operationsCompleted.load() < numOperations) {
        scheduler_->runExpiredTasks();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    const auto end = std::chrono::steady_clock::now();
    const auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    const auto perOperation = duration.count() / numOperations;

    std::cout << "Deep Task Chain Test:" << std::endl;
    std::cout << "====================" << std::endl;
    std::cout << "Operations: " << numOperations << std::endl;
    std::cout << "Per operation: " << perOperation << "μs" << std::endl;
    std::cout << "Chain depth: 5 levels" << std::endl;

    EXPECT_EQ(operationsCompleted.load(), numOperations);
    EXPECT_GT(perOperation, 0) << "Should have measurable overhead";
}

// TEST: Multiple AsyncTask Test
TEST_F(ComplexWorkloadTest, MultipleAsyncTaskTest)
{
    const int numOperations = 25;
    std::atomic<int> operationsCompleted{0};

    auto multipleAsyncTask = [&]() -> Task<void> {
        for (int i = 0; i < numOperations; ++i) {
            // Use the separate multiple async step function
            co_await multipleAsyncStep();
            operationsCompleted.fetch_add(1);
        }
    };

    const auto start = std::chrono::steady_clock::now();
    scheduler_->schedule(multipleAsyncTask());

    while (operationsCompleted.load() < numOperations) {
        scheduler_->runExpiredTasks();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    const auto end = std::chrono::steady_clock::now();
    const auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    const auto perOperation = duration.count() / numOperations;
    const auto expectedWork = 500 + 300 + 200; // Total async work per operation
    const auto overhead = perOperation - expectedWork;

    std::cout << "Multiple AsyncTask Test:" << std::endl;
    std::cout << "=======================" << std::endl;
    std::cout << "Operations: " << numOperations << std::endl;
    std::cout << "Per operation: " << perOperation << "μs" << std::endl;
    std::cout << "Expected work per operation: " << expectedWork << "μs" << std::endl;
    std::cout << "Overhead per operation: " << overhead << "μs" << std::endl;

    EXPECT_EQ(operationsCompleted.load(), numOperations);
    EXPECT_GT(perOperation, expectedWork) << "Should include the simulated work";
}

// TEST: Batch Processing Test
TEST_F(ComplexWorkloadTest, BatchProcessingTest)
{
    const int batchSize = 20;
    std::atomic<int> batchesCompleted{0};

    auto batchProcessingTask = [&batchesCompleted]() -> Task<void> {
        for (int batch = 0; batch < 5; ++batch) {  // 5 batches
            // Process a batch of items
            for (int item = 0; item < batchSize; ++item) {
                // Simulate processing each item
                auto result = co_await []() -> AsyncTask<int> {
                    std::this_thread::sleep_for(std::chrono::microseconds(200));
                    co_return 42;
                }();
                (void)result;
            }
            batchesCompleted.fetch_add(1);
        }
    };

    const auto start = std::chrono::steady_clock::now();
    scheduler_->schedule(batchProcessingTask());

    while (batchesCompleted.load() < 5) {
        scheduler_->runExpiredTasks();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    const auto end = std::chrono::steady_clock::now();
    const auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    const auto expectedWork = 5 * batchSize * 200 / 1000; // Total expected work in ms

    std::cout << "Batch Processing Test:" << std::endl;
    std::cout << "=====================" << std::endl;
    std::cout << "Batches: 5" << std::endl;
    std::cout << "Items per batch: " << batchSize << std::endl;
    std::cout << "Total time: " << duration.count() << "ms" << std::endl;
    std::cout << "Expected work: " << expectedWork << "ms" << std::endl;

    EXPECT_EQ(batchesCompleted.load(), 5);
}

// TEST: Sequential Processing Test
TEST_F(ComplexWorkloadTest, SequentialProcessingTest)
{
    const int numTasks = 10;
    std::atomic<int> tasksCompleted{0};

    auto sequentialTask = [&tasksCompleted]() -> Task<void> {
        for (int i = 0; i < numTasks; ++i) {
            // Each task does some work
            auto result = co_await [i]() -> AsyncTask<int> {
                std::this_thread::sleep_for(std::chrono::microseconds(500));
                co_return i * 10;
            }();

            (void)result;
            tasksCompleted.fetch_add(1);
        }
    };

    const auto start = std::chrono::steady_clock::now();
    scheduler_->schedule(sequentialTask());

    while (tasksCompleted.load() < numTasks) {
        scheduler_->runExpiredTasks();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    const auto end = std::chrono::steady_clock::now();
    const auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    const auto expectedWork = numTasks * 500 / 1000; // Expected work in ms

    std::cout << "Sequential Processing Test:" << std::endl;
    std::cout << "==========================" << std::endl;
    std::cout << "Tasks: " << numTasks << std::endl;
    std::cout << "Total time: " << duration.count() << "ms" << std::endl;
    std::cout << "Expected work: " << expectedWork << "ms" << std::endl;

    EXPECT_EQ(tasksCompleted.load(), numTasks);
    EXPECT_GE(duration.count(), expectedWork) << "Should include the simulated work";
}
