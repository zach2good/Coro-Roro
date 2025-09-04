#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <coroutine>
#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <queue>
#include <sstream>
#include <thread>
#include <type_traits>
#include <variant>
#include <vector>

// Just in case we're running on Compiler Explorer!
#if __has_include(<concurrentqueue/concurrentqueue.h>)
#include <concurrentqueue/concurrentqueue.h>
#else
#define RUNNING_ON_GODBOLT
// clang-format off
#include <https://raw.githubusercontent.com/cameron314/concurrentqueue/refs/heads/master/concurrentqueue.h>
// clang-format on
#endif

//
// From: https://lewissbaker.github.io/2020/05/11/understanding_symmetric_transfer
//
// A tweak was made to the design of coroutines in 2018 to add a capability called “symmetric transfer”,
// which allows you to suspend one coroutine and resume another coroutine without consuming any
// additional stack-space. This allows for much simpler and more efficient implementation of async
// coroutine types without sacrificing any of the safety aspects needed to guard against stack-overflow.
//

//
// Symmetric Transfer: Your understanding and the implementation are correct. The key is that `await_suspend`
// returns another coroutine handle (std::coroutine_handle<>). This tells the compiler to switch directly
// to the new coroutine's context without unwinding the stack, which is the core of "symmetric transfer".
//
// In this codebase:
// - Direct Transfer (Fast Path): Occurs when co_awaiting a task of the same affinity. This is like a non-blocking function call.
// - Symmetric Transfer (Slow Path): Occurs when co_awaiting a task of a different affinity. The new task is sent to the scheduler, and
//   the current thread immediately requests a different task to execute to avoid being idle. We then symmetric transfer to the new task.
//

//
// Macros
//

#if defined(__GNUC__) || defined(__clang__)
#define FORCE_INLINE inline __attribute__((always_inline))
#define HOT_PATH     __attribute__((hot))
#define COLD_PATH    __attribute__((cold))
#else
#define FORCE_INLINE inline
#define HOT_PATH
#define COLD_PATH
#endif

#define LIKELY   [[likely]]
#define UNLIKELY [[unlikely]]

#define CACHE_ALIGN alignas(std::hardware_destructive_interference_size)

#define NO_DISCARD [[nodiscard]]

// TODO
#define ASSERT_UNREACHABLE() std::abort()

namespace CoroRoro
{

//
// Logging
//

inline void coro_log(const std::string& message)
{
    static auto mainThreadString = std::this_thread::get_id();
    std::cout << ((std::this_thread::get_id() == mainThreadString) ? "[Main Thread] " : "[Worker Thread] ") << message << std::endl;
}

//
// Enums
//

enum class ThreadAffinity : uint8_t
{
    Main   = 0,
    Worker = 1,
};

//
// Forward declarations
//

class Scheduler;
class IntervalTask;
class CancellationToken;

namespace detail
{
template <ThreadAffinity Affinity, typename T>
struct TaskBase;

template <typename Derived, ThreadAffinity Affinity, typename T>
struct PromiseBase;

template <ThreadAffinity Affinity, typename T>
struct Promise;

template <ThreadAffinity From, ThreadAffinity To>
struct Continuation;
} // namespace detail

//
// Simple Scheduler for use demonstrating the rest of the
// coroutine machinery
//

class Scheduler final
{
public:
    explicit Scheduler(size_t workerThreadCount = 4)
    {
        running_.store(true);

        workerThreads_.reserve(workerThreadCount);
        for (size_t i = 0; i < workerThreadCount; ++i)
        {
            workerThreads_.emplace_back(
                [this]
                {
                    workerLoop();
                });
        }
    }

    ~Scheduler()
    {
        running_.store(false);
        workerCondition_.notify_all();

        for (auto& thread : workerThreads_)
        {
            if (thread.joinable())
            {
                thread.join();
            }
        }
    }

    Scheduler(const Scheduler&)            = delete;
    Scheduler& operator=(const Scheduler&) = delete;
    Scheduler(Scheduler&&)                 = delete;
    Scheduler& operator=(Scheduler&&)      = delete;

    void notifyTaskComplete()
    {
        inFlightTasks_.fetch_sub(1, std::memory_order_relaxed);
    }

    // Takes a temporary Task object (an r-value) and assumes ownership of its
    // coroutine handle. This is a "sink" function.
    template <ThreadAffinity Affinity, typename T>
    HOT_PATH void schedule(detail::TaskBase<Affinity, T>&& task)
    {
        const auto handle = task.handle_;
        if (handle && !handle.done())
            LIKELY
            {
                inFlightTasks_.fetch_add(1, std::memory_order_relaxed);
                handle.promise().scheduler_ = this;

                // The coroutine's lifetime is now managed by the scheduler. We must
                // release the handle from the task object that was passed in to
                // prevent it from being destroyed when it goes out of scope.
                task.handle_ = nullptr;

                scheduleHandleWithAffinity<Affinity>(handle);
            }
    }

    // Schedule a callable that returns a Task/AsyncTask
    // Supports lambdas, std::bind, function objects, etc.
    template <typename Callable>
    void schedule(Callable&& callable)
        requires std::is_invocable_v<Callable> && 
                 requires { std::invoke_result_t<Callable>{}; } &&
                 requires { std::invoke_result_t<Callable>{}.handle_; }
    {
        auto task = std::forward<Callable>(callable)();
        schedule(std::move(task));
    }

    HOT_PATH auto runExpiredTasks() -> std::chrono::milliseconds
    {
        auto start = std::chrono::steady_clock::now();
        
        // Process expired interval tasks
        processExpiredIntervalTasks();

        while (inFlightTasks_.load(std::memory_order_acquire) > 0)
        {
            if (auto task = getNextMainThreadTask(); task && !task.done())
            {
                task.resume();
            }
            else
            {
                // If the main thread runs out of work and we've still got tasks in flight,
                // we should help out by running worker tasks until everything is done, or
                // until we get more main thread tasks.
                if (auto workerTask = getNextWorkerThreadTask(); workerTask && !workerTask.done())
                {
                    workerTask.resume();
                }
                else
                {
                    // If both queues are empty, yield.
                    std::this_thread::yield();
                }
            }
        }

        const auto end = std::chrono::steady_clock::now();
        return std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    }

    auto runExpiredTasks(std::chrono::steady_clock::time_point /* referenceTime */) -> std::chrono::milliseconds
    {
        auto start = std::chrono::steady_clock::now();
        
        // Process expired interval tasks
        processExpiredIntervalTasks();

        while (inFlightTasks_.load(std::memory_order_acquire) > 0)
        {
            if (auto task = getNextMainThreadTask(); task && !task.done())
            {
                task.resume();
            }
            else
            {
                // If the main thread runs out of work and we've still got tasks in flight,
                // we should help out by running worker tasks until everything is done, or
                // until we get more main thread tasks.
                if (auto workerTask = getNextWorkerThreadTask(); workerTask && !workerTask.done())
                {
                    workerTask.resume();
                }
                else
                {
                    // If both queues are empty, yield.
                    std::this_thread::yield();
                }
            }
        }

        const auto end = std::chrono::steady_clock::now();
        return std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    }

    //
    // Interval Task Factory API
    //

    // Schedule an interval task that executes immediately, then every interval
    // Callable must return Task<void> (return values are discarded)
    // Supports lambdas, std::bind, function objects, etc.
    template <typename Rep, typename Period, typename Callable>
    auto scheduleInterval(std::chrono::duration<Rep, Period> interval,
                         Callable&& callable) -> CancellationToken;

    // Schedule a delayed task that executes once after delay
    // Callable must return Task<void> (return values are discarded)
    // Supports lambdas, std::bind, function objects, etc.
    template <typename Rep, typename Period, typename Callable>
    auto scheduleDelayed(std::chrono::duration<Rep, Period> delay,
                        Callable&& callable) -> CancellationToken;

    //
    // Status and Information
    //

    auto isRunning() const -> bool
    {
        return running_.load();
    }

    auto getWorkerThreadCount() const -> size_t
    {
        return workerThreads_.size();
    }

    auto getInFlightTaskCount() const -> size_t
    {
        return inFlightTasks_.load();
    }

    template <ThreadAffinity Affinity>
    FORCE_INLINE HOT_PATH void scheduleHandleWithAffinity(std::coroutine_handle<> handle) noexcept
    {
        if constexpr (Affinity == ThreadAffinity::Main)
        {
            scheduleMainThreadTask(handle);
        }
        else
        {
            scheduleWorkerThreadTask(handle);
        }
    }

    template <ThreadAffinity Affinity>
    FORCE_INLINE HOT_PATH auto getNextTaskWithAffinity() noexcept -> std::coroutine_handle<>
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
    void workerLoop()
    {
        while (running_.load())
        {
            std::coroutine_handle<> task = nullptr;

            // Try to get a task
            if (workerThreadTasks_.try_dequeue(task))
            {
                task.resume();
            }
            else
            {
                // If no task was found, go to sleep and wait for a notification.
                std::unique_lock<std::mutex> lock(workerMutex_);
                workerCondition_.wait(
                    lock,
                    [this]
                    {
                        return !running_.load() || workerThreadTasks_.size_approx() > 0;
                    });
            }
        }
    }

    FORCE_INLINE void scheduleMainThreadTask(std::coroutine_handle<> handle) noexcept
    {
        mainThreadTasks_.enqueue(handle);
    }

    void scheduleWorkerThreadTask(std::coroutine_handle<> handle) noexcept
    {
        workerThreadTasks_.enqueue(handle);
        workerCondition_.notify_one();
    }

    FORCE_INLINE auto getNextMainThreadTask() noexcept -> std::coroutine_handle<>
    {
        std::coroutine_handle<> handle = nullptr;
        if (mainThreadTasks_.try_dequeue(handle))
        {
            return handle;
        }
        return std::noop_coroutine();
    }

    auto getNextWorkerThreadTask() noexcept -> std::coroutine_handle<>
    {
        std::coroutine_handle<> handle = nullptr;
        if (workerThreadTasks_.try_dequeue(handle))
        {
            return handle;
        }
        return std::noop_coroutine();
    }

    moodycamel::ConcurrentQueue<std::coroutine_handle<>> mainThreadTasks_;
    moodycamel::ConcurrentQueue<std::coroutine_handle<>> workerThreadTasks_;

    std::vector<std::thread> workerThreads_;
    std::mutex               workerMutex_;
    std::condition_variable  workerCondition_;

    // Timer system
    std::priority_queue<std::unique_ptr<IntervalTask>> intervalQueue_;
    std::mutex timerMutex_;
    
    // Process expired interval tasks
    void processExpiredIntervalTasks();

    std::atomic<bool>   running_{ true };
    std::atomic<size_t> inFlightTasks_{ 0 };
};


//
// Comparison operator for priority queue (moved after IntervalTask definition)
//

namespace detail
{

//
// TransferPolicy
//
// A compile-time policy that defines how to transfer execution between coroutines.
//

template <ThreadAffinity CurrentAffinity, ThreadAffinity NextAffinity>
struct TransferPolicy final
{
    // This function is the key to symmetric transfer. By returning a coroutine handle from
    // `await_suspend`, we suspend the current coroutine and immediately resume the next
    // without growing the call stack.
    NO_DISCARD FORCE_INLINE HOT_PATH static auto transfer(Scheduler* scheduler, std::coroutine_handle<> handle) noexcept -> std::coroutine_handle<>
    {
        if constexpr (CurrentAffinity == NextAffinity)
        {
            // Fast Path: Same-affinity co_await.
            // Execution is transferred directly to the next coroutine, like a function call.
            return handle;
        }
        else
        {
            // Slow Path: Cross-affinity co_await.
            // 1. Schedule the next coroutine on its correct thread.
            scheduler->template scheduleHandleWithAffinity<NextAffinity>(handle);

            // 2. Symmetric Transfer: The current thread immediately gets a new task for itself
            //    to avoid becoming idle, maximizing throughput.
            return scheduler->template getNextTaskWithAffinity<CurrentAffinity>();
        }
    }
};

//
// Continuation
//
// A type-safe wrapper that preserves the affinity of a resuming coroutine.
//
// Why is this needed?
// When a coroutine finishes, its `final_suspend` only has a type-erased `std::coroutine_handle<>`
// for the parent coroutine it needs to resume. This handle lacks the affinity information
// required by `TransferPolicy` to make a compile-time decision.
//
// This struct solves the problem by capturing the parent's affinity (`To`) when the `co_await`
// is initiated. We store this typed struct in the child's promise, preserving the full type
// information until it's needed at the end of the child's lifetime.
//
// Does this prevent the fast path on the return journey?
// No, in fact, it's the very thing that *enables* the fast path on return. By preserving
// the parent's affinity (`To`), the `final_suspend` of the child can call the correct
// `TransferPolicy<ChildAffinity, ParentAffinity>`. This policy can then check `if constexpr
// (ChildAffinity == ParentAffinity)` and, if they match, return the parent's handle
// directly for immediate resumption—the fast path. Without this, every return would have
// to be a "slow path" through the scheduler's queues.
//

template <ThreadAffinity From, ThreadAffinity To>
struct CACHE_ALIGN Continuation final
{
    std::coroutine_handle<>      handle_;
    NO_DISCARD FORCE_INLINE auto resume(Scheduler* scheduler) const noexcept -> std::coroutine_handle<>
    {
        // We can now invoke the correct `TransferPolicy` because we have both `From` and `To`.
        return TransferPolicy<From, To>::transfer(scheduler, handle_);
    }
};

//
// ContinuationVariant
//
// A `std::variant` holding all possible `Continuation` types.
//
// Why use a variant?
// A coroutine can be awaited by a parent of any affinity, so we need a single member
// on the promise that can store any of the four `Continuation` possibilities.
//
// Performance Cost:
// This approach has virtually zero runtime overhead. `std::variant` is a stack-allocated,
// type-safe union that avoids dynamic allocation and virtual functions. Access via `std::visit`
// compiles to an efficient jump table. It is a pure compile-time polymorphism solution.
//

using ContinuationVariant = std::variant<
    std::monostate,
    Continuation<ThreadAffinity::Main, ThreadAffinity::Main>,
    Continuation<ThreadAffinity::Main, ThreadAffinity::Worker>,
    Continuation<ThreadAffinity::Worker, ThreadAffinity::Main>,
    Continuation<ThreadAffinity::Worker, ThreadAffinity::Worker>>;

//
// TaskBase
//
// A move-only, type-erased handle to a coroutine. This is the primary object that users
// will create and `co_await`. It is non-copyable to ensure clear ownership.
//

template <ThreadAffinity Affinity, typename T>
struct NO_DISCARD TaskBase final
{
    using ResultType = T;

    static constexpr ThreadAffinity affinity = Affinity;

    using promise_type = detail::Promise<Affinity, T>;

    TaskBase() noexcept = default;

    explicit TaskBase(std::coroutine_handle<promise_type> coroutine) noexcept
    : handle_(coroutine)
    {
    }

    TaskBase(TaskBase const&)            = delete;
    TaskBase& operator=(TaskBase const&) = delete;

    FORCE_INLINE TaskBase(TaskBase&& other) noexcept
    : handle_(other.handle_)
    {
        other.handle_ = nullptr;
    }

    FORCE_INLINE TaskBase& operator=(TaskBase&& other) noexcept
    {
        if (this != &other)
            LIKELY
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

    FORCE_INLINE ~TaskBase() noexcept
    {
        if (handle_)
        {
            handle_.destroy();
        }
    }

    auto done() const noexcept -> bool
    {
        return !handle_ || handle_.done();
    }

    template <typename U = T>
    NO_DISCARD auto result() noexcept -> std::enable_if_t<!std::is_void_v<U>, U&>
    {
        return handle_.promise().result();
    }

    template <typename U = T>
    NO_DISCARD auto result() const noexcept -> std::enable_if_t<!std::is_void_v<U>, const U&>
    {
        return handle_.promise().result();
    }

    auto await_ready() const noexcept -> bool
    {
        return done();
    }

    // This is a fallback for awaiting a raw TaskBase. The `await_transform` in the Promise
    // is the primary, more powerful mechanism for handling `co_await`.
    COLD_PATH auto await_suspend(std::coroutine_handle<> /* coroutine */) const noexcept -> std::coroutine_handle<>
    {
        // "fallback", lol.
        ASSERT_UNREACHABLE();

        // handle_.promise().continuation_ = detail::Continuation<Affinity, Affinity>{ coroutine };
        // return handle_;
    }

    auto await_resume() const noexcept
    {
        if constexpr (!std::is_void_v<T>)
        {
            return result();
        }
    }

public:
    std::coroutine_handle<promise_type> handle_ = nullptr;
};

//
// InitialAwaiter
//
// Ensures a newly created coroutine *always* suspends immediately. This gives the `schedule`
// function control of the coroutine handle before it begins execution, preventing it from
// running eagerly on the caller's stack.
//

struct InitialAwaiter final
{
    constexpr bool await_ready() const noexcept
    {
        return false; // Always suspend
    }
    constexpr void await_suspend(std::coroutine_handle<>) const noexcept
    {
    }
    constexpr void await_resume() const noexcept
    {
    }
};

//
// FinalAwaiter
//
// This awaiter is the key to chaining coroutines and performing symmetric transfer. When a
// coroutine completes, it either resumes its parent or asks the scheduler for a new task.
//

template <ThreadAffinity Affinity, typename Promise>
struct FinalAwaiter final
{
    Promise* promise_;

    FORCE_INLINE bool await_ready() const noexcept
    {
        return false; // Always suspend
    }

    NO_DISCARD HOT_PATH std::coroutine_handle<> await_suspend(std::coroutine_handle<> /* self */) const noexcept
    {
        return std::visit(
            [&](auto&& continuation) noexcept -> std::coroutine_handle<>
            {
                using TContinuation = std::decay_t<decltype(continuation)>;
                if constexpr (std::is_same_v<TContinuation, std::monostate>)
                {
                    // This was a top-level task. It has finished.
                    promise_->scheduler_->notifyTaskComplete();

                    // Symmetric Transfer on Task Completion:
                    // No continuation exists (a top-level task). Ask the scheduler for the next
                    // available task for this thread to execute immediately.
                    return promise_->scheduler_->template getNextTaskWithAffinity<Affinity>();
                }
                else
                {
                    // Resume Parent Coroutine:
                    // A continuation exists. Resume it using the `Continuation` object, which
                    // contains the correct `TransferPolicy`.
                    return continuation.resume(promise_->scheduler_);
                }
            },
            promise_->continuation_);
    }

    FORCE_INLINE void await_resume() const noexcept
    {
    }
};

//
// PromiseBase
//
// The promise is the core component of a coroutine, managing its state and interactions with the
// scheduler. It provides the necessary hooks for suspension, resumption, and exception handling.
// It also provides storage for the coroutine's state and result.
//

template <typename Derived, ThreadAffinity Affinity, typename T>
struct PromiseBase
{
    Scheduler*          scheduler_{ nullptr };
    ContinuationVariant continuation_{};

    static constexpr ThreadAffinity affinity = Affinity;

    NO_DISCARD FORCE_INLINE auto get_return_object() noexcept
    {
        return TaskBase<Affinity, T>{
            std::coroutine_handle<Derived>::from_promise(*static_cast<Derived*>(this))
        };
    }

    FORCE_INLINE auto initial_suspend() const noexcept
    {
        return InitialAwaiter{};
    }

    FORCE_INLINE auto final_suspend() noexcept
    {
        return FinalAwaiter<Affinity, PromiseBase>{ this };
    }

    COLD_PATH void unhandled_exception() noexcept
    {
        // TODO
        coro_log("!!! Unhandled exception !!!");
        std::terminate();
    }

    //
    // await_transform
    //
    // This critical customization point is called on `co_await`. It creates a special awaiter that:
    // 1. Stores the current coroutine's handle as a continuation in the `next_task`.
    // 2. Uses `TransferPolicy` to immediately transfer execution to `next_task` or a new task.
    //
    template <ThreadAffinity NextAffinity, typename NextT>
    HOT_PATH auto await_transform(TaskBase<NextAffinity, NextT>&& nextTask) noexcept
    {
        struct TransferAwaiter final
        {
            using promise_type = typename TaskBase<NextAffinity, NextT>::promise_type;

            Scheduler*                          scheduler_;
            std::coroutine_handle<promise_type> handle_;

            // Explicit constructor to handle initialization from the enclosing function.
            TransferAwaiter(Scheduler* scheduler, TaskBase<NextAffinity, NextT>&& task) noexcept
            : scheduler_(scheduler)
            , handle_(task.handle_)
            {
                // Before we can co_await the next task, we must propagate the scheduler pointer.
                // This ensures that when the next task completes, its `final_suspend` can correctly
                // interact with the scheduler to resume the continuation or fetch a new task.
                handle_.promise().scheduler_ = scheduler_;

                // Release the handle from the task to prevent double destruction
                task.handle_ = nullptr;
            }

            // This awaiter is move-only, as it contains a coroutine handle.
            // Explicitly deleting copy operations silences MSVC warning C4625.
            // Defaulting move operations silences MSVC warning C4626.
            TransferAwaiter(const TransferAwaiter&)            = delete;
            TransferAwaiter& operator=(const TransferAwaiter&) = delete;
            TransferAwaiter(TransferAwaiter&&)                 = default;
            TransferAwaiter& operator=(TransferAwaiter&&)      = default;

            FORCE_INLINE bool await_ready() const noexcept
            {
                return !handle_ || handle_.done();
            }

            NO_DISCARD HOT_PATH FORCE_INLINE auto await_suspend(std::coroutine_handle<> awaiting_coroutine) noexcept -> std::coroutine_handle<>
            {
                // Store this coroutine's handle as the continuation for the next task,
                // preserving the parent's affinity information for the eventual resume.
                handle_.promise().continuation_ = detail::Continuation<NextAffinity, Affinity>{ awaiting_coroutine };

                // Perform the "downward" transfer to start executing next_task. `TransferPolicy`
                // will either return `handle_` directly (same affinity) or schedule
                // it and return a new task for this thread (different affinity).
                return TransferPolicy<Affinity, NextAffinity>::transfer(scheduler_, handle_);
            }

            FORCE_INLINE auto await_resume() const noexcept
            {
                if constexpr (!std::is_void_v<NextT>)
                {
                    return handle_.promise().result();
                }
            }
        };
        return TransferAwaiter{ scheduler_, std::move(nextTask) };
    }

    template <typename AwaitableType>
    auto await_transform(AwaitableType&& awaitable) noexcept
    {
        return std::forward<AwaitableType>(awaitable);
    }
};

//
// Promise for <T>
//

template <ThreadAffinity Affinity, typename T>
struct Promise final : public PromiseBase<Promise<Affinity, T>, Affinity, T>
{
    T value_{};

    FORCE_INLINE void return_value(T&& value) noexcept(std::is_nothrow_move_assignable_v<T>)
    {
        value_ = std::move(value);
    }

    FORCE_INLINE void return_value(const T& value) noexcept(std::is_nothrow_copy_assignable_v<T>)
    {
        value_ = value;
    }

    FORCE_INLINE auto result() noexcept -> T&
    {
        return value_;
    }

    FORCE_INLINE auto result() const noexcept -> const T&
    {
        return value_;
    }
};

//
// Promise for <void>
//

template <ThreadAffinity Affinity>
struct Promise<Affinity, void> final : public PromiseBase<Promise<Affinity, void>, Affinity, void>
{
    FORCE_INLINE void return_void() noexcept
    {
    }
};

} // namespace detail

//
// User-facing Task Aliases
//

template <typename T = void>
using Task = detail::TaskBase<ThreadAffinity::Main, T>;

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

    void setToken(class CancellationToken* token);
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
    [[maybe_unused]] Scheduler* scheduler_;
    class CancellationToken* token_{nullptr};
    std::atomic<bool> cancelled_{false};
    std::atomic<bool> hasActiveChild_{false};
    [[maybe_unused]] bool isOneTime_{false};
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
    CancellationToken(CancellationToken&&)                 = delete;
    CancellationToken& operator=(CancellationToken&&)      = delete;

    //
    // Cancellation Control
    //

    void cancel();
    auto valid() const -> bool;
    auto isCancelled() const -> bool;
    explicit operator bool() const;

    //
    // Task Management
    //

    void setTask(IntervalTask* task);
    void clearTokenPointer();

private:
    //
    // Member Variables
    //

    IntervalTask* task_{nullptr};
    [[maybe_unused]] Scheduler* scheduler_;
    std::atomic<bool> cancelled_{false};
};

//
// Comparison operators for priority queue (moved after IntervalTask definition)
//

inline bool operator<(const IntervalTask& lhs, const IntervalTask& rhs)
{
    return lhs.getNextExecution() > rhs.getNextExecution(); // Min-heap
}

//
// Scheduler Method Implementations
//

// Implementation of processExpiredIntervalTasks moved here to avoid incomplete type issues
inline void Scheduler::processExpiredIntervalTasks()
{
    auto now = std::chrono::steady_clock::now();
    
    {
        std::lock_guard<std::mutex> lock(timerMutex_);
        
        while (!intervalQueue_.empty())
        {
            auto& intervalTask = intervalQueue_.top();
            if (intervalTask->getNextExecution() > now)
            {
                break; // No more expired tasks
            }
            
            // Remove from priority queue temporarily
            auto task = std::move(const_cast<std::unique_ptr<IntervalTask>&>(intervalQueue_.top()));
            intervalQueue_.pop();
            
            // Execute the task
            task->execute();
            
            // If it's not a one-time task, reschedule it
            if (!task->isCancelled())
            {
                intervalQueue_.push(std::move(task));
            }
        }
    }
}

template <typename Rep, typename Period, typename Callable>
auto Scheduler::scheduleInterval(std::chrono::duration<Rep, Period> interval,
                                Callable&& callable) -> CancellationToken
{
    // Minimal stub implementation - will fail tests
    (void)interval;
    (void)callable;
    return CancellationToken(nullptr, this);
}

template <typename Rep, typename Period, typename Callable>
auto Scheduler::scheduleDelayed(std::chrono::duration<Rep, Period> delay,
                               Callable&& callable) -> CancellationToken
{
    // Minimal stub implementation - will fail tests
    (void)delay;
    (void)callable;
    return CancellationToken(nullptr, this);
}

template <typename T>
using AsyncTask = detail::TaskBase<ThreadAffinity::Worker, T>;

//
// IntervalTask Implementation
//

inline IntervalTask::IntervalTask(std::function<Task<void>()> factory,
                                 std::chrono::milliseconds interval,
                                 Scheduler* scheduler,
                                 bool isOneTime)
    : factory_(std::move(factory))
    , nextExecution_(std::chrono::steady_clock::now())
    , interval_(interval)
    , scheduler_(scheduler)
    , isOneTime_(isOneTime)
{
}

inline IntervalTask::~IntervalTask()
{
    if (token_)
    {
        token_->clearTokenPointer();
    }
}

inline auto IntervalTask::createTrackedTask() -> std::optional<Task<void>>
{
    // Minimal stub - always returns nullopt to fail tests
    return std::nullopt;
}

inline void IntervalTask::execute()
{
    // Minimal stub - does nothing to fail tests
}

inline void IntervalTask::markCancelled()
{
    cancelled_.store(true);
}

inline auto IntervalTask::isCancelled() const -> bool
{
    return cancelled_.load();
}

inline auto IntervalTask::getNextExecution() const -> std::chrono::steady_clock::time_point
{
    return nextExecution_;
}

inline void IntervalTask::updateNextExecution()
{
    nextExecution_ += interval_;
}

inline void IntervalTask::setToken(CancellationToken* token)
{
    token_ = token;
}

inline void IntervalTask::clearTokenPointer()
{
    token_ = nullptr;
}

inline Task<void> IntervalTask::createTrackedWrapper(Task<void> originalTask)
{
    // Minimal stub - returns empty task to fail tests
    (void)originalTask;
    return []() -> Task<void> { co_return; }();
}

//
// CancellationToken Implementation
//

inline CancellationToken::CancellationToken(IntervalTask* task, Scheduler* scheduler)
    : task_(task)
    , scheduler_(scheduler)
{
    if (task_)
    {
        task_->setToken(this);
    }
}

inline CancellationToken::~CancellationToken()
{
    if (task_)
    {
        task_->clearTokenPointer();
        task_->markCancelled();
    }
}

inline void CancellationToken::cancel()
{
    cancelled_.store(true);
    if (task_)
    {
        task_->markCancelled();
    }
}

inline auto CancellationToken::valid() const -> bool
{
    return !cancelled_.load() && task_ != nullptr;
}

inline auto CancellationToken::isCancelled() const -> bool
{
    return cancelled_.load();
}

inline CancellationToken::operator bool() const
{
    return valid();
}

inline void CancellationToken::setTask(IntervalTask* task)
{
    task_ = task;
}

inline void CancellationToken::clearTokenPointer()
{
    task_ = nullptr;
}

} // namespace CoroRoro
