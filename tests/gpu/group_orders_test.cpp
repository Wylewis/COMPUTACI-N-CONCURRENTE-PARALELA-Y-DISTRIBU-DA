#include <iostream>
#include <numeric>
#include <random>
#include <string>
#include <vector>

#include "gpu/group_orders.h"

namespace {

int failures = 0;

void check(bool ok, const std::string& what) {
    if (!ok) {
        ++failures;
        std::cerr << "FALLO: " << what << "\n";
    }
}

std::vector<int> randomGroups(std::size_t orders, int groups, unsigned seed, bool skewed) {
    std::mt19937 rng(seed);
    std::uniform_int_distribution<int> any(0, groups - 1), percent(0, 99);
    std::vector<int> g(orders);
    for (int& v : g) v = skewed && percent(rng) < 60 ? groups / 2 : any(rng);
    return g;
}

bool wellFormed(const OrderGroups& out, const std::vector<int>& group, int groups) {
    if (out.start.size() != static_cast<std::size_t>(groups) + 1 || out.order.size() != group.size()) return false;
    if (out.start.front() != 0 || out.start.back() != static_cast<int>(group.size())) return false;
    std::vector<char> seen(group.size(), 0);
    for (int g = 0; g < groups; ++g) {
        if (out.start[static_cast<std::size_t>(g)] > out.start[static_cast<std::size_t>(g) + 1]) return false;
        for (int k = out.start[static_cast<std::size_t>(g)]; k < out.start[static_cast<std::size_t>(g) + 1]; ++k) {
            const int order = out.order[static_cast<std::size_t>(k)];
            if (order < 0 || order >= static_cast<int>(group.size()) || seen[static_cast<std::size_t>(order)]) return false;
            if (group[static_cast<std::size_t>(order)] != g) return false;
            seen[static_cast<std::size_t>(order)] = 1;
        }
    }
    return true;
}

}

int main() {
    {
        const std::vector<int> values{3, 0, 2, 5, 0, 1};
        const std::vector<int> expected{0, 3, 3, 5, 10, 10, 11};
        ThreadPool pool(4);
        check(exclusiveScanCpu(values) == expected, "scan exclusivo a mano: cada posicion suma las anteriores y la ultima es el total");
        check(exclusiveScanCpuParallel(values, pool) == expected, "scan multihilo igual al de un hilo");
        check(exclusiveScanGpu(values) == expected, "scan en la tarjeta igual al de CPU");
        check(exclusiveScanGpu({}) == std::vector<int>{0}, "el scan de nada es un solo cero");
    }
    for (const std::size_t n : {1u, 255u, 256u, 257u, 1000u, 70000u}) {
        std::mt19937 rng(static_cast<unsigned>(n));
        std::uniform_int_distribution<int> value(0, 9);
        std::vector<int> values(n);
        for (int& v : values) v = value(rng);
        ThreadPool pool(4);
        const std::vector<int> cpu = exclusiveScanCpu(values);
        GpuTimes times;
        check(exclusiveScanCpuParallel(values, pool) == cpu, "scan de " + std::to_string(n) + ": multihilo igual a un hilo");
        check(exclusiveScanGpu(values, &times) == cpu, "scan de " + std::to_string(n) + ": tarjeta igual a la CPU");
        check(times.totalMs >= times.kernelMs, "scan de " + std::to_string(n) + ": el tiempo total incluye al del kernel");
    }
    {
        const std::vector<int> group{2, 0, 2, 1, 0, 2};
        const OrderGroups cpu = groupOrdersCpu(group.data(), group.size(), 4);
        check(cpu.start == std::vector<int>({0, 2, 3, 6, 6}), "a mano: el tramo de cada restaurante empieza donde termina el anterior");
        check(cpu.order == std::vector<int>({1, 4, 3, 0, 2, 5}), "a mano: dentro de cada tramo los pedidos quedan en orden de llegada");
        OrderGroups gpu = groupOrdersGpu(group.data(), group.size(), 4);
        check(wellFormed(gpu, group, 4), "a mano: la tarjeta deja cada pedido una sola vez y en el tramo de su restaurante");
        sortWithinGroups(gpu);
        check(gpu == cpu, "a mano: la tarjeta arma los mismos tramos que la CPU");
    }
    for (const auto& [orders, groups] : std::vector<std::pair<std::size_t, int>>{{1, 1}, {300, 7}, {257, 600}, {2000, 40}, {1500, 1500}}) {
        for (const bool skewed : {false, true}) {
            const std::vector<int> group = randomGroups(orders, groups, static_cast<unsigned>(orders * 31 + static_cast<std::size_t>(groups)), skewed);
            ThreadPool pool(4);
            const std::string tag = std::to_string(orders) + " pedidos, " + std::to_string(groups) + " restaurantes" +
                                    (skewed ? ", uno concentra la demanda: " : ": ");
            const OrderGroups cpu = groupOrdersCpu(group.data(), orders, groups);
            check(wellFormed(cpu, group, groups), tag + "la version de un hilo arma tramos validos");
            check(groupOrdersCpuParallel(group.data(), orders, groups, pool) == cpu, tag + "multihilo igual a un hilo, con el mismo orden");
            GpuTimes times;
            OrderGroups gpu = groupOrdersGpu(group.data(), orders, groups, &times);
            check(gpu.start == cpu.start, tag + "la tarjeta da los mismos inicios de tramo");
            check(wellFormed(gpu, group, groups), tag + "la tarjeta no pierde ni repite pedidos");
            sortWithinGroups(gpu);
            check(gpu == cpu, tag + "los tramos de la tarjeta tienen los mismos pedidos que los de CPU");
            check(times.totalMs >= times.kernelMs, tag + "el tiempo total incluye al del kernel");
        }
    }
    {
        const OrderGroups empty = groupOrdersGpu(nullptr, 0, 5);
        check(empty.start == std::vector<int>(6, 0) && empty.order.empty(), "sin pedidos todos los tramos quedan vacios");
        check(groupOrdersCpu(nullptr, 0, 5) == empty, "sin pedidos la CPU da lo mismo");
    }

    if (failures == 0) {
        std::cout << "group_orders_test: todas las pruebas pasaron\n";
        return 0;
    }
    std::cerr << "group_orders_test: " << failures << " prueba(s) fallaron\n";
    return 1;
}
