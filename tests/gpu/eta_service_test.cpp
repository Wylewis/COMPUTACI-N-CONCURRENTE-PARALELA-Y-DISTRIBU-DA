#include <chrono>
#include <cstdio>
#include <future>
#include <iostream>
#include <map>
#include <string>
#include <thread>
#include <vector>

#include "eta_service.h"
#include "gpu/travel_table_graph.h"
#include "restaurants.h"

using namespace std::chrono_literals;

namespace {

int failures = 0;

void check(bool ok, const std::string& what) {
    if (!ok) {
        ++failures;
        std::cerr << "FALLO: " << what << "\n";
    }
}

Config lineCity(int couriers, int bag) {
    Config cfg;
    cfg.nodes = {{"A", 0.0, 0.0}, {"B", 0.0, 0.0089932}, {"C", 0.0, 0.0134898}};
    cfg.streets = {{"ab", "A", "B", false}, {"bc", "B", "C", false}};
    cfg.restaurants = {{"r0", "Uno", "B", 1, 0, 0}};
    cfg.fleet = {couriers, bag, 36.0, "A"};
    return cfg;
}

struct Rig {
    Config cfg;
    Graph graph;
    SimClock clock{1000.0};
    StopSignal stop;
    EventLogger logger;
    Restaurants restaurants;
    Fleet fleet;
    TravelTable table;
    GpuWorker gpu;
    std::unique_ptr<EtaBatchGpu> device;
    std::unique_ptr<EtaService> service;

    Rig(const std::string& path, int couriers, int bag)
        : cfg(lineCity(couriers, bag)), graph(cfg), logger(path, Fleet::courierIds(couriers)),
          restaurants(cfg.restaurants), fleet(cfg.fleet, graph, restaurants, logger, clock, stop) {
        const CsrGraph csr = csrFromGraph(graph, cfg.fleet.speedKmh);
        table = TravelTable(csr.n, travelTableCpu(csr));
        device = gpu.submit([this] { return std::make_unique<EtaBatchGpu>(table); }).get();
        service = std::make_unique<EtaService>(fleet, graph, clock, gpu, *device);
        clock.start();
    }
    ~Rig() {
        service->stop();
        gpu.submit([this] { device.reset(); }).get();
        stop.request();
        logger.close();
    }
};

}

int main() {
    const std::string path = "eta_service_test.tmp.log";
    {
        Rig r(path, 6, 2);
        const int restaurant = static_cast<int>(r.graph.nodeIndex("B"));
        const int customer = static_cast<int>(r.graph.nodeIndex("C"));
        std::vector<std::thread> clients;
        std::vector<EtaService::Reply> replies(40);
        for (int t = 0; t < 8; ++t)
            clients.emplace_back([&, t] {
                for (int k = 0; k < 5; ++k) replies[static_cast<std::size_t>(t * 5 + k)] = r.service->request(restaurant, customer, 0).get();
            });
        for (auto& c : clients) c.join();
        const int latest = static_cast<int>(r.clock.nowMs()) + 270'000;
        bool sane = true;
        for (const EtaService::Reply& reply : replies) {
            if (!reply.found) continue;
            sane = sane && reply.courierId.size() == 2 && reply.courierId[0] == 'c' && reply.etaMs >= 270'000 && reply.etaMs <= latest;
        }
        check(sane, "cada respuesta trae una moto de la flota y una hora de entrega coherente con la ruta A-B-C");
        check(r.service->requests() == 40 && r.service->rounds() >= 1 && r.service->rounds() <= 40,
              "8 hilos, 40 pedidos: todos atendidos en rondas");
    }
    {
        Rig r(path, 6, 2);
        const int restaurant = static_cast<int>(r.graph.nodeIndex("B"));
        const int customer = static_cast<int>(r.graph.nodeIndex("C"));
        std::promise<void> gate, started;
        std::shared_future<void> open = gate.get_future().share();
        auto busy = r.gpu.submit([open, &started] {
            started.set_value();
            open.wait();
        });
        started.get_future().wait();
        std::vector<std::future<EtaService::Reply>> pending;
        pending.push_back(r.service->request(restaurant, customer, 0));
        while (r.gpu.pending() != 1) std::this_thread::sleep_for(1ms);
        for (int i = 0; i < 19; ++i) pending.push_back(r.service->request(restaurant, customer, 0));
        gate.set_value();
        busy.get();
        std::map<std::string, int> perCourier;
        int found = 0, retried = 0;
        for (std::size_t i = 1; i < pending.size(); ++i) {
            const EtaService::Reply reply = pending[i].get();
            if (reply.retry) ++retried;
            if (!reply.found) continue;
            ++found;
            ++perCourier[reply.courierId];
        }
        const bool first = pending[0].get().found;
        bool withinBag = true;
        for (const auto& entry : perCourier) withinBag = withinBag && entry.second <= 2;
        check(first && r.service->rounds() == 2 && r.service->largestRound() == 19,
              "los 19 pedidos que llegan mientras la GPU esta ocupada salen juntos en una sola ronda");
        check(found == 12 && perCourier.size() == 6 && withinBag && retried == 7 && r.service->conflicts() == 7,
              "dentro de la ronda se prometen 12 lugares, 2 por moto, y los 7 que chocan vuelven a pedir");
    }
    {
        Rig r(path, 2, 1);
        r.service->stop();
        const EtaService::Reply late = r.service->request(0, 1, 0).get();
        check(!late.found && r.service->requests() == 0, "despues de stop la respuesta llega enseguida y sin moto");
    }
    std::remove(path.c_str());

    if (failures == 0) {
        std::cout << "eta_service_test: todas las pruebas pasaron\n";
        return 0;
    }
    std::cerr << "eta_service_test: " << failures << " prueba(s) fallaron\n";
    return 1;
}
