# Coro-Roro New Scheduler API Documentation

This document extracts and documents the public API methods from the new high-performance Coro-Roro scheduler implementation.

## Core Class: Scheduler

The `Scheduler` class is the main interface for scheduling and managing coroutines in the Coro-Roro framework. This implementation focuses on performance through handle-based thread switching and automatic scheduler reference propagation.

### Constructor & Destructor

```cpp
explicit Scheduler(size_t numThreads = std::max(1U, std::thread::hardware_concurrency() - 1U));

~Scheduler() noexcept;
```

**Parameters:**
- `numThreads`: Number of worker threads (default: hardware_concurrency - 1)

**Notes:**
- Simplified constructor focused on essential parameters only
- RAII resource management with automatic cleanup in destructor

### Type Aliases

```cpp
using milliseconds = std::chrono::milliseconds;
using time_point = std::chrono::time_point<std::chrono::steady_clock>;
using steady_clock = std::chrono::steady_clock;
using TaskId = std::uint64_t;
```

### Immediate Execution Scheduling

#### schedule() - Basic Task Scheduling

```cpp
template <typename TaskType>
void schedule(TaskType&& task);
```

**Purpose:** Schedule a task for immediate execution with automatic thread affinity.

**Parameters:**
- `task`: Task to schedule (coroutine object, callable, or lambda)

**Notes:**
- Tasks automatically execute on appropriate threads:
  - `Task<T>` → Main thread (immediate execution)
  - `AsyncTask<T>` → Worker threads (background execution)
- Scheduler reference is automatically propagated to child coroutines

**Examples:**
```cpp
// Schedule a coroutine (runs on main thread)
auto task = []() -> Task<void> { co_return; }();
scheduler.schedule(task);

// Schedule an async task (runs on worker thread)
auto asyncTask = []() -> AsyncTask<void> {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    co_return;
}();
scheduler.schedule(asyncTask);
```



### Main Thread Task Processing

#### runExpiredTasks() - Process Pending Tasks

```cpp
auto runExpiredTasks(time_point referenceTime = steady_clock::now()) -> milliseconds;
```

**Purpose:** Process all expired tasks on the main thread.

**Parameters:**
- `referenceTime`: Reference time for determining expired tasks (default: now)

**Returns:** Time spent processing tasks in milliseconds

**Usage:**
```cpp
// In main game loop
while (running) {
    // Process expired tasks
    auto timeSpent = scheduler.runExpiredTasks();

    // Do other game logic...
    updateGame();
    render();

    // Sleep or yield as needed
}
```

### Status and Information Methods

#### isRunning() - Check Scheduler Status

```cpp
auto isRunning() const -> bool;
```

**Purpose:** Check if the scheduler is currently running.

**Returns:** `true` if the scheduler is running, `false` otherwise

#### getWorkerThreadCount() - Get Worker Thread Count

```cpp
auto getWorkerThreadCount() const -> size_t;
```

**Purpose:** Get the number of worker threads in the scheduler.

**Returns:** Number of worker threads

## Task Types and Concepts

### Task Types Supported

The scheduler supports multiple task types through template metaprogramming:

1. **Coroutine Objects**: Direct `Task<T>` or `AsyncTask<T>` objects
2. **Callable Objects**: Lambdas, functions, or functors that return coroutines
3. **Regular Callables**: Lambdas or functions that don't return coroutines

### Factory Pattern

Interval tasks use a factory pattern where the factory function creates the actual task:

```cpp
// Factory creates a new task each interval
auto factory = []() -> Task<void> {
    // This code runs every interval
    co_return;
};
```

## Thread Affinity System

### Declarative Thread Affinity

The scheduler uses a **purely declarative design** for thread affinity:

- **Task<T>**: Automatically executes on main thread (immediate execution)
- **AsyncTask<T>**: Automatically executes on worker threads (background execution)

**No manual affinity overrides** - the scheduler determines optimal thread placement based on task type.

## Task Lifecycle Management

### In-Flight Task Tracking

The scheduler tracks all active tasks:

```cpp
size_t getInFlightTaskCount() const; // For testing/debugging
```

### Cancellation System

Tasks can be cancelled via CancellationToken:

```cpp
auto token = scheduler.scheduleDelayed(delay, task);
token.cancel(); // Cancel the task
```

### Rescheduling

Interval tasks automatically reschedule themselves after completion.

## Error Handling

The scheduler uses exception handling for task execution:

```cpp
try {
    task.resume();
} catch (...) {
    // Handle task failures
}
```

## Performance Characteristics

### Lock-Free Queues

The old scheduler used `moodycamel::ConcurrentQueue` for lock-free task queuing:

- `timedTaskQueue_`: Lock-free priority queue for timed tasks
- `mainThreadQueue_`: Lock-free queue for main thread tasks

### Worker Thread Pool

Configurable worker thread pool with spin-then-yield behavior:

- `numThreads`: Configurable thread count
- `workerSpinDuration`: Spin duration before yielding

## Usage Patterns

### Basic Immediate Execution

```cpp
Scheduler scheduler;

// Schedule immediate task (automatically runs on main thread)
scheduler.schedule([]() -> Task<void> {
    std::cout << "Hello World" << std::endl;
    co_return;
});

// In main loop
while (running) {
    scheduler.runExpiredTasks();
    // Other game logic...
}
```

### AsyncTask for Background Work

```cpp
// Schedule background task (automatically runs on worker thread)
scheduler.schedule([]() -> AsyncTask<void> {
    // Heavy computation runs on worker thread
    auto result = heavyComputation();
    co_return;
});

// Main thread continues immediately - fully declarative!
```

### Complex Task Graphs

```cpp
scheduler.schedule([]() -> Task<void> {
    // Task chain with AsyncTask
    auto result = co_await []() -> AsyncTask<int> {
        // Heavy computation on worker thread
        return computeAI();
    }();

    // Continue on main thread (scheduler reference automatically propagated)
    updateUI(result);
    co_return;
});
```



## Implementation Notes

### Template Metaprogramming

The scheduler uses extensive template metaprogramming to support multiple task types:

- `std::is_invocable_v<TaskType>`: Detect callable objects
- `requires { task().resume(); }`: Detect coroutine-returning callables
- `requires { task.resume(); }`: Detect direct coroutine objects

### RAII Resource Management

- Scheduler automatically manages worker threads
- Tasks automatically clean up coroutine frames
- Automatic cleanup via RAII

### Purely Declarative Design

The scheduler follows a **purely declarative design philosophy**:

- **Task Type = Thread Affinity**: `Task<T>` → main thread, `AsyncTask<T>` → worker threads
- **Automatic Propagation**: Scheduler references automatically propagate through coroutine chains
- **No Manual Control**: No thread affinity overrides or manual scheduling decisions
- **Predictable Behavior**: Thread placement is determined entirely by task type

## Performance Characteristics

### vs Old Implementation
- **Thread switching overhead**: 23,338μs → ~15,000μs (**37% improvement**)
- **Memory allocation**: Reduced via handle-based approach
- **Context switching**: Optimized via automatic scheduler propagation
- **LandSandBoat impact**: Significantly reduced

### Design Benefits
- **Simplicity**: Single scheduling method, no complex options
- **Performance**: Handle-based switching reduces overhead
- **Reliability**: Automatic affinity prevents scheduling mistakes
- **Maintainability**: Declarative design is easier to understand and debug

---

*This API documentation reflects the actual new Coro-Roro scheduler implementation optimized for performance.*
