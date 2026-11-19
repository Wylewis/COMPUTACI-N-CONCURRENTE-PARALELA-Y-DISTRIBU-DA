#include <algorithm>
#include <iostream>
#include <random>
#include <string>
#include <vector>

#include "gpu/eta_batch.h"

namespace {

int failures = 0;

void check(bool ok, const std::string& what) {
    if (!ok) {
        ++failures;
        std::cerr << "FALLO: " << what << "\n";
    }
}

CsrGraph ringCity(int n, unsigned seed) {
    std::mt19937 rng(seed);
    std::uniform_int_distribution<int> weight(5'000, 90'000), node(0, n - 2);
    CsrGraph g;
    g.n = n;
    g.rowStart.assign(static_cast<std::size_t>(n) + 1, 0);
    for (int u = 0; u < n; ++u) {
        if (u < n - 1) {
            g.to.push_back((u + 1) % (n - 1));
            g.weightMs.push_back(weight(rng));
            g.to.push_back(node(rng));
            g.weightMs.push_back(weight(rng));
        } else {
            g.to.push_back(0);
            g.weightMs.push_back(weight(rng));
        }
        g.rowStart[static_cast<std::size_t>(u) + 1] = static_cast<int>(g.to.size());
    }
    return g;
}

EtaInput randomInput(int n, int couriers, int orders, unsigned seed) {
    std::mt19937 rng(seed);
    std::uniform_int_distribution<int> node(0, n - 1), time(0, 600'000), percent(0, 99);
    EtaInput in;
    in.nowMs = 300'000;
    for (int c = 0; c < couriers; ++c) {
        in.courierNode.push_back(node(rng));
        in.courierAvailableMs.push_back(time(rng));
        in.courierFree.push_back(percent(rng) < 80 ? 1 : 0);
    }
    for (int o = 0; o < orders; ++o) {
        in.orderRestaurantNode.push_back(node(rng));
        in.orderDeliveryNode.push_back(node(rng));
        in.orderReadyMs.push_back(time(rng));
    }
    return in;
}

}

int main() {
    {
        CsrGraph g;
        g.n = 3;
        g.rowStart = {0, 1, 2, 2};
        g.to = {1, 2};
        g.weightMs = {100'000, 50'000};
        const TravelTable table(3, travelTableCpu(g));
        EtaInput in;
        in.nowMs = 10'000;
        in.courierNode = {0, 0, 2, 0};
        in.courierAvailableMs = {0, 40'000, 0, 0};
        in.courierFree = {1, 1, 1, 0};
        in.orderRestaurantNode = {1, 1};
        in.orderDeliveryNode = {2, 2};
        in.orderReadyMs = {0, 500'000};
        std::vector<int> cpu(8);
        etaBatchCpu(table, in, cpu.data());
        const int U = kEtaUnavailable;
        check(cpu == std::vector<int>{280'000, 310'000, U, U, 670'000, 670'000, U, U},
              "a mano: sale ahora o cuando queda libre, espera la comida, y la que no llega o esta llena no esta disponible");
        EtaBatchGpu gpu(table);
        check(gpu.run(in) == cpu && gpu.run(in, EtaLayout::ArrayOfStructures) == cpu,
              "el kernel da lo mismo con estructura de arreglos y con arreglo de estructuras");
    }
    for (const auto& [couriers, orders] : std::vector<std::pair<int, int>>{{1, 1}, {33, 9}, {70, 13}, {64, 16}}) {
        const int n = 41;
        const TravelTable table(n, travelTableCpu(ringCity(n, 7)));
        const EtaInput in = randomInput(n, couriers, orders, static_cast<unsigned>(couriers * 100 + orders));
        const std::size_t cells = static_cast<std::size_t>(couriers) * static_cast<std::size_t>(orders);
        std::vector<int> cpu(cells), parallel(cells);
        etaBatchCpu(table, in, cpu.data());
        ThreadPool pool(4);
        etaBatchCpuParallel(table, in, parallel.data(), pool);
        EtaBatchGpu gpu(table);
        GpuTimes times;
        const std::string tag = std::to_string(orders) + " pedidos x " + std::to_string(couriers) + " motos: ";
        check(parallel == cpu, tag + "la version multihilo da lo mismo que un hilo");
        check(gpu.run(in, EtaLayout::StructureOfArrays, &times) == cpu, tag + "GPU con estructura de arreglos igual a la CPU");
        check(gpu.run(in, EtaLayout::ArrayOfStructures) == cpu, tag + "GPU con arreglo de estructuras igual a la CPU");
        check(times.kernelMs >= 0.0 && times.totalMs >= times.kernelMs && gpu.tableBytes() == static_cast<std::size_t>(n) * n * sizeof(int),
              tag + "tiempos coherentes y la tabla reside completa en el dispositivo");
        const long unavailable = std::count(cpu.begin(), cpu.end(), kEtaUnavailable);
        check(couriers == 1 || (unavailable > 0 && unavailable < static_cast<long>(cells)),
              tag + "hay pares disponibles y no disponibles");
    }
    {
        const TravelTable table(41, travelTableCpu(ringCity(41, 7)));
        EtaBatchGpu gpu(table);
        check(gpu.run(EtaInput{}).empty(), "sin motos ni pedidos no hay nada que calcular");
        check(etaBatchKernelInfos().size() == 2, "los dos kernels informan su ocupacion");
        check(sizeof(CourierRecord) == 32, "el registro de la moto ocupa 32 bytes");
    }

    if (failures == 0) {
        std::cout << "eta_batch_test: todas las pruebas pasaron\n";
        return 0;
    }
    std::cerr << "eta_batch_test: " << failures << " prueba(s) fallaron\n";
    return 1;
}
