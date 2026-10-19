#!/bin/sh
# Corre la prueba de estres de averias y verifica su log con check_log.py.
# Uso: tests/run_stress_test.sh <ruta a breakdown_stress_test>
set -u
if [ $# -ne 1 ] || [ ! -x "$1" ]; then
    echo "uso: $0 <ruta a breakdown_stress_test>" >&2
    exit 2
fi
bin=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
root=$(cd "$(dirname "$0")/.." && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/delivery_sim_stress.XXXXXX")
trap 'rm -rf "$work"' EXIT
"$bin" "$work/stress.log" || exit 1
python3 "$root/tools/check_log.py" "$work/stress.log" --quiet > "$work/check.txt" 2>&1 && { echo "stress: log verificado"; exit 0; }
echo "FALLO  stress: el verificador reporta errores:"; cat "$work/check.txt"; exit 1
