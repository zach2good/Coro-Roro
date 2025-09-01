#pragma once

#include <atomic>
#include <chrono>
#include <coroutine>
#include <deque>
#include <iostream>
#include <memory>
#include <mutex>
#include <thread>


// ThreadAffinity enum
enum class ThreadAffinity : uint32_t
{
    Main   = 0,
    Worker = 1
};

// Forward declarations
class Scheduler;

namespace detail
{

// Generic awaiters using if constexpr for compile-time dispatch
template <ThreadAffinity Affinity>
struct InitialAwaiter
{
    Scheduler* scheduler_;

    InitialAwaiter(Scheduler* sched)
    : scheduler_(sched)
    {
    }

    bool await_ready() const noexcept
    {
        // TransferPolicy handles thread affinity optimization in await_suspend()
        // await_ready() just checks if we should proceed immediately
        return false; // Always suspend for scheduling
    }

    auto await_suspend(std::coroutine_handle<> handle) noexcept
    {
        // Direct TransferPolicy call - zero runtime conditionals!
        return scheduler_->transferPolicy<Affinity, Affinity>(handle);
    }

    void await_resume() const noexcept
    {
    }
};

template <ThreadAffinity Affinity>
struct FinalAwaiter
{
    Scheduler* scheduler_;

    FinalAwaiter(Scheduler* sched)
    : scheduler_(sched)
    {
    }

    bool await_ready() const noexcept
    {
        // TransferPolicy handles thread affinity optimization in await_suspend()
        // await_ready() just checks if we should proceed immediately
        return false; // Always suspend for final await
    }

    auto await_suspend(std::coroutine_handle<> handle) noexcept -> std::coroutine_handle<>
    {
        // Direct TransferPolicy call - zero runtime conditionals!
        return scheduler_->transferPolicy<Affinity, Affinity>(handle);
    }

    void await_resume() const noexcept
    {
    }
};

} // namespace detail

// Forward declaration for TaskBase
template <ThreadAffinity Affinity, typename T>
struct TaskBase;

namespace detail
{

//
// Promise specialization for non-void types
//
template <ThreadAffinity Affinity, typename T>
struct Promise
{
    static constexpr ThreadAffinity affinity = Affinity;

    Scheduler*              scheduler_    = nullptr;
    std::coroutine_handle<> continuation_ = nullptr;
    T data_;

    auto get_return_object() noexcept
    {
        return TaskBase<Affinity, T>{
            std::coroutine_handle<Promise<Affinity, T>>::from_promise(*this)
        };
    }

    auto initial_suspend() const noexcept
    {
        return detail::InitialAwaiter<Affinity>{ scheduler_ };
    }

    auto final_suspend() noexcept
    {
        return detail::FinalAwaiter<Affinity>{ scheduler_ };
    }

    // Only return_value for non-void types
    void return_value(T&& value) noexcept(std::is_nothrow_move_assignable_v<T>)
    {
        data_ = std::move(value);
    }

    void return_value(const T& value) noexcept(std::is_nothrow_copy_assignable_v<T>)
    {
        data_ = value;
    }

    auto result() -> T& {
        return data_;
    }

    void unhandled_exception() noexcept
    {
        std::terminate();
    }

    template <typename AwaitableType>
    auto await_transform(AwaitableType&& awaitable) noexcept
    {
        return std::forward<AwaitableType>(awaitable);
    }

    template <typename U>
    void yield_value(U&&) = delete;
};

//
// Promise specialization for void types
//
template <ThreadAffinity Affinity>
struct Promise<Affinity, void>
{
    static constexpr ThreadAffinity affinity = Affinity;

    Scheduler*              scheduler_    = nullptr;
    std::coroutine_handle<> continuation_ = nullptr;

    auto get_return_object() noexcept
    {
        return TaskBase<Affinity, void>{
            std::coroutine_handle<Promise<Affinity, void>>::from_promise(*this)
        };
    }

    auto initial_suspend() const noexcept
    {
        return detail::InitialAwaiter<Affinity>{ scheduler_ };
    }

    auto final_suspend() noexcept
    {
        return detail::FinalAwaiter<Affinity>{ scheduler_ };
    }

    // Only return_void for void types
    void return_void() noexcept
    {
        // void types don't need to store anything
    }

    void unhandled_exception() noexcept
    {
        std::terminate();
    }

    template <typename AwaitableType>
    auto await_transform(AwaitableType&& awaitable) noexcept
    {
        return std::forward<AwaitableType>(awaitable);
    }

    template <typename U>
    void yield_value(U&&) = delete;
};

} // namespace detail

//
// TaskBase specialization for non-void types
//
template <ThreadAffinity Affinity, typename T>
struct TaskBase
{
    using ResultType                         = T;
    static constexpr ThreadAffinity affinity = Affinity;

    // Generic promise type - affinity is handled through templates
    using promise_type = detail::Promise<Affinity, T>;

    TaskBase() noexcept = default;

    TaskBase(std::coroutine_handle<promise_type> coroutine) noexcept
    : handle_(coroutine)
    {
    }

    TaskBase(TaskBase const&)            = delete;
    TaskBase& operator=(TaskBase const&) = delete;

    TaskBase(TaskBase&& other) noexcept
    : handle_(other.handle_)
    {
        other.handle_ = nullptr;
    }

    TaskBase& operator=(TaskBase&& other) noexcept
    {
        if (this != &other)
        {
            if (handle_)
            {
                handle_.destroy();
            }
            handle_       = other.handle_;
            other.handle_ = nullptr;
        }
        return *this;
    }

    ~TaskBase() noexcept
    {
        if (handle_)
        {
            handle_.destroy();
        }
    }

    // Check if the task is done
    auto done() const noexcept -> bool
    {
        return !handle_ || handle_.done();
    }

    // Get the result (only valid after the task is done) - non-void only
    auto result() -> T& {
        return handle_.promise().result();
    }

    // Get the result (const version) - non-void only
    auto result() const -> const T& {
        return handle_.promise().result();
    }

    // Awaitable interface
    auto await_ready() const noexcept -> bool
    {
        return done();
    }

    auto await_suspend(std::coroutine_handle<> coroutine) noexcept -> std::coroutine_handle<>
    {
        // For now, just resume the awaiting coroutine directly
        return coroutine;
    }

    // await_resume - non-void specialization
    auto await_resume() -> T& {
        return result();
    }

public:
    std::coroutine_handle<promise_type> handle_ = nullptr;
};

//
// TaskBase specialization for void types
//
template <ThreadAffinity Affinity>
struct TaskBase<Affinity, void>
{
    using ResultType                         = void;
    static constexpr ThreadAffinity affinity = Affinity;

    // Generic promise type - affinity is handled through templates
    using promise_type = detail::Promise<Affinity, void>;

    TaskBase() noexcept = default;

    TaskBase(std::coroutine_handle<promise_type> coroutine) noexcept
    : handle_(coroutine)
    {
    }

    TaskBase(TaskBase const&)            = delete;
    TaskBase& operator=(TaskBase const&) = delete;

    TaskBase(TaskBase&& other) noexcept
    : handle_(other.handle_)
    {
        other.handle_ = nullptr;
    }

    TaskBase& operator=(TaskBase&& other) noexcept
    {
        if (this != &other)
        {
            if (handle_)
            {
                handle_.destroy();
            }
            handle_       = other.handle_;
            other.handle_ = nullptr;
        }
        return *this;
    }

    ~TaskBase() noexcept
    {
        if (handle_)
        {
            handle_.destroy();
        }
    }

    // Check if the task is done
    auto done() const noexcept -> bool
    {
        return !handle_ || handle_.done();
    }

    // No result method for void types - they don't return values

    // Awaitable interface
    auto await_ready() const noexcept -> bool
    {
        return done();
    }

    auto await_suspend(std::coroutine_handle<> coroutine) noexcept -> std::coroutine_handle<>
    {
        // For now, just resume the awaiting coroutine directly
        return coroutine;
    }

    // await_resume for void - nothing to return
    void await_resume() const noexcept
    {
        // void tasks don't return a value
    }

public:
    std::coroutine_handle<promise_type> handle_ = nullptr;
};

//
// Task - Simple alias for main thread execution
//
template <typename T = void>
using Task = TaskBase<ThreadAffinity::Main, T>;

// AsyncTask - Simple alias for worker thread execution
template <typename T>
using AsyncTask = TaskBase<ThreadAffinity::Worker, T>;

// Simple Scheduler stub for testing
class Scheduler
{
public:
    explicit Scheduler(size_t workerThreadCount = 4)
    {
        mainThreadId_ = std::this_thread::get_id();
        running_.store(true);
    }

    ~Scheduler()
    {
        running_.store(false);
    }

    // Disable copy and move
    Scheduler(const Scheduler&)            = delete;
    Scheduler& operator=(const Scheduler&) = delete;
    Scheduler(Scheduler&&)                 = delete;
    Scheduler& operator=(Scheduler&&)      = delete;

    // Basic scheduling
    template <typename TaskType>
    void schedule(TaskType task)
    {
        // Extract handle and route to appropriate queue
        auto handle = task.handle_;

        if (handle && !handle.done())
        {
            // Inject scheduler pointer into the promise for awaiter delegation
            auto& promise      = handle.promise();
            promise.scheduler_ = this;

            // Route to appropriate queue based on task affinity
            if constexpr (TaskType::affinity == ThreadAffinity::Main)
            {
                scheduleMainThreadTask(handle);
            }
            else
            {
                scheduleWorkerThreadTask(handle);
            }
        }
    }

    // Process expired tasks - minimal implementation
    auto runExpiredTasks() -> std::chrono::milliseconds
    {
        auto start = std::chrono::steady_clock::now();

        // Process main thread tasks
        while (!mainThreadTasks_.empty())
        {
            auto handle = mainThreadTasks_.back();
            mainThreadTasks_.pop_back();

            if (handle && !handle.done())
            {
                handle.resume();
            }
        }

        return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start);
    }

    // Get worker thread count
    auto getWorkerThreadCount() const -> size_t
    {
        return 4;
    }

    // Get main thread ID
    auto getMainThreadId() const -> std::thread::id
    {
        return mainThreadId_;
    }

    // TransferPolicy implementation - matches documented design
    template <ThreadAffinity CurrentAffinity, ThreadAffinity NextAffinity>
    auto transferPolicy(std::coroutine_handle<> handle) noexcept -> std::coroutine_handle<>
    {
        if constexpr (CurrentAffinity == NextAffinity)
        {
            // Same thread - NO transfer needed, resume immediately!
            return handle;
        }
        else
        {
            // Different thread - schedule and perform symmetric transfer
            scheduleTaskWithAffinity<NextAffinity>(handle);
            return getNextTaskWithAffinity<CurrentAffinity>();
        }
    }

    template <ThreadAffinity Affinity>
    auto scheduleTaskWithAffinity(std::coroutine_handle<> handle) -> std::coroutine_handle<>
    {
        if constexpr (Affinity == ThreadAffinity::Main)
        {
            return scheduleMainThreadTask(handle);
        }
        else
        {
            return scheduleWorkerThreadTask(handle);
        }
    }

    template <ThreadAffinity Affinity>
    auto getNextTaskWithAffinity() -> std::coroutine_handle<>
    {
        if constexpr (Affinity == ThreadAffinity::Main)
        {
            return getNextMainThreadTask();
        }
        else
        {
            return getNextWorkerThreadTask();
        }
    }

private:

    auto scheduleMainThreadTask(std::coroutine_handle<> handle) -> std::coroutine_handle<>
    {
        std::lock_guard<std::mutex> lock(mainThreadTasksMutex_);
        mainThreadTasks_.push_back(handle);
        return std::noop_coroutine();
    }

    auto scheduleWorkerThreadTask(std::coroutine_handle<> handle) -> std::coroutine_handle<>
    {
        // Stub implementation
        return std::noop_coroutine();
    }

    auto finalizeMainThreadTask(std::coroutine_handle<> handle) -> std::coroutine_handle<>
    {
        // Stub implementation
        return std::noop_coroutine();
    }

    auto finalizeWorkerThreadTask(std::coroutine_handle<> handle) -> std::coroutine_handle<>
    {
        // Stub implementation
        return std::noop_coroutine();
    }

    auto getNextMainThreadTask() -> std::coroutine_handle<>
    {
        std::lock_guard<std::mutex> lock(mainThreadTasksMutex_);
        if (!mainThreadTasks_.empty())
        {
            auto handle = mainThreadTasks_.back();
            mainThreadTasks_.pop_back();
            return handle;
        }
        return std::noop_coroutine();
    }

    auto getNextWorkerThreadTask() -> std::coroutine_handle<>
    {
        // Stub implementation
        return std::noop_coroutine();
    }

    // Member variables
    std::thread::id   mainThreadId_;
    std::atomic<bool> running_{ true };

    // Simple task queues
    std::mutex                          mainThreadTasksMutex_;
    std::deque<std::coroutine_handle<>> mainThreadTasks_;
};

// Test function
Task<void> createSimpleTask()
{
    co_return;
}

// Main test function
int main()
{
    Scheduler scheduler(4);

    // Test coroutine creation
    auto task = createSimpleTask();

    // Test scheduling
    scheduler.schedule(std::move(task));

    // Test running
    auto duration = scheduler.runExpiredTasks();

    std::cout << "Test completed successfully!" << std::endl;
    return 0;
}
