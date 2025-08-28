#pragma once

#include <atomic>
#include <cstdint>

namespace CoroRoro
{

//
// Forward declarations
//
class Scheduler;

//
// CancellationToken
//
//   RAII token for cancelling scheduled tasks.
//   The token automatically cancels its associated task on destruction.
//   Can only be moved, not copied.
//
class CancellationToken final
{
public:
    // Default constructor - creates invalid token
    CancellationToken() = default;

    // RAII - auto cancellation on destruction
    ~CancellationToken();

    CancellationToken(const CancellationToken&)            = delete;
    CancellationToken& operator=(const CancellationToken&) = delete;

    CancellationToken(CancellationToken&& other) noexcept;
    CancellationToken& operator=(CancellationToken&& other) noexcept;

    //
    // Public interface
    //

    void cancel();
    auto isValid() const -> bool;

private:
    friend class Scheduler;

    using TaskId = std::uint64_t;

    CancellationToken(Scheduler* scheduler, TaskId taskId);

    Scheduler*        scheduler_{ nullptr };
    TaskId            taskId_{ 0 };
    std::atomic<bool> cancelled_{ false };
};

//
// Inline implementations
//

inline auto CancellationToken::isValid() const -> bool
{
    return scheduler_ != nullptr && !cancelled_.load();
}

inline CancellationToken::CancellationToken(Scheduler* scheduler, TaskId taskId)
: scheduler_(scheduler)
, taskId_(taskId)
, cancelled_(false)
{
}

inline CancellationToken::CancellationToken(CancellationToken&& other) noexcept
: scheduler_(std::exchange(other.scheduler_, nullptr))
, taskId_(std::exchange(other.taskId_, 0))
, cancelled_(other.cancelled_.load())
{
    other.cancelled_ = true; // Mark moved-from token as cancelled
}

inline CancellationToken& CancellationToken::operator=(CancellationToken&& other) noexcept
{
    if (this != &other)
    {
        // Cancel current task if moving over an active token
        if (isValid())
        {
            cancel();
        }

        scheduler_ = std::exchange(other.scheduler_, nullptr);
        taskId_    = std::exchange(other.taskId_, 0);
        cancelled_ = other.cancelled_.load();

        other.cancelled_ = true; // Mark moved-from token as cancelled
    }
    return *this;
}

inline CancellationToken::~CancellationToken()
{
    if (isValid())
    {
        cancel();
    }
}

} // namespace CoroRoro
