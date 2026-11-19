#include <iostream>
#include <string>

#include "gpu_backend.h"

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
    const GpuDeviceInfo info = gpuDeviceInfo();
    check(!info.backend.empty() && !info.name.empty(), "el backend y el dispositivo tienen nombre");
    check(info.warpSize == 32 && info.maxThreadsPerBlock >= 256 && info.multiprocessors >= 1,
          "propiedades del dispositivo razonables");
    check(gpuSelfTest(1) && gpuSelfTest(64) && gpuSelfTest(1000), "el kernel de prueba suma bien con bloques parciales");
    check(gpuSelfTest(0), "n = 0 no lanza nada");
    std::cout << "backend: " << info.backend << "; dispositivo: " << info.name << "; SM " << info.multiprocessors
              << "; hilos por bloque " << info.maxThreadsPerBlock << "\n";
    if (failures == 0) {
        std::cout << "gpu_backend_test: todas las pruebas pasaron\n";
        return 0;
    }
    std::cerr << "gpu_backend_test: " << failures << " prueba(s) fallaron\n";
    return 1;
}
