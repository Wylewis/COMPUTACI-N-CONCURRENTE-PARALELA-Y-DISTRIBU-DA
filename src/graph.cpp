#include "graph.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <queue>
#include <stdexcept>
#include <utility>

namespace {
constexpr double kEarthRadiusM = 6'371'000.0;
constexpr double kPi = 3.14159265358979323846;

double radians(double degrees) {
    return degrees * kPi / 180.0;
}
}

double distanceM(double lat1, double lon1, double lat2, double lon2) {
    const double p1 = radians(lat1);
    const double p2 = radians(lat2);
    const double dp = p2 - p1;
    const double dl = radians(lon2 - lon1);
    const double a = std::sin(dp / 2) * std::sin(dp / 2) +
                     std::cos(p1) * std::cos(p2) * std::sin(dl / 2) * std::sin(dl / 2);
    return 2.0 * kEarthRadiusM * std::asin(std::sqrt(a));
}

Graph::Graph(const Config& cfg) {
    nodes_.reserve(cfg.nodes.size());
    for (const auto& n : cfg.nodes) {
        index_[n.id] = nodes_.size();
        nodes_.push_back(Node{n.id, n.lat, n.lon});
    }
    adjacency_.assign(nodes_.size(), {});

    for (const auto& s : cfg.streets) {
        auto fromIt = index_.find(s.from);
        auto toIt = index_.find(s.to);
        if (fromIt == index_.end() || toIt == index_.end()) {
            throw std::invalid_argument("la calle '" + s.id + "' apunta a un nodo desconocido");
        }
        const std::size_t a = fromIt->second;
        const std::size_t b = toIt->second;
        const double length = distanceM(nodes_[a].lat, nodes_[a].lon, nodes_[b].lat, nodes_[b].lon);
        adjacency_[a].push_back(Edge{b, length, s.id});
        ++edgeCount_;
        if (!s.oneWay) {
            adjacency_[b].push_back(Edge{a, length, s.id});
            ++edgeCount_;
        }
    }
}

std::size_t Graph::indexOf(const std::string& id) const {
    auto it = index_.find(id);
    if (it == index_.end()) {
        throw std::out_of_range("nodo desconocido: " + id);
    }
    return it->second;
}

const Graph::Node& Graph::node(const std::string& id) const {
    return nodes_[indexOf(id)];
}

const std::vector<Graph::Edge>& Graph::edgesFrom(const std::string& id) const {
    return adjacency_[indexOf(id)];
}

std::optional<Route> Graph::shortestPath(const std::string& from, const std::string& to) const {
    const std::size_t source = indexOf(from);
    const std::size_t target = indexOf(to);

    constexpr double kInf = std::numeric_limits<double>::infinity();
    std::vector<double> dist(nodes_.size(), kInf);
    std::vector<std::size_t> prevNode(nodes_.size(), nodes_.size());
    std::vector<const Edge*> prevEdge(nodes_.size(), nullptr);

    using Item = std::pair<double, std::size_t>;
    std::priority_queue<Item, std::vector<Item>, std::greater<Item>> queue;
    dist[source] = 0.0;
    queue.push({0.0, source});

    while (!queue.empty()) {
        const auto [d, u] = queue.top();
        queue.pop();
        if (d > dist[u]) continue;
        if (u == target) break;
        for (const Edge& e : adjacency_[u]) {
            const double nd = d + e.lengthM;
            if (nd < dist[e.to]) {
                dist[e.to] = nd;
                prevNode[e.to] = u;
                prevEdge[e.to] = &e;
                queue.push({nd, e.to});
            }
        }
    }

    if (dist[target] == kInf) return std::nullopt;

    Route route;
    route.lengthM = dist[target];
    for (std::size_t v = target; v != source; v = prevNode[v]) {
        route.nodes.push_back(nodes_[v].id);
        route.streets.push_back(prevEdge[v]->streetId);
    }
    route.nodes.push_back(nodes_[source].id);
    std::reverse(route.nodes.begin(), route.nodes.end());
    std::reverse(route.streets.begin(), route.streets.end());
    return route;
}

bool Graph::reachable(const std::string& from, const std::string& to) const {
    const std::size_t source = indexOf(from);
    const std::size_t target = indexOf(to);
    if (source == target) return true;

    std::vector<bool> seen(nodes_.size(), false);
    std::vector<std::size_t> stack{source};
    seen[source] = true;
    while (!stack.empty()) {
        const std::size_t u = stack.back();
        stack.pop_back();
        for (const Edge& e : adjacency_[u]) {
            if (e.to == target) return true;
            if (!seen[e.to]) {
                seen[e.to] = true;
                stack.push_back(e.to);
            }
        }
    }
    return false;
}

std::vector<std::string> Graph::reachableFrom(const std::string& from) const {
    const std::size_t source = indexOf(from);
    std::vector<bool> seen(nodes_.size(), false);
    std::vector<std::size_t> stack{source};
    seen[source] = true;
    std::vector<std::string> out;
    while (!stack.empty()) {
        const std::size_t u = stack.back();
        stack.pop_back();
        out.push_back(nodes_[u].id);
        for (const Edge& e : adjacency_[u]) {
            if (!seen[e.to]) {
                seen[e.to] = true;
                stack.push_back(e.to);
            }
        }
    }
    return out;
}
