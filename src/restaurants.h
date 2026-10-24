#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "config.h"
#include "restaurant.h"

class Restaurants {
public:
    explicit Restaurants(const std::vector<RestaurantConfig>& cfg);
    Restaurants(const Restaurants&) = delete;
    Restaurants& operator=(const Restaurants&) = delete;

    Restaurant& at(const std::string& id) { return *all_[index_.at(id)]; }
    const Restaurant& at(const std::string& id) const { return *all_[index_.at(id)]; }
    std::size_t size() const { return all_.size(); }
    const Restaurant& operator[](std::size_t i) const { return *all_[i]; }
    void wake();

private:
    std::vector<std::unique_ptr<Restaurant>> all_;
    std::unordered_map<std::string, std::size_t> index_;
};
