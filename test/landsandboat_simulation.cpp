#include <atomic>
#include <thread>

#include <corororo/corororo.h>
using namespace CoroRoro;

#include <landsandboat_simulation.h>
using namespace LandSandBoatSimulation;

#include <gtest/gtest.h>

class LandSandBoatSimulationTests : public ::testing::Test
{
protected:
    void SetUp() override
    {
        scheduler_ = std::make_unique<Scheduler>(numThreads_);
        for (size_t i = 0; i < numZones_; ++i)
        {
            zones_.emplace_back(createRealisticZone(i, numEntitiesPerZone_));
        }
    }

    void TearDown() override
    {
        scheduler_.reset();
        zones_.clear();
    }

    size_t numThreads_         = 8;
    size_t numZones_           = 3;
    size_t numEntitiesPerZone_ = 100;

    std::unique_ptr<Scheduler>         scheduler_;
    std::vector<std::unique_ptr<Zone>> zones_;
};

TEST_F(LandSandBoatSimulationTests, zoneServerTick)
{
    coro_log("Num threads: " + std::to_string(numThreads_));
    coro_log("Num zones: " + std::to_string(numZones_));
    coro_log("Num entities per zone: " + std::to_string(numEntitiesPerZone_));
    coro_log("Total entities: " + std::to_string(numZones_ * numEntitiesPerZone_));

    for (auto& zone : zones_)
    {
        scheduler_->schedule(zoneServerTick(zone.get()));
    }
    scheduler_->runExpiredTasks();
}

TEST_F(LandSandBoatSimulationTests, co_zoneServerTick)
{
    coro_log("Num threads: " + std::to_string(numThreads_));
    coro_log("Num zones: " + std::to_string(numZones_));
    coro_log("Num entities per zone: " + std::to_string(numEntitiesPerZone_));
    coro_log("Total entities: " + std::to_string(numZones_ * numEntitiesPerZone_));

    for (auto& zone : zones_)
    {
        scheduler_->schedule(co_zoneServerTick(zone.get()));
    }
    scheduler_->runExpiredTasks();
}
