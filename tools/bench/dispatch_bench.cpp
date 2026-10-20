// Experimento de despacho: el map de estimaciones de un anillo en serie, con
// una tarea std::async por candidato y sobre el pool, para distintos tamanos
// de anillo. Imprime una tabla Markdown con tiempos por map, speedup frente
// a la serie y el costo fijo por tarea que explica el resultado (Amdahl con
// sobrecarga). Uso: dispatch_bench <config.json> [repeticiones]
#include <chrono>
#include <iostream>
#include <string>
#include <vector>

#include "dispatch_select.h"
#include "hardware.h"

using clk = std::chrono::steady_clock;

template <class F>
double microsPer(int reps, F&& f) {
    f();  // calentar
    const auto t0 = clk::now();
    for (int i = 0; i < reps; ++i) f();
    return std::chrono::duration<double, std::micro>(clk::now() - t0).count() / reps;
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "uso: dispatch_bench <config.json> [repeticiones]\n";
        return 2;
    }
    const Config cfg = loadConfig(argv[1]);
    const int reps = argc > 2 ? std::stoi(argv[2]) : 200;
    const Graph g(cfg);
    const RestaurantConfig& r = cfg.restaurants[0];
    Order o;
    o.id = "o", o.restaurantId = r.id, o.deliveryNode = cfg.nodes[200].id;
    const OneEstimate one = [&](const CourierSnapshot& c) { return estimateDelivery(c, o, r, g, cfg.fleet.speedKmh, 0); };
    ThreadPool pool(recommendedPoolSize());
    const auto timeout = std::chrono::milliseconds(1000);

    std::cout << "| candidatos | serie (us) | async (us) | pool (us) | speedup async | speedup pool | por estimacion (us) | sobrecarga async por tarea (us) | sobrecarga pool por tarea (us) |\n";
    std::cout << "|---|---|---|---|---|---|---|---|---|\n";
    for (int n : {1, 8, 32, 128, 512}) {
        std::vector<CourierSnapshot> ring;
        for (int i = 0; i < n; ++i) ring.push_back({"c" + std::to_string(i), cfg.nodes[(i * 37) % cfg.nodes.size()].id, 0, 0, 3, false});
        const double serial = microsPer(reps, [&] { estimateAll(ring, o, r, g, cfg.fleet.speedKmh, 0); });
        const double async = microsPer(reps, [&] { estimateAllAsync(ring, one, timeout); });
        const double pooled = microsPer(reps, [&] { estimateAllPooled(ring, one, pool, timeout); });
        const double perEstimate = serial / n;
        // Modelo: T_par = T_serie / hilos + n * sobrecarga  =>  sobrecarga = (T_par - T_serie / hilos) / n
        const double threads = static_cast<double>(pool.size());
        const double ovAsync = (async - serial / std::min<double>(n, threads)) / n;
        const double ovPool = (pooled - serial / std::min<double>(n, threads)) / n;
        std::cout << "| " << n << " | " << serial << " | " << async << " | " << pooled << " | " << serial / async << " | "
                  << serial / pooled << " | " << perEstimate << " | " << ovAsync << " | " << ovPool << " |\n";
    }
    // Mismo map con estimaciones artificialmente caras (cada una se calcula k
    // veces): muestra donde el paralelismo empieza a pagar la sobrecarga.
    std::cout << "\nEstimaciones caras (cada una repetida k veces), anillo de 8 candidatos:\n\n";
    std::cout << "| k | por estimacion (us) | serie (us) | async (us) | pool (us) | speedup async | speedup pool |\n|---|---|---|---|---|---|---|\n";
    std::vector<CourierSnapshot> ring8;
    for (int i = 0; i < 8; ++i) ring8.push_back({"c" + std::to_string(i), cfg.nodes[(i * 37) % cfg.nodes.size()].id, 0, 0, 3, false});
    for (int k : {1, 4, 16, 64}) {
        const OneEstimate heavy = [&, k](const CourierSnapshot& c) {
            Estimate e;
            for (int i = 0; i < k; ++i) e = one(c);
            return e;
        };
        const double serial = microsPer(reps, [&] {
            for (const CourierSnapshot& c : ring8) {
                try {
                    heavy(c);
                } catch (const EstimateError&) {  // igual que estimateAll: la que falla se descarta
                }
            }
        });
        const double async = microsPer(reps, [&] { estimateAllAsync(ring8, heavy, timeout); });
        const double pooled = microsPer(reps, [&] { estimateAllPooled(ring8, heavy, pool, timeout); });
        std::cout << "| " << k << " | " << serial / 8 << " | " << serial << " | " << async << " | " << pooled << " | " << serial / async
                  << " | " << serial / pooled << " |\n";
    }
    std::cout << "\npool de " << pool.size() << " hilos (hardware: " << hardwareThreads() << "), " << reps << " repeticiones por celda, ciudad de "
              << cfg.nodes.size() << " nodos\n";
    return 0;
}
