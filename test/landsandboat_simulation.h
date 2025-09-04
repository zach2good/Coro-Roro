#pragma once

#include <atomic>
#include <chrono>
#include <memory>
#include <random>
#include <vector>

namespace LandSandBoatSimulation
{

auto getRandomFloat() -> float
{
    static std::random_device                    rd;
    static std::uniform_real_distribution<float> distribution(0.0f, 1.0f);
    static std::mt19937                          generator(rd());
    return distribution(generator);
}

//
// Realistic LandSandBoat Data Structures
//

// Position structure (3x floats) as used in LandSandBoat
struct position_t
{
    float x, y, z;

    position_t()
    : x(0.0f)
    , y(0.0f)
    , z(0.0f)
    {
    }
    position_t(float x_, float y_, float z_)
    : x(x_)
    , y(y_)
    , z(z_)
    {
    }

    // Basic vector operations
    position_t operator+(const position_t& other) const
    {
        return position_t(x + other.x, y + other.y, z + other.z);
    }

    position_t operator-(const position_t& other) const
    {
        return position_t(x - other.x, y - other.y, z - other.z);
    }

    float distance(const position_t& other) const
    {
        float dx = x - other.x;
        float dy = y - other.y;
        float dz = z - other.z;
        return std::sqrt(dx * dx + dy * dy + dz * dz);
    }
};

// Entity data structure (simplified from LandSandBoat)
struct Entity
{
    size_t                  id;
    position_t              position;
    position_t              target_position;
    std::vector<position_t> path;
    bool                    is_moving;
    bool                    needs_pathfinding;

    Entity(size_t entity_id)
    : id(entity_id)
    , position(0.0f, 0.0f, 0.0f)
    , target_position(0.0f, 0.0f, 0.0f)
    , is_moving(false)
    , needs_pathfinding(false)
    {
    }
};

// Zone data structure (simplified from LandSandBoat)
struct Zone
{
    size_t                               zone_id;
    std::vector<std::unique_ptr<Entity>> entities;
    std::atomic<int>                     active_entities{ 0 };

    Zone(size_t id)
    : zone_id(id)
    {
    }

    void addEntity(size_t entity_id)
    {
        entities.emplace_back(std::make_unique<Entity>(entity_id));
        active_entities.fetch_add(1);
    }

    Entity* getEntity(size_t entity_id)
    {
        for (auto& entity : entities)
        {
            if (entity->id == entity_id)
                return entity.get();
        }
        return nullptr;
    }
};

//
// Realistic Work Simulation Functions
//

// Simulate pathfinding calculation (like LandSandBoat's navmesh)
inline auto co_simulatePathfinding(const position_t& start, const position_t& end) -> AsyncTask<std::vector<position_t>>
{
    // Simulate actual pathfinding work (5ms as mentioned in the user's requirements)
    std::this_thread::sleep_for(std::chrono::milliseconds(5));

    // Generate a realistic path with waypoints
    std::vector<position_t> path;
    path.push_back(start);

    // Add some intermediate waypoints (simulating navmesh pathfinding)
    float distance  = start.distance(end);
    int   waypoints = static_cast<int>(distance / 10.0f); // Waypoint every 10 units

    for (int i = 1; i < waypoints && i < 5; ++i) // Max 5 waypoints
    {
        float      t = static_cast<float>(i) / static_cast<float>(waypoints);
        position_t waypoint(
            start.x + (end.x - start.x) * t,
            start.y + (end.y - start.y) * t,
            start.z + (end.z - start.z) * t);
        path.push_back(waypoint);
    }

    path.push_back(end);
    co_return path;
}

// Simulate pathfinding calculation (like LandSandBoat's navmesh)
inline auto simulatePathfinding(const position_t& start, const position_t& end) -> Task<std::vector<position_t>>
{
    // Simulate actual pathfinding work (5ms as mentioned in the user's requirements)
    std::this_thread::sleep_for(std::chrono::milliseconds(5));

    // Generate a realistic path with waypoints
    std::vector<position_t> path;
    path.push_back(start);

    // Add some intermediate waypoints (simulating navmesh pathfinding)
    float distance  = start.distance(end);
    int   waypoints = static_cast<int>(distance / 10.0f); // Waypoint every 10 units

    for (int i = 1; i < waypoints && i < 5; ++i) // Max 5 waypoints
    {
        float      t = static_cast<float>(i) / static_cast<float>(waypoints);
        position_t waypoint(
            start.x + (end.x - start.x) * t,
            start.y + (end.y - start.y) * t,
            start.z + (end.z - start.z) * t);
        path.push_back(waypoint);
    }

    path.push_back(end);
    co_return path;
}

inline auto co_simulateAIDecision(Entity* entity) -> AsyncTask<bool>
{
    // Simulate AI processing time (1-3ms)
    std::this_thread::sleep_for(std::chrono::milliseconds(entity->id % 2));

    // 30% chance entity needs to move
    co_return (entity->id % 10) < 3;
}

// Simulate AI decision making
inline auto simulateAIDecision(Entity* entity) -> Task<bool>
{
    // Simulate AI processing time (0-1ms)
    std::this_thread::sleep_for(std::chrono::milliseconds(entity->id % 2));

    // 30% chance entity needs to move
    co_return (entity->id % 10) < 3;
}

// Simulate entity movement
inline auto co_simulateEntityMovement(Entity* entity) -> AsyncTask<void>
{
    // Simulate movement processing (2-5ms)
    std::this_thread::sleep_for(std::chrono::milliseconds(1 + (entity->id % 3)));

    if (!entity->path.empty())
    {
        // Move towards next waypoint
        entity->position = entity->path[0];
        entity->path.erase(entity->path.begin());

        if (entity->path.empty())
        {
            entity->is_moving = false;
        }
    }

    co_return;
}

inline auto simulateEntityMovement(Entity* entity) -> Task<void>
{
    // Simulate movement processing (1-3ms)
    std::this_thread::sleep_for(std::chrono::milliseconds(1 + (entity->id % 3)));

    if (!entity->path.empty())
    {
        // Move towards next waypoint
        entity->position = entity->path[0];
        entity->path.erase(entity->path.begin());

        if (entity->path.empty())
        {
            entity->is_moving = false;
        }
    }

    co_return;
}

// Simulate database operations
inline auto co_simulateDatabaseUpdate(const Entity* entity) -> AsyncTask<void>
{
    // Simulate database write (1-2ms)
    std::this_thread::sleep_for(std::chrono::milliseconds(1 + (entity->id % 2)));
    co_return;
}

inline auto simulateDatabaseUpdate(const Entity* entity) -> Task<void>
{
    // Simulate database write (1-2ms)
    std::this_thread::sleep_for(std::chrono::milliseconds(1 + (entity->id % 2)));
    co_return;
}

// Simulate network packet sending
inline auto co_simulateNetworkPacket(const Entity* entity) -> AsyncTask<void>
{
    // Simulate network operations (0.5-1ms)
    std::this_thread::sleep_for(std::chrono::microseconds(500 + (entity->id % 500)));
    co_return;
}

inline auto simulateNetworkPacket(const Entity* entity) -> Task<void>
{
    // Simulate network operations (0.5-1ms)
    std::this_thread::sleep_for(std::chrono::microseconds(500 + (entity->id % 500)));
    co_return;
}

//
// co_* Realistic Coroutine Chain Simulation
//

// Level 4: AsyncTask for pathfinding (runs on worker thread)
inline auto co_pathfinding(const position_t& start, const position_t& end) -> AsyncTask<std::vector<position_t>>
{
    // This simulates the actual pathfinding call in LandSandBoat
    // This runs on worker thread and takes ~5ms
    auto path = co_await co_simulatePathfinding(start, end);
    co_return path;
}

// Level 3: Entity AI processing (Task on main thread)
inline auto co_processEntityAI(Entity* entity) -> AsyncTask<void>
{
    // Simulate AI decision making
    bool needs_movement = co_await simulateAIDecision(entity);
    if (needs_movement && !entity->is_moving)
    {
        position_t target(getRandomFloat(), getRandomFloat(), getRandomFloat());
        entity->target_position   = target;
        entity->needs_pathfinding = true;

        // This is the key suspension point - pathfinding runs on worker thread
        entity->path = co_await co_pathfinding(entity->position, target);

        entity->is_moving         = true;
        entity->needs_pathfinding = false;
    }

    // Process movement
    if (entity->is_moving)
    {
        co_await co_simulateEntityMovement(entity);
    }

    // Update database
    co_await co_simulateDatabaseUpdate(entity);

    co_return;
}

// Level 2: Zone entity processing (Task on main thread)
inline auto co_processZoneEntities(Zone* zone) -> AsyncTask<void>
{
    // Process each entity in the zone
    for (auto& entity : zone->entities)
    {
        if (entity)
        {
            co_await co_processEntityAI(entity.get());
        }
    }

    // Send network updates
    for (auto& entity : zone->entities)
    {
        if (entity)
        {
            co_await co_simulateNetworkPacket(entity.get());
        }
    }

    co_return;
}

// Level 1: Zone server tick (Task on main thread)
inline auto co_zoneServerTick(Zone* zone) -> Task<void>
{
    const auto start = std::chrono::high_resolution_clock::now();

    // Zone initialization work (like CZone::ZoneServer)
    std::this_thread::sleep_for(std::chrono::milliseconds(1));

    // Process all entities in the zone
    co_await co_processZoneEntities(zone);

    // Zone cleanup and state updates
    std::this_thread::sleep_for(std::chrono::milliseconds(1));

    const auto end      = std::chrono::high_resolution_clock::now();
    const auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    coro_log("Zone server tick duration: " + std::to_string(duration.count()) + "ms");

    co_return;
}

//
// SYNC Realistic Coroutine Chain Simulation
//

// Level 4: AsyncTask for pathfinding (runs on worker thread)
inline auto pathfinding(const position_t& start, const position_t& end) -> Task<std::vector<position_t>>
{
    // This simulates the actual pathfinding call in LandSandBoat
    // This runs on worker thread and takes ~5ms
    auto path = co_await simulatePathfinding(start, end);
    co_return path;
}

// Level 3: Entity AI processing (Task on main thread)
inline auto processEntityAI(Entity* entity) -> Task<void>
{
    // Simulate AI decision making
    bool needs_movement = co_await simulateAIDecision(entity);
    if (needs_movement && !entity->is_moving)
    {
        position_t target(getRandomFloat(), getRandomFloat(), getRandomFloat());
        entity->target_position   = target;
        entity->needs_pathfinding = true;

        // This is the key suspension point - pathfinding runs on worker thread
        entity->path = co_await pathfinding(entity->position, target);

        entity->is_moving         = true;
        entity->needs_pathfinding = false;
    }

    // Process movement
    if (entity->is_moving)
    {
        co_await simulateEntityMovement(entity);
    }

    // Update database
    co_await simulateDatabaseUpdate(entity);

    co_return;
}

// Level 2: Zone entity processing (Task on main thread)
inline auto processZoneEntities(Zone* zone) -> Task<void>
{
    // Process each entity in the zone
    for (auto& entity : zone->entities)
    {
        if (entity)
        {
            co_await processEntityAI(entity.get());
        }
    }

    // Send network updates
    for (auto& entity : zone->entities)
    {
        if (entity)
        {
            co_await simulateNetworkPacket(entity.get());
        }
    }

    co_return;
}

// Level 1: Zone server tick (Task on main thread)
inline auto zoneServerTick(Zone* zone) -> Task<void>
{
    const auto start = std::chrono::high_resolution_clock::now();

    // Zone initialization work (like CZone::ZoneServer)
    std::this_thread::sleep_for(std::chrono::milliseconds(1));

    // Process all entities in the zone
    co_await processZoneEntities(zone);

    // Zone cleanup and state updates
    std::this_thread::sleep_for(std::chrono::milliseconds(1));

    const auto end      = std::chrono::high_resolution_clock::now();
    const auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    coro_log("Zone server tick duration: " + std::to_string(duration.count()) + "ms");

    co_return;
}

//
// Realistic Load Generation
//

// Create a realistic zone with entities
inline auto createRealisticZone(size_t zone_id, size_t num_entities) -> std::unique_ptr<Zone>
{
    auto zone = std::make_unique<Zone>(zone_id);

    for (size_t i = 0U; i < num_entities; ++i)
    {
        zone->addEntity(i + 1U);
    }

    return zone;
}

} // namespace LandSandBoatSimulation
