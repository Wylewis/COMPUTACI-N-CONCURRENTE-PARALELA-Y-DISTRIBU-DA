#include "window.h"

#include <opencv2/highgui.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <stdexcept>

namespace {
cv::Point at(const MapProjection& proj, const Graph::Node& n) {
    const Pixel p = proj.toPixel(n.lat, n.lon);
    return {static_cast<int>(p.x), static_cast<int>(p.y)};
}
}  // namespace

MapWindow::MapWindow(const Config& cfg, const Graph& graph)
    : projection_(cfg.map.bounds, 1, 1) {
    base_ = cv::imread(cfg.imagePath(), cv::IMREAD_COLOR);
    if (base_.empty()) throw std::runtime_error("no se puede leer la imagen del mapa: " + cfg.imagePath());
    projection_ = MapProjection(cfg.map.bounds, base_.cols, base_.rows);
    drawStatic(cfg, graph);
}

void MapWindow::drawStatic(const Config& cfg, const Graph& graph) {
    for (const StreetConfig& s : cfg.streets) {
        const cv::Point a = at(projection_, graph.nodeAt(graph.nodeIndex(s.from)));
        const cv::Point b = at(projection_, graph.nodeAt(graph.nodeIndex(s.to)));
        cv::line(base_, a, b, s.oneWay ? oneWayColor() : twoWayColor(), 2, cv::LINE_AA);
        if (s.oneWay) cv::circle(base_, b, 3, oneWayColor(), cv::FILLED, cv::LINE_AA);  // punta: sentido de la calle
    }
    for (const RestaurantConfig& r : cfg.restaurants) {
        const cv::Point p = at(projection_, graph.nodeAt(graph.nodeIndex(r.node)));
        cv::circle(base_, p, 7, restaurantColor(), cv::FILLED, cv::LINE_AA);
        cv::circle(base_, p, 7, {255, 255, 255}, 1, cv::LINE_AA);
        cv::putText(base_, r.name, p + cv::Point(10, 4), cv::FONT_HERSHEY_SIMPLEX, 0.4, {30, 30, 30}, 1, cv::LINE_AA);
    }
    const int band = 18;
    cv::rectangle(base_, {0, base_.rows - band, base_.cols, band}, {255, 255, 255}, cv::FILLED);
    cv::putText(base_, cfg.map.attribution.empty() ? "(c) OpenStreetMap contributors" : cfg.map.attribution,
                {6, base_.rows - 5}, cv::FONT_HERSHEY_SIMPLEX, 0.4, {60, 60, 60}, 1, cv::LINE_AA);
}

bool MapWindow::show(const cv::Mat& frame, int waitMs) {
    if (!opened_) {
        cv::namedWindow(kTitle, cv::WINDOW_AUTOSIZE);
        opened_ = true;
    }
    cv::imshow(kTitle, frame);
    const int key = cv::waitKey(waitMs > 0 ? waitMs : 1);
    if (key == 'q' || key == 27) return false;
    return cv::getWindowProperty(kTitle, cv::WND_PROP_VISIBLE) >= 1.0;
}
