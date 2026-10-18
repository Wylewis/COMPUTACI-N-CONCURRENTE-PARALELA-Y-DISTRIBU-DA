#pragma once

#include <cstddef>
#include <vector>

// Scan exclusivo (suma prefija): out[i] = in[0] + ... + in[i-1], out[0] = 0.
// Con un vector de ceros y unos ("¿este pedido va a este restaurante?"), el
// resultado da a cada pedido su posicion entre los de su restaurante dentro
// de la rafaga (compactacion de flujo). Con los tiempos de servicio de una
// fila da el instante en que cada uno empieza. Version secuencial: los
// vectores son de pocas decenas de elementos.
template <class T>
std::vector<T> exclusiveScan(const std::vector<T>& in) {
    std::vector<T> out(in.size());
    T acc = 0;
    for (std::size_t i = 0; i < in.size(); ++i) {
        out[i] = acc;
        acc += in[i];
    }
    return out;
}

// Sobrecarga para llamadas con lista entre llaves, como {1, 0, 1}.
inline std::vector<std::size_t> exclusiveScan(const std::vector<std::size_t>& in) {
    return exclusiveScan<std::size_t>(in);
}
