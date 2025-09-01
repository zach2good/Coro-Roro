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

} // namespace CoroRoro