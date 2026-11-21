#!/usr/bin/env python3
"""Arma la tabla resumen de todas las aplicaciones de GPU.

Uso:
    python3 tools/gpu_summary.py --reports tools/reports/gpu --out tools/reports/gpu

Lee el mac.csv y el pc.csv de cada carpeta y escribe resumen.md con una fila
por aplicacion: concepto, si es la primera o la segunda de ese concepto, desde
que tamano la GPU de punta a punta le gana a la mejor CPU, el mejor speedup de
punta a punta y de kernel, y el veredicto. Los speedups son la mediana de la
mejor version de CPU dividida por la mediana de la GPU. Escribe tambien
resumen.png: a la izquierda el mejor speedup de kernel y de punta a punta de
cada aplicacion contra la laptop, y a la derecha que parte del tiempo de punta
a punta es kernel en el tamano mas grande. Ninguna cifra se escribe a mano.
Requiere pandas y matplotlib.
"""

import argparse
import os

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import pandas as pd

APPS = [
    ("03, memory tiling", "1.ª", "Tabla de tiempos", "travel_table", "travel_table", "CPU: el método de la GPU hace más trabajo que Dijkstra"),
    ("03, memory tiling", "2.ª", "Restaurante mejor ubicado", "through_restaurant", "through_restaurant", "GPU con la red de la ciudad; CPU con la del enunciado"),
    ("04, performance", "1.ª", "Estimación de entrega por lote", "eta_batch", "eta_batch", "CPU: bajar la matriz cuesta más que calcularla"),
    ("04, performance", "2.ª", "Mejor esquina para un restaurante nuevo", "round_trip_reach", "round_trip_reach", "CPU, con la tabla girada"),
    ("05, convolución", "1.ª", "Mapa de calor de demanda", "demand_heatmap", "demand_heatmap", "CPU: la cuadrícula del programa es chica y las copias pesan más"),
    ("05, convolución", "2.ª", "Tendencia de la demanda", "demand_trend", "demand_trend", "CPU: el resultado es tan grande como la entrada"),
    ("06, stencil", "1.ª", "Campo de cobertura", "coverage_field", "coverage_field", "CPU en el programa; GPU en la misma máquina desde 256 × 256"),
    ("06, stencil", "2.ª", "Campo de respuesta con motos ocupadas", "response_field", "response_field", "CPU en el programa; GPU en la misma máquina desde 256 × 256"),
    ("07, histograma", "1.ª", "Histograma de tiempos de entrega", "delivery_histogram", "delivery_histogram", "CPU: subir los datos cuesta más que contarlos"),
    ("07, histograma", "2.ª", "Pedidos por restaurante y franja horaria", "restaurant_slots", "restaurant_slots", "CPU: subir los datos cuesta más que contarlos"),
    ("08, reducción", "1.ª", "Mejor moto de cada pedido", "dispatch_argmin", "dispatch_argmin", "CPU sola; GPU si va encadenada a la estimación"),
    ("08, reducción", "2.ª", "Indicadores de entrega", "delivery_kpis", "delivery_kpis", "CPU: un hilo ya va muy rápido y bajan tres números"),
    ("09, scan", "1.ª", "Pedidos agrupados por restaurante", "group_orders", "group_orders", "Agrupar sí, en CPU: la ronda es chica para la tarjeta"),
    ("09, scan", "2.ª", "Curva de pedidos en espera", "backlog_curve", "backlog_curve", "CPU: poco cálculo y todo lo que sube tiene que bajar"),
]


def medians(df, app, version):
    return df[(df.application == app) & (df.version == version)].groupby("items").time_ms.median()


def best_cpu(df, app):
    s = df[(df.application == app) & df.version.isin(["cpu_1", "cpu_n"])]
    return s.groupby(["items", "version", "threads"]).time_ms.median().groupby("items").min()


def crossover(ratio, sizes):
    wins = ratio[ratio >= 1.0]
    return "no hay" if wins.empty else sizes[wins.index.min()]


def fx(x):
    return f"{x:.2f}x".replace(".", ",")


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--reports", required=True)
    ap.add_argument("--out", required=True)
    args = ap.parse_args()

    lines = ["# Resumen de las aplicaciones de GPU", "",
             "Generado por `tools/gpu_summary.py` a partir del `mac.csv` y el `pc.csv` de cada carpeta. Los speedups son contra la mejor "
             "versión de CPU en cada tamaño; mayor que 1 significa que gana la GPU.", "",
             "| N.º | Concepto | Aplicación del concepto | Aplicación | Cruce de punta a punta contra la laptop | Mejor punta a punta contra la laptop | "
             "Mejor kernel contra la laptop | Cruce en la misma máquina | Mejor punta a punta en la misma máquina | Veredicto | Puntos |",
             "|---|---|---|---|---|---|---|---|---|---|---|"]
    names, best_kernel, best_e2e, share = [], [], [], []
    for n, (concept, which, name, folder, app, verdict) in enumerate(APPS, start=1):
        mac = pd.read_csv(os.path.join(args.reports, folder, "mac.csv"))
        pc = pd.read_csv(os.path.join(args.reports, folder, "pc.csv"))
        sizes = pc[pc.application == app].drop_duplicates("items").set_index("items")["size"].astype(str).str.replace("x", " × ")
        e2e, kernel = medians(pc, app, "gpu_e2e"), medians(pc, app, "gpu_kernel")
        laptop, same = best_cpu(mac, app), best_cpu(pc, app)
        vs_laptop, vs_same, k_laptop = laptop / e2e, same / e2e, laptop / kernel
        lines.append(f"| {n} | {concept} | {which} | {name} | {crossover(vs_laptop, sizes)} | {fx(vs_laptop.max())} | {fx(k_laptop.max())} | "
                     f"{crossover(vs_same, sizes)} | {fx(vs_same.max())} | {verdict} | 1 |")
        names.append(f"{n}. {name}")
        best_kernel.append(float(k_laptop.max()))
        best_e2e.append(float(vs_laptop.max()))
        share.append(100.0 * float(kernel[kernel.index.max()]) / float(e2e[e2e.index.max()]))
        print(n, name, crossover(vs_laptop, sizes), fx(vs_laptop.max()), fx(k_laptop.max()), crossover(vs_same, sizes), fx(vs_same.max()))
    lines += ["", f"Puntos: {len(APPS)}, dos aplicaciones en cada uno de siete conceptos.", ""]
    with open(os.path.join(args.out, "resumen.md"), "w", encoding="utf-8") as f:
        f.write("\n".join(lines))

    rows = list(range(len(names)))[::-1]
    fig, (left, right) = plt.subplots(1, 2, figsize=(12.5, 6.0), gridspec_kw={"width_ratios": [1.5, 1.0]})
    left.barh([r + 0.2 for r in rows], [v - 1.0 for v in best_kernel], left=1.0, height=0.38, color="#D85A30", label="solo el kernel")
    left.barh([r - 0.2 for r in rows], [v - 1.0 for v in best_e2e], left=1.0, height=0.38, color="#185FA5",
              label="de punta a punta, con copias")
    left.axvline(1.0, color="#444441", linewidth=1.2)
    left.text(1.06, len(names) - 0.42, "gana la GPU", fontsize=9, color="#444441")
    left.text(0.94, len(names) - 0.42, "gana la CPU", fontsize=9, color="#444441", ha="right")
    left.set_xscale("log")
    left.set_xlim(0.02, 30)
    left.set_ylim(-0.7, len(names) + 0.1)
    left.set_yticks(rows)
    left.set_yticklabels(names, fontsize=9)
    left.set_xlabel("Mejor speedup de la GPU contra la mejor CPU de la laptop")
    left.grid(True, axis="x", which="both", color="#e1e0d9", linewidth=0.6)
    left.set_axisbelow(True)
    left.legend(loc="lower center", bbox_to_anchor=(0.5, 1.0), ncol=2, fontsize=9, frameon=False)
    right.barh(rows, share, height=0.6, color="#D85A30")
    for r, v in zip(rows, share):
        right.text(v + 1.5, r, f"{v:.0f} %", va="center", fontsize=9)
    right.set_xlim(0, 112)
    right.set_ylim(-0.7, len(names) + 0.1)
    right.set_yticks(rows)
    right.set_yticklabels([name.split(".")[0] for name in names], fontsize=9)
    right.set_xlabel("Parte del kernel en la punta a punta (%)")
    right.grid(True, axis="x", color="#e1e0d9", linewidth=0.6)
    right.set_axisbelow(True)
    fig.tight_layout()
    fig.savefig(os.path.join(args.out, "resumen.png"), dpi=130)
    print(f"{args.out}: resumen.md, resumen.png")


if __name__ == "__main__":
    main()
