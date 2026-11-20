#include <iostream>
#include <random>
#include <string>
#include <vector>

#include "gpu/argmin_rows.h"

namespace {

int failures = 0;

void check(bool ok, const std::string& what) {
    if (!ok) {
        ++failures;
        std::cerr << "FALLO: " << what << "\n";
    }
}

std::vector<int> randomMatrix(std::size_t orders, std::size_t couriers, unsigned seed, int distinct) {
    std::mt19937 rng(seed);
    std::uniform_int_distribution<int> eta(100'000, 100'000 + distinct - 1), percent(0, 99);
    std::vector<int> m(orders * couriers);
    for (int& v : m) v = percent(rng) < 20 ? kEtaUnavailable : eta(rng);
    return m;
}

}

int main() {
    {
        const int U = kEtaUnavailable;
        const std::vector<int> eta{500, 300, 300, 900, U, U, U, U, 700, 700, 700, 700};
        const std::vector<EtaKey> cpu = argminRowsCpu(eta.data(), 3, 4);
        check(etaOfKey(cpu[0]) == 300 && courierOfKey(cpu[0], 0, 4) == 1, "en un empate gana la moto de menor numero");
        check(!keyHasCourier(cpu[1]), "una fila sin motos disponibles queda marcada sin moto");
        check(argminRowsGpu(eta.data(), 3, 4) == cpu, "la reduccion en la tarjeta elige lo mismo, con el empate");
        const std::vector<int> rotation{0, 0, 2};
        const std::vector<EtaKey> rotated = argminRowsCpu(eta.data(), 3, 4, rotation.data());
        check(courierOfKey(rotated[2], 2, 4) == 2 && courierOfKey(cpu[2], 0, 4) == 0 && etaOfKey(rotated[2]) == 700,
              "con rotacion, entre motos empatadas gana otra, con la misma hora de entrega");
        check(argminRowsGpu(eta.data(), 3, 4, rotation.data()) == rotated, "la tarjeta respeta la rotacion");
    }
    for (const auto& [orders, couriers] : std::vector<std::pair<std::size_t, std::size_t>>{{1, 1}, {7, 255}, {5, 256}, {9, 700}, {12, 33}}) {
        for (const int distinct : {3, 1'000'000}) {
            const std::vector<int> eta = randomMatrix(orders, couriers, static_cast<unsigned>(orders * 1000 + couriers), distinct);
            std::vector<int> rotation(orders);
            for (std::size_t o = 0; o < orders; ++o) rotation[o] = static_cast<int>((o * 7919) % couriers);
            ThreadPool pool(4);
            const std::string tag = std::to_string(orders) + " x " + std::to_string(couriers) +
                                    (distinct == 3 ? ", muchos empates: " : ", pocos empates: ");
            const std::vector<EtaKey> cpu = argminRowsCpu(eta.data(), orders, couriers);
            GpuTimes times;
            check(argminRowsCpuParallel(eta.data(), orders, couriers, pool) == cpu, tag + "multihilo igual a un hilo");
            check(argminRowsGpu(eta.data(), orders, couriers, nullptr, &times) == cpu, tag + "tarjeta igual a la CPU");
            check(argminRowsGpu(eta.data(), orders, couriers, rotation.data()) == argminRowsCpu(eta.data(), orders, couriers, rotation.data()),
                  tag + "tarjeta igual a la CPU con rotacion");
            bool consistent = times.totalMs >= times.kernelMs;
            for (std::size_t o = 0; o < orders; ++o) {
                if (!keyHasCourier(cpu[o])) continue;
                const int c = courierOfKey(cpu[o], 0, static_cast<int>(couriers));
                consistent = consistent && eta[o * couriers + static_cast<std::size_t>(c)] == etaOfKey(cpu[o]);
            }
            check(consistent, tag + "la clave trae la hora de entrega de la moto elegida");
        }
    }
    {
        const int n = 3;
        CsrGraph g;
        g.n = n;
        g.rowStart = {0, 1, 2, 2};
        g.to = {1, 2};
        g.weightMs = {100'000, 50'000};
        const TravelTable table(n, travelTableCpu(g));
        EtaInput in;
        in.nowMs = 0;
        in.courierNode = {0, 0, 0, 2, 1};
        in.courierAvailableMs = {50'000, 0, 0, 0, 0};
        in.courierFree = {1, 1, 0, 1, 1};
        in.orderRestaurantNode = {1, 1};
        in.orderDeliveryNode = {2, 2};
        in.orderReadyMs = {0, 0};
        std::vector<int> matrix(in.orders() * in.couriers());
        etaBatchCpu(table, in, matrix.data());
        EtaBatchGpu gpu(table);
        const std::vector<unsigned long long> chained = gpu.runBest(in);
        check(chained == argminRowsCpu(matrix.data(), in.orders(), in.couriers()) && courierOfKey(chained[0], 0, 5) == 4,
              "estimar y elegir encadenados en la tarjeta dan lo mismo que la CPU: gana la moto que ya esta en el restaurante");
        check(argminRowsGpu(nullptr, 0, 5).empty() && argminRowsKernelInfo().threadsPerBlock == 256, "sin pedidos no hay nada que reducir");
    }

    if (failures == 0) {
        std::cout << "argmin_rows_test: todas las pruebas pasaron\n";
        return 0;
    }
    std::cerr << "argmin_rows_test: " << failures << " prueba(s) fallaron\n";
    return 1;
}
