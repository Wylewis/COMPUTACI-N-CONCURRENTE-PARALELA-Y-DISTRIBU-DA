#include <chrono>
#include <memory>
#include <iostream>
#include <string>
#include <vector>

#include "config.h"
#include "dispatcher_thread.h"
#include "distance_table_parallel.h"
#include "event_logger.h"
#include "fleet.h"
#include "generator_thread.h"
#include "graph.h"
#include "hardware.h"
#include "order_book.h"
#include "order_generator.h"
#include "restaurants.h"
#include "sim_clock.h"
#include "stop_signal.h"
#include "thread_pool.h"
#ifdef HAVE_OPENCV
#include "fleet_view.h"
#include "window.h"
#endif

namespace {

struct Options {
    std::string configPath;
    std::string logPath = "events.log";
    bool window = true;
};

void printUsage(std::ostream& out) {
    out << "Uso: delivery_sim <config.json> [--log <ruta>] [--no-window]\n";
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
        } else if (arg == "--no-window") {
            opts.window = false;
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
        EventLogger logger(opts.logPath, Fleet::courierIds(config.fleet.couriers));
        OrderBook book(config);
        OrderGenerator generator(config, graph);
        StopSignal stop;
        SignalWatcher watcher(stop);
        GeneratorThread generatorThread(generator, book, logger, clock, stop);
        Restaurants restaurants(config.restaurants);
        Fleet fleet(config.fleet, graph, restaurants, logger, clock, stop);
        fleet.enableBreakdowns(config.incidents, config.orders.seed, [&book](Order o) { book.putBack(std::move(o)); });
        std::vector<std::unique_ptr<DispatcherThread>> dispatchers;
        for (const RestaurantConfig& r : config.restaurants) {
            dispatchers.push_back(std::make_unique<DispatcherThread>(r, book, fleet, graph, config.fleet.speedKmh,
                                                                     config.dispatch.acceptTimeoutMs, logger, clock, stop));
        }

        clock.start();
        fleet.start();
        for (auto& d : dispatchers) d->start();
        generatorThread.start();
        std::cout << kStartLine << std::endl;

        // Esperar el fin: por tiempo simulado cumplido, por senal, o porque se
        // cerro la ventana, lo que ocurra primero. Con ventana, el hilo principal
        // dibuja un cuadro y espera unos milisegundos entre cuadros: esa es la
        // unica pausa periodica del programa, y solo regula la tasa de cuadros.
        const std::int64_t durationMs = config.simulation.durationS * 1000;
        bool windowClosed = false;
#ifdef HAVE_OPENCV
        if (opts.window) {
            MapWindow window(config, graph);
            while (!stop.requested() && (durationMs == 0 || clock.nowMs() < durationMs)) {
                cv::Mat frame = window.frame();
                drawFleet(frame, window.projection(), graph, fleet.views(), clock.nowMs());
                if (!window.show(frame, 33)) {
                    windowClosed = true;
                    break;
                }
            }
        } else
#endif
        if (durationMs > 0) {
            stop.waitUntil(clock.realTimeFor(durationMs));
        } else {
            stop.wait();
        }
        const bool bySignal = watcher.signalsReceived() > 0;
        stop.request();

        // Apagado ordenado: nadie crea mas pedidos, se registra la parada, el
        // libro se cierra (los despachadores devuelven lo que tenian en la mano
        // y terminan), las motos salen de filas y viajes, y lo que quedaba en
        // el libro o en las mochilas es pending.
        generatorThread.join();
        logger.publish(Event::simulationStopping(clock.nowMs()));
        book.close();
        for (auto& d : dispatchers) d->join();
        fleet.join();
        const std::vector<Order> unfinished = book.drain();
        std::size_t inBags = 0;
        for (const CourierSnapshot& c : fleet.snapshots(clock.nowMs())) inBags += static_cast<std::size_t>(c.bagCount);
        logger.close();

        std::int64_t assigned = 0, rejectedNoCourier = 0, retries = 0;
        for (const auto& d : dispatchers) assigned += d->assigned(), rejectedNoCourier += d->rejected(), retries += d->retries();
        std::cerr << "delivery_sim: parada por "
                  << (bySignal ? "senal" : windowClosed ? "ventana cerrada" : "tiempo cumplido") << " a los "
                  << clock.nowMs() << " ms simulados; " << generatorThread.batches() << " rafagas, "
                  << generatorThread.created() << " pedidos creados, " << generatorThread.rejected()
                  << " rechazados al crear, " << assigned << " asignados (" << retries << " reintentos), "
                  << rejectedNoCourier << " sin repartidor, " << fleet.delivered() << " entregados, "
                  << fleet.breakdowns() << " averias, " << fleet.rescues() << " rescates, " << unfinished.size()
                  << " en el libro y " << inBags << " en mochilas\n";
        std::cout << logger.summary().toJsonLine() << std::endl;
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "delivery_sim: error: " << e.what() << "\n";
        return 1;
    }
}
