#include <chrono>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include "dispatcher_thread.h"
#include "nlohmann/json.hpp"

using json = nlohmann::json;
using namespace std::chrono;
namespace {

int failures = 0;

void check(bool ok, const std::string& what) {
    if (!ok) {
        ++failures;
        std::cerr << "FALLO: " << what << "\n";
    }
}

Config lineCity(int couriers, std::int64_t acceptMs, std::int64_t agingMs) {
    Config cfg;
    cfg.nodes = {{"A", 0.0, 0.0}, {"B", 0.0, 0.0089932}, {"C", 0.0, 0.0134898}};
    cfg.streets = {{"ab", "A", "B", false}, {"bc", "B", "C", false}};
    cfg.restaurants = {{"r0", "Uno", "B", 1, 0, 0}, {"r1", "Dos", "C", 1, 0, 0}};
    cfg.fleet = {couriers, 1, 36.0, "A"};
    cfg.orders.maxPending = 10;
    cfg.orders.agingMs = agingMs;
    cfg.dispatch.acceptTimeoutMs = acceptMs;
    cfg.dispatch.quoteTimeoutMs = 200;
    return cfg;
}

struct Spec {
    std::string id, restaurant;
    std::int64_t createdAt = 0;
    bool urgent = false;
};

OrderBatch batch(const std::vector<Spec>& specs, EventLogger& logger) {
    OrderBatch b;
    for (const Spec& s : specs) {
        Order o;
        o.id = s.id;
        o.restaurantId = s.restaurant;
        o.deliveryNode = "C";
        o.createdAtMs = s.createdAt;
        o.urgent = s.urgent;
        logger.publish(Event::orderCreated(s.createdAt, s.id, s.restaurant, s.urgent));
        b.orders.push_back(std::move(o));
    }
    return b;
}

bool deliversAll(const Fleet& f, std::int64_t n) {
    for (int i = 0; i < 1000 && f.delivered() < n; ++i) std::this_thread::sleep_for(milliseconds(5));
    return f.delivered() == n;
}

bool assignsAtLeast(const DispatcherThread& d, std::int64_t n) {
    for (int i = 0; i < 1000 && d.assigned() < n; ++i) std::this_thread::sleep_for(milliseconds(5));
    return d.assigned() >= n;
}

std::vector<std::string> assignedOrder(const std::string& path) {
    std::vector<std::string> out;
    std::ifstream in(path);
    for (std::string line; std::getline(in, line);) {
        const json j = json::parse(line);
        if (j.contains("event") && j["event"] == "orderAssigned") out.push_back(j["order"]);
    }
    return out;
}

struct Rig {
    Config cfg;
    Graph graph;
    SimClock clock{1000.0};
    StopSignal stop;
    EventLogger logger;
    OrderBook book;
    ThreadPool estimators{4};
    Restaurants restaurants;
    Fleet fleet;
    std::vector<std::unique_ptr<DispatcherThread>> dispatchers;
    Rig(const std::string& path, int couriers, const std::vector<int>& which, std::int64_t acceptMs = 3'600'000,
        std::int64_t agingMs = 0)
        : cfg(lineCity(couriers, acceptMs, agingMs)), graph(cfg), logger(path, Fleet::courierIds(couriers)), book(cfg),
          restaurants(cfg.restaurants), fleet(cfg.fleet, graph, restaurants, logger, clock, stop) {
        book.onSubmitted([this] { fleet.notifyChange(); });
        for (int r : which)
            dispatchers.push_back(std::make_unique<DispatcherThread>(cfg.restaurants[r], book, fleet, graph, 36.0,
                                                                      cfg.dispatch, estimators, logger, clock, stop));
        clock.start();
        fleet.start();
        for (auto& d : dispatchers) d->start();
    }
    void shutdown() {
        stop.request();
        book.close();
        for (auto& d : dispatchers) d->join();
        fleet.join();
        logger.close();
    }
};

}

int main() {
    const std::string path = "dispatcher_thread_test.tmp.log";

    {
        Rig r(path, 2, {0});
        r.book.submit(batch({{"p1", "r0"}, {"p2", "r0"}, {"p3", "r0"}}, r.logger));
        check(deliversAll(r.fleet, 3) && r.book.pending() == 0, "los tres pedidos se entregan");
        r.shutdown();
        std::int64_t thirdAssignedT = -1, firstDeliveredT = -1, assigned = 0;
        std::ifstream in(path);
        for (std::string line; std::getline(in, line);) {
            const json j = json::parse(line);
            if (!j.contains("event")) continue;
            if (j["event"] == "orderAssigned" && ++assigned == 3) thirdAssignedT = j["t"];
            if (j["event"] == "orderDelivered" && firstDeliveredT < 0) firstDeliveredT = j["t"];
        }
        check(assigned == 3 && thirdAssignedT >= firstDeliveredT && r.dispatchers[0]->waits() >= 1,
              "la tercera asignacion llega despues de la primera entrega, tras dormir por la flota");
        check(r.dispatchers[0]->assigned() == 3 && r.logger.summary().delivered == 3, "contadores del despachador y resumen");
    }
    {
        Rig r(path, 1, {0, 1});
        r.book.submit(batch({{"q1", "r0"}, {"q2", "r1"}}, r.logger));
        check(deliversAll(r.fleet, 2), "con una moto para dos restaurantes se entregan los dos");
        r.shutdown();
        check(r.dispatchers[0]->assigned() + r.dispatchers[1]->assigned() == 2 &&
                  r.dispatchers[0]->waits() + r.dispatchers[1]->waits() >= 1,
              "cada despachador asigno el suyo y alguno tuvo que esperar");
    }
    {
        Rig r(path, 1, {0});
        r.book.submit(batch({{"s1", "r0"}, {"s2", "r0"}}, r.logger));
        std::this_thread::sleep_for(milliseconds(30));
        check(r.dispatchers[0]->assigned() == 1 && r.book.pending() == 1,
              "el primero asignado, el segundo vuelve al libro mientras no hay moto");
        const auto before = steady_clock::now();
        r.shutdown();
        check(steady_clock::now() - before < milliseconds(500) && r.book.pending() == 1, "la parada deja el pedido en el libro");
    }
    {
        Rig r(path, 1, {0}, 100'000);
        r.book.submit(batch({{"n1", "r0"}, {"n2", "r0"}}, r.logger));
        check(deliversAll(r.fleet, 1) && r.dispatchers[0]->assigned() == 1, "el primero se entrega");
        for (int i = 0; i < 200 && r.dispatchers[0]->rejected() < 1; ++i) std::this_thread::sleep_for(milliseconds(5));
        r.shutdown();
        std::int64_t rejectedT = -1;
        std::string reason;
        std::ifstream in(path);
        for (std::string line; std::getline(in, line);) {
            const json j = json::parse(line);
            if (j.contains("event") && j["event"] == "orderRejected") rejectedT = j["t"], reason = j["reason"];
        }
        check(r.dispatchers[0]->rejected() == 1 && reason == "noCourier" && rejectedT >= 100'000 && rejectedT < 200'000 &&
                  r.logger.summary().rejected == 1 && r.book.pending() == 0,
              "el segundo vence a los 100 s y se rechaza con noCourier; el resumen lo cuenta");
    }
    {
        Rig r(path, 1, {0});
        r.book.submit(batch({{"n1", "r0", 0, false}}, r.logger));
        check(assignsAtLeast(*r.dispatchers[0], 1), "la moto sale con el normal");
        r.book.submit(batch({{"n2", "r0", 1'000, false}, {"u1", "r0", 2'000, true}}, r.logger));
        check(deliversAll(r.fleet, 3), "los tres se entregan");
        r.shutdown();
        check(assignedOrder(path) == std::vector<std::string>{"n1", "u1", "n2"},
              "el urgente creado despues del normal se asigna antes que el");
        check(r.dispatchers[0]->waits() < 20, "el despachador durmio, no dio vueltas");
    }
    {
        Rig r(path, 1, {0}, 3'600'000, 50'000);
        r.book.submit(batch({{"n1", "r0", 0, false}}, r.logger));
        check(assignsAtLeast(*r.dispatchers[0], 1), "la moto sale con el normal");
        r.book.submit(batch({{"n2", "r0", 1'000, false}, {"u1", "r0", 2'000, true}}, r.logger));
        check(deliversAll(r.fleet, 3), "los tres se entregan");
        r.shutdown();
        check(assignedOrder(path) == std::vector<std::string>{"n1", "n2", "u1"},
              "el normal envejecido pasa delante del urgente mas nuevo");
        check(r.logger.summary().urgentCreated == 1 && r.logger.summary().urgentDelivered == 1,
              "el resumen cuenta el urgente creado y entregado");
    }
    std::remove(path.c_str());

    if (failures == 0) {
        std::cout << "dispatcher_thread_test: todas las pruebas pasaron\n";
        return 0;
    }
    std::cerr << "dispatcher_thread_test: " << failures << " prueba(s) fallaron\n";
    return 1;
}
