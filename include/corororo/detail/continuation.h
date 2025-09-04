#pragma once

#include "enums.h"
#include "forward_declarations.h"
#include "macros.h"
#include "transfer_policy.h"

#include <variant>

namespace CoroRoro
{
namespace detail
{

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

} // namespace detail
} // namespace CoroRoro
