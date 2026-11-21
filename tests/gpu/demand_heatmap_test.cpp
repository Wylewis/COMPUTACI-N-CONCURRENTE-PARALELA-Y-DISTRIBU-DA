#include <cmath>
#include <iostream>
#include <numeric>
#include <random>
#include <string>
#include <vector>

#include "gpu/demand_heatmap.h"

namespace {

int failures = 0;

void check(bool ok, const std::string& what) {
    if (!ok) {
        ++failures;
        std::cerr << "FALLO: " << what << "\n";
    }
}

float sumOf(const std::vector<float>& v) { return std::accumulate(v.begin(), v.end(), 0.0f); }

}

int main() {
    const std::vector<float> weights = gaussianWeights();
    {
        check(weights.size() == 49 && std::fabs(sumOf(weights) - 1.0f) < 1e-5f, "los 49 pesos suman uno");
        const float center = weights[24];
        bool peaked = true, symmetric = true;
        for (std::size_t i = 0; i < weights.size(); ++i) {
            peaked = peaked && weights[i] <= center && weights[i] > 0.0f;
            symmetric = symmetric && std::fabs(weights[i] - weights[48 - i]) < 1e-7f;
        }
        check(peaked && symmetric, "el peso mayor esta en el centro y el filtro es simetrico");
    }
    {
        const std::vector<float> x{0.05f, 0.05f, 0.55f, 0.99f, 1.0f, -0.1f};
        const std::vector<float> y{0.05f, 0.05f, 0.30f, 0.99f, 0.5f, 0.5f};
        const std::vector<float> counts = binDemand(x.data(), y.data(), x.size(), 4);
        check(counts[0] == 2.0f && counts[1 * 4 + 2] == 1.0f && counts[3 * 4 + 3] == 1.0f && sumOf(counts) == 4.0f,
              "cada pedido cae en su celda y los que estan fuera del mapa no se cuentan");
    }
    {
        const int grid = 21;
        std::vector<float> counts(static_cast<std::size_t>(grid * grid), 0.0f);
        counts[static_cast<std::size_t>(10 * grid + 10)] = 1.0f;
        const std::vector<float> cpu = smoothCpu(counts.data(), grid, weights.data());
        bool copy = true;
        for (int dy = -3; dy <= 3; ++dy)
            for (int dx = -3; dx <= 3; ++dx)
                copy = copy && cpu[static_cast<std::size_t>((10 + dy) * grid + 10 + dx)] ==
                                   weights[static_cast<std::size_t>((3 - dy) * 7 + 3 - dx)];
        check(copy && cpu[0] == 0.0f && std::fabs(sumOf(cpu) - 1.0f) < 1e-5f && hottestCell(cpu) == 10u * 21u + 10u,
              "un solo pedido en el centro deja la forma del filtro alrededor y conserva el total");
        check(heatmapsMatch(cpu, smoothGpu(counts.data(), grid, weights.data())), "la tarjeta da el mismo punto suavizado");
    }
    for (const int grid : {1, 5, 16, 17, 40}) {
        std::mt19937 rng(static_cast<unsigned>(grid));
        std::vector<float> counts(static_cast<std::size_t>(grid * grid));
        for (float& c : counts) c = rng() % 4u == 0 ? static_cast<float>(rng() % 9u) : 0.0f;
        ThreadPool pool(4);
        const std::string tag = "cuadricula de " + std::to_string(grid) + " x " + std::to_string(grid) + ": ";
        const std::vector<float> cpu = smoothCpu(counts.data(), grid, weights.data());
        GpuTimes times;
        check(smoothCpuParallel(counts.data(), grid, weights.data(), pool) == cpu, tag + "multihilo identico a un hilo");
        check(heatmapsMatch(cpu, smoothGpu(counts.data(), grid, weights.data(), HeatWeights::Constant, &times)),
              tag + "tarjeta con pesos en memoria constante igual a la CPU dentro de la tolerancia");
        check(heatmapsMatch(cpu, smoothGpu(counts.data(), grid, weights.data(), HeatWeights::Global)),
              tag + "tarjeta con pesos en memoria global igual a la CPU dentro de la tolerancia");
        check(sumOf(cpu) <= sumOf(counts) * (1.0f + 1e-4f), tag + "suavizar no agrega demanda: en los bordes solo se pierde");
        check(times.totalMs >= times.kernelMs, tag + "el tiempo total incluye al del kernel");
    }
    {
        std::vector<float> a{1.0f, 100.0f}, b{1.00005f, 100.005f}, c{1.01f, 100.0f};
        check(heatmapsMatch(a, b) && !heatmapsMatch(a, c) && !heatmapsMatch(a, {1.0f}),
              "la comparacion acepta diferencias de una parte en diez mil y rechaza las mayores");
        check(smoothGpu(nullptr, 0, weights.data()).empty(), "una cuadricula vacia da un resultado vacio");
    }

    if (failures == 0) {
        std::cout << "demand_heatmap_test: todas las pruebas pasaron\n";
        return 0;
    }
    std::cerr << "demand_heatmap_test: " << failures << " prueba(s) fallaron\n";
    return 1;
}
