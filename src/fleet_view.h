#pragma once

#include <cstdint>
#include <opencv2/core.hpp>
#include <string>
#include <vector>

#include "courier.h"
#include "graph.h"
#include "map_projection.h"

// La capa viva de la ventana: motos por estado, puntos de entrega activos y
// un panel con el conteo por estado. Dibuja sobre un cuadro a partir de
// copias (CourierView), sin tocar ningun monitor.

cv::Scalar stateColor(CourierState state);

// Posicion de la moto en el instante simulado: sobre su viaje si esta en uno, si no en su nodo.
GeoPoint courierPosition(const CourierView& view, const Graph& graph, std::int64_t nowMs);

void drawDeliveryPoints(cv::Mat& frame, const MapProjection& proj, const Graph& graph,
                        const std::vector<CourierView>& fleet);
void drawCouriers(cv::Mat& frame, const MapProjection& proj, const Graph& graph,
                  const std::vector<CourierView>& fleet, std::int64_t nowMs);
void drawStatePanel(cv::Mat& frame, const std::vector<CourierView>& fleet, std::int64_t nowMs);

// Contadores de la seccion 6.3 para el panel; los arma main desde los contadores en vivo.
struct PanelStats {
    std::int64_t created = 0, assigned = 0, pickedUp = 0, delivered = 0, rejected = 0, breakdowns = 0, pending = 0;
};
void drawCountersPanel(cv::Mat& frame, const PanelStats& stats);  // segunda linea de la banda superior

// Todo lo anterior en orden: puntos de entrega, motos, panel.
void drawFleet(cv::Mat& frame, const MapProjection& proj, const Graph& graph,
               const std::vector<CourierView>& fleet, std::int64_t nowMs);
