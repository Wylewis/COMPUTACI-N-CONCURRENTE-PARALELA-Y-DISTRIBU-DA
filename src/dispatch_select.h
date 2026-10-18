#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "config.h"
#include "estimate.h"
#include "graph.h"
#include "order.h"

// Seleccion del repartidor para un pedido, sobre copias (CourierSnapshot):
// anillos de radio creciente alrededor del restaurante; en cada anillo, map
// de estimaciones y reduce del minimo. Sin hilos ni cerrojos: el despachador
// y, mas adelante, el pool de estimaciones llaman a estas funciones.

constexpr double kFirstRadiusM = 300.0;

// Distancia en linea recta (geodesica) entre dos nodos.
double straightLineM(const Graph& graph, const std::string& a, const std::string& b);

// Candidatos del anillo (innerM, outerM] medido desde el nodo del restaurante
// hasta el nodo donde cada moto quedara libre, solo con lugar en la mochila.
std::vector<CourierSnapshot> ringCandidates(const std::vector<CourierSnapshot>& fleet, const Graph& graph,
                                            const std::string& restaurantNode, double innerM, double outerM);

// map: una estimacion por candidato; las que fallan se descartan.
std::vector<Estimate> estimateAll(const std::vector<CourierSnapshot>& candidates, const Order& order,
                                  const RestaurantConfig& restaurant, const Graph& graph, double speedKmh,
                                  std::int64_t nowMs);

// reduce: la de menor deliveryAtMs; en empate, el id menor (determinista).
std::optional<Estimate> bestEstimate(const std::vector<Estimate>& estimates);

struct Selection {
    Estimate estimate;
    double radiusM = 0.0;  // radio del anillo donde aparecio
    int rings = 0;         // anillos recorridos
    int estimated = 0;     // estimaciones calculadas en total
};

// Radio creciente desde firstRadiusM, doblando, hasta cubrir a toda la flota.
std::optional<Selection> selectCourier(const std::vector<CourierSnapshot>& fleet, const Order& order,
                                       const RestaurantConfig& restaurant, const Graph& graph, double speedKmh,
                                       std::int64_t nowMs, double firstRadiusM = kFirstRadiusM);
