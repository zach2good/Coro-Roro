#include <corororo/corororo.h>
using namespace CoroRoro;

#include <gtest/gtest.h>

#include "test_utils.h"

#include <chrono>
#include <future>
#include <thread>

//
// ThreadVerifier Tests
//
// Tests to verify that the ThreadVerifier utility correctly identifies
// which thread (main vs worker) coroutines are executing on.
//

TEST(ThreadVerifierTests, IdentifiesMainThread)
{
    auto& threadVerifier = TestUtils::getThreadVerifier();

    // This test should be running on the main thread
    EXPECT_TRUE(threadVerifier.isOnMainThread());
    EXPECT_FALSE(threadVerifier.isOnWorkerThread());
}

TEST(ThreadVerifierTests, IdentifiesWorkerThread)
{
    auto& threadVerifier = TestUtils::getThreadVerifier();

    // Create a promise to run a task on a worker thread
    auto workerTaskCompleted = std::promise<void>();
    auto workerTaskFuture    = workerTaskCompleted.get_future();

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
    auto& threadVerifier = TestUtils::getThreadVerifier();

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
    auto& threadVerifier = TestUtils::getThreadVerifier();

    std::vector<std::future<void>> futures;
    std::atomic<int>               mainThreadCount{ 0 };
    std::atomic<int>               workerThreadCount{ 0 };

    // Launch multiple worker threads
    for (int i = 0; i < 5; ++i)
    {
        futures.push_back(
            std::async(
                std::launch::async,
                [&]()
                {
                    if (threadVerifier.isOnMainThread())
                    {
                        ++mainThreadCount;
                    }
                    else if (threadVerifier.isOnWorkerThread())
                    {
                        ++workerThreadCount;
                    }
                }));
    }

    // Wait for all threads to complete
    for (auto& future : futures)
    {
        future.get();
    }

    // All worker threads should be identified as worker threads
    EXPECT_EQ(workerThreadCount, 5);
    EXPECT_EQ(mainThreadCount, 0);
}

TEST(ThreadVerifierTests, ThreadVerifierInitialisation)
{
    // Test that we can initialise the thread verifier
    auto& threadVerifier = TestUtils::getThreadVerifier();

    // Should not crash and should return valid results
    const bool isMain   = threadVerifier.isOnMainThread();
    const bool isWorker = threadVerifier.isOnWorkerThread();

    // One of them should be true (we're either on main or worker thread)
    EXPECT_TRUE(isMain || isWorker);

    // They should be mutually exclusive
    EXPECT_FALSE(isMain && isWorker);
}

TEST(ThreadVerifierTests, ThreadVerifierThreadSafety)
{
    auto& threadVerifier = TestUtils::getThreadVerifier();

    std::vector<std::future<void>> futures;

    // Launch multiple threads that all use the thread verifier simultaneously
    for (int i = 0; i < 10; ++i)
    {
        futures.push_back(
            std::async(
                std::launch::async,
                [&]()
                {
                    // Rapid succession of calls to test thread safety
                    for (int j = 0; j < 100; ++j)
                    {
                        (void)threadVerifier.isOnMainThread();
                        (void)threadVerifier.isOnWorkerThread();
                    }
                }));
    }

    // Wait for all threads to complete
    for (auto& future : futures)
    {
        future.get();
    }

    // If we get here without crashing or deadlocks, the thread verifier is thread-safe
    EXPECT_TRUE(true);
}
