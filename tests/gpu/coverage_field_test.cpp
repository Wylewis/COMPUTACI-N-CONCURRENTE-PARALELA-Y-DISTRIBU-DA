#include <cstdlib>
#include <iostream>
#include <random>
#include <string>
#include <vector>

#include "gpu/coverage_field.h"

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
        const std::vector<float> x{0.05f, 0.55f, 0.99f, 1.0f, -0.1f};
        const std::vector<float> y{0.05f, 0.30f, 0.99f, 0.5f, 0.5f};
        const std::vector<int> seed = coverageSeed(map, x.data(), y.data(), x.size());
        int sources = 0;
        for (const int v : seed) sources += v == 0;
        check(seed[0] == 0 && seed[1 * 4 + 2] == 0 && seed[3 * 4 + 3] == 0 && sources == 3 && seed[1] == kCoverFar,
              "cada moto libre marca su celda con cero y las que estan fuera del mapa no cuentan");
    }
    {
        const int grid = 9;
        const CoverageMap map = openMap(grid, 3, 5);
        std::vector<int> seed(static_cast<std::size_t>(grid * grid), kCoverFar);
        seed[static_cast<std::size_t>(4 * grid + 4)] = 0;
        int sweeps = 0;
        const std::vector<int> cpu = coverageCpu(map, seed.data(), &sweeps);
        bool exact = true;
        for (int row = 0; row < grid; ++row)
            for (int col = 0; col < grid; ++col)
                exact = exact && cpu[static_cast<std::size_t>(row * grid + col)] == 3 * std::abs(col - 4) + 5 * std::abs(row - 4);
        check(exact, "con una sola moto en el centro cada celda queda a 3 por columna y 5 por fila de distancia");
        check(sweeps == 9, "la informacion avanza una celda por pasada: 8 pasadas hasta la esquina y una mas para ver que nada cambia");
        check(coverageGpu(map, seed.data(), CoverKernel::Basic) == cpu, "la tarjeta sin tiles da el mismo campo");
        check(coverageGpu(map, seed.data(), CoverKernel::Tiled) == cpu, "la tarjeta con tiles da el mismo campo");
        const CoverageWorst worst = worstCovered(map, cpu);
        check(worst.ms == 32 && worst.reached == 81 && worst.unreached == 0 && worst.cell % 8 == 0,
              "la zona peor cubierta es una esquina, a 32 de la moto");
    }
    {
        const int grid = 7;
        CoverageMap map = openMap(grid, 2, 2);
        for (int row = 0; row < grid - 1; ++row) map.open[static_cast<std::size_t>(row * grid + 3)] = 0;
        std::vector<int> seed(static_cast<std::size_t>(grid * grid), kCoverFar);
        seed[0] = 0;
        const std::vector<int> cpu = coverageCpu(map, seed.data());
        check(cpu[3] == kCoverFar && cpu[static_cast<std::size_t>(2 * grid + 3)] == kCoverFar,
              "una celda sin calle no se actualiza nunca");
        check(cpu[6] == 2 * (6 + 6 + 6), "para llegar al otro lado del muro hay que rodearlo por la fila de abajo");
        check(coverageDijkstra(map, seed.data()) == cpu && coverageGpu(map, seed.data(), CoverKernel::Tiled) == cpu &&
                  coverageGpu(map, seed.data(), CoverKernel::Basic) == cpu,
              "con muro, Dijkstra y los dos kernels dan el mismo campo");
        seed[3] = 0;
        const std::vector<int> onWall = coverageCpu(map, seed.data());
        check(onWall[3] == 0 && onWall[4] == 2 && coverageGpu(map, seed.data()) == onWall,
              "una moto parada en una celda sin calle sigue siendo origen para sus vecinas");
    }
    for (const int grid : {1, 5, 15, 17, 33}) {
        std::mt19937 rng(static_cast<unsigned>(grid));
        CoverageMap map = openMap(grid, 7, 9);
        for (unsigned char& o : map.open) o = rng() % 5u != 0 ? 1 : 0;
        std::vector<int> seed(map.open.size(), kCoverFar);
        for (int& s : seed) s = rng() % 40u == 0 ? 0 : kCoverFar;
        seed[rng() % seed.size()] = 0;
        ThreadPool pool(4);
        const std::string tag = "cuadricula de " + std::to_string(grid) + " x " + std::to_string(grid) + ": ";
        int sweepsCpu = 0, sweepsPool = 0, sweepsGpu = 0;
        const std::vector<int> expected = coverageDijkstra(map, seed.data());
        const std::vector<int> cpu = coverageCpu(map, seed.data(), &sweepsCpu);
        GpuTimes times;
        check(cpu == expected, tag + "las pasadas de un hilo llegan al mismo campo que Dijkstra");
        check(coverageCpuParallel(map, seed.data(), pool, &sweepsPool) == expected && sweepsPool == sweepsCpu,
              tag + "multihilo identico a un hilo, con las mismas pasadas");
        check(coverageGpu(map, seed.data(), CoverKernel::Basic, &times, &sweepsGpu) == expected,
              tag + "tarjeta sin tiles identica a la CPU");
        check(coverageGpu(map, seed.data(), CoverKernel::Tiled) == expected, tag + "tarjeta con tiles identica a la CPU");
        check(sweepsGpu % kCoverCheckEvery == 0 && sweepsGpu >= sweepsCpu && sweepsGpu < sweepsCpu + kCoverCheckEvery,
              tag + "la tarjeta revisa la convergencia cada 16 pasadas y no sigue de largo");
        check(times.totalMs >= times.kernelMs, tag + "el tiempo total incluye al de las pasadas");
        bool closedKept = true;
        for (std::size_t p = 0; p < seed.size(); ++p) closedKept = closedKept && (map.open[p] || cpu[p] == seed[p]);
        check(closedKept, tag + "las celdas sin calle quedan como estaban");
    }
    {
        const CoverageMap map = openMap(6, 4, 4);
        const std::vector<int> seed(36, kCoverFar);
        int sweeps = -1;
        const std::vector<int> gpu = coverageGpu(map, seed.data(), CoverKernel::Tiled, nullptr, &sweeps);
        const CoverageWorst worst = worstCovered(map, gpu);
        check(gpu == seed && sweeps == kCoverCheckEvery && worst.reached == 0 && worst.unreached == 36,
              "sin motos libres el campo queda sin cubrir y la tarjeta para en la primera revision");
        check(coverageGpu(CoverageMap{}, nullptr).empty() && coverageCpu(CoverageMap{}, nullptr).empty(),
              "una cuadricula vacia da un resultado vacio");
    }

    if (failures == 0) {
        std::cout << "coverage_field_test: todas las pruebas pasaron\n";
        return 0;
    }
    std::cerr << "coverage_field_test: " << failures << " prueba(s) fallaron\n";
    return 1;
}
