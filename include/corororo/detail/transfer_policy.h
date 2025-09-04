#pragma once

#include "enums.h"
#include "forward_declarations.h"
#include "macros.h"

namespace CoroRoro
{
namespace detail
{

// Forward declaration - will be defined after Scheduler
template <ThreadAffinity CurrentAffinity, ThreadAffinity NextAffinity>
struct TransferPolicy;

} // namespace detail
} // namespace CoroRoro
