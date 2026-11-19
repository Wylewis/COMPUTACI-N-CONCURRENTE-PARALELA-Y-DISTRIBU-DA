#include <atomic>
#include <chrono>
#include <iostream>
#include <set>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "gpu_backend.h"
#include "gpu_worker.h"

using namespace std::chrono_literals;

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
        GpuWorker gpu;
        std::atomic<int> wrongThread{0};
        std::atomic<long> sum{0};
        std::vector<std::thread> clients;
        for (int c = 0; c < 8; ++c)
            clients.emplace_back([&, c] {
                for (int k = 0; k < 50; ++k) {
                    auto f = gpu.submit([&gpu, &wrongThread, c, k] {
                        if (!gpu.onOwnerThread()) wrongThread.fetch_add(1);
                        return c * 1000 + k;
                    });
                    sum.fetch_add(f.get());
                }
            });
        for (auto& t : clients) t.join();
        long expected = 0;
        for (int c = 0; c < 8; ++c)
            for (int k = 0; k < 50; ++k) expected += c * 1000 + k;
        check(wrongThread.load() == 0 && sum.load() == expected && gpu.jobsRun() == 400,
              "8 hilos envian 400 trabajos: todos corren en el hilo dueno y cada future trae su resultado");
        check(!gpu.onOwnerThread(), "el hilo que envia no es el dueno");
    }
    {
        GpuWorker gpu;
        auto bad = gpu.submit([]() -> int { throw std::runtime_error("kernel roto"); });
        bool threw = false;
        try {
            (void)bad.get();
        } catch (const std::runtime_error&) {
            threw = true;
        }
        auto after = gpu.submit([] { return 7; });
        check(threw && after.get() == 7, "la excepcion cruza por el future y el hilo sigue atendiendo");
    }
    {
        GpuWorker gpu;
        auto slow = gpu.submit([] {
            std::this_thread::sleep_for(150ms);
            return 1;
        });
        auto quick = gpu.submit([] { return 2; });
        const bool late = quick.wait_for(20ms) != std::future_status::ready;
        check(late && gpu.pending() == 1, "con el plazo vencido el que espera vuelve y el trabajo sigue en cola");
        check(slow.get() == 1 && quick.get() == 2, "los dos trabajos terminan igual, en orden");
    }
    {
        GpuWorker gpu;
        std::atomic<int> ran{0};
        std::vector<std::future<void>> pending;
        for (int i = 0; i < 20; ++i) pending.push_back(gpu.submit([&ran] { ran.fetch_add(1); }));
        gpu.stop();
        check(ran.load() == 20, "stop termina los trabajos que quedaban en la cola");
        bool threw = false;
        try {
            gpu.submit([] { return 0; });
        } catch (const std::runtime_error&) {
            threw = true;
        }
        check(threw, "despues de stop no se aceptan trabajos");
    }
    {
        GpuWorker gpu;
        check(gpu.submit([] { return gpuSelfTest(500); }).get(), "un kernel lanzado desde el hilo dueno da el resultado correcto");
    }

    if (failures == 0) {
        std::cout << "gpu_worker_test: todas las pruebas pasaron\n";
        return 0;
    }
    std::cerr << "gpu_worker_test: " << failures << " prueba(s) fallaron\n";
    return 1;
}
