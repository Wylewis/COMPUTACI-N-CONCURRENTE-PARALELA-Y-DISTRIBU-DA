# Experimentos del sistema: tabla de distancias, puestos, mochila y averías

Fecha: 2026-10-19. Máquina: Apple M5 Pro, 18 hilos, Apple clang 21, `-O2`. Ciudad real de
Equipetrol (320 nodos, 630 aristas). Commit base: `b1404bc`.

## 1. Tabla de distancias: serie frente a paralela (`distance_bench`)

Comando: `./build/distance_bench config/equipetrol.json 5`. Cada fila es una tabla completa
construida 5 veces; la paralela reparte los orígenes entre `tareas` lanzadas con `std::async`
(`defaultTaskCount`: tantas como orígenes, hasta el número de hilos del hardware).

| origenes | serie (ms) | paralelo (ms) | tareas | speedup | por origen en serie (us) |
|---|---|---|---|---|---|
| 9 | 0.0932084 | 0.1136 | 9 | 0.820496 | 10.3565 |
| 20 | 0.281367 | 0.193508 | 18 | 1.45403 | 14.0683 |
| 40 | 0.485725 | 0.239408 | 18 | 2.02886 | 12.1431 |
| 80 | 1.01768 | 0.283608 | 18 | 3.58834 | 12.721 |
| 160 | 1.95026 | 0.321292 | 18 | 6.07006 | 12.1891 |
| 320 | 3.08639 | 0.399725 | 18 | 7.72129 | 9.64497 |

Barrido de tareas con los 320 nodos como origenes:

| tareas | paralelo (ms) | speedup frente a la serie |
|---|---|---|
| 1 | 2.56749 | 1.05324 |
| 2 | 1.40657 | 1.92254 |
| 4 | 0.781983 | 3.4581 |
| 8 | 0.424558 | 6.36939 |
| 12 | 0.316 | 8.55752 |
| 18 | 0.303292 | 8.91609 |
| 36 | 0.384925 | 7.0252 |

hardware: 18 hilos; 5 repeticiones por celda; ciudad de 320 nodos

Lectura:

- Un origen (un Dijkstra uno-a-todos sobre 320 nodos) cuesta unos 10 a 14 µs. Con 9 orígenes (los
  restaurantes) la tabla en serie tarda 0,09 ms y la paralela 0,11: repartir 9 tareas de 10 µs cuesta
  más que hacerlas, speedup 0,82. Es la misma conclusión que el experimento de despacho.
- Desde 20 orígenes el paralelismo paga (1,45x) y crece casi linealmente con el tamaño: 7,7x con
  los 320 nodos y 18 tareas.
- El barrido de tareas con 320 orígenes muestra el techo: de 1 a 8 tareas el speedup sigue al
  número de tareas (1, 1,9, 3,5, 6,4); con 12 y 18 se aplana (8,6 y 8,9) porque los 18 hilos del
  M5 Pro no son 18 núcleos de rendimiento iguales y el reparto de 320 filas entre 18 deja colas
  desiguales; con 36 tareas, más que hilos, baja (7,0) por el costo de crear y unir hilos de más.
  Por eso `defaultTaskCount` no pasa de `hardware_concurrency`.
- En el programa la tabla se construye una sola vez al arrancar con los 9 restaurantes: la versión
  paralela no aporta ahí, y así se dice en el informe; el módulo existe porque el enunciado pide
  el patrón fan-out/fan-in y porque con ciudades grandes sí cambia el arranque.

## 2. Puestos de recogida (`restaurants.*.pickupSlots`)

Comando: `python3 tools/run_experiments.py --bin build/delivery_sim --config config/equipetrol.json
--vary 'restaurants.*.pickupSlots=1,2,3,4' --seeds 42,7 --duration 3600 --scale 1200 --jobs 3`.
Una hora simulada, configuración de ejemplo, medias de dos semillas, logs verificados.

| configuracion | entregados | rechazados | pendientes | entrega media (min) | averias | reasignados | reintentos | tardias | tiempo real (s) | corridas ok |
|---|---|---|---|---|---|---|---|---|---|---|
| restaurants.*.pickupSlots=1 | 56 | 294 | 58 | 22.8 | 0.5 | 1 | 432.5 | 0 | 3.0 | 2/2 |
| restaurants.*.pickupSlots=2 | 52.5 | 296 | 59.5 | 22.9 | 1 | 1 | 408 | 0 | 3.0 | 2/2 |
| restaurants.*.pickupSlots=3 | 51 | 293 | 64 | 23.0 | 0.5 | 1 | 407 | 0 | 3.0 | 2/2 |
| restaurants.*.pickupSlots=4 | 55 | 291.5 | 61.5 | 22.7 | 0.5 | 1 | 423.5 | 0 | 3.0 | 2/2 |

Lectura: los puestos no cambian nada medible (56, 52, 51, 55 entregas son ruido entre semillas).
Con 8 motos para 9 restaurantes casi nunca hay dos motos en la misma fila, así que el recurso
escaso es la flota, no los mostradores. El monitor del restaurante importa para la corrección (la
prueba de estrés lo satura con 24 motos y 2 restaurantes), no para el rendimiento de este config.

## 3. Capacidad de la mochila (`fleet.bagCapacity`)

| configuracion | entregados | rechazados | pendientes | entrega media (min) | averias | reasignados | reintentos | tardias | tiempo real (s) | corridas ok |
|---|---|---|---|---|---|---|---|---|---|---|
| fleet.bagCapacity=1 | 45.5 | 317.5 | 45 | 15.8 | 0.5 | 0 | 372.5 | 0 | 3.0 | 2/2 |
| fleet.bagCapacity=2 | 52 | 304 | 52 | 20.3 | 0 | 0 | 382 | 0 | 3.0 | 2/2 |
| fleet.bagCapacity=3 | 54.5 | 294 | 59.5 | 22.6 | 0.5 | 0.5 | 415.5 | 0 | 3.0 | 2/2 |
| fleet.bagCapacity=4 | 51.5 | 286 | 70.5 | 25.1 | 1 | 1 | 394.5 | 0 | 3.0 | 2/2 |

Lectura: una mochila más grande entrega algo más y rechaza menos (de 317 a 286 rechazos), pero cada
pedido tarda más: de 15,8 min con mochila 1 a 25,1 con mochila 4. Es la consecuencia directa de la
política de la moto (D29, D36.1): recoge todo lo asignado antes de entregar, y con la mochila llena
el primer pedido espera a que los demás estén preparados. Confirma D40.9: la mejora candidata con
más impacto es entregar lo recogido antes de ir por un pedido cuya preparación aún tarda.

## 4. Probabilidad de avería (`incidents.breakdownProbability`)

| configuracion | entregados | rechazados | pendientes | entrega media (min) | averias | reasignados | reintentos | tardias | tiempo real (s) | corridas ok |
|---|---|---|---|---|---|---|---|---|---|---|
| incidents.breakdownProbability=0 | 56.5 | 289.5 | 62 | 22.3 | 0 | 0 | 441.5 | 0 | 3.0 | 2/2 |
| incidents.breakdownProbability=0.02 | 51 | 296.5 | 60.5 | 23.6 | 0.5 | 1 | 396 | 0 | 3.0 | 2/2 |
| incidents.breakdownProbability=0.1 | 45 | 294.5 | 68.5 | 24.0 | 8.5 | 8.5 | 366 | 0 | 3.0 | 2/2 |
| incidents.breakdownProbability=0.3 | 35.5 | 307 | 65.5 | 28.5 | 29.5 | 28.5 | 378.5 | 0 | 3.0 | 2/2 |

Lectura: el efecto es monótono y el sistema lo absorbe sin romperse: con 30 % de averías por viaje
hay 29 averías y 28 reasignaciones por hora, las entregas bajan de 56 a 35 y el tiempo medio sube
de 22 a 28 minutos. Cada avería cuesta unos 5 minutos de reparación más el viaje de la rescatista
o la nueva asignación; todos los logs pasan el verificador con la liberación por avería.

## 5. Conclusiones

1. La tabla de distancias es el único punto del programa donde el paralelismo de datos da un
   speedup grande (7,7x a 8,9x), y solo con cientos de orígenes; con los 9 del config es neutro.
2. La configuración de ejemplo está limitada por la flota: ni más puestos ni más mochila la
   destraban; la palanca es la política de entrega, no la concurrencia.
3. Las averías degradan de forma proporcional y el traspaso mantiene las cuentas: es la evidencia de
   que la liberación de reservas y el rescate funcionan bajo carga realista, no solo en la prueba.
