#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>

// La orden de parar, compartida por todos los hilos de la simulacion.
//
// Leer si se pidio parar es una bandera atomica: barato, sin cerrojo, y es lo
// unico que se puede hacer desde un manejador de senal. Esperar a que se pida
// parar, en cambio, usa una variable de condicion, para que los hilos duerman
// sin consumir CPU y una sola llamada a request() los despierte a todos.
//
// Un hilo que espera tiempo simulado (un viaje, una preparacion) espera aqui
// con waitUntil(): duerme hasta el instante pedido o hasta que se ordene
// parar, lo que ocurra primero. Asi la parada nunca queda bloqueada detras de
// una espera larga.
class StopSignal {
public:
    StopSignal() = default;
    StopSignal(const StopSignal&) = delete;
    StopSignal& operator=(const StopSignal&) = delete;

    bool requested() const { return flag_.load(std::memory_order_acquire); }

    // Pide parar y despierta a todos los que esperan. Idempotente.
    void request();

    // Duerme hasta que se pida parar.
    void wait() const;

    // Duerme hasta `deadline` o hasta que se pida parar. Devuelve true si se
    // pidio parar (y entonces no hay que seguir con lo que se esperaba).
    template <class Clock, class Duration>
    bool waitUntil(const std::chrono::time_point<Clock, Duration>& deadline) const {
        std::unique_lock<std::mutex> lock(mutex_);
        cv_.wait_until(lock, deadline, [this] { return flag_.load(std::memory_order_acquire); });
        return flag_.load(std::memory_order_acquire);
    }

    template <class Rep, class Period>
    bool waitFor(const std::chrono::duration<Rep, Period>& duration) const {
        return waitUntil(std::chrono::steady_clock::now() + duration);
    }

private:
    std::atomic<bool> flag_{false};
    mutable std::mutex mutex_;
    mutable std::condition_variable cv_;
};

// Convierte SIGINT y SIGTERM en una llamada a StopSignal::request().
//
// Un manejador de senal no puede tomar un mutex ni notificar una variable de
// condicion: lo unico seguro es escribir un byte en un pipe. El vigia es un
// hilo que duerme en read() sobre ese pipe, sin consumir CPU; cuando llega el
// byte, pide parar. Si la parada llega por otro camino (tiempo cumplido), el
// destructor escribe el byte el mismo para despertar al vigia y unirlo.
class SignalWatcher {
public:
    explicit SignalWatcher(StopSignal& stop);
    ~SignalWatcher();

    SignalWatcher(const SignalWatcher&) = delete;
    SignalWatcher& operator=(const SignalWatcher&) = delete;

    // Senales recibidas hasta ahora (para el log y las pruebas).
    int signalsReceived() const { return received_.load(std::memory_order_acquire); }

private:
    void watch();

    StopSignal& stop_;
    int pipeFds_[2] = {-1, -1};
    std::atomic<int> received_{0};
    std::thread thread_;
};
