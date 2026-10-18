#pragma once

#include <cstddef>
#include <vector>

// Scan exclusivo (suma prefija): out[i] = in[0] + ... + in[i-1], out[0] = 0.
// Con un vector de ceros y unos ("¿este pedido va a este restaurante?"), el
// resultado da a cada pedido su posicion entre los de su restaurante dentro
// de la rafaga (compactacion de flujo). Version secuencial: las rafagas son
// de pocas decenas de elementos.
inline std::vector<std::size_t> exclusiveScan(const std::vector<std::size_t>& in) {
    std::vector<std::size_t> out(in.size());
    std::size_t acc = 0;
    for (std::size_t i = 0; i < in.size(); ++i) {
        out[i] = acc;
        acc += in[i];
    }
    return out;
}
