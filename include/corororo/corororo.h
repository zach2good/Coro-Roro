#pragma once

//
// CoroRoro Library
//
//   A convenience header that includes the whole CoroRoro library.
//

#include <corororo/coroutine/awaiter.h>
#include <corororo/coroutine/execution_context.h>
#include <corororo/coroutine/promise.h>
#include <corororo/coroutine/task.h>
#include <corororo/coroutine/task_state.h>
#include <corororo/coroutine/thread_affinity.h>
#include <corororo/coroutine/types.h>

#include <corororo/scheduler/cancellation_token.h>
#include <corororo/scheduler/delayed_task.h>
#include <corororo/scheduler/forced_affinity_task.h>
#include <corororo/scheduler/forced_thread_affinity.h>
#include <corororo/scheduler/interval_task.h>
#include <corororo/scheduler/schedulable_task.h>
#include <corororo/scheduler/scheduled_task.h>
#include <corororo/scheduler/scheduler.h>
#include <corororo/scheduler/task_id.h>
#include <corororo/scheduler/worker_pool.h>

#include <corororo/util/macros.h>
#include <corororo/util/run_coroutine_inline.h>
