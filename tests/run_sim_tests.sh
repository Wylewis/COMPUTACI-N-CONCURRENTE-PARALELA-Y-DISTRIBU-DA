#!/bin/sh
# Prueba de integracion: corre delivery_sim con un config corto derivado de
# config/equipetrol.json, por tiempo cumplido y por SIGINT, y verifica la
# linea de inicio, el estado de salida, el log y el resumen con check_log.py.
#
# Uso: tests/run_sim_tests.sh <ruta a delivery_sim>

set -u

if [ $# -ne 1 ] || [ ! -x "$1" ]; then
    echo "uso: $0 <ruta a delivery_sim>" >&2
    exit 2
fi
bin=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/.." && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/delivery_sim_test.XXXXXX")
trap 'rm -rf "$work"' EXIT
failures=0

# Config corto: 2 minutos simulados a 240x (medio segundo real), libro de 5
# para provocar queueFull, y pedidos cada 5 s simulados.
python3 - "$root/config/equipetrol.json" "$work/short.json" "$work/forever.json" <<'EOF'
import json, sys
src, short, forever = sys.argv[1:4]
c = json.load(open(src))
c["map"]["image"] = src.rsplit("/", 1)[0] + "/../data/equipetrol.png"
c["orders"]["meanIntervalMs"] = 5000
c["orders"]["maxPending"] = 5
c["simulation"]["durationS"] = 120
c["simulation"]["timeScale"] = 240
json.dump(c, open(short, "w"))
c["simulation"]["durationS"] = 0
json.dump(c, open(forever, "w"))
# Ciclo completo: 40 minutos simulados a 2400x (un segundo real), preparacion
# de 30 a 60 s para que haya entregas, y averias frecuentes para que haya
# liberaciones, reasignaciones y rescates.
c = json.load(open(src))
c["map"]["image"] = src.rsplit("/", 1)[0] + "/../data/equipetrol.png"
c["orders"]["meanIntervalMs"] = 15000
for r in c["restaurants"]:
    r["prepTimeMs"] = [30000, 60000]
c["incidents"]["breakdownProbability"] = 0.3
c["orders"]["urgentProbability"] = 0.3
c["orders"]["agingMs"] = 300000
c["simulation"]["durationS"] = 2400
c["simulation"]["timeScale"] = 2400
json.dump(c, open(short.replace("short.json", "full.json"), "w"))
EOF

check_run() {
    name=$1; status=$2; out=$3; log=$4; cfg=$5
    if [ "$status" -ne 0 ]; then
        echo "FALLO  $name: estado de salida $status"; failures=$((failures + 1)); return
    fi
    if [ "$(head -n 1 "$out")" != "Simulation has started..." ]; then
        echo "FALLO  $name: la primera linea de stdout no es la linea de inicio"; failures=$((failures + 1)); return
    fi
    tail -n 1 "$out" > "$work/$name.summary"
    if python3 "$root/tools/check_log.py" "$log" --summary "$work/$name.summary" --config "$cfg" --quiet > "$work/$name.check" 2>&1; then
        echo "ok     $name -> $(tail -n 1 "$work/$name.summary")"
    else
        echo "FALLO  $name: el verificador reporta errores:"; cat "$work/$name.check"; failures=$((failures + 1))
    fi
}

# 1. Termina por tiempo cumplido
"$bin" "$work/short.json" --no-window --log "$work/tiempo.log" > "$work/tiempo.out" 2> "$work/tiempo.err"
check_run "tiempo_cumplido" $? "$work/tiempo.out" "$work/tiempo.log" "$work/short.json"
created=$(grep -c '"orderCreated"' "$work/tiempo.log")
if [ "$created" -lt 5 ]; then
    echo "FALLO  tiempo_cumplido: solo $created pedidos creados en 2 minutos simulados"; failures=$((failures + 1))
fi
if ! grep -q '"queueFull"' "$work/tiempo.log"; then
    echo "FALLO  tiempo_cumplido: con libro de 5 se esperaba algun queueFull"; failures=$((failures + 1))
fi

# 1b. Ciclo completo: entregas, averias, liberaciones y rescates reales, y el
#     resumen cuadra con el log (lo comprueba check_log con --summary)
"$bin" "$work/full.json" --no-window --log "$work/full.log" > "$work/full.out" 2> "$work/full.err"
check_run "ciclo_completo" $? "$work/full.out" "$work/full.log" "$work/full.json"
for needle in orderDelivered courierBreakdown orderReassigned pickupStarted; do
    if ! grep -q "\"$needle\"" "$work/full.log"; then
        echo "FALLO  ciclo_completo: no aparece ningun $needle en 40 minutos simulados"; failures=$((failures + 1))
    fi
done
delivered=$(grep -c '"orderDelivered"' "$work/full.log")
if [ "$delivered" -lt 20 ]; then
    echo "FALLO  ciclo_completo: solo $delivered entregas en 40 minutos simulados"; failures=$((failures + 1))
fi
if ! grep -q '"urgent":true' "$work/full.log"; then
    echo "FALLO  ciclo_completo: con urgentProbability 0.3 se esperaban pedidos urgentes en el log"; failures=$((failures + 1))
fi
if grep -q " 0 rescates" "$work/full.err"; then
    echo "AVISO  ciclo_completo: sin rescates en esta corrida (hace falta una averia con pedidos ya recogidos)"
fi

# 2. Termina por SIGINT, en menos de 5 segundos reales
"$bin" "$work/forever.json" --no-window --log "$work/senal.log" > "$work/senal.out" 2> "$work/senal.err" &
pid=$!
sleep 1
start=$(date +%s)
kill -INT "$pid"
wait "$pid"
status=$?
elapsed=$(( $(date +%s) - start ))
check_run "sigint" $status "$work/senal.out" "$work/senal.log" "$work/forever.json"
if [ "$elapsed" -gt 5 ]; then
    echo "FALLO  sigint: tardo $elapsed s en salir, el limite es 5"; failures=$((failures + 1))
fi

# 3. Termina por SIGTERM
"$bin" "$work/forever.json" --no-window --log "$work/term.log" > "$work/term.out" 2> "$work/term.err" &
pid=$!
sleep 1
kill -TERM "$pid"
wait "$pid"
check_run "sigterm" $? "$work/term.out" "$work/term.log" "$work/forever.json"

if [ "$failures" -eq 0 ]; then
    echo "sim_tests: todas las pruebas pasaron"
    exit 0
fi
echo "sim_tests: $failures prueba(s) fallaron"
exit 1
