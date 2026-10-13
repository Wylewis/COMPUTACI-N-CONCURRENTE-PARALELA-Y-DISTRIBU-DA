// Pruebas del movimiento sobre una ruta: tiempos de viaje, posicion en cada
// instante, continuidad del avance y permanencia sobre la calle. Con la ruta
// del config real como argumento, prueba ademas una ruta de la ciudad.
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>

#include "config.h"
#include "graph.h"
#include "route_motion.h"

namespace {

int failures = 0;

void check(bool ok, const std::string& what) {
    if (!ok) {
        ++failures;
        std::cerr << "FALLO: " << what << "\n";
    }
}

bool near(double a, double b, double tol) {
    return std::fabs(a - b) <= tol;
}

// Tres nodos en linea sobre el ecuador: A en 0, B a 1000 m, C a 1500 m (hacia el este).
Config lineCity() {
    Config cfg;
    cfg.nodes = {{"A", 0.0, 0.0}, {"B", 0.0, 0.0089932}, {"C", 0.0, 0.0134898}};
    cfg.streets = {{"ab", "A", "B", false}, {"bc", "B", "C", false}};
    return cfg;
}

// Distancia perpendicular aproximada de p a la recta a-b, en grados.
double offLine(const GeoPoint& p, const GeoPoint& a, const GeoPoint& b) {
    const double dx = b.lon - a.lon, dy = b.lat - a.lat;
    const double len = std::hypot(dx, dy);
    if (len == 0) return std::hypot(p.lon - a.lon, p.lat - a.lat);
    return std::fabs(dy * p.lon - dx * p.lat + b.lon * a.lat - b.lat * a.lon) / len;
}

}  // namespace

int main(int argc, char** argv) {
    // Tiempo de viaje: 36 km/h son 10 m/s
    check(travelTimeMs(1000.0, 36.0) == 100'000, "1000 m a 36 km/h son 100 s");
    check(travelTimeMs(0.0, 30.0) == 0, "0 m tardan 0");
    check(travelTimeMs(-5.0, 30.0) == 0, "metros negativos cuentan como 0");
    {
        bool threw = false;
        try { travelTimeMs(100.0, 0.0); } catch (const std::invalid_argument&) { threw = true; }
        check(threw, "velocidad 0 se rechaza");
    }

    // Interpolacion
    {
        const GeoPoint m = interpolate({0, 0}, {2, 4}, 0.25);
        check(near(m.lat, 0.5, 1e-12) && near(m.lon, 1.0, 1e-12), "interpolate a un cuarto");
        const GeoPoint over = interpolate({0, 0}, {2, 4}, 1.7);
        check(near(over.lat, 2, 1e-12) && near(over.lon, 4, 1e-12), "t mayor que 1 se recorta a b");
    }

    const Config cfg = lineCity();
    const Graph g(cfg);
    const auto route = g.shortestPath("A", "C");
    check(route.has_value() && route->nodes.size() == 3, "ruta A-B-C");
    if (!route) return 1;

    const RouteMotion motion(*route, g, 36.0, 5'000);
    check(motion.legs().size() == 2, "dos tramos");
    check(near(motion.lengthM(), 1500.0, 1.0), "longitud total 1500 m");
    check(near(static_cast<double>(motion.durationMs()), 150'000.0, 100.0), "1500 m a 36 km/h duran 150 s");
    check(motion.arrivalMs() == motion.startMs() + motion.durationMs(), "llegada = salida + duracion");
    check(motion.legs()[0].streetId == "ab" && motion.legs()[1].streetId == "bc", "los tramos llevan su calle");

    // Antes de salir: en el origen
    {
        const GeoPoint p = motion.positionAt(0);
        check(near(p.lat, 0, 1e-9) && near(p.lon, 0, 1e-9), "antes de salir esta en A");
        check(motion.metersAt(4'999) == 0.0, "antes de salir no avanzo");
        check(!motion.arrived(4'999), "antes de salir no llego");
    }
    // A mitad del primer tramo: 500 m, 50 s despues de salir
    {
        const GeoPoint p = motion.positionAt(5'000 + 50'000);
        check(near(p.lat, 0, 1e-9) && near(p.lon, 0.0089932 / 2, 1e-6), "a los 50 s esta a mitad de A-B");
        check(motion.legIndexAt(5'000 + 50'000) == 0, "sigue en el primer tramo");
    }
    // Justo pasado B: tramo 1
    {
        check(motion.legIndexAt(5'000 + 100'500) == 1, "despues de 100.5 s esta en el segundo tramo");
        const GeoPoint p = motion.positionAt(5'000 + 125'000);
        check(near(p.lon, 0.0089932 + 0.0044966 / 2, 1e-6), "a los 125 s esta a mitad de B-C");
    }
    // Llegada y mas alla: en C
    {
        const GeoPoint p = motion.positionAt(5'000 + 150'000);
        check(near(p.lon, 0.0134898, 1e-6), "al llegar esta en C");
        const GeoPoint q = motion.positionAt(5'000 + 999'000);
        check(near(q.lon, 0.0134898, 1e-6), "mucho despues sigue en C");
        check(motion.arrived(5'000 + 150'000) && !motion.arrived(5'000 + 149'999), "arrived cambia justo en la llegada");
        check(motion.legIndexAt(5'000 + 999'000) == 1, "una vez llegado, el tramo es el ultimo");
    }
    // Avance continuo y monotono, siempre sobre la calle
    {
        double lastLon = -1.0;
        bool monotone = true, onStreet = true;
        for (std::int64_t t = 0; t <= 5'000 + 160'000; t += 1'000) {
            const GeoPoint p = motion.positionAt(t);
            if (p.lon < lastLon - 1e-12) monotone = false;
            lastLon = p.lon;
            if (std::fabs(p.lat) > 1e-9) onStreet = false;
        }
        check(monotone, "la posicion nunca retrocede");
        check(onStreet, "la posicion nunca se sale de la linea A-B-C");
        // Dos instantes separados 1 s difieren en 10 m
        const GeoPoint p1 = motion.positionAt(20'000), p2 = motion.positionAt(21'000);
        check(near(distanceM(p1.lat, p1.lon, p2.lat, p2.lon), 10.0, 0.05), "cada segundo avanza 10 m");
    }
    // Duraciones por tramo suman la duracion total
    {
        const auto d = motion.legDurationsMs();
        check(d.size() == 2 && near(static_cast<double>(d[0]), 100'000, 100) && near(static_cast<double>(d[1]), 50'000, 100),
              "duraciones por tramo: 100 s y 50 s");
    }
    // Ruta de un solo nodo: no hay viaje
    {
        const auto still = g.shortestPath("B", "B");
        const RouteMotion m(*still, g, 36.0, 0);
        check(m.legs().empty() && m.lengthM() == 0.0 && m.durationMs() == 0, "ruta trivial: sin tramos ni duracion");
        const GeoPoint p = m.positionAt(12'345);
        check(near(p.lon, 0.0089932, 1e-9), "ruta trivial: siempre en el nodo");
        check(m.arrived(0), "ruta trivial: ya llego");
    }
    // Errores
    {
        bool threw = false;
        try { RouteMotion bad(Route{}, g, 36.0, 0); } catch (const std::invalid_argument&) { threw = true; }
        check(threw, "ruta vacia se rechaza");
    }

    // Ciudad real
    if (argc > 1) {
        try {
            Config city = loadConfig(argv[1]);
            validateConfig(city);
            const Graph cg(city);
            const auto r = cg.shortestPath(city.restaurants.front().node, city.restaurants.back().node);
            check(r.has_value(), "hay ruta entre el primer y el ultimo restaurante");
            if (r) {
                const RouteMotion m(*r, cg, city.fleet.speedKmh, 0);
                check(near(m.lengthM(), r->lengthM, 1e-6), "la longitud coincide con la de la ruta");
                bool onStreet = true;
                for (std::int64_t t = 0; t <= m.durationMs(); t += 500) {
                    const GeoPoint p = m.positionAt(t);
                    const RouteLeg& leg = m.legs()[m.legIndexAt(t)];
                    if (offLine(p, leg.a, leg.b) > 1e-9) onStreet = false;
                }
                check(onStreet, "en la ciudad real la moto siempre esta sobre su calle");
                std::cout << "ciudad real: ruta de " << r->lengthM << " m en " << m.legs().size()
                          << " tramos, " << m.durationMs() / 1000 << " s simulados a " << city.fleet.speedKmh
                          << " km/h\n";
            }
        } catch (const std::exception& e) {
            check(false, std::string("la ciudad real no se pudo cargar: ") + e.what());
        }
    }

    if (failures == 0) {
        std::cout << "route_motion_test: todas las pruebas pasaron\n";
        return 0;
    }
    std::cerr << "route_motion_test: " << failures << " prueba(s) fallaron\n";
    return 1;
}
