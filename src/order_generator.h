#pragma once

#include <cstdint>
#include <random>
#include <vector>

#include "config.h"
#include "graph.h"
#include "order.h"

// Una rafaga: uno o mas pedidos creados en el mismo instante simulado.
struct OrderBatch {
    std::int64_t atMs = 0;
    std::vector<Order> orders;
};

// Produce la secuencia de pedidos de la simulacion (seccion 4, "Pedidos"):
// a intervalos aleatorios con media `orders.meanIntervalMs`, en rafagas de
// hasta `orders.burstMax`, cada pedido con un restaurante y un nodo de entrega
// al azar, y su hora de preparacion lista segun `prepTimeMs` del restaurante.
//
// Si el nodo de entrega no tiene camino dirigido desde el restaurante, el
// pedido sale ya en estado Rejected con motivo Unreachable: quien lo reciba
// registra orderCreated y orderRejected.
//
// Toda decision aleatoria sale de un unico generador sembrado con
// `orders.seed`, en un orden fijo de extracciones, asi la misma semilla
// produce exactamente la misma secuencia de pedidos.
//
// No es seguro para varios hilos: lo usa el hilo generador, que es el unico
// que crea pedidos.
class OrderGenerator {
public:
    OrderGenerator(const Config& cfg, const Graph& graph);

    // Instante simulado de la proxima rafaga.
    std::int64_t nextAtMs() const { return nextAtMs_; }

    // Crea la proxima rafaga y programa la siguiente.
    OrderBatch next();

    // Pedidos creados hasta ahora (incluidos los rechazados por unreachable).
    std::int64_t created() const { return counter_; }

private:
    std::int64_t drawIntervalMs();
    int drawBurstSize();

    const Config& cfg_;
    const Graph& graph_;
    std::mt19937_64 rng_;
    std::int64_t nextAtMs_ = 0;
    std::int64_t counter_ = 0;
};

// Probabilidad de que una llegada sea una rafaga de 2 a burstMax pedidos en
// lugar de un pedido solo. El enunciado no la configura; es una constante del
// modelo y queda expuesta para los experimentos.
constexpr double kBurstProbability = 0.2;
