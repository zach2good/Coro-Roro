#include <gtest/gtest.h>
#include <corororo/corororo.h>
#include <atomic>
#include <chrono>
#include <future>
#include <thread>
#include <vector>

// Note: This assumes test_utils.h exists and provides getThreadVerifier()
// If not available, these tests will need to be adapted or skipped

// Placeholder thread verifier for demonstration
class ThreadVerifier {
public:
    bool isOnMainThread() const { return std::this_thread::get_id() == mainThreadId_; }
    bool isOnWorkerThread() const { return !isOnMainThread(); }

private:
    std::thread::id mainThreadId_ = std::this_thread::get_id();
};

ThreadVerifier& getThreadVerifier() {
    static ThreadVerifier verifier;
    return verifier;
}

using namespace std::chrono_literals;
using namespace CoroRoro;

//
// THREAD VERIFIER TESTS - Thread Identity and Consistency
//

TEST(ThreadVerifierTests, MainThreadIdentification)
{
    auto& threadVerifier = getThreadVerifier();

    // Test that main thread is correctly identified
    EXPECT_TRUE(threadVerifier.isOnMainThread());
    EXPECT_FALSE(threadVerifier.isOnWorkerThread());

    // Test consistency across multiple calls
    for (int i = 0; i < 10; ++i) {
        EXPECT_TRUE(threadVerifier.isOnMainThread());
        EXPECT_FALSE(threadVerifier.isOnWorkerThread());
    }
}

TEST(ThreadVerifierTests, WorkerThreadIdentification)
{
    auto& threadVerifier = getThreadVerifier();

    // This test should be run from a worker thread
    std::promise<void> workerTaskCompleted;
    auto workerTaskFuture = workerTaskCompleted.get_future();

    std::thread workerThread(
        [&]()
        {
            // This should be running on a worker thread
            EXPECT_FALSE(threadVerifier.isOnMainThread());
            EXPECT_TRUE(threadVerifier.isOnWorkerThread());

            workerTaskCompleted.set_value();
        });

    workerThread.join();
    workerTaskFuture.get();
}

TEST(ThreadVerifierTests, ConsistentThreadIdentification)
{
    auto& threadVerifier = getThreadVerifier();

    // Test that the same thread is consistently identified
    const bool isMainInitially   = threadVerifier.isOnMainThread();
    const bool isWorkerInitially = threadVerifier.isOnWorkerThread();

    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    // Should be the same after sleeping
    EXPECT_EQ(isMainInitially, threadVerifier.isOnMainThread());
    EXPECT_EQ(isWorkerInitially, threadVerifier.isOnWorkerThread());

    // Test consistency across multiple calls
    for (int i = 0; i < 10; ++i)
    {
        EXPECT_EQ(isMainInitially, threadVerifier.isOnMainThread());
        EXPECT_EQ(isWorkerInitially, threadVerifier.isOnWorkerThread());
    }
}

TEST(ThreadVerifierTests, MultipleThreadsCorrectlyIdentified)
{
    auto& threadVerifier = getThreadVerifier();

    std::vector<std::future<void>> futures;
    std::atomic<int>               mainThreadCount{ 0 };
    std::atomic<int>               workerThreadCount{ 0 };

    // Launch multiple worker threads
    const int numWorkerThreads = 5;
    for (int i = 0; i < numWorkerThreads; ++i)
    {
        auto future = std::async(std::launch::async,
            [&]()
            {
                // Each worker thread should be identified correctly
                EXPECT_FALSE(threadVerifier.isOnMainThread());
                EXPECT_TRUE(threadVerifier.isOnWorkerThread());

                workerThreadCount.fetch_add(1);
            });
        futures.push_back(std::move(future));
    }

    // Main thread should still be identified correctly
    EXPECT_TRUE(threadVerifier.isOnMainThread());
    EXPECT_FALSE(threadVerifier.isOnWorkerThread());
    mainThreadCount.fetch_add(1);

    // Wait for all worker threads to complete
    for (auto& future : futures)
    {
        future.get();
    }

    // Verify counts
    EXPECT_EQ(mainThreadCount.load(), 1);
    EXPECT_EQ(workerThreadCount.load(), numWorkerThreads);
}

TEST(ThreadVerifierTests, ThreadIdentificationAfterContextSwitch)
{
    auto& threadVerifier = getThreadVerifier();

    // Record initial state
    const bool initialMain   = threadVerifier.isOnMainThread();
    const bool initialWorker = threadVerifier.isOnWorkerThread();

    // Force a context switch
    std::this_thread::sleep_for(std::chrono::milliseconds(1));

    // Should still be the same thread
    EXPECT_EQ(initialMain, threadVerifier.isOnMainThread());
    EXPECT_EQ(initialWorker, threadVerifier.isOnWorkerThread());

    // Force another context switch
    std::this_thread::yield();

    // Should still be consistent
    EXPECT_EQ(initialMain, threadVerifier.isOnMainThread());
    EXPECT_EQ(initialWorker, threadVerifier.isOnWorkerThread());
}

TEST(ThreadVerifierTests, ThreadVerifierThreadSafety)
{
    auto& threadVerifier = getThreadVerifier();

    std::vector<std::future<void>> futures;
    std::atomic<int>               successCount{ 0 };

    // Test thread safety by calling verifier from multiple threads simultaneously
    const int numTestThreads = 10;
    for (int i = 0; i < numTestThreads; ++i)
    {
        auto future = std::async(std::launch::async,
            [&, i]()
            {
                // Each thread should get consistent results
                const bool isMain   = threadVerifier.isOnMainThread();
                const bool isWorker = threadVerifier.isOnWorkerThread();

                // Basic sanity checks
                EXPECT_TRUE(isMain != isWorker); // Should be one or the other
                EXPECT_FALSE(isMain && isWorker); // Can't be both

                // Consistency check - same thread should always report the same
                for (int j = 0; j < 5; ++j)
                {
                    EXPECT_EQ(isMain, threadVerifier.isOnMainThread());
                    EXPECT_EQ(isWorker, threadVerifier.isOnWorkerThread());
                }

                successCount.fetch_add(1);
            });
        futures.push_back(std::move(future));
    }

    // Wait for all tests to complete
    for (auto& future : futures)
    {
        future.get();
    }

    EXPECT_EQ(successCount.load(), numTestThreads);
}

TEST(ThreadVerifierTests, ThreadVerifierPerformance)
{
    auto& threadVerifier = getThreadVerifier();

    const int numIterations = 100000;
    const auto start = std::chrono::steady_clock::now();

    // Measure performance of thread verification calls
    for (int i = 0; i < numIterations; ++i)
    {
        volatile bool isMain = threadVerifier.isOnMainThread();
        volatile bool isWorker = threadVerifier.isOnWorkerThread();
        (void)isMain;
        (void)isWorker;
    }

    const auto end = std::chrono::steady_clock::now();
    const auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start);
    const auto perCall = duration.count() / numIterations;

    std::cout << "ThreadVerifier Performance Test:" << std::endl;
    std::cout << "===============================" << std::endl;
    std::cout << "Iterations: " << numIterations << std::endl;
    std::cout << "Total time: " << duration.count() << "ns" << std::endl;
    std::cout << "Per call: " << perCall << "ns" << std::endl;
    std::cout << "Calls per second: " << (1000000000.0 / perCall) << std::endl;

    EXPECT_LT(perCall, 100) << "Thread verification should be very fast";
}
