// Pruebas de las decisiones que dependen del hardware: el tamano del pool
// respeta minimo y maximo, sigue al hardware cuando esta en rango, y el
// numero de tareas nunca supera el trabajo.
#include <iostream>
#include <string>
#include <thread>

#include "hardware.h"

namespace {

int failures = 0;

void check(bool ok, const std::string& what) {
    if (!ok) {
        ++failures;
        std::cerr << "FALLO: " << what << "\n";
    }
}

}  // namespace

int main() {
    check(clampPoolSize(8, 2, 32) == 8, "dentro del rango, un hilo por hilo de hardware");
    check(clampPoolSize(1, 2, 32) == 2, "un solo nucleo: el minimo");
    check(clampPoolSize(0, 2, 32) == 2, "hardware desconocido: el minimo");
    check(clampPoolSize(128, 2, 32) == 32, "mas nucleos que el maximo: el maximo");
    check(clampPoolSize(5, 0, 32) == 5, "minimo 0 se trata como 1");
    check(clampPoolSize(5, 8, 4) == 8, "maximo menor que el minimo: manda el minimo");

    const unsigned hw = hardwareThreads();
    const unsigned pool = recommendedPoolSize();
    check(pool >= kPoolMinimum && pool <= kPoolMaximum, "el pool recomendado esta en el rango");
    check(hw == 0 || hw < kPoolMinimum || hw > kPoolMaximum || pool == hw, "si el hardware esta en rango, el pool lo sigue");
    check(hardwareThreads() == std::thread::hardware_concurrency(), "hardwareThreads informa lo mismo que la libreria");

    check(defaultTaskCount(1000) == (hw == 0 ? 2u : hw), "con mucho trabajo, una tarea por nucleo");
    check(defaultTaskCount(3) <= 3, "nunca mas tareas que trabajo");
    check(defaultTaskCount(0) == 1, "sin trabajo, una tarea");

    std::cout << "hardware: " << hw << " hilos; pool recomendado: " << pool << "\n";
    if (failures == 0) {
        std::cout << "hardware_test: todas las pruebas pasaron\n";
        return 0;
    }
    std::cerr << "hardware_test: " << failures << " prueba(s) fallaron\n";
    return 1;
}
