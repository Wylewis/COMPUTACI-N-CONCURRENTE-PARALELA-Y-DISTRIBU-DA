#include <cstdio>
#include <fstream>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "event_logger.h"
#include "nlohmann/json.hpp"

using json = nlohmann::json;

namespace {

int failures = 0;

void check(bool ok, const std::string& what) {
    if (!ok) {
        ++failures;
        std::cerr << "FALLO: " << what << "\n";
    }
}

std::vector<std::string> readLines(const std::string& path) {
    std::ifstream in(path);
    std::vector<std::string> lines;
    std::string line;
    while (std::getline(in, line)) lines.push_back(line);
    return lines;
}

}

int main() {
    const std::string path = "event_logger_test.tmp.log";
    std::remove(path.c_str());
    const int producers = 8, perProducer = 500;

    {
        Summary summary;
        {
            EventLogger logger(path, {"c0"});
            std::vector<std::thread> threads;
            for (int p = 0; p < producers; ++p) {
                threads.emplace_back([&logger, p] {
                    for (int k = 0; k < perProducer; ++k) {
                        const std::string id = "p" + std::to_string(p) + "-" + std::to_string(k);
                        logger.publish(Event::orderCreated(1000 + k, id, "r0"));
                    }
                });
            }
            for (auto& t : threads) t.join();
            check(logger.published() == producers * perProducer, "se publicaron todos los eventos");
            bool threw = false;
            try {
                logger.summary();
            } catch (const std::logic_error&) {
                threw = true;
            }
            check(threw, "el resumen antes de cerrar lanza");
            logger.close();
            check(logger.written() == logger.published(), "al cerrar, todo lo publicado esta escrito");
            check(!logger.publish(Event::simulationStopping(5)), "publicar tras cerrar devuelve false");
            summary = logger.summary();
        }
        const auto lines = readLines(path);
        check(static_cast<int>(lines.size()) == producers * perProducer, "el archivo tiene una linea por evento");

        std::map<int, int> lastK;
        bool allJson = true, ordered = true, noDuplicates = true;
        std::map<std::string, int> seen;
        for (const auto& l : lines) {
            json j;
            try {
                j = json::parse(l);
            } catch (const json::parse_error&) {
                allJson = false;
                continue;
            }
            if (!j.is_object() || !j.contains("order")) {
                allJson = false;
                continue;
            }
            const std::string id = j["order"];
            if (++seen[id] > 1) noDuplicates = false;
            const int p = std::stoi(id.substr(1, id.find('-') - 1));
            const int k = std::stoi(id.substr(id.find('-') + 1));
            auto it = lastK.find(p);
            if (it != lastK.end() && k != it->second + 1) ordered = false;
            lastK[p] = k;
        }
        check(allJson, "cada linea es un objeto JSON completo: ninguna se mezclo");
        check(noDuplicates, "ningun evento se escribio dos veces");
        check(ordered, "los eventos de cada productor salen en su orden de publicacion");
        check(summary.created == producers * perProducer, "el resumen cuenta todos los orderCreated");
        check(summary.pending == producers * perProducer, "todos quedan pending");
        check(summary.deliveredPerCourier.count("c0") == 1, "deliveredPerCourier lista al repartidor dado");
    }

    {
        {
            EventLogger logger(path);
            for (int i = 0; i < 100; ++i) logger.publish(Event::orderCreated(i, "o" + std::to_string(i), "r0"));
        }
        check(readLines(path).size() == 100, "el destructor escribe lo pendiente antes de terminar");
    }

    {
        EventLogger logger(path, {"c0", "c1"});
        logger.publish(Event::orderCreated(1000, "o1", "r0"));
        logger.publish(Event::orderAssigned(1500, "o1", "c0"));
        logger.publish(Event::pickupStarted(4000, "o1", "c0", "r0"));
        logger.publish(Event::orderPickedUp(4500, "o1", "c0", "r0"));
        logger.publish(Event::orderDelivered(9000, "o1", "c0"));
        logger.publish(Event::simulationStopping(10000));
        logger.close();
        logger.close();
        const Summary s = logger.summary();
        check(s.created == 1 && s.delivered == 1 && s.pending == 0 && s.avgDeliveryMs == 8000,
              "resumen correcto tras un flujo completo");
        check(s.deliveredPerCourier.at("c0") == 1 && s.deliveredPerCourier.at("c1") == 0,
              "reparto por repartidor con ceros");
    }

    {
        bool threw = false;
        try {
            EventLogger bad("/directorio/inexistente/events.log");
        } catch (const std::runtime_error&) {
            threw = true;
        }
        check(threw, "una ruta que no se puede escribir lanza en el constructor");
    }

    std::remove(path.c_str());
    if (failures == 0) {
        std::cout << "event_logger_test: todas las pruebas pasaron\n";
        return 0;
    }
    std::cerr << "event_logger_test: " << failures << " prueba(s) fallaron\n";
    return 1;
}
