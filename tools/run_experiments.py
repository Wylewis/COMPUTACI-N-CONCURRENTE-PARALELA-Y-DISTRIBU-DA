#!/usr/bin/env python3
"""Corredor de experimentos: corre delivery_sim con varias configuraciones
derivadas de una base, en procesos paralelos, y agrega los resultados en una
tabla Markdown (y un JSON con los datos crudos).

Uso:
    python3 tools/run_experiments.py --bin build/delivery_sim --config config/equipetrol.json \\
        --vary fleet.bagCapacity=1,2,3,4 --seeds 42,7,2024 --duration 1800 --scale 600 \\
        --jobs 4 --out tools/reports/exp_bag.md

--vary toma una ruta de clave del config y los valores a probar (numeros o
cadenas); se puede repetir para variar varias claves a la vez (se combinan
todas con todas). --set fija otras claves en todas las corridas. Cada
combinacion se corre una vez por semilla; la tabla muestra la media por
combinacion y el detalle por semilla queda en el JSON. Cada log se verifica
con check_log.py y la tabla marca las corridas con errores.

Solo usa la libreria estandar de Python.
"""

import argparse
import concurrent.futures
import copy
import itertools
import json
import os
import re
import statistics
import subprocess
import sys
import tempfile
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from check_log import check  # noqa: E402

CLOSING = re.compile(
    r"(?P<assigned>\d+) asignados \((?P<retries>\d+) reintentos, (?P<late>\d+) estimaciones tardias\), "
    r"(?P<noCourier>\d+) sin repartidor, (?P<delivered>\d+) entregados, (?P<breakdowns>\d+) averias, "
    r"(?P<rescues>\d+) rescates")


def parse_value(text):
    for conv in (int, float):
        try:
            return conv(text)
        except ValueError:
            pass
    return {"true": True, "false": False}.get(text.lower(), text)


def set_path(cfg, path, value):
    keys = path.split(".")
    node = cfg
    for k in keys[:-1]:
        node = node[k]
    node[keys[-1]] = value


def make_config(base, overrides, seed, duration, scale, image, out_path):
    cfg = copy.deepcopy(base)
    for path, value in overrides.items():
        set_path(cfg, path, value)
    cfg["orders"]["seed"] = seed
    cfg["simulation"]["durationS"] = duration
    cfg["simulation"]["timeScale"] = scale
    cfg["map"]["image"] = image
    with open(out_path, "w", encoding="utf-8") as f:
        json.dump(cfg, f)
    return cfg


def run_one(binary, cfg_path, log_path):
    t0 = time.monotonic()
    proc = subprocess.run([binary, cfg_path, "--no-window", "--log", log_path], capture_output=True, text=True)
    wall = time.monotonic() - t0
    result = {"status": proc.returncode, "wallS": round(wall, 2)}
    lines = [l for l in proc.stdout.splitlines() if l.strip()]
    try:
        result["summary"] = json.loads(lines[-1]) if lines else {}
    except json.JSONDecodeError:
        result["summary"] = {}
    m = CLOSING.search(proc.stderr)
    result["closing"] = {k: int(v) for k, v in m.groupdict().items()} if m else {}
    try:
        with open(log_path, encoding="utf-8") as f:
            errors, _ = check([l.rstrip("\n") for l in f], result["summary"] or None, json.load(open(cfg_path)))
        result["logErrors"] = len(errors)
    except (OSError, ValueError) as e:
        result["logErrors"] = -1
        result["logError"] = str(e)
    return result


def label(overrides):
    return ", ".join(f"{k}={v}" for k, v in overrides.items()) or "base"


def mean(values):
    values = [v for v in values if v is not None]
    return round(statistics.mean(values), 1) if values else 0


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--bin", required=True)
    ap.add_argument("--config", required=True)
    ap.add_argument("--vary", action="append", default=[], help="clave=v1,v2,... (repetible)")
    ap.add_argument("--set", action="append", default=[], help="clave=valor fijo para todas las corridas")
    ap.add_argument("--seeds", default="42")
    ap.add_argument("--duration", type=int, default=1800, help="segundos simulados")
    ap.add_argument("--scale", type=float, default=600.0)
    ap.add_argument("--jobs", type=int, default=max(1, (os.cpu_count() or 4) // 4))
    ap.add_argument("--out", required=True, help="tabla Markdown; el JSON crudo va al lado")
    ap.add_argument("--title", default="")
    args = ap.parse_args()

    base = json.load(open(args.config, encoding="utf-8"))
    image = os.path.abspath(os.path.join(os.path.dirname(args.config), base["map"]["image"]))
    fixed = dict(kv.split("=", 1) for kv in args.set)
    fixed = {k: parse_value(v) for k, v in fixed.items()}
    axes = []
    for spec in args.vary:
        key, values = spec.split("=", 1)
        axes.append([(key, parse_value(v)) for v in values.split(",")])
    combos = [dict(c) for c in itertools.product(*axes)] if axes else [{}]
    seeds = [int(s) for s in args.seeds.split(",")]

    work = tempfile.mkdtemp(prefix="delivery_sim_exp.")
    jobs = []
    for i, combo in enumerate(combos):
        for seed in seeds:
            overrides = {**fixed, **combo}
            cfg_path = os.path.join(work, f"c{i}_s{seed}.json")
            make_config(base, overrides, seed, args.duration, args.scale, image, cfg_path)
            jobs.append((i, combo, seed, cfg_path, os.path.join(work, f"c{i}_s{seed}.log")))

    results = []
    with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as pool:
        futures = {pool.submit(run_one, args.bin, cfg, log): (i, combo, seed) for i, combo, seed, cfg, log in jobs}
        for fut in concurrent.futures.as_completed(futures):
            i, combo, seed = futures[fut]
            r = fut.result()
            r.update({"combo": i, "overrides": combo, "seed": seed})
            results.append(r)
            print(f"  {label(combo)} semilla {seed}: estado {r['status']}, entregados "
                  f"{r['summary'].get('delivered', '?')}, {r['wallS']} s, errores de log {r['logErrors']}", file=sys.stderr)
    results.sort(key=lambda r: (r["combo"], r["seed"]))

    cols = [("entregados", lambda r: r["summary"].get("delivered")), ("rechazados", lambda r: r["summary"].get("rejected")),
            ("pendientes", lambda r: r["summary"].get("pending")), ("entrega media (min)", lambda r: r["summary"].get("avgDeliveryMs", 0) / 60000),
            ("averias", lambda r: r["summary"].get("breakdowns")), ("reasignados", lambda r: r["summary"].get("reassigned")),
            ("reintentos", lambda r: r["closing"].get("retries")), ("tardias", lambda r: r["closing"].get("late")),
            ("tiempo real (s)", lambda r: r["wallS"])]
    rows = []
    for i, combo in enumerate(combos):
        rs = [r for r in results if r["combo"] == i]
        bad = sum(1 for r in rs if r["status"] != 0 or r["logErrors"] != 0)
        rows.append([label(combo)] + [mean([f(r) for r in rs]) for _, f in cols] + [f"{len(rs) - bad}/{len(rs)}"])
    header = ["configuracion"] + [c for c, _ in cols] + ["corridas ok"]
    md = []
    if args.title:
        md.append(f"# {args.title}\n")
    md.append(f"Base `{os.path.relpath(args.config)}`, {args.duration} s simulados a {args.scale}x, semillas {args.seeds}"
              + (f", fijos: {label(fixed)}" if fixed else "") + ". Medias por configuracion; detalle por semilla en el JSON.\n")
    md.append("| " + " | ".join(header) + " |")
    md.append("|" + "---|" * len(header))
    for row in rows:
        md.append("| " + " | ".join(str(v) for v in row) + " |")
    os.makedirs(os.path.dirname(os.path.abspath(args.out)), exist_ok=True)
    with open(args.out, "w", encoding="utf-8") as f:
        f.write("\n".join(md) + "\n")
    with open(os.path.splitext(args.out)[0] + ".json", "w", encoding="utf-8") as f:
        json.dump({"args": vars(args), "results": results}, f, indent=1)
    print("\n".join(md))
    return 0 if all(r["status"] == 0 and r["logErrors"] == 0 for r in results) else 1


if __name__ == "__main__":
    sys.exit(main())
