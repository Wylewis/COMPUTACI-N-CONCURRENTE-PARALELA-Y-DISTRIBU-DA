#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <mutex>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include "courier_thread.h"
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

// A ---1000 m--- B ---500 m--- C, doble sentido; restaurante r0 en B
Config lineCity() {
    Config cfg;
    cfg.nodes = {{"A", 0.0, 0.0}, {"B", 0.0, 0.0089932}, {"C", 0.0, 0.0134898}};
    cfg.streets = {{"ab", "A", "B", false}, {"bc", "B", "C", false}};
    cfg.restaurants = {{"r0", "R", "B", 1, 0, 0}};
    return cfg;
}

Order created(const std::string& id, const std::string& delivery, EventLogger& logger) {
    Order o;
    o.id = id;
    o.restaurantId = "r0";
    o.deliveryNode = delivery;
    logger.publish(Event::orderCreated(0, id, "r0"));
    return o;
}

// (evento, pedido) de cada linea del log, sin el resumen
std::vector<std::pair<std::string, std::string>> events(const std::string& path) {
    std::vector<std::pair<std::string, std::string>> out;
    std::ifstream in(path);
    for (std::string line; std::getline(in, line);)
        if (const json j = json::parse(line); j.contains("event")) out.emplace_back(j["event"], j.value("order", ""));
    return out;
}

bool deliversAll(const CourierThread& t, std::int64_t n) {
    for (int i = 0; i < 600 && t.delivered() < n; ++i) std::this_thread::sleep_for(milliseconds(5));
    return t.delivered() == n;
}

struct Rig {  // una moto con su hilo, reloj a escala y log propio
    SimClock clock;
    StopSignal stop;
    EventLogger logger;
    Restaurants restaurants;
    Courier courier;
    CourierThread thread;
    Rig(const std::string& path, const std::string& id, int bag, const Config& cfg, const Graph& g, double scale = 1000.0)
        : clock(scale), logger(path, {id}), restaurants(cfg.restaurants), courier(id, "A", bag),
          thread(courier, g, restaurants, 36.0, logger, clock, stop) {
        clock.start();
        thread.start();
    }
};

}  // namespace

int main() {
    const Config cfg = lineCity();
    const Graph g(cfg);
    const std::string path = "courier_thread_test.tmp.log";

    {   // ciclo completo: recogida en B, entrega en B (mas cercano) y luego en C
        Rig r(path, "c0", 2, cfg, g);
        Order far = created("far", "C", r.logger), near = created("near", "B", r.logger), extra = created("x", "C", r.logger);
        check(r.thread.assign(far) && r.thread.assign(near) && !r.thread.assign(extra) && extra.id == "x",
              "dos asignaciones entran, la tercera no cabe y sigue fuera");
        check(deliversAll(r.thread, 2) && r.courier.node() == "C" && r.courier.state() == CourierState::Idle,
              "entrega los dos y queda libre en C");
        r.stop.request();
        r.thread.join();
        r.logger.close();
        const std::vector<std::pair<std::string, std::string>> expected = {
            {"orderCreated", "far"}, {"orderCreated", "near"}, {"orderCreated", "x"}, {"orderAssigned", "far"},
            {"orderAssigned", "near"}, {"pickupStarted", "far"}, {"orderPickedUp", "far"}, {"pickupStarted", "near"},
            {"orderPickedUp", "near"}, {"orderDelivered", "near"}, {"orderDelivered", "far"}};
        check(events(path) == expected, "secuencia exacta de eventos, el destino mas cercano primero");
        check(r.logger.summary().delivered == 2 && r.logger.summary().deliveredPerCourier.at("c0") == 2, "resumen con 2 entregas");
    }
    {   // parada en mitad de un tramo: sale en el acto y queda en el ultimo nodo alcanzado
        Rig r(path, "c1", 1, cfg, g, 100.0);  // el tramo dura 1 s real: la carga de la maquina no lo adelanta
        Order o = created("o", "C", r.logger);
        r.thread.assign(o);
        std::this_thread::sleep_for(milliseconds(30));
        check(r.courier.state() == CourierState::ToRestaurant && r.courier.view().trip != nullptr, "en camino al restaurante");
        const auto before = steady_clock::now();
        r.stop.request();
        r.thread.join();
        check(steady_clock::now() - before < milliseconds(500) && r.thread.delivered() == 0 && r.courier.node() == "A" &&
              r.courier.state() == CourierState::Idle && r.courier.bagCount() == 1, "la parada interrumpe el tramo");
    }
    {   // sin pedidos duerme; la parada lo despierta y join vuelve
        Rig r(path, "c2", 1, cfg, g);
        std::this_thread::sleep_for(milliseconds(20));
        r.stop.request();
        const auto before = steady_clock::now();
        r.thread.join();
        check(steady_clock::now() - before < milliseconds(500), "join despierta al hilo dormido");
    }
    {   // dos motos y un solo puesto: la segunda hace fila y recoge despues de que la primera termina
        std::remove(path.c_str());
        SimClock clock(1000.0);
        StopSignal stop;
        EventLogger logger(path, {"d0", "d1"});
        Restaurants restaurants(cfg.restaurants);
        Courier c0("d0", "A", 1), c1("d1", "A", 1);
        CourierThread t0(c0, g, restaurants, 36.0, logger, clock, stop), t1(c1, g, restaurants, 36.0, logger, clock, stop);
        clock.start();
        t0.start(), t1.start();
        Order a = created("a", "C", logger), b = created("b", "C", logger);
        t0.assign(a), t1.assign(b);
        for (int i = 0; i < 600 && t0.delivered() + t1.delivered() < 2; ++i) std::this_thread::sleep_for(milliseconds(5));
        stop.request();
        t0.join(), t1.join();
        logger.close();
        std::int64_t firstPickedUp = -1, secondStarted = -1;
        std::ifstream in(path);
        for (std::string line; std::getline(in, line);) {
            const json j = json::parse(line);
            if (!j.contains("event")) continue;
            if (j["event"] == "orderPickedUp" && firstPickedUp < 0) firstPickedUp = j["t"];
            if (j["event"] == "pickupStarted") secondStarted = j["t"];
        }
        check(t0.delivered() + t1.delivered() == 2 && secondStarted >= firstPickedUp && restaurants.at("r0").served() == 2,
              "con un puesto, la segunda recogida empieza cuando termina la primera");
    }
    {   // averia: c0 (probabilidad 1) se averia en el tramo, libera el pedido, se repara; c1 lo recibe reasignado
        std::remove(path.c_str());
        SimClock clock(1000.0);
        StopSignal stop;
        EventLogger logger(path, {"e0", "e1"});
        Restaurants restaurants(cfg.restaurants);
        Courier c0("e0", "A", 1), c1("e1", "A", 1);
        CourierThread t0(c0, g, restaurants, 36.0, logger, clock, stop), t1(c1, g, restaurants, 36.0, logger, clock, stop);
        std::vector<Order> released;
        std::mutex m;
        t0.enableBreakdowns(7, 0, 1.0);
        t0.onReleased([&](Order o) { std::lock_guard<std::mutex> lock(m); released.push_back(std::move(o)); });
        std::atomic<int> freed{0};
        t0.onFreed([&] { ++freed; });
        clock.start();
        t0.start(), t1.start();
        Order o = created("o", "C", logger);
        t0.assign(o);
        for (int i = 0; i < 200 && t0.breakdowns() < 1; ++i) std::this_thread::sleep_for(milliseconds(5));
        std::this_thread::sleep_for(milliseconds(20));
        Order back;
        bool releasedOk;
        {
            std::lock_guard<std::mutex> lock(m);
            releasedOk = released.size() == 1 && released[0].courierId == "e0" && released[0].state == OrderState::Created;
            if (releasedOk) back = std::move(released[0]);
        }
        check(t0.breakdowns() == 1 && releasedOk && c0.state() == CourierState::BrokenDown && c0.bagCount() == 0 &&
                  c0.node() == "B" && c0.estimateSnapshot(0).broken,
              "se averia al final del tramo, libera el pedido reservado y queda averiada y marcada en el nodo");
        check(releasedOk && t1.assign(back) && deliversAll(t1, 1), "otra moto recibe el pedido liberado y lo entrega");
        for (int i = 0; i < 200 && freed < 1; ++i) std::this_thread::sleep_for(milliseconds(5));
        check(freed == 1 && c0.state() == CourierState::Idle, "tras kRepairMs vuelve al servicio y avisa a la flota");
        stop.request();
        t0.join(), t1.join();
        logger.close();
        const auto ev = events(path);
        const bool hasBreak = std::find(ev.begin(), ev.end(), std::make_pair(std::string("courierBreakdown"), std::string())) != ev.end();
        const bool hasReassign = std::find(ev.begin(), ev.end(), std::make_pair(std::string("orderReassigned"), std::string("o"))) != ev.end();
        const Summary sm = logger.summary();
        check(hasBreak && hasReassign && sm.breakdowns == 1 && sm.reassigned == 1 && sm.delivered == 1,
              "el log tiene courierBreakdown y orderReassigned y el resumen los cuenta");
    }
    {   // traspaso: f0 recoge en B y se averia camino a D con el pedido recogido; f1, cerca, llega antes de la
        // reparacion, va hasta ella y lo entrega
        std::remove(path.c_str());
        Config city = cfg;  // A-B-C-D: el viaje B->D tiene dos tramos
        city.nodes.push_back({"D", 0.0, 0.0359728});
        city.streets.push_back({"cd", "C", "D", false});
        const Graph g4(city);
        SimClock clock(1000.0);
        StopSignal stop;
        EventLogger logger(path, {"f0", "f1"});
        Restaurants restaurants(city.restaurants);
        Courier c0("f0", "B", 1), c1("f1", "C", 1);
        CourierThread t0(c0, g4, restaurants, 36.0, logger, clock, stop), t1(c1, g4, restaurants, 36.0, logger, clock, stop);
        t0.enableBreakdowns(3, 0, 1.0);
        t0.onRescueNeeded([&](const std::string&) { t1.rescue(c0); });
        clock.start();
        t0.start(), t1.start();
        Order o = created("o", "D", logger);
        t0.assign(o);
        check(deliversAll(t1, 1) && t1.rescues() == 1 && c0.bagCount() == 0, "f1 rescata el pedido recogido y lo entrega");
        stop.request();
        t0.join(), t1.join();
        logger.close();
        const auto ev = events(path);
        const std::vector<std::pair<std::string, std::string>> expected = {
            {"orderCreated", "o"}, {"orderAssigned", "o"}, {"pickupStarted", "o"}, {"orderPickedUp", "o"},
            {"courierBreakdown", ""}, {"orderReassigned", "o"}, {"orderDelivered", "o"}};
        const Summary sm = logger.summary();
        check(ev == expected && sm.reassigned == 1 && sm.breakdowns == 1 && sm.deliveredPerCourier.at("f1") == 1,
              "secuencia: recogida, averia, traspaso y entrega por la rescatista");
    }
    std::remove(path.c_str());

    if (failures == 0) {
        std::cout << "courier_thread_test: todas las pruebas pasaron\n";
        return 0;
    }
    std::cerr << "courier_thread_test: " << failures << " prueba(s) fallaron\n";
    return 1;
}
