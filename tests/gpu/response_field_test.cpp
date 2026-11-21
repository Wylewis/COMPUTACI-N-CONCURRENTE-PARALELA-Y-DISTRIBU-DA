#include <iostream>
#include <random>
#include <string>
#include <vector>

#include "gpu/response_field.h"

namespace {

int failures = 0;

void check(bool ok, const std::string& what) {
    if (!ok) {
        ++failures;
        std::cerr << "FALLO: " << what << "\n";
    }
}

CoverageMap openMap(int grid, int stepX, int stepY) {
    CoverageMap map;
    map.grid = grid;
    map.stepXMs = stepX;
    map.stepYMs = stepY;
    map.open.assign(static_cast<std::size_t>(grid) * static_cast<std::size_t>(grid), 1);
    return map;
}

}

int main() {
    {
        const CoverageMap map = openMap(4, 1, 1);
        const std::vector<float> x{0.05f, 0.05f, 0.80f, 0.80f, 1.5f};
        const std::vector<float> y{0.05f, 0.05f, 0.80f, 0.80f, 0.5f};
        const std::vector<int> wait{9000, 4000, 0, 7000, 100};
        const std::vector<int> seed = responseSeed(map, x.data(), y.data(), wait.data(), x.size());
        int sources = 0;
        for (const int v : seed) sources += v < kCoverFar;
        check(seed[0] == 4000 && seed[3 * 4 + 3] == 0 && sources == 2,
              "cada celda arranca con la espera de su moto mas pronta, y una moto libre vale cero");
    }
    {
        const int grid = 11;
        const CoverageMap map = openMap(grid, 10, 10);
        std::vector<int> seed(static_cast<std::size_t>(grid * grid), kCoverFar);
        seed[0] = 0;
        seed[static_cast<std::size_t>(grid * grid - 1)] = 35;
        const std::vector<int> cpu = coverageCpu(map, seed.data());
        check(cpu[static_cast<std::size_t>(grid * grid - 2)] == 45 && cpu[1] == 10 && cpu[static_cast<std::size_t>(5 * grid + 5)] == 100,
              "una moto ocupada cubre su zona recien despues de su espera, y el centro lo gana la que llega antes");
        check(coverageDijkstra(map, seed.data()) == cpu, "con motos ocupadas, Dijkstra da el mismo campo");
        check(responseFieldGpu(map, seed.data(), ResponseKernel::ManySweeps) == cpu &&
                  responseFieldGpu(map, seed.data(), ResponseKernel::OneSweep) == cpu,
              "los dos kernels dan el mismo campo");
        seed[static_cast<std::size_t>(grid * grid - 1)] = kCoverFar;
        const std::vector<int> onlyFree = coverageCpu(map, seed.data());
        bool neverWorse = true;
        for (std::size_t p = 0; p < cpu.size(); ++p) neverWorse = neverWorse && cpu[p] <= onlyFree[p];
        check(neverWorse && worstCovered(map, cpu).ms < worstCovered(map, onlyFree).ms,
              "contar las motos ocupadas nunca empeora una zona y aqui mejora la peor");
    }
    for (const int grid : {1, 7, 15, 16, 17, 31, 33, 48, 70}) {
        std::mt19937 rng(static_cast<unsigned>(grid) + 12u);
        CoverageMap map = openMap(grid, 7, 9);
        for (unsigned char& o : map.open) o = rng() % 5u != 0 ? 1 : 0;
        std::vector<int> seed(map.open.size(), kCoverFar);
        for (int& s : seed)
            if (rng() % 60u == 0) s = rng() % 2u == 0 ? 0 : static_cast<int>(rng() % 500u);
        seed[rng() % seed.size()] = 0;
        ThreadPool pool(4);
        const std::string tag = "cuadricula de " + std::to_string(grid) + " x " + std::to_string(grid) + ": ";
        int sweepsCpu = 0, sweepsMany = 0, sweepsOne = 0, launchesMany = 0, launchesOne = 0;
        const std::vector<int> expected = coverageDijkstra(map, seed.data());
        GpuTimes times;
        check(coverageCpu(map, seed.data(), &sweepsCpu) == expected, tag + "las pasadas de un hilo llegan al mismo campo que Dijkstra");
        check(coverageCpuParallel(map, seed.data(), pool) == expected, tag + "multihilo identico a un hilo");
        check(responseFieldGpu(map, seed.data(), ResponseKernel::ManySweeps, &times, &sweepsMany, &launchesMany) == expected,
              tag + "varias pasadas por lanzamiento identico a la CPU");
        check(responseFieldGpu(map, seed.data(), ResponseKernel::OneSweep, nullptr, &sweepsOne, &launchesOne) == expected,
              tag + "una pasada por lanzamiento identico a la CPU");
        check(sweepsMany % kCoverCheckEvery == 0 && sweepsMany >= sweepsOne && sweepsMany <= sweepsOne + kCoverCheckEvery &&
                  sweepsOne >= sweepsCpu,
              tag + "los dos kernels paran en una revision, y el de varias pasadas a lo sumo una revision despues");
        check(launchesMany == sweepsMany / kResponseSweepsPerLaunch + sweepsMany / kCoverCheckEvery &&
                  launchesOne == sweepsOne + sweepsOne / kCoverCheckEvery && launchesMany < launchesOne,
              tag + "con 8 pasadas por lanzamiento hacen falta muchos menos lanzamientos");
        check(times.totalMs >= times.kernelMs, tag + "el tiempo total incluye al de las pasadas");
    }
    {
        const CoverageMap map = openMap(6, 4, 4);
        const std::vector<int> seed(36, kCoverFar);
        int sweeps = -1;
        check(responseFieldGpu(map, seed.data(), ResponseKernel::ManySweeps, nullptr, &sweeps) == seed && sweeps == kCoverCheckEvery,
              "sin motos el campo queda sin cubrir y la tarjeta para en la primera revision");
        check(responseFieldGpu(CoverageMap{}, nullptr).empty() && responseSeed(CoverageMap{}, nullptr, nullptr, nullptr, 0).empty(),
              "una cuadricula vacia da un resultado vacio");
    }

    if (failures == 0) {
        std::cout << "response_field_test: todas las pruebas pasaron\n";
        return 0;
    }
    std::cerr << "response_field_test: " << failures << " prueba(s) fallaron\n";
    return 1;
}
