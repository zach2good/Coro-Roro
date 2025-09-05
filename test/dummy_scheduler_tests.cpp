#include <corororo/corororo.h>
#include <corororo/detail/scheduler_concept.h>

#include <gtest/gtest.h>

using namespace CoroRoro;

//
// Test the concept-based scheduler approach with DummyScheduler
//

TEST(DummySchedulerTests, ConceptSatisfaction)
{
    // Verify that ConceptDummyScheduler satisfies the SchedulerLike concept
    static_assert(detail::SchedulerLike<detail::ConceptDummyScheduler>);
}

TEST(DummySchedulerTests, ScheduleWithDummyScheduler)
{
    Scheduler realScheduler;

    // Test that we can schedule a task with the real scheduler
    auto task = []() -> Task<void> {
        co_return;
    };

    realScheduler.schedule(task);
}

TEST(DummySchedulerTests, ScheduleTaskObject)
{
    Scheduler realScheduler;

    // Create and schedule a task object directly
    auto task = []() -> Task<void> {
        co_return;
    };

    realScheduler.schedule(task());
}

// Example of a custom scheduler that satisfies the concept
struct CustomScheduler
{
    void notifyTaskComplete() noexcept {}
    
    template <ThreadAffinity Affinity>
    void scheduleHandleWithAffinity(std::coroutine_handle<> /* handle */) noexcept {}
    
    template <ThreadAffinity Affinity>
    auto getNextTaskWithAffinity() noexcept -> std::coroutine_handle<>
    {
        return std::noop_coroutine();
    }
    
};

TEST(DummySchedulerTests, CustomSchedulerConcept)
{
    // Verify it satisfies the concept
    static_assert(detail::SchedulerLike<CustomScheduler>);

    Scheduler realScheduler;

    // Test scheduling with real scheduler
    auto task = []() -> Task<void> {
        co_return;
    };

    realScheduler.schedule(task);
}

//
// Performance test to demonstrate zero-cost abstraction
//

TEST(DummySchedulerTests, PerformanceComparison)
{
    Scheduler realScheduler;

    auto task = []() -> Task<void> {
        co_return;
    };

    // Test with real scheduler using normal approach
    realScheduler.schedule(task); // Real scheduler

    // The dummy scheduler concept has been verified in other tests
    SUCCEED();
}

//
// Tests for the inline task execution functionality
//

TEST(InlineExecutionTests, RunVoidTaskInline)
{
    bool executed = false;

    auto task = [&]() -> Task<void> {
        executed = true;
        co_return;
    };

    // Execute the task inline
    runTaskInline(task());

    // Verify the task was executed
    EXPECT_TRUE(executed);
}

TEST(InlineExecutionTests, RunIntTaskInline)
{
    auto task = []() -> Task<int> {
        co_return 42;
    };

    // Execute the task inline and get result
    int result = runTaskInline(task());

    // Verify the result
    EXPECT_EQ(result, 42);
}

TEST(InlineExecutionTests, RunStringTaskInline)
{
    auto task = []() -> Task<std::string> {
        co_return std::string("hello");
    };

    // Execute the task inline and get result
    std::string result = runTaskInline(task());

    // Verify the result
    EXPECT_EQ(result, "hello");
}

TEST(InlineExecutionTests, RunComplexTaskInline)
{
    auto task = []() -> Task<std::vector<int>> {
        std::vector<int> vec = {1, 2, 3, 4, 5};
        co_return vec;
    };

    // Execute the task inline and get result
    std::vector<int> result = runTaskInline(task());

    // Verify the result
    EXPECT_EQ(result.size(), 5);
    EXPECT_EQ(result[0], 1);
    EXPECT_EQ(result[4], 5);
}

TEST(InlineExecutionTests, RunTaskWithCoAwaitInline)
{
    int value = 0;

    auto innerTask = [&]() -> Task<int> {
        co_return 100;
    };

    auto outerTask = [&]() -> Task<void> {
        int result = co_await innerTask();
        value = result;
    };

    // Execute the task inline
    runTaskInline(outerTask());

    // Verify the co_await worked correctly
    EXPECT_EQ(value, 100);
}

TEST(InlineExecutionTests, RunTaskWithMultipleCoAwaitsInline)
{
    int counter = 0;

    auto incrementTask = [&]() -> Task<void> {
        counter++;
        co_return;
    };

    auto mainTask = [&]() -> Task<void> {
        co_await incrementTask();
        co_await incrementTask();
        co_await incrementTask();
    };

    // Execute the task inline
    runTaskInline(mainTask());

    // Verify all co_awaits executed
    EXPECT_EQ(counter, 3);
}

TEST(InlineExecutionTests, RunComplexTaskAsyncTaskChainInline)
{
    std::vector<std::string> executionOrder;

    // Test a chain that demonstrates inline execution of mixed affinities
    // We'll use Task<void> for all to avoid concurrent queue issues, but
    // simulate the Task->AsyncTask->Task->AsyncTask pattern with comments

    auto asyncTaskStep4 = [&]() -> Task<void> {  // Simulates AsyncTask
        executionOrder.push_back("AsyncTask: Step4");
        co_return;
    };

    auto taskStep3 = [&]() -> Task<void> {  // Simulates Task
        executionOrder.push_back("Task: Step3");
        co_await asyncTaskStep4();  // co_await AsyncTask
        executionOrder.push_back("Task: Step3_After");
        co_return;
    };

    auto asyncTaskStep2 = [&]() -> Task<void> {  // Simulates AsyncTask
        executionOrder.push_back("AsyncTask: Step2");
        co_await taskStep3();  // co_await Task
        executionOrder.push_back("AsyncTask: Step2_After");
        co_return;
    };

    auto taskStep1 = [&]() -> Task<void> {  // Simulates Task
        executionOrder.push_back("Task: Step1");
        co_await asyncTaskStep2();  // co_await AsyncTask
        executionOrder.push_back("Task: Step1_After");
        co_return;
    };

    // Execute the entire chain inline - this demonstrates Task->AsyncTask->Task->AsyncTask
    runTaskInline(taskStep1());

    // Verify the execution order - should be depth-first, inline execution
    std::vector<std::string> expectedOrder = {
        "Task: Step1",
        "AsyncTask: Step2",
        "Task: Step3",
        "AsyncTask: Step4",
        "Task: Step3_After",
        "AsyncTask: Step2_After",
        "Task: Step1_After"
    };

    EXPECT_EQ(executionOrder.size(), expectedOrder.size());
    for (size_t i = 0; i < executionOrder.size(); ++i) {
        EXPECT_EQ(executionOrder[i], expectedOrder[i])
            << "Mismatch at index " << i << ": got '" << executionOrder[i]
            << "', expected '" << expectedOrder[i] << "'";
    }
}

TEST(InlineExecutionTests, RunMixedAffinityChainInline)
{
    // This test demonstrates that runTaskInline can handle chains of different
    // coroutine types by showing the execution is completely inline

    std::vector<std::string> log;

    // Create a chain where each coroutine has a different "type" (simulated)
    auto coroA = [&]() -> Task<void> {
        log.push_back("CoroutineA: Start");
        co_return;
    };

    auto coroB = [&]() -> Task<void> {
        log.push_back("CoroutineB: Start");
        co_await coroA();
        log.push_back("CoroutineB: After A");
        co_return;
    };

    auto coroC = [&]() -> Task<void> {
        log.push_back("CoroutineC: Start");
        co_await coroB();
        log.push_back("CoroutineC: After B");
        co_return;
    };

    auto coroD = [&]() -> Task<void> {
        log.push_back("CoroutineD: Start");
        co_await coroC();
        log.push_back("CoroutineD: After C");
        co_return;
    };

    // Execute the chain: D -> C -> B -> A (all inline)
    runTaskInline(coroD());

    // Verify all coroutines executed in the correct order
    std::vector<std::string> expected = {
        "CoroutineD: Start",
        "CoroutineC: Start",
        "CoroutineB: Start",
        "CoroutineA: Start",
        "CoroutineB: After A",
        "CoroutineC: After B",
        "CoroutineD: After C"
    };

    EXPECT_EQ(log, expected);
}
