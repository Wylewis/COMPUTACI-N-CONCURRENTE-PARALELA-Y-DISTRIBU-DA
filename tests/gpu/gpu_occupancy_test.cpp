#include <algorithm>
#include <iostream>
#include <string>
#include <vector>

#include "gpu/gpu_backend.h"
#include "gpu/gpu_kernel_info.h"

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
    const std::vector<GpuKernelInfo> kernels = gpuKernelInfos();
    const bool realGpu = gpuDeviceInfo().realGpu;
    check(!kernels.empty(), "hay kernels registrados");
    const auto table = std::find_if(kernels.begin(), kernels.end(),
                                    [](const GpuKernelInfo& k) { return k.application == "travel_table"; });
    check(table != kernels.end() && table->threadsPerBlock == 256 && !table->kernel.empty(),
          "el kernel de la tabla de tiempos esta registrado con bloques de 256 hilos");
    for (const GpuKernelInfo& k : kernels) {
        const std::string tag = k.application + "/" + k.kernel + ": ";
        check(k.threadsPerBlock >= 1 && k.threadsPerBlock <= 1024 && !k.limiter.empty(), tag + "bloque valido y limitante con nombre");
        if (realGpu) {
            check(k.measured && k.blocksPerMultiprocessor >= 1 && k.occupancyPercent > 0.0 && k.occupancyPercent <= 100.0 &&
                      k.registersPerThread > 0,
                  tag + "con GPU real la ocupacion sale de CUDA y esta entre 0 y 100 %");
        } else {
            check(!k.measured && k.blocksPerMultiprocessor == 0, tag + "en la emulacion no se inventa una ocupacion");
        }
        std::cout << tag << k.threadsPerBlock << " hilos por bloque, " << k.blocksPerMultiprocessor
                  << " bloques por SM, ocupacion " << k.occupancyPercent << " %, limita: " << k.limiter << "\n";
    }

    if (failures == 0) {
        std::cout << "gpu_occupancy_test: todas las pruebas pasaron\n";
        return 0;
    }
    std::cerr << "gpu_occupancy_test: " << failures << " prueba(s) fallaron\n";
    return 1;
}
