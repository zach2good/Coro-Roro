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

// Forward declarations
template <typename T = void>
class Task;

template <typename T = void>
class AsyncTask;

} // namespace CoroRoro