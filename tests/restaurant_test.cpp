#include <atomic>
#include <chrono>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "restaurant.h"

using namespace std::chrono;
namespace {

int failures = 0;

void check(bool ok, const std::string& what) {
    if (!ok) {
        ++failures;
        std::cerr << "FALLO: " << what << "\n";
    }
}

RestaurantConfig cfg(int slots) { return {"r0", "Uno", "B", slots, 0, 0}; }

}  // namespace

int main() {
    {   // un puesto, tres motos que llegan en orden: pasan de a una y en ese orden
        Restaurant r(cfg(1));
        StopSignal stop;
        std::vector<std::string> order;
        std::mutex m;
        std::atomic<int> inside{0}, maxInside{0};
        std::vector<std::thread> threads;
        for (const std::string id : {"a", "b", "c"}) r.enterQueue(id);
        check(r.snapshot().waiting.size() == 3 && r.snapshot().waiting[0] == "a", "los tres en fila, a primero");
        for (const std::string id : {"a", "b", "c"}) {
            threads.emplace_back([&, id] {
                check(r.waitForSlot(id, stop), "cada moto consigue su puesto");
                const int now = ++inside;
                int seen = maxInside.load();
                while (now > seen && !maxInside.compare_exchange_weak(seen, now)) {}
                { std::lock_guard<std::mutex> lock(m); order.push_back(id); }
                std::this_thread::sleep_for(milliseconds(20));
                --inside;
                r.leaveSlot();
            });
        }
        for (auto& t : threads) t.join();
        check(order == std::vector<std::string>{"a", "b", "c"} && maxInside == 1 && r.served() == 3,
              "orden de llegada y nunca mas de un puesto ocupado");
        const QueueSnapshot s = r.snapshot();
        check(s.waiting.empty() && s.busy == 0 && s.slots == 1, "fila vacia y puestos libres al final");
    }
    {   // dos puestos: dos pasan a la vez, el tercero espera a que uno salga
        Restaurant r(cfg(2));
        StopSignal stop;
        r.enterQueue("a"), r.enterQueue("b"), r.enterQueue("c");
        check(r.waitForSlot("a", stop) && r.waitForSlot("b", stop) && r.snapshot().busy == 2, "a y b ocupan los dos puestos");
        std::atomic<bool> cIn{false};
        std::thread tc([&] { r.waitForSlot("c", stop); cIn = true; });
        std::this_thread::sleep_for(milliseconds(30));
        check(!cIn && r.snapshot().waiting == std::vector<std::string>{"c"}, "c espera mientras los puestos estan llenos");
        r.leaveSlot();
        tc.join();
        check(cIn && r.snapshot().busy == 2, "al salir uno, c pasa");
    }
    {   // la parada saca de la fila a quien espera
        Restaurant r(cfg(1));
        StopSignal stop;
        r.enterQueue("a"), r.enterQueue("b");
        r.waitForSlot("a", stop);
        bool got = true;
        std::thread tb([&] { got = r.waitForSlot("b", stop); });
        std::this_thread::sleep_for(milliseconds(20));
        stop.request();
        r.wake();
        tb.join();
        check(!got && r.snapshot().waiting.empty(), "b despierta con false y sale de la fila");
    }
    {   // espera prevista por scan exclusivo
        check(expectedStartMs({60, 120, 60}, 1) == std::vector<std::int64_t>{0, 60, 180}, "un puesto: suma prefija de los servicios");
        check(expectedStartMs({60, 120, 60}, 2) == std::vector<std::int64_t>{0, 30, 90}, "dos puestos: la mitad");
        check(expectedStartMs({}, 1).empty(), "fila vacia");
    }

    if (failures == 0) {
        std::cout << "restaurant_test: todas las pruebas pasaron\n";
        return 0;
    }
    std::cerr << "restaurant_test: " << failures << " prueba(s) fallaron\n";
    return 1;
}
