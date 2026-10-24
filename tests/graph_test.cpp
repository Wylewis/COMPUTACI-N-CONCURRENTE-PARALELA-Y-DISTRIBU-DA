#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>

#include "config.h"
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

Config testConfig() {
    Config cfg;
    cfg.nodes = {{"A", 0.0000, 0.0000}, {"B", 0.0000, 0.0009}, {"C", 0.0009, 0.0009},
                 {"D", 0.0009, 0.0018}, {"E", -0.0009, 0.0000}};
    cfg.streets = {{"ab", "A", "B", false}, {"bc", "B", "C", true}, {"cd", "C", "D", true},
                   {"db", "D", "B", true},  {"ae", "A", "E", true}};
    return cfg;
}

}

int main(int argc, char** argv) {

    check(near(distanceM(0, 0, 1, 0), 111'195, 50), "un grado de latitud son unos 111.2 km");
    check(near(distanceM(0, 0, 0, 0), 0, 1e-9), "la distancia de un punto a si mismo es 0");
    check(near(distanceM(-17.7625, -63.1965, -17.7640, -63.1975), 196, 3),
          "dos puntos de Equipetrol a unos 196 m");

    const Config cfg = testConfig();
    const Graph g(cfg);
    check(g.nodeCount() == 5, "5 nodos");
    check(g.edgeCount() == 6, "6 aristas: la doble mano cuenta dos");
    check(g.hasNode("A") && !g.hasNode("Z"), "hasNode");

    {
        auto r = g.shortestPath("A", "D");
        check(r.has_value(), "A llega a D");
        if (r) {
            check(r->nodes == std::vector<std::string>{"A", "B", "C", "D"}, "A->D pasa por B y C");
            check(r->streets == std::vector<std::string>{"ab", "bc", "cd"}, "A->D usa ab, bc, cd");
            const double expected = distanceM(0, 0, 0, 0.0009) + distanceM(0, 0.0009, 0.0009, 0.0009) +
                                    distanceM(0.0009, 0.0009, 0.0009, 0.0018);
            check(near(r->lengthM, expected, 1e-6), "la longitud es la suma de las aristas");
        }
    }

    {
        auto r = g.shortestPath("D", "A");
        check(r.has_value() && r->nodes == std::vector<std::string>{"D", "B", "A"}, "D->A va por B");
    }

    {
        auto r = g.shortestPath("C", "B");
        check(r.has_value() && r->nodes == std::vector<std::string>{"C", "D", "B"}, "C->B da la vuelta por D");
    }

    check(g.reachable("A", "E"), "E es alcanzable desde A");
    check(!g.reachable("E", "A"), "desde E no se sale");
    check(!g.shortestPath("E", "B").has_value(), "no hay ruta de E a B");
    check(g.reachable("B", "B"), "un nodo se alcanza a si mismo");
    {
        auto r = g.shortestPath("B", "B");
        check(r.has_value() && r->nodes.size() == 1 && r->streets.empty() && r->lengthM == 0.0,
              "la ruta de un nodo a si mismo es trivial");
    }
    check(g.reachableFrom("A").size() == 5, "desde A se alcanza todo");
    check(g.reachableFrom("E").size() == 1, "desde E solo E");

    {
        bool threw = false;
        try {
            g.shortestPath("A", "Z");
        } catch (const std::out_of_range&) {
            threw = true;
        }
        check(threw, "un id desconocido lanza out_of_range");
    }

    if (argc > 1) {
        try {
            Config city = loadConfig(argv[1]);
            validateConfig(city);
            const Graph cg(city);
            check(cg.nodeCount() == city.nodes.size(), "el grafo tiene todos los nodos del config");
            const auto fromStart = cg.reachableFrom(city.fleet.startNode);
            check(fromStart.size() * 10 >= cg.nodeCount() * 9,
                  "desde el startNode se alcanza al menos el 90 % de la ciudad");
            for (const auto& r : city.restaurants) {
                check(cg.reachable(city.fleet.startNode, r.node),
                      "el restaurante " + r.name + " es alcanzable desde el startNode");
                check(cg.reachable(r.node, city.fleet.startNode),
                      "desde " + r.name + " se vuelve al startNode");
            }

            auto r = cg.shortestPath(city.restaurants.front().node, city.restaurants.back().node);
            check(r.has_value(), "hay ruta entre el primer y el ultimo restaurante");
            if (r) {
                check(r->lengthM > 1000 && r->lengthM < 5000, "esa ruta mide entre 1 y 5 km");
                check(r->streets.size() + 1 == r->nodes.size(), "una calle por cada par de nodos");
                std::cout << "ciudad real: " << cg.nodeCount() << " nodos, " << cg.edgeCount()
                          << " aristas; " << fromStart.size() << " nodos alcanzables desde "
                          << city.fleet.startNode << "; ruta " << city.restaurants.front().name
                          << " -> " << city.restaurants.back().name << ": " << r->lengthM
                          << " m por " << r->streets.size() << " calles\n";
            }
        } catch (const std::exception& e) {
            check(false, std::string("la ciudad real no se pudo cargar: ") + e.what());
        }
    }

    if (failures == 0) {
        std::cout << "graph_test: todas las pruebas pasaron\n";
        return 0;
    }
    std::cerr << "graph_test: " << failures << " prueba(s) fallaron\n";
    return 1;
}
