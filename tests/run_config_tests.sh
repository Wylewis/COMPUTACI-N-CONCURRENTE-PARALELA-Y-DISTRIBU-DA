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

valid="$here/config/minimal.json"
err=$("$bin" "$valid" 2>&1 >/dev/null)
if printf '%s' "$err" | grep -q "configuracion cargada"; then
    echo "ok     $(basename "$valid") -> cargado"
else
    echo "FALLO  $(basename "$valid"): no se cargo: $(printf '%s' "$err" | head -n 1)"
    failures=$((failures + 1))
fi

if [ "$failures" -eq 0 ]; then
    echo "config_tests: todas las pruebas pasaron"
    exit 0
fi
echo "config_tests: $failures prueba(s) fallaron"
exit 1
