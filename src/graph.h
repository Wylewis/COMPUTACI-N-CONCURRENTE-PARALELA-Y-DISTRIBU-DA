#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "config.h"

double distanceM(double lat1, double lon1, double lat2, double lon2);

struct Route {
    std::vector<std::string> nodes;
    std::vector<std::string> streets;
    double lengthM = 0.0;
};

class Graph {
public:
    struct Node {
        std::string id;
        double lat = 0.0;
        double lon = 0.0;
    };

    struct Edge {
        std::size_t to = 0;
        double lengthM = 0.0;
        std::string streetId;
    };

    explicit Graph(const Config& cfg);

    std::size_t nodeCount() const { return nodes_.size(); }
    std::size_t edgeCount() const { return edgeCount_; }

    bool hasNode(const std::string& id) const { return index_.count(id) != 0; }

    const Node& node(const std::string& id) const;
    const std::vector<Edge>& edgesFrom(const std::string& id) const;

    std::size_t nodeIndex(const std::string& id) const { return indexOf(id); }
    const Node& nodeAt(std::size_t index) const { return nodes_.at(index); }
    const std::vector<Edge>& edgesAt(std::size_t index) const { return adjacency_.at(index); }

    std::optional<Route> shortestPath(const std::string& from, const std::string& to) const;

    bool reachable(const std::string& from, const std::string& to) const;

    std::vector<std::string> reachableFrom(const std::string& from) const;

private:
    std::size_t indexOf(const std::string& id) const;

    std::vector<Node> nodes_;
    std::vector<std::vector<Edge>> adjacency_;
    std::unordered_map<std::string, std::size_t> index_;
    std::size_t edgeCount_ = 0;
};
