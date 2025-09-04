#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <coroutine>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <priority_queue>
#include <thread>
#include <vector>

#include <corororo/corororo.h>

namespace CoroRoro
{

//
// Forward declarations
//

class IntervalTask;
class CancellationToken;

//
// Scheduler
//
// High-performance coroutine scheduler with interval task factory and cancellation support.
// Uses priority queue-based timer system with single-execution guarantees.
//

class Scheduler final
{
public:
    //
    // Constructor & Destructor
    //

    explicit Scheduler(size_t workerThreadCount = 4);
    ~Scheduler();

    Scheduler(const Scheduler&)            = delete;
    Scheduler& operator=(const Scheduler&) = delete;
    Scheduler(Scheduler&&)                 = delete;
    Scheduler& operator=(Scheduler&&)      = delete;

    //
    // Basic Scheduling
    //

    // Schedule a task for immediate execution with automatic thread affinity
    template <ThreadAffinity Affinity, typename T>
    void schedule(detail::TaskBase<Affinity, T>&& task);

    // Process expired tasks and execute until completion
    // Called periodically from external event loop
    auto runExpiredTasks() -> std::chrono::milliseconds;
    auto runExpiredTasks(std::chrono::steady_clock::time_point referenceTime) -> std::chrono::milliseconds;

    //
    // Interval Task Factory API
    //

    // Schedule an interval task that executes immediately, then every interval
    template <typename Rep, typename Period, typename TaskFunction>
    auto scheduleInterval(std::chrono::duration<Rep, Period> interval,
                         TaskFunction&& taskFactory) -> CancellationToken;

    // Schedule a delayed task that executes once after delay
    template <typename Rep, typename Period, typename TaskFunction>
    auto scheduleDelayed(std::chrono::duration<Rep, Period> delay,
                        TaskFunction&& taskFactory) -> CancellationToken;

    //
    // Status and Information
    //

    auto isRunning() const -> bool;
    auto getWorkerThreadCount() const -> size_t;
    auto getInFlightTaskCount() const -> size_t;

private:
    //
    // Internal Methods
    //

    void workerLoop();
    void processExpiredTasks();
    void addIntervalTask(std::unique_ptr<IntervalTask> task);
    void removeIntervalTask(IntervalTask* task);

    template <ThreadAffinity Affinity>
    void scheduleHandleWithAffinity(std::coroutine_handle<> handle);

    template <ThreadAffinity Affinity>
    auto getNextTaskWithAffinity() -> std::coroutine_handle<>;

    void scheduleMainThreadTask(std::coroutine_handle<> handle);
    void scheduleWorkerThreadTask(std::coroutine_handle<> handle);
    auto getNextMainThreadTask() -> std::coroutine_handle<>;
    auto getNextWorkerThreadTask() -> std::coroutine_handle<>;

    void notifyTaskComplete();

    //
    // Member Variables
    //

    // Task queues
    moodycamel::ConcurrentQueue<std::coroutine_handle<>> mainThreadTasks_;
    moodycamel::ConcurrentQueue<std::coroutine_handle<>> workerThreadTasks_;

    // Worker thread management
    std::vector<std::thread> workerThreads_;
    std::mutex workerMutex_;
    std::condition_variable workerCondition_;

    // Timer system
    std::priority_queue<std::unique_ptr<IntervalTask>> intervalQueue_;
    std::mutex timerMutex_;

    // State
    std::atomic<bool> running_{true};
    std::atomic<size_t> inFlightTasks_{0};
};

//
// IntervalTask
//
// Represents a recurring task that executes at regular intervals.
// Uses factory pattern to create fresh task instances on each execution.
//

class IntervalTask final
{
public:
    //
    // Constructor & Destructor
    //

    IntervalTask(std::function<Task<void>()> factory,
                std::chrono::milliseconds interval,
                Scheduler* scheduler,
                bool isOneTime = false);

    ~IntervalTask();

    IntervalTask(const IntervalTask&)            = delete;
    IntervalTask& operator=(const IntervalTask&) = delete;
    IntervalTask(IntervalTask&&)                 = delete;
    IntervalTask& operator=(IntervalTask&&)      = delete;

    //
    // Execution Control
    //

    // Try to create a tracked task from the factory
    // Returns nullopt if a child task is already in flight (single-execution guarantee)
    auto createTrackedTask() -> std::optional<Task<void>>;

    // Execute the task (called by scheduler when timer expires)
    void execute();

    // Mark as cancelled
    void markCancelled();

    //
    // Status and Information
    //

    auto isCancelled() const -> bool;
    auto getNextExecution() const -> std::chrono::steady_clock::time_point;
    void updateNextExecution();

    //
    // Cancellation Token Management
    //

    void setToken(CancellationToken* token);
    void clearTokenPointer();

private:
    //
    // Internal Methods
    //

    Task<void> createTrackedWrapper(Task<void> originalTask);

    //
    // Member Variables
    //

    std::function<Task<void>()> factory_;
    std::chrono::steady_clock::time_point nextExecution_;
    std::chrono::milliseconds interval_;
    Scheduler* scheduler_;
    CancellationToken* token_{nullptr};
    std::atomic<bool> cancelled_{false};
    std::atomic<bool> hasActiveChild_{false};
    bool isOneTime_{false};
};

//
// CancellationToken
//
// Provides safe cancellation of interval and delayed tasks.
// Uses bidirectional pointer management for cleanup.
//

class CancellationToken final
{
public:
    //
    // Constructor & Destructor
    //

    CancellationToken(IntervalTask* task, Scheduler* scheduler);
    ~CancellationToken();

    CancellationToken(const CancellationToken&)            = delete;
    CancellationToken& operator=(const CancellationToken&) = delete;
    CancellationToken(CancellationToken&&)                 = default;
    CancellationToken& operator=(CancellationToken&&)      = default;

    //
    // Cancellation Control
    //

    void cancel();
    auto isCancelled() const -> bool;
    auto isValid() const -> bool;
    explicit operator bool() const;

    //
    // Task Management
    //

    void setTask(IntervalTask* task);
    void clearTaskPointer();

private:
    //
    // Member Variables
    //

    IntervalTask* task_{nullptr};
    Scheduler* scheduler_;
    std::atomic<bool> cancelled_{false};
};

//
// Comparison operators for priority queue
//

bool operator<(const IntervalTask& lhs, const IntervalTask& rhs);

} // namespace CoroRoro
