#include "map_projection.h"

#include <stdexcept>

#include "graph.h"

MapProjection::MapProjection(const Bounds& bounds, int widthPx, int heightPx)
    : bounds_(bounds), width_(widthPx), height_(heightPx) {
    if (!(bounds.south < bounds.north) || !(bounds.west < bounds.east)) {
        throw std::invalid_argument("map.bounds invalido: se requiere south < north y west < east");
    }
    if (widthPx <= 0 || heightPx <= 0) {
        throw std::invalid_argument("la imagen del mapa debe tener ancho y alto positivos");
    }
}

Pixel MapProjection::toPixel(double lat, double lon) const {
    Pixel p;
    p.x = (lon - bounds_.west) / (bounds_.east - bounds_.west) * width_;
    p.y = (bounds_.north - lat) / (bounds_.north - bounds_.south) * height_;
    return p;
}

void MapProjection::toGeo(const Pixel& p, double& lat, double& lon) const {
    lon = bounds_.west + p.x / width_ * (bounds_.east - bounds_.west);
    lat = bounds_.north - p.y / height_ * (bounds_.north - bounds_.south);
}

bool MapProjection::contains(double lat, double lon) const {
    return lat <= bounds_.north && lat >= bounds_.south && lon >= bounds_.west && lon <= bounds_.east;
}

double MapProjection::metersPerPixel() const {
    const double midLat = (bounds_.north + bounds_.south) / 2.0;
    const double midLon = (bounds_.west + bounds_.east) / 2.0;
    const double horizontal = distanceM(midLat, bounds_.west, midLat, bounds_.east) / width_;
    const double vertical = distanceM(bounds_.north, midLon, bounds_.south, midLon) / height_;
    return (horizontal + vertical) / 2.0;
}
