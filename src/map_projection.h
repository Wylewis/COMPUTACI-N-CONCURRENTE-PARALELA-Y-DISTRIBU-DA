#pragma once

#include "config.h"

struct Pixel {
    double x = 0.0;
    double y = 0.0;
};

class MapProjection {
public:

    MapProjection(const Bounds& bounds, int widthPx, int heightPx);

    int width() const { return width_; }
    int height() const { return height_; }
    const Bounds& bounds() const { return bounds_; }

    Pixel toPixel(double lat, double lon) const;
    void toGeo(const Pixel& p, double& lat, double& lon) const;

    bool contains(double lat, double lon) const;

    double metersPerPixel() const;

private:
    Bounds bounds_;
    int width_;
    int height_;
};
