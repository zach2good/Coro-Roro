#pragma once

#include <corororo/coroutine/types.hpp>

namespace CoroRoro
{

// Simple thread affinity wrapper for backward compatibility
class ThreadAffinityWrapper
{
public:
    ThreadAffinityWrapper() = default;
    explicit ThreadAffinityWrapper(ThreadAffinity affinity) : affinity_(affinity) {}
    
    operator ThreadAffinity() const { return affinity_; }
    
    ThreadAffinity get() const { return affinity_; }
    void set(ThreadAffinity affinity) { affinity_ = affinity; }

private:
    ThreadAffinity affinity_ = ThreadAffinity::Any;
};

} // namespace CoroRoro
