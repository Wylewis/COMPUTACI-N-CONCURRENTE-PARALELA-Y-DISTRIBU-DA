#include <iostream>
#include <random>
#include <string>
#include <vector>

#include "gpu/round_trip_reach.h"

namespace {

int failures = 0;

void check(bool ok, const std::string& what) {
    if (!ok) {
        ++failures;
        std::cerr << "FALLO: " << what << "\n";
    }
}

}

int main() {
    {
        const int n = 3;
        const std::vector<int> table{0, 10, 50, 40, 0, 20, 90, 30, 0};
        const std::vector<int> turned = turnedTable(table, n);
        check(turned == std::vector<int>{0, 40, 90, 10, 0, 30, 50, 20, 0}, "la tabla girada cambia filas por columnas");
        const std::vector<unsigned> expected{2, 3, 2};
        ThreadPool pool(4);
        const std::vector<unsigned> cpu = roundTripReachCpu(table.data(), turned.data(), n, 50);
        check(cpu == expected,
              "a mano: de la esquina 0 se va y vuelve a la 1 en 50, justo en el limite, y a la 2 en 140; cada esquina se cuenta a si misma");
        check(roundTripReachCpuColumn(table.data(), n, 50) == expected, "a mano: leer la vuelta por columnas da lo mismo");
        check(roundTripReachCpuParallel(table.data(), turned.data(), n, pool, 50) == expected, "a mano: multihilo igual a un hilo");
        check(roundTripReachGpu(table.data(), turned.data(), n, RoundTripLayout::Turned, nullptr, 50) == expected,
              "a mano: la tarjeta con la tabla girada da lo mismo");
        check(roundTripReachGpu(table.data(), nullptr, n, RoundTripLayout::Column, nullptr, 50) == expected,
              "a mano: la tarjeta leyendo por columnas da lo mismo");
        check(widestReach(expected) == 1, "la esquina con mas alcance es la del medio");
    }
    for (const int n : {1, 2, 63, 64, 65, 255, 256, 257, 300}) {
        std::mt19937 rng(static_cast<unsigned>(n) + 4u);
        std::vector<int> table(static_cast<std::size_t>(n) * static_cast<std::size_t>(n));
        for (int& v : table) v = rng() % 11u == 0 ? kUnreachableMs : static_cast<int>(rng() % 600'000u);
        for (int i = 0; i < n; ++i) table[static_cast<std::size_t>(i) * static_cast<std::size_t>(n) + static_cast<std::size_t>(i)] = 0;
        const std::vector<int> turned = turnedTable(table, n);
        ThreadPool pool(4);
        const std::string tag = std::to_string(n) + " esquinas: ";
        const std::vector<unsigned> cpu = roundTripReachCpuColumn(table.data(), n);
        GpuTimes times;
        bool selfCounted = true;
        for (const unsigned r : cpu) selfCounted = selfCounted && r >= 1 && r <= static_cast<unsigned>(n);
        check(selfCounted, tag + "cada esquina se alcanza a si misma y a lo sumo a todas");
        check(roundTripReachCpu(table.data(), turned.data(), n) == cpu, tag + "con la tabla girada da lo mismo que por columnas");
        check(roundTripReachCpuParallel(table.data(), turned.data(), n, pool) == cpu, tag + "multihilo identico a un hilo");
        check(roundTripReachGpu(table.data(), turned.data(), n, RoundTripLayout::Turned, &times) == cpu,
              tag + "tarjeta con la tabla girada identica a la CPU");
        check(roundTripReachGpu(table.data(), nullptr, n, RoundTripLayout::Column) == cpu,
              tag + "tarjeta leyendo por columnas identica a la CPU");
        check(times.totalMs >= times.kernelMs, tag + "el tiempo total incluye al del kernel");
    }
    {
        ThreadPool pool(2);
        check(roundTripReachGpu(nullptr, nullptr, 0).empty() && roundTripReachCpu(nullptr, nullptr, 0).empty() &&
                  roundTripReachCpuParallel(nullptr, nullptr, 0, pool).empty() && turnedTable({}, 0).empty(),
              "sin esquinas el resultado queda vacio");
        const std::vector<int> far(4, kUnreachableMs);
        check(roundTripReachCpuColumn(far.data(), 2) == std::vector<unsigned>{0, 0} &&
                  roundTripReachGpu(far.data(), far.data(), 2) == std::vector<unsigned>{0, 0},
              "dos tramos sin camino no desbordan la suma ni cuentan como alcanzables");
    }

    if (failures == 0) {
        std::cout << "round_trip_reach_test: todas las pruebas pasaron\n";
        return 0;
    }
    std::cerr << "round_trip_reach_test: " << failures << " prueba(s) fallaron\n";
    return 1;
}
