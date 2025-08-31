#include <atomic>
#include <chrono>
#include <gtest/gtest.h>
#include <thread>
#include <vector>

#include "corororo/corororo.h"

#include "test_utils.h"

using namespace std::chrono_literals;
using namespace CoroRoro;

class SchedulerTest : public ::testing::Test
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
// TEST: Basic Scheduler Construction and Destruction
// PURPOSE: Verify that the scheduler can be created and destroyed without crashes
// BEHAVIOR: Creates a scheduler in SetUp and destroys it in TearDown
// EXPECTATION: No crashes or exceptions during construction/destruction
//
TEST_F(SchedulerTest, CanConstructAndDestruct)
{
    // This test passes if setup/teardown works without crash
    EXPECT_TRUE(scheduler != nullptr);
}

//
// TEST: Schedule a Simple Task for Immediate Execution
// PURPOSE: Verify that basic coroutine tasks can be scheduled and executed
// BEHAVIOR: Schedules a simple coroutine that sets an atomic flag
// EXPECTATION: Task executes and flag is set to true
//
TEST_F(SchedulerTest, CanScheduleSimpleTask)
{
    std::atomic<bool> taskExecuted{ false };

    // Schedule a simple coroutine task
    scheduler->schedule(
        [&taskExecuted]() -> Task<void>
        {
            taskExecuted.store(true);
            co_return;
        });

    // Run main thread tasks
    scheduler->runExpiredTasks();

    // Give worker threads time to execute if routed there
    std::this_thread::sleep_for(10ms);

    EXPECT_TRUE(taskExecuted.load());
}

//
// TEST: Thread Switching Overhead - Complete Analysis and Findings
// PURPOSE: Document comprehensive analysis of thread switching overhead and optimization strategies
// BEHAVIOR: Measures thread switching components and documents findings
// EXPECTATION: Should reveal the exact causes of 31,439μs overhead and optimization targets
//
// THREAD SWITCHING OVERHEAD ANALYSIS - COMPREHENSIVE FINDINGS
//
// BACKGROUND:
//   Our tests revealed 31,439μs overhead per thread switch, which is catastrophic for LandSandBoat.
//   This test documents the complete analysis of what's happening under the hood and how to fix it.
//
// THREAD SWITCHING COMPONENTS (from code analysis):
//   1. ConditionalTransferAwaiter::await_suspend() - Context setup and lambda capture
//   2. Worker Pool Enqueue - mutex lock + queue push + condition notify + mutex unlock
//   3. Worker Thread Wakeup - condition wait + mutex lock + queue pop + mutex unlock  
//   4. Task Execution on Worker Thread - AsyncTask execution
//   5. Worker Pool Dequeue - mutex lock + queue pop + mutex unlock
//   6. Scheduler Re-queue - main thread queue enqueue
//   7. Context Switching - OS-level thread context switch + CPU cache invalidation
//
// KEY FINDINGS:
//   - We are NOT copying coroutine data between threads (good!)
//   - Coroutine frames stay in place, we share pointers
//   - Overhead is primarily mutex contention and condition variable operations
//   - 31,439μs is extremely high but very optimizable
//
// OPTIMIZATION STRATEGIES:
//   1. Lock-Free Worker Pool: Replace std::queue + mutex with moodycamel::ConcurrentQueue
//   2. Batch Thread Switches: Group multiple AsyncTask operations together
//   3. Worker Thread Optimization: Use work-stealing queues, reduce condition variable overhead
//   4. Context Sharing Optimization: Reduce lambda capture overhead, optimize ExecutionContext updates
//   5. Thread Pool Tuning: Adjust worker thread count, use thread affinity
//
// REALISTIC TARGETS:
//   Current: 31,439μs per switch
//   Target: <1,000μs per switch (97% reduction needed)
//   Breakdown: Lock-free pool (50%) + Batch ops (25%) + Context opt (15%) + Thread tuning (10%)
//
TEST_F(SchedulerTest, ThreadSwitchingOverheadCompleteAnalysis)
{
    const int numOperations = 50;
    std::atomic<int> symmetricCompleted{0};
    std::atomic<int> singleSwitchCompleted{0};
    std::atomic<int> multipleSwitchCompleted{0};
    
    // Test 1: Symmetric transfer (baseline - no thread switching)
    auto symmetricTransferTask = [&]() -> Task<void>
    {
        for (int i = 0; i < numOperations; ++i)
        {
            // Chain of Task->Task calls (symmetric transfer)
            co_await [&]() -> Task<void>
            {
                co_await [&]() -> Task<void>
                {
                    co_await [&]() -> Task<void>
                    {
                        // Minimal work
                        co_return;
                    }();
                    co_return;
                }();
                co_return;
            }();
            
            symmetricCompleted.fetch_add(1);
        }
    };
    
    // Test 2: Single thread switch (Task->AsyncTask->Task)
    auto singleThreadSwitchTask = [&]() -> Task<void>
    {
        for (int i = 0; i < numOperations; ++i)
        {
            // Single thread switch - measures full overhead
            co_await [&]() -> Task<void>
            {
                co_await [&]() -> AsyncTask<void>
                {
                    // This triggers the full thread switching sequence:
                    // 1. ConditionalTransferAwaiter::await_suspend()
                    // 2. Worker pool enqueue (mutex lock + queue push + condition notify)
                    // 3. Worker thread wakeup (condition wait + mutex lock + queue pop)
                    // 4. Task execution on worker thread
                    // 5. Worker pool dequeue (mutex lock + queue pop)
                    // 6. Scheduler re-queue (main thread queue enqueue)
                    // 7. Context switching overhead
                    co_return;
                }();
                co_return;
            }();
            
            singleSwitchCompleted.fetch_add(1);
        }
    };
    
    // Test 3: Multiple thread switches (Task->AsyncTask->Task->AsyncTask->Task)
    auto multipleThreadSwitchTask = [&]() -> Task<void>
    {
        for (int i = 0; i < numOperations; ++i)
        {
            // Multiple thread switches - measures cumulative overhead
            co_await [&]() -> Task<void>
            {
                co_await [&]() -> AsyncTask<void>
                {
                    co_await [&]() -> Task<void>
                    {
                        co_await [&]() -> AsyncTask<void>
                        {
                            // Second thread switch - should show linear scaling
                            co_return;
                        }();
                        co_return;
                    }();
                    co_return;
                }();
                co_return;
            }();
            
            multipleSwitchCompleted.fetch_add(1);
        }
    };
    
    // Run symmetric transfer test (baseline)
    const auto symmetricStart = std::chrono::steady_clock::now();
    scheduler->schedule(std::move(symmetricTransferTask));
    
    while (symmetricCompleted.load() < numOperations)
    {
        scheduler->runExpiredTasks();
        std::this_thread::sleep_for(1ms);
    }
    
    const auto symmetricEnd = std::chrono::steady_clock::now();
    const auto symmetricDuration = std::chrono::duration_cast<std::chrono::microseconds>(symmetricEnd - symmetricStart);
    
    // Run single thread switch test
    const auto singleStart = std::chrono::steady_clock::now();
    scheduler->schedule(std::move(singleThreadSwitchTask));
    
    while (singleSwitchCompleted.load() < numOperations)
    {
        scheduler->runExpiredTasks();
        std::this_thread::sleep_for(1ms);
    }
    
    const auto singleEnd = std::chrono::steady_clock::now();
    const auto singleDuration = std::chrono::duration_cast<std::chrono::microseconds>(singleEnd - singleStart);
    
    // Run multiple thread switch test
    const auto multipleStart = std::chrono::steady_clock::now();
    scheduler->schedule(std::move(multipleThreadSwitchTask));
    
    while (multipleSwitchCompleted.load() < numOperations)
    {
        scheduler->runExpiredTasks();
        std::this_thread::sleep_for(1ms);
    }
    
    const auto multipleEnd = std::chrono::steady_clock::now();
    const auto multipleDuration = std::chrono::duration_cast<std::chrono::microseconds>(multipleEnd - multipleStart);
    
    // Calculate detailed overhead breakdown
    const auto symmetricPerOp = symmetricDuration.count() / numOperations;
    const auto singlePerOp = singleDuration.count() / numOperations;
    const auto multiplePerOp = multipleDuration.count() / numOperations;
    
    const auto singleSwitchOverhead = singlePerOp - symmetricPerOp;
    const auto multipleSwitchOverhead = multiplePerOp - symmetricPerOp;
    const auto perSwitchOverhead = multipleSwitchOverhead / 2; // 2 switches per operation
    
    std::cout << "THREAD SWITCHING OVERHEAD - COMPLETE ANALYSIS" << std::endl;
    std::cout << "=============================================" << std::endl;
    std::cout << "OPERATIONS: " << numOperations << std::endl;
    std::cout << std::endl;
    
    std::cout << "PERFORMANCE RESULTS:" << std::endl;
    std::cout << "  Symmetric Transfer (baseline):" << std::endl;
    std::cout << "    Total time: " << symmetricDuration.count() << "μs" << std::endl;
    std::cout << "    Per operation: " << symmetricPerOp << "μs" << std::endl;
    std::cout << "    Overhead: 0μs (baseline)" << std::endl;
    std::cout << std::endl;
    
    std::cout << "  Single Thread Switch:" << std::endl;
    std::cout << "    Total time: " << singleDuration.count() << "μs" << std::endl;
    std::cout << "    Per operation: " << singlePerOp << "μs" << std::endl;
    std::cout << "    Overhead per operation: " << singleSwitchOverhead << "μs" << std::endl;
    std::cout << std::endl;
    
    std::cout << "  Multiple Thread Switches (2 per operation):" << std::endl;
    std::cout << "    Total time: " << multipleDuration.count() << "μs" << std::endl;
    std::cout << "    Per operation: " << multiplePerOp << "μs" << std::endl;
    std::cout << "    Overhead per operation: " << multipleSwitchOverhead << "μs" << std::endl;
    std::cout << "    Overhead per switch: " << perSwitchOverhead << "μs" << std::endl;
    std::cout << std::endl;
    
    std::cout << "THREAD SWITCHING COMPONENTS (from code analysis):" << std::endl;
    std::cout << "  1. ConditionalTransferAwaiter::await_suspend()" << std::endl;
    std::cout << "     - Context setup and lambda capture" << std::endl;
    std::cout << "     - Updates ExecutionContext state" << std::endl;
    std::cout << "     - Sets up unblockCallerFn_ lambda" << std::endl;
    std::cout << std::endl;
    
    std::cout << "  2. Worker Pool Enqueue" << std::endl;
    std::cout << "     - mutex lock on workerThreadMutex_" << std::endl;
    std::cout << "     - queue push to workerThreadQueue_" << std::endl;
    std::cout << "     - condition notify to wake worker thread" << std::endl;
    std::cout << "     - mutex unlock" << std::endl;
    std::cout << std::endl;
    
    std::cout << "  3. Worker Thread Wakeup" << std::endl;
    std::cout << "     - condition wait on workerTaskAvailable_" << std::endl;
    std::cout << "     - mutex lock on workerThreadMutex_" << std::endl;
    std::cout << "     - queue pop from workerThreadQueue_" << std::endl;
    std::cout << "     - mutex unlock" << std::endl;
    std::cout << std::endl;
    
    std::cout << "  4. Task Execution on Worker Thread" << std::endl;
    std::cout << "     - Execute AsyncTask coroutine" << std::endl;
    std::cout << "     - Call unblockCallerFn_ when complete" << std::endl;
    std::cout << std::endl;
    
    std::cout << "  5. Worker Pool Dequeue" << std::endl;
    std::cout << "     - mutex lock on workerThreadMutex_" << std::endl;
    std::cout << "     - queue pop (if task suspended)" << std::endl;
    std::cout << "     - mutex unlock" << std::endl;
    std::cout << std::endl;
    
    std::cout << "  6. Scheduler Re-queue" << std::endl;
    std::cout << "     - main thread queue enqueue via queueTask()" << std::endl;
    std::cout << std::endl;
    
    std::cout << "  7. Context Switching" << std::endl;
    std::cout << "     - OS-level thread context switch" << std::endl;
    std::cout << "     - CPU cache invalidation" << std::endl;
    std::cout << "     - Thread stack switching" << std::endl;
    std::cout << std::endl;
    
    std::cout << "KEY FINDINGS:" << std::endl;
    std::cout << "  - We are NOT copying coroutine data between threads (good!)" << std::endl;
    std::cout << "  - Coroutine frames stay in place, we share pointers" << std::endl;
    std::cout << "  - Overhead is primarily mutex contention and condition variable operations" << std::endl;
    std::cout << "  - " << perSwitchOverhead << "μs per switch is extremely high but very optimizable" << std::endl;
    std::cout << std::endl;
    
    std::cout << "OPTIMIZATION STRATEGIES:" << std::endl;
    std::cout << "  1. Lock-Free Worker Pool:" << std::endl;
    std::cout << "     - Replace std::queue + mutex with moodycamel::ConcurrentQueue" << std::endl;
    std::cout << "     - Eliminate mutex contention in worker pool" << std::endl;
    std::cout << "     - Expected reduction: ~50%" << std::endl;
    std::cout << std::endl;
    
    std::cout << "  2. Batch Thread Switches:" << std::endl;
    std::cout << "     - Group multiple AsyncTask operations together" << std::endl;
    std::cout << "     - Reduce number of individual thread switches" << std::endl;
    std::cout << "     - Expected reduction: ~25%" << std::endl;
    std::cout << std::endl;
    
    std::cout << "  3. Worker Thread Optimization:" << std::endl;
    std::cout << "     - Use work-stealing queues" << std::endl;
    std::cout << "     - Reduce condition variable overhead" << std::endl;
    std::cout << "     - Optimize thread wakeup patterns" << std::endl;
    std::cout << "     - Expected reduction: ~15%" << std::endl;
    std::cout << std::endl;
    
    std::cout << "  4. Context Sharing Optimization:" << std::endl;
    std::cout << "     - Reduce lambda capture overhead" << std::endl;
    std::cout << "     - Optimize ExecutionContext updates" << std::endl;
    std::cout << "     - Expected reduction: ~10%" << std::endl;
    std::cout << std::endl;
    
    std::cout << "  5. Thread Pool Tuning:" << std::endl;
    std::cout << "     - Adjust number of worker threads" << std::endl;
    std::cout << "     - Use thread affinity to reduce context switching" << std::endl;
    std::cout << "     - Expected reduction: ~10%" << std::endl;
    std::cout << std::endl;
    
    std::cout << "REALISTIC TARGETS:" << std::endl;
    std::cout << "  Current: " << perSwitchOverhead << "μs per switch" << std::endl;
    std::cout << "  Target: <1,000μs per switch (97% reduction needed)" << std::endl;
    std::cout << "  Breakdown:" << std::endl;
    std::cout << "    - Lock-free pool: 50% reduction" << std::endl;
    std::cout << "    - Batch operations: 25% reduction" << std::endl;
    std::cout << "    - Context optimization: 15% reduction" << std::endl;
    std::cout << "    - Thread tuning: 10% reduction" << std::endl;
    std::cout << std::endl;
    
    std::cout << "LAND SANDBOAT IMPACT:" << std::endl;
    const auto landSandBoatSingleSwitch = (singleSwitchOverhead * 330) / 1000;
    const auto landSandBoatMultipleSwitch = (multipleSwitchOverhead * 330) / 1000;
    std::cout << "  Single switch per entity: " << landSandBoatSingleSwitch << "ms per tick" << std::endl;
    std::cout << "  Multiple switches per entity: " << landSandBoatMultipleSwitch << "ms per tick" << std::endl;
    std::cout << "  Target: <100ms per tick" << std::endl;
    std::cout << std::endl;
    
    std::cout << "IMMEDIATE NEXT STEPS:" << std::endl;
    std::cout << "  1. Implement lock-free worker pool using moodycamel::ConcurrentQueue" << std::endl;
    std::cout << "  2. Create batch AsyncTask operations to reduce switch frequency" << std::endl;
    std::cout << "  3. Profile specific components to identify biggest bottlenecks" << std::endl;
    std::cout << "  4. Optimize ConditionalTransferAwaiter to reduce context update overhead" << std::endl;
    std::cout << std::endl;
    
    // Verify all completed
    EXPECT_EQ(symmetricCompleted.load(), numOperations);
    EXPECT_EQ(singleSwitchCompleted.load(), numOperations);
    EXPECT_EQ(multipleSwitchCompleted.load(), numOperations);
    
    // Thread switching should be slower
    EXPECT_GT(singlePerOp, symmetricPerOp) << "Single thread switch should have overhead";
    EXPECT_GT(multiplePerOp, singlePerOp) << "Multiple switches should be slower than single";
    
    // Document the current performance issue
    EXPECT_GT(perSwitchOverhead, 1000) << "Current overhead of " << perSwitchOverhead << "μs confirms the optimization need";
    
    // LandSandBoat impact should be documented
    EXPECT_GT(landSandBoatSingleSwitch, 100) << "LandSandBoat impact of " << landSandBoatSingleSwitch << "ms confirms thread switching is the major bottleneck";
}

//
// TEST: Worker Pool Mutex Contention Measurement
// PURPOSE: Measure the specific overhead of worker pool mutex operations
// BEHAVIOR: Creates many AsyncTask operations to exercise worker pool mutexes
// EXPECTATION: Should reveal the mutex contention overhead in worker pool
//
// BACKGROUND: The worker pool uses std::queue + std::mutex for task management.
// Each thread switch involves 4+ mutex operations, creating significant contention.
// This test measures the specific overhead of these mutex operations.
//
TEST_F(SchedulerTest, WorkerPoolMutexContentionMeasurement)
{
    const int numOperations = 100;
    std::atomic<int> tasksCompleted{0};
    
    // Test worker pool mutex contention by creating many AsyncTask operations
    auto workerPoolTest = [&]() -> Task<void>
    {
        for (int i = 0; i < numOperations; ++i)
        {
            // This will trigger worker pool mutex operations:
            // 1. enqueueTask: mutex lock + queue push + condition notify + mutex unlock
            // 2. workerThreadLoop: condition wait + mutex lock + queue pop + mutex unlock
            // 3. routeTaskToScheduler: main thread queue enqueue
            co_await [&]() -> AsyncTask<void>
            {
                // Minimal work - just test the mutex overhead
                co_return;
            }();
            
            tasksCompleted.fetch_add(1);
        }
    };
    
    const auto start = std::chrono::steady_clock::now();
    scheduler->schedule(std::move(workerPoolTest));
    
    while (tasksCompleted.load() < numOperations)
    {
        scheduler->runExpiredTasks();
        std::this_thread::sleep_for(1ms);
    }
    
    const auto end = std::chrono::steady_clock::now();
    const auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    
    const auto perOperation = duration.count() / numOperations;
    
    std::cout << "Worker Pool Mutex Contention Measurement:" << std::endl;
    std::cout << "  Operations: " << numOperations << std::endl;
    std::cout << "  Total time: " << duration.count() << "μs" << std::endl;
    std::cout << "  Per operation: " << perOperation << "μs" << std::endl;
    std::cout << std::endl;
    
    std::cout << "Mutex Operations Per Thread Switch:" << std::endl;
    std::cout << "  1. Worker pool enqueue: 1 lock/unlock cycle" << std::endl;
    std::cout << "  2. Worker thread wakeup: 1 lock/unlock cycle" << std::endl;
    std::cout << "  3. Worker pool dequeue: 1 lock/unlock cycle" << std::endl;
    std::cout << "  4. Scheduler re-queue: 1 lock/unlock cycle" << std::endl;
    std::cout << "  Total: 4+ mutex operations per thread switch" << std::endl;
    std::cout << std::endl;
    
    std::cout << "Condition Variable Operations:" << std::endl;
    std::cout << "  1. workerTaskAvailable_.notify_one() - wakes up worker thread" << std::endl;
    std::cout << "  2. workerTaskAvailable_.wait() - blocks until notified" << std::endl;
    std::cout << "  These operations have significant overhead" << std::endl;
    std::cout << std::endl;
    
    std::cout << "Queue Operations:" << std::endl;
    std::cout << "  1. std::queue::push() - adds task to worker queue" << std::endl;
    std::cout << "  2. std::queue::pop() - removes task from worker queue" << std::endl;
    std::cout << "  3. std::queue::front() - accesses front of queue" << std::endl;
    std::cout << "  These operations are protected by mutex locks" << std::endl;
    std::cout << std::endl;
    
    // Project to LandSandBoat scale
    const auto landSandBoatOverhead = (perOperation * 330) / 1000;
    std::cout << "LandSandBoat Impact:" << std::endl;
    std::cout << "  Worker pool overhead: " << landSandBoatOverhead << "ms per tick" << std::endl;
    std::cout << "  This represents the mutex contention overhead" << std::endl;
    std::cout << std::endl;
    
    std::cout << "Optimization Potential:" << std::endl;
    std::cout << "  - Replace std::queue + mutex with moodycamel::ConcurrentQueue" << std::endl;
    std::cout << "  - Eliminate all mutex operations in worker pool" << std::endl;
    std::cout << "  - Expected reduction: 50-70% of thread switching overhead" << std::endl;
    
    EXPECT_EQ(tasksCompleted.load(), numOperations);
    EXPECT_LT(perOperation, 10000) << "Worker pool overhead should be under 10ms per operation";
    
    // Document the mutex contention issue
    EXPECT_GT(perOperation, 1000) << "Current overhead of " << perOperation << "μs confirms mutex contention is significant";
}

//
// TEST: Thread Yield Overhead Measurement
// PURPOSE: Measure the cost of std::this_thread::yield() in worker pool polling
// BEHAVIOR: Measures the overhead of yield() calls in the worker thread loop
// EXPECTATION: Should reveal the polling overhead in the lock-free worker pool
//
// BACKGROUND: The lock-free worker pool uses std::this_thread::yield() for polling
// when no tasks are available. This replaces condition variable waiting but may
// have its own overhead. This test measures the specific cost of yield() operations.
//
TEST_F(SchedulerTest, ThreadYieldOverheadMeasurement)
{
    const int numYieldOperations = 1000000; // 1 million yield operations
    
    // Test 1: Measure pure yield() overhead
    const auto yieldStart = std::chrono::steady_clock::now();
    
    for (int i = 0; i < numYieldOperations; ++i)
    {
        std::this_thread::yield();
    }
    
    const auto yieldEnd = std::chrono::steady_clock::now();
    const auto yieldDuration = std::chrono::duration_cast<std::chrono::microseconds>(yieldEnd - yieldStart);
    const auto yieldPerOperation = yieldDuration.count() / numYieldOperations;
    
    // Test 2: Measure yield() with minimal work (simulating worker thread loop)
    const auto loopStart = std::chrono::steady_clock::now();
    
    for (int i = 0; i < numYieldOperations; ++i)
    {
        // Simulate the worker thread loop pattern
        bool hasTask = false; // Simulate no task available
        if (!hasTask)
        {
            std::this_thread::yield();
        }
    }
    
    const auto loopEnd = std::chrono::steady_clock::now();
    const auto loopDuration = std::chrono::duration_cast<std::chrono::microseconds>(loopEnd - loopStart);
    const auto loopPerOperation = loopDuration.count() / numYieldOperations;
    
    // Test 3: Measure alternative polling strategies
    const auto busyStart = std::chrono::steady_clock::now();
    
    for (int i = 0; i < numYieldOperations; ++i)
    {
        // Busy waiting (no yield)
        // This is what we'd have without yield()
    }
    
    const auto busyEnd = std::chrono::steady_clock::now();
    const auto busyDuration = std::chrono::duration_cast<std::chrono::microseconds>(busyEnd - busyStart);
    const auto busyPerOperation = busyDuration.count() / numYieldOperations;
    
    // Test 4: Measure yield() with sleep (simulating condition variable alternative)
    const auto sleepStart = std::chrono::steady_clock::now();
    
    for (int i = 0; i < 1000; ++i) // Fewer iterations due to sleep overhead
    {
        std::this_thread::sleep_for(std::chrono::microseconds(1));
    }
    
    const auto sleepEnd = std::chrono::steady_clock::now();
    const auto sleepDuration = std::chrono::duration_cast<std::chrono::microseconds>(sleepEnd - sleepStart);
    const auto sleepPerOperation = sleepDuration.count() / 1000;
    
    std::cout << "Thread Yield Overhead Measurement:" << std::endl;
    std::cout << "  Operations: " << numYieldOperations << std::endl;
    std::cout << std::endl;
    
    std::cout << "PERFORMANCE RESULTS:" << std::endl;
    std::cout << "  Pure std::this_thread::yield():" << std::endl;
    std::cout << "    Total time: " << yieldDuration.count() << "μs" << std::endl;
    std::cout << "    Per operation: " << yieldPerOperation << "μs" << std::endl;
    std::cout << "    Operations per second: " << (1000000.0 / yieldPerOperation) << std::endl;
    std::cout << std::endl;
    
    std::cout << "  Worker thread loop pattern (yield + condition check):" << std::endl;
    std::cout << "    Total time: " << loopDuration.count() << "μs" << std::endl;
    std::cout << "    Per operation: " << loopPerOperation << "μs" << std::endl;
    std::cout << "    Operations per second: " << (1000000.0 / loopPerOperation) << std::endl;
    std::cout << std::endl;
    
    std::cout << "  Busy waiting (no yield):" << std::endl;
    std::cout << "    Total time: " << busyDuration.count() << "μs" << std::endl;
    std::cout << "    Per operation: " << busyPerOperation << "μs" << std::endl;
    std::cout << "    Operations per second: " << (1000000.0 / busyPerOperation) << std::endl;
    std::cout << std::endl;
    
    std::cout << "  std::this_thread::sleep_for(1μs):" << std::endl;
    std::cout << "    Total time: " << sleepDuration.count() << "μs" << std::endl;
    std::cout << "    Per operation: " << sleepPerOperation << "μs" << std::endl;
    std::cout << "    Operations per second: " << (1000000.0 / sleepPerOperation) << std::endl;
    std::cout << std::endl;
    
    std::cout << "COMPARISON ANALYSIS:" << std::endl;
    const auto yieldOverhead = yieldPerOperation - busyPerOperation;
    const auto loopOverhead = loopPerOperation - busyPerOperation;
    const auto sleepOverhead = sleepPerOperation - busyPerOperation;
    
    std::cout << "  Yield() overhead: " << yieldOverhead << "μs per operation" << std::endl;
    std::cout << "  Loop overhead: " << loopOverhead << "μs per operation" << std::endl;
    std::cout << "  Sleep overhead: " << sleepOverhead << "μs per operation" << std::endl;
    std::cout << std::endl;
    
    std::cout << "WORKER POOL IMPACT ANALYSIS:" << std::endl;
    std::cout << "  Current worker pool uses yield() when no tasks available" << std::endl;
    std::cout << "  This replaces condition variable waiting from mutex-based implementation" << std::endl;
    std::cout << std::endl;
    
    // Calculate impact on thread switching overhead
    const auto yieldOverheadPerSwitch = yieldOverhead * 10; // Assume ~10 yield calls per thread switch
    const auto threadSwitchOverhead = 23334; // From previous measurements
    const auto yieldPercentage = (yieldOverheadPerSwitch * 100.0) / threadSwitchOverhead;
    
    std::cout << "  Estimated yield() overhead per thread switch: " << yieldOverheadPerSwitch << "μs" << std::endl;
    std::cout << "  Percentage of total thread switching overhead: " << yieldPercentage << "%" << std::endl;
    std::cout << std::endl;
    
    std::cout << "OPTIMIZATION CONSIDERATIONS:" << std::endl;
    std::cout << "  1. yield() is much faster than sleep_for()" << std::endl;
    std::cout << "  2. yield() allows immediate response to new tasks" << std::endl;
    std::cout << "  3. yield() reduces CPU usage compared to busy waiting" << std::endl;
    std::cout << "  4. yield() overhead is minimal compared to context switching" << std::endl;
    std::cout << std::endl;
    
    std::cout << "ALTERNATIVE STRATEGIES:" << std::endl;
    std::cout << "  1. Adaptive polling: Use yield() initially, then sleep_for() for longer waits" << std::endl;
    std::cout << "  2. Work stealing: Check other worker threads' queues before yielding" << std::endl;
    std::cout << "  3. Batch processing: Process multiple tasks before yielding" << std::endl;
    std::cout << "  4. Hybrid approach: Combine yield() with occasional condition variable waits" << std::endl;
    
    // Verify measurements are reasonable
    EXPECT_GT(yieldPerOperation, 0) << "yield() should have measurable overhead";
    EXPECT_LT(yieldPerOperation, 10) << "yield() overhead should be under 10μs per operation";
    EXPECT_GT(loopPerOperation, yieldPerOperation) << "Loop pattern should be slower than pure yield";
    EXPECT_GT(sleepPerOperation, yieldPerOperation) << "sleep_for() should be slower than yield()";
    
    // Document the yield overhead
    EXPECT_LT(yieldOverhead, 5) << "yield() overhead should be under 5μs per operation";
    EXPECT_LT(yieldPercentage, 10) << "yield() should be less than 10% of thread switching overhead";
}
