// Pruebas del pool de hilos: resultados por future, trabajos en hilos del
// pool, excepciones que cruzan, orden con un solo hilo, apagado que termina
// lo pendiente, rechazo despues de parar, y una comparacion de tiempo contra
// un hilo por trabajo.
#include <atomic>
#include <chrono>
#include <future>
#include <iostream>
#include <mutex>
#include <set>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "thread_pool.h"

namespace {

int failures = 0;

void check(bool ok, const std::string& what) {
    if (!ok) {
        ++failures;
        std::cerr << "FALLO: " << what << "\n";
    }
}

}  // namespace

int main() {
    // Resultados por future: 1000 trabajos, la suma de sus resultados es un reduce
    {
        ThreadPool pool(4);
        check(pool.size() == 4, "el pool tiene 4 hilos");
        std::vector<std::future<int>> results;
        for (int i = 0; i < 1000; ++i) results.push_back(pool.submit([i] { return i * 2; }));
        long long sum = 0;
        for (auto& f : results) sum += f.get();
        check(sum == 999LL * 1000, "la suma de los 1000 resultados es la esperada");
    }
    // Los trabajos corren en hilos del pool, no en el que los envia
    {
        ThreadPool pool(3);
        std::mutex m;
        std::set<std::thread::id> ids;
        std::vector<std::future<void>> fs;
        for (int i = 0; i < 300; ++i) {
            fs.push_back(pool.submit([&] {
                std::lock_guard<std::mutex> lock(m);
                ids.insert(std::this_thread::get_id());
            }));
        }
        for (auto& f : fs) f.get();
        check(ids.count(std::this_thread::get_id()) == 0, "ningun trabajo corrio en el hilo principal");
        check(!ids.empty() && ids.size() <= 3, "los trabajos usaron como mucho los 3 hilos del pool");
    }
    // Una excepcion dentro de un trabajo llega por el future, y el pool sigue vivo
    {
        ThreadPool pool(2);
        auto bad = pool.submit([]() -> int { throw std::runtime_error("estimacion fallida"); });
        bool threw = false;
        try {
            bad.get();
        } catch (const std::runtime_error& e) {
            threw = std::string(e.what()) == "estimacion fallida";
        }
        check(threw, "la excepcion del trabajo se relanza en get()");
        check(pool.submit([] { return 7; }).get() == 7, "el pool sigue aceptando trabajos despues de una excepcion");
    }
    // Con un solo hilo, los trabajos se ejecutan en orden de envio
    {
        ThreadPool pool(1);
        std::vector<int> order;
        std::vector<std::future<void>> fs;
        for (int i = 0; i < 50; ++i) fs.push_back(pool.submit([&order, i] { order.push_back(i); }));
        for (auto& f : fs) f.get();
        bool sorted = true;
        for (int i = 0; i < 50; ++i) sorted = sorted && order[i] == i;
        check(sorted, "con un hilo la cola es FIFO");
    }
    // Parar termina lo pendiente: nada se pierde
    {
        std::atomic<int> done{0};
        {
            ThreadPool pool(2);
            for (int i = 0; i < 40; ++i) {
                pool.submit([&done] {
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                    done.fetch_add(1);
                });
            }
            // el destructor llama a stop(): debe esperar los 40
        }
        check(done.load() == 40, "al destruir el pool se completan los trabajos encolados");
    }
    // Despues de stop() no se aceptan trabajos, y stop() es idempotente
    {
        ThreadPool pool(2);
        pool.stop();
        pool.stop();
        bool threw = false;
        try {
            pool.submit([] {});
        } catch (const std::runtime_error&) {
            threw = true;
        }
        check(threw, "submit despues de stop lanza");
        check(pool.pending() == 0, "sin pendientes tras parar");
    }
    // Un trabajo puede encolar otro sin bloquearse (el cerrojo no se retiene al ejecutar)
    {
        ThreadPool pool(1);
        auto outer = pool.submit([&pool] { return pool.submit([] { return 5; }); });
        check(outer.get().get() == 5, "un trabajo encola otro y ambos terminan, incluso con un solo hilo");
    }
    // Tiempo: muchos trabajos chicos en el pool contra un hilo nuevo por trabajo
    {
        const int jobs = 2000;
        auto t0 = std::chrono::steady_clock::now();
        {
            ThreadPool pool(std::max(2u, std::thread::hardware_concurrency()));
            std::vector<std::future<int>> fs;
            for (int i = 0; i < jobs; ++i) fs.push_back(pool.submit([i] { return i % 7; }));
            for (auto& f : fs) f.get();
        }
        const auto poolMs = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
        t0 = std::chrono::steady_clock::now();
        {
            std::vector<std::future<int>> fs;
            for (int i = 0; i < jobs; ++i) fs.push_back(std::async(std::launch::async, [i] { return i % 7; }));
            for (auto& f : fs) f.get();
        }
        const auto asyncMs = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
        std::cout << jobs << " trabajos chicos: pool " << poolMs << " ms, un hilo por trabajo " << asyncMs << " ms\n";
    }

    if (failures == 0) {
        std::cout << "thread_pool_test: todas las pruebas pasaron\n";
        return 0;
    }
    std::cerr << "thread_pool_test: " << failures << " prueba(s) fallaron\n";
    return 1;
}
