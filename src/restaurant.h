#pragma once

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <vector>

#include "config.h"
#include "stop_signal.h"

struct QueueSnapshot {
    std::vector<std::string> waiting;  // repartidores en fila, el primero es el siguiente en pasar
    int busy = 0;                      // puestos ocupados
    int slots = 1;
};

// El restaurante como monitor: `pickupSlots` puestos de recogida y una fila
// por orden de llegada. Un repartidor entra en la fila, espera a ser el
// primero y a que haya puesto libre, recoge y deja el puesto. La espera de
// preparacion es asunto del repartidor, fuera de la fila.
class Restaurant {
public:
    explicit Restaurant(const RestaurantConfig& cfg);
    Restaurant(const Restaurant&) = delete;
    Restaurant& operator=(const Restaurant&) = delete;

    const std::string& id() const { return id_; }
    const std::string& node() const { return node_; }
    int slots() const { return slots_; }

    std::size_t enterQueue(const std::string& courierId);  // posicion en la fila (0 = el siguiente)
    // true cuando ocupa un puesto; false si se pidio parar (sale de la fila).
    bool waitForSlot(const std::string& courierId, const StopSignal& stop);
    void leaveSlot();
    void wake();  // despues de stop.request()

    QueueSnapshot snapshot() const;
    std::int64_t served() const;

private:
    const std::string id_, node_;
    const int slots_;
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::deque<std::string> queue_;
    int busy_ = 0;
    std::int64_t served_ = 0;
};

// Instante relativo en que empezaria cada uno de la fila, dado el tiempo de
// servicio de cada uno: scan exclusivo de los servicios, repartido entre los
// puestos. Es la "espera prevista" de los apuntes.
std::vector<std::int64_t> expectedStartMs(const std::vector<std::int64_t>& serviceMs, int slots);
