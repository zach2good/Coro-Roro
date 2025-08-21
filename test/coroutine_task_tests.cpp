#include <corororo/corororo.h>
#include <gtest/gtest.h>

#include "test_utils.h"

#include <chrono>
#include <thread>

using namespace CoroRoro;

class CoroutineTests : public ::testing::Test
{
protected:
    TestUtils::Watchdog watchdog{ std::chrono::seconds(1) };

    size_t taskCallCount       = 0;
    size_t suspensionCount     = 0;
    size_t affinityChangeCount = 0;
    size_t callsAfterMainLogic = 0;

    void SetUp() override
    {
    }
};

TEST_F(CoroutineTests, BasicTask)
{
    {
        auto task = [&]() -> Task<void>
        {
            ++taskCallCount;
            co_return;
        }();

        while (!task.done())
        {
            const auto startingAffinity = task.threadAffinity();
            const auto taskState        = task.resume();
            const auto endingAffinity   = task.threadAffinity();

            if (taskState == TaskState::Suspended)
            {
                ++suspensionCount;
            }

            if (startingAffinity != endingAffinity)
            {
                ++affinityChangeCount;
            }
        }

        EXPECT_EQ(task.threadAffinity(), ThreadAffinity::MainThread);
    }

    EXPECT_EQ(taskCallCount, 1);
    EXPECT_EQ(suspensionCount, 0);
    EXPECT_EQ(affinityChangeCount, 0);
}

TEST_F(CoroutineTests, BasicAsyncTask)
{
    {
        auto task = [&]() -> AsyncTask<void>
        {
            ++taskCallCount;
            co_return;
        }();

        while (!task.done())
        {
            const auto startingAffinity = task.threadAffinity();
            const auto taskState        = task.resume();
            const auto endingAffinity   = task.threadAffinity();

            if (taskState == TaskState::Suspended)
            {
                ++suspensionCount;
            }

            if (startingAffinity != endingAffinity)
            {
                ++affinityChangeCount;
            }
        }

        EXPECT_EQ(task.threadAffinity(), ThreadAffinity::WorkerThread);
    }

    EXPECT_EQ(taskCallCount, 1);
    EXPECT_EQ(suspensionCount, 0);
    EXPECT_EQ(affinityChangeCount, 0);
}

TEST_F(CoroutineTests, BasicNestedTask)
{
    auto task03 = [&]() -> Task<void>
    {
        ++taskCallCount;
        co_return;
    };

    auto task02 = [&]() -> Task<void>
    {
        ++taskCallCount;
        co_await task03();
        co_return;
    };

    auto task01 = [&]() -> Task<void>
    {
        ++taskCallCount;
        co_await task02();
        co_return;
    }();

    while (!task01.done())
    {
        const auto startingAffinity = task01.threadAffinity();
        const auto taskState        = task01.resume();
        const auto endingAffinity   = task01.threadAffinity();

        if (taskState == TaskState::Suspended)
        {
            ++suspensionCount;
        }

        if (startingAffinity != endingAffinity)
        {
            ++affinityChangeCount;
        }
    }

    EXPECT_EQ(taskCallCount, 3);
    EXPECT_EQ(suspensionCount, 0);
    EXPECT_EQ(affinityChangeCount, 0);
    EXPECT_EQ(task01.threadAffinity(), ThreadAffinity::MainThread);
}

TEST_F(CoroutineTests, BasicNestedAsyncTask)
{
    {
        auto task03 = [&]() -> AsyncTask<void>
        {
            ++taskCallCount;
            co_return;
        };

        auto task02 = [&]() -> AsyncTask<void>
        {
            ++taskCallCount;
            co_await task03();
            co_return;
        };

        auto task01 = [&]() -> AsyncTask<void>
        {
            ++taskCallCount;
            co_await task02();
            co_return;
        }();

        while (!task01.done())
        {
            const auto startingAffinity = task01.threadAffinity();
            const auto taskState        = task01.resume();
            const auto endingAffinity   = task01.threadAffinity();

            if (taskState == TaskState::Suspended)
            {
                ++suspensionCount;
            }

            if (startingAffinity != endingAffinity)
            {
                ++affinityChangeCount;
            }
        }

        EXPECT_EQ(task01.threadAffinity(), ThreadAffinity::WorkerThread);
    }

    EXPECT_EQ(taskCallCount, 3);
    EXPECT_EQ(suspensionCount, 0);
    EXPECT_EQ(affinityChangeCount, 0);
}

TEST_F(CoroutineTests, BasicNestedSuspension)
{
    auto task03 = [&]() -> Task<void>
    {
        ++taskCallCount;
        co_return;
    };

    // Affinity change here
    auto task02 = [&]() -> AsyncTask<void>
    {
        ++taskCallCount;
        co_await task03();
        co_return;
    };

    // Affinity change here
    auto task01 = [&]() -> Task<void>
    {
        ++taskCallCount;
        co_await task02();
        co_return;
    }();

    while (!task01.done())
    {
        const auto startingAffinity = task01.threadAffinity();
        const auto taskState        = task01.resume();
        const auto endingAffinity   = task01.threadAffinity();

        if (taskState == TaskState::Suspended)
        {
            ++suspensionCount;
        }

        if (startingAffinity != endingAffinity)
        {
            ++affinityChangeCount;
        }
    }

    EXPECT_EQ(taskCallCount, 3);
    EXPECT_EQ(suspensionCount, 4);
    EXPECT_EQ(affinityChangeCount, 4);
    EXPECT_EQ(task01.threadAffinity(), ThreadAffinity::MainThread);
}

TEST_F(CoroutineTests, TaskWithReturnValue)
{
    auto task = [&]() -> Task<int>
    {
        co_return 42;
    }();

    while (!task.done())
    {
        task.resume();
    }

    EXPECT_EQ(task.state(), TaskState::Done);
    EXPECT_EQ(task.get_result(), 42);
}

TEST_F(CoroutineTests, BasicExceptionHandling)
{
    auto throwingTask = [&]() -> Task<void>
    {
        throw std::runtime_error("Test exception");

        ASSERT_UNREACHABLE();
        co_return;
    }();

    while (!throwingTask.done())
    {
        throwingTask.resume();
    }

    EXPECT_EQ(throwingTask.state(), TaskState::Failed);
    EXPECT_THROW(throwingTask.get_result(), std::runtime_error);
}

TEST_F(CoroutineTests, LongAlternatingChain)
{
    {
        // Create a long alternating chain: Task -> AsyncTask -> Task -> AsyncTask -> etc.
        auto task12 = [&]() -> AsyncTask<void>
        {
            ++taskCallCount;
            co_return;
        };

        auto task11 = [&]() -> Task<void>
        {
            ++taskCallCount;
            co_await task12();
            co_return;
        };

        auto task10 = [&]() -> AsyncTask<void>
        {
            ++taskCallCount;
            co_await task11();
            co_return;
        };

        auto task09 = [&]() -> Task<void>
        {
            ++taskCallCount;
            co_await task10();
            co_return;
        };

        auto task08 = [&]() -> AsyncTask<void>
        {
            ++taskCallCount;
            co_await task09();
            co_return;
        };

        auto task07 = [&]() -> Task<void>
        {
            ++taskCallCount;
            co_await task08();
            co_return;
        };

        auto task06 = [&]() -> AsyncTask<void>
        {
            ++taskCallCount;
            co_await task07();
            co_return;
        };

        auto task05 = [&]() -> Task<void>
        {
            ++taskCallCount;
            co_await task06();
            co_return;
        };

        auto task04 = [&]() -> AsyncTask<void>
        {
            ++taskCallCount;
            co_await task05();
            co_return;
        };

        auto task03 = [&]() -> Task<void>
        {
            ++taskCallCount;
            co_await task04();
            co_return;
        };

        auto task02 = [&]() -> AsyncTask<void>
        {
            ++taskCallCount;
            co_await task03();
            co_return;
        };

        auto task01 = [&]() -> Task<void>
        {
            ++taskCallCount;
            co_await task02();
            co_return;
        }();

        // Track affinity changes
        auto lastAffinity = task01.threadAffinity();

        while (!task01.done())
        {
            const auto currentAffinity = task01.threadAffinity();
            if (currentAffinity != lastAffinity)
            {
                ++affinityChangeCount;
                lastAffinity = currentAffinity;
            }

            const auto taskState = task01.resume();

            if (taskState == TaskState::Failed)
            {
                FAIL() << "Task failed unexpectedly";
            }
        }
    }

    EXPECT_EQ(taskCallCount, 12);
    EXPECT_EQ(affinityChangeCount, 22); // Suspend-in/suspend-out for each alternating transition
}

TEST_F(CoroutineTests, StridesPattern)
{
    {
        // Create "strides" pattern: 3 Tasks, 3 AsyncTasks, 3 Tasks, 3 AsyncTasks

        // Final stride: 3 AsyncTasks
        auto task12 = [&]() -> AsyncTask<void>
        {
            ++taskCallCount;
            co_return;
        };

        auto task11 = [&]() -> AsyncTask<void>
        {
            ++taskCallCount;
            co_await task12();
            co_return;
        };

        auto task10 = [&]() -> AsyncTask<void>
        {
            ++taskCallCount;
            co_await task11();
            co_return;
        };

        // Third stride: 3 Tasks
        auto task09 = [&]() -> Task<void>
        {
            ++taskCallCount;
            co_await task10();
            co_return;
        };

        auto task08 = [&]() -> Task<void>
        {
            ++taskCallCount;
            co_await task09();
            co_return;
        };

        auto task07 = [&]() -> Task<void>
        {
            ++taskCallCount;
            co_await task08();
            co_return;
        };

        // Second stride: 3 AsyncTasks
        auto task06 = [&]() -> AsyncTask<void>
        {
            ++taskCallCount;
            co_await task07();
            co_return;
        };

        auto task05 = [&]() -> AsyncTask<void>
        {
            ++taskCallCount;
            co_await task06();
            co_return;
        };

        auto task04 = [&]() -> AsyncTask<void>
        {
            ++taskCallCount;
            co_await task05();
            co_return;
        };

        // First stride: 3 Tasks
        auto task03 = [&]() -> Task<void>
        {
            ++taskCallCount;
            co_await task04();
            co_return;
        };

        auto task02 = [&]() -> Task<void>
        {
            ++taskCallCount;
            co_await task03();
            co_return;
        };

        auto task01 = [&]() -> Task<void>
        {
            ++taskCallCount;
            co_await task02();
            co_return;
        }();

        // Track affinity changes
        auto lastAffinity = task01.threadAffinity();

        while (!task01.done())
        {
            const auto currentAffinity = task01.threadAffinity();
            if (currentAffinity != lastAffinity)
            {
                ++affinityChangeCount;
                lastAffinity = currentAffinity;
            }

            const auto taskState = task01.resume();

            if (taskState == TaskState::Failed)
            {
                FAIL() << "Task failed unexpectedly";
            }
        }
    }

    EXPECT_EQ(taskCallCount, 12);
    EXPECT_EQ(affinityChangeCount, 6); // Transitions in strides pattern with suspend-in/suspend-out counting
}
