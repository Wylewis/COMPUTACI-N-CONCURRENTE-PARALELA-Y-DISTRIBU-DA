#include <chrono>
#include <cstdio>
#include <fstream>
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

struct Rig {  // una moto con su hilo, reloj x1000 y log propio
    SimClock clock{1000.0};
    StopSignal stop;
    EventLogger logger;
    Courier courier;
    CourierThread thread;
    Rig(const std::string& path, const std::string& id, int bag, const Config& cfg, const Graph& g)
        : logger(path, {id}), courier(id, "A", bag), thread(courier, g, cfg.restaurants, 36.0, logger, clock, stop) {
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
        Rig r(path, "c1", 1, cfg, g);
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
    std::remove(path.c_str());

    if (failures == 0) {
        std::cout << "courier_thread_test: todas las pruebas pasaron\n";
        return 0;
    }
    std::cerr << "courier_thread_test: " << failures << " prueba(s) fallaron\n";
    return 1;
}
