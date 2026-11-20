#include "bench.h"

#include <cstddef>
#include <functional>
#include <iostream>
#include <string>
#include <vector>

#include <memory>
#include <random>
#include <thread>

#include "gpu/argmin_rows.h"
#include "gpu/backlog_curve.h"
#include "gpu/coverage_field.h"
#include "gpu/delivery_histogram.h"
#include "gpu/delivery_kpis.h"
#include "gpu/demand_heatmap.h"
#include "gpu/demand_trend.h"
#include "gpu/eta_batch.h"
#include "gpu/gpu_backend.h"
#include "gpu/group_orders.h"
#include "gpu/restaurant_slots.h"
#include "gpu/travel_table.h"
#include "gpu/travel_table_graph.h"
#include "parallel_for.h"

namespace {

void benchVectorAdd(BenchRunner& runner) {
    for (const std::size_t n : {1'000u, 10'000u, 100'000u, 1'000'000u, 10'000'000u}) {
        if (!runner.wants(n)) continue;
        std::vector<int> a(n), b(n), expected(n), out(n);
        for (std::size_t i = 0; i < n; ++i) {
            a[i] = static_cast<int>(i % 1000);
            b[i] = static_cast<int>(2 * (i % 777) + 1);
            expected[i] = a[i] + b[i];
        }
        BenchCase c;
        c.app = "vector_add";
        c.concept = "00_reference";
        c.size = std::to_string(n);
        c.items = n;
        c.cpu1 = [&] {
            Timed r;
            r.ms = timeMs([&] {
                for (std::size_t i = 0; i < n; ++i) out[i] = a[i] + b[i];
            });
            r.correct = out == expected;
            return r;
        };
        c.cpuN = [&](ThreadPool& pool) {
            Timed r;
            r.ms = timeMs([&] {
                parallelFor(pool, n, [&](std::size_t begin, std::size_t end, std::size_t) {
                    for (std::size_t i = begin; i < end; ++i) out[i] = a[i] + b[i];
                });
            });
            r.correct = out == expected;
            return r;
        };
        c.gpu = [&] {
            GpuOutcome r;
            r.times = gpuVectorAdd(a.data(), b.data(), out.data(), static_cast<int>(n));
            r.correct = out == expected;
            return r;
        };
        runner.run(c);
    }
}

void benchTravelTable(BenchRunner& runner) {
    const CsrGraph city = csrFromGraph(runner.graph(), runner.config().fleet.speedKmh);
    const int start = static_cast<int>(runner.graph().nodeIndex(runner.config().fleet.startNode));
    for (const int wanted : {128, 256, 512, 1024, 2048, 4096}) {
        if (wanted > city.n) break;
        const CsrGraph g = prefixSubgraph(city, start, wanted);
        const std::size_t items = static_cast<std::size_t>(g.n) * static_cast<std::size_t>(g.n);
        if (!runner.wants(items)) continue;
        const std::vector<int> expected = travelTableCpu(g);
        BenchCase c;
        c.app = "travel_table";
        c.concept = "03_memory_tiling";
        c.size = std::to_string(g.n) + "x" + std::to_string(g.n);
        c.items = items;
        c.cpu1 = [&] {
            Timed r;
            std::vector<int> out;
            r.ms = timeMs([&] { out = travelTableCpu(g); });
            r.correct = out == expected;
            return r;
        };
        c.cpuN = [&](ThreadPool& pool) {
            Timed r;
            std::vector<int> out;
            r.ms = timeMs([&] { out = travelTableCpuParallel(g, pool); });
            r.correct = out == expected;
            return r;
        };
        c.gpu = [&] {
            GpuOutcome r;
            const std::vector<int> out = travelTableGpu(g, &r.times);
            r.correct = out == expected;
            return r;
        };
        runner.run(c);
    }
}

EtaInput randomEtaInput(int nodes, std::size_t orders, std::size_t couriers, unsigned seed) {
    std::mt19937 rng(seed);
    std::uniform_int_distribution<int> node(0, nodes - 1), time(0, 600'000), percent(0, 99);
    EtaInput in;
    in.nowMs = 300'000;
    for (std::size_t c = 0; c < couriers; ++c) {
        in.courierNode.push_back(node(rng));
        in.courierAvailableMs.push_back(time(rng));
        in.courierFree.push_back(percent(rng) < 85 ? 1 : 0);
    }
    for (std::size_t o = 0; o < orders; ++o) {
        in.orderRestaurantNode.push_back(node(rng));
        in.orderDeliveryNode.push_back(node(rng));
        in.orderReadyMs.push_back(time(rng));
    }
    return in;
}

void benchEtaBatch(BenchRunner& runner) {
    const CsrGraph city = csrFromGraph(runner.graph(), runner.config().fleet.speedKmh);
    ThreadPool builders(std::max(1u, std::thread::hardware_concurrency()));
    const TravelTable table(city.n, travelTableCpuParallel(city, builders));
    std::shared_ptr<EtaBatchGpu> device;
    const std::vector<std::pair<std::size_t, std::size_t>> sizes{{10, 50}, {50, 200}, {100, 500}, {200, 1000}, {500, 2000}, {1000, 3000}};
    for (const auto& size : sizes) {
        const std::size_t orders = size.first, couriers = size.second;
        const std::size_t items = orders * couriers;
        if (!runner.wants(items)) continue;
        const EtaInput in = randomEtaInput(city.n, orders, couriers, static_cast<unsigned>(items));
        std::vector<int> expected(items), out(items);
        etaBatchCpu(table, in, expected.data());
        BenchCase c;
        c.app = "eta_batch";
        c.concept = "04_performance";
        c.size = std::to_string(orders) + "x" + std::to_string(couriers);
        c.items = items;
        c.cpu1 = [&] {
            Timed r;
            r.ms = timeMs([&] { etaBatchCpu(table, in, out.data()); });
            r.correct = out == expected;
            return r;
        };
        c.cpuN = [&](ThreadPool& pool) {
            Timed r;
            r.ms = timeMs([&] { etaBatchCpuParallel(table, in, out.data(), pool); });
            r.correct = out == expected;
            return r;
        };
        c.gpu = [&] {
            if (!device) device = std::make_shared<EtaBatchGpu>(table);
            GpuOutcome r;
            r.correct = device->run(in, EtaLayout::StructureOfArrays, &r.times) == expected;
            return r;
        };
        c.gpuVariants.push_back({"aos", [&] {
            if (!device) device = std::make_shared<EtaBatchGpu>(table);
            GpuOutcome r;
            r.correct = device->run(in, EtaLayout::ArrayOfStructures, &r.times) == expected;
            return r;
        }});
        c.gpuVariants.push_back({"cold", [&] {
            EtaBatchGpu cold(table);
            GpuOutcome r;
            r.correct = cold.run(in, EtaLayout::StructureOfArrays, &r.times) == expected;
            r.times.totalMs += cold.tableUploadMs();
            return r;
        }});
        runner.run(c);
    }
}

void benchDispatchArgmin(BenchRunner& runner) {
    const CsrGraph city = csrFromGraph(runner.graph(), runner.config().fleet.speedKmh);
    ThreadPool builders(std::max(1u, std::thread::hardware_concurrency()));
    const TravelTable table(city.n, travelTableCpuParallel(city, builders));
    const std::vector<std::pair<std::size_t, std::size_t>> sizes{{10, 50}, {50, 200}, {100, 500}, {200, 1000}, {500, 2000}, {1000, 3000}, {4000, 3000}};
    for (const auto& size : sizes) {
        const std::size_t orders = size.first, couriers = size.second;
        const std::size_t items = orders * couriers;
        if (!runner.wants(items)) continue;
        const EtaInput in = randomEtaInput(city.n, orders, couriers, static_cast<unsigned>(items));
        std::vector<int> matrix(items);
        etaBatchCpu(table, in, matrix.data());
        const std::vector<EtaKey> expected = argminRowsCpu(matrix.data(), orders, couriers);
        BenchCase c;
        c.app = "dispatch_argmin";
        c.concept = "08_reduction";
        c.size = std::to_string(orders) + "x" + std::to_string(couriers);
        c.items = items;
        c.cpu1 = [&] {
            Timed r;
            std::vector<EtaKey> out;
            r.ms = timeMs([&] { out = argminRowsCpu(matrix.data(), orders, couriers); });
            r.correct = out == expected;
            return r;
        };
        c.cpuN = [&](ThreadPool& pool) {
            Timed r;
            std::vector<EtaKey> out;
            r.ms = timeMs([&] { out = argminRowsCpuParallel(matrix.data(), orders, couriers, pool); });
            r.correct = out == expected;
            return r;
        };
        c.gpu = [&] {
            GpuOutcome r;
            r.correct = argminRowsGpu(matrix.data(), orders, couriers, nullptr, &r.times) == expected;
            return r;
        };
        runner.run(c);
    }
}

void benchDispatchRound(BenchRunner& runner) {
    const CsrGraph city = csrFromGraph(runner.graph(), runner.config().fleet.speedKmh);
    ThreadPool builders(std::max(1u, std::thread::hardware_concurrency()));
    const TravelTable table(city.n, travelTableCpuParallel(city, builders));
    std::shared_ptr<EtaBatchGpu> device;
    const std::vector<std::pair<std::size_t, std::size_t>> sizes{{10, 50}, {50, 200}, {100, 500}, {200, 1000}, {500, 2000}, {1000, 3000}, {4000, 3000}};
    for (const auto& size : sizes) {
        const std::size_t orders = size.first, couriers = size.second;
        const std::size_t items = orders * couriers;
        if (!runner.wants(items)) continue;
        const EtaInput in = randomEtaInput(city.n, orders, couriers, static_cast<unsigned>(items));
        std::vector<int> matrix(items);
        etaBatchCpu(table, in, matrix.data());
        const std::vector<EtaKey> expected = argminRowsCpu(matrix.data(), orders, couriers);
        BenchCase c;
        c.app = "dispatch_round";
        c.concept = "08_reduction";
        c.size = std::to_string(orders) + "x" + std::to_string(couriers);
        c.items = items;
        c.cpu1 = [&] {
            Timed r;
            std::vector<EtaKey> out;
            r.ms = timeMs([&] {
                etaBatchCpu(table, in, matrix.data());
                out = argminRowsCpu(matrix.data(), orders, couriers);
            });
            r.correct = out == expected;
            return r;
        };
        c.cpuN = [&](ThreadPool& pool) {
            Timed r;
            std::vector<EtaKey> out;
            r.ms = timeMs([&] {
                etaBatchCpuParallel(table, in, matrix.data(), pool);
                out = argminRowsCpuParallel(matrix.data(), orders, couriers, pool);
            });
            r.correct = out == expected;
            return r;
        };
        c.gpu = [&] {
            if (!device) device = std::make_shared<EtaBatchGpu>(table);
            GpuOutcome r;
            r.correct = device->runBest(in, {}, &r.times) == expected;
            return r;
        };
        c.gpuVariants.push_back({"sustained", [&] {
            if (!device) device = std::make_shared<EtaBatchGpu>(table);
            GpuOutcome r;
            for (int round = 0; round < 16; ++round) r.correct = device->runBest(in, {}, &r.times) == expected;
            return r;
        }});
        c.gpuVariants.push_back({"matrix", [&] {
            if (!device) device = std::make_shared<EtaBatchGpu>(table);
            GpuOutcome r;
            GpuTimes etaTimes;
            const std::vector<int> down = device->run(in, EtaLayout::StructureOfArrays, &etaTimes);
            std::vector<EtaKey> out;
            const double reduceMs = timeMs([&] { out = argminRowsCpu(down.data(), orders, couriers); });
            r.times.kernelMs = etaTimes.kernelMs;
            r.times.totalMs = etaTimes.totalMs + reduceMs;
            r.correct = out == expected;
            return r;
        }});
        runner.run(c);
    }
}

void benchGroupOrders(BenchRunner& runner) {
    const std::vector<std::pair<std::size_t, int>> sizes{{400, 20}, {4'000, 200}, {40'000, 2'000}, {400'000, 20'000},
                                                         {4'000'000, 200'000}, {16'000'000, 800'000}};
    for (const auto& size : sizes) {
        const std::size_t orders = size.first;
        const int groups = size.second;
        if (!runner.wants(orders)) continue;
        std::mt19937 rng(static_cast<unsigned>(orders));
        std::uniform_int_distribution<int> any(0, groups - 1);
        std::vector<int> group(orders);
        for (int& g : group) g = any(rng);
        const OrderGroups expected = groupOrdersCpu(group.data(), orders, groups);
        std::vector<int> counts(static_cast<std::size_t>(groups), 0);
        for (const int g : group) ++counts[static_cast<std::size_t>(g)];
        BenchCase c;
        c.app = "group_orders";
        c.concept = "09_scan";
        c.size = std::to_string(orders) + "x" + std::to_string(groups);
        c.items = orders;
        c.cpu1 = [&] {
            Timed r;
            OrderGroups out;
            r.ms = timeMs([&] { out = groupOrdersCpu(group.data(), orders, groups); });
            r.correct = out == expected;
            return r;
        };
        c.cpuN = [&](ThreadPool& pool) {
            Timed r;
            OrderGroups out;
            r.ms = timeMs([&] { out = groupOrdersCpuParallel(group.data(), orders, groups, pool); });
            r.correct = out == expected;
            return r;
        };
        c.gpu = [&] {
            GpuOutcome r;
            OrderGroups out = groupOrdersGpu(group.data(), orders, groups, &r.times);
            sortWithinGroups(out);
            r.correct = out == expected;
            return r;
        };
        c.gpuVariants.push_back({"scan", [&] {
            GpuOutcome r;
            r.correct = exclusiveScanGpu(counts, &r.times) == expected.start;
            return r;
        }});
        runner.run(c);
    }
}

EtaInput restaurantEtaInput(int nodes, std::size_t orders, std::size_t couriers, std::size_t restaurants, unsigned seed) {
    EtaInput in = randomEtaInput(nodes, orders, couriers, seed);
    std::mt19937 rng(seed ^ 0x9e3779b9u);
    std::uniform_int_distribution<int> node(0, nodes - 1);
    std::vector<int> kitchens(restaurants);
    for (int& k : kitchens) k = node(rng);
    std::uniform_int_distribution<std::size_t> pick(0, restaurants - 1);
    for (int& r : in.orderRestaurantNode) r = kitchens[pick(rng)];
    return in;
}

void benchRoundByRestaurant(BenchRunner& runner, bool grouped) {
    const CsrGraph city = csrFromGraph(runner.graph(), runner.config().fleet.speedKmh);
    ThreadPool builders(std::max(1u, std::thread::hardware_concurrency()));
    const TravelTable table(city.n, travelTableCpuParallel(city, builders));
    std::shared_ptr<EtaBatchGpu> device;
    const std::size_t couriers = 3000, restaurants = 200;
    for (const std::size_t orders : {50u, 150u, 500u, 1000u, 2000u, 4000u}) {
        const std::size_t items = orders * couriers;
        if (!runner.wants(items)) continue;
        const EtaInput in = restaurantEtaInput(city.n, orders, couriers, restaurants, static_cast<unsigned>(items));
        std::vector<int> matrix(items);
        etaBatchCpu(table, in, matrix.data());
        const std::vector<EtaKey> expected = argminRowsCpu(matrix.data(), orders, couriers);
        const auto round = [&](const std::function<OrderGroups()>& group,
                               const std::function<std::vector<EtaKey>(const EtaInput&)>& best) {
            if (!grouped) return best(in);
            const OrderGroups groups = group();
            const std::vector<EtaKey> arranged = best(ordersPermuted(in, groups.order));
            std::vector<EtaKey> out(orders);
            for (std::size_t k = 0; k < orders; ++k) out[static_cast<std::size_t>(groups.order[k])] = arranged[k];
            return out;
        };
        const auto groupOnCpu = [&] { return groupOrdersCpu(in.orderRestaurantNode.data(), orders, city.n); };
        BenchCase c;
        c.app = grouped ? "round_grouped" : "round_arrival";
        c.concept = "09_scan";
        c.size = std::to_string(orders) + "x" + std::to_string(couriers);
        c.items = items;
        c.cpu1 = [&] {
            Timed r;
            std::vector<EtaKey> out;
            r.ms = timeMs([&] {
                out = round(groupOnCpu, [&](const EtaInput& arranged) {
                    etaBatchCpu(table, arranged, matrix.data());
                    return argminRowsCpu(matrix.data(), orders, couriers);
                });
            });
            r.correct = out == expected;
            return r;
        };
        c.cpuN = [&](ThreadPool& pool) {
            Timed r;
            std::vector<EtaKey> out;
            r.ms = timeMs([&] {
                out = round([&] { return groupOrdersCpuParallel(in.orderRestaurantNode.data(), orders, city.n, pool); },
                            [&](const EtaInput& arranged) {
                                etaBatchCpuParallel(table, arranged, matrix.data(), pool);
                                return argminRowsCpuParallel(matrix.data(), orders, couriers, pool);
                            });
            });
            r.correct = out == expected;
            return r;
        };
        const auto onDevice = [&](const std::function<OrderGroups()>& group) {
            if (!device) device = std::make_shared<EtaBatchGpu>(table);
            GpuOutcome r;
            GpuTimes times;
            std::vector<EtaKey> out;
            r.times.totalMs = timeMs([&] {
                out = round(group, [&](const EtaInput& arranged) { return device->runBest(arranged, {}, &times); });
            });
            r.times.kernelMs = times.kernelMs;
            r.correct = out == expected;
            return r;
        };
        c.gpu = [&] { return onDevice(groupOnCpu); };
        if (grouped)
            c.gpuVariants.push_back({"devicegroup", [&] {
                return onDevice([&] { return groupOrdersGpu(in.orderRestaurantNode.data(), orders, city.n); });
            }});
        runner.run(c);
    }
}

void benchRoundArrival(BenchRunner& runner) { benchRoundByRestaurant(runner, false); }
void benchRoundGrouped(BenchRunner& runner) { benchRoundByRestaurant(runner, true); }

void benchDeliveryHistogram(BenchRunner& runner) {
    const int bins = 120;
    for (const std::size_t n : {1'000u, 10'000u, 100'000u, 1'000'000u, 10'000'000u, 40'000'000u}) {
        if (!runner.wants(n)) continue;
        std::mt19937 rng(static_cast<unsigned>(n));
        std::vector<int> ms(n);
        for (int& v : ms) v = 600'000 + static_cast<int>(rng() % 600'000u + rng() % 600'000u + rng() % 600'000u);
        const std::vector<unsigned> expected = deliveryHistogramCpu(ms.data(), n, bins);
        BenchCase c;
        c.app = "delivery_histogram";
        c.concept = "07_histogram";
        c.size = std::to_string(n);
        c.items = n;
        c.cpu1 = [&] {
            Timed r;
            std::vector<unsigned> out;
            r.ms = timeMs([&] { out = deliveryHistogramCpu(ms.data(), n, bins); });
            r.correct = out == expected;
            return r;
        };
        c.cpuN = [&](ThreadPool& pool) {
            Timed r;
            std::vector<unsigned> out;
            r.ms = timeMs([&] { out = deliveryHistogramCpuParallel(ms.data(), n, bins, pool); });
            r.correct = out == expected;
            return r;
        };
        c.gpu = [&] {
            GpuOutcome r;
            r.correct = deliveryHistogramGpu(ms.data(), n, bins, HistogramMode::Privatized, &r.times) == expected;
            return r;
        };
        c.gpuVariants.push_back({"atomic", [&] {
            GpuOutcome r;
            r.correct = deliveryHistogramGpu(ms.data(), n, bins, HistogramMode::Atomic, &r.times) == expected;
            return r;
        }});
        runner.run(c);
    }
}

void benchDeliveryKpis(BenchRunner& runner) {
    for (const std::size_t n : {1'000u, 10'000u, 100'000u, 1'000'000u, 10'000'000u, 40'000'000u}) {
        if (!runner.wants(n)) continue;
        std::mt19937 rng(static_cast<unsigned>(n) + 8u);
        std::vector<int> ms(n);
        for (int& v : ms) v = 600'000 + static_cast<int>(rng() % 900'000u + rng() % 900'000u + rng() % 900'000u);
        const DeliveryKpis expected = deliveryKpisCpu(ms.data(), n);
        BenchCase c;
        c.app = "delivery_kpis";
        c.concept = "08_reduction";
        c.size = std::to_string(n);
        c.items = n;
        c.cpu1 = [&] {
            Timed r;
            DeliveryKpis out = noDeliveries();
            r.ms = timeMs([&] { out = deliveryKpisCpu(ms.data(), n); });
            r.correct = out == expected;
            return r;
        };
        c.cpuN = [&](ThreadPool& pool) {
            Timed r;
            DeliveryKpis out = noDeliveries();
            r.ms = timeMs([&] { out = deliveryKpisCpuParallel(ms.data(), n, pool); });
            r.correct = out == expected;
            return r;
        };
        c.gpu = [&] {
            GpuOutcome r;
            r.correct = deliveryKpisGpu(ms.data(), n, kLateDeliveryMs, kKpiPerThread, &r.times) == expected;
            return r;
        };
        c.gpuVariants.push_back({"fine", [&] {
            GpuOutcome r;
            r.correct = deliveryKpisGpu(ms.data(), n, kLateDeliveryMs, 1, &r.times) == expected;
            return r;
        }});
        runner.run(c);
    }
}

void benchBacklogCurve(BenchRunner& runner) {
    for (const std::size_t n : {1'000u, 10'000u, 100'000u, 1'000'000u, 10'000'000u, 40'000'000u}) {
        if (!runner.wants(n)) continue;
        std::mt19937 rng(static_cast<unsigned>(n) + 9u);
        std::vector<int> delta(n);
        int waiting = 0;
        for (int& d : delta) {
            const int in = static_cast<int>(rng() % 4u);
            const int out = waiting + in > 0 ? static_cast<int>(rng() % static_cast<unsigned>(waiting + in + 1)) % 4 : 0;
            d = in - out;
            waiting += d;
        }
        const std::vector<int> expected = backlogCurveCpu(delta.data(), n);
        BenchCase c;
        c.app = "backlog_curve";
        c.concept = "09_scan";
        c.size = std::to_string(n);
        c.items = n;
        c.cpu1 = [&] {
            Timed r;
            std::vector<int> out;
            r.ms = timeMs([&] { out = backlogCurveCpu(delta.data(), n); });
            r.correct = out == expected;
            return r;
        };
        c.cpuN = [&](ThreadPool& pool) {
            Timed r;
            std::vector<int> out;
            r.ms = timeMs([&] { out = backlogCurveCpuParallel(delta.data(), n, pool); });
            r.correct = out == expected;
            return r;
        };
        c.gpu = [&] {
            GpuOutcome r;
            r.correct = backlogCurveGpu(delta.data(), n, ScanKernel::KoggeStone, &r.times) == expected;
            return r;
        };
        c.gpuVariants.push_back({"brentkung", [&] {
            GpuOutcome r;
            r.correct = backlogCurveGpu(delta.data(), n, ScanKernel::BrentKung, &r.times) == expected;
            return r;
        }});
        runner.run(c);
    }
}

void benchRestaurantSlots(BenchRunner& runner) {
    const int restaurants = 200, slots = 144;
    for (const std::size_t n : {1'000u, 10'000u, 100'000u, 1'000'000u, 10'000'000u, 40'000'000u}) {
        if (!runner.wants(n)) continue;
        std::mt19937 rng(static_cast<unsigned>(n) + 7u);
        std::vector<int> restaurant(n), ms(n);
        const std::size_t dayMs = static_cast<std::size_t>(slots) * static_cast<std::size_t>(kDemandSlotMs);
        for (std::size_t i = 0; i < n; ++i) {
            restaurant[i] = static_cast<int>(rng() % static_cast<unsigned>(restaurants));
            ms[i] = static_cast<int>(i * dayMs / n);
        }
        std::vector<std::size_t> next(static_cast<std::size_t>(restaurants) + 1, 0);
        for (const int r : restaurant) ++next[static_cast<std::size_t>(r) + 1];
        for (std::size_t r = 1; r < next.size(); ++r) next[r] += next[r - 1];
        std::vector<int> groupedRestaurant(n), groupedMs(n);
        for (std::size_t i = 0; i < n; ++i) {
            const std::size_t at = next[static_cast<std::size_t>(restaurant[i])]++;
            groupedRestaurant[at] = restaurant[i];
            groupedMs[at] = ms[i];
        }
        const std::vector<unsigned> expected = restaurantSlotsCpu(restaurant.data(), ms.data(), n, restaurants, slots);
        BenchCase c;
        c.app = "restaurant_slots";
        c.concept = "07_histogram";
        c.size = std::to_string(n);
        c.items = n;
        c.cpu1 = [&] {
            Timed r;
            std::vector<unsigned> out;
            r.ms = timeMs([&] { out = restaurantSlotsCpu(restaurant.data(), ms.data(), n, restaurants, slots); });
            r.correct = out == expected;
            return r;
        };
        c.cpuN = [&](ThreadPool& pool) {
            Timed r;
            std::vector<unsigned> out;
            r.ms = timeMs([&] { out = restaurantSlotsCpuParallel(restaurant.data(), ms.data(), n, restaurants, slots, pool); });
            r.correct = out == expected;
            return r;
        };
        const auto onDevice = [&](const std::vector<int>& who, const std::vector<int>& when, SlotKernel kernel) {
            GpuOutcome r;
            r.correct = restaurantSlotsGpu(who.data(), when.data(), n, restaurants, slots, kernel, &r.times) == expected;
            return r;
        };
        c.gpu = [&] { return onDevice(restaurant, ms, SlotKernel::Atomic); };
        c.gpuVariants.push_back({"aggregated", [&] { return onDevice(restaurant, ms, SlotKernel::Aggregated); }});
        c.gpuVariants.push_back({"grouped", [&] { return onDevice(groupedRestaurant, groupedMs, SlotKernel::Atomic); }});
        c.gpuVariants.push_back(
            {"grouped_aggregated", [&] { return onDevice(groupedRestaurant, groupedMs, SlotKernel::Aggregated); }});
        runner.run(c);
    }
}

void benchDemandHeatmap(BenchRunner& runner) {
    const std::vector<float> weights = gaussianWeights();
    for (const int grid : {64, 128, 256, 512, 1024, 2048, 4096}) {
        const std::size_t cells = static_cast<std::size_t>(grid) * static_cast<std::size_t>(grid);
        if (!runner.wants(cells)) continue;
        std::mt19937 rng(static_cast<unsigned>(grid));
        std::vector<float> counts(cells);
        for (float& c : counts) c = rng() % 16u == 0 ? static_cast<float>(1u + rng() % 5u) : 0.0f;
        const std::vector<float> expected = smoothCpu(counts.data(), grid, weights.data());
        BenchCase c;
        c.app = "demand_heatmap";
        c.concept = "05_conv";
        c.size = std::to_string(grid) + "x" + std::to_string(grid);
        c.items = cells;
        c.cpu1 = [&] {
            Timed r;
            std::vector<float> out;
            r.ms = timeMs([&] { out = smoothCpu(counts.data(), grid, weights.data()); });
            r.correct = out == expected;
            return r;
        };
        c.cpuN = [&](ThreadPool& pool) {
            Timed r;
            std::vector<float> out;
            r.ms = timeMs([&] { out = smoothCpuParallel(counts.data(), grid, weights.data(), pool); });
            r.correct = out == expected;
            return r;
        };
        c.gpu = [&] {
            GpuOutcome r;
            r.correct = heatmapsMatch(expected, smoothGpu(counts.data(), grid, weights.data(), HeatWeights::Constant, &r.times));
            return r;
        };
        c.gpuVariants.push_back({"globalweights", [&] {
            GpuOutcome r;
            r.correct = heatmapsMatch(expected, smoothGpu(counts.data(), grid, weights.data(), HeatWeights::Global, &r.times));
            return r;
        }});
        runner.run(c);
    }
}

void benchDemandTrend(BenchRunner& runner) {
    const std::vector<float> weights = trendWeights();
    for (const std::size_t n : {1'000u, 10'000u, 100'000u, 1'000'000u, 10'000'000u, 40'000'000u}) {
        if (!runner.wants(n)) continue;
        std::mt19937 rng(static_cast<unsigned>(n) + 5u);
        std::vector<float> counts(n);
        for (float& c : counts) c = static_cast<float>(rng() % 24u);
        const std::vector<float> expected = trendCpu(counts.data(), n, weights.data());
        BenchCase c;
        c.app = "demand_trend";
        c.concept = "05_conv";
        c.size = std::to_string(n);
        c.items = n;
        c.cpu1 = [&] {
            Timed r;
            std::vector<float> out;
            r.ms = timeMs([&] { out = trendCpu(counts.data(), n, weights.data()); });
            r.correct = out == expected;
            return r;
        };
        c.cpuN = [&](ThreadPool& pool) {
            Timed r;
            std::vector<float> out;
            r.ms = timeMs([&] { out = trendCpuParallel(counts.data(), n, weights.data(), pool); });
            r.correct = out == expected;
            return r;
        };
        c.gpu = [&] {
            GpuOutcome r;
            r.correct = trendsMatch(expected, trendGpu(counts.data(), n, weights.data(), TrendKernel::Global, &r.times));
            return r;
        };
        c.gpuVariants.push_back({"tiled", [&] {
            GpuOutcome r;
            r.correct = trendsMatch(expected, trendGpu(counts.data(), n, weights.data(), TrendKernel::Tiled, &r.times));
            return r;
        }});
        runner.run(c);
    }
}

void benchCoverageField(BenchRunner& runner) {
    for (const int grid : {64, 128, 256, 512, 1024, 2048, 4096}) {
        const std::size_t cells = static_cast<std::size_t>(grid) * static_cast<std::size_t>(grid);
        if (!runner.wants(cells)) continue;
        std::mt19937 rng(static_cast<unsigned>(grid));
        CoverageMap map;
        map.grid = grid;
        map.stepXMs = 7;
        map.stepYMs = 9;
        map.open.resize(cells);
        for (unsigned char& o : map.open) o = rng() % 7u != 0 ? 1 : 0;
        std::vector<int> seed(cells, kCoverFar);
        for (std::size_t k = 0; k < std::max<std::size_t>(4, cells / 1024); ++k) seed[rng() % cells] = 0;
        int sweeps = 0;
        const std::vector<int> expected = coverageCpu(map, seed.data(), &sweeps);
        std::cerr << "bench: coverage_field " << grid << "x" << grid << ": " << sweeps << " pasadas hasta converger\n";
        BenchCase c;
        c.app = "coverage_field";
        c.concept = "06_stencil";
        c.size = std::to_string(grid) + "x" + std::to_string(grid);
        c.items = cells;
        c.cpu1 = [&] {
            Timed r;
            std::vector<int> out;
            r.ms = timeMs([&] { out = coverageCpu(map, seed.data()); });
            r.correct = out == expected;
            return r;
        };
        c.cpuN = [&](ThreadPool& pool) {
            Timed r;
            std::vector<int> out;
            r.ms = timeMs([&] { out = coverageCpuParallel(map, seed.data(), pool); });
            r.correct = out == expected;
            return r;
        };
        c.cpuReferences.push_back({"dijkstra", [&] {
            Timed r;
            std::vector<int> out;
            r.ms = timeMs([&] { out = coverageDijkstra(map, seed.data()); });
            r.correct = out == expected;
            return r;
        }});
        c.gpu = [&] {
            GpuOutcome r;
            r.correct = coverageGpu(map, seed.data(), CoverKernel::Basic, &r.times) == expected;
            return r;
        };
        c.gpuVariants.push_back({"tiled", [&] {
            GpuOutcome r;
            r.correct = coverageGpu(map, seed.data(), CoverKernel::Tiled, &r.times) == expected;
            return r;
        }});
        runner.run(c);
    }
}

}

std::vector<BenchApp> benchApps() {
    return {
        {"vector_add", "00_reference", benchVectorAdd},
        {"travel_table", "03_memory_tiling", benchTravelTable},
        {"eta_batch", "04_performance", benchEtaBatch},
        {"dispatch_argmin", "08_reduction", benchDispatchArgmin},
        {"dispatch_round", "08_reduction", benchDispatchRound},
        {"group_orders", "09_scan", benchGroupOrders},
        {"round_arrival", "09_scan", benchRoundArrival},
        {"round_grouped", "09_scan", benchRoundGrouped},
        {"delivery_histogram", "07_histogram", benchDeliveryHistogram},
        {"demand_heatmap", "05_conv", benchDemandHeatmap},
        {"coverage_field", "06_stencil", benchCoverageField},
        {"delivery_kpis", "08_reduction", benchDeliveryKpis},
        {"backlog_curve", "09_scan", benchBacklogCurve},
        {"restaurant_slots", "07_histogram", benchRestaurantSlots},
        {"demand_trend", "05_conv", benchDemandTrend},
    };
}
