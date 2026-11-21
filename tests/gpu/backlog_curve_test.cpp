#include <cstdint>
#include <iostream>
#include <random>
#include <string>
#include <vector>

#include "gpu/backlog_curve.h"

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
        const std::vector<std::int64_t> created{0, 500, 999, 1000, 3200, 3900};
        const std::vector<std::int64_t> closed{2500, 3100, 5000};
        const std::vector<int> delta = backlogDeltas(created, closed);
        check(delta == std::vector<int>{3, 1, -1, 1, 0, -1},
              "cada pedido creado suma uno en su segundo y cada pedido cerrado resta uno en el suyo");
        const std::vector<int> expected{3, 4, 3, 4, 4, 3};
        ThreadPool pool(4);
        const std::vector<int> cpu = backlogCurveCpu(delta.data(), delta.size());
        check(cpu == expected, "a mano: la curva es la suma acumulada de los cambios");
        check(backlogCurveCpuParallel(delta.data(), delta.size(), pool) == expected, "a mano: multihilo igual a un hilo");
        check(backlogCurveGpu(delta.data(), delta.size(), ScanKernel::KoggeStone) == expected, "a mano: Kogge-Stone da lo mismo");
        check(backlogCurveGpu(delta.data(), delta.size(), ScanKernel::BrentKung) == expected, "a mano: Brent-Kung da lo mismo");
        const BacklogPeak peak = backlogPeak(expected);
        check(peak.waiting == 4 && peak.slot == 1, "el pico es el primer segundo con mas pedidos en espera");
        check(cpu.back() == static_cast<int>(created.size() - closed.size()),
              "el ultimo valor de la curva son los pedidos que quedaron sin cerrar");
        check(backlogDeltas({}, {}).empty() && backlogDeltas({7000}, {}, 2000).size() == 4,
              "sin pedidos no hay curva, y el ancho del tramo se puede cambiar");
    }
    for (const std::size_t n : {1u, 2u, 255u, 256u, 257u, 511u, 1000u, 65'536u, 65'537u, 70'000u}) {
        std::mt19937 rng(static_cast<unsigned>(n));
        std::vector<int> delta(n);
        int waiting = 0;
        for (int& d : delta) {
            const int in = static_cast<int>(rng() % 4u);
            const int out = waiting + in > 0 ? static_cast<int>(rng() % static_cast<unsigned>(waiting + in + 1)) % 4 : 0;
            d = in - out;
            waiting += d;
        }
        ThreadPool pool(4);
        const std::string tag = std::to_string(n) + " segundos: ";
        const std::vector<int> cpu = backlogCurveCpu(delta.data(), n);
        GpuTimes times;
        int levelsKogge = 0, levelsBrent = 0;
        check(cpu.back() == waiting, tag + "la curva termina en los pedidos que quedan en espera");
        check(backlogCurveCpuParallel(delta.data(), n, pool) == cpu, tag + "multihilo identico a un hilo");
        check(backlogCurveGpu(delta.data(), n, ScanKernel::KoggeStone, &times, &levelsKogge) == cpu,
              tag + "Kogge-Stone identico a la CPU");
        check(backlogCurveGpu(delta.data(), n, ScanKernel::BrentKung, nullptr, &levelsBrent) == cpu,
              tag + "Brent-Kung identico a la CPU");
        const int expectedLevels = n > 65'536 ? 3 : (n > 256 ? 2 : 1);
        check(levelsKogge == expectedLevels && levelsBrent == expectedLevels,
              tag + "los totales de cada seccion se vuelven a acumular hasta que entran en una sola");
        check(times.totalMs >= times.kernelMs, tag + "el tiempo total incluye al de los kernels");
    }
    {
        const std::vector<int> falling{-3, -2, 5, -1};
        check(backlogCurveGpu(falling.data(), 4, ScanKernel::BrentKung) == std::vector<int>{-3, -5, 0, -1} &&
                  backlogPeak({-3, -5, 0, -1}).waiting == 0,
              "los cambios negativos se acumulan igual y el pico nunca es menor que cero");
        int levels = -1;
        ThreadPool pool(2);
        check(backlogCurveGpu(nullptr, 0, ScanKernel::KoggeStone, nullptr, &levels).empty() && levels == 0 &&
                  backlogCurveCpu(nullptr, 0).empty() && backlogCurveCpuParallel(nullptr, 0, pool).empty(),
              "sin segundos no se lanza nada");
    }

    if (failures == 0) {
        std::cout << "backlog_curve_test: todas las pruebas pasaron\n";
        return 0;
    }
    std::cerr << "backlog_curve_test: " << failures << " prueba(s) fallaron\n";
    return 1;
}
