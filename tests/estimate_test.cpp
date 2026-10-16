// Pruebas de la estimacion de entrega: tiempos conocidos en una ciudad en
// linea, espera de preparacion, repartidor ocupado, mochila llena, sin ruta,
// comparacion entre candidatos, y una comprobacion sobre la ciudad real.
#include <cmath>
#include <iostream>
#include <string>

#include "config.h"
#include "estimate.h"
#include "graph.h"
#include "order.h"

namespace {

int failures = 0;

void check(bool ok, const std::string& what) {
    if (!ok) {
        ++failures;
        std::cerr << "FALLO: " << what << "\n";
    }
}

bool near(std::int64_t a, std::int64_t b, std::int64_t tol) {
    return std::llabs(a - b) <= tol;
}

// A ---1000 m--- B ---500 m--- C, doble sentido. E es un callejon: se entra desde A
// y no se sale. F es lo contrario: de F se sale hacia A, pero nadie puede llegar a F.
Config lineCity() {
    Config cfg;
    cfg.nodes = {{"A", 0.0, 0.0}, {"B", 0.0, 0.0089932}, {"C", 0.0, 0.0134898},
                 {"E", -0.0009, 0.0}, {"F", 0.0009, 0.0}};
    cfg.streets = {{"ab", "A", "B", false}, {"bc", "B", "C", false}, {"ae", "A", "E", true},
                   {"fa", "F", "A", true}};
    cfg.restaurants = {{"r0", "Uno", "B", 1, 0, 0}};
    cfg.fleet = {2, 2, 36.0, "A"};
    return cfg;
}

Order orderAt(const std::string& node, std::int64_t createdMs, std::int64_t readyMs) {
    Order o;
    o.id = "o1";
    o.restaurantId = "r0";
    o.deliveryNode = node;
    o.createdAtMs = createdMs;
    o.readyAtMs = readyMs;
    return o;
}

bool throwsEstimate(const CourierSnapshot& c, const Order& o, const RestaurantConfig& r, const Graph& g,
                    std::string& msg) {
    try {
        estimateDelivery(c, o, r, g, 36.0, 0);
    } catch (const EstimateError& e) {
        msg = e.what();
        return true;
    }
    return false;
}

}  // namespace

int main(int argc, char** argv) {
    const Config cfg = lineCity();
    const Graph g(cfg);
    const RestaurantConfig& r0 = cfg.restaurants[0];
    const std::int64_t now = 10'000;

    // A 36 km/h: A->B 100 s, B->C 50 s. Recoger y entregar: 60 s cada uno.
    // Pedido listo enseguida: recoge a los 100 s de salir, entrega a 100+60+50+60 = 270 s.
    {
        CourierSnapshot c{"c0", "A", 0, 0, 2};
        const Estimate e = estimateDelivery(c, orderAt("C", now, now), r0, g, 36.0, now);
        check(e.courierId == "c0", "la estimacion lleva el id del repartidor");
        check(near(e.pickupAtMs, now + 100'000, 100), "recoge al llegar, a los 100 s");
        check(near(e.deliveryAtMs, now + 270'000, 200), "entrega a los 270 s");
        check(std::fabs(e.toRestaurantM - 1000) < 1 && std::fabs(e.toCustomerM - 500) < 1, "distancias de 1000 m y 500 m");
    }
    // Pedido que estara listo recien a los 500 s: espera la preparacion
    {
        CourierSnapshot c{"c0", "A", 0, 0, 2};
        const Estimate e = estimateDelivery(c, orderAt("C", now, now + 500'000), r0, g, 36.0, now);
        check(near(e.pickupAtMs, now + 500'000, 1), "recoge cuando el pedido esta listo");
        check(near(e.deliveryAtMs, now + 500'000 + 60'000 + 50'000 + 60'000, 200), "entrega 170 s despues de listo");
    }
    // Repartidor ocupado hasta los 200 s en B (ya en el restaurante)
    {
        CourierSnapshot c{"c1", "B", now + 200'000, 1, 2};
        const Estimate e = estimateDelivery(c, orderAt("C", now, now), r0, g, 36.0, now);
        check(near(e.pickupAtMs, now + 200'000, 1), "sale cuando queda libre y ya esta en el restaurante");
        check(near(e.deliveryAtMs, now + 200'000 + 60'000 + 50'000 + 60'000, 200), "entrega 170 s despues de quedar libre");
        check(e.toRestaurantM == 0.0, "sin trayecto al restaurante si ya esta ahi");
    }
    // availableAt en el pasado cuenta como ahora
    {
        CourierSnapshot c{"c0", "A", 0, 0, 2};
        const Estimate e = estimateDelivery(c, orderAt("C", now, now), r0, g, 36.0, now);
        check(near(e.pickupAtMs, now + 100'000, 100), "disponible antes de ahora: sale ahora");
    }
    // Comparacion: el que esta mas cerca gana
    {
        CourierSnapshot nearC{"c0", "B", 0, 0, 2};
        CourierSnapshot farC{"c1", "A", 0, 0, 2};
        const Order o = orderAt("C", now, now);
        const Estimate a = estimateDelivery(nearC, o, r0, g, 36.0, now);
        const Estimate b = estimateDelivery(farC, o, r0, g, 36.0, now);
        check(a.deliveryAtMs < b.deliveryAtMs, "el repartidor que esta en el restaurante entrega antes");
    }
    // Errores: mochila llena, sin ruta al restaurante, sin ruta al cliente, restaurante equivocado
    {
        std::string msg;
        CourierSnapshot full{"c0", "A", 0, 2, 2};
        check(throwsEstimate(full, orderAt("C", 0, 0), r0, g, msg) && msg.find("mochila llena") != std::string::npos,
              "mochila llena lanza EstimateError");
        CourierSnapshot stuck{"c0", "E", 0, 0, 2};
        check(throwsEstimate(stuck, orderAt("C", 0, 0), r0, g, msg) && msg.find("sin ruta") != std::string::npos,
              "sin ruta al restaurante lanza EstimateError");
        CourierSnapshot ok{"c0", "A", 0, 0, 2};
        check(throwsEstimate(ok, orderAt("F", 0, 0), r0, g, msg) && msg.find("punto de entrega") != std::string::npos,
              "sin ruta al cliente lanza EstimateError");
        check(!throwsEstimate(ok, orderAt("E", 0, 0), r0, g, msg), "el callejon E si es entregable desde B");
        RestaurantConfig other{"r9", "Otro", "B", 1, 0, 0};
        check(throwsEstimate(ok, orderAt("C", 0, 0), other, g, msg), "restaurante que no es el del pedido lanza");
    }

    // Ciudad real: desde el punto de partida, cada restaurante da una estimacion finita y creciente con la distancia
    if (argc > 1) {
        try {
            Config city = loadConfig(argv[1]);
            validateConfig(city);
            const Graph cg(city);
            CourierSnapshot c{"c0", city.fleet.startNode, 0, 0, city.fleet.bagCapacity};
            int ok = 0;
            for (const auto& r : city.restaurants) {
                Order o;
                o.id = "o" + r.id;
                o.restaurantId = r.id;
                o.deliveryNode = city.fleet.startNode;  // ida y vuelta al punto de partida
                o.readyAtMs = 0;
                const Estimate e = estimateDelivery(c, o, r, cg, city.fleet.speedKmh, 0);
                const std::int64_t travel = e.deliveryAtMs - kPickupHandoverMs - kDeliveryHandoverMs;
                check(travel > 0 && travel < 3'600'000, "la estimacion real es positiva y menor a una hora simulada");
                check(e.toRestaurantM > 0 && e.toCustomerM > 0, "distancias reales positivas");
                ++ok;
            }
            check(ok == static_cast<int>(city.restaurants.size()), "todos los restaurantes se estiman");
            std::cout << "ciudad real: " << ok << " restaurantes estimados desde " << city.fleet.startNode << "\n";
        } catch (const std::exception& e) {
            check(false, std::string("la ciudad real no se pudo cargar: ") + e.what());
        }
    }

    if (failures == 0) {
        std::cout << "estimate_test: todas las pruebas pasaron\n";
        return 0;
    }
    std::cerr << "estimate_test: " << failures << " prueba(s) fallaron\n";
    return 1;
}
