#include <cmath>
#include <iostream>
#include <set>
#include <string>
#include <vector>

#include "config.h"
#include "graph.h"
#include "order.h"
#include "order_generator.h"

namespace {

int failures = 0;

void check(bool ok, const std::string& what) {
    if (!ok) {
        ++failures;
        std::cerr << "FALLO: " << what << "\n";
    }
}

Config smallCity(std::uint64_t seed, int burstMax) {
    Config cfg;
    cfg.nodes = {{"A", 0.0000, 0.0000}, {"B", 0.0000, 0.0009}, {"C", 0.0009, 0.0009},
                 {"D", 0.0009, 0.0018}, {"E", -0.0009, 0.0000}};
    cfg.streets = {{"ab", "A", "B", false}, {"bc", "B", "C", true}, {"cd", "C", "D", true},
                   {"db", "D", "B", true},  {"ae", "A", "E", true}};
    cfg.restaurants = {{"r0", "Uno", "A", 1, 60'000, 120'000}, {"r1", "Dos", "E", 2, 30'000, 30'000}};
    cfg.orders = {20'000, burstMax, 50, seed};
    return cfg;
}

struct Flat {
    std::string id, restaurant, node;
    std::int64_t at, ready;
    OrderState state;
};

std::vector<Flat> run(const Config& cfg, const Graph& g, int batches) {
    OrderGenerator gen(cfg, g);
    std::vector<Flat> out;
    for (int i = 0; i < batches; ++i) {
        OrderBatch b = gen.next();
        for (const Order& o : b.orders)
            out.push_back({o.id, o.restaurantId, o.deliveryNode, b.atMs, o.readyAtMs, o.state});
    }
    return out;
}

}

int main(int argc, char** argv) {
    const Config cfg = smallCity(7, 4);
    const Graph g(cfg);

    {
        const auto a = run(cfg, g, 300);
        const auto b = run(cfg, g, 300);
        bool same = a.size() == b.size();
        for (std::size_t i = 0; same && i < a.size(); ++i)
            same = a[i].id == b[i].id && a[i].restaurant == b[i].restaurant && a[i].node == b[i].node &&
                   a[i].at == b[i].at && a[i].ready == b[i].ready && a[i].state == b[i].state;
        check(same, "la misma semilla produce exactamente la misma secuencia");

        const Config other = smallCity(8, 4);
        const auto c = run(other, g, 300);
        bool differs = c.size() != a.size();
        for (std::size_t i = 0; !differs && i < a.size(); ++i)
            differs = a[i].at != c[i].at || a[i].node != c[i].node;
        check(differs, "otra semilla produce otra secuencia");
    }

    {
        OrderGenerator gen(cfg, g);
        std::int64_t lastAt = -1;
        std::set<std::string> ids;
        int total = 0, bursts = 0, unreachable = 0;
        for (int i = 0; i < 500; ++i) {
            OrderBatch b = gen.next();
            check(b.atMs > lastAt, "los instantes de las rafagas crecen estrictamente");
            lastAt = b.atMs;
            check(!b.orders.empty() && static_cast<int>(b.orders.size()) <= 4, "tamano de rafaga entre 1 y burstMax");
            if (b.orders.size() > 1) ++bursts;
            for (const Order& o : b.orders) {
                ++total;
                check(ids.insert(o.id).second, "ids unicos");
                check(o.createdAtMs == b.atMs, "createdAt es el instante de la rafaga");
                const RestaurantConfig* r = nullptr;
                for (const auto& rc : cfg.restaurants) if (rc.id == o.restaurantId) r = &rc;
                check(r != nullptr, "el restaurante existe");
                if (r) {
                    check(o.deliveryNode != r->node, "la entrega no es el nodo del restaurante");
                    check(o.readyAtMs >= o.createdAtMs + r->prepMinMs && o.readyAtMs <= o.createdAtMs + r->prepMaxMs,
                          "la hora de listo respeta prepTimeMs");
                    const bool reach = g.reachable(r->node, o.deliveryNode);
                    if (reach) {
                        check(o.state == OrderState::Created, "pedido alcanzable nace Created");
                    } else {
                        check(o.state == OrderState::Rejected && o.rejectReason == RejectReason::Unreachable,
                              "pedido inalcanzable nace Rejected/unreachable");
                        ++unreachable;
                    }
                }
                check(g.hasNode(o.deliveryNode), "el nodo de entrega existe");
            }
        }
        check(gen.created() == total, "created() cuenta todos los pedidos");
        check(bursts > 0, "hubo rafagas de mas de un pedido");

        check(unreachable > 0, "hubo pedidos unreachable desde el restaurante del callejon");
        check(unreachable < total, "y tambien pedidos alcanzables desde el otro restaurante");
        check(ids.size() == static_cast<std::size_t>(total), "ningun id repetido");

        check(ids.count("o0") == 1, "los ids empiezan en o0");
    }

    {
        const Config one = smallCity(3, 1);
        OrderGenerator gen(one, g);
        bool allSingle = true;
        for (int i = 0; i < 200; ++i) allSingle = allSingle && gen.next().orders.size() == 1;
        check(allSingle, "con burstMax 1 cada rafaga tiene un pedido");
    }

    if (argc > 1) {
        try {
            Config city = loadConfig(argv[1]);
            validateConfig(city);
            const Graph cg(city);
            OrderGenerator gen(city, cg);
            const int batches = 3000;
            std::int64_t last = 0;
            double sum = 0;
            int orders = 0, unreachable = 0;
            for (int i = 0; i < batches; ++i) {
                OrderBatch b = gen.next();
                sum += static_cast<double>(b.atMs - last);
                last = b.atMs;
                for (const Order& o : b.orders) {
                    ++orders;
                    if (o.state == OrderState::Rejected) ++unreachable;
                }
            }
            const double mean = sum / batches;
            check(std::fabs(mean - static_cast<double>(city.orders.meanIntervalMs)) <
                      0.1 * static_cast<double>(city.orders.meanIntervalMs),
                  "la media de los intervalos esta dentro del 10 % de meanIntervalMs");
            check(unreachable > 0, "en la ciudad real aparecen pedidos unreachable");
            std::cout << "ciudad real: " << batches << " rafagas, " << orders << " pedidos, intervalo medio "
                      << mean << " ms (config " << city.orders.meanIntervalMs << "), " << unreachable
                      << " unreachable\n";
        } catch (const std::exception& e) {
            check(false, std::string("la ciudad real no se pudo cargar: ") + e.what());
        }
    }

    if (failures == 0) {
        std::cout << "order_generator_test: todas las pruebas pasaron\n";
        return 0;
    }
    std::cerr << "order_generator_test: " << failures << " prueba(s) fallaron\n";
    return 1;
}
