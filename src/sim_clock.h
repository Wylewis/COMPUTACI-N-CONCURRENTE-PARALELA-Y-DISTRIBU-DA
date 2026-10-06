#pragma once

#include <chrono>
#include <cstdint>

// Reloj de la simulacion. Convierte entre el tiempo real del proceso y el
// tiempo simulado, que avanza `timeScale` veces mas rapido (seccion 4 del
// enunciado: "la simulacion corre en tiempo acelerado, controlado por timeScale").
//
// Se basa en steady_clock, que nunca retrocede ni salta aunque cambie la hora
// del sistema: el tiempo simulado de un pedido jamas disminuye.
//
// Despues de start() el objeto es de solo lectura: todos los hilos pueden
// consultarlo a la vez sin cerrojo, porque nadie escribe.
class SimClock {
public:
    using RealClock = std::chrono::steady_clock;
    using RealDuration = RealClock::duration;
    using RealTimePoint = RealClock::time_point;

    // timeScale: segundos simulados por segundo real. Debe ser > 0.
    explicit SimClock(double timeScale);

    // Fija el instante real que corresponde a t = 0 ms simulados.
    void start(RealTimePoint realStart = RealClock::now());

    double timeScale() const { return scale_; }
    RealTimePoint realStart() const { return start_; }

    // Milisegundos simulados transcurridos desde start().
    std::int64_t nowMs() const { return simMsAt(RealClock::now()); }

    // Milisegundos simulados que corresponden a un instante real dado.
    std::int64_t simMsAt(RealTimePoint realNow) const;

    // Cuanto tiempo real hay que esperar para que pasen `simMs` simulados.
    RealDuration toReal(std::int64_t simMs) const;

    // Cuantos milisegundos simulados caben en una duracion real.
    std::int64_t toSimMs(RealDuration real) const;

    // Instante real en que el reloj simulado alcanzara `simMs`.
    // Es lo que se pasa a wait_until para dormir "hasta las t simulados".
    RealTimePoint realTimeFor(std::int64_t simMs) const;

private:
    double scale_;
    RealTimePoint start_;
};
