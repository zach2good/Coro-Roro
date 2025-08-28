#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <iostream>
#include <mutex>
#include <random>
#include <thread>

namespace TestUtils
{

//
// Watchdog
//
//   A simple watchdog timer that will terminate the test if it runs too long.
//   Useful for detecting infinite loops or deadlocks in tests.
//

class Watchdog
{
public:
    // clang-format off
    explicit Watchdog(
        std::chrono::seconds  timeout,
        std::function<void()> onTimeout =
        []()
        {
            std::abort();
        })
    // clang-format on
    : timeout_(timeout)
    , onTimeout_(onTimeout)
    , armed_(true)
    {
        watchThread_ =
            std::thread(
                [this]()
                {
                    std::unique_lock<std::mutex> lock(mutex_);

                    if (cv_.wait_for(lock, timeout_, [this]
                                     { return !armed_; }))
                    {
                        return;
                    }

                    if (armed_)
                    {
                        onTimeout_();
                    }
                });
    }

    ~Watchdog()
    {
        disarm();
        if (watchThread_.joinable())
        {
            watchThread_.join();
        }
    }

    void disarm()
    {
        std::unique_lock<std::mutex> lock(mutex_);
        armed_ = false;
        cv_.notify_all();
    }

private:
    std::chrono::seconds  timeout_;
    std::function<void()> onTimeout_;

    std::mutex              mutex_;
    std::condition_variable cv_;
    std::thread             watchThread_;
    bool                    armed_;
};

//
// Thread Verification Helpers
//
//   Helper functions to verify which thread the current code is running on.
//   These are used to ensure that Tasks and AsyncTasks run on the correct threads.
//

class ThreadVerifier
{
public:
    ThreadVerifier()
    {
        mainThreadId_ = std::this_thread::get_id();
    }

    [[nodiscard]] auto isOnMainThread() const noexcept -> bool
    {
        return std::this_thread::get_id() == mainThreadId_;
    }

    [[nodiscard]] auto isOnWorkerThread() const noexcept -> bool
    {
        return !isOnMainThread();
    }

    [[nodiscard]] auto getMainThreadId() const noexcept -> std::thread::id
    {
        return mainThreadId_;
    }

    [[nodiscard]] auto getCurrentThreadId() const noexcept -> std::thread::id
    {
        return std::this_thread::get_id();
    }

private:
    std::thread::id mainThreadId_;
};

//
// Global thread verifier instance for tests that need it
//

inline auto getThreadVerifier() -> ThreadVerifier&
{
    static ThreadVerifier verifier;
    return verifier;
}

//
// Convenience functions for backward compatibility
//

inline auto isOnMainThread() -> bool
{
    return getThreadVerifier().isOnMainThread();
}

inline auto isOnWorkerThread() -> bool
{
    return getThreadVerifier().isOnWorkerThread();
}

//
// Random
//

inline auto getRandomFloat() -> float
{
    static thread_local std::random_device               rd;
    static thread_local std::mt19937                     gen(rd());
    static thread_local std::uniform_real_distribution<> dis(0.0f, 1.0f);
    return dis(gen);
}

} // namespace TestUtils
