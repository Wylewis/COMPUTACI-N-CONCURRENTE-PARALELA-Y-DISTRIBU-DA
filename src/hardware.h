#pragma once

#include <cstddef>

unsigned hardwareThreads();

unsigned clampPoolSize(unsigned hardware, unsigned minimum, unsigned maximum);

constexpr unsigned kPoolMinimum = 2;
constexpr unsigned kPoolMaximum = 32;

unsigned recommendedPoolSize();

unsigned defaultTaskCount(std::size_t work);
