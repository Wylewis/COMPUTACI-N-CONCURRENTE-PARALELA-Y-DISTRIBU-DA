#include "restaurants.h"

Restaurants::Restaurants(const std::vector<RestaurantConfig>& cfg) {
    for (const RestaurantConfig& r : cfg) {
        index_[r.id] = all_.size();
        all_.push_back(std::make_unique<Restaurant>(r));
    }
}

void Restaurants::wake() {
    for (auto& r : all_) r->wake();
}
