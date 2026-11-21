#include <algorithm>
#include <iostream>
#include <numeric>
#include <random>
#include <string>
#include <vector>

#include "gpu/restaurant_slots.h"

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

}

int main() {
    {
        const std::vector<int> restaurant{0, 0, 1, 1, 1, 2, 0, 1};
        const std::vector<int> ms{0, 599'999, 600'000, 700'000, 1'300'000, 9'000'000, -4, 650'000};
        const std::vector<unsigned> expected{3, 0, 0, 0, 3, 1, 0, 0, 1};
        ThreadPool pool(4);
        const std::vector<unsigned> cpu = restaurantSlotsCpu(restaurant.data(), ms.data(), ms.size(), 3, 3);
        check(cpu == expected,
              "a mano: una fila por restaurante y una columna por franja de diez minutos; lo que pasa de la ultima franja cae en la ultima");
        check(restaurantSlotsCpuParallel(restaurant.data(), ms.data(), ms.size(), 3, 3, pool) == expected,
              "a mano: multihilo igual a un hilo");
        check(restaurantSlotsGpu(restaurant.data(), ms.data(), ms.size(), 3, 3, SlotKernel::Atomic) == expected,
              "a mano: la tarjeta con atomicos da lo mismo");
        check(restaurantSlotsGpu(restaurant.data(), ms.data(), ms.size(), 3, 3, SlotKernel::Aggregated) == expected,
              "a mano: la tarjeta juntando repetidos da lo mismo");
        const BusiestSlot busiest = busiestSlot(expected, 3);
        check(busiest.restaurant == 0 && busiest.slot == 0 && busiest.orders == 3,
              "la celda mas cargada es la primera con mas pedidos");
        check(busiestSlot({0, 0}, 2).restaurant == -1, "sin pedidos no hay celda mas cargada");
    }
    for (const std::size_t n : {1u, 63u, 64u, 65u, 16'384u, 16'385u, 40'000u}) {
        for (const bool grouped : {false, true}) {
            const int restaurants = 37, slots = 19;
            std::mt19937 rng(static_cast<unsigned>(n) + (grouped ? 1u : 0u));
            std::vector<int> restaurant(n), ms(n);
            for (std::size_t i = 0; i < n; ++i) {
                restaurant[i] = static_cast<int>(rng() % static_cast<unsigned>(restaurants));
                ms[i] = static_cast<int>(i * (static_cast<std::size_t>(slots) * 600'000u) / n);
            }
            if (grouped) std::sort(restaurant.begin(), restaurant.end());
            ThreadPool pool(4);
            const std::string tag = std::to_string(n) + " pedidos" + (grouped ? ", agrupados por restaurante: " : ": ");
            const std::vector<unsigned> cpu = restaurantSlotsCpu(restaurant.data(), ms.data(), n, restaurants, slots);
            GpuTimes times;
            check(totalOf(cpu) == n, tag + "el total es el numero de pedidos");
            check(restaurantSlotsCpuParallel(restaurant.data(), ms.data(), n, restaurants, slots, pool) == cpu,
                  tag + "multihilo identico a un hilo");
            check(restaurantSlotsGpu(restaurant.data(), ms.data(), n, restaurants, slots, SlotKernel::Atomic, &times) == cpu,
                  tag + "tarjeta con atomicos identica a la CPU");
            check(restaurantSlotsGpu(restaurant.data(), ms.data(), n, restaurants, slots, SlotKernel::Aggregated) == cpu,
                  tag + "tarjeta juntando repetidos identica a la CPU");
            check(times.totalMs >= times.kernelMs, tag + "el tiempo total incluye al del kernel");
        }
    }
    {
        const std::vector<int> same(5'000, 4), early(5'000, 100);
        const std::vector<unsigned> one = restaurantSlotsGpu(same.data(), early.data(), same.size(), 6, 2, SlotKernel::Aggregated);
        check(one[8] == 5'000 && totalOf(one) == 5'000 &&
                  restaurantSlotsGpu(same.data(), early.data(), same.size(), 6, 2, SlotKernel::Atomic) == one,
              "todos los pedidos en la misma celda: no se pierde ninguna suma");
        ThreadPool pool(2);
        check(totalOf(restaurantSlotsGpu(nullptr, nullptr, 0, 6, 2)) == 0 && restaurantSlotsGpu(nullptr, nullptr, 0, 6, 2).size() == 12 &&
                  restaurantSlotsCpu(nullptr, nullptr, 0, 0, 5).empty() &&
                  restaurantSlotsCpuParallel(nullptr, nullptr, 0, 6, 2, pool).size() == 12,
              "sin pedidos la tabla queda en cero, y sin restaurantes queda vacia");
    }

    if (failures == 0) {
        std::cout << "restaurant_slots_test: todas las pruebas pasaron\n";
        return 0;
    }
    std::cerr << "restaurant_slots_test: " << failures << " prueba(s) fallaron\n";
    return 1;
}
