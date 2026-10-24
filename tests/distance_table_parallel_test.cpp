#include <chrono>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "config.h"
#include "distance_table.h"
#include "distance_table_parallel.h"
#include "graph.h"

namespace {

int failures = 0;

void check(bool ok, const std::string& what) {
    if (!ok) {
        ++failures;
        std::cerr << "FALLO: " << what << "\n";
    }
}

bool sameTables(const DistanceTable& a, const DistanceTable& b, const std::vector<std::string>& origins) {
    if (a.originCount() != b.originCount() || a.nodeCount() != b.nodeCount()) return false;
    for (const auto& o : origins) {
        const auto& ra = a.row(o);
        const auto& rb = b.row(o);
        if (ra.size() != rb.size()) return false;
        for (std::size_t i = 0; i < ra.size(); ++i) {
            const bool ia = ra[i] == DistanceTable::kUnreachable, ib = rb[i] == DistanceTable::kUnreachable;
            if (ia != ib) return false;
            if (!ia && std::fabs(ra[i] - rb[i]) > 1e-9) return false;
        }
    }
    return true;
}

Config smallCity() {
    Config cfg;
    cfg.nodes = {{"A", 0.0000, 0.0000}, {"B", 0.0000, 0.0009}, {"C", 0.0009, 0.0009},
                 {"D", 0.0009, 0.0018}, {"E", -0.0009, 0.0000}, {"F", 0.0009, 0.0000}};
    cfg.streets = {{"ab", "A", "B", false}, {"bc", "B", "C", true}, {"cd", "C", "D", true},
                   {"db", "D", "B", true},  {"ae", "A", "E", true}, {"fa", "F", "A", true}};
    return cfg;
}

template <class F>
long long microsOf(F&& f) {
    const auto t0 = std::chrono::steady_clock::now();
    f();
    return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - t0).count();
}

}

int main(int argc, char** argv) {

    {
        const unsigned cores = std::thread::hardware_concurrency();
        const unsigned expect = cores == 0 ? 2 : cores;
        check(defaultTaskCount(1000) == expect, "con mucho trabajo, una tarea por nucleo");
        check(defaultTaskCount(3) == std::min(3u, expect), "nunca mas tareas que trabajo");
        check(defaultTaskCount(0) == 1, "sin trabajo, una tarea");
    }

    const Config cfg = smallCity();
    const Graph g(cfg);
    const std::vector<std::string> all{"A", "B", "C", "D", "E", "F"};
    const DistanceTable serial(g, all);

    check(sameTables(serial, buildDistanceTableParallel(g, all), all), "paralelo por defecto = serial");
    check(sameTables(serial, buildDistanceTableParallel(g, all, 1), all), "con 1 tarea = serial");
    check(sameTables(serial, buildDistanceTableParallel(g, all, 4), all), "con 4 tareas = serial");
    check(sameTables(serial, buildDistanceTableParallel(g, all, 50), all), "con mas tareas que origenes = serial");
    {
        const std::vector<std::string> none;
        const DistanceTable empty = buildDistanceTableParallel(g, none);
        check(empty.originCount() == 0, "sin origenes, tabla vacia");
    }

    {
        bool threw = false;
        try {
            buildDistanceTableParallel(g, {"A", "Z", "B"}, 3);
        } catch (const std::out_of_range&) {
            threw = true;
        }
        check(threw, "un origen desconocido en una tarea se propaga como excepcion");
    }

    if (argc > 1) {
        try {
            Config city = loadConfig(argv[1]);
            validateConfig(city);
            const Graph cg(city);

            std::vector<std::string> restaurants;
            for (const auto& r : city.restaurants) restaurants.push_back(r.node);
            std::vector<std::string> nodes;
            for (const auto& n : city.nodes) nodes.push_back(n.id);

            const DistanceTable sr(cg, restaurants);
            check(sameTables(sr, buildDistanceTableParallel(cg, restaurants), restaurants), "ciudad real, 9 origenes: paralelo = serial");
            const DistanceTable sn(cg, nodes);
            check(sameTables(sn, buildDistanceTableParallel(cg, nodes), nodes), "ciudad real, 320 origenes: paralelo = serial");

            auto best = [](auto&& f) {
                long long b = -1;
                for (int i = 0; i < 5; ++i) {
                    const long long us = microsOf(f);
                    if (b < 0 || us < b) b = us;
                }
                return b;
            };
            const long long s9 = best([&] { DistanceTable t(cg, restaurants); });
            const long long p9 = best([&] { buildDistanceTableParallel(cg, restaurants); });
            const long long s320 = best([&] { DistanceTable t(cg, nodes); });
            const long long p320 = best([&] { buildDistanceTableParallel(cg, nodes); });
            std::cout << "ciudad real, " << defaultTaskCount(nodes.size()) << " tareas por defecto\n"
                      << "  9 origenes:   serial " << s9 << " us, paralelo " << p9 << " us, speedup "
                      << (p9 ? static_cast<double>(s9) / p9 : 0.0) << "x\n"
                      << "  320 origenes: serial " << s320 << " us, paralelo " << p320 << " us, speedup "
                      << (p320 ? static_cast<double>(s320) / p320 : 0.0) << "x\n";
        } catch (const std::exception& e) {
            check(false, std::string("la ciudad real no se pudo cargar: ") + e.what());
        }
    }

    if (failures == 0) {
        std::cout << "distance_table_parallel_test: todas las pruebas pasaron\n";
        return 0;
    }
    std::cerr << "distance_table_parallel_test: " << failures << " prueba(s) fallaron\n";
    return 1;
}
