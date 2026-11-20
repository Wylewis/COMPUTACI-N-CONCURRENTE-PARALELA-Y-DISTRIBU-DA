# demand_heatmap: CPU contra GPU

Generado por `tools/gpu_report.py` a partir de `mac.csv` y `pc.csv`. 10 repeticiones medidas por versión y tamaño, después de una de calentamiento.

### Veredicto: la GPU contra la CPU de la laptop

| Tamaño | CPU 1 hilo (ms) | CPU 18 hilos (ms) | GPU kernel (ms) | GPU punta a punta (ms) | Mejor CPU | Speedup de la GPU contra la mejor CPU (kernel / punta a punta) | ¿Correcto? |
|---|---|---|---|---|---|---|---|
| 64x64 | 0.037 (0.036–0.038) | 0.046 (0.040–0.067) | 0.077 (0.009–0.122) | 0.966 (0.722–1.197) | 0.024 (4 hilos) | 0.31x / 0.02x | sí |
| 128x128 | 0.160 (0.148–0.173) | 0.082 (0.063–0.091) | 0.019 (0.009–0.121) | 0.847 (0.663–1.024) | 0.053 (4 hilos) | 2.78x / 0.06x | sí |
| 256x256 | 0.580 (0.572–0.612) | 0.106 (0.101–0.116) | 0.025 (0.010–0.108) | 1.014 (0.726–1.208) | 0.106 (18 hilos) | 4.24x / 0.10x | sí |
| 512x512 | 2.293 (2.264–2.344) | 0.308 (0.288–0.383) | 0.034 (0.022–0.103) | 1.113 (1.044–1.520) | 0.308 (18 hilos) | 9.12x / 0.28x | sí |
| 1024x1024 | 9.400 (9.238–9.561) | 0.991 (0.814–1.106) | 0.062 (0.057–0.096) | 2.733 (2.517–3.148) | 0.970 (16 hilos) | 15.69x / 0.36x | sí |
| 2048x2048 | 37.946 (37.777–38.107) | 3.800 (3.733–4.316) | 0.275 (0.266–0.321) | 6.737 (6.615–6.947) | 3.800 (18 hilos) | 13.81x / 0.56x | sí |
| 4096x4096 | 174.671 (173.278–177.440) | 14.580 (14.075–22.212) | 1.383 (0.974–1.812) | 23.072 (22.580–28.790) | 14.580 (18 hilos) | 10.54x / 0.63x | sí |

Tiempos de CPU: MacBook Pro M5 Pro. Mediana de las repeticiones y, entre paréntesis, mínimo y máximo.

Cruce de punta a punta contra la mejor CPU de la laptop: **no hay cruce en el rango medido**; la CPU gana en todos los tamaños.

### Misma máquina: la GPU contra la CPU de su propia máquina

| Tamaño | CPU 1 hilo (ms) | CPU 16 hilos (ms) | GPU kernel (ms) | GPU punta a punta (ms) | Mejor CPU | Speedup de la GPU contra la mejor CPU (kernel / punta a punta) | ¿Correcto? |
|---|---|---|---|---|---|---|---|
| 64x64 | 0.107 (0.105–0.110) | 0.336 (0.305–0.374) | 0.077 (0.009–0.122) | 0.966 (0.722–1.197) | 0.107 (1 hilo) | 1.39x / 0.11x | sí |
| 128x128 | 0.443 (0.426–0.485) | 0.390 (0.342–0.443) | 0.019 (0.009–0.121) | 0.847 (0.663–1.024) | 0.226 (8 hilos) | 11.95x / 0.27x | sí |
| 256x256 | 1.766 (1.739–1.839) | 0.475 (0.429–0.525) | 0.025 (0.010–0.108) | 1.014 (0.726–1.208) | 0.475 (16 hilos) | 18.95x / 0.47x | sí |
| 512x512 | 7.157 (7.105–7.205) | 1.191 (1.159–1.537) | 0.034 (0.022–0.103) | 1.113 (1.044–1.520) | 1.191 (16 hilos) | 35.32x / 1.07x | sí |
| 1024x1024 | 28.718 (28.528–30.800) | 4.704 (3.924–4.987) | 0.062 (0.057–0.096) | 2.733 (2.517–3.148) | 4.704 (16 hilos) | 76.03x / 1.72x | sí |
| 2048x2048 | 115.834 (114.602–116.900) | 17.014 (16.587–20.329) | 0.275 (0.266–0.321) | 6.737 (6.615–6.947) | 17.014 (16 hilos) | 61.83x / 2.53x | sí |
| 4096x4096 | 510.084 (496.301–521.424) | 109.317 (96.942–127.672) | 1.383 (0.974–1.812) | 23.072 (22.580–28.790) | 109.317 (16 hilos) | 79.05x / 4.74x | sí |

Tiempos de CPU: PC Ryzen 7 3700X + RTX 3060 Ti. Mediana de las repeticiones y, entre paréntesis, mínimo y máximo.

Cruce de punta a punta contra la mejor CPU de la misma máquina: **512x512** (262144 elementos).

### Variantes de GPU

Mediana en ms de cada variante medida en la misma corrida.

| Tamaño | `gpu_kernel` | `gpu_e2e` | `gpu_e2e_globalweights` | `gpu_kernel_globalweights` |
|---|---|---|---|---|
| 64x64 | 0.077 | 0.966 | 0.912 | 0.053 |
| 128x128 | 0.019 | 0.847 | 0.855 | 0.049 |
| 256x256 | 0.025 | 1.014 | 0.883 | 0.058 |
| 512x512 | 0.034 | 1.113 | 1.585 | 0.070 |
| 1024x1024 | 0.062 | 2.733 | 3.000 | 0.106 |
| 2048x2048 | 0.275 | 6.737 | 7.165 | 0.443 |
| 4096x4096 | 1.383 | 23.072 | 25.853 | 2.264 |

### Barrido de hilos, MacBook Pro M5 Pro

Speedup de la versión multihilo contra ella misma con 1 hilo.

| Tamaño | 1 hilo | 2 hilos | 4 hilos | 8 hilos | 16 hilos | 18 hilos |
|---|---|---|---|---|---|---|
| 64x64 | 1.00x | 1.52x | 2.09x | 1.29x | 0.99x | 1.07x |
| 128x128 | 1.00x | 1.66x | 2.93x | 2.69x | 1.85x | 1.88x |
| 256x256 | 1.00x | 1.85x | 3.41x | 3.65x | 4.16x | 5.50x |
| 512x512 | 1.00x | 1.88x | 3.66x | 4.03x | 6.87x | 7.45x |
| 1024x1024 | 1.00x | 1.93x | 3.80x | 5.19x | 9.68x | 9.48x |
| 2048x2048 | 1.00x | 1.91x | 3.76x | 5.27x | 8.99x | 9.84x |
| 4096x4096 | 1.00x | 1.91x | 3.77x | 6.62x | 11.03x | 11.80x |

### Barrido de hilos, PC Ryzen 7 3700X + RTX 3060 Ti

Speedup de la versión multihilo contra ella misma con 1 hilo.

| Tamaño | 1 hilo | 2 hilos | 4 hilos | 8 hilos | 16 hilos |
|---|---|---|---|---|---|
| 64x64 | 1.00x | 1.40x | 1.60x | 0.74x | 0.52x |
| 128x128 | 1.00x | 1.85x | 2.14x | 2.26x | 1.31x |
| 256x256 | 1.00x | 1.92x | 3.13x | 3.69x | 3.94x |
| 512x512 | 1.00x | 1.94x | 3.68x | 4.88x | 6.03x |
| 1024x1024 | 1.00x | 1.95x | 3.81x | 5.02x | 6.08x |
| 2048x2048 | 1.00x | 1.95x | 3.69x | 5.00x | 6.73x |
| 4096x4096 | 1.00x | 1.79x | 2.31x | 3.62x | 4.66x |

### Gráficos

![Tiempo contra tamaño](tiempo.png)

![Reparto del tiempo de GPU](copias.png)

![Barrido de hilos](hilos.png)

