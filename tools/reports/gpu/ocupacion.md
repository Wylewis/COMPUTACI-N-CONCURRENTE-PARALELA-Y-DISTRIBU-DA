# Mallas, bloques y ocupación de los kernels

Las columnas de ocupación salen de `./delivery_sim ../config/ciudad.json --gpu-info` en la máquina de la GPU, que usa `cudaOccupancyMaxActiveBlocksPerMultiprocessor` y los atributos de cada kernel. La malla y el motivo describen el código.

La tarjeta admite 1536 hilos por multiprocesador y tiene 38: aloja 58 368 hilos a la vez. Con bloques de 256 hilos entran 6 por multiprocesador, y en los 29 kernels el límite son los hilos, no los registros ni la shared memory.

| Aplicación | Kernel | Hilos por bloque | Malla | Por qué esa malla | Bloques por multiprocesador | Registros por hilo | Shared memory por bloque | Ocupación | Qué la limita |
|---|---|---|---|---|---|---|---|---|---|
| Tabla de tiempos de todos contra todos | `minPlusTiledKernel` | 16 × 16 = 256 | un bloque por tile de la tabla: ⌈n/16⌉ × ⌈n/16⌉ | cada bloque calcula un tile de 16 × 16 y guarda dos tiles en shared memory | 6 | 36 | 2048 B | 100 % | hilos |
| Estimación de entrega por lote | `etaSoaKernel` | 32 × 8 = 256 | ⌈motos/32⌉ × ⌈pedidos/8⌉ | 32 motos seguidas por fila: un warp lee 32 motos contiguas | 6 | 26 | 0 B | 100 % | hilos |
| Estimación de entrega por lote | `etaAosKernel` | 32 × 8 = 256 | ⌈motos/32⌉ × ⌈pedidos/8⌉ | la misma malla, para comparar solo la disposición de los datos | 6 | 24 | 0 B | 100 % | hilos |
| Mejor moto de cada pedido | `argminRowsKernel` | 256 = 256 | un bloque por pedido | cada bloque reduce una fila de la matriz con un árbol de 256 lugares | 6 | 32 | 2048 B | 100 % | hilos |
| Pedidos agrupados por restaurante | `countGroupsKernel` | 256 = 256 | ⌈pedidos/256⌉ | un hilo por pedido | 6 | 10 | 0 B | 100 % | hilos |
| Pedidos agrupados por restaurante | `scanSectionKernel` | 256 = 256 | ⌈restaurantes/256⌉ | una sección de 256 conteos por bloque, en shared memory | 6 | 12 | 1024 B | 100 % | hilos |
| Pedidos agrupados por restaurante | `addOffsetsKernel` | 256 = 256 | ⌈restaurantes/256⌉ | un hilo por conteo, con la misma malla que el scan | 6 | 10 | 0 B | 100 % | hilos |
| Pedidos agrupados por restaurante | `scatterOrdersKernel` | 256 = 256 | ⌈pedidos/256⌉ | un hilo por pedido | 6 | 16 | 0 B | 100 % | hilos |
| Histograma de tiempos de entrega | `histogramPrivatizedKernel` | 256 = 256 | ⌈entregas/16 384⌉ | 64 entregas por hilo; un histograma por bloque en shared memory | 6 | 24 | 1024 B | 100 % | hilos |
| Histograma de tiempos de entrega | `histogramAtomicKernel` | 256 = 256 | ⌈entregas/16 384⌉ | la misma malla, para comparar solo dónde se cuenta | 6 | 22 | 0 B | 100 % | hilos |
| Mapa de calor de demanda | `smoothConstantKernel` | 16 × 16 = 256 | ⌈lado/16⌉ × ⌈lado/16⌉ | un hilo por celda de salida, en bloques cuadrados como la cuadrícula | 6 | 21 | 0 B | 100 % | hilos |
| Mapa de calor de demanda | `smoothGlobalKernel` | 16 × 16 = 256 | ⌈lado/16⌉ × ⌈lado/16⌉ | la misma malla, para comparar solo dónde están los pesos | 6 | 26 | 0 B | 100 % | hilos |
| Campo de cobertura de las motos libres | `sweepBasicKernel` | 16 × 16 = 256 | ⌈lado/16⌉ × ⌈lado/16⌉ | un hilo por celda | 6 | 22 | 0 B | 100 % | hilos |
| Campo de cobertura de las motos libres | `sweepTiledKernel` | 16 × 16 = 256 | ⌈lado/14⌉ × ⌈lado/14⌉ | cada bloque carga un tile de 16 × 16 y calcula las 14 × 14 de adentro | 6 | 14 | 1024 B | 100 % | hilos |
| Campo de cobertura de las motos libres | `countChangedKernel` | 256 = 256 | ⌈celdas/16 384⌉ | 64 celdas por hilo y un árbol de 256 lugares para reducir | 6 | 36 | 1024 B | 100 % | hilos |
| Indicadores de entrega | `kpiReduceKernel` | 256 = 256 | ⌈entregas/16 384⌉ | 64 entregas por hilo y un árbol de 256 lugares | 6 | 20 | 4096 B | 100 % | hilos |
| Indicadores de entrega | `kpiCombineKernel` | 256 = 256 | ⌈parciales/16 384⌉ | la misma reducción sobre los parciales de los bloques | 6 | 36 | 4096 B | 100 % | hilos |
| Curva de pedidos en espera | `koggeStoneSectionKernel` | 256 = 256 | ⌈segundos/256⌉ | una sección de 256 valores por bloque, en shared memory | 6 | 14 | 1024 B | 100 % | hilos |
| Curva de pedidos en espera | `brentKungSectionKernel` | 256 = 256 | ⌈segundos/256⌉ | la misma malla, para comparar solo el esquema del scan | 6 | 20 | 1024 B | 100 % | hilos |
| Curva de pedidos en espera | `addPreviousSectionsKernel` | 256 = 256 | ⌈segundos/256⌉ | un hilo por valor, con la misma malla que el scan | 6 | 10 | 0 B | 100 % | hilos |
| Pedidos por restaurante y franja horaria | `slotsAtomicKernel` | 256 = 256 | ⌈pedidos/16 384⌉ | 64 pedidos por hilo, leídos intercalados | 6 | 18 | 0 B | 100 % | hilos |
| Pedidos por restaurante y franja horaria | `slotsAggregatedKernel` | 256 = 256 | ⌈pedidos/16 384⌉ | 64 pedidos seguidos por hilo, para poder juntar repetidos | 6 | 21 | 0 B | 100 % | hilos |
| Tendencia de la demanda | `trendGlobalKernel` | 256 = 256 | ⌈minutos/256⌉ | un hilo por minuto de salida | 6 | 19 | 0 B | 100 % | hilos |
| Tendencia de la demanda | `trendTiledKernel` | 256 = 256 | ⌈minutos/256⌉ | cada bloque carga sus 256 valores y un borde de 7 a cada lado | 6 | 28 | 1080 B | 100 % | hilos |
| Campo de respuesta con motos ocupadas | `sweepManyKernel` | 16 × 16 = 256 | ⌈lado/16⌉ × ⌈lado/16⌉ | cada bloque carga un tile de 32 × 32 y entrega las 16 × 16 de adentro | 6 | 40 | 9216 B | 100 % | hilos |
| Restaurante mejor ubicado | `throughTiledKernel` | 16 × 16 = 256 | ⌈n/16⌉ × ⌈n/16⌉ | un hilo por par de esquinas; dos tiles de 16 × 16 en shared memory por fase | 6 | 38 | 2048 B | 100 % | hilos |
| Restaurante mejor ubicado | `throughGlobalKernel` | 16 × 16 = 256 | ⌈n/16⌉ × ⌈n/16⌉ | la misma malla, para comparar solo el uso de tiles | 6 | 40 | 0 B | 100 % | hilos |
| Mejor esquina para un restaurante nuevo | `reachTurnedKernel` | 256 = 256 | un bloque por esquina de salida | 256 hilos recorren la fila y un árbol reduce el conteo | 6 | 37 | 1024 B | 100 % | hilos |
| Mejor esquina para un restaurante nuevo | `reachColumnKernel` | 256 = 256 | un bloque por esquina de salida | la misma malla, para comparar solo cómo se lee la vuelta | 6 | 40 | 1024 B | 100 % | hilos |
