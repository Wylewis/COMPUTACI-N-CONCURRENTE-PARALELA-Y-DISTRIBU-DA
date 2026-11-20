#include <chrono>
#include <memory>
#include <iostream>
#include <string>
#include <vector>

#include "bench/bench.h"
#include "config.h"
#include "dispatcher_thread.h"
#include "distance_table_parallel.h"
#include "eta_service.h"
#include "event_logger.h"
#include "fleet.h"
#include "generator_thread.h"
#include "gpu/eta_batch.h"
#include "gpu/gpu_backend.h"
#include "gpu/gpu_kernel_info.h"
#include "gpu/gpu_worker.h"
#include "gpu/travel_table.h"
#include "gpu/travel_table_graph.h"
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
    bool gpuBackend = false;
    bool gpuInfo = false;
    std::string estimator;
    bool bench = false;
    BenchOptions benchOptions;
};

void printUsage(std::ostream& out) {
    out << "Uso: delivery_sim <config.json> [--log <ruta>] [--no-window | --headless] [--backend cpu|gpu]\n"
           "                    [--estimator serial|async|pool|table|gpu]\n"
           "       delivery_sim <config.json> --gpu-info\n"
           "       delivery_sim <config.json> --headless --bench <resultados.csv> [--bench-once] [--bench-reps <n>]\n"
           "                    [--bench-apps <a,b,...>] [--bench-sweep] [--bench-threads <n>] [--bench-max-items <n>]\n"
           "                    [--bench-cold]\n";
}

std::vector<std::string> splitCommas(const std::string& text) {
    std::vector<std::string> out;
    std::string current;
    for (const char ch : text) {
        if (ch == ',') {
            if (!current.empty()) out.push_back(current);
            current.clear();
        } else {
            current += ch;
        }
    }
    if (!current.empty()) out.push_back(current);
    return out;
}

bool parsePositive(const std::string& text, unsigned long long& value) {
    if (text.empty() || text.find_first_not_of("0123456789") != std::string::npos) return false;
    try {
        value = std::stoull(text);
    } catch (const std::exception&) {
        return false;
    }
    return value > 0;
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
        } else if (arg == "--no-window" || arg == "--headless") {
            opts.window = false;
        } else if (arg == "--estimator") {
            if (i + 1 >= argc) {
                error = "--estimator requiere serial, async, pool, table o gpu";
                return false;
            }
            opts.estimator = argv[++i];
        } else if (arg == "--gpu-info") {
            opts.gpuInfo = true;
        } else if (arg == "--backend") {
            if (i + 1 >= argc || (std::string(argv[i + 1]) != "cpu" && std::string(argv[i + 1]) != "gpu")) {
                error = "--backend requiere cpu o gpu";
                return false;
            }
            opts.gpuBackend = std::string(argv[++i]) == "gpu";
        } else if (arg == "--bench") {
            if (i + 1 >= argc) {
                error = "--bench requiere la ruta del CSV";
                return false;
            }
            opts.bench = true;
            opts.benchOptions.csvPath = argv[++i];
        } else if (arg == "--bench-once") {
            opts.benchOptions.once = true;
        } else if (arg == "--bench-sweep") {
            opts.benchOptions.sweep = true;
        } else if (arg == "--bench-cold") {
            opts.benchOptions.wake = false;
        } else if (arg == "--bench-apps") {
            if (i + 1 >= argc) {
                error = "--bench-apps requiere una lista separada por comas";
                return false;
            }
            opts.benchOptions.apps = splitCommas(argv[++i]);
        } else if (arg == "--bench-reps" || arg == "--bench-threads" || arg == "--bench-max-items") {
            unsigned long long value = 0;
            if (i + 1 >= argc || !parsePositive(argv[i + 1], value)) {
                error = arg + " requiere un entero positivo";
                return false;
            }
            ++i;
            if (arg == "--bench-reps") opts.benchOptions.reps = static_cast<int>(value);
            if (arg == "--bench-threads") opts.benchOptions.threads = static_cast<unsigned>(value);
            if (arg == "--bench-max-items") opts.benchOptions.maxItems = static_cast<std::size_t>(value);
        } else {
            error = "argumento desconocido: " + arg;
            return false;
        }
    }
    return true;
}

}

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
        if (!opts.estimator.empty()) config.dispatch.estimator = opts.estimator;
        if (opts.gpuBackend && opts.estimator.empty()) config.dispatch.estimator = "gpu";
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

        const GpuDeviceInfo gpu = gpuDeviceInfo();
        std::cerr << "delivery_sim: backend de GPU: " << gpu.backend << " (" << gpu.name << ")\n";
        GpuWorker gpuWorker;
        if (!gpuWorker.submit([] { return gpuSelfTest(256); }).get()) {
            std::cerr << "delivery_sim: el kernel de prueba no dio el resultado esperado\n";
            return 1;
        }
        if (opts.gpuInfo) {
            std::cout << "dispositivo: " << gpu.name << "; multiprocesadores: " << gpu.multiprocessors
                      << "; hilos por bloque: " << gpu.maxThreadsPerBlock << "; memoria global: "
                      << gpu.globalMemBytes / (1024 * 1024) << " MiB; memoria compartida por bloque: "
                      << gpu.sharedMemPerBlock / 1024 << " KiB\n";
            std::cout << "aplicacion,kernel,hilos_por_bloque,bloques_por_multiprocesador,registros_por_hilo,"
                         "memoria_compartida_bytes,ocupacion_pct,limitante\n";
            for (const GpuKernelInfo& k : gpuWorker.submit([] { return gpuKernelInfos(); }).get()) {
                std::cout << k.application << ',' << k.kernel << ',' << k.threadsPerBlock << ',';
                if (k.measured)
                    std::cout << k.blocksPerMultiprocessor << ',' << k.registersPerThread << ',' << k.sharedBytesPerBlock
                              << ',' << k.occupancyPercent << ',';
                else
                    std::cout << "-,-,-,-,";
                std::cout << k.limiter << '\n';
            }
            return 0;
        }
        if (opts.bench) return runBenchmarks(opts.benchOptions, config, graph, gpuWorker, gpu.realGpu);
        if ((opts.gpuBackend || config.dispatch.estimator == "gpu") && !gpu.realGpu) {
            std::cerr << "delivery_sim: el backend y el estimador gpu necesitan una GPU real; este binario usa la "
                         "emulacion (compilar con -DUSE_CUDA=ON)\n";
            return 1;
        }

        ThreadPool estimators(recommendedPoolSize());
        std::cerr << "delivery_sim: pool de estimacion de " << estimators.size() << " hilos (hardware: "
                  << hardwareThreads() << ", rango " << kPoolMinimum << "-" << kPoolMaximum << ")\n";

        const CsrGraph streets = csrFromGraph(graph, config.fleet.speedKmh);
        const auto tableStart = std::chrono::steady_clock::now();
        const TravelTable travel(streets.n, opts.gpuBackend
                                                ? gpuWorker.submit([&streets] { return travelTableGpu(streets); }).get()
                                                : travelTableCpuParallel(streets, estimators));
        const auto tableMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                                 std::chrono::steady_clock::now() - tableStart).count();
        std::cerr << "delivery_sim: tabla de tiempos de " << travel.size() << " x " << travel.size() << " en "
                  << tableMs << " ms (" << (opts.gpuBackend ? "GPU, min-plus en mosaicos" : "CPU, Dijkstra multihilo")
                  << "), " << travel.unreachablePairs() << " pares inalcanzables\n";
        std::unique_ptr<EtaBatchGpu> etaDevice;
        if (config.dispatch.estimator == "gpu") {
            etaDevice = gpuWorker.submit([&travel] { return std::make_unique<EtaBatchGpu>(travel); }).get();
            std::cerr << "delivery_sim: tabla de tiempos residente en la GPU, " << etaDevice->tableBytes() / (1024 * 1024)
                      << " MiB subidos en " << etaDevice->tableUploadMs() << " ms\n";
        }
        std::cerr << "delivery_sim: estimador " << config.dispatch.estimator << "\n";

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
        book.onSubmitted([&fleet] { fleet.notifyChange(); });
        std::unique_ptr<EtaService> etaService;
        if (etaDevice) etaService = std::make_unique<EtaService>(fleet, graph, clock, gpuWorker, *etaDevice);
        std::vector<std::unique_ptr<DispatcherThread>> dispatchers;
        for (const RestaurantConfig& r : config.restaurants) {
            dispatchers.push_back(std::make_unique<DispatcherThread>(r, book, fleet, graph, config.fleet.speedKmh,
                                                                     config.dispatch, estimators, logger, clock, stop));
            dispatchers.back()->useTravelTable(&travel, etaService.get());
        }

        clock.start();
        fleet.start();
        for (auto& d : dispatchers) d->start();
        generatorThread.start();
        std::cout << kStartLine << std::endl;

        const std::int64_t durationMs = config.simulation.durationS * 1000;
        bool windowClosed = false;
#ifdef HAVE_OPENCV
        if (opts.window) {
            MapWindow window(config, graph);
            while (!stop.requested() && (durationMs == 0 || clock.nowMs() < durationMs)) {
                cv::Mat frame = window.frame();
                drawFleet(frame, window.projection(), graph, fleet.views(), clock.nowMs());
                PanelStats st;
                st.created = generatorThread.created();
                st.rejected = generatorThread.rejected();
                for (const auto& d : dispatchers) st.assigned += d->assigned(), st.rejected += d->rejected();
                const CountersSnapshot live = fleet.counters().snapshot();
                st.pickedUp = live.pickedUp, st.delivered = live.delivered, st.breakdowns = live.breakdowns;
                st.pending = st.created - st.delivered - st.rejected;
                drawCountersPanel(frame, st);
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

        generatorThread.join();
        logger.publish(Event::simulationStopping(clock.nowMs()));
        book.close();
        for (auto& d : dispatchers) d->join();
        if (etaService) {
            etaService->stop();
            std::cerr << "delivery_sim: rondas de estimacion en GPU: " << etaService->rounds() << ", pedidos estimados: "
                      << etaService->requests() << ", ronda mas grande: " << etaService->largestRound()
                      << " pedidos, choques dentro de una ronda: " << etaService->conflicts() << "\n";
        }
        if (etaDevice) gpuWorker.submit([&etaDevice] { etaDevice.reset(); }).get();
        fleet.join();
        const std::vector<Order> unfinished = book.drain();
        std::size_t inBags = 0;
        for (const CourierSnapshot& c : fleet.snapshots(clock.nowMs())) inBags += static_cast<std::size_t>(c.bagCount);
        logger.close();

        std::int64_t assigned = 0, rejectedNoCourier = 0, retries = 0, late = 0;
        for (const auto& d : dispatchers)
            assigned += d->assigned(), rejectedNoCourier += d->rejected(), retries += d->retries(), late += d->lateQuotes();
        std::cerr << "delivery_sim: parada por "
                  << (bySignal ? "senal" : windowClosed ? "ventana cerrada" : "tiempo cumplido") << " a los "
                  << clock.nowMs() << " ms simulados; " << generatorThread.batches() << " rafagas, "
                  << generatorThread.created() << " pedidos creados, " << generatorThread.rejected()
                  << " rechazados al crear, " << assigned << " asignados (" << retries << " reintentos, " << late
                  << " estimaciones tardias), "
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
