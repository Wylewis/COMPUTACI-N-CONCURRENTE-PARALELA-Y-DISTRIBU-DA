#include <atomic>
#include <chrono>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include "config.h"
#include "order.h"
#include "order_book.h"
#include "order_generator.h"
#include "scan.h"

using namespace std::chrono_literals;

namespace {

int failures = 0;

void check(bool ok, const std::string& what) {
    if (!ok) {
        ++failures;
        std::cerr << "FALLO: " << what << "\n";
    }
}

Config cityWith(int maxPending) {
    Config cfg;
    cfg.restaurants = {{"r0", "Uno", "A", 1, 0, 0}, {"r1", "Dos", "B", 1, 0, 0}, {"r2", "Tres", "C", 1, 0, 0}};
    cfg.orders.maxPending = maxPending;
    return cfg;
}

Order make(const std::string& id, const std::string& restaurant, bool unreachable = false) {
    Order o;
    o.id = id;
    o.restaurantId = restaurant;
    o.deliveryNode = "X";
    if (unreachable) o.reject(RejectReason::Unreachable);
    return o;
}

OrderBatch batchOf(std::vector<Order> orders, std::int64_t t = 0) {
    OrderBatch b;
    b.atMs = t;
    b.orders = std::move(orders);
    return b;
}

}

int main() {

    {
        const auto out = exclusiveScan({1, 0, 1, 1, 0, 1});
        check(out == std::vector<std::size_t>{0, 1, 1, 2, 3, 3}, "scan exclusivo de ceros y unos");
        check(exclusiveScan({}).empty(), "scan de vacio es vacio");
        check(exclusiveScan({5}) == std::vector<std::size_t>{0}, "scan de un elemento es 0");
    }

    {
        OrderBook book(cityWith(50));
        std::vector<Order> first;
        first.push_back(make("a", "r0"));
        first.push_back(make("b", "r1"));
        first.push_back(make("c", "r0"));
        first.push_back(make("d", "r0"));
        first.push_back(make("e", "r1"));
        const SubmitOutcome o1 = book.submit(batchOf(std::move(first)));
        check(o1.accepted.size() == 5 && o1.rejected.empty(), "los 5 entran");
        std::vector<std::size_t> pos;
        for (const auto& p : o1.accepted) pos.push_back(p.position);
        check(pos == std::vector<std::size_t>{0, 0, 1, 2, 1}, "posiciones [0,0,1,2,1] por restaurante");
        check(book.pending() == 5 && book.pendingAt("r0") == 3 && book.pendingAt("r1") == 2 && book.pendingAt("r2") == 0,
              "pendientes por restaurante");

        std::vector<Order> second;
        second.push_back(make("f", "r0"));
        second.push_back(make("g", "r2"));
        second.push_back(make("h", "r0"));
        const SubmitOutcome o2 = book.submit(batchOf(std::move(second)));
        pos.clear();
        for (const auto& p : o2.accepted) pos.push_back(p.position);
        check(pos == std::vector<std::size_t>{3, 0, 4}, "la posicion suma lo que ya habia en la cola");
    }

    {
        OrderBook book(cityWith(3));
        std::vector<Order> b;
        b.push_back(make("u", "r0", true));
        for (int i = 0; i < 5; ++i) b.push_back(make("o" + std::to_string(i), "r" + std::to_string(i % 3)));
        const SubmitOutcome out = book.submit(batchOf(std::move(b)));
        check(out.accepted.size() == 3, "entran exactamente maxPending");
        check(out.rejected.size() == 3, "los demas se rechazan");
        int unreachable = 0, full = 0;
        for (const auto& o : out.rejected) {
            if (o.rejectReason == RejectReason::Unreachable) ++unreachable;
            if (o.rejectReason == RejectReason::QueueFull) ++full;
        }
        check(unreachable == 1 && full == 2, "1 unreachable (venia asi) y 2 queueFull");
        check(book.pending() == 3, "pending es 3");

        auto taken = book.take("r0", 0);
        check(taken.has_value(), "take devuelve el pedido de r0");
        std::vector<Order> one;
        one.push_back(make("z", "r2"));
        check(book.submit(batchOf(std::move(one))).accepted.size() == 1, "tras un take hay lugar otra vez");
    }

    {
        OrderBook book(cityWith(50));
        std::vector<Order> b;
        b.push_back(make("1", "r0"));
        b.push_back(make("2", "r0"));
        b.push_back(make("3", "r0"));
        book.submit(batchOf(std::move(b)));
        auto first = book.take("r0", 0);
        check(first && first->id == "1", "take saca el mas antiguo");
        book.putBack(std::move(*first));
        auto again = book.take("r0", 0);
        check(again && again->id == "1", "putBack lo deja disponible y sigue siendo el primero");
        check(book.take("r0", 0)->id == "2" && book.take("r0", 0)->id == "3", "el resto sigue en orden");
    }

    {
        OrderBook book(cityWith(50));
        std::atomic<int> got{0}, empty{0};
        std::vector<std::thread> dispatchers;
        for (const std::string rid : {"r0", "r1", "r2"}) {
            dispatchers.emplace_back([&, rid] {
                auto o = book.take(rid, 0);
                if (o) got.fetch_add(1); else empty.fetch_add(1);
                auto o2 = book.take(rid, 0);
                if (o2) got.fetch_add(1); else empty.fetch_add(1);
            });
        }
        std::this_thread::sleep_for(50ms);
        check(got.load() == 0, "sin pedidos, los despachadores siguen dormidos");
        std::vector<Order> b;
        b.push_back(make("x", "r1"));
        b.push_back(make("y", "r2"));
        b.push_back(make("w", "r0"));
        book.submit(batchOf(std::move(b)));
        std::this_thread::sleep_for(50ms);
        check(got.load() == 3, "cada despachador recibio el pedido de su restaurante");
        book.close();
        for (auto& t : dispatchers) t.join();
        check(empty.load() == 3, "close despierta a los tres y devuelven nullopt");
        check(book.closed(), "el libro queda cerrado");
        std::vector<Order> late;
        late.push_back(make("tarde", "r0"));
        const SubmitOutcome out = book.submit(batchOf(std::move(late)));
        check(out.rejected.size() == 1 && out.rejected[0].rejectReason == RejectReason::QueueFull,
              "con el libro cerrado no entra nada");
    }

    {
        Config cfg = cityWith(50);
        cfg.orders.agingMs = 300'000;
        cfg.dispatch.acceptTimeoutMs = 600'000;
        OrderBook book(cfg);
        auto at = [](const std::string& id, std::int64_t created, bool urgent) {
            Order o = make(id, "r0");
            o.createdAtMs = created;
            o.urgent = urgent;
            return o;
        };
        std::vector<Order> b;
        b.push_back(at("o1", 0, false));
        b.push_back(at("o2", 10'000, true));
        b.push_back(at("o3", 20'000, false));
        b.push_back(at("o4", 5'000, true));
        book.submit(batchOf(std::move(b)));
        check(book.nextWakeMs("r0", 0) == 300'000, "el proximo plazo es el envejecimiento del normal mas viejo");
        auto first = book.take("r0", 30'000);
        check(first && first->id == "o4", "sale el urgente mas antiguo aunque entro despues");
        auto second = book.take("r0", 30'000);
        check(second && second->id == "o2", "despues el otro urgente");
        book.putBack(std::move(*first));
        book.putBack(std::move(*second));
        auto aged = book.take("r0", 300'000);
        check(aged && aged->id == "o1", "un normal envejecido pasa delante de los urgentes mas viejos");
        book.putBack(std::move(*aged));
        check(book.nextWakeMs("r0", 300'000) == 320'000, "ya envejecido o1, el proximo plazo es el envejecimiento de o3");
        auto expired = book.takeExpired("r0", 600'000);
        check(expired.size() == 1 && expired[0].id == "o1" && book.pending() == 3, "a los 600 s vence solo o1");
        Order back = at("o9", 0, false);
        back.courierId = "c0";
        book.putBack(std::move(back));
        check(book.takeExpired("r0", 900'000).size() == 3 && book.pending() == 1,
              "los que ya tuvieron moto no vencen nunca");
        check(!book.nextWakeMs("r0", 900'000), "sin plazos pendientes no hay instante de despertar");
    }

    {
        OrderBook book(cityWith(50));
        std::vector<Order> b;
        b.push_back(make("o9", "r0"));
        b.push_back(make("o10", "r0"));
        b.push_back(make("o2", "r0"));
        book.submit(batchOf(std::move(b)));
        check(book.take("r0", 0)->id == "o2" && book.take("r0", 0)->id == "o9" && book.take("r0", 0)->id == "o10",
              "a igual instante desempata la parte numerica del id");
    }

    {
        OrderBook book(cityWith(50));
        std::vector<Order> b;
        for (int i = 0; i < 7; ++i) b.push_back(make("o" + std::to_string(i), "r" + std::to_string(i % 3)));
        book.submit(batchOf(std::move(b)));
        const auto left = book.drain();
        check(left.size() == 7 && book.pending() == 0, "drain devuelve los 7 y deja el libro vacio");
    }

    {
        OrderBook book(cityWith(50));
        bool threw = false;
        try { book.take("r9", 0); } catch (const std::out_of_range&) { threw = true; }
        check(threw, "take de un restaurante desconocido lanza");
    }

    {
        OrderBook book(cityWith(1000));
        std::atomic<int> taken{0};
        std::vector<std::thread> consumers;
        for (const std::string rid : {"r0", "r1", "r2"}) {
            consumers.emplace_back([&, rid] {
                while (book.take(rid, 0)) taken.fetch_add(1);
            });
        }
        std::vector<std::thread> producers;
        std::atomic<int> accepted{0}, rejected{0};
        for (int p = 0; p < 4; ++p) {
            producers.emplace_back([&, p] {
                for (int k = 0; k < 300; ++k) {
                    std::vector<Order> b;
                    for (int j = 0; j < 3; ++j)
                        b.push_back(make("p" + std::to_string(p) + "-" + std::to_string(k) + "-" + std::to_string(j),
                                         "r" + std::to_string((p + j) % 3)));
                    const SubmitOutcome out = book.submit(batchOf(std::move(b)));
                    accepted.fetch_add(static_cast<int>(out.accepted.size()));
                    rejected.fetch_add(static_cast<int>(out.rejected.size()));
                }
            });
        }
        for (auto& t : producers) t.join();
        book.close();
        for (auto& t : consumers) t.join();
        check(accepted.load() + rejected.load() == 4 * 300 * 3, "todo pedido fue aceptado o rechazado");
        check(taken.load() == accepted.load(), "los despachadores sacaron exactamente los aceptados");
        check(book.pending() == 0, "al final no queda nada");
    }

    if (failures == 0) {
        std::cout << "order_book_test: todas las pruebas pasaron\n";
        return 0;
    }
    std::cerr << "order_book_test: " << failures << " prueba(s) fallaron\n";
    return 1;
}
