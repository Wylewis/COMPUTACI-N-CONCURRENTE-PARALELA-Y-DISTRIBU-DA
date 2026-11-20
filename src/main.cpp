#include <algorithm>
#include <chrono>
#include <cmath>
#include <memory>
#include <iostream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "bench/bench.h"
#include "config.h"
#include "demand_log.h"
#include "dispatcher_thread.h"
#include "distance_table_parallel.h"
#include "eta_service.h"
#include "event_logger.h"
#include "fleet.h"
#include "generator_thread.h"
#include "gpu/backlog_curve.h"
#include "gpu/coverage_field.h"
#include "gpu/delivery_histogram.h"
#include "gpu/delivery_kpis.h"
#include "gpu/demand_heatmap.h"
#include "gpu/demand_trend.h"
#include "gpu/eta_batch.h"
#include "gpu/gpu_backend.h"
#include "gpu/gpu_kernel_info.h"
#include "gpu/gpu_worker.h"
#include "gpu/restaurant_slots.h"
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

std::pair<float, float> mapSpot(const Bounds& b, double lat, double lon) {
    const double width = b.east - b.west, height = b.north - b.south;
    if (!(width > 0.0) || !(height > 0.0)) return {-1.0f, -1.0f};
    return {static_cast<float>((lon - b.west) / width), static_cast<float>((b.north - lat) / height)};
}

CoverageMap streetCoverageMap(const Config& config, const Graph& graph, int grid) {
    const Bounds& b = config.map.bounds;
    CoverageMap map;
    map.grid = grid;
    map.open.assign(static_cast<std::size_t>(grid) * static_cast<std::size_t>(grid), 0);
    const double msPerMetre = 3600.0 / std::max(1.0, config.fleet.speedKmh);
    const double midLat = (b.north + b.south) / 2.0, midLon = (b.east + b.west) / 2.0;
    map.stepXMs = std::max(1, static_cast<int>(std::lround(distanceM(midLat, b.west, midLat, b.east) / grid * msPerMetre)));
    map.stepYMs = std::max(1, static_cast<int>(std::lround(distanceM(b.south, midLon, b.north, midLon) / grid * msPerMetre)));
    const auto cellOf = [grid](float v) { return std::clamp(static_cast<int>(v * static_cast<float>(grid)), 0, grid - 1); };
    const auto open = [&](int row, int col) {
        map.open[static_cast<std::size_t>(row) * static_cast<std::size_t>(grid) + static_cast<std::size_t>(col)] = 1;
    };
    for (std::size_t i = 0; i < graph.nodeCount(); ++i) {
        const auto from = mapSpot(b, graph.nodeAt(i).lat, graph.nodeAt(i).lon);
        for (const Graph::Edge& e : graph.edgesAt(i)) {
            const auto to = mapSpot(b, graph.nodeAt(e.to).lat, graph.nodeAt(e.to).lon);
            const float span = std::max(std::fabs(to.first - from.first), std::fabs(to.second - from.second));
            const int pieces = 1 + static_cast<int>(span * static_cast<float>(grid) * 2.0f);
            int lastRow = cellOf(from.second), lastCol = cellOf(from.first);
            open(lastRow, lastCol);
            for (int k = 1; k <= pieces; ++k) {
                const float t = static_cast<float>(k) / static_cast<float>(pieces);
                const int row = cellOf(from.second + (to.second - from.second) * t);
                const int col = cellOf(from.first + (to.first - from.first) * t);
                if (row != lastRow && col != lastCol) open(lastRow, col);
                open(row, col);
                lastRow = row, lastCol = col;
            }
        }
    }
    return map;
}

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
                  << tableMs << " ms (" << (opts.gpuBackend ? "GPU, min-plus en tiles" : "CPU, Dijkstra multihilo")
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
        if (etaDevice) {
            etaService = std::make_unique<EtaService>(fleet, graph, clock, gpuWorker, *etaDevice, opts.gpuBackend);
            std::cerr << "delivery_sim: cada ronda se agrupa por restaurante en " << (opts.gpuBackend ? "la GPU" : "la CPU") << "\n";
        }
        std::vector<std::unique_ptr<DispatcherThread>> dispatchers;
        for (const RestaurantConfig& r : config.restaurants) {
            dispatchers.push_back(std::make_unique<DispatcherThread>(r, book, fleet, graph, config.fleet.speedKmh,
                                                                     config.dispatch, estimators, logger, clock, stop));
            dispatchers.back()->useTravelTable(&travel, etaService.get());
        }

        clock.start();
        fleet.start();
        for (auto& d : dispatchers) d->start();
        DemandLog demand;
        std::unordered_map<std::string, std::pair<float, float>> restaurantSpot;
        for (const RestaurantConfig& r : config.restaurants) {
            const Graph::Node& node = graph.nodeAt(graph.nodeIndex(r.node));
            restaurantSpot[r.id] = mapSpot(config.map.bounds, node.lat, node.lon);
        }
        generatorThread.onCreated([&demand, &restaurantSpot, &clock](const std::string& restaurantId) {
            const auto spot = restaurantSpot.find(restaurantId);
            if (spot != restaurantSpot.end()) demand.add(clock.nowMs(), spot->second.first, spot->second.second);
        });
        static constexpr int heatGrid = 64;
        const std::vector<float> heatWeights = gaussianWeights();
        const auto demandHeat = [&](std::int64_t fromMs) {
            const std::vector<DemandPoint> points = demand.since(fromMs);
            std::vector<float> xs, ys;
            xs.reserve(points.size()), ys.reserve(points.size());
            for (const DemandPoint& p : points) xs.push_back(p.x), ys.push_back(p.y);
            const std::vector<float> counts = binDemand(xs.data(), ys.data(), points.size(), heatGrid);
            return opts.gpuBackend ? gpuWorker.submit([&counts, &heatWeights] {
                                                  return smoothGpu(counts.data(), heatGrid, heatWeights.data());
                                              }).get()
                                   : smoothCpuParallel(counts.data(), heatGrid, heatWeights.data(), estimators);
        };
        const CoverageMap streetMap = streetCoverageMap(config, graph, heatGrid);
        const auto idleSpots = [&] {
            std::vector<float> spots;
            const std::int64_t now = clock.nowMs();
            for (const CourierView& v : fleet.views()) {
                if (v.state != CourierState::Idle) continue;
                GeoPoint at;
                if (v.trip) {
                    at = v.trip->positionAt(now);
                } else {
                    const Graph::Node& node = graph.nodeAt(graph.nodeIndex(v.node));
                    at = {node.lat, node.lon};
                }
                const auto spot = mapSpot(config.map.bounds, at.lat, at.lon);
                spots.push_back(spot.first), spots.push_back(spot.second);
            }
            return spots;
        };
        const auto coverageFrom = [&](const std::vector<float>& spots, int* sweeps) {
            std::vector<float> xs, ys;
            for (std::size_t i = 0; i + 1 < spots.size(); i += 2) xs.push_back(spots[i]), ys.push_back(spots[i + 1]);
            const std::vector<int> seed = coverageSeed(streetMap, xs.data(), ys.data(), xs.size());
            return opts.gpuBackend ? gpuWorker.submit([&streetMap, &seed, sweeps] {
                                                  return coverageGpu(streetMap, seed.data(), CoverKernel::Basic, nullptr, sweeps);
                                              }).get()
                                   : coverageCpu(streetMap, seed.data(), sweeps);
        };
        generatorThread.start();
        std::cout << kStartLine << std::endl;

        const std::int64_t durationMs = config.simulation.durationS * 1000;
        bool windowClosed = false;
#ifdef HAVE_OPENCV
        if (opts.window) {
            MapWindow window(config, graph);
            std::cerr << "delivery_sim: en la ventana, la tecla h muestra u oculta el mapa de calor de demanda y la tecla c "
                         "el campo de cobertura de las motos libres\n";
            bool showHeat = false, showCover = false;
            std::vector<float> heat;
            std::vector<int> cover;
            std::int64_t frames = 0;
            while (!stop.requested() && (durationMs == 0 || clock.nowMs() < durationMs)) {
                cv::Mat frame = window.frame();
                if (showCover) {
                    if (cover.empty() || frames % 15 == 0) cover = coverageFrom(idleSpots(), nullptr);
                    drawCoverage(frame, cover, heatGrid, kCoverFar);
                }
                if (showHeat) {
                    if (heat.empty() || frames % 15 == 0) heat = demandHeat(clock.nowMs() - 600'000);
                    drawHeatmap(frame, heat, heatGrid);
                }
                ++frames;
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
                if (window.lastKey() == 'h') {
                    showHeat = !showHeat;
                    heat.clear();
                }
                if (window.lastKey() == 'c') {
                    showCover = !showCover;
                    cover.clear();
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
        const std::vector<float> idleAtClose = idleSpots();
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
        if (demand.size() == 0) {
            std::cerr << "delivery_sim: sin pedidos, no hay mapa de calor de demanda\n";
        } else {
            const std::vector<float> wholeRun = demandHeat(0);
            const std::size_t hottest = hottestCell(wholeRun);
            std::cerr << "delivery_sim: mapa de calor de demanda, cuadricula de " << heatGrid << " x " << heatGrid << " ("
                      << (opts.gpuBackend ? "GPU" : "CPU") << "): " << demand.size()
                      << " pedidos; zona de mayor demanda en la fila " << hottest / static_cast<std::size_t>(heatGrid)
                      << ", columna " << hottest % static_cast<std::size_t>(heatGrid) << "\n";
        }
        if (idleAtClose.empty()) {
            std::cerr << "delivery_sim: sin motos libres al cierre, no hay campo de cobertura\n";
        } else {
            int sweeps = 0;
            const std::vector<int> closing = coverageFrom(idleAtClose, &sweeps);
            const CoverageWorst worst = worstCovered(streetMap, closing);
            std::cerr << "delivery_sim: campo de cobertura, cuadricula de " << heatGrid << " x " << heatGrid << " ("
                      << (opts.gpuBackend ? "GPU" : "CPU") << "): " << idleAtClose.size() / 2 << " motos libres al cierre, "
                      << sweeps << " pasadas; zona peor cubierta en la fila " << worst.cell / static_cast<std::size_t>(heatGrid)
                      << ", columna " << worst.cell % static_cast<std::size_t>(heatGrid) << ", a " << worst.ms / 1000
                      << " s de la moto libre mas cercana; " << worst.reached << " celdas con calle cubiertas y "
                      << worst.unreached << " sin alcanzar\n";
        }
        const std::vector<int> deliveryMs = logger.deliveryTimesMs();
        if (deliveryMs.empty()) {
            std::cerr << "delivery_sim: sin entregas, no hay histograma de tiempos\n";
        } else {
            const int bins = 180;
            const std::vector<unsigned> perMinute =
                opts.gpuBackend ? gpuWorker.submit([&deliveryMs] {
                                               return deliveryHistogramGpu(deliveryMs.data(), deliveryMs.size(), bins);
                                           }).get()
                                : deliveryHistogramCpuParallel(deliveryMs.data(), deliveryMs.size(), bins, estimators);
            std::size_t busiest = 0;
            for (std::size_t b = 1; b < perMinute.size(); ++b)
                if (perMinute[b] > perMinute[busiest]) busiest = b;
            std::cerr << "delivery_sim: tiempos de entrega, un tramo por minuto (" << (opts.gpuBackend ? "GPU" : "CPU") << "): "
                      << deliveryMs.size() << " entregas; mediana en el minuto " << histogramPercentileBin(perMinute, 0.5)
                      << ", p90 en el " << histogramPercentileBin(perMinute, 0.9) << ", p99 en el "
                      << histogramPercentileBin(perMinute, 0.99) << "; minuto mas cargado: " << busiest << " con "
                      << perMinute[busiest] << " entregas\n";
            const DeliveryKpis kpis = opts.gpuBackend ? gpuWorker.submit([&deliveryMs] {
                                                                     return deliveryKpisGpu(deliveryMs.data(), deliveryMs.size());
                                                                 }).get()
                                                      : deliveryKpisCpu(deliveryMs.data(), deliveryMs.size());
            const long long count = static_cast<long long>(deliveryMs.size());
            std::cerr << "delivery_sim: indicadores de entrega (" << (opts.gpuBackend ? "GPU" : "CPU") << "): promedio de "
                      << (kpis.sumMs + count / 2) / count << " ms, la mas lenta tardo " << kpis.maxMs << " ms y "
                      << kpis.late << " de " << count << " pasaron de " << kLateDeliveryMs / 60'000 << " minutos ("
                      << 100 * static_cast<long long>(kpis.late) / count << " %)\n";
        }
        const std::vector<int> backlogChanges = backlogDeltas(logger.createdTimesMs(), logger.closedTimesMs());
        if (backlogChanges.empty()) {
            std::cerr << "delivery_sim: sin pedidos, no hay curva de pedidos en espera\n";
        } else {
            const std::vector<int> waiting =
                opts.gpuBackend ? gpuWorker.submit([&backlogChanges] {
                                               return backlogCurveGpu(backlogChanges.data(), backlogChanges.size());
                                           }).get()
                                : backlogCurveCpu(backlogChanges.data(), backlogChanges.size());
            const BacklogPeak peak = backlogPeak(waiting);
            std::cerr << "delivery_sim: pedidos en espera, un tramo por segundo (" << (opts.gpuBackend ? "GPU" : "CPU") << "): "
                      << waiting.size() << " segundos; pico de " << peak.waiting << " pedidos en el minuto " << peak.slot / 60
                      << "; al cierre quedan " << waiting.back() << "\n";
        }
        const std::vector<int> orderRestaurant = logger.createdRestaurants();
        if (orderRestaurant.empty()) {
            std::cerr << "delivery_sim: sin pedidos, no hay tabla de pedidos por restaurante y franja\n";
        } else {
            const std::vector<std::string> restaurantNames = logger.restaurantNames();
            const std::vector<std::int64_t> createdAt = logger.createdTimesMs();
            std::vector<int> createdMs(createdAt.size());
            std::int64_t lastCreated = 0;
            for (std::size_t i = 0; i < createdAt.size(); ++i) {
                createdMs[i] = static_cast<int>(std::min<std::int64_t>(std::max<std::int64_t>(createdAt[i], 0), 2'000'000'000));
                lastCreated = std::max<std::int64_t>(lastCreated, createdMs[i]);
            }
            const int slotCount = static_cast<int>(lastCreated / kDemandSlotMs) + 1;
            const int restaurantCount = static_cast<int>(restaurantNames.size());
            const std::vector<unsigned> perSlot =
                opts.gpuBackend ? gpuWorker.submit([&orderRestaurant, &createdMs, restaurantCount, slotCount] {
                                               return restaurantSlotsGpu(orderRestaurant.data(), createdMs.data(),
                                                                         createdMs.size(), restaurantCount, slotCount);
                                           }).get()
                                : restaurantSlotsCpu(orderRestaurant.data(), createdMs.data(), createdMs.size(), restaurantCount,
                                                     slotCount);
            const BusiestSlot busiest = busiestSlot(perSlot, slotCount);
            std::cerr << "delivery_sim: pedidos por restaurante y franja de diez minutos (" << (opts.gpuBackend ? "GPU" : "CPU")
                      << "): " << restaurantCount << " restaurantes por " << slotCount << " franjas, " << createdMs.size()
                      << " pedidos; la celda mas cargada es " << restaurantNames[static_cast<std::size_t>(busiest.restaurant)]
                      << " entre el minuto " << busiest.slot * 10 << " y el " << busiest.slot * 10 + 10 << ", con "
                      << busiest.orders << " pedidos\n";
        }
        const std::vector<float> perMinute = ordersPerSlot(logger.createdTimesMs());
        if (perMinute.empty()) {
            std::cerr << "delivery_sim: sin pedidos, no hay tendencia de la demanda\n";
        } else {
            const std::vector<float> trendFilter = trendWeights();
            const std::vector<float> trend =
                opts.gpuBackend ? gpuWorker.submit([&perMinute, &trendFilter] {
                                               return trendGpu(perMinute.data(), perMinute.size(), trendFilter.data());
                                           }).get()
                                : trendCpu(perMinute.data(), perMinute.size(), trendFilter.data());
            const std::size_t sustained = trendPeak(trend);
            const std::size_t rawPeak = trendPeak(perMinute);
            std::cerr << "delivery_sim: tendencia de la demanda, un tramo por minuto (" << (opts.gpuBackend ? "GPU" : "CPU")
                      << "): " << perMinute.size() << " minutos; pico sostenido de " << std::lround(trend[sustained] * 10.0f) / 10.0
                      << " pedidos por minuto alrededor del minuto " << sustained << "; sin suavizar, el minuto mas cargado fue el "
                      << rawPeak << " con " << std::lround(perMinute[rawPeak]) << " pedidos\n";
        }
        std::cout << logger.summary().toJsonLine() << std::endl;
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "delivery_sim: error: " << e.what() << "\n";
        return 1;
    }
}
