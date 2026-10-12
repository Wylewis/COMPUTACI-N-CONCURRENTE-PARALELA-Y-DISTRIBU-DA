#pragma once

#include <cstdint>
#include <fstream>
#include <map>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "order.h"

// Linea exacta que se imprime en stdout cuando la simulacion esta lista (6.1).
constexpr const char* kStartLine = "Simulation has started...";

// Un evento del log (seccion 6.2). Solo se escriben los campos presentes.
struct Event {
    std::int64_t t = 0;                      // milisegundos simulados desde el inicio
    EventType type = EventType::OrderCreated;
    std::optional<std::string> order;
    std::optional<std::string> courier;
    std::optional<std::string> restaurant;
    std::optional<std::string> from;         // orderReassigned: repartidor que entrega
    std::optional<std::string> to;           // orderReassigned: repartidor que recibe
    std::optional<RejectReason> reason;      // orderRejected

    // Constructores de conveniencia para cada evento, con los campos que exige la tabla 6.2.
    static Event orderCreated(std::int64_t t, const std::string& order, const std::string& restaurant);
    static Event orderAssigned(std::int64_t t, const std::string& order, const std::string& courier);
    static Event pickupStarted(std::int64_t t, const std::string& order, const std::string& courier,
                               const std::string& restaurant);
    static Event orderPickedUp(std::int64_t t, const std::string& order, const std::string& courier,
                               const std::string& restaurant);
    static Event orderDelivered(std::int64_t t, const std::string& order, const std::string& courier);
    static Event orderRejected(std::int64_t t, const std::string& order, RejectReason reason);
    static Event courierBreakdown(std::int64_t t, const std::string& courier);
    static Event orderReassigned(std::int64_t t, const std::string& order, const std::string& from,
                                 const std::string& to);
    static Event simulationStopping(std::int64_t t);

    // El objeto JSON de una linea, sin salto de linea.
    std::string toJsonLine() const;
};

// Resumen final (seccion 6.3), derivado de los eventos escritos.
struct Summary {
    std::int64_t created = 0;
    std::int64_t delivered = 0;
    std::int64_t rejected = 0;
    std::int64_t pending = 0;      // created - delivered - rejected
    std::int64_t reassigned = 0;
    std::int64_t breakdowns = 0;
    std::int64_t avgDeliveryMs = 0;  // de orderCreated a orderDelivered, redondeado
    std::int64_t maxDeliveryMs = 0;
    std::map<std::string, std::int64_t> deliveredPerCourier;

    // El objeto JSON de una linea, con las claves en el orden de la seccion 6.3.
    std::string toJsonLine() const;
};

// Escribe el log de eventos: un objeto JSON por linea, y lleva los contadores
// del resumen a medida que escribe, de modo que el resumen coincide con el log
// por construccion.
//
// Esta clase no es segura para varios hilos: en la simulacion la usara un solo
// hilo escritor que consume una cola de eventos.
class EventLog {
public:
    // Abre (y trunca) el archivo. Lanza std::runtime_error si no se puede escribir.
    // courierIds: repartidores que deben aparecer en deliveredPerCourier aunque
    // no entreguen nada.
    explicit EventLog(const std::string& path, const std::vector<std::string>& courierIds = {});
    ~EventLog();

    EventLog(const EventLog&) = delete;
    EventLog& operator=(const EventLog&) = delete;

    // Escribe una linea y actualiza los contadores. Si el t de un pedido fuera
    // menor que el de su evento anterior, se escribe con el t anterior para que
    // el log nunca retroceda; esos casos se cuentan en clockFixes().
    void write(const Event& e);

    // Vacia los buffers y cierra el archivo. Idempotente.
    void close();

    Summary summary() const;
    std::int64_t linesWritten() const { return lines_; }
    std::int64_t clockFixes() const { return clockFixes_; }

private:
    std::ofstream out_;
    bool closed_ = false;
    std::int64_t lines_ = 0;
    std::int64_t clockFixes_ = 0;

    Summary summary_;
    std::int64_t deliveredTotalMs_ = 0;
    std::unordered_map<std::string, std::int64_t> createdAt_;  // pedido -> t de creacion
    std::unordered_map<std::string, std::int64_t> lastT_;      // pedido -> ultimo t escrito
};
