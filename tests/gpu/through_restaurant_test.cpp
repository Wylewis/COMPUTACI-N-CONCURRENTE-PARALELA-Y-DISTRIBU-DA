#include <cstdlib>
#include <iostream>
#include <numeric>
#include <random>
#include <string>
#include <vector>

#include "gpu/through_restaurant.h"

namespace {

int failures = 0;

void check(bool ok, const std::string& what) {
    if (!ok) {
        ++failures;
        std::cerr << "FALLO: " << what << "\n";
    }
}

unsigned long long totalOf(const std::vector<unsigned>& wins) { return std::accumulate(wins.begin(), wins.end(), 0ULL); }

}

int main() {
    {
        const int n = 4;
        std::vector<int> table(static_cast<std::size_t>(n * n));
        for (int i = 0; i < n; ++i)
            for (int j = 0; j < n; ++j) table[static_cast<std::size_t>(i * n + j)] = 10 * std::abs(i - j);
        const RestaurantLegs legs = restaurantLegs(table, n, {1, 3});
        check(legs.nodes == 4 && legs.restaurants == 2 && legs.to[0] == 10 && legs.to[1] == 30 && legs.from[0] == 10 &&
                  legs.from[static_cast<std::size_t>(n + 2)] == 10,
              "los tramos salen de la tabla: de cada esquina a cada restaurante y de cada restaurante a cada esquina");
        const std::vector<unsigned> expected{13, 3};
        ThreadPool pool(4);
        const std::vector<unsigned> cpu = throughRestaurantCpu(legs);
        check(cpu == expected, "a mano, cuatro esquinas en linea: el restaurante del medio gana 13 pares y el de la punta 3; el empate es para el primero");
        check(throughRestaurantCpuParallel(legs, pool) == expected, "a mano: multihilo igual a un hilo");
        check(throughRestaurantGpu(legs, ThroughKernel::Tiled) == expected, "a mano: la tarjeta con tiles da lo mismo");
        check(throughRestaurantGpu(legs, ThroughKernel::Global) == expected, "a mano: la tarjeta sin tiles da lo mismo");
        check(bestPlaced(expected) == 0, "el restaurante mejor ubicado es el que gana mas pares");
    }
    for (const int n : {1, 5, 16, 17, 33, 50}) {
        for (const int restaurants : {1, 7, 16, 17, 40}) {
            std::mt19937 rng(static_cast<unsigned>(n * 100 + restaurants));
            RestaurantLegs legs;
            legs.nodes = n;
            legs.restaurants = restaurants;
            legs.to.resize(static_cast<std::size_t>(n) * static_cast<std::size_t>(restaurants));
            legs.from.resize(legs.to.size());
            for (int& v : legs.to) v = rng() % 9u == 0 ? kUnreachableMs : static_cast<int>(rng() % 50u);
            for (int& v : legs.from) v = rng() % 9u == 0 ? kUnreachableMs : static_cast<int>(rng() % 50u);
            ThreadPool pool(4);
            const std::string tag = std::to_string(n) + " esquinas y " + std::to_string(restaurants) + " restaurantes: ";
            const std::vector<unsigned> cpu = throughRestaurantCpu(legs);
            GpuTimes times;
            check(totalOf(cpu) <= static_cast<unsigned long long>(n) * static_cast<unsigned long long>(n),
                  tag + "cada par cuenta a lo sumo una vez");
            check(throughRestaurantCpuParallel(legs, pool) == cpu, tag + "multihilo identico a un hilo");
            check(throughRestaurantGpu(legs, ThroughKernel::Tiled, &times) == cpu, tag + "tarjeta con tiles identica a la CPU");
            check(throughRestaurantGpu(legs, ThroughKernel::Global) == cpu, tag + "tarjeta sin tiles identica a la CPU");
            check(times.totalMs >= times.kernelMs, tag + "el tiempo total incluye al del kernel");
        }
    }
    {
        RestaurantLegs cut;
        cut.nodes = 3;
        cut.restaurants = 2;
        cut.to.assign(6, kUnreachableMs);
        cut.from.assign(6, 5);
        ThreadPool pool(2);
        check(totalOf(throughRestaurantCpu(cut)) == 0 && totalOf(throughRestaurantGpu(cut, ThroughKernel::Tiled)) == 0 &&
                  totalOf(throughRestaurantGpu(cut, ThroughKernel::Global)) == 0,
              "si no se llega a ningun restaurante, ningun par cuenta");
        check(throughRestaurantGpu(RestaurantLegs{}).empty() && throughRestaurantCpu(RestaurantLegs{}).empty() &&
                  throughRestaurantCpuParallel(RestaurantLegs{}, pool).empty(),
              "sin restaurantes el resultado queda vacio");
    }

    if (failures == 0) {
        std::cout << "through_restaurant_test: todas las pruebas pasaron\n";
        return 0;
    }
    std::cerr << "through_restaurant_test: " << failures << " prueba(s) fallaron\n";
    return 1;
}
