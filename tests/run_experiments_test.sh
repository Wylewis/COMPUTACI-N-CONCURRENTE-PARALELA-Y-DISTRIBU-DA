#!/bin/sh
# El corredor de experimentos produce una tabla con una fila por configuracion.
# Uso: tests/run_experiments_test.sh <ruta a delivery_sim>
set -u
root=$(cd "$(dirname "$0")/.." && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/delivery_sim_exp_test.XXXXXX")
trap 'rm -rf "$work"' EXIT
python3 "$root/tools/run_experiments.py" --bin "$1" --config "$root/config/equipetrol.json" \
    --vary fleet.bagCapacity=1,3 --seeds 42 --duration 120 --scale 1200 --jobs 2 --out "$work/exp.md" > "$work/out.txt" 2>&1 || {
    echo "FALLO  el corredor termino con error:"; cat "$work/out.txt"; exit 1; }
rows=$(grep -c '^| fleet.bagCapacity=' "$work/exp.md")
[ "$rows" -eq 2 ] || { echo "FALLO  se esperaban 2 filas, hay $rows:"; cat "$work/exp.md"; exit 1; }
grep -q '"results"' "$work/exp.json" || { echo "FALLO  falta el JSON crudo"; exit 1; }
echo "experiments_test: tabla con 2 configuraciones y JSON crudo"
