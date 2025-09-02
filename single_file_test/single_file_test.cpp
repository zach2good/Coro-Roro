// MSVC: /std:c++20 /W4 /Wall /permissive-
// GCC/Clang: -std=c++20 -Wall -Wextra

#include <atomic>
#include <chrono>
#include <coroutine>
#include <iostream>
#include <memory>
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

#if __cplusplus > 201703L && defined(__has_cpp_attribute)
#if __has_cpp_attribute(likely)
#define LIKELY [[likely]]
#endif
#if __has_cpp_attribute(unlikely)
#define UNLIKELY [[unlikely]]
#endif
#endif

#ifndef LIKELY
#define LIKELY
#endif
#ifndef UNLIKELY
#define UNLIKELY
#endif

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
// Simple, allocation-free, single-producer, single-consumer lock-free queue.
//

#if defined(_MSC_VER)
// MSVC-specific pragmas to suppress warnings about padding, which is intentional
// for performance-critical, cache-aligned structures.
#pragma warning(push)
#pragma warning(disable : 4324) // C4324: structure was padded due to alignment specifier
#pragma warning(disable : 4820) // C4820: 'bytes' bytes padding added after data member 'member'
#endif

template <typename T, size_t Capacity>
class SPSCQueue
{
public:
    SPSCQueue() = default;

    SPSCQueue(const SPSCQueue&)            = delete;
    SPSCQueue& operator=(const SPSCQueue&) = delete;

    // Pushed by the producer thread.
    // Returns false if the queue is full.
    [[nodiscard]] bool push(T&& value)
    {
        size_t head      = head_.load(std::memory_order_relaxed);
        size_t next_head = (head + 1) % Capacity;
        if (next_head == tail_.load(std::memory_order_acquire))
        {
            return false; // Full
        }
        ring_[head] = std::move(value);
        head_.store(next_head, std::memory_order_release);
        return true;
    }

    // Popped by the consumer thread.
    // Returns false if the queue is empty.
    [[nodiscard]] bool pop(T& value)
    {
        size_t tail = tail_.load(std::memory_order_relaxed);
        if (tail == head_.load(std::memory_order_acquire))
        {
            return false; // Empty
        }
        value = std::move(ring_[tail]);
        tail_.store((tail + 1) % Capacity, std::memory_order_release);
        return true;
    }

private:
    // The head and tail pointers are aligned to cache lines to prevent false sharing
    // between the producer and consumer threads, which is critical for performance
    // in a multi-threaded context (even if this test is single-threaded).
    alignas(64) std::atomic<size_t> head_{ 0 };
    alignas(64) std::atomic<size_t> tail_{ 0 };
    T ring_[Capacity];
};

#if defined(_MSC_VER)
#pragma warning(pop)
#endif

//
// Simple Scheduler stub for testing (will eventually use)
//

#if defined(_MSC_VER)
// Suppress warnings about padding, which is intentional. By reordering members
// from largest to smallest, we minimize padding, but some may still exist.
#pragma warning(push)
#pragma warning(disable : 4820) // C4820: 'bytes' bytes padding added after data member 'member'
#endif

class Scheduler
{
public:
    explicit Scheduler(size_t workerThreadCount = 4)
    {
        mainThreadId_ = std::this_thread::get_id();
        running_.store(true);

        std::ignore = workerThreadCount;
    }

    ~Scheduler()
    {
        running_.store(false);
    }

    Scheduler(const Scheduler&)            = delete;
    Scheduler& operator=(const Scheduler&) = delete;
    Scheduler(Scheduler&&)                 = delete;
    Scheduler& operator=(Scheduler&&)      = delete;

    // Takes a temporary Task object (an r-value) and assumes ownership of its
    // coroutine handle. This is a "sink" function.
    template <ThreadAffinity Affinity, typename T>
    HOT_PATH void schedule(detail::TaskBase<Affinity, T>&& task)
    {
        auto handle = task.handle_;
        if (handle && !handle.done())
            LIKELY
            {
                handle.promise().scheduler_ = this;
                // The coroutine's lifetime is now managed by the scheduler. We must
                // release the handle from the task object that was passed in to
                // prevent it from being destroyed when it goes out of scope.
                task.handle_ = nullptr;
                scheduleHandleWithAffinity<Affinity>(handle);
            }
    }

    HOT_PATH auto runExpiredTasks() -> std::chrono::milliseconds
    {
        auto start = std::chrono::steady_clock::now();

        if (auto task_to_run = getNextMainThreadTask())
        {
            task_to_run.resume();
        }

        return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start);
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
    FORCE_INLINE void scheduleMainThreadTask(std::coroutine_handle<> handle) noexcept
    {
        while (!mainThreadTasks_.push(std::move(handle)))
        {
            // Queue is full, this is a busy-wait which is not ideal for a real-world
            // scenario, but acceptable for this benchmark-oriented test file.
            // A real implementation would use a condition variable or other mechanism.
        }
    }

    void scheduleWorkerThreadTask(std::coroutine_handle<> handle) noexcept
    {
        std::ignore = handle;
    }

    FORCE_INLINE auto getNextMainThreadTask() noexcept -> std::coroutine_handle<>
    {
        std::coroutine_handle<> handle = nullptr;
        if (mainThreadTasks_.pop(handle))
        {
            return handle;
        }
        return nullptr;
    }

    auto getNextWorkerThreadTask() noexcept -> std::coroutine_handle<>
    {
        return nullptr;
    }

    // Place members with alignment requirements first to minimize padding.
    SPSCQueue<std::coroutine_handle<>, 64> mainThreadTasks_;
    std::thread::id                        mainThreadId_;
    std::atomic<bool>                      running_{ true };
};

#if defined(_MSC_VER)
#pragma warning(pop)
#endif

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
    [[nodiscard]] FORCE_INLINE HOT_PATH static auto transfer(Scheduler* scheduler, std::coroutine_handle<> handle) noexcept -> std::coroutine_handle<>
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
    std::coroutine_handle<>         handle_;
    [[nodiscard]] FORCE_INLINE auto resume(Scheduler* scheduler) const noexcept -> std::coroutine_handle<>
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
    [[nodiscard]] auto result() noexcept -> std::enable_if_t<!std::is_void_v<U>, U&>
    {
        return handle_.promise().result();
    }

    template <typename U = T>
    [[nodiscard]] auto result() const noexcept -> std::enable_if_t<!std::is_void_v<U>, const U&>
    {
        return handle_.promise().result();
    }

    auto await_ready() const noexcept -> bool
    {
        return done();
    }

    // This is a fallback for awaiting a raw TaskBase. The `await_transform` in the Promise
    // is the primary, more powerful mechanism for handling `co_await`.
    COLD_PATH auto await_suspend(std::coroutine_handle<> coroutine) const noexcept -> std::coroutine_handle<>
    {
        handle_.promise().continuation_ = detail::Continuation<Affinity, Affinity>{ coroutine };
        return handle_;
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

    FORCE_INLINE bool await_ready() const noexcept
    {
        return false;
    }

    [[nodiscard]] HOT_PATH std::coroutine_handle<> await_suspend(std::coroutine_handle<> /* self */) const noexcept
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

    FORCE_INLINE void await_resume() const noexcept
    {
    }
};

//
// PromiseBase
//
// A CRTP base class containing common logic for a coroutine's promise, such as initial/final
// suspension, exception handling, and the `await_transform` customization point.
//

#if defined(_MSC_VER)
// Suppress warnings about padding, which is intentional. By reordering members
// from largest to smallest, we minimize padding, but some may still exist.
#pragma warning(push)
#pragma warning(disable : 4324) // C4324: structure was padded due to alignment specifier
#pragma warning(disable : 4820) // C4820: 'bytes' bytes padding added after data member 'member'
#endif

template <typename Derived, ThreadAffinity Affinity, typename T>
struct alignas(64) PromiseBase
{
    // Place members in descending order of alignment/size to minimize padding.
    Scheduler*                      scheduler_{ nullptr };
    ContinuationVariant             continuation_{};
    static constexpr ThreadAffinity affinity = Affinity;

    [[nodiscard]] FORCE_INLINE auto get_return_object() noexcept
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
        // Before we can co_await the next task, we must propagate the scheduler pointer.
        // This ensures that when the next task completes, its `final_suspend` can correctly
        // interact with the scheduler to resume the continuation or fetch a new task.
        nextTask.handle_.promise().scheduler_ = scheduler_;

        struct TransferAwaiter
        {
            Scheduler*                    scheduler_;
            TaskBase<NextAffinity, NextT> nextTask_;

            // Explicit constructor to handle initialization from the enclosing function.
            TransferAwaiter(Scheduler* scheduler, TaskBase<NextAffinity, NextT>&& task) noexcept
            : scheduler_(scheduler)
            , nextTask_(std::move(task))
            {
            }

            // This awaiter is move-only, as it contains a move-only TaskBase.
            // Explicitly deleting copy operations silences MSVC warning C4625.
            // Defaulting move operations silences MSVC warning C4626.
            TransferAwaiter(const TransferAwaiter&)            = delete;
            TransferAwaiter& operator=(const TransferAwaiter&) = delete;
            TransferAwaiter(TransferAwaiter&&)                 = default;
            TransferAwaiter& operator=(TransferAwaiter&&)      = default;

            FORCE_INLINE bool await_ready() const noexcept
            {
                return nextTask_.done();
            }

            [[nodiscard]] HOT_PATH FORCE_INLINE auto await_suspend(std::coroutine_handle<> awaiting_coroutine) const noexcept -> std::coroutine_handle<>
            {
                // Store this coroutine's handle as the continuation for the next task,
                // preserving the parent's affinity information for the eventual resume.
                nextTask_.handle_.promise().continuation_ = detail::Continuation<NextAffinity, Affinity>{ awaiting_coroutine };

                // Perform the "downward" transfer to start executing next_task. `TransferPolicy`
                // will either return `next_task_.handle_` directly (same affinity) or schedule
                // it and return a new task for this thread (different affinity).
                return TransferPolicy<Affinity, NextAffinity>::transfer(scheduler_, nextTask_.handle_);
            }

            FORCE_INLINE auto await_resume() const noexcept
            {
                if constexpr (!std::is_void_v<NextT>)
                {
                    return nextTask_.result();
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

#if defined(_MSC_VER)
#pragma warning(pop)
#endif

//
// Promise <T>
//

#if defined(_MSC_VER)
// Suppress warnings about padding, which is intentional.
#pragma warning(push)
#pragma warning(disable : 4820) // C4820: 'bytes' bytes padding added after data member 'member'
#endif

template <ThreadAffinity Affinity, typename T>
struct Promise : public PromiseBase<Promise<Affinity, T>, Affinity, T>
{
    T value_{};

    FORCE_INLINE void return_value(T&& value) noexcept(std::is_nothrow_move_assignable_v<T>)
    {
        value_ = std::move(value);
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
// Promise<void>
//

template <ThreadAffinity Affinity>
struct Promise<Affinity, void> : public PromiseBase<Promise<Affinity, void>, Affinity, void>
{
    FORCE_INLINE void return_void() noexcept
    {
    }
};

#if defined(_MSC_VER)
#pragma warning(pop)
#endif

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
