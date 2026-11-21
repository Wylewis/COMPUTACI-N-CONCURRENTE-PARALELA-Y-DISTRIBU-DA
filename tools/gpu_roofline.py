#!/usr/bin/env python3
"""Arma la figura de roofline de los kernels de GPU.

Uso:
    python3 tools/gpu_roofline.py --reports tools/reports/gpu --out tools/reports/gpu

Para cada aplicacion toma, del pc.csv de su carpeta, la mediana del tiempo de
kernel en el tamano mas grande medido. La intensidad aritmetica y el trabajo
total salen de contar el codigo de cada kernel:

  ops    operaciones aritmeticas y comparaciones por elemento de trabajo
  bytes  bytes de memoria global que el kernel lee y escribe por elemento
  veces  cuantas veces se recorre cada elemento (pasadas, elevaciones)

  intensidad  = ops / bytes                     operaciones por byte
  rendimiento = ops * elementos * veces / tiempo

Los techos son los nominales de la tarjeta: ancho de banda de memoria y pico
de computo. Escribe roofline.png y roofline.md. Ninguna cifra medida se
escribe a mano: los tiempos salen de los CSV.
Requiere pandas y matplotlib.
"""

import argparse
import os

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import pandas as pd

BANDWIDTH_GB_S = 448.0
PEAK_GOPS = 16200.0
OFFSETS = {4: (-15, 6), 5: (8, 4), 6: (8, -11), 7: (-15, 5), 9: (-13, -13), 10: (8, -10), 11: (9, -5), 12: (-18, 7)}

KERNELS = [
    dict(n=1, name="Tabla de tiempos", folder="travel_table", app="travel_table", version="gpu_kernel",
         ops=8192, bytes=2056, times=9,
         why="256 fases; por fase 2 lecturas de 4 B y 16 sumas con su comparación; 9 elevaciones al cuadrado"),
    dict(n=3, name="Estimación por lote", folder="eta_batch", app="eta_batch", version="gpu_kernel",
         ops=10, bytes=36, times=1,
         why="12 B de la moto, 12 B del pedido, 2 lecturas de la tabla y 4 B de salida; 10 operaciones de enteros"),
    dict(n=11, name="Mejor moto", folder="dispatch_argmin", app="dispatch_argmin", version="gpu_kernel",
         ops=5, bytes=4, times=1,
         why="una lectura de 4 B; rotación, clave y comparación"),
    dict(n=13, name="Agrupado por restaurante", folder="group_orders", app="group_orders", version="gpu_kernel",
         ops=4, bytes=32, times=1,
         why="contar: 4 B y un atómico de 8 B; repartir: 8 B de lectura, un atómico de 8 B y 4 B de salida"),
    dict(n=9, name="Histograma de entregas", folder="delivery_histogram", app="delivery_histogram", version="gpu_kernel",
         ops=4, bytes=4, times=1,
         why="una lectura de 4 B; división, dos comparaciones y una suma en shared memory"),
    dict(n=5, name="Mapa de calor", folder="demand_heatmap", app="demand_heatmap", version="gpu_kernel",
         ops=98, bytes=200, times=1,
         why="49 lecturas de vecinas de 4 B y 4 B de salida; 49 multiplicaciones y 49 sumas"),
    dict(n=7, name="Campo de cobertura", folder="coverage_field", app="coverage_field", version="gpu_kernel",
         ops=7, bytes=25, times=96,
         why="5 lecturas de 4 B, 1 B de calle y 4 B de salida; 2 sumas y 5 comparaciones; 96 pasadas"),
    dict(n=12, name="Indicadores de entrega", folder="delivery_kpis", app="delivery_kpis", version="gpu_kernel",
         ops=4, bytes=4, times=1,
         why="una lectura de 4 B; suma, máximo, comparación y conteo"),
    dict(n=14, name="Curva de pedidos en espera", folder="backlog_curve", app="backlog_curve", version="gpu_kernel",
         ops=9, bytes=16, times=1,
         why="leer y escribir 4 B en el scan y otra vez al sumar los totales; 8 sumas del scan y una más"),
    dict(n=10, name="Pedidos por restaurante y franja", folder="restaurant_slots", app="restaurant_slots", version="gpu_kernel",
         ops=5, bytes=16, times=1,
         why="8 B de lectura y un atómico de 8 B; división, comparación, producto y dos sumas"),
    dict(n=6, name="Tendencia de la demanda", folder="demand_trend", app="demand_trend", version="gpu_kernel",
         ops=30, bytes=64, times=1,
         why="15 lecturas de 4 B y 4 B de salida; 15 multiplicaciones y 15 sumas"),
    dict(n=8, name="Campo de respuesta", folder="response_field", app="response_field", version="gpu_kernel",
         ops=224, bytes=24, times=14,
         why="por celda entregada: 4 celdas del tile de 5 B y 4 B de salida; 4 celdas por 8 pasadas por 7 operaciones; 14 lanzamientos"),
    dict(n=2, name="Restaurante mejor ubicado", folder="through_restaurant", app="through_restaurant", version="gpu_kernel",
         ops=400, bytes=112, times=1,
         why="13 fases con 2 lecturas de 4 B y un atómico de 8 B; 200 sumas con su comparación"),
    dict(n=4, name="Mejor esquina para un restaurante", folder="round_trip_reach", app="round_trip_reach", version="gpu_kernel",
         ops=3, bytes=8, times=1,
         why="2 lecturas de 4 B; suma, comparación y conteo"),
]


def measure(reports, k):
    df = pd.read_csv(os.path.join(reports, k["folder"], "pc.csv"))
    df = df[(df.application == k["app"]) & (df.version == k["version"])]
    items = int(df["items"].max())
    top = df[df["items"] == items]
    return items, str(top["size"].iloc[0]), float(top.time_ms.median())


def num(x, digits=2):
    return f"{x:.{digits}f}".replace(".", ",")


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--reports", required=True)
    ap.add_argument("--out", required=True)
    args = ap.parse_args()

    rows = []
    for k in sorted(KERNELS, key=lambda kernel: kernel["n"]):
        items, size, ms = measure(args.reports, k)
        intensity = k["ops"] / k["bytes"]
        gops = k["ops"] * items * k["times"] / (ms / 1000.0) / 1e9
        roof = min(PEAK_GOPS, intensity * BANDWIDTH_GB_S)
        rows.append(dict(k, items=items, size=size, ms=ms, intensity=intensity, gops=gops, roof=roof))

    fig, ax = plt.subplots(figsize=(9.5, 6.2))
    xs = [0.05, PEAK_GOPS / BANDWIDTH_GB_S, 1000]
    ax.plot(xs, [min(PEAK_GOPS, x * BANDWIDTH_GB_S) for x in xs], color="#444441", linewidth=2)
    ax.text(3.2, 3.2 * BANDWIDTH_GB_S * 1.55, f"techo de memoria: {BANDWIDTH_GB_S:.0f} GB/s", rotation=40, fontsize=9, color="#444441")
    ax.text(45, PEAK_GOPS * 0.62, "techo de cómputo: 16,2 billones de op/s", fontsize=9, color="#444441")
    for r in rows:
        above = r["gops"] > r["roof"]
        ax.scatter(r["intensity"], r["gops"], s=70, color="#D85A30" if above else "#185FA5", zorder=3,
                   marker="^" if above else "o", edgecolors="white", linewidths=1)
        ax.annotate(str(r["n"]), (r["intensity"], r["gops"]), textcoords="offset points", xytext=OFFSETS.get(r["n"], (7, 5)), fontsize=10)
    ax.set_xscale("log")
    ax.set_yscale("log")
    ax.set_xlim(0.05, 1000)
    ax.set_ylim(1, 60000)
    ax.set_xlabel("Intensidad aritmética: operaciones por byte de memoria global")
    ax.set_ylabel("Rendimiento: miles de millones de operaciones por segundo")
    ax.set_title("Roofline de los kernels en la RTX 3060 Ti, en el tamaño más grande medido")
    ax.grid(True, which="both", color="#e1e0d9", linewidth=0.6)
    ax.scatter([], [], s=70, color="#185FA5", marker="o", label="por debajo del techo")
    ax.scatter([], [], s=70, color="#D85A30", marker="^", label="por encima del techo de memoria: la caché sirve las lecturas repetidas")
    ax.legend(loc="lower right", fontsize=9, frameon=False)
    fig.tight_layout()
    fig.savefig(os.path.join(args.out, "roofline.png"), dpi=130)

    lines = ["# Roofline de los kernels", "",
             "Generado por `tools/gpu_roofline.py`. Los tiempos son la mediana del kernel en el tamaño más grande de cada `pc.csv`; "
             "las operaciones y los bytes por elemento salen de contar el código de cada kernel.", "",
             "| N.º | Aplicación | Tamaño | Elementos | Veces | Operaciones por elemento | Bytes por elemento | Intensidad (op/B) | Kernel (ms) | "
             "Rendimiento (miles de millones de op/s) | Techo a esa intensidad | Parte del techo |",
             "|---|---|---|---|---|---|---|---|---|---|---|---|"]
    for r in rows:
        lines.append(f"| {r['n']} | {r['name']} | {r['size']} | {r['items']} | {r['times']} | {r['ops']} | {r['bytes']} | "
                     f"{num(r['intensity'])} | {num(r['ms'], 3)} | {num(r['gops'], 0)} | {num(r['roof'], 0)} | "
                     f"{num(100 * r['gops'] / r['roof'], 0)} % |")
    lines += ["", "De dónde sale cada cuenta:", ""]
    for r in rows:
        lines.append(f"- **{r['n']}. {r['name']}:** {r['why']}.")
    lines += ["", "![Roofline](roofline.png)", ""]
    with open(os.path.join(args.out, "roofline.md"), "w", encoding="utf-8") as f:
        f.write("\n".join(lines))
    print(f"{args.out}: roofline.png, roofline.md")
    for r in rows:
        print(r["n"], r["name"], r["size"], round(r["intensity"], 2), round(r["gops"], 1), round(r["roof"], 1), f"{100 * r['gops'] / r['roof']:.0f}%")


if __name__ == "__main__":
    main()
