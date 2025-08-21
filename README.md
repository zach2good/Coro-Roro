# CoroRoro

A lightweight C++20 cooperative multitasking library using coroutines with explicit thread affinity.

CoroRoro provides two simple coroutine types:
- **`Task<T>`** - Hinting to run on main thread
- **`AsyncTask<T>`** - Hinting to run on worker threads

## Features

- 🚀 **Zero-overhead abstraction** - Direct transfers for same-thread operations
- 🧵 **Thread-aware scheduling** - Automatic suspend/resume across thread affinity boundaries  
- 📦 **Header-only** - Just include and go
- 🎯 **Simple API** - Only 4 methods: `resume()`, `done()`, `state()`, `threadAffinity()`

## Why the Name?

Following the Tarutaru naming convention from Final Fantasy XI:
> *"Tarutaru males are given two rhyming names put together, such as Ajido-Marujido or Zonpa-Zippa."*

## Quick start

```cpp
#include <corororo/corororo.h>
using namespace CoroRoro;

auto simpleTask() -> Task<void>
{
    co_return;
}

// Execute the task
auto task = simpleTask();
while (!task.done())
{
    // Will run until completion or a suspension point
    task.resume();
}
```

## Building

Requirements: CMake 3.18+, C++20 compiler

```bash
cmake -S . -B build
cmake --build build
```

Run tests:
```bash
cmake -S . -B build 
cmake --build build
./build/test/coro_roro_tests
```

## License

MIT License - see LICENSE file for details.
