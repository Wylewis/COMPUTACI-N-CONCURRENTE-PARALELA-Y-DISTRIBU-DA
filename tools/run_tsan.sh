#!/bin/sh
# Corrida completa bajo ThreadSanitizer, con la salida sin editar guardada en
# tools/reports/. Construye build/tsan si hace falta y corre:
#   1. la simulacion con rafagas y averias frecuentes hasta el fin por tiempo,
#   2. la misma con SIGINT a mitad,
#   3. la prueba de estres de averias simultaneas,
# y verifica cada log con check_log.py. Sale con 1 si ThreadSanitizer reporta
# algo o si algun paso falla.
#
# Uso: tools/run_tsan.sh [nombre del reporte]   (por defecto, tsan_<fecha>.txt)
set -u
root=$(cd "$(dirname "$0")/.." && pwd)
report="$root/tools/reports/${1:-tsan_$(date +%Y%m%d).txt}"
build="$root/build/tsan"
work=$(mktemp -d "${TMPDIR:-/tmp}/delivery_sim_tsan.XXXXXX")
trap 'rm -rf "$work"' EXIT
status=0

mkdir -p "$build" "$root/tools/reports"
( cd "$build" && cmake -DENABLE_TSAN=ON "$root" > "$work/cmake.txt" 2>&1 && make delivery_sim breakdown_stress_test > "$work/make.txt" 2>&1 ) || {
    echo "no se pudo construir con ThreadSanitizer:"; cat "$work/cmake.txt" "$work/make.txt"; exit 1; }

python3 - "$root/config/equipetrol.json" "$work/tsan.json" "$work/forever.json" <<'PY'
import json, sys
src, out, forever = sys.argv[1:4]
c = json.load(open(src))
c["map"]["image"] = src.rsplit("/", 1)[0] + "/../data/equipetrol.png"
c["orders"]["meanIntervalMs"] = 10000
c["orders"]["burstMax"] = 12
for r in c["restaurants"]:
    r["prepTimeMs"] = [30000, 90000]
c["incidents"]["breakdownProbability"] = 0.3
c["simulation"]["durationS"] = 1800
c["simulation"]["timeScale"] = 600
json.dump(c, open(out, "w"))
c["simulation"]["durationS"] = 0
json.dump(c, open(forever, "w"))
PY

{
    echo "ThreadSanitizer: corrida completa de delivery_sim"
    echo "fecha: $(date)"
    echo "maquina: $(uname -srm), $(sysctl -n machdep.cpu.brand_string 2>/dev/null || echo cpu desconocida), $(sysctl -n hw.ncpu 2>/dev/null) hilos"
    echo "compilador: $(cc --version | head -n 1)"
    echo "commit: $(cd "$root" && git rev-parse --short HEAD 2>/dev/null)"
    echo "config: config/equipetrol.json con pedidos cada 10 s, rafagas de hasta 12, preparacion 30-90 s, averias 0.3, 30 min simulados a 600x"
    echo "salida sin editar; solo las rutas del repositorio y del directorio temporal se abrevian como <repo> y <tmp>"
    echo
    echo "=== 1. fin por tiempo cumplido ==="
    "$build/delivery_sim" "$work/tsan.json" --no-window --log "$work/tiempo.log" 2>&1; echo "estado de salida: $?"
    python3 "$root/tools/check_log.py" "$work/tiempo.log" --config "$work/tsan.json" 2>&1 | tail -n 3
    echo
    echo "=== 2. SIGINT a mitad de la corrida ==="
    "$build/delivery_sim" "$work/forever.json" --no-window --log "$work/senal.log" > "$work/senal.out" 2>&1 &
    pid=$!
    sleep 2
    kill -INT "$pid"
    wait "$pid"; echo "estado de salida: $?"
    cat "$work/senal.out"
    python3 "$root/tools/check_log.py" "$work/senal.log" --config "$work/forever.json" 2>&1 | tail -n 3
    echo
    echo "=== 3. estres de averias simultaneas ==="
    "$build/breakdown_stress_test" "$work/stress.log" 2>&1; echo "estado de salida: $?"
    python3 "$root/tools/check_log.py" "$work/stress.log" 2>&1 | tail -n 3
    echo
    echo "=== resumen ==="
} > "$report" 2>&1

# Las rutas de la maquina no aportan nada y no deben publicarse: se abrevian.
sed -i.bak -e "s|$root|<repo>|g" -e "s|$work|<tmp>|g" "$report" && rm -f "$report.bak"

warnings=$(grep -c "WARNING: ThreadSanitizer" "$report")
bad=$(grep -c "estado de salida: [^0]" "$report")
errs=$(grep -c "check_log: [1-9]" "$report")
{
    echo "reportes de ThreadSanitizer: $warnings"
    echo "salidas con estado distinto de 0: $bad"
    echo "logs con errores del verificador: $errs"
} >> "$report"
echo "reporte: $report"
tail -n 3 "$report"
[ "$warnings" -eq 0 ] && [ "$bad" -eq 0 ] && [ "$errs" -eq 0 ] || status=1
exit $status
