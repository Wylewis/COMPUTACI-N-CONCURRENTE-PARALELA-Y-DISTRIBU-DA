# Efecto en la simulación completa

Seis corridas de una hora simulada de `config/ciudad.json` en la máquina de la GPU, sin ventana: tres con el estimador `table` (tabla de tiempos en CPU, un pedido por despachador) y tres con el estimador `gpu` (rondas en la tarjeta). Las medidas de cada corrida salen de su log con `tools/gpu_sim_metrics.py` y de la línea final del programa.

```sh
./delivery_sim ciudad_1h.json --headless --backend cpu --estimator table --log table.log
./delivery_sim ciudad_1h.json --headless --backend gpu --estimator gpu --log gpu.log
python3 tools/gpu_sim_metrics.py table.log
python3 tools/check_log.py table.log --summary resumen.json --config ciudad_1h.json
```

`ciudad_1h.json` es `config/ciudad.json` con `durationS` en 3600.

| Estimador | Corrida | Segundos reales | Asignados por hora | Entregados por hora | Demora de despacho, mediana (ms simulados) | Demora de despacho, p90 | Tiempo de entrega, mediana | Reintentos | Estimaciones tardías | Verificador del log |
|---|---|---|---|---|---|---|---|---|---|---|
| `table` | 1 | 61,22 | 13 936 | 4 973 | 2 448 | 574 070 | 2 279 929 | 136 928 | 0 | 1 línea observada |
| `gpu` | 1 | 62,24 | 14 717 | 5 709 | 861 | 521 446 | 2 057 418 | 631 098 | 0 | sin errores |
| `table` | 2 | 61,17 | 13 962 | 4 993 | 2 472 | 572 980 | 2 274 565 | 137 347 | 0 | 1 línea observada |
| `gpu` | 2 | 62,21 | 14 662 | 5 663 | 833 | 526 424 | 2 070 942 | 631 729 | 0 | sin errores |
| `table` | 3 | 61,14 | 13 993 | 5 033 | 2 463 | 574 975 | 2 272 791 | 140 773 | 0 | 1 línea observada |
| `gpu` | 3 | 62,21 | 14 612 | 5 610 | 945 | 523 042 | 2 072 454 | 633 576 | 0 | sin errores |

La línea que observa el verificador en las corridas con `table` es una moto con 4 pedidos y capacidad 3. La mochila nunca pasa de 3: al entregar, la moto libera el lugar antes de escribir el evento de entrega, y con la flota saturada otro despachador alcanza a tomar ese lugar y escribir su evento de asignación primero. Es el orden de dos eventos en el log, no un pedido de más.
