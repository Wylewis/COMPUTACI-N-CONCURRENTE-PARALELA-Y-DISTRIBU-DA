#pragma once

#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

// Error de configuracion: archivo ilegible, JSON mal formado, propiedad
// faltante o con tipo invalido. El mensaje esta pensado para una persona.
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
    std::string image;        // tal como aparece en el JSON, relativa al archivo de configuracion
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
    std::int64_t quoteTimeoutMs = 0;   // tiempo real
    std::int64_t acceptTimeoutMs = 0;  // tiempo simulado
};

struct IncidentsConfig {
    double breakdownProbability = 0.0;
};

struct SimulationConfig {
    std::int64_t durationS = 0;  // 0: correr hasta recibir una senal
    double timeScale = 1.0;      // segundos simulados por segundo real
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

    std::string configDir;  // directorio del archivo de configuracion

    // Ruta de la imagen del mapa resuelta respecto al archivo de configuracion.
    std::string imagePath() const;
};

// Lee y decodifica el archivo de configuracion. Lanza ConfigError si el archivo
// no se puede leer, el JSON esta mal formado, falta una propiedad obligatoria o
// una propiedad tiene un tipo invalido. No valida referencias entre entidades.
Config loadConfig(const std::string& path);

// Comprueba lo que la seccion 5 del enunciado exige de un archivo valido:
// al menos un nodo; calles, restaurantes y startNode que apuntan a nodos
// existentes; los rangos que declara la tabla (couriers, bagCapacity, burstMax
// y pickupSlots >= 1, breakdownProbability entre 0 y 1); y que la imagen del
// mapa se pueda leer. Lanza ConfigError con el motivo.
void validateConfig(const Config& cfg);
