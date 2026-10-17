#include "distance_table_parallel.h"

#include <algorithm>
#include <future>
#include <thread>
#include <utility>

unsigned defaultTaskCount(std::size_t work) {
    unsigned cores = std::thread::hardware_concurrency();
    if (cores == 0) cores = 2;  // el hardware no informa: una suposicion conservadora
    const std::size_t limit = std::max<std::size_t>(1, work);
    return static_cast<unsigned>(std::min<std::size_t>(cores, limit));
}

DistanceTable buildDistanceTableParallel(const Graph& graph, const std::vector<std::string>& origins,
                                         unsigned tasks) {
    const std::size_t n = origins.size();
    std::vector<DistanceTable::Row> rows(n);  // una fila por origen, ya dimensionado
    if (n == 0) return DistanceTable(graph, origins, std::move(rows));

    if (tasks == 0) tasks = defaultTaskCount(n);
    tasks = static_cast<unsigned>(std::min<std::size_t>(tasks, n));

    // Fan-out: la tarea k calcula las filas [begin, end), un rango disjunto.
    // Escribir en elementos distintos de un vector desde hilos distintos no es
    // una carrera de datos: son objetos distintos.
    std::vector<std::future<void>> pending;
    pending.reserve(tasks);
    const std::size_t chunk = (n + tasks - 1) / tasks;
    for (unsigned k = 0; k < tasks; ++k) {
        const std::size_t begin = static_cast<std::size_t>(k) * chunk;
        const std::size_t end = std::min(n, begin + chunk);
        if (begin >= end) break;
        pending.push_back(std::async(std::launch::async, [&graph, &origins, &rows, begin, end] {
            for (std::size_t i = begin; i < end; ++i) {
                rows[i] = DistanceTable::computeRow(graph, origins[i]);
            }
        }));
    }

    // Fan-in: esperar a todas. get() relanza aqui cualquier excepcion de la tarea.
    for (auto& f : pending) f.get();

    return DistanceTable(graph, origins, std::move(rows));
}
