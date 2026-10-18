#!/bin/sh
# Prueba la carga y validacion del archivo de configuracion.
#
# Uso: tests/run_config_tests.sh <ruta a delivery_sim>
#
# Cada archivo de tests/invalid/ debe hacer que el programa salga con estado
# distinto de cero e imprima un motivo en stderr. El archivo valido
# tests/config/minimal.json debe cargarse (el programa anuncia la carga en
# stderr; todavia no simula). Sale con 0 si todas las pruebas pasan.

set -u

if [ $# -ne 1 ] || [ ! -x "$1" ]; then
    echo "uso: $0 <ruta a delivery_sim>" >&2
    exit 2
fi
bin=$1
here=$(cd "$(dirname "$0")" && pwd)
failures=0

for f in "$here"/invalid/*.json; do
    name=$(basename "$f")
    err=$("$bin" "$f" 2>&1 >/dev/null)
    status=$?
    if [ "$status" -eq 0 ]; then
        echo "FALLO  $name: salio con estado 0 (debia rechazarse)"
        failures=$((failures + 1))
    elif [ -z "$err" ]; then
        echo "FALLO  $name: estado $status pero sin mensaje en stderr"
        failures=$((failures + 1))
    else
        echo "ok     $name -> $(printf '%s' "$err" | head -n 1)"
    fi
done

# Configs validos: se ejecutan con una copia acortada a 1 segundo simulado
# (la ruta de la imagen se vuelve absoluta para que la copia la encuentre).
# Deben imprimir la linea de inicio y salir con estado 0.
work=$(mktemp -d "${TMPDIR:-/tmp}/delivery_sim_cfg.XXXXXX")
trap 'rm -rf "$work"' EXIT
for valid in "$here/config/minimal.json" "$here/../config/equipetrol.json"; do
    name=$(basename "$valid")
    python3 - "$valid" "$work/$name" <<'EOF'
import json, os, sys
src, dst = sys.argv[1], sys.argv[2]
c = json.load(open(src))
c["map"]["image"] = os.path.abspath(os.path.join(os.path.dirname(src), c["map"]["image"]))
c["simulation"]["durationS"] = 1
c["simulation"]["timeScale"] = 1000
json.dump(c, open(dst, "w"))
EOF
    out=$("$bin" "$work/$name" --log "$work/$name.log" 2>"$work/$name.err")
    status=$?
    if [ "$status" -eq 0 ] && [ "$(printf '%s' "$out" | head -n 1)" = "Simulation has started..." ]; then
        echo "ok     $name -> cargado y simulado 1 s"
    else
        echo "FALLO  $name: estado $status: $(head -n 1 "$work/$name.err")"
        failures=$((failures + 1))
    fi
done

if [ "$failures" -eq 0 ]; then
    echo "config_tests: todas las pruebas pasaron"
    exit 0
fi
echo "config_tests: $failures prueba(s) fallaron"
exit 1
