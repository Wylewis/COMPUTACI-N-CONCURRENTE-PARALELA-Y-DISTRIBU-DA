#include <iostream>

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "Uso: delivery_sim <config.json> [--log <ruta>]\n";
        return 1;
    }

    std::cerr << "delivery_sim: la simulacion todavia no esta implementada "
              << "(config: " << argv[1] << ")\n";
    return 1;
}
