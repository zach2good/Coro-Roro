#pragma once

#include "enums.h"

namespace CoroRoro
{

class Scheduler;
class IntervalTask;
class CancellationToken;

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

} // namespace CoroRoro
