#include <iostream>
#include <string>

#include <chrono>

#include "config.h"
#include "distance_table_parallel.h"
#include "graph.h"
#include "hardware.h"
#include "thread_pool.h"

namespace {

struct Options {
    std::string configPath;
    std::string logPath = "events.log";
};

void printUsage(std::ostream& out) {
    out << "Uso: delivery_sim <config.json> [--log <ruta>]\n";
}

// Interpreta la linea de comandos: la ruta del config es obligatoria y
// `--log <ruta>` es el unico flag opcional.
bool parseArgs(int argc, char** argv, Options& opts, std::string& error) {
    if (argc < 2) {
        error = "falta la ruta del archivo de configuracion";
        return false;
    }
    opts.configPath = argv[1];
    for (int i = 2; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--log") {
            if (i + 1 >= argc) {
                error = "--log requiere una ruta";
                return false;
            }
            opts.logPath = argv[++i];
        } else {
            error = "argumento desconocido: " + arg;
            return false;
        }
    }
    return true;
}

}  // namespace

int main(int argc, char** argv) {
    Options opts;
    std::string error;
    if (!parseArgs(argc, argv, opts, error)) {
        std::cerr << "delivery_sim: " << error << "\n";
        printUsage(std::cerr);
        return 1;
    }

    Config config;
    try {
        config = loadConfig(opts.configPath);
        validateConfig(config);
    } catch (const ConfigError& e) {
        std::cerr << "delivery_sim: configuracion invalida: " << e.what() << "\n";
        return 1;
    }

    const Graph graph(config);
    const auto reachableFromStart = graph.reachableFrom(config.fleet.startNode).size();

    std::cerr << "delivery_sim: configuracion cargada: " << config.nodes.size() << " nodos, "
              << config.streets.size() << " calles, " << config.restaurants.size() << " restaurantes, "
              << config.fleet.couriers << " repartidores; mapa " << config.imagePath()
              << "; log " << opts.logPath << "\n";
    std::cerr << "delivery_sim: red de calles: " << graph.edgeCount() << " aristas dirigidas, "
              << reachableFromStart << " de " << graph.nodeCount()
              << " nodos alcanzables desde " << config.fleet.startNode << "\n";

    // Tabla de distancias desde cada restaurante, construida en paralelo:
    // una tarea por grupo de restaurantes, tantas como nucleos disponibles.
    std::vector<std::string> origins;
    for (const auto& r : config.restaurants) origins.push_back(r.node);
    const auto t0 = std::chrono::steady_clock::now();
    const DistanceTable distances = buildDistanceTableParallel(graph, origins);
    const auto buildUs = std::chrono::duration_cast<std::chrono::microseconds>(
                             std::chrono::steady_clock::now() - t0).count();
    std::cerr << "delivery_sim: tabla de distancias: " << distances.originCount() << " restaurantes x "
              << distances.nodeCount() << " nodos en " << buildUs << " us con "
              << defaultTaskCount(origins.size()) << " tareas\n";

    // Pool de estimacion: un hilo por hilo de hardware, entre kPoolMinimum y
    // kPoolMaximum. Las estimaciones de un pedido se reparten aqui.
    ThreadPool estimators(recommendedPoolSize());
    std::cerr << "delivery_sim: pool de estimacion: " << estimators.size() << " hilos (hardware: "
              << hardwareThreads() << ", rango " << kPoolMinimum << "-" << kPoolMaximum << ")\n";
    std::cerr << "delivery_sim: la simulacion todavia no esta implementada\n";
    return 1;
}
