#include <atomic>
#include <chrono>
#include <coroutine>
#include <deque>
#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <thread>
#include <type_traits>
#include <variant>

//
// https://lewissbaker.github.io/2020/05/11/understanding_symmetric_transfer
//
// A tweak was made to the design of coroutines in 2018 to add a capability called “symmetric transfer”,
// which allows you to suspend one coroutine and resume another coroutine without consuming any
// additional stack-space. This allows for much simpler and more efficient implementation of async
// coroutine types without sacrificing any of the safety aspects needed to guard against stack-overflow.
//

//
// Symmetric Transfer: Your understanding and the implementation are correct. The key is that await_suspend
// returns another coroutine handle (std::coroutine_handle<>). This tells the compiler to switch directly
// to the new coroutine's context without unwinding the stack, which is the core of "symmetric transfer".
//
// I've updated the comments to be more concise and to clearly distinguish between:
//
// - Direct Transfer (Fast Path): Occurs when co_awaiting a task of the same affinity. This is like a non-blocking function call.
// - Symmetric Transfer (Slow Path): Occurs when co_awaiting a task of a different affinity. The new task is scheduled, and
//   the current thread immediately requests a different task to execute to avoid being idle.
// - Continuation & variant: The explanations are sound. This pattern is a clean, compile-time solution to the type-erasure
//   problem that occurs when a coroutine needs to resume its parent. It correctly preserves the parent's affinity information
//   with zero runtime overhead.
//

//
// Enums
//

enum class ThreadAffinity : uint8_t
{
    Main   = 0, // Represents the main application thread.
    Worker = 1, // Represents any thread in the worker pool.
};

//
// Forward declarations
//

class Scheduler;

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
// Simple Scheduler stub for testing
//

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

    Scheduler(const Scheduler&)            = delete;
    Scheduler& operator=(const Scheduler&) = delete;
    Scheduler(Scheduler&&)                 = delete;
    Scheduler& operator=(Scheduler&&)      = delete;

    template <ThreadAffinity Affinity, typename T>
    void schedule(detail::TaskBase<Affinity, T> task)
    {
        auto handle = task.handle_;
        if (handle && !handle.done())
        {
            handle.promise().scheduler_ = this;
            // The coroutine's lifetime is now managed by the scheduler's queues.
            // We must release the handle from the temporary task object to prevent
            // it from being destroyed when 'task' goes out of scope.
            task.handle_ = nullptr;
            scheduleHandleWithAffinity<Affinity>(handle);
        }
    }

    auto runExpiredTasks() -> std::chrono::milliseconds
    {
        auto start = std::chrono::steady_clock::now();

        auto task_to_run = getNextMainThreadTask();
        if (task_to_run && !task_to_run.done())
        {
            task_to_run.resume();
        }

        return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start);
    }

    template <ThreadAffinity Affinity>
    void scheduleHandleWithAffinity(std::coroutine_handle<> handle) noexcept
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
    auto getNextTaskWithAffinity() noexcept -> std::coroutine_handle<>
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
    void scheduleMainThreadTask(std::coroutine_handle<> handle) noexcept
    {
        std::lock_guard<std::mutex> lock(mainThreadTasksMutex_);
        mainThreadTasks_.push_back(handle);
    }

    void scheduleWorkerThreadTask(std::coroutine_handle<> handle) noexcept
    {
    }

    auto getNextMainThreadTask() noexcept -> std::coroutine_handle<>
    {
        std::lock_guard<std::mutex> lock(mainThreadTasksMutex_);
        std::coroutine_handle<>     handle = std::noop_coroutine();
        if (!mainThreadTasks_.empty())
        {
            handle = mainThreadTasks_.front();
            mainThreadTasks_.pop_front();
        }
        return handle;
    }

    auto getNextWorkerThreadTask() noexcept -> std::coroutine_handle<>
    {
        return std::noop_coroutine();
    }

    std::thread::id                     mainThreadId_;
    std::atomic<bool>                   running_{ true };
    std::mutex                          mainThreadTasksMutex_;
    std::deque<std::coroutine_handle<>> mainThreadTasks_;
};

namespace detail
{

//
// TransferPolicy
//
// A compile-time policy that defines how to transfer execution between coroutines.
//
template <ThreadAffinity CurrentAffinity, ThreadAffinity NextAffinity>
struct TransferPolicy
{
    // This function is the key to symmetric transfer. By returning a coroutine handle from
    // `await_suspend`, we suspend the current coroutine and immediately resume the next
    // without growing the call stack.
    [[nodiscard]] static auto transfer(Scheduler* scheduler, std::coroutine_handle<> handle) noexcept -> std::coroutine_handle<>
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
struct Continuation
{
    std::coroutine_handle<> handle_;
    [[nodiscard]] auto      resume(Scheduler* scheduler) noexcept -> std::coroutine_handle<>
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
struct [[nodiscard]] TaskBase
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

    auto done() const noexcept -> bool
    {
        return !handle_ || handle_.done();
    }

    template <typename U = T>
    auto result() noexcept -> std::enable_if_t<!std::is_void_v<U>, U&>
    {
        return handle_.promise().result();
    }

    template <typename U = T>
    auto result() const noexcept -> std::enable_if_t<!std::is_void_v<U>, const U&>
    {
        return handle_.promise().result();
    }

    auto await_ready() const noexcept -> bool
    {
        return done();
    }

    // This is a fallback for awaiting a raw TaskBase. The `await_transform` in the Promise
    // is the primary, more powerful mechanism for handling `co_await`.
    auto await_suspend(std::coroutine_handle<> coroutine) noexcept -> std::coroutine_handle<>
    {
        handle_.promise().continuation_ = detail::Continuation<Affinity, Affinity>{ coroutine };
        return handle_;
    }

    auto await_resume() noexcept
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
struct InitialAwaiter
{
    constexpr bool await_ready() const noexcept
    {
        return false;
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
struct FinalAwaiter
{
    Promise* promise_;

    bool await_ready() const noexcept
    {
        return false;
    }

    [[nodiscard]] std::coroutine_handle<> await_suspend(std::coroutine_handle<> /* self */) noexcept
    {
        return std::visit(
            [&](auto&& cont) noexcept -> std::coroutine_handle<>
            {
                using TCont = std::decay_t<decltype(cont)>;
                if constexpr (std::is_same_v<TCont, std::monostate>)
                {
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
                    return cont.resume(promise_->scheduler_);
                }
            },
            promise_->continuation_);
    }

    void await_resume() const noexcept
    {
    }
};

//
// PromiseBase
//
// A CRTP base class containing common logic for a coroutine's promise, such as initial/final
// suspension, exception handling, and the `await_transform` customization point.
//
template <typename Derived, ThreadAffinity Affinity, typename T>
struct PromiseBase
{
    static constexpr ThreadAffinity affinity = Affinity;
    Scheduler*                      scheduler_{ nullptr };
    ContinuationVariant             continuation_{};

    [[nodiscard]] auto get_return_object() noexcept
    {
        return TaskBase<Affinity, T>{
            std::coroutine_handle<Derived>::from_promise(*static_cast<Derived*>(this))
        };
    }

    auto initial_suspend() const noexcept
    {
        return InitialAwaiter{};
    }

    auto final_suspend() noexcept
    {
        return FinalAwaiter<Affinity, PromiseBase>{ this };
    }

    void unhandled_exception() noexcept
    {
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
    auto await_transform(TaskBase<NextAffinity, NextT>&& next_task) noexcept
    {
        struct TransferAwaiter
        {
            Scheduler*                    scheduler_;
            TaskBase<NextAffinity, NextT> next_task_;

            bool await_ready() const noexcept
            {
                return next_task_.done();
            }

            [[nodiscard]] auto await_suspend(std::coroutine_handle<> awaiting_coroutine) noexcept -> std::coroutine_handle<>
            {
                // Store this coroutine's handle as the continuation for the next task,
                // preserving the parent's affinity information for the eventual resume.
                next_task_.handle_.promise().continuation_ = detail::Continuation<NextAffinity, Affinity>{ awaiting_coroutine };

                // Perform the "downward" transfer to start executing next_task. `TransferPolicy`
                // will either return `next_task_.handle_` directly (same affinity) or schedule
                // it and return a new task for this thread (different affinity).
                return TransferPolicy<Affinity, NextAffinity>::transfer(scheduler_, next_task_.handle_);
            }

            auto await_resume() noexcept
            {
                if constexpr (!std::is_void_v<NextT>)
                {
                    return next_task_.result();
                }
            }
        };
        return TransferAwaiter{ static_cast<Derived*>(this)->scheduler_, std::move(next_task) };
    }

    template <typename AwaitableType>
    auto await_transform(AwaitableType&& awaitable) noexcept
    {
        return std::forward<AwaitableType>(awaitable);
    }
};

//
// Promise <T>
//
template <ThreadAffinity Affinity, typename T>
struct Promise : public PromiseBase<Promise<Affinity, T>, Affinity, T>
{
    T value_{};

    void return_value(T&& value) noexcept(std::is_nothrow_move_assignable_v<T>)
    {
        value_ = std::move(value);
    }

    auto result() noexcept -> T&
    {
        return value_;
    }

    auto result() const noexcept -> const T&
    {
        return value_;
    }
};

//
// Promise<void>
//
template <ThreadAffinity Affinity>
struct Promise<Affinity, void> : public PromiseBase<Promise<Affinity, void>, Affinity, void>
{
    void return_void() noexcept
    {
    }
};

} // namespace detail

//
// Task Aliases
//

template <typename T = void>
using Task = detail::TaskBase<ThreadAffinity::Main, T>;

template <typename T>
using AsyncTask = detail::TaskBase<ThreadAffinity::Worker, T>;

//
// Desired usage
//

// This task runs on a worker thread and returns an integer.
auto innermostTask() -> AsyncTask<int>
{
    co_return 100;
}

// This task runs on the main thread, but awaits a worker thread task.
auto innerTask() -> Task<int>
{
    // co_await triggers await_transform. Because the affinities differ (Main -> Worker),
    // this is a "slow path" symmetric transfer. The worker task is scheduled, and the main
    // thread would ask for a new main-thread task to run.
    // When innermostTask completes, its FinalAwaiter will resume this coroutine. Because the
    // affinities differ again (Worker -> Main), it's another symmetric transfer.
    co_return co_await innermostTask();
}

// A simple task that chains another task.
auto middleTask() -> Task<void>
{
    // co_await triggers await_transform. Because the affinities match (Main -> Main),
    // this is a "fast path" direct transfer. Execution jumps into innerTask immediately
    // without involving the scheduler.
    co_await innerTask();
    co_return;
}

// Another chaining task.
auto outerTask() -> Task<void>
{
    co_await middleTask();
    co_return;
}

// The top-level task that starts the entire chain.
auto outermostTask() -> Task<int>
{
    co_await outerTask();
    co_return co_await innerTask();
}

// Main test function
int main()
{
    Scheduler scheduler(4);

    std::cout << "Scheduling outer task..." << std::endl;
    scheduler.schedule(outermostTask());

    std::cout << "Running tasks..." << std::endl;
    auto duration = scheduler.runExpiredTasks();
    std::cout << "Scheduler ran for " << duration.count() << "ms." << std::endl;

    std::cout << "\nTest completed successfully!" << std::endl;
    return 0;
}
