#pragma once

#include <opencv2/core.hpp>

#include "config.h"
#include "graph.h"
#include "map_projection.h"

class MapWindow {
public:
    static constexpr const char* kTitle = "Equipetrol Delivery";

    MapWindow(const Config& cfg, const Graph& graph);

    const MapProjection& projection() const { return projection_; }
    const cv::Mat& base() const { return base_; }
    cv::Mat frame() const { return base_.clone(); }

    bool show(const cv::Mat& frame, int waitMs);

    static cv::Scalar restaurantColor() { return {0, 140, 255}; }
    static cv::Scalar twoWayColor() { return {120, 120, 120}; }
    static cv::Scalar oneWayColor() { return {200, 120, 40}; }

private:
    void drawStatic(const Config& cfg, const Graph& graph);

    MapProjection projection_;
    cv::Mat base_;
    bool opened_ = false;
};
