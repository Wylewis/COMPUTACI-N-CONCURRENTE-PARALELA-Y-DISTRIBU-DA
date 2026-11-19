#!/bin/sh
# Prueba del modo de medicion: corre `--headless --bench` con el caso de
# referencia y revisa el CSV: cabecera, columnas, cantidad de filas por
# version, tiempos numericos y resultados correctos. Si el binario no tiene
# GPU real, no debe haber filas de GPU: la emulacion no se cronometra.
#
# Uso: tests/run_bench_test.sh <ruta a delivery_sim>

set -u

if [ $# -ne 1 ] || [ ! -x "$1" ]; then
    echo "uso: $0 <ruta a delivery_sim>" >&2
    exit 2
fi
bin=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/.." && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/delivery_sim_bench.XXXXXX")
trap 'rm -rf "$work"' EXIT

"$bin" "$root/config/equipetrol.json" --headless --bench "$work/r.csv" --bench-reps 3 --bench-sweep \
    --bench-threads 4 --bench-apps vector_add --bench-max-items 100000 > "$work/out" 2> "$work/err"
status=$?
if [ "$status" -ne 0 ]; then
    echo "FALLO  el modo de medicion salio con estado $status"; cat "$work/err"; exit 1
fi

"$bin" "$root/config/equipetrol.json" --headless --bench "$work/once.csv" --bench-once \
    --bench-apps vector_add --bench-max-items 1000 > /dev/null 2>&1 || { echo "FALLO  --bench-once"; exit 1; }
"$bin" "$root/config/equipetrol.json" --headless --bench "$work/x.csv" --bench-apps no_existe > /dev/null 2>&1 &&
    { echo "FALLO  una aplicacion desconocida debia rechazarse"; exit 1; }

python3 - "$work/r.csv" "$work/once.csv" "$work/err" <<'PY'
import csv, sys
rows = list(csv.DictReader(open(sys.argv[1])))
once = list(csv.DictReader(open(sys.argv[2])))
gpu_real = "sin GPU real" not in open(sys.argv[3]).read()
fail = []
def need(ok, what):
    if not ok: fail.append(what)
cols = ["application", "concept", "version", "threads", "size", "items", "repetition", "time_ms", "correct"]
need(rows and list(rows[0].keys()) == cols, "cabecera con las columnas esperadas")
sizes = sorted({int(r["items"]) for r in rows})
need(sizes == [1000, 10000, 100000], f"tres tamanos hasta el limite pedido, hay {sizes}")
def count(version, threads=None):
    return sum(1 for r in rows if r["version"] == version and (threads is None or r["threads"] == str(threads)))
need(count("cpu_1") == 9, "cpu_1: 3 tamanos x 3 repeticiones")
need(count("cpu_n", 1) == 9 and count("cpu_n", 2) == 9 and count("cpu_n", 4) == 9, "cpu_n: barrido de hilos 1, 2 y 4")
if gpu_real:
    need(count("gpu_kernel") == 9 and count("gpu_e2e") == 9, "con GPU real hay filas de kernel y de punta a punta")
    need(all(float(k["time_ms"]) <= float(e["time_ms"]) for k, e in zip(
        [r for r in rows if r["version"] == "gpu_kernel"], [r for r in rows if r["version"] == "gpu_e2e"])),
        "el kernel solo nunca tarda mas que la punta a punta")
else:
    need(count("gpu_kernel") == 0 and count("gpu_e2e") == 0, "sin GPU real no se escriben filas de GPU")
need(all(r["correct"] == "yes" for r in rows), "todas las corridas dan el resultado correcto")
need(all(float(r["time_ms"]) >= 0 for r in rows), "tiempos numericos no negativos")
need(sorted({r["repetition"] for r in rows}) == ["0", "1", "2"], "repeticiones 0, 1 y 2")
need({r["repetition"] for r in once} == {"0"} and {r["items"] for r in once} == {"1000"}, "--bench-once: una sola repeticion")
for f in fail: print("FALLO ", f)
print(f"bench_test: {len(rows)} filas, GPU real: {'si' if gpu_real else 'no'}")
sys.exit(1 if fail else 0)
PY
status=$?
if [ "$status" -eq 0 ]; then
    echo "bench_test: todas las pruebas pasaron"
fi
exit $status
