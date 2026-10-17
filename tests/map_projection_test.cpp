// Pruebas de la proyeccion del mapa: esquinas, centro, ida y vuelta, puntos
// fuera, limites invalidos, y con la ciudad real que todos los nodos y
// restaurantes caen dentro de la imagen.
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>

#include "config.h"
#include "map_projection.h"

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

}  // namespace

int main(int argc, char** argv) {
    Bounds b;
    b.north = -17.75;
    b.south = -17.77;
    b.west = -63.20;
    b.east = -63.18;
    const MapProjection proj(b, 200, 400);

    // Esquinas
    {
        const Pixel nw = proj.toPixel(b.north, b.west);
        check(near(nw.x, 0, 1e-9) && near(nw.y, 0, 1e-9), "la esquina noroeste es (0, 0)");
        const Pixel se = proj.toPixel(b.south, b.east);
        check(near(se.x, 200, 1e-9) && near(se.y, 400, 1e-9), "la esquina sureste es (ancho, alto)");
        const Pixel ne = proj.toPixel(b.north, b.east);
        check(near(ne.x, 200, 1e-9) && near(ne.y, 0, 1e-9), "la esquina noreste es (ancho, 0)");
    }
    // Centro y proporciones: la latitud crece hacia arriba, el pixel y hacia abajo
    {
        const Pixel c = proj.toPixel(-17.76, -63.19);
        check(near(c.x, 100, 1e-6) && near(c.y, 200, 1e-6), "el centro geografico es el centro de la imagen");
        const Pixel q = proj.toPixel(-17.755, -63.195);
        check(near(q.x, 50, 1e-6) && near(q.y, 100, 1e-6), "un cuarto hacia el noroeste");
        const Pixel up = proj.toPixel(-17.751, -63.19);
        const Pixel down = proj.toPixel(-17.769, -63.19);
        check(up.y < down.y, "mas al norte es un y menor");
    }
    // Ida y vuelta
    {
        const double lat0 = -17.7634, lon0 = -63.1912;
        const Pixel p = proj.toPixel(lat0, lon0);
        double lat = 0, lon = 0;
        proj.toGeo(p, lat, lon);
        check(near(lat, lat0, 1e-12) && near(lon, lon0, 1e-12), "toGeo deshace a toPixel");
    }
    // Dentro y fuera
    {
        check(proj.contains(-17.76, -63.19), "el centro esta dentro");
        check(proj.contains(b.north, b.west) && proj.contains(b.south, b.east), "los bordes cuentan como dentro");
        check(!proj.contains(-17.74, -63.19), "al norte del limite esta fuera");
        check(!proj.contains(-17.76, -63.17), "al este del limite esta fuera");
        const Pixel out = proj.toPixel(-17.74, -63.21);
        check(out.x < 0 && out.y < 0, "un punto fuera da pixeles negativos, no se recorta");
    }
    // Escala: la imagen de 200 px cubre 0.02 grados de longitud, unos 2.1 km
    {
        const double mpp = proj.metersPerPixel();
        check(mpp > 5.0 && mpp < 12.0, "metros por pixel plausibles para este recuadro");
    }
    // Limites invalidos
    {
        bool threw = false;
        Bounds bad = b;
        bad.north = bad.south;
        try { MapProjection p(bad, 10, 10); } catch (const std::invalid_argument&) { threw = true; }
        check(threw, "north igual a south se rechaza");
        threw = false;
        try { MapProjection p(b, 0, 10); } catch (const std::invalid_argument&) { threw = true; }
        check(threw, "ancho 0 se rechaza");
    }

    // Ciudad real: todos los nodos dentro de la imagen de 712 x 991
    if (argc > 1) {
        try {
            Config city = loadConfig(argv[1]);
            validateConfig(city);
            const MapProjection cp(city.map.bounds, 712, 991);
            bool allInside = true;
            for (const auto& n : city.nodes) {
                const Pixel p = cp.toPixel(n.lat, n.lon);
                if (!cp.contains(n.lat, n.lon) || p.x < 0 || p.x > 712 || p.y < 0 || p.y > 991) allInside = false;
            }
            check(allInside, "todos los nodos de la ciudad caen dentro de la imagen");
            const auto& start = city.fleet.startNode;
            Pixel sp;
            for (const auto& n : city.nodes) if (n.id == start) sp = cp.toPixel(n.lat, n.lon);
            check(sp.x > 100 && sp.x < 612 && sp.y > 100 && sp.y < 891, "el punto de partida no esta pegado al borde");
            std::cout << "ciudad real: " << cp.metersPerPixel() << " m por pixel; startNode en (" << sp.x << ", "
                      << sp.y << ")\n";
        } catch (const std::exception& e) {
            check(false, std::string("la ciudad real no se pudo cargar: ") + e.what());
        }
    }

    if (failures == 0) {
        std::cout << "map_projection_test: todas las pruebas pasaron\n";
        return 0;
    }
    std::cerr << "map_projection_test: " << failures << " prueba(s) fallaron\n";
    return 1;
}
