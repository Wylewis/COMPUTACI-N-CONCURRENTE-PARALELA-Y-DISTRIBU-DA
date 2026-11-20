# group_orders: CPU contra GPU

Generado por `tools/gpu_report.py` a partir de `mac.csv` y `pc.csv`. 10 repeticiones medidas por versión y tamaño, después de una de calentamiento.

### Veredicto: la GPU contra la CPU de la laptop

| Tamaño | CPU 1 hilo (ms) | CPU 18 hilos (ms) | GPU kernel (ms) | GPU punta a punta (ms) | Mejor CPU | Speedup de la GPU contra la mejor CPU (kernel / punta a punta) | ¿Correcto? |
|---|---|---|---|---|---|---|---|
| 400x20 | 0.001 (0.001–0.001) | 0.156 (0.134–0.170) | 0.065 (0.046–0.084) | 0.671 (0.611–0.851) | 0.001 (1 hilo) | 0.01x / 0.00x | sí |
| 4000x200 | 0.003 (0.003–0.005) | 0.199 (0.182–0.213) | 0.054 (0.042–0.065) | 0.629 (0.620–0.645) | 0.003 (1 hilo) | 0.05x / 0.00x | sí |
| 40000x2000 | 0.028 (0.026–0.028) | 0.220 (0.201–0.264) | 0.080 (0.075–0.098) | 0.711 (0.684–0.785) | 0.028 (1 hilo) | 0.34x / 0.04x | sí |
| 400000x20000 | 0.397 (0.390–0.424) | 0.372 (0.350–0.392) | 0.140 (0.115–0.153) | 1.508 (1.441–1.978) | 0.244 (4 hilos) | 1.75x / 0.16x | sí |
| 4000000x200000 | 7.005 (6.845–7.195) | 2.350 (2.280–2.420) | 1.662 (1.628–1.713) | 8.595 (8.301–8.982) | 2.217 (8 hilos) | 1.33x / 0.26x | sí |
| 16000000x800000 | 64.542 (63.429–72.544) | 23.637 (23.013–29.488) | 15.821 (15.794–15.881) | 38.048 (37.677–39.576) | 21.177 (8 hilos) | 1.34x / 0.56x | sí |

Tiempos de CPU: MacBook Pro M5 Pro. Mediana de las repeticiones y, entre paréntesis, mínimo y máximo.

Cruce de punta a punta contra la mejor CPU de la laptop: **no hay cruce en el rango medido**; la CPU gana en todos los tamaños.

### Misma máquina: la GPU contra la CPU de su propia máquina

| Tamaño | CPU 1 hilo (ms) | CPU 16 hilos (ms) | GPU kernel (ms) | GPU punta a punta (ms) | Mejor CPU | Speedup de la GPU contra la mejor CPU (kernel / punta a punta) | ¿Correcto? |
|---|---|---|---|---|---|---|---|
| 400x20 | 0.002 (0.002–0.003) | 1.150 (1.118–1.192) | 0.065 (0.046–0.084) | 0.671 (0.611–0.851) | 0.002 (1 hilo) | 0.03x / 0.00x | sí |
| 4000x200 | 0.007 (0.006–0.007) | 1.480 (1.455–1.510) | 0.054 (0.042–0.065) | 0.629 (0.620–0.645) | 0.007 (1 hilo) | 0.12x / 0.01x | sí |
| 40000x2000 | 0.067 (0.062–0.090) | 1.487 (1.466–1.518) | 0.080 (0.075–0.098) | 0.711 (0.684–0.785) | 0.067 (1 hilo) | 0.84x / 0.09x | sí |
| 400000x20000 | 2.086 (1.247–2.461) | 2.245 (2.125–2.746) | 0.140 (0.115–0.153) | 1.508 (1.441–1.978) | 1.008 (2 hilos) | 7.21x / 0.67x | sí |
| 4000000x200000 | 66.470 (65.275–69.717) | 17.001 (15.537–18.248) | 1.662 (1.628–1.713) | 8.595 (8.301–8.982) | 17.001 (16 hilos) | 10.23x / 1.98x | sí |
| 16000000x800000 | 495.813 (481.645–511.291) | 265.969 (261.984–287.373) | 15.821 (15.794–15.881) | 38.048 (37.677–39.576) | 213.139 (8 hilos) | 13.47x / 5.60x | sí |

Tiempos de CPU: PC Ryzen 7 3700X + RTX 3060 Ti. Mediana de las repeticiones y, entre paréntesis, mínimo y máximo.

Cruce de punta a punta contra la mejor CPU de la misma máquina: **4000000x200000** (4000000 elementos).

### Variantes de GPU

Mediana en ms de cada variante medida en la misma corrida.

| Tamaño | `gpu_kernel` | `gpu_e2e` | `gpu_e2e_scan` | `gpu_kernel_scan` |
|---|---|---|---|---|
| 400x20 | 0.065 | 0.671 | 0.616 | 0.042 |
| 4000x200 | 0.054 | 0.629 | 0.546 | 0.040 |
| 40000x2000 | 0.080 | 0.711 | 0.585 | 0.077 |
| 400000x20000 | 0.140 | 1.508 | 0.673 | 0.113 |
| 4000000x200000 | 1.662 | 8.595 | 0.906 | 0.153 |
| 16000000x800000 | 15.821 | 38.048 | 2.592 | 0.682 |

### Barrido de hilos, MacBook Pro M5 Pro

Speedup de la versión multihilo contra ella misma con 1 hilo.

| Tamaño | 1 hilo | 2 hilos | 4 hilos | 8 hilos | 16 hilos | 18 hilos |
|---|---|---|---|---|---|---|
| 400x20 | 1.00x | 0.80x | 0.62x | 0.29x | 0.19x | 0.18x |
| 4000x200 | 1.00x | 0.76x | 0.60x | 0.30x | 0.15x | 0.15x |
| 40000x2000 | 1.00x | 1.05x | 0.94x | 0.56x | 0.33x | 0.32x |
| 400000x20000 | 1.00x | 1.70x | 2.51x | 1.80x | 1.60x | 1.64x |
| 4000000x200000 | 1.00x | 1.89x | 3.46x | 4.17x | 4.10x | 3.93x |
| 16000000x800000 | 1.00x | 1.79x | 2.73x | 3.49x | 3.29x | 3.12x |

### Barrido de hilos, PC Ryzen 7 3700X + RTX 3060 Ti

Speedup de la versión multihilo contra ella misma con 1 hilo.

| Tamaño | 1 hilo | 2 hilos | 4 hilos | 8 hilos | 16 hilos |
|---|---|---|---|---|---|
| 400x20 | 1.00x | 0.97x | 0.55x | 0.36x | 0.25x |
| 4000x200 | 1.00x | 0.99x | 0.56x | 0.34x | 0.20x |
| 40000x2000 | 1.00x | 1.07x | 0.70x | 0.43x | 0.25x |
| 400000x20000 | 1.00x | 2.46x | 1.82x | 1.54x | 1.10x |
| 4000000x200000 | 1.00x | 1.97x | 3.64x | 4.16x | 4.64x |
| 16000000x800000 | 1.00x | 1.63x | 2.39x | 3.85x | 3.08x |

### Gráficos

![Tiempo contra tamaño](agrupar_tiempo.png)

![Reparto del tiempo de GPU](agrupar_copias.png)

![Barrido de hilos](agrupar_hilos.png)

