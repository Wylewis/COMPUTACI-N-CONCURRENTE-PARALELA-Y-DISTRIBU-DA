#include <chrono>
#include <cstdio>
#include <iostream>
#include <string>
#include <thread>

#include "fleet.h"

namespace {

int failures = 0;

void check(bool ok, const std::string& what) {
    if (!ok) {
        ++failures;
        std::cerr << "FALLO: " << what << "\n";
    }
}

}  // namespace

int main() {
    Config cfg;  // A ---1000 m--- B, doble sentido; r0 en B
    cfg.nodes = {{"A", 0.0, 0.0}, {"B", 0.0, 0.0089932}};
    cfg.streets = {{"ab", "A", "B", false}};
    cfg.restaurants = {{"r0", "Uno", "B", 1, 0, 0}};
    cfg.fleet = {3, 2, 36.0, "A"};
    const Graph g(cfg);
    const std::string path = "fleet_test.tmp.log";
    SimClock clock(1000.0);
    StopSignal stop;
    EventLogger logger(path, Fleet::courierIds(3));
    Restaurants restaurants(cfg.restaurants);
    Fleet fleet(cfg.fleet, g, restaurants, logger, clock, stop);
    check(Fleet::courierIds(3) == std::vector<std::string>{"c0", "c1", "c2"} && fleet.size() == 3, "ids c0..c2");

    const auto snaps = fleet.snapshots(0);
    check(snaps.size() == 3 && snaps[1].id == "c1" && snaps[1].node == "A" && snaps[1].bagCapacity == 2 &&
              fleet.views().size() == 3 && fleet.courier(2).id() == "c2",
          "copias de toda la flota con los datos de configuracion");

    clock.start();
    fleet.start();
    Order o;
    o.id = "o1";
    o.restaurantId = "r0";
    o.deliveryNode = "B";
    logger.publish(Event::orderCreated(0, "o1", "r0"));
    const std::uint64_t v0 = fleet.version();
    Order other;
    other.id = "o2";
    check(!fleet.assign("c9", other) && other.id == "o2" && fleet.assign("c1", o) && fleet.snapshots(0)[1].bagCount == 1,
          "assign por id: desconocido falla con el pedido intacto; c1 lo reserva");
    const auto before = std::chrono::steady_clock::now();
    fleet.waitForChange(v0);  // la entrega de c1 cambia la version
    check(fleet.version() == v0 + 1 && fleet.delivered() == 1 &&
              std::chrono::steady_clock::now() - before < std::chrono::seconds(3),
          "la entrega incrementa la version y despierta a quien esperaba");
    const std::uint64_t v1 = fleet.version();
    std::thread waiter([&] { fleet.waitForChange(v1); });
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    stop.request();
    fleet.join();
    waiter.join();
    check(fleet.version() == v1, "la parada despierta a los dormidos sin cambiar la version");
    check(fleet.rescues() == 0 && fleet.breakdowns() == 0, "sin averias no hay rescates");
    logger.close();
    std::remove(path.c_str());

    if (failures == 0) {
        std::cout << "fleet_test: todas las pruebas pasaron\n";
        return 0;
    }
    std::cerr << "fleet_test: " << failures << " prueba(s) fallaron\n";
    return 1;
}
