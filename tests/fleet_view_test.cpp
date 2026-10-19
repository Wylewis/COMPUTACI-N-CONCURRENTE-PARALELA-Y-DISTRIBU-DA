// Prueba sin pantalla de la capa viva: motos por estado, en viaje o en nodo,
// puntos de entrega y panel. Ciudad sintetica de dos nodos en un lienzo.
#include <iostream>
#include <memory>
#include <string>

#include "fleet_view.h"
#include "route_motion.h"

namespace {

int failures = 0;

void check(bool ok, const std::string& what) {
    if (!ok) {
        ++failures;
        std::cerr << "FALLO: " << what << "\n";
    }
}

bool isColor(const cv::Vec3b& px, const cv::Scalar& c) { return px[0] == c[0] && px[1] == c[1] && px[2] == c[2]; }

}  // namespace

int main() {
    Config cfg;  // A (oeste) ---1000 m--- B (este) y C suelto en medio; lienzo de 400x200: A x=100, C x=200, B x=300
    cfg.map.bounds = {0.0009, -0.0009, -0.0044966, 0.0134898};
    cfg.nodes = {{"A", 0.0, 0.0}, {"B", 0.0, 0.0089932}, {"C", 0.0, 0.0044966}};
    cfg.streets = {{"ab", "A", "B", false}};
    const Graph g(cfg);
    const MapProjection proj(cfg.map.bounds, 400, 200);
    const Pixel pa = proj.toPixel(0.0, 0.0), pb = proj.toPixel(0.0, 0.0089932);
    check(static_cast<int>(pa.x) == 100 && static_cast<int>(pb.x) == 300 && static_cast<int>(pa.y) == 100, "proyeccion del lienzo");

    auto trip = std::make_shared<const RouteMotion>(*g.shortestPath("A", "B"), g, 36.0, 0);  // 100 s de viaje
    CourierView idle{"c0", CourierState::Idle, "A", nullptr, 0, 2, {}};
    CourierView moving{"c1", CourierState::Delivering, "A", trip, 1, 2, {"C"}};
    CourierView broken{"c2", CourierState::BrokenDown, "B", nullptr, 1, 2, {}};
    const std::vector<CourierView> fleet = {idle, moving, broken};

    const GeoPoint mid = courierPosition(moving, g, 50'000);
    check(mid.lon > 0.0044 && mid.lon < 0.0046 && courierPosition(idle, g, 50'000).lon == 0.0, "posicion sobre el viaje o en el nodo");

    cv::Mat frame(200, 400, CV_8UC3, cv::Scalar(255, 255, 255));
    drawFleet(frame, proj, g, fleet, 25'000);  // c1 a un cuarto del viaje: x=150
    check(isColor(frame.at<cv::Vec3b>(100, 100), stateColor(CourierState::Idle)), "c0 inactiva pintada en A con su color");
    check(isColor(frame.at<cv::Vec3b>(100, 150), stateColor(CourierState::Delivering)), "c1 a un cuarto del viaje, en verde");
    check(isColor(frame.at<cv::Vec3b>(100, 300), stateColor(CourierState::BrokenDown)), "c2 averiada en B en rojo");
    check(isColor(frame.at<cv::Vec3b>(97, 203), {200, 0, 200}), "el punto de entrega de c1 (C) se marca en el nodo");
    check(isColor(frame.at<cv::Vec3b>(1, 1), {255, 255, 255}) && !isColor(frame.at<cv::Vec3b>(11, 76), {255, 255, 255}),
          "el panel de estados ocupa la banda superior con sus marcas");

    if (failures == 0) {
        std::cout << "fleet_view_test: todas las pruebas pasaron\n";
        return 0;
    }
    std::cerr << "fleet_view_test: " << failures << " prueba(s) fallaron\n";
    return 1;
}
