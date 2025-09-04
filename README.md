# Coro-Roro

A high-performance C++20 coroutine scheduler, written for use with [LandSandBoat](https://github.com/LandSandBoat/server).

Coro-Roro provides a sophisticated coroutine scheduling system with two main task types:
- **`Task<T>`** - Executes on the main thread with immediate scheduling
- **`AsyncTask<T>`** - Executes on worker threads for background processing

## Features

- 🚀 **Optimized thread transfers** - Compile-time template dispatch minimizes runtime thread checks
- 🧵 **Symmetric transfer optimization** - Same-thread transfers are zero-overhead, cross-thread uses symmetric handoff
- 📦 **Header-only library** - Just include and go, no linking required
- 🔒 **Lockless scheduling** - Uses `moodycamel::ConcurrentQueue` for maximum performance
- ⏰ **Interval & delayed tasks** - Built-in timer system with cancellation support
- 🎯 **Thread affinity awareness** - Compile-time thread placement optimization
- 🔄 **Single-execution guarantee** - Prevents race conditions in interval tasks

## Quick Start

### Basic Usage

```cpp
#include <corororo/corororo.h>
using namespace CoroRoro;

// Create a scheduler with 4 worker threads
Scheduler scheduler(4);

// Simple main thread task
auto mainTask = []() -> Task<void> {
    std::cout << "Running on main thread" << std::endl;
    co_return;
};

// Background worker task
auto workerTask = []() -> AsyncTask<int> {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    co_return 42;
};

// Schedule tasks
scheduler.schedule(mainTask());
scheduler.schedule(workerTask());

// Process tasks (call periodically in your main loop)
scheduler.runExpiredTasks();
```

### Interval Tasks

```cpp
// Schedule a task that runs every 1 second
auto token = scheduler.scheduleInterval(std::chrono::seconds(1), []() -> Task<void> {
    std::cout << "Interval task executed!" << std::endl;
    co_return;
});

// Cancel the interval task
token.cancel();
```

### Delayed Tasks

```cpp
// Schedule a task to run after 5 seconds
auto token = scheduler.scheduleDelayed(std::chrono::seconds(5), []() -> Task<void> {
    std::cout << "Delayed task executed!" << std::endl;
    co_return;
});
```

### Cross-Thread Coordination

```cpp
auto coordinator = []() -> Task<void> {
    // This runs on the main thread
    std::cout << "Starting on main thread" << std::endl;
    
    // This automatically transfers to a worker thread
    auto result = co_await []() -> AsyncTask<int> {
        // Heavy computation on worker thread
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        co_return 42;
    }();
    
    // This resumes on the main thread
    std::cout << "Result: " << result << std::endl;
    co_return;
};

scheduler.schedule(coordinator());
```

## Performance Characteristics

- **Zero system calls in hot path** - No `std::this_thread::get_id()` calls in scheduling hot path (only in debug logging)
- **Compile-time optimization** - Template instantiation eliminates runtime conditionals for thread affinity
- **Lockless queues** - `moodycamel::ConcurrentQueue` for maximum throughput
- **Symmetric transfers** - Same-thread transfers are zero-overhead, cross-thread transfers use symmetric handoff
- **Efficient task processing** - Main thread continues processing until all tasks complete, then yields
- **Optimized worker threads** - 5ms aggressive spinning before sleeping to avoid CV overhead for quick tasks
- **Memory efficient** - RAII-based resource management with automatic cleanup

## Building

### Requirements
- CMake 3.18+
- C++20 compiler (GCC 10+, Clang 12+, MSVC 2019 16.11+)
- C++20 coroutines support

### Build Instructions

```bash
# Configure and build
cmake -S . -B build
cmake --build build

# Run tests
./build/test/corororo_tests
```

## Why the Name?

Following the Tarutaru naming convention from Final Fantasy XI:
> *"Tarutaru males are given two rhyming names put together, such as Ajido-Marujido or Zonpa-Zippa."*

## License

MIT License - see LICENSE file for details.
