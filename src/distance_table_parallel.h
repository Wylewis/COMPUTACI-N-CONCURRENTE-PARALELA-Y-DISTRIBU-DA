#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "distance_table.h"
#include "graph.h"
#include "hardware.h"

// Construye la misma tabla que DistanceTable(graph, origins), pero repartiendo
// los origenes entre `tasks` tareas (0 = defaultTaskCount) lanzadas con
// std::async. Cada tarea calcula las filas de su grupo y las escribe en
// posiciones disjuntas de un vector ya dimensionado, asi no hay nada que
// proteger con un cerrojo; al final se esperan todas las tareas (fan-in).
// Si una tarea lanza (por ejemplo un origen desconocido), la excepcion llega
// aqui a traves de su future y se propaga.
DistanceTable buildDistanceTableParallel(const Graph& graph, const std::vector<std::string>& origins,
                                         unsigned tasks = 0);
