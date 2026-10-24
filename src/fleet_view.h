#pragma once

#include <cstdint>
#include <opencv2/core.hpp>
#include <string>
#include <vector>

#include "courier.h"
#include "graph.h"
#include "map_projection.h"

cv::Scalar stateColor(CourierState state);

GeoPoint courierPosition(const CourierView& view, const Graph& graph, std::int64_t nowMs);

void drawDeliveryPoints(cv::Mat& frame, const MapProjection& proj, const Graph& graph,
                        const std::vector<CourierView>& fleet);
void drawCouriers(cv::Mat& frame, const MapProjection& proj, const Graph& graph,
                  const std::vector<CourierView>& fleet, std::int64_t nowMs);
void drawStatePanel(cv::Mat& frame, const std::vector<CourierView>& fleet, std::int64_t nowMs);

struct PanelStats {
    std::int64_t created = 0, assigned = 0, pickedUp = 0, delivered = 0, rejected = 0, breakdowns = 0, pending = 0;
};
void drawCountersPanel(cv::Mat& frame, const PanelStats& stats);

void drawFleet(cv::Mat& frame, const MapProjection& proj, const Graph& graph,
               const std::vector<CourierView>& fleet, std::int64_t nowMs);
