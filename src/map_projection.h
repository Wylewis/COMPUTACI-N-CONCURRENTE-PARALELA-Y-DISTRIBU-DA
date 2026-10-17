#pragma once

#include "config.h"

struct Pixel {
    double x = 0.0;  // columnas, de izquierda a derecha
    double y = 0.0;  // filas, de arriba hacia abajo
};

// Convierte entre coordenadas geograficas y pixeles de la imagen del mapa,
// usando el bounding box registrado en map.bounds (seccion 3): la esquina
// noroeste es el pixel (0, 0) y la sureste es (ancho, alto). La imagen se
// genero recortada exactamente a esos limites, asi que la conversion es
// lineal en cada eje.
//
// Inmutable: se consulta desde cualquier hilo sin cerrojo.
class MapProjection {
public:
    // Lanza std::invalid_argument si los limites o el tamano no tienen sentido.
    MapProjection(const Bounds& bounds, int widthPx, int heightPx);

    int width() const { return width_; }
    int height() const { return height_; }
    const Bounds& bounds() const { return bounds_; }

    Pixel toPixel(double lat, double lon) const;
    void toGeo(const Pixel& p, double& lat, double& lon) const;

    // El punto cae dentro de la imagen.
    bool contains(double lat, double lon) const;

    // Metros por pixel en el centro de la imagen (promedio de los dos ejes).
    double metersPerPixel() const;

private:
    Bounds bounds_;
    int width_;
    int height_;
};
