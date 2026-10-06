#include "config.h"

#include <fstream>
#include <string>
#include <unordered_set>

#include "nlohmann/json.hpp"

using json = nlohmann::json;

namespace {

std::string join(const std::string& path, const std::string& key) {
    return path.empty() ? key : path + "." + key;
}

// Devuelve el subobjeto `key` de `obj`, que debe existir y ser un objeto JSON.
const json& section(const json& obj, const std::string& path, const char* key) {
    const std::string full = join(path, key);
    auto it = obj.find(key);
    if (it == obj.end()) {
        throw ConfigError("falta la propiedad obligatoria '" + full + "'");
    }
    if (!it->is_object()) {
        throw ConfigError("la propiedad '" + full + "' debe ser un objeto");
    }
    return *it;
}

// Devuelve el array `key` de `obj`, que debe existir y ser un array JSON.
const json& array(const json& obj, const std::string& path, const char* key) {
    const std::string full = join(path, key);
    auto it = obj.find(key);
    if (it == obj.end()) {
        throw ConfigError("falta la propiedad obligatoria '" + full + "'");
    }
    if (!it->is_array()) {
        throw ConfigError("la propiedad '" + full + "' debe ser un array");
    }
    return *it;
}

// Lee la propiedad `key` de `obj` convertida al tipo T, con un mensaje claro
// si falta o si su tipo no corresponde.
template <class T>
T field(const json& obj, const std::string& path, const char* key) {
    const std::string full = join(path, key);
    auto it = obj.find(key);
    if (it == obj.end()) {
        throw ConfigError("falta la propiedad obligatoria '" + full + "'");
    }
    try {
        return it->get<T>();
    } catch (const json::exception& e) {
        throw ConfigError("la propiedad '" + full + "' tiene un valor invalido: " + e.what());
    }
}

std::string indexed(const char* key, std::size_t i) {
    return std::string(key) + "[" + std::to_string(i) + "]";
}

void requireObject(const json& item, const std::string& path) {
    if (!item.is_object()) {
        throw ConfigError("el elemento '" + path + "' debe ser un objeto");
    }
}

std::string directoryOf(const std::string& path) {
    const auto slash = path.find_last_of('/');
    if (slash == std::string::npos) return ".";
    if (slash == 0) return "/";
    return path.substr(0, slash);
}

}  // namespace

std::string Config::imagePath() const {
    if (!map.image.empty() && map.image.front() == '/') return map.image;
    return configDir + "/" + map.image;
}

Config loadConfig(const std::string& path) {
    std::ifstream in(path);
    if (!in) {
        throw ConfigError("no se puede leer el archivo de configuracion: " + path);
    }

    json root;
    try {
        root = json::parse(in);
    } catch (const json::parse_error& e) {
        throw ConfigError("JSON mal formado en " + path + ": " + e.what());
    }
    if (!root.is_object()) {
        throw ConfigError("el archivo de configuracion debe contener un objeto JSON");
    }

    Config cfg;
    cfg.configDir = directoryOf(path);

    {
        const json& map = section(root, "", "map");
        cfg.map.image = field<std::string>(map, "map", "image");
        cfg.map.attribution = field<std::string>(map, "map", "attribution");
        const json& b = section(map, "map", "bounds");
        cfg.map.bounds.north = field<double>(b, "map.bounds", "north");
        cfg.map.bounds.south = field<double>(b, "map.bounds", "south");
        cfg.map.bounds.west = field<double>(b, "map.bounds", "west");
        cfg.map.bounds.east = field<double>(b, "map.bounds", "east");
    }

    {
        const json& nodes = array(root, "", "nodes");
        for (std::size_t i = 0; i < nodes.size(); ++i) {
            const std::string p = indexed("nodes", i);
            requireObject(nodes[i], p);
            NodeConfig n;
            n.id = field<std::string>(nodes[i], p, "id");
            n.lat = field<double>(nodes[i], p, "lat");
            n.lon = field<double>(nodes[i], p, "lon");
            cfg.nodes.push_back(std::move(n));
        }
    }

    {
        const json& streets = array(root, "", "streets");
        for (std::size_t i = 0; i < streets.size(); ++i) {
            const std::string p = indexed("streets", i);
            requireObject(streets[i], p);
            StreetConfig s;
            s.id = field<std::string>(streets[i], p, "id");
            s.from = field<std::string>(streets[i], p, "from");
            s.to = field<std::string>(streets[i], p, "to");
            s.oneWay = field<bool>(streets[i], p, "oneWay");
            cfg.streets.push_back(std::move(s));
        }
    }

    {
        const json& restaurants = array(root, "", "restaurants");
        for (std::size_t i = 0; i < restaurants.size(); ++i) {
            const std::string p = indexed("restaurants", i);
            requireObject(restaurants[i], p);
            RestaurantConfig r;
            r.id = field<std::string>(restaurants[i], p, "id");
            r.name = field<std::string>(restaurants[i], p, "name");
            r.node = field<std::string>(restaurants[i], p, "node");
            r.pickupSlots = field<int>(restaurants[i], p, "pickupSlots");
            const json& prep = array(restaurants[i], p, "prepTimeMs");
            if (prep.size() != 2) {
                throw ConfigError("la propiedad '" + p + ".prepTimeMs' debe ser un array [min, max]");
            }
            try {
                r.prepMinMs = prep[0].get<std::int64_t>();
                r.prepMaxMs = prep[1].get<std::int64_t>();
            } catch (const json::exception& e) {
                throw ConfigError("la propiedad '" + p + ".prepTimeMs' tiene un valor invalido: " + e.what());
            }
            cfg.restaurants.push_back(std::move(r));
        }
    }

    {
        const json& fleet = section(root, "", "fleet");
        cfg.fleet.couriers = field<int>(fleet, "fleet", "couriers");
        cfg.fleet.bagCapacity = field<int>(fleet, "fleet", "bagCapacity");
        cfg.fleet.speedKmh = field<double>(fleet, "fleet", "speedKmh");
        cfg.fleet.startNode = field<std::string>(fleet, "fleet", "startNode");
    }

    {
        const json& orders = section(root, "", "orders");
        cfg.orders.meanIntervalMs = field<std::int64_t>(orders, "orders", "meanIntervalMs");
        cfg.orders.burstMax = field<int>(orders, "orders", "burstMax");
        cfg.orders.maxPending = field<int>(orders, "orders", "maxPending");
        cfg.orders.seed = field<std::uint64_t>(orders, "orders", "seed");
    }

    {
        const json& dispatch = section(root, "", "dispatch");
        cfg.dispatch.quoteTimeoutMs = field<std::int64_t>(dispatch, "dispatch", "quoteTimeoutMs");
        cfg.dispatch.acceptTimeoutMs = field<std::int64_t>(dispatch, "dispatch", "acceptTimeoutMs");
    }

    {
        const json& incidents = section(root, "", "incidents");
        cfg.incidents.breakdownProbability = field<double>(incidents, "incidents", "breakdownProbability");
    }

    {
        const json& simulation = section(root, "", "simulation");
        cfg.simulation.durationS = field<std::int64_t>(simulation, "simulation", "durationS");
        cfg.simulation.timeScale = field<double>(simulation, "simulation", "timeScale");
    }

    return cfg;
}

void validateConfig(const Config& cfg) {
    if (cfg.nodes.empty()) {
        throw ConfigError("'nodes' debe tener al menos un nodo");
    }

    std::unordered_set<std::string> nodeIds;
    for (const auto& n : cfg.nodes) nodeIds.insert(n.id);

    for (std::size_t i = 0; i < cfg.streets.size(); ++i) {
        const auto& s = cfg.streets[i];
        if (!nodeIds.count(s.from)) {
            throw ConfigError("la calle '" + s.id + "' (streets[" + std::to_string(i) +
                              "]) hace referencia al nodo inexistente '" + s.from + "'");
        }
        if (!nodeIds.count(s.to)) {
            throw ConfigError("la calle '" + s.id + "' (streets[" + std::to_string(i) +
                              "]) hace referencia al nodo inexistente '" + s.to + "'");
        }
    }

    for (std::size_t i = 0; i < cfg.restaurants.size(); ++i) {
        const auto& r = cfg.restaurants[i];
        if (!nodeIds.count(r.node)) {
            throw ConfigError("el restaurante '" + r.id + "' (restaurants[" + std::to_string(i) +
                              "]) hace referencia al nodo inexistente '" + r.node + "'");
        }
        if (r.pickupSlots < 1) {
            throw ConfigError("el restaurante '" + r.id + "' tiene pickupSlots = " +
                              std::to_string(r.pickupSlots) + "; debe ser un entero >= 1");
        }
    }

    if (!nodeIds.count(cfg.fleet.startNode)) {
        throw ConfigError("'fleet.startNode' hace referencia al nodo inexistente '" +
                          cfg.fleet.startNode + "'");
    }
    if (cfg.fleet.couriers < 1) {
        throw ConfigError("'fleet.couriers' debe ser un entero >= 1");
    }
    if (cfg.fleet.bagCapacity < 1) {
        throw ConfigError("'fleet.bagCapacity' debe ser un entero >= 1");
    }
    if (cfg.orders.burstMax < 1) {
        throw ConfigError("'orders.burstMax' debe ser un entero >= 1");
    }
    const double p = cfg.incidents.breakdownProbability;
    if (!(p >= 0.0 && p <= 1.0)) {
        throw ConfigError("'incidents.breakdownProbability' debe estar entre 0 y 1");
    }

    std::ifstream image(cfg.imagePath(), std::ios::binary);
    if (!image) {
        throw ConfigError("no se puede leer la imagen del mapa 'map.image': " + cfg.imagePath());
    }
}
