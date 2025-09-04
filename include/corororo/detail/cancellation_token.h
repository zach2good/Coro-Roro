#pragma once

#include "enums.h"
#include "forward_declarations.h"
#include "macros.h"

#include <atomic>

namespace CoroRoro
{

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

    CancellationToken() = default;
    CancellationToken(IntervalTask* task, Scheduler* scheduler);
    ~CancellationToken();

    CancellationToken(const CancellationToken&) = delete;
    CancellationToken(CancellationToken&& other) noexcept;
    CancellationToken& operator=(CancellationToken&& other) noexcept;

    // Assignment operator for overwriting tokens
    CancellationToken& operator=(const CancellationToken& other);

    //
    // Cancellation Control
    //

    void cancel();
    auto valid() const -> bool;

    // Internal method for IntervalTask
    void clearTokenPointer();

private:
    //
    // Member Variables
    //

    IntervalTask*               task_{ nullptr };
    [[maybe_unused]] Scheduler* scheduler_{ nullptr };
    std::atomic<bool>           cancelled_{ false };
};

} // namespace CoroRoro