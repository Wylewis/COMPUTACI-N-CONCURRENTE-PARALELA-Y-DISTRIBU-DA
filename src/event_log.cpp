#include "event_log.h"

#include <cmath>
#include <stdexcept>

#include "nlohmann/json.hpp"

// ordered_json conserva el orden de insercion de las claves: t y event primero.
using json = nlohmann::ordered_json;

// ---------------------------------------------------------------------------
// Event
// ---------------------------------------------------------------------------

Event Event::orderCreated(std::int64_t t, const std::string& order, const std::string& restaurant) {
    Event e;
    e.t = t;
    e.type = EventType::OrderCreated;
    e.order = order;
    e.restaurant = restaurant;
    return e;
}

Event Event::orderAssigned(std::int64_t t, const std::string& order, const std::string& courier) {
    Event e;
    e.t = t;
    e.type = EventType::OrderAssigned;
    e.order = order;
    e.courier = courier;
    return e;
}

Event Event::pickupStarted(std::int64_t t, const std::string& order, const std::string& courier,
                           const std::string& restaurant) {
    Event e;
    e.t = t;
    e.type = EventType::PickupStarted;
    e.order = order;
    e.courier = courier;
    e.restaurant = restaurant;
    return e;
}

Event Event::orderPickedUp(std::int64_t t, const std::string& order, const std::string& courier,
                           const std::string& restaurant) {
    Event e;
    e.t = t;
    e.type = EventType::OrderPickedUp;
    e.order = order;
    e.courier = courier;
    e.restaurant = restaurant;
    return e;
}

Event Event::orderDelivered(std::int64_t t, const std::string& order, const std::string& courier) {
    Event e;
    e.t = t;
    e.type = EventType::OrderDelivered;
    e.order = order;
    e.courier = courier;
    return e;
}

Event Event::orderRejected(std::int64_t t, const std::string& order, RejectReason reason) {
    Event e;
    e.t = t;
    e.type = EventType::OrderRejected;
    e.order = order;
    e.reason = reason;
    return e;
}

Event Event::courierBreakdown(std::int64_t t, const std::string& courier) {
    Event e;
    e.t = t;
    e.type = EventType::CourierBreakdown;
    e.courier = courier;
    return e;
}

Event Event::orderReassigned(std::int64_t t, const std::string& order, const std::string& from,
                             const std::string& to) {
    Event e;
    e.t = t;
    e.type = EventType::OrderReassigned;
    e.order = order;
    e.from = from;
    e.to = to;
    return e;
}

Event Event::simulationStopping(std::int64_t t) {
    Event e;
    e.t = t;
    e.type = EventType::SimulationStopping;
    return e;
}

std::string Event::toJsonLine() const {
    json j;
    j["t"] = t;
    j["event"] = toString(type);
    if (order) j["order"] = *order;
    if (courier) j["courier"] = *courier;
    if (restaurant) j["restaurant"] = *restaurant;
    if (from) j["from"] = *from;
    if (to) j["to"] = *to;
    if (reason) j["reason"] = toString(*reason);
    return j.dump();  // una sola linea, sin saltos
}

// ---------------------------------------------------------------------------
// Summary
// ---------------------------------------------------------------------------

std::string Summary::toJsonLine() const {
    json j;
    j["created"] = created;
    j["delivered"] = delivered;
    j["rejected"] = rejected;
    j["pending"] = pending;
    j["reassigned"] = reassigned;
    j["breakdowns"] = breakdowns;
    j["avgDeliveryMs"] = avgDeliveryMs;
    j["maxDeliveryMs"] = maxDeliveryMs;
    json per = json::object();
    for (const auto& [courier, n] : deliveredPerCourier) per[courier] = n;
    j["deliveredPerCourier"] = per;
    return j.dump();
}

// ---------------------------------------------------------------------------
// EventLog
// ---------------------------------------------------------------------------

EventLog::EventLog(const std::string& path, const std::vector<std::string>& courierIds)
    : out_(path, std::ios::out | std::ios::trunc) {
    if (!out_) {
        throw std::runtime_error("no se puede escribir el log de eventos: " + path);
    }
    for (const auto& id : courierIds) summary_.deliveredPerCourier[id] = 0;
}

EventLog::~EventLog() {
    close();
}

void EventLog::write(const Event& event) {
    if (closed_) {
        throw std::logic_error("el log de eventos ya esta cerrado");
    }

    Event e = event;
    if (e.order) {
        auto it = lastT_.find(*e.order);
        if (it != lastT_.end() && e.t < it->second) {
            e.t = it->second;  // el tiempo de un pedido nunca retrocede
            ++clockFixes_;
        }
        lastT_[*e.order] = e.t;
    }

    switch (e.type) {
        case EventType::OrderCreated:
            ++summary_.created;
            if (e.order) createdAt_[*e.order] = e.t;
            break;
        case EventType::OrderDelivered: {
            ++summary_.delivered;
            if (e.courier) ++summary_.deliveredPerCourier[*e.courier];
            std::int64_t elapsed = 0;
            if (e.order) {
                auto it = createdAt_.find(*e.order);
                if (it != createdAt_.end()) elapsed = e.t - it->second;
            }
            deliveredTotalMs_ += elapsed;
            if (elapsed > summary_.maxDeliveryMs) summary_.maxDeliveryMs = elapsed;
            break;
        }
        case EventType::OrderRejected:
            ++summary_.rejected;
            break;
        case EventType::OrderReassigned:
            ++summary_.reassigned;
            break;
        case EventType::CourierBreakdown:
            ++summary_.breakdowns;
            break;
        default:
            break;
    }

    out_ << e.toJsonLine() << '\n';
    out_.flush();
    ++lines_;
}

void EventLog::close() {
    if (closed_) return;
    out_.flush();
    out_.close();
    closed_ = true;
}

Summary EventLog::summary() const {
    Summary s = summary_;
    s.pending = s.created - s.delivered - s.rejected;
    s.avgDeliveryMs = s.delivered > 0
        ? static_cast<std::int64_t>(std::llround(static_cast<double>(deliveredTotalMs_) /
                                                 static_cast<double>(s.delivered)))
        : 0;
    return s;
}
