#pragma once

#include <condition_variable>
#include <cstddef>
#include <deque>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

// Un grupo fijo de hilos que ejecutan trabajos tomados de una cola.
//
// Los hilos se crean una sola vez y se reutilizan: un trabajo que no tiene
// hilo libre espera en la cola. Cada trabajo enviado devuelve un future con
// su resultado o con la excepcion que lanzo. La cola es un monitor: el mutex
// protege solo la cola y la bandera de parada, y la variable de condicion
// duerme a los hilos sin consumir CPU hasta que haya trabajo o se pare.
// Los trabajos se ejecutan fuera del cerrojo.
//
// stop() cierra la cola: lo que ya estaba encolado se termina, lo nuevo se
// rechaza, y los hilos se despiertan y se unen. El destructor llama a stop().
class ThreadPool {
public:
    explicit ThreadPool(unsigned workers);
    ~ThreadPool();

    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    unsigned size() const { return static_cast<unsigned>(workers_.size()); }

    // Trabajos encolados que ningun hilo tomo todavia.
    std::size_t pending() const;

    // Encola `job` y devuelve el future de su resultado. Lanza std::runtime_error
    // si el pool ya fue parado.
    template <class F>
    auto submit(F&& job) -> std::future<std::invoke_result_t<F>> {
        using R = std::invoke_result_t<F>;
        // packaged_task es solo movible y std::function exige copiable: se
        // comparte con un shared_ptr, que la cola puede copiar.
        auto task = std::make_shared<std::packaged_task<R()>>(std::forward<F>(job));
        std::future<R> result = task->get_future();
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (stopping_) {
                throw std::runtime_error("el pool de hilos ya fue parado");
            }
            jobs_.emplace_back([task] { (*task)(); });
        }
        available_.notify_one();  // un solo trabajo nuevo: basta despertar a un hilo
        return result;
    }

    // Cierra la cola y espera a que los hilos terminen lo pendiente. Idempotente.
    void stop();

private:
    void workerLoop();

    std::vector<std::thread> workers_;
    std::deque<std::function<void()>> jobs_;
    mutable std::mutex mutex_;
    std::condition_variable available_;
    bool stopping_ = false;
};
