#!/usr/bin/env python3
"""Arma la tabla y los graficos de un experimento de CPU contra GPU.

Uso:
    python3 tools/gpu_report.py --app travel_table \
        --laptop tools/reports/gpu/travel_table/mac.csv \
        --gpu tools/reports/gpu/travel_table/pc.csv \
        --out tools/reports/gpu/travel_table

Lee los CSV que escribe `delivery_sim --headless --bench` en las dos maquinas:
la laptop, de donde salen los tiempos de CPU del veredicto, y la maquina con
la GPU, de donde salen los tiempos de GPU y los de CPU de la comparacion en la
misma maquina. Escribe en --out:

  tabla.md    una fila por tamano: mediana y dispersion de cada version, el
              speedup de la GPU contra la mejor CPU y si el resultado fue
              correcto; la misma tabla con la CPU de la maquina de la GPU; y
              el barrido de hilos.
  tiempo.png  tiempo contra tamano en ejes log-log, las cuatro versiones, con
              el cruce marcado si la GPU de punta a punta alcanza a la CPU.
  copias.png  parte del tiempo de GPU que se va en copias y en el kernel.
  hilos.png   speedup de la version multihilo segun el numero de hilos.

Ninguna cifra se escribe a mano: todo sale de los CSV.
Requiere pandas y matplotlib.
"""

import argparse
import os

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import pandas as pd

COLORS = {"cpu_1": "#5F5E5A", "cpu_n": "#185FA5", "gpu_kernel": "#D85A30", "gpu_e2e": "#993C1D"}


def summarize(df):
    g = df.groupby(["items", "size", "version", "threads"]).time_ms
    out = g.agg(median="median", low="min", high="max", runs="count").reset_index()
    wrong = df[df.correct != "yes"].groupby(["items", "version", "threads"]).size()
    out["wrong"] = [int(wrong.get((r.items, r.version, r.threads), 0)) for r in out.itertuples()]
    return out


def pick(summary, version, threads=None):
    s = summary[summary.version == version]
    if threads is not None:
        s = s[s.threads == threads]
    return s.set_index("items").sort_index()


def fmt(row):
    if row is None:
        return "—"
    return f"{row['median']:.3f} ({row['low']:.3f}–{row['high']:.3f})"


def cell(table, items):
    return table.loc[items] if items in table.index else None


def best_cpu(summary, items):
    s = summary[(summary['items'] == items) & (summary.version.isin(["cpu_1", "cpu_n"]))]
    r = s.loc[s['median'].idxmin()]
    return r['median'], ("1 hilo" if r.version == "cpu_1" else f"{int(r.threads)} hilos")


def speed(cpu, gpu_row):
    return "—" if gpu_row is None else f"{cpu / gpu_row['median']:.2f}x"


def table_block(title, cpu_summary, gpu_summary, n_threads, cpu_machine):
    cpu1 = pick(cpu_summary, "cpu_1")
    cpun = pick(cpu_summary, "cpu_n", n_threads)
    kern = pick(gpu_summary, "gpu_kernel")
    e2e = pick(gpu_summary, "gpu_e2e")
    lines = [f"### {title}", "",
             f"| Tamaño | CPU 1 hilo (ms) | CPU {n_threads} hilos (ms) | GPU kernel (ms) | GPU punta a punta (ms) | "
             f"Mejor CPU | Speedup de la GPU contra la mejor CPU (kernel / punta a punta) | ¿Correcto? |",
             "|---|---|---|---|---|---|---|---|"]
    for items in cpu1.index:
        best, which = best_cpu(cpu_summary, items)
        k, e = cell(kern, items), cell(e2e, items)
        wrong = int(cpu_summary[cpu_summary['items'] == items].wrong.sum())
        wrong += int(gpu_summary[(gpu_summary['items'] == items) & gpu_summary.version.str.startswith("gpu")].wrong.sum())
        lines.append(f"| {cpu1.loc[items, 'size']} | {fmt(cpu1.loc[items])} | {fmt(cell(cpun, items))} | {fmt(k)} | "
                     f"{fmt(e)} | {best:.3f} ({which}) | {speed(best, k)} / {speed(best, e)} | "
                     f"{'sí' if wrong == 0 else f'NO ({wrong})'} |")
    lines += ["", f"Tiempos de CPU: {cpu_machine}. Mediana de las repeticiones y, entre paréntesis, mínimo y máximo.", ""]
    return lines


def crossover(cpu_summary, gpu_summary):
    e2e = pick(gpu_summary, "gpu_e2e")
    for items in sorted(e2e.index):
        best, _ = best_cpu(cpu_summary, items)
        if e2e.loc[items, "median"] < best:
            return items, e2e.loc[items, "size"]
    return None, None


def plot_time(laptop, gpu, n_laptop, n_gpu, cross, app, path):
    fig, ax = plt.subplots(figsize=(7.2, 4.6))
    series = [(pick(laptop, "cpu_1"), "CPU, 1 hilo (laptop)", COLORS["cpu_1"], "-", "o"),
              (pick(laptop, "cpu_n", n_laptop), f"CPU, {n_laptop} hilos (laptop)", COLORS["cpu_n"], "-", "s"),
              (pick(gpu, "cpu_n", n_gpu), f"CPU, {n_gpu} hilos (máquina de la GPU)", COLORS["cpu_n"], ":", "s"),
              (pick(gpu, "gpu_kernel"), "GPU, solo kernel", COLORS["gpu_kernel"], "--", "^"),
              (pick(gpu, "gpu_e2e"), "GPU, punta a punta", COLORS["gpu_e2e"], "-", "D")]
    for table, label, color, style, marker in series:
        if len(table):
            ax.plot(table.index, table["median"], style, color=color, marker=marker, markersize=4.5, linewidth=1.6, label=label)
    if cross[0] is not None:
        ax.axvline(cross[0], color="#888780", linewidth=0.8, linestyle="-.")
        ax.annotate(f"cruce: {cross[1]}", (cross[0], ax.get_ylim()[0]), textcoords="offset points", xytext=(4, 6), fontsize=8)
    ax.set_xscale("log")
    ax.set_yscale("log")
    ax.set_xlabel("elementos de trabajo independientes")
    ax.set_ylabel("tiempo (ms), mediana")
    ax.set_title(f"{app}: tiempo contra tamaño" + ("" if cross[0] is not None else " (sin cruce en el rango medido)"), fontsize=10)
    ax.grid(True, which="both", linewidth=0.3, alpha=0.6)
    ax.legend(fontsize=8, frameon=False)
    fig.tight_layout()
    fig.savefig(path, dpi=150)
    plt.close(fig)


def plot_copies(gpu, app, path):
    kern, e2e = pick(gpu, "gpu_kernel"), pick(gpu, "gpu_e2e")
    if not len(e2e):
        return
    share_kernel = (kern["median"] / e2e["median"] * 100).clip(upper=100)
    labels = [str(s) for s in e2e["size"]]
    fig, ax = plt.subplots(figsize=(7.2, 3.8))
    ax.bar(labels, share_kernel, color=COLORS["gpu_kernel"], label="kernel")
    ax.bar(labels, 100 - share_kernel, bottom=share_kernel, color="#B4B2A9", label="copias y reserva de memoria")
    for i, v in enumerate(share_kernel):
        ax.text(i, min(v, 96) / 2, f"{v:.0f} %", ha="center", va="center", fontsize=8, color="white")
    ax.set_ylabel("% del tiempo de punta a punta")
    ax.set_xlabel("tamaño")
    ax.set_title(f"{app}: reparto del tiempo de GPU", fontsize=10)
    ax.legend(fontsize=8, frameon=False, loc="upper center", bbox_to_anchor=(0.5, -0.22), ncol=2)
    fig.tight_layout()
    fig.savefig(path, dpi=150)
    plt.close(fig)


def sweep_table(summary, machine):
    cpun = summary[summary.version == "cpu_n"]
    threads = sorted(cpun.threads.unique())
    if len(threads) < 2:
        return [], None
    one = pick(summary, "cpu_n", 1)
    lines = [f"### Barrido de hilos, {machine}", "",
             "Speedup de la versión multihilo contra ella misma con 1 hilo.", "",
             "| Tamaño | " + " | ".join(("1 hilo" if t == 1 else f"{t} hilos") for t in threads) + " |",
             "|---|" + "---|" * len(threads)]
    data = {}
    for items in one.index:
        row = []
        for t in threads:
            tab = pick(summary, "cpu_n", t)
            row.append(one.loc[items, "median"] / tab.loc[items, "median"])
        data[one.loc[items, "size"]] = row
        lines.append(f"| {one.loc[items, 'size']} | " + " | ".join(f"{v:.2f}x" for v in row) + " |")
    lines.append("")
    return lines, (threads, data)


def plot_sweep(sweep, app, machine, path):
    threads, data = sweep
    fig, ax = plt.subplots(figsize=(7.2, 4.2))
    shades = plt.cm.Blues([0.35 + 0.65 * i / max(1, len(data) - 1) for i in range(len(data))])
    for (size, row), color in zip(data.items(), shades):
        ax.plot(threads, row, marker="o", markersize=4, linewidth=1.5, color=color, label=str(size))
    ax.plot(threads, threads, color="#888780", linestyle="--", linewidth=0.9, label="ideal")
    ax.set_xscale("log", base=2)
    ax.set_yscale("log", base=2)
    ax.set_xticks(threads)
    ax.set_xticklabels([str(t) for t in threads])
    ax.set_yticks([1, 2, 4, 8, 16])
    ax.set_yticklabels(["1", "2", "4", "8", "16"])
    ax.set_xlabel("hilos")
    ax.set_ylabel("speedup contra 1 hilo")
    ax.set_title(f"{app}: barrido de hilos en {machine}", fontsize=10)
    ax.grid(True, which="both", linewidth=0.3, alpha=0.6)
    ax.legend(fontsize=8, frameon=False, title="tamaño", title_fontsize=8)
    fig.tight_layout()
    fig.savefig(path, dpi=150)
    plt.close(fig)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--app", required=True)
    ap.add_argument("--laptop", required=True, help="CSV medido en la laptop (CPU)")
    ap.add_argument("--gpu", required=True, help="CSV medido en la maquina con la GPU")
    ap.add_argument("--out", required=True)
    ap.add_argument("--laptop-name", default="laptop")
    ap.add_argument("--gpu-name", default="máquina de la GPU")
    args = ap.parse_args()

    laptop_raw = pd.read_csv(args.laptop)
    gpu_raw = pd.read_csv(args.gpu)
    laptop_raw = laptop_raw[laptop_raw.application == args.app]
    gpu_raw = gpu_raw[gpu_raw.application == args.app]
    laptop, gpu = summarize(laptop_raw), summarize(gpu_raw)
    n_laptop = int(laptop[laptop.version == "cpu_n"].threads.max())
    n_gpu = int(gpu[gpu.version == "cpu_n"].threads.max())
    cross = crossover(laptop, gpu)
    cross_same = crossover(gpu, gpu)
    os.makedirs(args.out, exist_ok=True)

    runs = int(gpu[gpu.version == "gpu_e2e"].runs.min())
    lines = [f"# {args.app}: CPU contra GPU", "",
             f"Generado por `tools/gpu_report.py` a partir de `{os.path.basename(args.laptop)}` y "
             f"`{os.path.basename(args.gpu)}`. {runs} repeticiones medidas por versión y tamaño, después de una de calentamiento.", ""]
    lines += table_block("Veredicto: la GPU contra la CPU de la laptop", laptop, gpu, n_laptop, args.laptop_name)
    lines.append("Cruce de punta a punta contra la mejor CPU de la laptop: " +
                 (f"**{cross[1]}** ({cross[0]} elementos)." if cross[0] is not None else "**no hay cruce en el rango medido**; la CPU gana en todos los tamaños."))
    lines.append("")
    lines += table_block("Misma máquina: la GPU contra la CPU de su propia máquina", gpu, gpu, n_gpu, args.gpu_name)
    lines.append("Cruce de punta a punta contra la mejor CPU de la misma máquina: " +
                 (f"**{cross_same[1]}** ({cross_same[0]} elementos)." if cross_same[0] is not None else "**no hay cruce en el rango medido**."))
    lines.append("")
    sweep_lines, sweep = sweep_table(laptop, args.laptop_name)
    lines += sweep_lines
    sweep_lines_gpu, _ = sweep_table(gpu, args.gpu_name)
    lines += sweep_lines_gpu
    lines += ["### Gráficos", "", "![Tiempo contra tamaño](tiempo.png)", "", "![Reparto del tiempo de GPU](copias.png)", ""]
    if sweep:
        lines += ["![Barrido de hilos](hilos.png)", ""]
        plot_sweep(sweep, args.app, args.laptop_name, os.path.join(args.out, "hilos.png"))
    plot_time(laptop, gpu, n_laptop, n_gpu, cross, args.app, os.path.join(args.out, "tiempo.png"))
    plot_copies(gpu, args.app, os.path.join(args.out, "copias.png"))
    with open(os.path.join(args.out, "tabla.md"), "w") as f:
        f.write("\n".join(lines) + "\n")
    print(f"{args.out}: tabla.md, tiempo.png, copias.png" + (", hilos.png" if sweep else ""))
    print("cruce (laptop):", cross[1], "| cruce (misma maquina):", cross_same[1])


if __name__ == "__main__":
    main()
