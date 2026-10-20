// Experimento de la tabla de distancias: construccion en serie frente a la
// paralela (std::async por grupo de origenes) para distintos numeros de
// origenes, y barrido del numero de tareas para la ciudad completa. Imprime
// tablas Markdown. Uso: distance_bench <config.json> [repeticiones]
#include <chrono>
#include <iostream>
#include <string>
#include <vector>

#include "distance_table.h"
#include "distance_table_parallel.h"
#include "hardware.h"

using clk = std::chrono::steady_clock;

template <class F>
double millisPer(int reps, F&& f) {
    f();
    const auto t0 = clk::now();
    for (int i = 0; i < reps; ++i) f();
    return std::chrono::duration<double, std::milli>(clk::now() - t0).count() / reps;
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "uso: distance_bench <config.json> [repeticiones]\n";
        return 2;
    }
    const Config cfg = loadConfig(argv[1]);
    const int reps = argc > 2 ? std::stoi(argv[2]) : 5;
    const Graph g(cfg);
    std::vector<std::string> all;
    for (const auto& n : cfg.nodes) all.push_back(n.id);

    std::cout << "| origenes | serie (ms) | paralelo (ms) | tareas | speedup | por origen en serie (us) |\n|---|---|---|---|---|---|\n";
    for (int n : {9, 20, 40, 80, 160, 320}) {
        std::vector<std::string> origins(all.begin(), all.begin() + std::min<std::size_t>(n, all.size()));
        if (n == 9) {
            origins.clear();
            for (const auto& r : cfg.restaurants) origins.push_back(r.node);
        }
        const double serial = millisPer(reps, [&] { DistanceTable(g, origins); });
        const unsigned tasks = defaultTaskCount(origins.size());
        const double par = millisPer(reps, [&] { buildDistanceTableParallel(g, origins, tasks); });
        std::cout << "| " << origins.size() << " | " << serial << " | " << par << " | " << tasks << " | " << serial / par << " | "
                  << serial * 1000 / origins.size() << " |\n";
    }
    std::cout << "\nBarrido de tareas con los " << all.size() << " nodos como origenes:\n\n| tareas | paralelo (ms) | speedup frente a la serie |\n|---|---|---|\n";
    const double serialAll = millisPer(reps, [&] { DistanceTable(g, all); });
    for (unsigned t : {1u, 2u, 4u, 8u, 12u, 18u, 36u}) {
        const double par = millisPer(reps, [&] { buildDistanceTableParallel(g, all, t); });
        std::cout << "| " << t << " | " << par << " | " << serialAll / par << " |\n";
    }
    std::cout << "\nhardware: " << hardwareThreads() << " hilos; " << reps << " repeticiones por celda; ciudad de " << all.size() << " nodos\n";
    return 0;
}
