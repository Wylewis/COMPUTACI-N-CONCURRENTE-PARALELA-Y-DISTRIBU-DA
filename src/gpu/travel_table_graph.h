#pragma once

#include <algorithm>
#include <cmath>
#include <deque>
#include <vector>

#include "graph.h"
#include "travel_table.h"

inline int travelMsOf(double lengthM, double speedKmh) {
    const double ms = lengthM / (speedKmh / 3.6) * 1000.0;
    return std::max(1, static_cast<int>(std::llround(ms)));
}

inline CsrGraph csrFromGraph(const Graph& graph, double speedKmh) {
    CsrGraph g;
    g.n = static_cast<int>(graph.nodeCount());
    g.rowStart.assign(static_cast<std::size_t>(g.n) + 1, 0);
    for (int i = 0; i < g.n; ++i) {
        for (const Graph::Edge& e : graph.edgesAt(static_cast<std::size_t>(i))) {
            g.to.push_back(static_cast<int>(e.to));
            g.weightMs.push_back(travelMsOf(e.lengthM, speedKmh));
        }
        g.rowStart[static_cast<std::size_t>(i) + 1] = static_cast<int>(g.to.size());
    }
    return g;
}

inline CsrGraph prefixSubgraph(const CsrGraph& g, int start, int wanted) {
    const int n = std::min(wanted, g.n);
    std::vector<int> newIndex(static_cast<std::size_t>(g.n), -1);
    std::vector<int> order;
    std::deque<int> frontier{start};
    newIndex[static_cast<std::size_t>(start)] = 0;
    order.push_back(start);
    while (!frontier.empty() && static_cast<int>(order.size()) < n) {
        const int u = frontier.front();
        frontier.pop_front();
        for (int e = g.rowStart[static_cast<std::size_t>(u)]; e < g.rowStart[static_cast<std::size_t>(u) + 1]; ++e) {
            const int v = g.to[static_cast<std::size_t>(e)];
            if (newIndex[static_cast<std::size_t>(v)] >= 0 || static_cast<int>(order.size()) >= n) continue;
            newIndex[static_cast<std::size_t>(v)] = static_cast<int>(order.size());
            order.push_back(v);
            frontier.push_back(v);
        }
    }
    CsrGraph out;
    out.n = static_cast<int>(order.size());
    out.rowStart.assign(static_cast<std::size_t>(out.n) + 1, 0);
    for (int i = 0; i < out.n; ++i) {
        const int u = order[static_cast<std::size_t>(i)];
        for (int e = g.rowStart[static_cast<std::size_t>(u)]; e < g.rowStart[static_cast<std::size_t>(u) + 1]; ++e) {
            const int v = newIndex[static_cast<std::size_t>(g.to[static_cast<std::size_t>(e)])];
            if (v < 0) continue;
            out.to.push_back(v);
            out.weightMs.push_back(g.weightMs[static_cast<std::size_t>(e)]);
        }
        out.rowStart[static_cast<std::size_t>(i) + 1] = static_cast<int>(out.to.size());
    }
    return out;
}
