#pragma once

#include <cstddef>
#include <limits>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "graph.h"

// Distancias mas cortas, en metros y respetando sentidos, desde un conjunto
// de origenes (los restaurantes) hasta todos los nodos de la red. Se calcula
// una vez al arrancar con un Dijkstra uno-a-todos por origen; despues cada
// consulta es una lectura.
//
// Inmutable despues de construirse: se consulta desde cualquier hilo sin
// cerrojo.
class DistanceTable {
public:
    static constexpr double kUnreachable = std::numeric_limits<double>::infinity();

    // Una fila: distancia a cada nodo por indice del grafo, kUnreachable si no hay camino.
    using Row = std::vector<double>;

    // Dijkstra uno-a-todos desde `origin`. Es la unidad de trabajo que se
    // reparte entre tareas cuando la tabla se construye en paralelo.
    static Row computeRow(const Graph& graph, const std::string& origin);

    // Construye las filas de `origins` una tras otra (version serial).
    DistanceTable(const Graph& graph, const std::vector<std::string>& origins);

    // Construye la tabla a partir de filas ya calculadas (misma posicion que `origins`).
    DistanceTable(const Graph& graph, const std::vector<std::string>& origins, std::vector<Row> rows);

    std::size_t originCount() const { return rows_.size(); }
    std::size_t nodeCount() const { return nodeCount_; }
    bool hasOrigin(const std::string& origin) const { return rowIndex_.count(origin) != 0; }

    // Distancia de `origin` a `target`; std::nullopt si `origin` no esta en la
    // tabla o si `target` no es alcanzable. Lanza std::out_of_range si
    // `target` no es un nodo del grafo.
    std::optional<double> distanceM(const std::string& origin, const std::string& target) const;

    // Fila completa de un origen. Lanza std::out_of_range si no esta en la tabla.
    const Row& row(const std::string& origin) const;

    // Cuantos nodos son alcanzables desde `origin`.
    std::size_t reachableCount(const std::string& origin) const;

private:
    const Graph& graph_;
    std::size_t nodeCount_ = 0;
    std::unordered_map<std::string, std::size_t> rowIndex_;
    std::vector<Row> rows_;
};
