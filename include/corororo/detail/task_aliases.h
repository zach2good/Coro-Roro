#pragma once

#include "enums.h"
#include "task_base.h"

namespace CoroRoro
{

//
// User-facing Task Aliases
//

template <typename T = void>
using Task = CoroRoro::detail::TaskBase<CoroRoro::ThreadAffinity::Main, T>;

template <typename T = void>
using AsyncTask = CoroRoro::detail::TaskBase<CoroRoro::ThreadAffinity::Worker, T>;

} // namespace CoroRoro
