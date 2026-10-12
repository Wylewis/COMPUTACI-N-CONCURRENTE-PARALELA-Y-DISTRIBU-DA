// Pruebas del log de eventos: una linea JSON valida por evento con solo los
// campos que corresponden, contadores iguales a las lineas escritas, y el
// resumen con el formato de la seccion 6.3.
#include <cstdio>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "event_log.h"
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

bool hasExactly(const json& j, const std::vector<std::string>& keys) {
    if (j.size() != keys.size()) return false;
    for (const auto& k : keys)
        if (!j.contains(k)) return false;
    return true;
}

}  // namespace

int main() {
    const std::string path = "event_log_test.tmp.log";
    std::remove(path.c_str());

    Summary summary;
    std::int64_t written = 0;
    {
        EventLog log(path, {"c0", "c1"});
        log.write(Event::orderCreated(1000, "o1", "r0"));
        log.write(Event::orderCreated(1000, "o2", "r0"));       // misma t: rafaga
        log.write(Event::orderCreated(1200, "o3", "r1"));
        log.write(Event::orderRejected(1200, "o3", RejectReason::Unreachable));
        log.write(Event::orderAssigned(1500, "o1", "c0"));
        log.write(Event::orderAssigned(1600, "o2", "c1"));
        log.write(Event::pickupStarted(4000, "o1", "c0", "r0"));
        log.write(Event::orderPickedUp(4500, "o1", "c0", "r0"));
        log.write(Event::courierBreakdown(5000, "c1"));
        log.write(Event::orderReassigned(5000, "o2", "c1", "c0"));
        log.write(Event::orderDelivered(9000, "o1", "c0"));     // 8000 ms desde su creacion
        log.write(Event::orderCreated(9500, "o4", "r1"));
        log.write(Event::simulationStopping(10000));
        written = log.linesWritten();
        check(log.clockFixes() == 0, "ningun ajuste de reloj en una secuencia ordenada");
        summary = log.summary();
        log.close();
    }

    const auto lines = readLines(path);
    check(static_cast<std::int64_t>(lines.size()) == written, "el archivo tiene tantas lineas como eventos escritos");
    check(lines.size() == 13, "se escribieron 13 lineas");

    // Cada linea es un objeto JSON completo por si solo
    std::vector<json> parsed;
    for (const auto& l : lines) {
        try {
            json j = json::parse(l);
            check(j.is_object(), "cada linea es un objeto JSON");
            check(j.contains("t") && j.contains("event"), "cada linea tiene t y event");
            parsed.push_back(j);
        } catch (const json::parse_error&) {
            check(false, "linea con JSON invalido: " + l);
        }
    }
    check(parsed.size() == 13, "las 13 lineas parsean");
    if (parsed.size() == 13) {
        check(hasExactly(parsed[0], {"t", "event", "order", "restaurant"}), "orderCreated lleva order y restaurant, nada mas");
        check(parsed[0]["event"] == "orderCreated" && parsed[0]["t"] == 1000, "orderCreated con t y nombre exactos");
        check(hasExactly(parsed[3], {"t", "event", "order", "reason"}) && parsed[3]["reason"] == "unreachable",
              "orderRejected lleva reason con el nombre exacto");
        check(hasExactly(parsed[4], {"t", "event", "order", "courier"}), "orderAssigned lleva order y courier");
        check(hasExactly(parsed[6], {"t", "event", "order", "courier", "restaurant"}), "pickupStarted lleva order, courier y restaurant");
        check(hasExactly(parsed[8], {"t", "event", "courier"}), "courierBreakdown lleva solo courier");
        check(hasExactly(parsed[9], {"t", "event", "order", "from", "to"}) && parsed[9]["from"] == "c1" && parsed[9]["to"] == "c0",
              "orderReassigned lleva from y to");
        check(hasExactly(parsed[12], {"t", "event"}) && parsed[12]["event"] == "simulationStopping",
              "simulationStopping lleva solo t y event");
        check(lines[0].rfind("{\"t\":", 0) == 0, "t es la primera clave de la linea");
    }

    // El resumen coincide con las lineas del log
    check(summary.created == 4, "created = 4 lineas orderCreated");
    check(summary.delivered == 1, "delivered = 1 linea orderDelivered");
    check(summary.rejected == 1, "rejected = 1 linea orderRejected");
    check(summary.pending == 2, "pending = created - delivered - rejected = 2");
    check(summary.reassigned == 1, "reassigned = 1");
    check(summary.breakdowns == 1, "breakdowns = 1");
    check(summary.avgDeliveryMs == 8000 && summary.maxDeliveryMs == 8000, "tiempos de entrega desde orderCreated");
    check(summary.deliveredPerCourier.size() == 2, "deliveredPerCourier lista a todos los repartidores");
    check(summary.deliveredPerCourier["c0"] == 1 && summary.deliveredPerCourier["c1"] == 0,
          "deliveredPerCourier cuenta por repartidor, con ceros");

    const std::string s = summary.toJsonLine();
    json sj = json::parse(s);
    check(hasExactly(sj, {"created", "delivered", "rejected", "pending", "reassigned", "breakdowns",
                          "avgDeliveryMs", "maxDeliveryMs", "deliveredPerCourier"}),
          "el resumen tiene exactamente las claves de la seccion 6.3");
    check(s.rfind("{\"created\":", 0) == 0, "created es la primera clave del resumen");
    check(sj["deliveredPerCourier"]["c0"] == 1, "deliveredPerCourier en el JSON del resumen");
    check(s.find('\n') == std::string::npos, "el resumen es una sola linea");

    // Promedio con varias entregas: (8000 + 3000) / 2 = 5500
    {
        EventLog log(path);
        log.write(Event::orderCreated(0, "a", "r0"));
        log.write(Event::orderCreated(0, "b", "r0"));
        log.write(Event::orderDelivered(8000, "a", "c0"));
        log.write(Event::orderDelivered(3000, "b", "c0"));
        const auto sm = log.summary();
        check(sm.avgDeliveryMs == 5500 && sm.maxDeliveryMs == 8000, "promedio y maximo con dos entregas");
        check(sm.deliveredPerCourier.size() == 1 && sm.deliveredPerCourier.at("c0") == 2,
              "sin lista previa, aparecen solo los repartidores que entregaron");
    }

    // El tiempo de un pedido nunca retrocede en el log
    {
        EventLog log(path);
        log.write(Event::orderCreated(5000, "x", "r0"));
        log.write(Event::orderAssigned(4000, "x", "c0"));   // t menor: se escribe 5000
        log.close();
        const auto l = readLines(path);
        check(l.size() == 2 && json::parse(l[1])["t"] == 5000, "un t menor se eleva al t anterior del pedido");
        check(log.clockFixes() == 1, "el ajuste se cuenta");
    }

    // Escribir despues de cerrar es un error de programacion
    {
        EventLog log(path);
        log.close();
        bool threw = false;
        try {
            log.write(Event::simulationStopping(1));
        } catch (const std::logic_error&) {
            threw = true;
        }
        check(threw, "escribir en un log cerrado lanza");
    }

    // Ruta imposible de abrir
    {
        bool threw = false;
        try {
            EventLog bad("/directorio/que/no/existe/events.log");
        } catch (const std::runtime_error&) {
            threw = true;
        }
        check(threw, "una ruta que no se puede escribir lanza");
    }

    check(std::string(kStartLine) == "Simulation has started...", "linea de inicio exacta");

    std::remove(path.c_str());
    if (failures == 0) {
        std::cout << "event_log_test: todas las pruebas pasaron\n";
        return 0;
    }
    std::cerr << "event_log_test: " << failures << " prueba(s) fallaron\n";
    return 1;
}
