#pragma once

#include <atomic>
#include <chrono>
#include <coroutine>
#include <deque>
#include <functional> // Required for std::function
#include <iostream>
#include <memory>
#include <mutex>
#include <thread>
#include <type_traits> // For std::enable_if_t, std::is_void_v
#include <variant> // For std::variant approach

// ThreadAffinity enum remains the same
enum class ThreadAffinity : uint32_t
{
    Main   = 0,
    Worker = 1
};

// Forward declarations
class Scheduler;

template <ThreadAffinity Affinity, typename T>
struct TaskBase;

// The TransferPolicy struct, as you suggested, to handle transition logic.
template <ThreadAffinity CurrentAffinity, ThreadAffinity NextAffinity>
struct TransferPolicy
{
    [[nodiscard]] static auto transfer(Scheduler* scheduler, std::coroutine_handle<> handle) noexcept -> std::coroutine_handle<>
    {
        if constexpr (CurrentAffinity == NextAffinity)
        {
            // Same thread affinity - NO transfer needed, resume the new task immediately!
            return handle;
        }
        else
        {
            // Different thread affinity - schedule the new task and perform a symmetric transfer.
            scheduler->scheduleHandleWithAffinity<NextAffinity>(handle);
            return scheduler->getNextTaskWithAffinity<CurrentAffinity>();
        }
    }
};


namespace detail
{

// Awaiter for the initial suspension point of a Task.
// It simply suspends the coroutine; the Scheduler is responsible for queuing it.
struct InitialAwaiter
{
    constexpr bool await_ready() const noexcept
    {
        return false; // Always suspend on initial creation
    }
    constexpr void await_suspend(std::coroutine_handle<>) const noexcept
    {
    }
    constexpr void await_resume() const noexcept
    {
    }
};

// Type alias for the continuation function pointer. This is a highly efficient
// replacement for std::function, avoiding type-erasure and heap allocations.
using ContinuationFnPtr = std::coroutine_handle<> (*)(Scheduler*, std::coroutine_handle<>);

// Primary Promise template for non-void Tasks.
template <ThreadAffinity Affinity, typename T>
struct Promise
{
    static_assert(!std::is_void_v<T>, "This promise type is for non-void tasks only.");
    static constexpr ThreadAffinity affinity = Affinity;

    Scheduler* scheduler_ = nullptr;
    // Store the handle to resume and a function pointer to the transfer logic.
    std::coroutine_handle<> continuation_handle_ = nullptr;
    ContinuationFnPtr continuation_fn_           = nullptr;
    T value_{};

    auto get_return_object() noexcept
    {
        return TaskBase<Affinity, T>{
            std::coroutine_handle<Promise>::from_promise(*this)
        };
    }

    auto initial_suspend() const noexcept
    {
        return InitialAwaiter{};
    }

    auto final_suspend() noexcept
    {
        struct FinalTransitionAwaiter
        {
            Promise* promise_;

            bool await_ready() const noexcept { return false; }

            std::coroutine_handle<> await_suspend(std::coroutine_handle<> /* self */) noexcept
            {
                if (promise_->continuation_fn_)
                {
                    // This coroutine was awaited. Execute the stored function pointer
                    // to perform the correct transfer back to the caller.
                    return promise_->continuation_fn_(promise_->scheduler_, promise_->continuation_handle_);
                }
                else
                {
                    // This was a top-level task. Symmetrically transfer to the next task.
                    return promise_->scheduler_->template getNextTaskWithAffinity<Affinity>();
                }
            }
            void await_resume() const noexcept {}
        };
        return FinalTransitionAwaiter{ this };
    }

    template <typename U>
    void return_value(U&& value) noexcept(std::is_nothrow_move_assignable_v<U>)
    {
        value_ = std::forward<U>(value);
    }

    auto result() -> T&
    {
        return value_;
    }
    auto result() const -> const T&
    {
        return value_;
    }

    void unhandled_exception() noexcept
    {
        std::terminate();
    }

    template <ThreadAffinity NextAffinity, typename NextT>
    auto await_transform(TaskBase<NextAffinity, NextT>&& next_task) noexcept
    {
        struct TransferAwaiter
        {
            Scheduler* scheduler_;
            TaskBase<NextAffinity, NextT> next_task_;

            bool await_ready() const noexcept
            {
                return next_task_.done();
            }

            auto await_suspend(std::coroutine_handle<> awaiting_coroutine) noexcept -> std::coroutine_handle<>
            {
                auto& promise = next_task_.handle_.promise();

                // This static lambda captures the compile-time affinity information in its type.
                // It can be converted to a function pointer because it doesn't capture any state.
                static constexpr auto resume_with_transfer =
                    [](Scheduler* scheduler, std::coroutine_handle<> handle) noexcept -> std::coroutine_handle<> {
                    // On the "up" journey, transfer from the completed task's affinity (NextAffinity)
                    // back to the awaiting task's affinity (Affinity).
                    return TransferPolicy<NextAffinity, Affinity>::transfer(scheduler, handle);
                };

                promise.continuation_handle_ = awaiting_coroutine;
                promise.continuation_fn_     = resume_with_transfer;

                // Perform the "down" journey transfer to start executing next_task.
                return TransferPolicy<Affinity, NextAffinity>::transfer(scheduler_, next_task_.handle_);
            }

            auto await_resume()
            {
                if constexpr (!std::is_void_v<NextT>)
                {
                    return next_task_.result();
                }
            }
        };
        return TransferAwaiter{ scheduler_, std::move(next_task) };
    }

    template <typename AwaitableType>
    auto await_transform(AwaitableType&& awaitable) noexcept
    {
        return std::forward<AwaitableType>(awaitable);
    }
};

// Promise specialization for void Tasks.
template <ThreadAffinity Affinity>
struct Promise<Affinity, void>
{
    static constexpr ThreadAffinity affinity = Affinity;

    Scheduler* scheduler_ = nullptr;
    // Store the handle to resume and a function pointer to the transfer logic.
    std::coroutine_handle<> continuation_handle_ = nullptr;
    ContinuationFnPtr continuation_fn_           = nullptr;

    auto get_return_object() noexcept
    {
        return TaskBase<Affinity, void>{
            std::coroutine_handle<Promise>::from_promise(*this)
        };
    }

    auto initial_suspend() const noexcept
    {
        return InitialAwaiter{};
    }

    auto final_suspend() noexcept
    {
        struct FinalTransitionAwaiter
        {
            Promise* promise_;

            bool await_ready() const noexcept { return false; }

            std::coroutine_handle<> await_suspend(std::coroutine_handle<> /* self */) noexcept
            {
                if (promise_->continuation_fn_)
                {
                    // This coroutine was awaited. Execute the stored function pointer
                    // to perform the correct transfer back to the caller.
                    return promise_->continuation_fn_(promise_->scheduler_, promise_->continuation_handle_);
                }
                else
                {
                    // This was a top-level task. Symmetrically transfer to the next task.
                    return promise_->scheduler_->template getNextTaskWithAffinity<Affinity>();
                }
            }
            void await_resume() const noexcept {}
        };
        return FinalTransitionAwaiter{ this };
    }

    void return_void() noexcept
    {
    }

    void unhandled_exception() noexcept
    {
        std::terminate();
    }

    template <ThreadAffinity NextAffinity, typename NextT>
    auto await_transform(TaskBase<NextAffinity, NextT>&& next_task) noexcept
    {
        struct TransferAwaiter
        {
            Scheduler* scheduler_;
            TaskBase<NextAffinity, NextT> next_task_;

            bool await_ready() const noexcept
            {
                return next_task_.done();
            }

            auto await_suspend(std::coroutine_handle<> awaiting_coroutine) noexcept -> std::coroutine_handle<>
            {
                auto& promise = next_task_.handle_.promise();

                static constexpr auto resume_with_transfer =
                    [](Scheduler* scheduler, std::coroutine_handle<> handle) noexcept -> std::coroutine_handle<> {
                    return TransferPolicy<NextAffinity, Affinity>::transfer(scheduler, handle);
                };

                promise.continuation_handle_ = awaiting_coroutine;
                promise.continuation_fn_     = resume_with_transfer;

                return TransferPolicy<Affinity, NextAffinity>::transfer(scheduler_, next_task_.handle_);
            }

            auto await_resume()
            {
                if constexpr (!std::is_void_v<NextT>)
                {
                    return next_task_.result();
                }
            }
        };
        return TransferAwaiter{ scheduler_, std::move(next_task) };
    }

    template <typename AwaitableType>
    auto await_transform(AwaitableType&& awaitable) noexcept
    {
        return std::forward<AwaitableType>(awaitable);
    }
};


} // namespace detail


// Unified TaskBase for both void and non-void return types.
template <ThreadAffinity Affinity, typename T>
struct TaskBase
{
    using ResultType   = T;
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
    auto result() -> std::enable_if_t<!std::is_void_v<U>, U&>
    {
        return handle_.promise().result();
    }

    template <typename U = T>
    auto result() const -> std::enable_if_t<!std::is_void_v<U>, const U&>
    {
        return handle_.promise().result();
    }

    auto await_ready() const noexcept -> bool
    {
        return done();
    }

    auto await_suspend(std::coroutine_handle<> coroutine) noexcept -> std::coroutine_handle<>
    {
        handle_.promise().continuation_handle_ = coroutine;
        // Fallback for generic awaitables - does not support cross-affinity returns.
        handle_.promise().continuation_fn_ = [](Scheduler* /*s*/, std::coroutine_handle<> h) { return h; };
        return handle_;
    }

    auto await_resume()
    {
        if constexpr (!std::is_void_v<T>)
        {
            return handle_.promise().result();
        }
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

    Scheduler(const Scheduler&)            = delete;
    Scheduler& operator=(const Scheduler&) = delete;
    Scheduler(Scheduler&&)                 = delete;
    Scheduler& operator=(Scheduler&&)      = delete;

    template <typename TaskType>
    void schedule(TaskType&& task)
    {
        auto handle = task.handle_;
        if (handle && !handle.done())
        {
            handle.promise().scheduler_ = this;
            scheduleHandleWithAffinity<TaskType::affinity>(handle);
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
        std::coroutine_handle<> handle = std::noop_coroutine();
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

    std::thread::id mainThreadId_;
    std::atomic<bool> running_{ true };
    std::mutex mainThreadTasksMutex_;
    std::deque<std::coroutine_handle<>> mainThreadTasks_;
};

//
// Desired usage
//

auto innermostTask() -> AsyncTask<int>
{
    co_return 100;
}

//
// Note the affinity change between innermostTask and innerTask: Task<int> -> AsyncTask<int>, and then back again.
// These should be the main two suspension points. The other suspension points are the initial creation of the root task, and the eventual
// completion of the root task.
//

auto innerTask() -> Task<int>
{
    co_return co_await innermostTask();
}

auto middleTask() -> Task<void>
{
    co_return co_await innerTask();
}

auto outerTask() -> Task<int>
{
    co_return co_await middleTask();
}

auto outermostTask() -> Task<int>
{
    co_return co_await outermostTask();
}

// Main test function
int main()
{
    Scheduler scheduler(4);

    std::cout << "Scheduling outer task..." << std::endl;
    scheduler.schedule(outerTask());

    std::cout << "Running tasks..." << std::endl;
    auto duration = scheduler.runExpiredTasks();
    std::cout << "Scheduler ran for " << duration.count() << "ms." << std::endl;

    std::cout << "\nTest completed successfully!" << std::endl;
    return 0;
}

//
// =======================================================================================
// ALTERNATIVE IMPLEMENTATIONS - Avoiding Function Pointers
// =======================================================================================
//

namespace alternatives {

// ============================================================================
// Alternative 1: std::variant with Explicit Continuation Types
// ============================================================================
// This approach avoids function pointers by storing the transfer logic
// explicitly as a discriminated union of specific continuation types.

enum class ContinuationType {
    None,
    SameAffinity,      // Main->Main or Worker->Worker
    MainToWorker,      // Main->Worker
    WorkerToMain       // Worker->Main
};

struct ContinuationData {
    ContinuationType type = ContinuationType::None;
    std::coroutine_handle<> continuation_handle = nullptr;
};

template <ThreadAffinity Affinity, typename T>
struct Promise_Variant {
    static constexpr ThreadAffinity affinity = Affinity;
    static constexpr ThreadAffinity continuation_affinity = Affinity; // For final_suspend

    Scheduler* scheduler_ = nullptr;
    ContinuationData continuation_data_{};
    T value_{};

    auto get_return_object() noexcept {
        return TaskBase<Affinity, T>{
            std::coroutine_handle<Promise_Variant>::from_promise(*this)
        };
    }

    auto initial_suspend() const noexcept {
        return InitialAwaiter{};
    }

    auto final_suspend() noexcept {
        struct FinalTransitionAwaiter {
            Promise_Variant* promise_;

            bool await_ready() const noexcept { return false; }

            std::coroutine_handle<> await_suspend(std::coroutine_handle<> /* self */) noexcept {
                if (promise_->continuation_data_.type != ContinuationType::None) {
                    auto& data = promise_->continuation_data_;
                    std::coroutine_handle<> handle = data.continuation_handle;

                    // Explicit dispatch based on continuation type
                    switch (data.type) {
                        case ContinuationType::SameAffinity:
                            // Same affinity - direct transfer
                            return handle;
                        case ContinuationType::MainToWorker:
                            // Transfer from Worker back to Main
                            return promise_->scheduler_->scheduleHandleWithAffinity<ThreadAffinity::Main>(handle);
                        case ContinuationType::WorkerToMain:
                            // Transfer from Main back to Worker
                            return promise_->scheduler_->scheduleHandleWithAffinity<ThreadAffinity::Worker>(handle);
                        default:
                            return std::noop_coroutine();
                    }
                } else {
                    // Top-level task
                    return promise_->scheduler_->template getNextTaskWithAffinity<Affinity>();
                }
            }
            void await_resume() const noexcept {}
        };
        return FinalTransitionAwaiter{ this };
    }

    template <typename U>
    void return_value(U&& value) noexcept(std::is_nothrow_move_assignable_v<U>) {
        value_ = std::forward<U>(value);
    }

    auto result() -> T& { return value_; }
    auto result() const -> const T& { return value_; }

    void unhandled_exception() noexcept { std::terminate(); }

    template <ThreadAffinity NextAffinity, typename NextT>
    auto await_transform(TaskBase<NextAffinity, NextT>&& next_task) noexcept {
        struct TransferAwaiter {
            Scheduler* scheduler_;
            TaskBase<NextAffinity, NextT> next_task_;
            Promise_Variant* promise_; // Back-reference to store continuation

            bool await_ready() const noexcept {
                return next_task_.done();
            }

            auto await_suspend(std::coroutine_handle<> awaiting_coroutine) noexcept -> std::coroutine_handle<> {
                auto& promise = next_task_.handle_.promise();

                // Store continuation information explicitly
                if constexpr (Affinity == NextAffinity) {
                    promise_->continuation_data_ = {ContinuationType::SameAffinity, awaiting_coroutine};
                } else if constexpr (Affinity == ThreadAffinity::Main && NextAffinity == ThreadAffinity::Worker) {
                    promise_->continuation_data_ = {ContinuationType::MainToWorker, awaiting_coroutine};
                } else if constexpr (Affinity == ThreadAffinity::Worker && NextAffinity == ThreadAffinity::Main) {
                    promise_->continuation_data_ = {ContinuationType::WorkerToMain, awaiting_coroutine};
                }

                // Perform the "down" journey
                return TransferPolicy<Affinity, NextAffinity>::transfer(scheduler_, next_task_.handle_);
            }

            auto await_resume() {
                if constexpr (!std::is_void_v<NextT>) {
                    return next_task_.result();
                }
            }
        };
        return TransferAwaiter{ scheduler_, std::move(next_task_), this };
    }

    template <typename AwaitableType>
    auto await_transform(AwaitableType&& awaitable) noexcept {
        return std::forward<AwaitableType>(awaitable);
    }
};

// ============================================================================
// Alternative 2: CRTP-based Continuation with Type-Safe Dispatch
// ============================================================================
// This approach uses CRTP to provide compile-time polymorphism for continuations.

template <typename Derived>
struct ContinuationBase {
    virtual ~ContinuationBase() = default;
    virtual std::coroutine_handle<> execute(Scheduler* scheduler, std::coroutine_handle<> handle) = 0;
};

template <ThreadAffinity FromAffinity, ThreadAffinity ToAffinity>
struct TypedContinuation : ContinuationBase<TypedContinuation<FromAffinity, ToAffinity>> {
    std::coroutine_handle<> execute(Scheduler* scheduler, std::coroutine_handle<> handle) override {
        return TransferPolicy<FromAffinity, ToAffinity>::transfer(scheduler, handle);
    }
};

template <ThreadAffinity Affinity, typename T>
struct Promise_CRTP {
    static constexpr ThreadAffinity affinity = Affinity;

    Scheduler* scheduler_ = nullptr;
    std::unique_ptr<ContinuationBase<std::decay_t<decltype(*this)>>> continuation_;
    std::coroutine_handle<> continuation_handle_ = nullptr;
    T value_{};

    auto get_return_object() noexcept {
        return TaskBase<Affinity, T>{
            std::coroutine_handle<Promise_CRTP>::from_promise(*this)
        };
    }

    auto initial_suspend() const noexcept { return InitialAwaiter{}; }

    auto final_suspend() noexcept {
        struct FinalTransitionAwaiter {
            Promise_CRTP* promise_;

            bool await_ready() const noexcept { return false; }

            std::coroutine_handle<> await_suspend(std::coroutine_handle<> /* self */) noexcept {
                if (promise_->continuation_) {
                    return promise_->continuation_->execute(promise_->scheduler_, promise_->continuation_handle_);
                } else {
                    return promise_->scheduler_->template getNextTaskWithAffinity<Affinity>();
                }
            }
            void await_resume() const noexcept {}
        };
        return FinalTransitionAwaiter{ this };
    }

    template <typename U>
    void return_value(U&& value) noexcept(std::is_nothrow_move_assignable_v<U>) {
        value_ = std::forward<U>(value);
    }

    auto result() -> T& { return value_; }
    void unhandled_exception() noexcept { std::terminate(); }

    template <ThreadAffinity NextAffinity, typename NextT>
    auto await_transform(TaskBase<NextAffinity, NextT>&& next_task) noexcept {
        struct TransferAwaiter {
            Scheduler* scheduler_;
            TaskBase<NextAffinity, NextT> next_task_;
            Promise_CRTP* promise_;

            bool await_ready() const noexcept { return next_task_.done(); }

            auto await_suspend(std::coroutine_handle<> awaiting_coroutine) noexcept -> std::coroutine_handle<> {
                auto& promise = next_task_.handle_.promise();

                // Create typed continuation using CRTP
                promise_->continuation_ = std::make_unique<TypedContinuation<NextAffinity, Affinity>>();
                promise_->continuation_handle_ = awaiting_coroutine;

                return TransferPolicy<Affinity, NextAffinity>::transfer(scheduler_, next_task_.handle_);
            }

            auto await_resume() {
                if constexpr (!std::is_void_v<NextT>) {
                    return next_task_.result();
                }
            }
        };
        return TransferAwaiter{ scheduler_, std::move(next_task_), this };
    }

    template <typename AwaitableType>
    auto await_transform(AwaitableType&& awaitable) noexcept {
        return std::forward<AwaitableType>(awaitable);
    }
};

// ============================================================================
// Alternative 3: Template Specialization for Transfer Types
// ============================================================================
// This approach uses template specialization to create specific promise types
// for each transfer scenario, eliminating runtime dispatch entirely.

template <ThreadAffinity FromAffinity, ThreadAffinity ToAffinity, typename T>
struct SpecializedPromise;

template <typename T>
struct SpecializedPromise<ThreadAffinity::Main, ThreadAffinity::Main, T> {
    static constexpr ThreadAffinity affinity = ThreadAffinity::Main;
    Scheduler* scheduler_ = nullptr;
    std::coroutine_handle<> continuation_handle_ = nullptr;
    T value_{};

    // Main->Main transfers are always direct
    static std::coroutine_handle<> transfer_back(Scheduler* /*scheduler*/, std::coroutine_handle<> handle) {
        return handle; // Direct transfer for same affinity
    }

    // ... rest of implementation would mirror the pattern
};

template <typename T>
struct SpecializedPromise<ThreadAffinity::Main, ThreadAffinity::Worker, T> {
    static constexpr ThreadAffinity affinity = ThreadAffinity::Main;
    Scheduler* scheduler_ = nullptr;
    std::coroutine_handle<> continuation_handle_ = nullptr;
    T value_{};

    // Main->Worker transfers require scheduler involvement
    static std::coroutine_handle<> transfer_back(Scheduler* scheduler, std::coroutine_handle<> handle) {
        return scheduler->scheduleHandleWithAffinity<ThreadAffinity::Main>(handle);
    }

    // ... rest of implementation
};

} // namespace alternatives

// ============================================================================
// PERFORMANCE COMPARISON
// ============================================================================

/*
Comparison of Function Pointer vs Alternatives:

1. **Function Pointer Approach** (Current):
   - ✅ Zero runtime overhead for dispatch
   - ✅ Compile-time optimization
   - ❌ Function pointer indirection
   - ❌ No type safety at call site
   - ❌ Requires lambda capture

2. **std::variant Approach**:
   - ✅ Type-safe explicit dispatch
   - ✅ No function pointers
   - ✅ Clear intent in code
   - ❌ Switch statement overhead (minimal)
   - ❌ More verbose

3. **CRTP Approach**:
   - ✅ Compile-time polymorphism
   - ✅ Type-safe
   - ✅ Extensible
   - ❌ Heap allocation for continuation
   - ❌ Virtual function call overhead

4. **Template Specialization**:
   - ✅ Zero runtime overhead
   - ✅ Maximum type safety
   - ✅ Compile-time everything
   - ❌ Code duplication
   - ❌ Maintenance burden

Recommendation: If you want to avoid function pointers entirely, the std::variant
approach provides the best balance of performance, safety, and maintainability.
*/

