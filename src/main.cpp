#include <iostream>
#include <string>

#include "config.h"

namespace {

struct Options {
    std::string configPath;
    std::string logPath = "events.log";
};

void printUsage(std::ostream& out) {
    out << "Uso: delivery_sim <config.json> [--log <ruta>]\n";
}

// Interpreta la linea de comandos: la ruta del config es obligatoria y
// `--log <ruta>` es el unico flag opcional.
bool parseArgs(int argc, char** argv, Options& opts, std::string& error) {
    if (argc < 2) {
        error = "falta la ruta del archivo de configuracion";
        return false;
    }
    opts.configPath = argv[1];
    for (int i = 2; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--log") {
            if (i + 1 >= argc) {
                error = "--log requiere una ruta";
                return false;
            }
            opts.logPath = argv[++i];
        } else {
            error = "argumento desconocido: " + arg;
            return false;
        }
    }
    return true;
}

}  // namespace

int main(int argc, char** argv) {
    Options opts;
    std::string error;
    if (!parseArgs(argc, argv, opts, error)) {
        std::cerr << "delivery_sim: " << error << "\n";
        printUsage(std::cerr);
        return 1;
    }

    Config config;
    try {
        config = loadConfig(opts.configPath);
    } catch (const ConfigError& e) {
        std::cerr << "delivery_sim: configuracion invalida: " << e.what() << "\n";
        return 1;
    }

    std::cerr << "delivery_sim: configuracion cargada: " << config.nodes.size() << " nodos, "
              << config.streets.size() << " calles, " << config.restaurants.size() << " restaurantes, "
              << config.fleet.couriers << " repartidores; mapa " << config.imagePath()
              << "; log " << opts.logPath << "\n";
    std::cerr << "delivery_sim: la simulacion todavia no esta implementada\n";
    return 1;
}
