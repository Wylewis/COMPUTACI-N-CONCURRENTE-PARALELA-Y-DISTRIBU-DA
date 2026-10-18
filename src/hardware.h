#pragma once

#include <cstddef>

// Decisiones que dependen del paralelismo disponible en el hardware.

// Hilos que el hardware puede ejecutar a la vez segun
// std::thread::hardware_concurrency(); 0 si no lo informa.
unsigned hardwareThreads();

// Tamano del pool de estimacion a partir de los hilos del hardware:
//  - uno por hilo de hardware, porque las estimaciones son calculo puro y
//    mas hilos que nucleos solo agregan cambios de contexto;
//  - nunca menos de `minimum`, para que exista concurrencia real aunque la
//    maquina tenga un solo nucleo o no informe cuantos tiene;
//  - nunca mas de `maximum`, porque los candidatos de un pedido son pocas
//    decenas y un pool mayor quedaria ocioso.
unsigned clampPoolSize(unsigned hardware, unsigned minimum, unsigned maximum);

constexpr unsigned kPoolMinimum = 2;
constexpr unsigned kPoolMaximum = 32;

// clampPoolSize(hardwareThreads(), kPoolMinimum, kPoolMaximum)
unsigned recommendedPoolSize();

// Tareas para `work` unidades de trabajo independientes (precalculos al
// arrancar): como maximo una por hilo de hardware y nunca mas que unidades;
// 2 si el hardware no informa.
unsigned defaultTaskCount(std::size_t work);
