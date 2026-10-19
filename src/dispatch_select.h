#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "config.h"
#include "estimate.h"
#include "graph.h"
#include "order.h"
#include "thread_pool.h"

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

// map concurrente: una tarea por candidato con std::async(launch::async).
// Espera los resultados hasta `quoteTimeout` (tiempo real); los que llegan
// tarde se ignoran y una estimacion que lanza se descarta como error. La
// estimacion de un candidato la hace `one` (normalmente estimateDelivery).
using OneEstimate = std::function<Estimate(const CourierSnapshot&)>;
struct AsyncEstimates {
    std::vector<Estimate> estimates;
    int late = 0;
    int failed = 0;
};
AsyncEstimates estimateAllAsync(const std::vector<CourierSnapshot>& candidates, const OneEstimate& one,
                                std::chrono::milliseconds quoteTimeout);

// Igual, pero las tareas van al pool en vez de a un hilo nuevo cada una. Los
// futures del pool vienen de packaged_task y no se unen al destruirse: una
// estimacion tardia sigue en el pool sin retener al despachador.
AsyncEstimates estimateAllPooled(const std::vector<CourierSnapshot>& candidates, const OneEstimate& one,
                                 ThreadPool& pool, std::chrono::milliseconds quoteTimeout);

// reduce: la de menor deliveryAtMs; en empate, el id menor (determinista).
std::optional<Estimate> bestEstimate(const std::vector<Estimate>& estimates);

struct Selection {
    Estimate estimate;
    double radiusM = 0.0;  // radio del anillo donde aparecio
    int rings = 0;         // anillos recorridos
    int estimated = 0;     // estimaciones calculadas en total
};

// Radio creciente desde firstRadiusM, doblando, hasta cubrir a toda la flota.
// El map de cada anillo lo hace `mapper`; por defecto, estimateAll en serie.
using Mapper = std::function<std::vector<Estimate>(const std::vector<CourierSnapshot>&)>;
std::optional<Selection> selectCourier(const std::vector<CourierSnapshot>& fleet, const Order& order,
                                       const RestaurantConfig& restaurant, const Graph& graph, double speedKmh,
                                       std::int64_t nowMs, double firstRadiusM = kFirstRadiusM,
                                       const Mapper& mapper = {});
