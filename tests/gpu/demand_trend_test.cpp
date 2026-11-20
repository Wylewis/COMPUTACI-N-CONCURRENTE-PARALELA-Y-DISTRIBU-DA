#include <cmath>
#include <cstdint>
#include <iostream>
#include <numeric>
#include <random>
#include <string>
#include <vector>

#include "gpu/demand_trend.h"

namespace {

int failures = 0;

void check(bool ok, const std::string& what) {
    if (!ok) {
        ++failures;
        std::cerr << "FALLO: " << what << "\n";
    }
}

double totalOf(const std::vector<float>& v) { return std::accumulate(v.begin(), v.end(), 0.0); }

}

int main() {
    const std::vector<float> weights = trendWeights();
    {
        bool symmetric = true, falling = true;
        for (int k = 0; k < kTrendRadius; ++k) {
            symmetric = symmetric && weights[static_cast<std::size_t>(k)] == weights[static_cast<std::size_t>(kTrendDim - 1 - k)];
            falling = falling && weights[static_cast<std::size_t>(k)] < weights[static_cast<std::size_t>(k + 1)];
        }
        check(weights.size() == 15 && std::fabs(totalOf(weights) - 1.0) < 1e-5 && symmetric && falling,
              "los 15 pesos suman uno, son simetricos y el del centro es el mayor");
    }
    {
        const std::vector<float> perMinute = ordersPerSlot({0, 59'999, 60'000, 185'000, 185'001, 190'000});
        check(perMinute == std::vector<float>{2.0f, 1.0f, 0.0f, 3.0f}, "cada pedido suma uno en el minuto en que se creo");
        check(ordersPerSlot({}).empty() && ordersPerSlot({500}, 100).size() == 6,
              "sin pedidos no hay serie, y el ancho del tramo se puede cambiar");
    }
    {
        std::vector<float> counts(41, 0.0f);
        counts[20] = 6.0f;
        const std::vector<float> cpu = trendCpu(counts.data(), counts.size(), weights.data());
        bool shape = true;
        for (int k = -kTrendRadius; k <= kTrendRadius; ++k)
            shape = shape && cpu[static_cast<std::size_t>(20 + k)] == 6.0f * weights[static_cast<std::size_t>(kTrendRadius - k)];
        check(shape && cpu[12] == 0.0f && cpu[28] == 0.0f, "un solo minuto con pedidos deja la forma de los pesos y nada mas alla");
        check(std::fabs(totalOf(cpu) - 6.0) < 1e-4, "lejos del borde, suavizar no agrega ni quita pedidos");
        check(trendPeak(cpu) == 20, "el pico de la tendencia queda en el mismo minuto");
        check(trendsMatch(cpu, trendGpu(counts.data(), counts.size(), weights.data(), TrendKernel::Global)) &&
                  trendsMatch(cpu, trendGpu(counts.data(), counts.size(), weights.data(), TrendKernel::Tiled)),
              "los dos kernels dan la misma forma");
        std::vector<float> edge(41, 0.0f);
        edge[0] = 6.0f;
        const std::vector<float> atEdge = trendCpu(edge.data(), edge.size(), weights.data());
        check(atEdge[0] == 6.0f * weights[static_cast<std::size_t>(kTrendRadius)] && totalOf(atEdge) < 6.0 &&
                  trendsMatch(atEdge, trendGpu(edge.data(), edge.size(), weights.data(), TrendKernel::Tiled)),
              "en el borde lo que cae fuera se pierde, no se inventa");
    }
    for (const std::size_t n : {1u, 7u, 14u, 15u, 255u, 256u, 257u, 512u, 1'000u, 5'000u}) {
        std::mt19937 rng(static_cast<unsigned>(n));
        std::vector<float> counts(n);
        for (float& c : counts) c = static_cast<float>(rng() % 30u);
        ThreadPool pool(4);
        const std::string tag = std::to_string(n) + " minutos: ";
        const std::vector<float> cpu = trendCpu(counts.data(), n, weights.data());
        GpuTimes times;
        check(trendCpuParallel(counts.data(), n, weights.data(), pool) == cpu, tag + "multihilo identico a un hilo");
        check(trendsMatch(cpu, trendGpu(counts.data(), n, weights.data(), TrendKernel::Global, &times)),
              tag + "tarjeta leyendo de memoria global dentro de la tolerancia");
        check(trendsMatch(cpu, trendGpu(counts.data(), n, weights.data(), TrendKernel::Tiled)),
              tag + "tarjeta con tile en shared memory dentro de la tolerancia");
        check(totalOf(cpu) <= totalOf(counts) * 1.0001 + 1e-3, tag + "suavizar no agrega pedidos");
        check(times.totalMs >= times.kernelMs, tag + "el tiempo total incluye al del kernel");
    }
    {
        ThreadPool pool(2);
        check(trendGpu(nullptr, 0, weights.data()).empty() && trendCpu(nullptr, 0, weights.data()).empty() &&
                  trendCpuParallel(nullptr, 0, weights.data(), pool).empty(),
              "una serie vacia da un resultado vacio");
        check(!trendsMatch({1.0f, 2.0f}, {1.0f}) && !trendsMatch({1.0f}, {1.01f}) && trendsMatch({100.0f}, {100.005f}),
              "la comparacion acepta una parte en diez mil y nada mas");
    }

    if (failures == 0) {
        std::cout << "demand_trend_test: todas las pruebas pasaron\n";
        return 0;
    }
    std::cerr << "demand_trend_test: " << failures << " prueba(s) fallaron\n";
    return 1;
}
