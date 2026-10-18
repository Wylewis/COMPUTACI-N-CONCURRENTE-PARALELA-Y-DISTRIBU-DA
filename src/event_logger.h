#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "event_log.h"

// El log de eventos como productor-consumidor: todos los hilos publican
// eventos en una cola, y un unico hilo escritor los saca y los escribe con
// EventLog. Asi hay un solo escritor del archivo (ninguna linea se mezcla),
// la escritura a disco queda fuera de las secciones criticas de quien
// publica, y los contadores del resumen los lleva ese unico hilo.
//
// La cola es un monitor: el mutex cubre solo la cola y la bandera de cierre;
// el escritor toma todo lo pendiente de una vez y escribe fuera del cerrojo.
// close() cierra la cola, espera a que se escriba lo pendiente y une el hilo;
// despues de close() el resumen es definitivo y se puede leer.
class EventLogger {
public:
    EventLogger(const std::string& path, const std::vector<std::string>& courierIds = {});
    ~EventLogger();

    EventLogger(const EventLogger&) = delete;
    EventLogger& operator=(const EventLogger&) = delete;

    // Encola un evento. Devuelve false (y lo descarta) si el log ya esta cerrado.
    bool publish(Event event);

    // Cierra la cola, escribe lo pendiente y une el hilo escritor. Idempotente.
    void close();

    bool closed() const { return closedFlag_.load(std::memory_order_acquire); }

    // Eventos publicados y eventos ya escritos en el archivo.
    std::int64_t published() const { return published_.load(std::memory_order_acquire); }
    std::int64_t written() const { return written_.load(std::memory_order_acquire); }

    // Solo despues de close(): lanza std::logic_error si el escritor sigue activo.
    Summary summary() const;

private:
    void writerLoop();

    EventLog log_;
    std::deque<Event> queue_;
    std::mutex mutex_;
    std::condition_variable available_;
    bool closed_ = false;                 // bajo mutex_: la revisa el predicado del escritor
    std::atomic<bool> closedFlag_{false}; // lectura rapida sin cerrojo
    std::atomic<std::int64_t> published_{0};
    std::atomic<std::int64_t> written_{0};
    std::thread writer_;
};
