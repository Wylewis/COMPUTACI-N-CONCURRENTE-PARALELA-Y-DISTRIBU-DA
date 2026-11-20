#include <algorithm>
#include <iostream>
#include <random>
#include <string>
#include <vector>

#include "gpu/travel_table.h"

namespace {

int failures = 0;

void check(bool ok, const std::string& what) {
    if (!ok) {
        ++failures;
        std::cerr << "FALLO: " << what << "\n";
    }
}

CsrGraph fromEdges(int n, std::vector<std::vector<std::pair<int, int>>> adjacency) {
    CsrGraph g;
    g.n = n;
    g.rowStart.assign(static_cast<std::size_t>(n) + 1, 0);
    for (int u = 0; u < n; ++u) {
        for (const auto& [v, w] : adjacency[static_cast<std::size_t>(u)]) {
            g.to.push_back(v);
            g.weightMs.push_back(w);
        }
        g.rowStart[static_cast<std::size_t>(u) + 1] = static_cast<int>(g.to.size());
    }
    return g;
}

CsrGraph randomCity(int n, unsigned seed) {
    std::mt19937 rng(seed);
    std::uniform_int_distribution<int> weight(1, 900), node(0, n - 2), extra(0, 2);
    std::vector<std::vector<std::pair<int, int>>> adjacency(static_cast<std::size_t>(n));
    for (int u = 0; u + 1 < n - 1; ++u) adjacency[static_cast<std::size_t>(u)].push_back({u + 1, weight(rng)});
    adjacency[static_cast<std::size_t>(n - 2)].push_back({0, weight(rng)});
    for (int u = 0; u < n - 1; ++u)
        for (int k = extra(rng); k > 0; --k) adjacency[static_cast<std::size_t>(u)].push_back({node(rng), weight(rng)});
    adjacency[static_cast<std::size_t>(n - 1)].push_back({0, weight(rng)});
    return fromEdges(n, adjacency);
}

}

int main() {
    {
        const CsrGraph g = fromEdges(3, {{{1, 100}}, {{2, 50}}, {}});
        const std::vector<int> cpu = travelTableCpu(g);
        int rounds = 0;
        const std::vector<int> gpu = travelTableGpu(g, nullptr, &rounds);
        const int U = kUnreachableMs;
        check(cpu == std::vector<int>{0, 100, 150, U, 0, 50, U, U, 0},
              "linea de un sentido A-B-C: Dijkstra da 100, 150 y 50, y la vuelta es inalcanzable");
        check(gpu == cpu && rounds >= 1, "el producto min-plus en tiles da la misma tabla");
    }
    for (const int n : {17, 37, 64}) {
        const CsrGraph g = randomCity(n, static_cast<unsigned>(n));
        const std::vector<int> cpu = travelTableCpu(g);
        ThreadPool pool(4);
        GpuTimes times;
        int rounds = 0;
        const std::vector<int> gpu = travelTableGpu(g, &times, &rounds);
        const std::string tag = "n = " + std::to_string(n) + ": ";
        check(travelTableCpuParallel(g, pool) == cpu, tag + "Dijkstra en paralelo da lo mismo que en un hilo");
        check(gpu == cpu, tag + "la tabla de GPU es identica a Dijkstra desde cada nodo");
        bool diagonalZero = true;
        for (int i = 0; i < n; ++i) diagonalZero = diagonalZero && gpu[static_cast<std::size_t>(i) * n + i] == 0;
        check(diagonalZero, tag + "la diagonal es cero");
        const TravelTable table(n, gpu);
        check(table.unreachablePairs() == static_cast<std::size_t>(n - 1) && table.ms(0, n - 1) == kUnreachableMs &&
                  table.ms(n - 1, 0) < kUnreachableMs,
              tag + "el nodo que solo tiene salida queda inalcanzable para los demas y marcado");
        check(times.kernelMs >= 0.0 && times.totalMs >= times.kernelMs && rounds >= 2,
              tag + "varias elevaciones al cuadrado y tiempos coherentes");
    }
    {
        CsrGraph empty;
        empty.rowStart = {0};
        check(travelTableGpu(empty).empty() && travelTableCpu(empty).empty(), "un grafo vacio da una tabla vacia");
    }

    if (failures == 0) {
        std::cout << "travel_table_test: todas las pruebas pasaron\n";
        return 0;
    }
    std::cerr << "travel_table_test: " << failures << " prueba(s) fallaron\n";
    return 1;
}
