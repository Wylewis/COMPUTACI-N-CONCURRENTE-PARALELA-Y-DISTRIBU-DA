#include <iostream>
#include <numeric>
#include <random>
#include <string>
#include <vector>

#include "gpu/delivery_histogram.h"

namespace {

int failures = 0;

void check(bool ok, const std::string& what) {
    if (!ok) {
        ++failures;
        std::cerr << "FALLO: " << what << "\n";
    }
}

unsigned long long totalOf(const std::vector<unsigned>& counts) {
    return std::accumulate(counts.begin(), counts.end(), 0ULL);
}

std::vector<int> deliveryTimes(std::size_t n, unsigned seed, bool concentrated) {
    std::mt19937 rng(seed);
    std::vector<int> ms(n);
    for (int& v : ms)
        v = concentrated ? 25 * 60'000 + static_cast<int>(rng() % 60'000u)
                         : static_cast<int>(rng() % (130u * 60'000u));
    return ms;
}

}

int main() {
    {
        const std::vector<int> ms{0, 59'999, 60'000, 61'000, 125'000, 600'000, -5, 9'000'000};
        const std::vector<unsigned> expected{3, 2, 1, 2};
        ThreadPool pool(4);
        const std::vector<unsigned> cpu = deliveryHistogramCpu(ms.data(), ms.size(), 4);
        check(cpu == expected, "a mano: un tramo por minuto, lo que pasa del ultimo tramo cae en el ultimo y lo negativo en el primero");
        check(deliveryHistogramCpuParallel(ms.data(), ms.size(), 4, pool) == expected, "a mano: multihilo igual a un hilo");
        check(deliveryHistogramGpu(ms.data(), ms.size(), 4, HistogramMode::Privatized) == expected,
              "a mano: la tarjeta con contadores privados da lo mismo");
        check(deliveryHistogramGpu(ms.data(), ms.size(), 4, HistogramMode::Atomic) == expected,
              "a mano: la tarjeta con atomicos globales da lo mismo");
        check(histogramPercentileBin(expected, 0.5) == 1 && histogramPercentileBin(expected, 0.99) == 3 &&
                  histogramPercentileBin({0, 0, 0}, 0.5) == -1,
              "el percentil es el primer tramo donde el acumulado llega a la fraccion pedida");
    }
    for (const std::size_t n : {1u, 255u, 16'384u, 16'385u, 40'000u}) {
        for (const int bins : {1, 120, 256}) {
            for (const bool concentrated : {false, true}) {
                const std::vector<int> ms = deliveryTimes(n, static_cast<unsigned>(n) + static_cast<unsigned>(bins), concentrated);
                ThreadPool pool(4);
                const std::string tag = std::to_string(n) + " entregas, " + std::to_string(bins) + " tramos" +
                                        (concentrated ? ", todas en el mismo minuto: " : ": ");
                const std::vector<unsigned> cpu = deliveryHistogramCpu(ms.data(), n, bins);
                GpuTimes times;
                check(totalOf(cpu) == n, tag + "el total de un hilo es el numero de entregas");
                check(deliveryHistogramCpuParallel(ms.data(), n, bins, pool) == cpu, tag + "multihilo igual a un hilo");
                check(deliveryHistogramGpu(ms.data(), n, bins, HistogramMode::Privatized, &times) == cpu,
                      tag + "contadores privados en la tarjeta igual a la CPU");
                check(deliveryHistogramGpu(ms.data(), n, bins, HistogramMode::Atomic) == cpu,
                      tag + "atomicos globales en la tarjeta igual a la CPU");
                check(times.totalMs >= times.kernelMs, tag + "el tiempo total incluye al del kernel");
            }
        }
    }
    {
        const std::vector<int> ms{1, 2, 3};
        check(deliveryHistogramGpu(ms.data(), 0, 10) == std::vector<unsigned>(10, 0), "sin entregas todos los tramos quedan en cero");
        check(deliveryHistogramCpu(ms.data(), 3, 1000).size() == 256 && deliveryHistogramGpu(ms.data(), 3, 1000).size() == 256,
              "mas de 256 tramos se recortan a 256 en CPU y en la tarjeta");
    }

    if (failures == 0) {
        std::cout << "delivery_histogram_test: todas las pruebas pasaron\n";
        return 0;
    }
    std::cerr << "delivery_histogram_test: " << failures << " prueba(s) fallaron\n";
    return 1;
}
