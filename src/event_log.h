#pragma once

#include <cstdint>
#include <fstream>
#include <map>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "order.h"

constexpr const char* kStartLine = "Simulation has started...";

struct Event {
    std::int64_t t = 0;
    EventType type = EventType::OrderCreated;
    std::optional<std::string> order;
    std::optional<std::string> courier;
    std::optional<std::string> restaurant;
    std::optional<std::string> from;
    std::optional<std::string> to;
    std::optional<RejectReason> reason;

    static Event orderCreated(std::int64_t t, const std::string& order, const std::string& restaurant);
    static Event orderAssigned(std::int64_t t, const std::string& order, const std::string& courier);
    static Event pickupStarted(std::int64_t t, const std::string& order, const std::string& courier,
                               const std::string& restaurant);
    static Event orderPickedUp(std::int64_t t, const std::string& order, const std::string& courier,
                               const std::string& restaurant);
    static Event orderDelivered(std::int64_t t, const std::string& order, const std::string& courier);
    static Event orderRejected(std::int64_t t, const std::string& order, RejectReason reason);
    static Event courierBreakdown(std::int64_t t, const std::string& courier);
    static Event orderReassigned(std::int64_t t, const std::string& order, const std::string& from,
                                 const std::string& to);
    static Event simulationStopping(std::int64_t t);

    std::string toJsonLine() const;
};

struct Summary {
    std::int64_t created = 0;
    std::int64_t delivered = 0;
    std::int64_t rejected = 0;
    std::int64_t pending = 0;
    std::int64_t reassigned = 0;
    std::int64_t breakdowns = 0;
    std::int64_t avgDeliveryMs = 0;
    std::int64_t maxDeliveryMs = 0;
    std::map<std::string, std::int64_t> deliveredPerCourier;

    std::string toJsonLine() const;
};

class EventLog {
public:

    explicit EventLog(const std::string& path, const std::vector<std::string>& courierIds = {});
    ~EventLog();

    EventLog(const EventLog&) = delete;
    EventLog& operator=(const EventLog&) = delete;

    void write(const Event& e);

    void close();

    Summary summary() const;
    std::int64_t linesWritten() const { return lines_; }
    std::int64_t clockFixes() const { return clockFixes_; }

private:
    std::ofstream out_;
    bool closed_ = false;
    std::int64_t lines_ = 0;
    std::int64_t clockFixes_ = 0;

    Summary summary_;
    std::int64_t deliveredTotalMs_ = 0;
    std::unordered_map<std::string, std::int64_t> createdAt_;
    std::unordered_map<std::string, std::int64_t> lastT_;
};
