#include <chrono>
#include <iostream>
#include <string>
#include <vector>

#include "config.h"
#include "distance_table_parallel.h"
#include "event_logger.h"
#include "generator_thread.h"
#include "graph.h"
#include "hardware.h"
#include "order_book.h"
#include "order_generator.h"
#include "sim_clock.h"
#include "stop_signal.h"
#include "thread_pool.h"

namespace {

struct Options {
    std::string configPath;
    std::string logPath = "events.log";
};

void printUsage(std::ostream& out) {
    out << "Uso: delivery_sim <config.json> [--log <ruta>]\n";
}

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

std::vector<std::string> courierIds(const Config& config) {
    std::vector<std::string> ids;
    for (int i = 0; i < config.fleet.couriers; ++i) ids.push_back("c" + std::to_string(i));
    return ids;
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

    try {
        const Graph graph(config);
        std::cerr << "delivery_sim: " << config.nodes.size() << " nodos, " << config.streets.size() << " calles, "
                  << config.restaurants.size() << " restaurantes, " << config.fleet.couriers << " repartidores; "
                  << graph.edgeCount() << " aristas; mapa " << config.imagePath() << "; log " << opts.logPath << "\n";

        std::vector<std::string> origins;
        for (const auto& r : config.restaurants) origins.push_back(r.node);
        const auto t0 = std::chrono::steady_clock::now();
        const DistanceTable distances = buildDistanceTableParallel(graph, origins);
        const auto buildUs = std::chrono::duration_cast<std::chrono::microseconds>(
                                 std::chrono::steady_clock::now() - t0).count();
        std::cerr << "delivery_sim: tabla de distancias de " << distances.originCount() << " restaurantes en "
                  << buildUs << " us con " << defaultTaskCount(origins.size()) << " tareas\n";

        ThreadPool estimators(recommendedPoolSize());
        std::cerr << "delivery_sim: pool de estimacion de " << estimators.size() << " hilos (hardware: "
                  << hardwareThreads() << ", rango " << kPoolMinimum << "-" << kPoolMaximum << ")\n";

        SimClock clock(config.simulation.timeScale);
        EventLogger logger(opts.logPath, courierIds(config));
        OrderBook book(config);
        OrderGenerator generator(config, graph);
        StopSignal stop;
        SignalWatcher watcher(stop);
        GeneratorThread generatorThread(generator, book, logger, clock, stop);

        clock.start();
        generatorThread.start();
        std::cout << kStartLine << std::endl;

        // Esperar el fin: por tiempo simulado cumplido o por senal, lo que ocurra primero.
        const std::int64_t durationMs = config.simulation.durationS * 1000;
        if (durationMs > 0) {
            stop.waitUntil(clock.realTimeFor(durationMs));
        } else {
            stop.wait();
        }
        const bool bySignal = watcher.signalsReceived() > 0;
        stop.request();

        // Apagado ordenado: nadie crea mas pedidos, se registra la parada,
        // el libro se cierra y lo que quedaba en espera es pending.
        generatorThread.join();
        logger.publish(Event::simulationStopping(clock.nowMs()));
        book.close();
        const std::vector<Order> unfinished = book.drain();
        logger.close();

        std::cerr << "delivery_sim: parada por " << (bySignal ? "senal" : "tiempo cumplido") << " a los "
                  << clock.nowMs() << " ms simulados; " << generatorThread.batches() << " rafagas, "
                  << generatorThread.created() << " pedidos creados, " << generatorThread.rejected()
                  << " rechazados, " << unfinished.size() << " en espera\n";
        std::cout << logger.summary().toJsonLine() << std::endl;
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "delivery_sim: error: " << e.what() << "\n";
        return 1;
    }
}
