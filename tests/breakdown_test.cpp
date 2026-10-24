#include <iostream>
#include <set>
#include <string>
#include <vector>

#include "breakdown.h"

namespace {

int failures = 0;

void check(bool ok, const std::string& what) {
    if (!ok) {
        ++failures;
        std::cerr << "FALLO: " << what << "\n";
    }
}

std::vector<int> sequence(std::uint64_t seed, std::size_t courier, double p, int n) {
    BreakdownRoll r(seed, courier, p);
    std::vector<int> out;
    for (int i = 0; i < n; ++i) {
        const auto leg = r.roll(4);
        out.push_back(leg ? static_cast<int>(*leg) : -1);
    }
    return out;
}

}

int main() {
    {
        BreakdownRoll never(7, 0, 0.0), always(7, 0, 1.0);
        bool ok0 = true, ok1 = true;
        std::set<std::size_t> legs;
        for (int i = 0; i < 1000; ++i) {
            ok0 = ok0 && !never.roll(3);
            const auto leg = always.roll(3);
            ok1 = ok1 && leg && *leg < 3;
            if (leg) legs.insert(*leg);
        }
        check(ok0 && never.trips() == 1000 && never.breakdowns() == 0, "con probabilidad 0 no hay averias");
        check(ok1 && always.breakdowns() == 1000 && legs.size() == 3, "con probabilidad 1 todas, y el tramo recorre los tres");
        check(!always.roll(0) && always.trips() == 1000, "un viaje sin tramos no se sortea");
    }
    {
        BreakdownRoll r(42, 3, 0.02);
        for (int i = 0; i < 10000; ++i) r.roll(5);
        check(r.breakdowns() > 150 && r.breakdowns() < 250, "la frecuencia observada ronda la probabilidad");
    }
    {
        const auto a = sequence(42, 0, 0.3, 200), b = sequence(42, 0, 0.3, 200);
        check(a == b, "la misma semilla y moto dan la misma secuencia");
        check(sequence(42, 1, 0.3, 200) != a && sequence(43, 0, 0.3, 200) != a, "otra moto u otra semilla dan otra secuencia");
    }

    if (failures == 0) {
        std::cout << "breakdown_test: todas las pruebas pasaron\n";
        return 0;
    }
    std::cerr << "breakdown_test: " << failures << " prueba(s) fallaron\n";
    return 1;
}
