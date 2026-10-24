#pragma once

#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

struct ConfigError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

struct Bounds {
    double north = 0.0;
    double south = 0.0;
    double west = 0.0;
    double east = 0.0;
};

struct MapConfig {
    std::string image;
    std::string attribution;
    Bounds bounds;
};

struct NodeConfig {
    std::string id;
    double lat = 0.0;
    double lon = 0.0;
};

struct StreetConfig {
    std::string id;
    std::string from;
    std::string to;
    bool oneWay = false;
};

struct RestaurantConfig {
    std::string id;
    std::string name;
    std::string node;
    int pickupSlots = 1;
    std::int64_t prepMinMs = 0;
    std::int64_t prepMaxMs = 0;
};

struct FleetConfig {
    int couriers = 1;
    int bagCapacity = 1;
    double speedKmh = 0.0;
    std::string startNode;
};

struct OrdersConfig {
    std::int64_t meanIntervalMs = 0;
    int burstMax = 1;
    int maxPending = 0;
    std::uint64_t seed = 0;
};

struct DispatchConfig {
    std::int64_t quoteTimeoutMs = 0;
    std::int64_t acceptTimeoutMs = 0;
    std::string estimator = "pool";
};

struct IncidentsConfig {
    double breakdownProbability = 0.0;
};

struct SimulationConfig {
    std::int64_t durationS = 0;
    double timeScale = 1.0;
};

struct Config {
    MapConfig map;
    std::vector<NodeConfig> nodes;
    std::vector<StreetConfig> streets;
    std::vector<RestaurantConfig> restaurants;
    FleetConfig fleet;
    OrdersConfig orders;
    DispatchConfig dispatch;
    IncidentsConfig incidents;
    SimulationConfig simulation;

    std::string configDir;

    std::string imagePath() const;
};

Config loadConfig(const std::string& path);

void validateConfig(const Config& cfg);
