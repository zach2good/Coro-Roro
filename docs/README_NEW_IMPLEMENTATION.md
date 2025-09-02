# CoroRoro New Implementation

## Overview

This is a new implementation of the CoroRoro coroutine and scheduler system designed to address the performance issues identified in the PERFORMANCE_PLAN.md. The new system implements **handle-based thread switching** instead of complex context transfer, which should dramatically reduce the thread switching overhead from 23,338μs to approximately 1,000-5,000μs per switch.

## Key Changes

### 1. Handle-Based Architecture

The new system eliminates the complex `ExecutionContext` and context transfer mechanism in favor of direct coroutine handle management:

- **Before**: Complex context objects with shared_ptr overhead and child tracking
- **After**: Direct coroutine handle transfer with automatic scheduler reference propagation

### 2. Simplified Promise System

- **Removed**: Joinable concept and semaphore handling
- **Simplified**: Single promise type with automatic scheduler reference inheritance
- **Benefit**: Cleaner code, less memory overhead, easier to understand

### 3. Direct Scheduler Integration

- **Before**: Tasks were standalone entities that could be executed independently
- **After**: Tasks require a scheduler to function, with automatic reference propagation
- **Benefit**: Enables handle-based optimizations and eliminates context transfer overhead

### 4. Automatic Work Distribution

- **Removed**: Manual thread switching and CPU affinity control
- **Simplified**: Automatic work distribution to available worker threads
- **Benefit**: Cleaner API, easier to use, focused on performance optimization

### 5. Clean Dependency Injection

- **Removed**: Global static scheduler instances
- **Implemented**: Scheduler reference propagation through promise system
- **Benefit**: Clean dependency injection, testable code, no global state

### 6. Pure Declarative Design ⭐

- **Removed**: Manual suspension and thread control
- **Implemented**: Pure declarative coroutine definitions
- **Benefit**: **Natural Syntax** - Tasks look like regular coroutines without any infrastructure concerns

## 🚀 **Core Benefits**

### **Pure Declarative Design**
- Tasks are purely declarative - developers define what work needs to be done
- No manual thread management, suspension, or infrastructure code
- Scheduler automatically handles all optimization and thread distribution

### **Clean API**
- Only two task types: `Task<T>` and `AsyncTask<T>`
- Single scheduling method: `scheduler.schedule(task)`
- No complex configuration or manual thread control

### **Natural Syntax**
```cpp
// Clean, natural coroutine syntax - no scheduler capture needed
auto task = []() -> Task<int> {
    int result = 42;
    co_return result;
}();

// Scheduler reference only visible at the boundary
scheduler.schedule(task);
```

### **Testability** ⭐
- **Each test can use its own scheduler instance without global state**
- No shared global scheduler between tests
- Easy to create isolated test environments
- Clean unit testing without side effects

### **Maintainability** ⭐
- **Scheduler changes don't require updating task implementations**
- Tasks are completely decoupled from scheduler implementation details
- Infrastructure changes are isolated to the scheduler layer
- Easy to modify or replace scheduler without touching task code

## Architecture

### Core Components

1. **Task<T>**: Basic coroutine task type
2. **AsyncTask<T>**: Task that automatically suspends to worker threads
3. **Scheduler**: Handle-based scheduler with automatic work distribution
4. **WorkerPool**: Efficient worker thread management
5. **WorkQueue**: Simple work distribution per worker

### Scheduler Reference Flow

```cpp
// Scheduler reference is completely invisible to user code
auto parentTask = []() -> Task<int> {
    // Promise system automatically has scheduler reference from initial setup
    
    auto childTask = []() -> Task<int> {
        // Promise system automatically inherits scheduler reference from parent
        // No manual capture needed - completely invisible to user code
        
        auto asyncTask = []() -> AsyncTask<int> {
            // Promise system automatically inherits scheduler reference from parent
            // Handle transfer uses the inherited scheduler reference
            co_return 42;
        }();
        
        co_return co_await asyncTask;
    }();
    
    co_return co_await childTask;
};

scheduler.schedule(parentTask);  // Only place scheduler reference is visible to user code
```

## Performance Improvements

### Expected Results

- **Thread switching overhead**: 23,338μs → 1,000-5,000μs (95.7% reduction)
- **LandSandBoat impact**: 118,764ms → 5,000-25,000ms (95.8% reduction)
- **Zone scaling**: 20 zones → 25 zones (25% improvement)

### Optimization Techniques

1. **Handle Transfer**: Replace complex context transfer with simple handle transfer
2. **Direct Scheduling**: Eliminate intermediate objects and lambda captures
3. **Automatic Work Distribution**: Efficient round-robin distribution to worker threads
4. **Simplified Queues**: Single work queue per worker for maximum efficiency
5. **Clean Dependency Injection**: No global state, proper scheduler propagation
6. **Declarative Coroutines**: No manual thread control, pure performance optimization

## Usage

### Basic Task

```cpp
auto task = []() -> Task<int> {
    // Do some work
    int result = 42;
    co_return result;
}();

scheduler.schedule(task);
```

### Async Task (Worker Thread)

```cpp
auto asyncTask = []() -> AsyncTask<int> {
    // This automatically suspends to be scheduled on a worker thread
    co_return 42;
}();

scheduler.schedule(asyncTask);
```

### Task Composition

```cpp
auto parentTask = []() -> Task<int> {
    auto childTask = []() -> Task<int> {
        // Automatically inherits scheduler reference from parent
        co_return 42;
    }();
    
    co_return co_await childTask;
}();

scheduler.schedule(parentTask);
```

## Building

### Prerequisites

- C++20 compiler (MSVC 2019+, GCC 10+, Clang 10+)
- CMake 3.20+

### Build Steps

```bash
mkdir build_new
cd build_new
cmake -f ../CMakeLists_new.txt ..
cmake --build .
```

### Test

```bash
./corororo_test
```

## Migration Guide

### From Old Implementation

1. **Replace CoroutineTask with Task**
   ```cpp
   // Old
   CoroutineTask<ThreadAffinity::Any, int> task;
   
   // New
   Task<int> task;
   ```

2. **Replace AsyncTask with AsyncTask**
   ```cpp
   // Old
   AsyncTask<int> asyncTask;
   
   // New
   AsyncTask<int> asyncTask; // Same name, different implementation
   ```

3. **Update scheduler calls**
   ```cpp
   // Old
   scheduler->schedule(task);
   
   // New
   scheduler.schedule(task); // Note: dot instead of arrow
   ```

4. **Remove manual thread switching**
   ```cpp
   // Old - Manual CPU affinity
   co_await CORORORO_SUSPEND4(0x02); // Use CPU 1
   
   // New - Automatic scheduling only
   // No manual suspension - tasks are automatically distributed
   ```

5. **Remove global scheduler references**
   ```cpp
   // Old - Global instance
   Scheduler::instance().schedule(task);
   
   // New - Explicit scheduler reference
   scheduler.schedule(task);
   ```

### Breaking Changes

1. **Tasks require scheduler**: Tasks can no longer be executed standalone
2. **No joinable concept**: Simplified task lifecycle management
3. **No manual thread control**: Automatic work distribution only
4. **No global scheduler**: Must explicitly pass scheduler reference
5. **No manual suspension**: Tasks are purely declarative
6. **Different header structure**: Some include paths may have changed

## Testing

The new implementation maintains compatibility with the existing test suite:

- **Basic scheduler tests**: Task scheduling and execution
- **Complex workload tests**: Multiple task coordination
- **Performance tests**: Thread switching overhead measurement
- **Pressure tests**: High-load scenarios

## Future Optimizations

### Phase 2: Advanced Handle Transfer

- **Direct handle transfer**: 1,000μs → 100-500μs per switch
- **Handle pooling**: 100μs → 50-200μs per switch
- **Lock-free transfer**: 50μs → 10-100μs per switch

### Phase 3: Batching

- **Batch operations**: Reduce thread switch frequency by 90%+
- **Vector-based batching**: Efficient bulk operations
- **Smart batching**: Automatic batching detection

### Phase 4: Memory Optimization

- **Memory pools**: 80%+ reduction in allocation overhead
- **Object pooling**: Reuse frequently allocated types
- **Cache optimization**: Better memory locality

## Conclusion

The new CoroRoro implementation represents a fundamental architectural shift from complex context transfer to efficient handle-based scheduling. While this breaks some of the original encapsulation boundaries, it provides the performance improvements necessary to achieve the target of 400ms per tick per zone.

The key insight is that **handle-based thread switching** can dramatically reduce overhead while maintaining the same syntax and functionality. Combined with **automatic scheduler reference propagation** and **clean dependency injection**, this approach provides clean dependency injection without the performance penalties of the old system.

**Performance Target**: Achieve 95.7% reduction in thread switching overhead to enable scaling from 20 to 100 zones while maintaining the elegant coroutine syntax that makes the codebase maintainable and readable.

**Clean Architecture**: By removing global state and implementing proper dependency injection through the promise system, the code is now testable, maintainable, and follows clean architecture principles while achieving the required performance targets.

**Declarative Design**: Coroutines are now purely declarative - developers define what work needs to be done, and the scheduler automatically handles all thread management and optimization. This eliminates the complexity of manual thread control while maximizing performance through automatic handle-based scheduling.
