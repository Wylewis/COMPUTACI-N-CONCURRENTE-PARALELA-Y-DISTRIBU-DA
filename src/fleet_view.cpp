#include "fleet_view.h"

#include <array>
#include <opencv2/imgproc.hpp>

namespace {
cv::Point px(const MapProjection& proj, double lat, double lon) {
    const Pixel p = proj.toPixel(lat, lon);
    return {static_cast<int>(p.x), static_cast<int>(p.y)};
}
}  // namespace

cv::Scalar stateColor(CourierState state) {
    switch (state) {  // BGR
        case CourierState::Idle:         return {140, 140, 140};
        case CourierState::ToRestaurant: return {220, 120, 0};
        case CourierState::Queued:       return {0, 200, 230};
        case CourierState::PickingUp:    return {0, 140, 255};
        case CourierState::Delivering:   return {60, 180, 60};
        case CourierState::BrokenDown:   return {0, 0, 220};
    }
    return {0, 0, 0};
}

GeoPoint courierPosition(const CourierView& view, const Graph& graph, std::int64_t nowMs) {
    if (view.trip) return view.trip->positionAt(nowMs);
    const Graph::Node& n = graph.nodeAt(graph.nodeIndex(view.node));
    return {n.lat, n.lon};
}

void drawDeliveryPoints(cv::Mat& frame, const MapProjection& proj, const Graph& graph,
                        const std::vector<CourierView>& fleet) {
    for (const CourierView& c : fleet)
        for (const std::string& node : c.deliveryNodes) {
            const Graph::Node& n = graph.nodeAt(graph.nodeIndex(node));
            const cv::Point p = px(proj, n.lat, n.lon);
            cv::rectangle(frame, p - cv::Point(4, 4), p + cv::Point(4, 4), {200, 0, 200}, cv::FILLED);
        }
}

void drawCouriers(cv::Mat& frame, const MapProjection& proj, const Graph& graph,
                  const std::vector<CourierView>& fleet, std::int64_t nowMs) {
    for (const CourierView& c : fleet) {
        const GeoPoint g = courierPosition(c, graph, nowMs);
        const cv::Point p = px(proj, g.lat, g.lon);
        cv::circle(frame, p, 7, stateColor(c.state), cv::FILLED, cv::LINE_AA);
        cv::circle(frame, p, 7, {255, 255, 255}, 1, cv::LINE_AA);
        cv::putText(frame, c.id + " " + std::to_string(c.bagCount) + "/" + std::to_string(c.bagCapacity),
                    p + cv::Point(9, -6), cv::FONT_HERSHEY_SIMPLEX, 0.38, {20, 20, 20}, 1, cv::LINE_AA);
    }
}

void drawStatePanel(cv::Mat& frame, const std::vector<CourierView>& fleet, std::int64_t nowMs) {
    constexpr std::array<CourierState, 6> kStates = {CourierState::Idle,      CourierState::ToRestaurant,
                                                    CourierState::Queued,    CourierState::PickingUp,
                                                    CourierState::Delivering, CourierState::BrokenDown};
    const int height = 22;
    cv::rectangle(frame, {0, 0, frame.cols, height}, {255, 255, 255}, cv::FILLED);
    const std::int64_t s = nowMs / 1000;
    std::string line = "t " + std::to_string(s / 60) + ":" + (s % 60 < 10 ? "0" : "") + std::to_string(s % 60);
    int x = 6;
    cv::putText(frame, line, {x, 15}, cv::FONT_HERSHEY_SIMPLEX, 0.42, {20, 20, 20}, 1, cv::LINE_AA);
    x += 70;
    for (CourierState st : kStates) {
        int n = 0;
        for (const CourierView& c : fleet) n += c.state == st;
        cv::circle(frame, {x, 11}, 5, stateColor(st), cv::FILLED, cv::LINE_AA);
        const std::string label = std::string(toString(st)) + " " + std::to_string(n);
        cv::putText(frame, label, {x + 9, 15}, cv::FONT_HERSHEY_SIMPLEX, 0.4, {20, 20, 20}, 1, cv::LINE_AA);
        x += 24 + static_cast<int>(label.size()) * 8;
    }
}

void drawCountersPanel(cv::Mat& frame, const PanelStats& st) {
    cv::rectangle(frame, {0, 22, frame.cols, 20}, {255, 255, 255}, cv::FILLED);
    const std::string line = "creados " + std::to_string(st.created) + "   asignados " + std::to_string(st.assigned) +
                             "   recogidos " + std::to_string(st.pickedUp) + "   entregados " + std::to_string(st.delivered) +
                             "   rechazados " + std::to_string(st.rejected) + "   averias " + std::to_string(st.breakdowns) +
                             "   pendientes " + std::to_string(st.pending);
    cv::putText(frame, line, {6, 37}, cv::FONT_HERSHEY_SIMPLEX, 0.4, {20, 20, 20}, 1, cv::LINE_AA);
}

void drawFleet(cv::Mat& frame, const MapProjection& proj, const Graph& graph,
               const std::vector<CourierView>& fleet, std::int64_t nowMs) {
    drawDeliveryPoints(frame, proj, graph, fleet);
    drawCouriers(frame, proj, graph, fleet, nowMs);
    drawStatePanel(frame, fleet, nowMs);
}
