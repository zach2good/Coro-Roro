#pragma once

#include <iostream>
#include <string>
#include <thread>

namespace CoroRoro
{
namespace detail
{

//
// Logging
//

inline void coro_log(const std::string& message)
{
    static auto mainThreadString = std::this_thread::get_id();
    std::cout << ((std::this_thread::get_id() == mainThreadString) ? "[Main Thread] " : "[Worker Thread] ") << message << std::endl;
}

} // namespace detail
} // namespace CoroRoro
