# Experimento de despacho: serie, una tarea por candidato y pool

Fecha: 2026-10-19. Máquina: Apple M5 Pro, 18 hilos, Apple clang 21, compilación `-O2`.
Ciudad real de Equipetrol (320 nodos, 630 aristas). Commit base: `60a3ef5`.

## Pregunta

El despachador calcula una estimación de entrega por candidato en cada anillo (map) y se queda con la
mejor (reduce). ¿Conviene calcular ese map en serie, con una tarea `std::async` por candidato, o
enviando las tareas al pool de estimación? ¿Dónde empieza a pagar el paralelismo?

## 1. El map aislado (`dispatch_bench`)

Comando: `./build/dispatch_bench config/equipetrol.json 300`. Tiempo por map en microsegundos, media
de 300 repeticiones por celda. La sobrecarga por tarea se despeja del modelo
`T_par = T_serie / hilos + n · sobrecarga` con `hilos = min(n, 18)`.

| candidatos | serie (us) | async (us) | pool (us) | speedup async | speedup pool | por estimacion (us) | sobrecarga async por tarea (us) | sobrecarga pool por tarea (us) |
|---|---|---|---|---|---|---|---|---|
| 1 | 4.06083 | 34.2354 | 14.8665 | 0.118615 | 0.273153 | 4.06083 | 30.1746 | 10.8057 |
| 8 | 25.6918 | 89.2024 | 39.1001 | 0.288017 | 0.657077 | 3.21148 | 10.7489 | 4.48608 |
| 32 | 66.0622 | 208.754 | 107.754 | 0.316459 | 0.613084 | 2.06444 | 6.40888 | 3.25262 |
| 128 | 299.254 | 762.728 | 372.545 | 0.392347 | 0.803271 | 2.33792 | 5.82893 | 2.78062 |
| 512 | 1217.2 | 2974.44 | 1491.98 | 0.409219 | 0.815829 | 2.37734 | 5.67738 | 2.78194 |

Estimaciones caras (cada una repetida k veces), anillo de 8 candidatos:

| k | por estimacion (us) | serie (us) | async (us) | pool (us) | speedup async | speedup pool |
|---|---|---|---|---|---|---|
| 1 | 1.98194 | 15.8556 | 74.0725 | 33.6967 | 0.214055 | 0.470538 |
| 4 | 6.00226 | 48.0181 | 76.0899 | 49.216 | 0.63107 | 0.97566 |
| 16 | 22.0684 | 176.548 | 140.107 | 115.127 | 1.26009 | 1.5335 |
| 64 | 85.9517 | 687.614 | 387.949 | 363.915 | 1.77243 | 1.88949 |

pool de 18 hilos (hardware: 18), 300 repeticiones por celda, ciudad de 320 nodos

Lectura:

- Una estimación (un Dijkstra sobre 320 nodos más la aritmética de tiempos) cuesta entre 2 y 4 µs.
- Crear un hilo con `std::async` y recoger su `future` cuesta unos 30 µs para una tarea sola y se
  amortiza hasta unos 6 µs por tarea en anillos grandes; con el pool la sobrecarga baja a unos 3 µs
  por tarea, porque no se crean hilos, pero sigue siendo del orden del trabajo que reparte.
- Por eso ninguna variante paralela supera a la serie con esta ciudad: el speedup del pool crece
  con el tamaño del anillo (0,28 con 1 candidato, 0,84 con 512) pero no llega a 1.
- Con estimaciones artificialmente caras el cruce aparece: a partir de unos 6 µs por estimación el
  pool empata con la serie, y con 22 µs ya gana 1,5x y con 86 µs, 1,9x. `async` cruza más tarde
  porque su sobrecarga es el doble.

## 2. La simulación completa (`run_experiments.py`)

Comando: `python3 tools/run_experiments.py --bin build/delivery_sim --config config/equipetrol.json
--vary dispatch.estimator=serial,async,pool --seeds 42,7 --duration 1800 --scale 600 --jobs 3`.
Treinta minutos simulados a 600x, config de ejemplo, medias de dos semillas, logs verificados.

| configuracion | entregados | rechazados | pendientes | entrega media (min) | averias | reasignados | reintentos | tardias | tiempo real (s) | corridas ok |
|---|---|---|---|---|---|---|---|---|---|---|
| dispatch.estimator=serial | 17 | 114 | 77 | 20.9 | 0 | 0 | 74 | 0 | 3.0 | 2/2 |
| dispatch.estimator=async | 17 | 113 | 78 | 20.8 | 0 | 0 | 135 | 0 | 3.0 | 2/2 |
| dispatch.estimator=pool | 17 | 111.5 | 74.5 | 20.5 | 0 | 0 | 141.5 | 0 | 3.0 | 2/2 |

Lectura: las tres variantes entregan lo mismo (el reduce elige la misma moto; las estimaciones son
idénticas) y el tiempo real es el mismo porque lo fija el reloj simulado. La única diferencia
visible en el sistema es el número de reintentos del despachador: con el map más lento (`async`,
`pool`) pasa más tiempo entre la copia de la flota y la reserva, y otro despachador gana la moto
más veces. No hubo estimaciones tardías con el plazo de 200 ms en ninguna variante.

## 3. Amdahl con sobrecarga

El map es casi totalmente paralelizable: `n` estimaciones independientes y un reduce trivial, así que
la ley de Amdahl a secas (`S = 1 / ((1 - p) + p / N)` con `p ≈ 1`) predice un speedup cercano al
número de hilos. Lo observado es menor que 1. La diferencia es la sobrecarga fija por tarea `o`:

    T_serie = n · c                       (c = costo de una estimación)
    T_par   = n · c / N + n · o            (N hilos efectivos, o = sobrecarga por tarea)
    S       = c / (c / N + o)

Con `N = 18`: el paralelismo paga (`S > 1`) solo si `c > o · N / (N - 1) ≈ 1,06 · o`, es decir, cuando
una estimación cuesta más que la sobrecarga de repartirla. Con `o ≈ 3 µs` del pool y `c ≈ 2 a 4 µs`
de esta ciudad, estamos justo debajo del cruce, que es lo que mide la tabla 1; con `c ≈ 6 µs`
(`k = 4`) el pool empata y con `c ≈ 22 µs` gana, que es lo que mide la tabla de estimaciones caras.
El techo de la fórmula con `c = 86 µs` sería `S ≈ 86 / (86 / 8 + 3) ≈ 6` para 8 candidatos; se
observa 1,9 porque despertar a ocho trabajadores dormidos y recoger ocho futures no es simultáneo.

## 4. El cambio de diseño: de `async` a pool, antes y después

| | Antes (PR 31, `std::async` por candidato) | Después (PR 32, pool) |
|---|---|---|
| Map de 8 candidatos | 91 µs | 39 µs (2,4x menos) |
| Sobrecarga por tarea, anillos grandes | 6 µs | 3 µs |
| Estimación que se cuelga | el `future` de `async` se une al destruirse: el despachador espera a que termine | el `future` del pool no se une: la llamada vuelve al vencer `quoteTimeoutMs` |
| Hilos creados por map | uno por candidato | ninguno |
| Resultado de la simulación | igual | igual |

La medición del PR 31 motivó el cambio y el PR 32 lo verificó con la prueba de la tarea tardía; este
experimento lo repite sobre más tamaños y sobre la corrida completa.

## 5. Conclusión

Para la ciudad de Equipetrol la serie es la variante más rápida, y lo sería hasta unas 500
estimaciones por anillo. El pool se mantiene como diseño porque la sección 7 del enunciado exige
estimaciones concurrentes con plazo, porque acota la espera del despachador ante una estimación
colgada (cosa que la serie no puede hacer), y porque gana en cuanto la estimación es cara: una ciudad
diez veces mayor, o una estimación que considere el tráfico, cruzan el umbral. Es la misma lección
que la tabla de distancias (10x con 320 orígenes, 0,5x con 9): el paralelismo paga cuando cada tarea
trabaja más de lo que cuesta repartirla.
