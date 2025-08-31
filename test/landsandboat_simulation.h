#pragma once

#include <atomic>
#include <chrono>
#include <random>
#include <vector>
#include <memory>

namespace LandSandBoatSimulation
{

//
// Realistic LandSandBoat Data Structures
//

// Position structure (3x floats) as used in LandSandBoat
struct position_t
{
    float x, y, z;
    
    position_t() : x(0.0f), y(0.0f), z(0.0f) {}
    position_t(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}
    
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
        return std::sqrt(dx*dx + dy*dy + dz*dz);
    }
};

// Entity data structure (simplified from LandSandBoat)
struct Entity
{
    uint32_t id;
    position_t position;
    position_t target_position;
    std::vector<position_t> path;
    bool is_moving;
    bool needs_pathfinding;
    
    Entity(uint32_t entity_id) 
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
    uint32_t zone_id;
    std::vector<std::unique_ptr<Entity>> entities;
    std::atomic<int> active_entities{0};
    
    Zone(uint32_t id) : zone_id(id) {}
    
    void addEntity(uint32_t entity_id)
    {
        entities.emplace_back(std::make_unique<Entity>(entity_id));
        active_entities.fetch_add(1);
    }
    
    Entity* getEntity(uint32_t entity_id)
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
inline auto simulatePathfinding(const position_t& start, const position_t& end) -> std::vector<position_t>
{
    // Simulate actual pathfinding work (5ms as mentioned in the user's requirements)
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    
    // Generate a realistic path with waypoints
    std::vector<position_t> path;
    path.push_back(start);
    
    // Add some intermediate waypoints (simulating navmesh pathfinding)
    float distance = start.distance(end);
    int waypoints = static_cast<int>(distance / 10.0f); // Waypoint every 10 units
    
    for (int i = 1; i < waypoints && i < 5; ++i) // Max 5 waypoints
    {
        float t = static_cast<float>(i) / static_cast<float>(waypoints);
        position_t waypoint(
            start.x + (end.x - start.x) * t,
            start.y + (end.y - start.y) * t,
            start.z + (end.z - start.z) * t
        );
        path.push_back(waypoint);
    }
    
    path.push_back(end);
    return path;
}

// Simulate AI decision making
inline auto simulateAIDecision(Entity* entity) -> bool
{
    // Simulate AI processing time (1-3ms)
    std::this_thread::sleep_for(std::chrono::milliseconds(1 + (entity->id % 3)));
    
    // 30% chance entity needs to move
    return (entity->id % 10) < 3;
}

// Simulate entity movement
inline auto simulateEntityMovement(Entity* entity) -> void
{
    // Simulate movement processing (2-5ms)
    std::this_thread::sleep_for(std::chrono::milliseconds(2 + (entity->id % 4)));
    
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
}

// Simulate database operations
inline auto simulateDatabaseUpdate(const Entity* entity) -> void
{
    // Simulate database write (1-2ms)
    std::this_thread::sleep_for(std::chrono::milliseconds(1 + (entity->id % 2)));
}

// Simulate network packet sending
inline auto simulateNetworkPacket(const Entity* entity) -> void
{
    // Simulate network operations (0.5-1ms)
    std::this_thread::sleep_for(std::chrono::microseconds(500 + (entity->id % 500)));
}

//
// Realistic Coroutine Chain Simulation
//

// Level 4: AsyncTask for pathfinding (runs on worker thread)
template<typename AsyncTaskType>
inline auto asyncPathfinding(const position_t& start, const position_t& end) -> AsyncTaskType
{
    // This simulates the actual pathfinding call in LandSandBoat
    // This runs on worker thread and takes ~5ms
    auto path = simulatePathfinding(start, end);
    co_return path;
}

// Level 3: Entity AI processing (Task on main thread)
template<typename TaskType, typename AsyncTaskType>
inline auto processEntityAI(Entity* entity) -> TaskType
{
    // Simulate AI decision making
    bool needs_movement = simulateAIDecision(entity);
    
    if (needs_movement && !entity->is_moving)
    {
        // Generate random target position
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_real_distribution<float> dist(-100.0f, 100.0f);
        
        position_t target(dist(gen), dist(gen), dist(gen));
        entity->target_position = target;
        entity->needs_pathfinding = true;
        
        // This is the key suspension point - pathfinding runs on worker thread
        entity->path = co_await asyncPathfinding<AsyncTaskType>(entity->position, target);
        
        entity->is_moving = true;
        entity->needs_pathfinding = false;
    }
    
    // Process movement
    if (entity->is_moving)
    {
        simulateEntityMovement(entity);
    }
    
    // Update database
    simulateDatabaseUpdate(entity);
    
    co_return;
}

// Level 2: Zone entity processing (Task on main thread)
template<typename TaskType, typename AsyncTaskType>
inline auto processZoneEntities(Zone* zone) -> TaskType
{
    // Process each entity in the zone
    for (auto& entity : zone->entities)
    {
        if (entity)
        {
            co_await processEntityAI<TaskType, AsyncTaskType>(entity.get());
        }
    }
    
    // Send network updates
    for (auto& entity : zone->entities)
    {
        if (entity)
        {
            simulateNetworkPacket(entity.get());
        }
    }
    
    co_return;
}

// Level 1: Zone server tick (Task on main thread)
template<typename TaskType, typename AsyncTaskType>
inline auto zoneServerTick(Zone* zone) -> TaskType
{
    // Zone initialization work (like CZone::ZoneServer)
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    
    // Process all entities in the zone
    co_await processZoneEntities<TaskType, AsyncTaskType>(zone);
    
    // Zone cleanup and state updates
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    
    co_return;
}

//
// Realistic Load Generation
//

// Create a realistic zone with entities
inline auto createRealisticZone(uint32_t zone_id, uint32_t num_entities) -> std::unique_ptr<Zone>
{
    auto zone = std::make_unique<Zone>(zone_id);
    
    for (uint32_t i = 0; i < num_entities; ++i)
    {
        zone->addEntity(i + 1);
    }
    
    return zone;
}

//
// Performance Measurement Utilities
//

struct PerformanceMetrics
{
    std::atomic<int> zone_ticks{0};
    std::atomic<int> entity_ai_calls{0};
    std::atomic<int> pathfinding_calls{0};
    std::atomic<int> movement_updates{0};
    std::atomic<int> database_updates{0};
    std::atomic<int> network_packets{0};
    
    void reset()
    {
        zone_ticks.store(0);
        entity_ai_calls.store(0);
        pathfinding_calls.store(0);
        movement_updates.store(0);
        database_updates.store(0);
        network_packets.store(0);
    }
};

} // namespace LandSandBoatSimulation
