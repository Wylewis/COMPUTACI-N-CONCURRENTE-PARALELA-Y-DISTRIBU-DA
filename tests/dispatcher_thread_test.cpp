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

// A ---1000 m--- B ---500 m--- C, doble sentido; r0 en B, r1 en C; flota en A
Config lineCity(int couriers) {
    Config cfg;
    cfg.nodes = {{"A", 0.0, 0.0}, {"B", 0.0, 0.0089932}, {"C", 0.0, 0.0134898}};
    cfg.streets = {{"ab", "A", "B", false}, {"bc", "B", "C", false}};
    cfg.restaurants = {{"r0", "Uno", "B", 1, 0, 0}, {"r1", "Dos", "C", 1, 0, 0}};
    cfg.fleet = {couriers, 1, 36.0, "A"};
    cfg.orders.maxPending = 10;
    cfg.dispatch.acceptTimeoutMs = 3'600'000;
    cfg.dispatch.quoteTimeoutMs = 200;
    return cfg;
}

OrderBatch batch(const std::vector<std::pair<std::string, std::string>>& idRestaurant, EventLogger& logger) {
    OrderBatch b;
    for (const auto& [id, r] : idRestaurant) {
        Order o;
        o.id = id;
        o.restaurantId = r;
        o.deliveryNode = "C";
        logger.publish(Event::orderCreated(0, id, r));
        b.orders.push_back(std::move(o));
    }
    return b;
}

bool deliversAll(const Fleet& f, std::int64_t n) {
    for (int i = 0; i < 1000 && f.delivered() < n; ++i) std::this_thread::sleep_for(milliseconds(5));
    return f.delivered() == n;
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
    Rig(const std::string& path, int couriers, const std::vector<int>& which, std::int64_t acceptMs = 3'600'000)
        : cfg(lineCity(couriers)), graph(cfg), logger(path, Fleet::courierIds(couriers)), book(cfg),
          restaurants(cfg.restaurants), fleet(cfg.fleet, graph, restaurants, logger, clock, stop) {
        cfg.dispatch.acceptTimeoutMs = acceptMs;
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

}  // namespace

int main() {
    const std::string path = "dispatcher_thread_test.tmp.log";

    {   // dos motos de mochila 1 y tres pedidos: el tercero espera a que una entrega libere lugar, sin sondeo
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
    {   // dos despachadores compiten por una sola moto: uno espera; ambos pedidos se entregan
        Rig r(path, 1, {0, 1});
        r.book.submit(batch({{"q1", "r0"}, {"q2", "r1"}}, r.logger));
        check(deliversAll(r.fleet, 2), "con una moto para dos restaurantes se entregan los dos");
        r.shutdown();
        check(r.dispatchers[0]->assigned() + r.dispatchers[1]->assigned() == 2 &&
                  r.dispatchers[0]->waits() + r.dispatchers[1]->waits() >= 1,
              "cada despachador asigno el suyo y alguno tuvo que esperar");
    }
    {   // parada con un pedido esperando moto: vuelve al libro y el despachador sale en el acto
        Rig r(path, 1, {0});
        r.book.submit(batch({{"s1", "r0"}, {"s2", "r0"}}, r.logger));
        std::this_thread::sleep_for(milliseconds(30));
        check(r.dispatchers[0]->assigned() == 1 && r.book.pending() == 0, "el primero asignado, el segundo en manos del despachador");
        const auto before = steady_clock::now();
        r.shutdown();
        check(steady_clock::now() - before < milliseconds(500) && r.book.pending() == 1, "la parada devuelve el pedido al libro");
    }
    {   // una moto, dos pedidos y plazo de 100 s simulados: el segundo se rechaza con noCourier al vencer
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
    std::remove(path.c_str());

    if (failures == 0) {
        std::cout << "dispatcher_thread_test: todas las pruebas pasaron\n";
        return 0;
    }
    std::cerr << "dispatcher_thread_test: " << failures << " prueba(s) fallaron\n";
    return 1;
}
