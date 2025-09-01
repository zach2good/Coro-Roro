
// The TransferPolicy is a compile-time mechanism to determine how to transition
// from one coroutine to another based on their thread affinities. It is the
// core of the efficient scheduling logic.
template <ThreadAffinity CurrentAffinity, ThreadAffinity NextAffinity>
struct TransferPolicy
{
    // The transfer function is called by awaiters to execute the transition.
    [[nodiscard]] static auto transfer(Scheduler* scheduler, std::coroutine_handle<> handle) noexcept -> std::coroutine_handle<>
    {
        // `if constexpr` allows the compiler to completely eliminate one of these
        // branches at compile time, resulting in zero runtime overhead for the check.
        if constexpr (CurrentAffinity == NextAffinity)
        {
            // OPTIMIZATION: The current and next tasks have the same thread affinity.
            // There's no need to go through the scheduler's queue. We can perform a
            // direct symmetric transfer by returning the handle to the next task,
            // which the runtime will immediately resume.
            return handle;
        }
        else
        {
            // AFFINITY CHANGE: The tasks have different affinities. We must suspend
            // the current execution flow and involve the scheduler.
            // 1. Schedule the new task on the queue corresponding to its affinity.
            scheduler->scheduleHandleWithAffinity<NextAffinity>(handle);
            // 2. Symmetrically transfer to the next available task on the *current*
            //    thread's queue. This keeps the current thread busy.
            return scheduler->getNextTaskWithAffinity<CurrentAffinity>();
        }
    }
};

