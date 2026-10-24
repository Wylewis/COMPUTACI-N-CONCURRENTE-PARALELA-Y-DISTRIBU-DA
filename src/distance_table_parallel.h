#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "distance_table.h"
#include "graph.h"
#include "hardware.h"

DistanceTable buildDistanceTableParallel(const Graph& graph, const std::vector<std::string>& origins,
                                         unsigned tasks = 0);
