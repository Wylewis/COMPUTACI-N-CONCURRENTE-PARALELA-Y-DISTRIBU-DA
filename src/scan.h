#pragma once

#include <cstddef>
#include <vector>

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

inline std::vector<std::size_t> exclusiveScan(const std::vector<std::size_t>& in) {
    return exclusiveScan<std::size_t>(in);
}
