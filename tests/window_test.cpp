#include <iostream>
#include <string>

#include "window.h"

namespace {

int failures = 0;

void check(bool ok, const std::string& what) {
    if (!ok) {
        ++failures;
        std::cerr << "FALLO: " << what << "\n";
    }
}

bool isColor(const cv::Vec3b& px, const cv::Scalar& c) {
    return px[0] == c[0] && px[1] == c[1] && px[2] == c[2];
}

}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "uso: window_test <config.json>\n";
        return 2;
    }
    const Config cfg = loadConfig(argv[1]);
    const Graph graph(cfg);
    const MapWindow window(cfg, graph);
    const cv::Mat& base = window.base();
    check(base.cols == 712 && base.rows == 991 && window.projection().width() == 712, "el cuadro tiene el tamano de la imagen");

    int restaurantsDrawn = 0;
    for (const RestaurantConfig& r : cfg.restaurants) {
        const Graph::Node& n = graph.nodeAt(graph.nodeIndex(r.node));
        const Pixel p = window.projection().toPixel(n.lat, n.lon);
        if (isColor(base.at<cv::Vec3b>(static_cast<int>(p.y), static_cast<int>(p.x)), MapWindow::restaurantColor())) ++restaurantsDrawn;
    }
    check(restaurantsDrawn == static_cast<int>(cfg.restaurants.size()), "cada restaurante tiene su circulo en el pixel proyectado");

    int streetPx = 0;
    for (int y = 0; y < base.rows; ++y)
        for (int x = 0; x < base.cols; ++x) {
            const cv::Vec3b px = base.at<cv::Vec3b>(y, x);
            if (isColor(px, MapWindow::twoWayColor()) || isColor(px, MapWindow::oneWayColor())) ++streetPx;
        }
    check(streetPx > 5000, "hay pixeles de calle dibujados");

    cv::Mat f = window.frame();
    f.setTo(cv::Scalar(0, 0, 0));
    check(!isColor(base.at<cv::Vec3b>(5, 5), {0, 0, 0}) || f.data != base.data, "frame() es una copia independiente de la base");
    check(isColor(base.at<cv::Vec3b>(base.rows - 2, 2), {255, 255, 255}), "la banda de atribucion esta al pie");

    if (failures == 0) {
        std::cout << "window_test: todas las pruebas pasaron\n";
        return 0;
    }
    std::cerr << "window_test: " << failures << " prueba(s) fallaron\n";
    return 1;
}
