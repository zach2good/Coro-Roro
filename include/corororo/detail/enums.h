#pragma once

#include <cstdint>

namespace CoroRoro
{

//
// Enums
//

enum class ThreadAffinity : std::uint8_t
{
    Main   = 0,
    Worker = 1,
};

} // namespace CoroRoro
