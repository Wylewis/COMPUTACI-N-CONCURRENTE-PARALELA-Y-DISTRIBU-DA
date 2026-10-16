#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "config.h"

// Distancia geodesica en metros entre dos coordenadas (formula de haversine).
double distanceM(double lat1, double lon1, double lat2, double lon2);

// Una ruta por la red de calles: nodos visitados en orden, calles recorridas
// (una menos que nodos) y longitud total en metros.
struct Route {
    std::vector<std::string> nodes;
    std::vector<std::string> streets;
    double lengthM = 0.0;
};

// La red de calles como grafo dirigido: cada calle de doble sentido aporta dos
// aristas y cada calle de sentido unico, una. El peso de una arista es la
// distancia geodesica entre sus extremos; como la red trae nodos en los
// quiebres de las calles curvas, el error respecto al trazado real es menor
// que el ancho de una calle.
//
// Inmutable despues de construirse: todos los hilos pueden consultarlo a la
// vez sin cerrojo, porque nadie escribe.
class Graph {
public:
    struct Node {
        std::string id;
        double lat = 0.0;
        double lon = 0.0;
    };

    struct Edge {
        std::size_t to = 0;      // indice del nodo destino
        double lengthM = 0.0;
        std::string streetId;
    };

    // Construye el grafo a partir de un config ya validado (las referencias a
    // nodos existen). Lanza std::invalid_argument si una calle apunta a un nodo
    // desconocido.
    explicit Graph(const Config& cfg);

    std::size_t nodeCount() const { return nodes_.size(); }
    std::size_t edgeCount() const { return edgeCount_; }

    bool hasNode(const std::string& id) const { return index_.count(id) != 0; }

    // Lanza std::out_of_range si el id no existe.
    const Node& node(const std::string& id) const;
    const std::vector<Edge>& edgesFrom(const std::string& id) const;

    // Acceso por indice (0 .. nodeCount()-1), para tablas que guardan una
    // entrada por nodo. Lanza std::out_of_range si el id no existe.
    std::size_t nodeIndex(const std::string& id) const { return indexOf(id); }
    const Node& nodeAt(std::size_t index) const { return nodes_.at(index); }
    const std::vector<Edge>& edgesAt(std::size_t index) const { return adjacency_.at(index); }

    // Ruta mas corta respetando sentidos (Dijkstra). std::nullopt si no existe.
    // Lanza std::out_of_range si alguno de los ids no existe.
    std::optional<Route> shortestPath(const std::string& from, const std::string& to) const;

    // Hay camino dirigido de `from` a `to`. Un nodo es alcanzable desde si mismo.
    bool reachable(const std::string& from, const std::string& to) const;

    // Ids de todos los nodos alcanzables desde `from`, incluido `from`.
    std::vector<std::string> reachableFrom(const std::string& from) const;

private:
    std::size_t indexOf(const std::string& id) const;

    std::vector<Node> nodes_;
    std::vector<std::vector<Edge>> adjacency_;
    std::unordered_map<std::string, std::size_t> index_;
    std::size_t edgeCount_ = 0;
};
