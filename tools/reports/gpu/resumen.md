# Resumen de las aplicaciones de GPU

Generado por `tools/gpu_summary.py` a partir del `mac.csv` y el `pc.csv` de cada carpeta. Los speedups son contra la mejor versión de CPU en cada tamaño; mayor que 1 significa que gana la GPU.

| N.º | Concepto | Aplicación del concepto | Aplicación | Cruce de punta a punta contra la laptop | Mejor punta a punta contra la laptop | Mejor kernel contra la laptop | Cruce en la misma máquina | Mejor punta a punta en la misma máquina | Veredicto | Puntos |
|---|---|---|---|---|---|---|---|---|---|---|
| 1 | 03, memory tiling | 1.ª | Tabla de tiempos | no hay | 0,16x | 0,26x | no hay | 0,34x | CPU: el método de la GPU hace más trabajo que Dijkstra | 1 |
| 2 | 03, memory tiling | 2.ª | Restaurante mejor ubicado | 512 × 512 | 5,80x | 13,36x | 256 × 256 | 10,01x | GPU con la red de la ciudad; CPU con la del enunciado | 1 |
| 3 | 04, performance | 1.ª | Estimación de entrega por lote | no hay | 0,26x | 1,74x | 500 × 2000 | 2,75x | CPU: bajar la matriz cuesta más que calcularla | 1 |
| 4 | 04, performance | 2.ª | Mejor esquina para un restaurante nuevo | no hay | 0,03x | 1,61x | no hay | 0,23x | CPU, con la tabla girada | 1 |
| 5 | 05, convolución | 1.ª | Mapa de calor de demanda | no hay | 0,47x | 15,05x | 512 × 512 | 1,98x | CPU: la cuadrícula del programa es chica y las copias pesan más | 1 |
| 6 | 05, convolución | 2.ª | Tendencia de la demanda | no hay | 0,05x | 3,08x | no hay | 0,79x | CPU: el resultado es tan grande como la entrada | 1 |
| 7 | 06, stencil | 1.ª | Campo de cobertura | no hay | 0,87x | 1,94x | 256 × 256 | 8,14x | CPU en el programa; GPU en la misma máquina desde 256 × 256 | 1 |
| 8 | 06, stencil | 2.ª | Campo de respuesta con motos ocupadas | no hay | 0,85x | 1,90x | 256 × 256 | 7,38x | CPU en el programa; GPU en la misma máquina desde 256 × 256 | 1 |
| 9 | 07, histograma | 1.ª | Histograma de tiempos de entrega | no hay | 0,07x | 3,38x | no hay | 0,94x | CPU: subir los datos cuesta más que contarlos | 1 |
| 10 | 07, histograma | 2.ª | Pedidos por restaurante y franja horaria | no hay | 0,07x | 3,98x | no hay | 0,44x | CPU: subir los datos cuesta más que contarlos | 1 |
| 11 | 08, reducción | 1.ª | Mejor moto de cada pedido | no hay | 0,07x | 2,34x | no hay | 0,68x | CPU sola; GPU si va encadenada a la estimación | 1 |
| 12 | 08, reducción | 2.ª | Indicadores de entrega | no hay | 0,03x | 1,69x | no hay | 0,23x | CPU: un hilo ya va muy rápido y bajan tres números | 1 |
| 13 | 09, scan | 1.ª | Pedidos agrupados por restaurante | no hay | 0,26x | 1,63x | 4000000 × 200000 | 2,59x | Agrupar sí, en CPU: la ronda es chica para la tarjeta | 1 |
| 14 | 09, scan | 2.ª | Curva de pedidos en espera | no hay | 0,03x | 0,85x | no hay | 0,80x | CPU: poco cálculo y todo lo que sube tiene que bajar | 1 |

Puntos: 14, dos aplicaciones en cada uno de siete conceptos.
