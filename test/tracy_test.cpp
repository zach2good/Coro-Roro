#include <corororo/corororo.h>
using namespace CoroRoro;

#include <tracy/Tracy.hpp>

auto innerTask() -> AsyncTask<void>
{
    ZoneScoped;
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    co_return;
}

auto outerTask() -> Task<void>
{
    ZoneScoped;
    for (int i = 0; i < 100; ++i)
    {
        FrameMark;
        co_await innerTask();
    }
}

auto main() -> int
{
    {
        FrameMark;
        ZoneScoped;

        Scheduler scheduler(16);
        for (int i = 0; i < 10; ++i)
        {
            scheduler.schedule(outerTask());
        }
        scheduler.runExpiredTasks();
    }

    std::this_thread::sleep_for(std::chrono::seconds(1));

    return 0;
}
