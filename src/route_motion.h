#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "graph.h"

struct GeoPoint {
    double lat = 0.0;
    double lon = 0.0;
};

// Milisegundos simulados que lleva recorrer `meters` a `speedKmh`.
// Lanza std::invalid_argument si la velocidad no es positiva.
std::int64_t travelTimeMs(double meters, double speedKmh);

// Punto intermedio entre a y b: t = 0 da a, t = 1 da b (interpolacion lineal
// en lat/lon, suficiente a la escala de una ciudad).
GeoPoint interpolate(const GeoPoint& a, const GeoPoint& b, double t);

// Un tramo de una ruta: una calle entre dos nodos consecutivos.
struct RouteLeg {
    std::string from;
    std::string to;
    std::string streetId;
    GeoPoint a;
    GeoPoint b;
    double lengthM = 0.0;
};

// El movimiento de una moto por una ruta a velocidad constante, a partir de un
// instante simulado. Es un objeto de solo lectura una vez construido: el hilo
// del repartidor lo crea al salir de viaje, y la ventana lo consulta para
// dibujar la posicion en cualquier instante sin tocar al repartidor.
class RouteMotion {
public:
    // Lanza std::invalid_argument si la ruta esta vacia o la velocidad no es positiva.
    RouteMotion(const Route& route, const Graph& graph, double speedKmh, std::int64_t startMs);

    const std::vector<RouteLeg>& legs() const { return legs_; }
    double lengthM() const { return lengthM_; }
    double speedKmh() const { return speedKmh_; }
    std::int64_t startMs() const { return startMs_; }
    std::int64_t durationMs() const { return durationMs_; }
    std::int64_t arrivalMs() const { return startMs_ + durationMs_; }

    // Metros recorridos en el instante t: 0 antes de salir, la longitud total
    // despues de llegar.
    double metersAt(std::int64_t simMs) const;

    // Posicion sobre la ruta a `meters` del inicio (recortado a [0, longitud]).
    GeoPoint positionAtMeters(double meters) const;

    // Posicion en el instante simulado t.
    GeoPoint positionAt(std::int64_t simMs) const { return positionAtMeters(metersAt(simMs)); }

    // Indice del tramo que se esta recorriendo en el instante t
    // (el ultimo tramo una vez llegado).
    std::size_t legIndexAt(std::int64_t simMs) const;

    // Duracion de cada tramo, en el mismo orden que legs().
    std::vector<std::int64_t> legDurationsMs() const;

    bool arrived(std::int64_t simMs) const { return simMs >= arrivalMs(); }

private:
    std::vector<RouteLeg> legs_;
    std::vector<double> cumulativeM_;  // metros al final de cada tramo
    double lengthM_ = 0.0;
    double speedKmh_ = 0.0;
    std::int64_t startMs_ = 0;
    std::int64_t durationMs_ = 0;
    GeoPoint origin_;
};
