#include <atomic>
#include <chrono>
#include <csignal>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include "stop_signal.h"

using namespace std::chrono_literals;

namespace {

int failures = 0;

void check(bool ok, const std::string& what) {
    if (!ok) {
        ++failures;
        std::cerr << "FALLO: " << what << "\n";
    }
}

template <class F>
long long millisOf(F&& f) {
    const auto t0 = std::chrono::steady_clock::now();
    f();
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
}

}

int main() {

    {
        StopSignal stop;
        check(!stop.requested(), "al inicio no se pidio parar");
        stop.request();
        stop.request();
        check(stop.requested(), "despues de request, requested es true");
        check(millisOf([&] { stop.wait(); }) < 50, "wait vuelve de inmediato si ya se pidio parar");
        check(stop.waitFor(500ms), "waitFor devuelve true de inmediato si ya se pidio parar");
    }

    {
        StopSignal stop;
        bool stopped = true;
        const long long ms = millisOf([&] { stopped = stop.waitFor(150ms); });
        check(!stopped, "sin orden de parar, waitFor devuelve false");
        check(ms >= 140 && ms < 1000, "y espera aproximadamente el tiempo pedido");
    }

    {
        StopSignal stop;
        std::thread t([&] {
            std::this_thread::sleep_for(50ms);
            stop.request();
        });
        bool stopped = false;
        const long long ms = millisOf([&] { stopped = stop.waitFor(5s); });
        t.join();
        check(stopped, "la orden interrumpe la espera");
        check(ms < 1000, "y lo hace enseguida, no al vencer el plazo");
    }

    {
        StopSignal stop;
        std::atomic<int> woke{0};
        std::vector<std::thread> sleepers;
        for (int i = 0; i < 10; ++i) {
            sleepers.emplace_back([&] {
                stop.wait();
                woke.fetch_add(1);
            });
        }
        std::this_thread::sleep_for(50ms);
        check(woke.load() == 0, "antes de la orden, nadie despierta");
        stop.request();
        for (auto& t : sleepers) t.join();
        check(woke.load() == 10, "los 10 hilos despertaron con una sola orden");
    }

    {
        StopSignal stop;
        {
            SignalWatcher watcher(stop);
            std::raise(SIGINT);
            check(stop.waitFor(2s), "SIGINT produce la orden de parar");
            check(watcher.signalsReceived() == 1, "el vigia conto una senal");
        }
        check(stop.requested(), "la orden sigue en pie tras destruir el vigia");
    }

    {
        StopSignal stop;
        {
            SignalWatcher watcher(stop);
            std::raise(SIGTERM);
            check(stop.waitFor(2s), "SIGTERM produce la orden de parar");
        }
    }

    {
        StopSignal stop;
        const long long ms = millisOf([&] {
            SignalWatcher watcher(stop);
            std::this_thread::sleep_for(20ms);
        });
        check(!stop.requested(), "sin senal no hay orden de parar");
        check(ms < 1000, "el vigia se destruye sin esperar a una senal");
    }

    {
        StopSignal stop;
        bool created = false;
        try {
            SignalWatcher again(stop);
            created = true;
        } catch (...) {
        }
        check(created, "se puede crear un vigia nuevo despues de destruir el anterior");
    }

    if (failures == 0) {
        std::cout << "stop_signal_test: todas las pruebas pasaron\n";
        return 0;
    }
    std::cerr << "stop_signal_test: " << failures << " prueba(s) fallaron\n";
    return 1;
}
