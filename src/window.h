#pragma once

#include <opencv2/core.hpp>

#include "config.h"
#include "graph.h"
#include "map_projection.h"

// La ventana del mapa. Dibuja una vez la capa estatica (imagen, calles,
// restaurantes, atribucion) y entrega copias para pintar la flota encima.
// Solo recibe copias: no toca ningun monitor.
class MapWindow {
public:
    static constexpr const char* kTitle = "Equipetrol Delivery";

    // Carga la imagen de map.image; lanza std::runtime_error si no se puede.
    MapWindow(const Config& cfg, const Graph& graph);

    const MapProjection& projection() const { return projection_; }
    const cv::Mat& base() const { return base_; }
    cv::Mat frame() const { return base_.clone(); }

    // Muestra el cuadro y espera waitMs. false si se cerro la ventana o se pulso q o Esc.
    bool show(const cv::Mat& frame, int waitMs);

    static cv::Scalar restaurantColor() { return {0, 140, 255}; }  // BGR: naranja
    static cv::Scalar twoWayColor() { return {120, 120, 120}; }
    static cv::Scalar oneWayColor() { return {200, 120, 40}; }

private:
    void drawStatic(const Config& cfg, const Graph& graph);

    MapProjection projection_;
    cv::Mat base_;
    bool opened_ = false;
};
