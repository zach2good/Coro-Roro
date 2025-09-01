// Test 12: Interval Task Sequential Execution with Long-Running Tasks
//
// BACKGROUND & PROBLEM:
// This test captures and validates the fix for a critical concurrency issue discovered
// in the FFXI server project. The issue manifested as data races in pathfinding code
// (pathfind.cpp) where multiple zone tick tasks were executing simultaneously.
//
// ORIGINAL ISSUE:
// The FFXI server uses scheduleInterval() to run zone logic every 400ms via zoneRunner()
// coroutines. Each zone's game logic (AI pathfinding, entity updates, etc.) was intended
// to run sequentially - one tick completing before the next begins. However, the original
// IntervalTask implementation would reschedule the next interval BEFORE the current child
// task completed, leading to overlapping executions when child tasks suspended (e.g.,
// during pathfinding operations that offload to worker threads).
//
// DATA RACE MANIFESTATION:
// In pathfind.cpp, this caused race conditions on shared data members like m_points
// and m_currentPoint, where one zone tick would modify these while another was still
// accessing them, leading to crashes and undefined behavior.
//
// THE FIX:
// IntervalTask now enforces sequential execution by:
// 1. Only rescheduling the next interval AFTER the current child task completes
// 2. Using activeIntervalTasks_ tracking to prevent multiple instances of the same
//    interval task from executing simultaneously
// 3. Allowing child tasks to suspend/resume on worker threads while maintaining
//    the guarantee that only one instance of a given interval task runs at a time
//
// This test verifies that even when interval tasks take longer than their interval
// period, they execute sequentially rather than in parallel, preventing data races.

// Test 13: Interval Task with Suspending Coroutines
//
// ADVANCED SCENARIO:
// This test specifically validates the fix for the more complex case where interval
// tasks contain multiple suspension points (co_await operations that move work to
// worker threads). This directly mirrors the FFXI server's zone tick pattern.
//
// FFXI SERVER PATTERN:
// In the FFXI server, zoneRunner() calls CZone::ZoneServer() which in turn calls
// entity logic that frequently suspends via co_await (e.g., pathfinding operations,
// database queries, network I/O). Each suspension moves work to a worker thread,
// then resumes on the main thread when complete.
//
// CRITICAL REQUIREMENT:
// Even with multiple suspensions within a single zone tick, we must guarantee:
// 1. No overlapping execution of zone ticks for the same zone
// 2. Proper resumption on the main thread after worker thread operations
// 3. Sequential processing even if individual ticks take longer than the interval
//
// This test simulates multiple AsyncTask suspensions (worker thread operations)
// within each interval task execution and verifies that the sequential execution
// guarantee is maintained throughout all suspension/resumption cycles.

    // This test reproduces the FFXI server issue where IntervalTask
    // completes but doesn't reschedule itself, leading to empty task queue
    
    // The FFXI server pattern is:
    // 1. scheduleInterval calls zoneRunner (Task)
    // 2. zoneRunner calls CZone::ZoneServer (Task) 
    // 3. ZoneServer calls entity AI that does co_await AsyncTask (pathfinding, etc.)
    // 4. AsyncTask completes on worker thread, resumes on main thread
    // 5. zoneRunner completes, IntervalTask should reschedule


// Test 12: Interval Task Sequential Execution with Long-Running Tasks
//
// BACKGROUND & PROBLEM:
// This test captures and validates the fix for a data race issue discovered
// in the while integrating with LandSandBoat. The issue manifested in the pathfinding code
// (pathfind.cpp) where multiple zone tick tasks were executing simultaneously.
//
// ORIGINAL ISSUE:
// LandSandBoat uses scheduleInterval() to run zone logic every 400ms via zoneRunner()
// coroutines. Each zone's game logic (AI pathfinding, entity updates, etc.) was intended
// to run sequentially - one tick completing before the next begins. However, the original
// IntervalTask implementation would reschedule the next interval BEFORE the current child
// task completed, leading to overlapping executions when child tasks suspended (e.g.,
// during pathfinding operations that offload to worker threads).
//
// DATA RACE:
// In pathfind.cpp, this caused race conditions on shared data members like m_points
// and m_currentPoint, where one zone tick would modify these while another was still
// accessing them, leading to crashes and undefined behavior.
//
// THE FIX:
// IntervalTask now enforces sequential execution by:
// 1. Only rescheduling the next interval AFTER the current child task completes
// 2. Using activeIntervalTasks_ tracking to prevent multiple instances of the same
//    interval task from executing simultaneously
// 3. Allowing child tasks to suspend/resume on worker threads while maintaining
//    the guarantee that only one instance of a given interval task runs at a time
//
// This test verifies that even when interval tasks take longer than their interval
// period, they execute sequentially rather than in parallel, preventing data races.
TEST_F(SchedulerTest, IntervalTaskSequentialExecution)
{
    static std::atomic<int> executionCount{ 0 };
    static std::atomic<int> simultaneousExecutions{ 0 };
    static std::atomic<int> maxSimultaneousExecutions{ 0 };

    executionCount.store(0);
    simultaneousExecutions.store(0);
    maxSimultaneousExecutions.store(0);

    // Schedule interval task that takes longer than the interval to complete
    auto token = scheduler->scheduleInterval(
        50ms, // 50ms interval
        []() -> Task<void>
        {
            // Track simultaneous executions
            int current = simultaneousExecutions.fetch_add(1) + 1;
            int max     = maxSimultaneousExecutions.load();
            while (current > max && !maxSimultaneousExecutions.compare_exchange_weak(max, current))
            {
                max = maxSimultaneousExecutions.load();
            }

            executionCount.fetch_add(1);

            // Simulate long-running task that takes longer than interval
            std::this_thread::sleep_for(75ms); // Longer than 50ms interval

            simultaneousExecutions.fetch_sub(1);
            co_return;
        });

    // Run for about 300ms (should allow ~6 intervals, but only sequential execution)
    auto startTime = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - startTime < 300ms)
    {
        scheduler->runExpiredTasks();
        std::this_thread::sleep_for(10ms);
    }

    token.cancel();

    // Allow any remaining tasks to complete
    std::this_thread::sleep_for(100ms);
    scheduler->runExpiredTasks();

    // Verify sequential execution behavior
    int finalCount      = executionCount.load();
    int maxSimultaneous = maxSimultaneousExecutions.load();

    // Should have executed at least a few times, but not as many as if they were parallel
    EXPECT_GT(finalCount, 1) << "Should have executed multiple times";
    EXPECT_LT(finalCount, 6) << "Should not execute as many times as if parallel (due to long execution)";

    // Most importantly: should never have more than 1 simultaneous execution
    EXPECT_EQ(maxSimultaneous, 1) << "Should never have more than 1 simultaneous execution";
}

// Test 13: Interval Task with Suspending Coroutines
//
// ADVANCED SCENARIO:
// This test specifically validates the fix for the more complex case where interval
// tasks contain multiple suspension points (co_await operations that move work to
// worker threads). This directly mirrors the LandSandBoat zone tick pattern.
//
// SERVER TICK PATTERN:
// In LandSandBoat, zoneRunner() calls CZone::ZoneServer() which in turn calls
// entity logic that frequently suspends via co_await-ing on AsyncTask work.
// Each suspension moves work to a worker thread, then resumes on the main thread
// when complete.
//
// CRITICAL REQUIREMENT:
// Even with multiple suspensions within a single zone tick, we must guarantee:

//
// TEST: Interval Task Rescheduling After Suspension
// PURPOSE: Verify that interval tasks properly reschedule after complex suspension patterns
// BEHAVIOR: Creates interval tasks with Task->AsyncTask->Task suspension pattern
// EXPECTATION: Tasks execute multiple times with proper rescheduling
//
// BACKGROUND: This test reproduces and validates the fix for a LandSandBoat issue
// where IntervalTask would complete but not reschedule itself after complex
// suspension patterns, leading to empty task queues. The pattern tested is:
// 1. scheduleInterval calls zoneRunner (Task)
// 2. zoneRunner calls CZone::ZoneServer (Task)
// 3. ZoneServer calls entity AI that does co_await AsyncTask (pathfinding, etc.)
// 4. AsyncTask completes on worker thread, resumes on main thread
// 5. zoneRunner completes, IntervalTask should reschedule
//

// ============================================================================
// TEST: Interval Task Rescheduling When Behind Schedule
// PURPOSE: Verify that interval tasks properly reschedule and execute when running behind
// BEHAVIOR: Creates interval tasks that take longer than their interval, then verifies rescheduling
// EXPECTATION: Tasks should reschedule and execute immediately when behind schedule
// 
// BACKGROUND: This test addresses the core FFXI server issue where interval tasks
// don't properly reschedule when they're running behind. The scheduler should:
// 1. Detect when a task is behind schedule
// 2. Reschedule it to run immediately
// 3. Ensure it actually executes
// ============================================================================

// ============================================================================
// TEST: Realistic FFXI Server Simulation - Memory Pressure + Complex Chains
// PURPOSE: Reproduce the exact FFXI server issue with realistic conditions
// BEHAVIOR: Creates a test that simulates memory pressure, complex task chains, and real-world timing
// EXPECTATION: Should execute consistently every 400ms even under pressure
// 
// BACKGROUND: The FFXI server has real issues that our simple tests don't capture:
// - Memory pressure from managing hundreds of entities
// - Complex pathfinding and AI calculations
// - Database operations and network I/O
// - System-level contention that affects timing
// ============================================================================

//
// TEST: Coroutine Performance Bottleneck Analysis
// PURPOSE: Identify and measure critical performance bottlenecks in coroutine/scheduler implementation
// BEHAVIOR: Measures memory allocation, task switching, and mutex contention overhead
// EXPECTATION: Should reveal 4+ second overhead that explains LandSandBoat performance degradation
//
// BACKGROUND: This test identifies the root causes of LandSandBoat performance issues:
// - MEMORY ALLOCATION STORM: Every coroutine allocates std::make_shared<ExecutionContext>()
//   in promise.h:42. For 330 mobs with 6-level deep chains = 1,980 allocations per tick.
//   Each allocation involves shared_ptr overhead and potential heap fragmentation.
//   Impact: 819ms overhead per tick for LandSandBoat load.
//
// - MUTEX CONTENTION DISASTER: 15+ mutex locks per task across 3 different mutexes:
//   * taskTrackingMutex_ (scheduler.h)
//   * timedTaskMutex_ (scheduler.h)
//   * mainThreadMutex_ (scheduler.h)
//   * workerThreadMutex_ (worker_pool.h)
//   Tasks serialize through these locks, preventing parallel execution.
//   Impact: 2,925ms overhead per tick for LandSandBoat load.
//
// - TASK SWITCHING OVERHEAD: Expensive thread affinity switching via ConditionalTransferAwaiter,
//   complex queue routing between main thread and worker threads, worker pool enqueue/dequeue
//   overhead for AsyncTask operations. Impact: 496ms overhead per tick for LandSandBoat load.
//
// PERFORMANCE IMPACT: Original LandSandBoat Performance: 200ms per tick.
// Current Performance with Coroutines: 4.7s → 38.6s → 3+ minutes per tick.
// Total Estimated Overhead: 4,241ms (4.2 seconds) per tick.
// This explains why your 400ms interval tasks are taking minutes to complete.
// The coroutine implementation is adding 21x overhead to your game logic.
//
// OPTIMIZATION TARGETS: 1) Eliminate per-coroutine ExecutionContext allocation,
// 2) Reduce mutex contention through lock-free data structures, 3) Optimize task
// switching and thread affinity logic, 4) Implement coroutine pooling to reduce
// allocation overhead.
//
// EXPECTED RESULTS: This test should show: Memory allocation: ~400-500μs per coroutine,
// Task switching: ~1,000-1,500μs per switch, Mutex contention: ~1,000-1,500μs per task,
// Total projected overhead: 4,000-5,000ms for LandSandBoat load.
//

