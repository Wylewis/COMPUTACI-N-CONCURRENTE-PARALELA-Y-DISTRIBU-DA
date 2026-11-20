#include <iostream>
#include <random>
#include <string>
#include <vector>

#include "gpu/delivery_kpis.h"

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
        const std::vector<int> ms{600'000, 2'700'000, 2'700'001, 1'200'000, 5'400'000, 0};
        ThreadPool pool(4);
        const DeliveryKpis cpu = deliveryKpisCpu(ms.data(), ms.size());
        check(cpu.sumMs == 12'600'001 && cpu.maxMs == 5'400'000 && cpu.late == 2,
              "a mano: suma, maximo y dos entregas de mas de 45 minutos; la de 45 minutos justos no cuenta");
        check(deliveryKpisCpuParallel(ms.data(), ms.size(), pool) == cpu, "a mano: multihilo igual a un hilo");
        check(deliveryKpisGpu(ms.data(), ms.size()) == cpu, "a mano: la tarjeta da lo mismo");
        check(deliveryKpisGpu(ms.data(), ms.size(), kLateDeliveryMs, 1) == cpu, "a mano: la tarjeta con un dato por hilo da lo mismo");
        check(deliveryKpisCpu(ms.data(), ms.size(), 0).late == 5, "el limite de tardanza se puede cambiar");
    }
    {
        const std::vector<int> big(70'000, 2'000'000'000);
        const DeliveryKpis cpu = deliveryKpisCpu(big.data(), big.size());
        check(cpu.sumMs == 140'000'000'000'000LL && deliveryKpisGpu(big.data(), big.size()) == cpu,
              "la suma no se desborda: se acumula en 64 bits");
        const std::vector<int> negative{-5, -9, -1};
        check(deliveryKpisCpu(negative.data(), 3).maxMs == -1 && deliveryKpisGpu(negative.data(), 3).maxMs == -1,
              "el maximo parte del menor entero, no de cero");
    }
    for (const std::size_t n : {1u, 255u, 256u, 257u, 16'384u, 16'385u, 40'000u, 70'000u}) {
        std::mt19937 rng(static_cast<unsigned>(n));
        std::vector<int> ms(n);
        for (int& v : ms) v = static_cast<int>(rng() % (90u * 60'000u));
        ms[rng() % n] = 95 * 60'000;
        ThreadPool pool(4);
        const std::string tag = std::to_string(n) + " entregas: ";
        const DeliveryKpis cpu = deliveryKpisCpu(ms.data(), n);
        GpuTimes times;
        int coarse = 0, fine = 0;
        check(cpu.maxMs == 95 * 60'000, tag + "el maximo es la entrega mas lenta");
        check(deliveryKpisCpuParallel(ms.data(), n, pool) == cpu, tag + "multihilo identico a un hilo");
        check(deliveryKpisGpu(ms.data(), n, kLateDeliveryMs, kKpiPerThread, &times, &coarse) == cpu,
              tag + "tarjeta con 64 datos por hilo identica a la CPU");
        check(deliveryKpisGpu(ms.data(), n, kLateDeliveryMs, 1, nullptr, &fine) == cpu,
              tag + "tarjeta con un dato por hilo identica a la CPU");
        check(times.totalMs >= times.kernelMs, tag + "el tiempo total incluye al del kernel");
        const int expectedCoarse = n > 16'384 ? 2 : 1;
        const int expectedFine = n > 65'536 ? 3 : (n > 256 ? 2 : 1);
        check(coarse == expectedCoarse && fine == expectedFine,
              tag + "los parciales de cada bloque se vuelven a reducir hasta que queda uno");
    }
    {
        int launches = -1;
        const DeliveryKpis none = deliveryKpisGpu(nullptr, 0, kLateDeliveryMs, kKpiPerThread, nullptr, &launches);
        ThreadPool pool(2);
        check(none == deliveryKpisCpu(nullptr, 0) && none == deliveryKpisCpuParallel(nullptr, 0, pool) && none.sumMs == 0 &&
                  none.late == 0 && launches == 0,
              "sin entregas no se lanza nada y los indicadores quedan vacios");
    }

    if (failures == 0) {
        std::cout << "delivery_kpis_test: todas las pruebas pasaron\n";
        return 0;
    }
    std::cerr << "delivery_kpis_test: " << failures << " prueba(s) fallaron\n";
    return 1;
}
