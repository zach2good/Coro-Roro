#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <coroutine>
#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <queue>
#include <sstream>
#include <thread>
#include <type_traits>
#include <variant>
#include <vector>

// clang-format off
// Include detail headers
#include "detail/macros.h"
#include "detail/logging.h"
#include "detail/enums.h"
#include "detail/forward_declarations.h"
#include "detail/transfer_policy.h"
#include "detail/continuation.h"
#include "detail/task_base.h"
#include "detail/awaiters.h"
#include "detail/promise.h"
#include "detail/interval_task.h"
#include "detail/cancellation_token.h"
#include "detail/scheduler.h"
#include "detail/task_aliases.h"
#include "detail/scheduler_extensions.h"
#include "detail/transfer_policy_impl.h"
#include "detail/interval_task_impl.h"
#include "detail/cancellation_token_impl.h"
#include "detail/utils.h"
// clang-format on

namespace CoroRoro
{

// Bring logging function into main namespace for convenience
using detail::coro_log;

// Bring commonly used types into main namespace for convenience
using detail::ContinuationVariant;
using detail::TransferPolicy;

// Bring utility functions into main namespace for convenience
using detail::runCoroutineInline;

} // namespace CoroRoro
