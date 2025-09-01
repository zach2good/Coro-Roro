#pragma once

#include <cstdint>

namespace CoroRoro
{

// Thread affinity types
enum class ThreadAffinity : uint32_t
{
    Main = 1,
    Worker = 2
};

// Task and AsyncTask are now aliases defined in task.h

} // namespace CoroRoro