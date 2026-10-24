#include <chrono>
#include <cmath>
#include <iostream>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

#include "config.h"
#include "distance_table.h"
#include "graph.h"

namespace {

int failures = 0;

void check(bool ok, const std::string& what) {
    if (!ok) {
        ++failures;
        std::cerr << "FALLO: " << what << "\n";
    }
}

bool near(double a, double b, double tol) {
    return std::fabs(a - b) <= tol;
}

Config smallCity() {
    Config cfg;
    cfg.nodes = {{"A", 0.0000, 0.0000}, {"B", 0.0000, 0.0009}, {"C", 0.0009, 0.0009},
                 {"D", 0.0009, 0.0018}, {"E", -0.0009, 0.0000}, {"F", 0.0009, 0.0000}};
    cfg.streets = {{"ab", "A", "B", false}, {"bc", "B", "C", true}, {"cd", "C", "D", true},
                   {"db", "D", "B", true},  {"ae", "A", "E", true}, {"fa", "F", "A", true}};
    return cfg;
}

}

int main(int argc, char** argv) {
    const Config cfg = smallCity();
    const Graph g(cfg);
    const DistanceTable table(g, {"A", "E"});

    check(table.originCount() == 2 && table.nodeCount() == 6, "dos origenes, seis nodos");
    check(table.hasOrigin("A") && !table.hasOrigin("B"), "hasOrigin");

    {
        const double ab = distanceM(0, 0, 0, 0.0009);
        const double bc = distanceM(0, 0.0009, 0.0009, 0.0009);
        const double cd = distanceM(0.0009, 0.0009, 0.0009, 0.0018);
        check(near(*table.distanceM("A", "A"), 0.0, 1e-9), "A a A es 0");
        check(near(*table.distanceM("A", "B"), ab, 1e-6), "A a B es la arista ab");
        check(near(*table.distanceM("A", "D"), ab + bc + cd, 1e-6), "A a D suma ab, bc, cd");
        check(table.distanceM("A", "E").has_value(), "A llega a E");
        check(!table.distanceM("A", "F").has_value(), "A no llega a F");
        check(table.reachableCount("A") == 5, "desde A se alcanzan 5 de 6");
    }

    {
        check(near(*table.distanceM("E", "E"), 0.0, 1e-9), "E a E es 0");
        check(!table.distanceM("E", "A").has_value(), "de E no se sale");
        check(table.reachableCount("E") == 1, "desde E solo E");
    }

    {
        bool same = true;
        for (const auto& n : cfg.nodes) {
            const auto r = g.shortestPath("A", n.id);
            const auto d = table.distanceM("A", n.id);
            if (r.has_value() != d.has_value()) same = false;
            else if (r && !near(r->lengthM, *d, 1e-6)) same = false;
        }
        check(same, "la fila de A coincide con shortestPath nodo a nodo");
    }

    {
        check(!table.distanceM("B", "A").has_value(), "un origen sin fila devuelve nullopt");
        bool threw = false;
        try { table.distanceM("A", "Z"); } catch (const std::out_of_range&) { threw = true; }
        check(threw, "un destino inexistente lanza");
        threw = false;
        try { table.row("B"); } catch (const std::out_of_range&) { threw = true; }
        check(threw, "row() de un origen sin fila lanza");
    }

    {
        std::vector<DistanceTable::Row> rows{DistanceTable::computeRow(g, "A"), DistanceTable::computeRow(g, "E")};
        const DistanceTable fromRows(g, {"A", "E"}, rows);
        check(near(*fromRows.distanceM("A", "D"), *table.distanceM("A", "D"), 1e-9), "la tabla desde filas es igual a la serial");
        bool threw = false;
        try { DistanceTable bad(g, {"A"}, rows); } catch (const std::invalid_argument&) { threw = true; }
        check(threw, "filas y origenes de distinto tamano se rechazan");
    }

    if (argc > 1) {
        try {
            Config city = loadConfig(argv[1]);
            validateConfig(city);
            const Graph cg(city);
            std::vector<std::string> origins;
            for (const auto& r : city.restaurants) origins.push_back(r.node);

            const auto t0 = std::chrono::steady_clock::now();
            const DistanceTable ct(cg, origins);
            const auto us = std::chrono::duration_cast<std::chrono::microseconds>(
                                std::chrono::steady_clock::now() - t0).count();

            std::mt19937 rng(7);
            std::uniform_int_distribution<std::size_t> pick(0, city.nodes.size() - 1);
            bool same = true;
            for (int i = 0; i < 200 && same; ++i) {
                const std::string& o = origins[i % origins.size()];
                const std::string& t = city.nodes[pick(rng)].id;
                const auto r = cg.shortestPath(o, t);
                const auto d = ct.distanceM(o, t);
                if (r.has_value() != d.has_value()) same = false;
                else if (r && !near(r->lengthM, *d, 1e-6)) same = false;
            }
            check(same, "en la ciudad real la tabla coincide con shortestPath en 200 pares");
            std::size_t minReach = city.nodes.size();
            for (const auto& o : origins) minReach = std::min(minReach, ct.reachableCount(o));
            check(minReach * 10 >= city.nodes.size() * 9, "cada restaurante alcanza al menos el 90 % de los nodos");
            std::cout << "ciudad real: " << origins.size() << " filas de " << cg.nodeCount() << " nodos en " << us
                      << " us (serial); alcance minimo " << minReach << " nodos\n";
        } catch (const std::exception& e) {
            check(false, std::string("la ciudad real no se pudo cargar: ") + e.what());
        }
    }

    if (failures == 0) {
        std::cout << "distance_table_test: todas las pruebas pasaron\n";
        return 0;
    }
    std::cerr << "distance_table_test: " << failures << " prueba(s) fallaron\n";
    return 1;
}
