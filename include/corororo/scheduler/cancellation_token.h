#pragma once

#include <atomic>
#include <memory>

namespace CoroRoro
{

//
// CancellationToken
//
//   Token that can be used to cancel scheduled tasks.
//   When cancelled, the associated task will not execute.
//

class CancellationToken
{
public:
    CancellationToken()
        : cancelled_(std::make_shared<std::atomic<bool>>(false))
    {}

    // Check if the token has been cancelled
    bool isCancelled() const
    {
        return cancelled_->load();
    }

    // Cancel the token (will prevent associated tasks from executing)
    void cancel()
    {
        cancelled_->store(true);
    }

    // Reset the token to non-cancelled state
    void reset()
    {
        cancelled_->store(false);
    }

private:
    std::shared_ptr<std::atomic<bool>> cancelled_;
};

} // namespace CoroRoro
