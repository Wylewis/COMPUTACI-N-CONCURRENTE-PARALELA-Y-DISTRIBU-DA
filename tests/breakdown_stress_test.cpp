// Prueba de estres: probabilidad de averia 1, muchas motos y pedidos, dos
// restaurantes. Todo se averia en cada viaje: liberaciones, reasignaciones,
// rescates que tambien se averian y reparaciones encadenadas. Lo que se
// exige: que nada se cuelgue (el apagado termina en menos de 5 s), que los
// contadores cuadren y que el log quede verificable. Se corre bajo
// ThreadSanitizer para buscar carreras. Recibe la ruta del log.
#include <chrono>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "dispatcher_thread.h"

using namespace std::chrono;
namespace {

int failures = 0;

void check(bool ok, const std::string& what) {
    if (!ok) {
        ++failures;
        std::cerr << "FALLO: " << what << "\n";
    }
}

// A --- B --- C --- D --- E en linea, 500 m entre vecinos, doble sentido; restaurantes en B y D
Config city(int couriers) {
    Config cfg;
    const char* ids[] = {"A", "B", "C", "D", "E"};
    for (int i = 0; i < 5; ++i) cfg.nodes.push_back({ids[i], 0.0, 0.0044966 * i});
    for (int i = 0; i < 4; ++i) cfg.streets.push_back({std::string(ids[i]) + ids[i + 1], ids[i], ids[i + 1], false});
    cfg.restaurants = {{"r0", "Uno", "B", 2, 0, 0}, {"r1", "Dos", "D", 2, 0, 0}};
    cfg.fleet = {couriers, 2, 36.0, "C"};
    cfg.orders.maxPending = 1000;
    cfg.orders.seed = 11;
    cfg.dispatch = {200, 3'600'000};
    cfg.incidents.breakdownProbability = 1.0;
    return cfg;
}

}  // namespace

int main(int argc, char** argv) {
    const std::string path = argc > 1 ? argv[1] : "breakdown_stress_test.tmp.log";
    constexpr int kCouriers = 24, kOrders = 400;
    const Config cfg = city(kCouriers);
    const Graph graph(cfg);
    SimClock clock(3000.0);
    StopSignal stop;
    EventLogger logger(path, Fleet::courierIds(kCouriers));
    OrderBook book(cfg);
    ThreadPool estimators(4);
    Restaurants restaurants(cfg.restaurants);
    Fleet fleet(cfg.fleet, graph, restaurants, logger, clock, stop);
    fleet.enableBreakdowns(cfg.incidents, cfg.orders.seed, [&book](Order o) { book.putBack(std::move(o)); });
    std::vector<std::unique_ptr<DispatcherThread>> dispatchers;
    for (const RestaurantConfig& r : cfg.restaurants)
        dispatchers.push_back(std::make_unique<DispatcherThread>(r, book, fleet, graph, 36.0, cfg.dispatch, estimators, logger, clock, stop));

    clock.start();
    fleet.start();
    for (auto& d : dispatchers) d->start();
    const char* dests[] = {"A", "C", "E"};
    for (int batchNo = 0; batchNo < kOrders / 20; ++batchNo) {  // 20 pedidos por rafaga, alternando restaurante
        OrderBatch batch;
        batch.atMs = clock.nowMs();
        for (int i = 0; i < 20; ++i) {
            Order o;
            o.id = "o" + std::to_string(batchNo * 20 + i);
            o.restaurantId = (i % 2) ? "r1" : "r0";
            o.deliveryNode = dests[i % 3];
            o.createdAtMs = batch.atMs;
            logger.publish(Event::orderCreated(batch.atMs, o.id, o.restaurantId));
            batch.orders.push_back(std::move(o));
        }
        book.submit(std::move(batch));
        std::this_thread::sleep_for(milliseconds(60));
    }
    std::this_thread::sleep_for(milliseconds(900));

    const auto before = steady_clock::now();
    stop.request();
    logger.publish(Event::simulationStopping(clock.nowMs()));
    book.close();
    for (auto& d : dispatchers) d->join();
    fleet.join();
    const std::vector<Order> left = book.drain();
    logger.close();
    const double shutdownS = duration<double>(steady_clock::now() - before).count();
    const Summary s = logger.summary();
    std::int64_t inBags = 0;
    for (const CourierSnapshot& c : fleet.snapshots(0)) inBags += c.bagCount;

    std::cout << "breakdown_stress_test: " << kCouriers << " motos, " << s.created << " pedidos, " << s.delivered << " entregados, "
              << s.breakdowns << " averias, " << s.reassigned << " reasignaciones, " << fleet.rescues() << " rescates, "
              << s.pending << " pendientes (" << left.size() << " en el libro, " << inBags << " en mochilas); apagado en "
              << shutdownS << " s\n";
    check(shutdownS < 5.0, "el apagado termina en menos de 5 s con todo averiandose");
    check(s.created == kOrders && s.pending == s.created - s.delivered - s.rejected, "los contadores del resumen cuadran");
    check(s.breakdowns >= kCouriers && s.reassigned > 0, "hubo averias en toda la flota y reasignaciones");
    check(static_cast<std::int64_t>(left.size()) + inBags == s.pending, "lo pendiente esta en el libro o en una mochila");

    if (failures == 0) {
        std::cout << "breakdown_stress_test: todas las pruebas pasaron\n";
        return 0;
    }
    std::cerr << "breakdown_stress_test: " << failures << " prueba(s) fallaron\n";
    return 1;
}
