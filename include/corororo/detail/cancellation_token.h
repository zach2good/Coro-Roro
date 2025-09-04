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

    CancellationToken(IntervalTask* task, Scheduler* scheduler);
    ~CancellationToken();

    CancellationToken(const CancellationToken&)            = delete;
    CancellationToken& operator=(const CancellationToken&) = delete;
    CancellationToken(CancellationToken&& other) noexcept;
    CancellationToken& operator=(CancellationToken&&) = delete;

    //
    // Cancellation Control
    //

    void     cancel();
    auto     valid() const -> bool;
    auto     isCancelled() const -> bool;
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

    IntervalTask*               task_{ nullptr };
    [[maybe_unused]] Scheduler* scheduler_;
    std::atomic<bool>           cancelled_{ false };
};

} // namespace CoroRoro