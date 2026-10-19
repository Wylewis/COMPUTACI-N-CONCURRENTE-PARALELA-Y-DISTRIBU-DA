#include <chrono>
#include <cmath>
#include <thread>
#include <iostream>
#include <string>
#include <vector>

#include "dispatch_select.h"

namespace {

int failures = 0;

void check(bool ok, const std::string& what) {
    if (!ok) {
        ++failures;
        std::cerr << "FALLO: " << what << "\n";
    }
}

// A ---1000 m--- B ---500 m--- C ---2500 m--- D, doble sentido; F solo sale hacia A.
// Restaurante r0 en B.
Config lineCity() {
    Config cfg;
    cfg.nodes = {{"A", 0.0, 0.0}, {"B", 0.0, 0.0089932}, {"C", 0.0, 0.0134898}, {"D", 0.0, 0.0359728},
                 {"F", 0.0009, 0.0}};
    cfg.streets = {{"ab", "A", "B", false}, {"bc", "B", "C", false}, {"cd", "C", "D", false}, {"fa", "F", "A", true}};
    cfg.restaurants = {{"r0", "Uno", "B", 1, 0, 0}};
    return cfg;
}

CourierSnapshot at(const std::string& id, const std::string& node, std::int64_t availableAtMs = 0, int bag = 0) {
    return {id, node, availableAtMs, bag, 2};
}

Order orderTo(const std::string& node) {
    Order o;
    o.id = "o1";
    o.restaurantId = "r0";
    o.deliveryNode = node;
    return o;
}

}  // namespace

int main() {
    const Config cfg = lineCity();
    const Graph g(cfg);
    const RestaurantConfig& r0 = cfg.restaurants[0];
    const Order toC = orderTo("C");
    // c0 en A (1000 m), c1 en C (500 m), c2 en D (3000 m) con la mochila llena, c3 en B (0 m) libre en 10 min
    const std::vector<CourierSnapshot> fleet = {at("c0", "A"), at("c1", "C"), at("c2", "D", 0, 2), at("c3", "B", 600'000)};

    check(std::fabs(straightLineM(g, "A", "B") - 1000.0) < 1.0 && std::fabs(straightLineM(g, "B", "D") - 3000.0) < 3.0,
          "distancia en linea recta");
    {   // anillos: (−1, 300] solo c3; (300, 600] solo c1; (600, 1200] solo c0; c2 nunca (mochila llena)
        const auto r1 = ringCandidates(fleet, g, "B", -1.0, 300.0), r2 = ringCandidates(fleet, g, "B", 300.0, 600.0),
                   r3 = ringCandidates(fleet, g, "B", 600.0, 1200.0), r4 = ringCandidates(fleet, g, "B", 1200.0, 5000.0);
        check(r1.size() == 1 && r1[0].id == "c3" && r2.size() == 1 && r2[0].id == "c1" && r3.size() == 1 &&
              r3[0].id == "c0" && r4.empty(), "cada anillo tiene solo a los suyos y excluye mochilas llenas");
    }
    {   // map + reduce: c1 (500 m, libre ya) gana a c0 (1000 m); el empate lo decide el id
        const auto ests = estimateAll({at("c0", "A"), at("c1", "C"), at("c2", "D", 0, 2)}, toC, r0, g, 36.0, 0);
        const auto best = bestEstimate(ests);
        check(ests.size() == 2 && best && best->courierId == "c1", "map descarta la mochila llena y reduce elige c1");
        const auto tie = bestEstimate(estimateAll({at("cb", "A"), at("ca", "A")}, toC, r0, g, 36.0, 0));
        check(tie && tie->courierId == "ca", "en empate gana el id menor");
        check(!bestEstimate({}).has_value(), "reduce de nada es nada");
    }
    {   // radio creciente: el primer anillo con alguien factible decide, aunque mas lejos haya uno mas rapido
        const auto sel = selectCourier(fleet, toC, r0, g, 36.0, 0);
        check(sel && sel->estimate.courierId == "c3" && sel->rings == 1 && sel->estimated == 1 && sel->radiusM == 300.0,
              "c3 esta en el restaurante y se elige en el primer anillo");
        const auto noC3 = selectCourier({fleet[0], fleet[1], fleet[2]}, toC, r0, g, 36.0, 0);
        check(noC3 && noC3->estimate.courierId == "c1" && noC3->rings == 2 && noC3->estimated == 1,
              "sin c3, el segundo anillo trae a c1 y c0 no se estima");
        const auto onlyFar = selectCourier({fleet[0], fleet[2]}, toC, r0, g, 36.0, 0);
        check(onlyFar && onlyFar->estimate.courierId == "c0" && onlyFar->rings == 3 && onlyFar->radiusM == 1200.0,
              "tres anillos hasta llegar a c0");
    }
    {   // map concurrente: mismo resultado que en serie; la tardia se ignora; la que lanza es un error
        const OneEstimate one = [&](const CourierSnapshot& c) { return estimateDelivery(c, toC, r0, g, 36.0, 0); };
        const auto serial = estimateAll(fleet, toC, r0, g, 36.0, 0);
        const AsyncEstimates par = estimateAllAsync(fleet, one, std::chrono::milliseconds(500));
        bool same = par.estimates.size() == serial.size();
        for (std::size_t i = 0; same && i < serial.size(); ++i)
            same = par.estimates[i].courierId == serial[i].courierId && par.estimates[i].deliveryAtMs == serial[i].deliveryAtMs;
        check(same && par.late == 0 && par.failed == 1, "el map concurrente da lo mismo que el serial y cuenta la mochila llena como error");

        const OneEstimate slowOrBad = [&](const CourierSnapshot& c) {
            if (c.id == "c0") std::this_thread::sleep_for(std::chrono::milliseconds(80));
            if (c.id == "c3") throw EstimateError("sin ruta");
            return estimateDelivery(c, toC, r0, g, 36.0, 0);
        };
        const AsyncEstimates timed = estimateAllAsync(fleet, slowOrBad, std::chrono::milliseconds(20));
        check(timed.late == 1 && timed.failed == 2 && timed.estimates.size() == 1 && timed.estimates[0].courierId == "c1",
              "la tardia se ignora, las que lanzan se cuentan y solo queda c1");

        int calls = 0;
        const Mapper counting = [&](const std::vector<CourierSnapshot>& ring) { ++calls; return estimateAll(ring, toC, r0, g, 36.0, 0); };
        const auto sel = selectCourier(fleet, toC, r0, g, 36.0, 0, kFirstRadiusM, counting);
        check(sel && sel->estimate.courierId == "c3" && calls == 1, "selectCourier usa el map que se le pasa, una vez por anillo");
    }
    {   // sin candidato: todos llenos, o destino inalcanzable, o flota vacia
        check(!selectCourier({at("c2", "D", 0, 2)}, toC, r0, g, 36.0, 0), "una flota llena no da seleccion");
        const auto unreachable = selectCourier(fleet, orderTo("F"), r0, g, 36.0, 0);
        check(!unreachable, "destino inalcanzable: ninguna estimacion sale");
        check(!selectCourier({}, toC, r0, g, 36.0, 0), "flota vacia");
    }

    if (failures == 0) {
        std::cout << "dispatch_select_test: todas las pruebas pasaron\n";
        return 0;
    }
    std::cerr << "dispatch_select_test: " << failures << " prueba(s) fallaron\n";
    return 1;
}
