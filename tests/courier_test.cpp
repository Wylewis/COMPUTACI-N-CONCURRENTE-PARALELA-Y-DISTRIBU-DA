#include <atomic>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "config.h"
#include "courier.h"

namespace {

int failures = 0;

void check(bool ok, const std::string& what) {
    if (!ok) {
        ++failures;
        std::cerr << "FALLO: " << what << "\n";
    }
}

Config lineCity() {
    Config cfg;
    cfg.nodes = {{"A", 0.0, 0.0}, {"B", 0.0, 0.0089932}, {"C", 0.0, 0.0134898}};
    cfg.streets = {{"ab", "A", "B", false}, {"bc", "B", "C", false}};
    return cfg;
}

Order assigned(const std::string& id, const std::string& delivery) {
    Order o;
    o.id = id;
    o.restaurantId = "r0";
    o.deliveryNode = delivery;
    return o;
}

}

int main() {
    const Graph g(lineCity());

    {
        Courier c("c0", "A", 2);
        Order a = assigned("a", "C"), b = assigned("b", "B"), d = assigned("d", "C");
        check(c.reserve(a) && c.reserve(b) && a.id.empty() && b.id.empty() && c.bagItems()[0].state == OrderState::Assigned,
              "dos reservas entran por move y quedan asignadas");
        check(!c.reserve(d) && d.id == "d" && c.bagCount() == 2, "la tercera se rechaza y el pedido sigue fuera");
        const auto out = c.removeOrder("a");
        check(out && out->id == "a" && !c.hasOrder("a") && c.hasOrder("b"), "removeOrder saca el pedido por move");
        check(!c.removeOrder("zz") && c.reserve(d) && c.bagCount() == 2, "ausente da nullopt; tras liberar lugar entra");
        c.setState(CourierState::BrokenDown);
        Order e = assigned("e", "B");
        const bool rejected = !c.reserve(e);
        const auto bag = c.takeBag();
        check(rejected && bag.size() == 2 && c.bagCount() == 0, "averiado no acepta; takeBag vacia la mochila");
    }
    {
        Courier c("c2", "A", 2);
        const CourierSnapshot idle = c.estimateSnapshot(1000);
        check(c.state() == CourierState::Idle && idle.node == "A" && idle.availableAtMs == 1000 && idle.bagCapacity == 2,
              "inactivo: su nodo, disponible ahora");
        const auto trip = std::make_shared<const RouteMotion>(*g.shortestPath("A", "C"), g, 36.0, 5000);
        c.startTrip(trip, CourierState::ToRestaurant);
        const CourierSnapshot moving = c.estimateSnapshot(10'000), late = c.estimateSnapshot(trip->arrivalMs() + 1000);
        check(c.state() == CourierState::ToRestaurant && moving.node == "C" && moving.availableAtMs == trip->arrivalMs() &&
              late.availableAtMs == trip->arrivalMs() + 1000, "en viaje: destino y llegada, o ahora si ya debio llegar");
        const CourierView v = c.view();
        const GeoPoint p = v.trip->positionAt(55'000);
        check(v.trip == trip && v.state == CourierState::ToRestaurant && p.lon > 0.004 && p.lon < 0.005,
              "la vista comparte el viaje y calcula la posicion sin tocar al repartidor");
        c.endTrip("C", CourierState::Idle);
        check(c.node() == "C" && c.state() == CourierState::Idle && !c.view().trip, "endTrip fija el nodo y suelta el viaje");
    }
    {
        Courier c("c3", "A", 3);
        Order far = assigned("far", "C"), near = assigned("near", "B"), pending = assigned("p", "B");
        c.reserve(far), c.reserve(near), c.reserve(pending);
        check(!c.nearestDelivery(g) && c.markPickedUp("far") && c.markPickedUp("near") && !c.markPickedUp("zz"),
              "sin recogidos no hay siguiente; markPickedUp solo sobre la mochila");
        const auto next = c.nearestDelivery(g);
        check(next && *next == "B" && c.view().deliveryNodes.size() == 3, "el destino mas cercano por calles va primero");
    }
    {
        Courier c("c4", "A", 5);
        std::atomic<int> accepted{0};
        std::vector<std::thread> threads;
        for (int t = 0; t < 8; ++t)
            threads.emplace_back([&, t] {
                for (int k = 0; k < 50; ++k) {
                    Order o = assigned("o" + std::to_string(t) + "-" + std::to_string(k), "B");
                    if (c.reserve(o)) accepted.fetch_add(1);
                }
            });
        for (auto& th : threads) th.join();
        check(accepted.load() == 5 && c.bagCount() == 5, "8 hilos reservando a la vez: entran exactamente 5");
    }

    if (failures == 0) {
        std::cout << "courier_test: todas las pruebas pasaron\n";
        return 0;
    }
    std::cerr << "courier_test: " << failures << " prueba(s) fallaron\n";
    return 1;
}
