#!/bin/sh
# Corrida de memoria en Linux dentro de Docker: AddressSanitizer con
# LeakSanitizer (que no existe en macOS con Apple Silicon). Construye la
# imagen tools/docker/Dockerfile.asan si hace falta, compila el proyecto
# con -DENABLE_ASAN=ON dentro del contenedor, corre toda la suite de ctest
# y los mismos tres pasos que tools/run_tsan.sh, y guarda la salida sin
# editar en tools/reports/asan_linux_<fecha>.txt. Sale con 1 si el sanitizer
# reporta errores o fugas, o si algun paso falla.
#
# Uso: tools/run_asan_linux.sh [nombre del reporte]
set -u
root=$(cd "$(dirname "$0")/.." && pwd)
report="$root/tools/reports/${1:-asan_linux_$(date +%Y%m%d).txt}"
image=delivery-sim-asan
mkdir -p "$root/tools/reports"

docker image inspect "$image" > /dev/null 2>&1 || docker build -q -t "$image" -f "$root/tools/docker/Dockerfile.asan" "$root/tools/docker" > /dev/null || {
    echo "no se pudo construir la imagen $image"; exit 1; }

# Todo lo que corre en el contenedor; el repositorio se monta en solo lectura.
cat > "$root/tools/reports/.asan_inner.sh" <<'INNER'
set -u
export ASAN_OPTIONS=detect_leaks=1:abort_on_error=0
export LSAN_OPTIONS=suppressions=/dev/null
echo "AddressSanitizer + LeakSanitizer: corrida completa de delivery_sim en Linux"
echo "fecha: $(date)"
echo "maquina: $(uname -srm) (contenedor Docker, $(nproc) hilos)"
echo "compilador: $(g++ --version | head -n 1); $(cmake --version | head -n 1)"
echo "commit: $(cd /src && git rev-parse --short HEAD 2>/dev/null)"
echo "salida sin editar; solo las rutas del directorio temporal se abrevian como <tmp>"
echo
mkdir -p /tmp/build && cd /tmp/build
cmake -DENABLE_ASAN=ON /src > /tmp/cmake.txt 2>&1 || { cat /tmp/cmake.txt; exit 1; }
make -j"$(nproc)" > /tmp/make.txt 2>&1 || { tail -n 40 /tmp/make.txt; exit 1; }
echo "=== 0. suite de pruebas (ctest) bajo AddressSanitizer ==="
ctest --output-on-failure 2>&1 | tail -n 40; echo "estado de salida: $?"
echo
python3 - /src/config/equipetrol.json /tmp/asan.json /tmp/forever.json <<'PY'
import json, sys
src, out, forever = sys.argv[1:4]
c = json.load(open(src))
c["map"]["image"] = "/src/data/equipetrol.png"
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
echo "=== 1. fin por tiempo cumplido ==="
./delivery_sim /tmp/asan.json --no-window --log /tmp/tiempo.log 2>&1; echo "estado de salida: $?"
python3 /src/tools/check_log.py /tmp/tiempo.log --config /tmp/asan.json 2>&1 | tail -n 3
echo
echo "=== 2. SIGINT a mitad de la corrida ==="
./delivery_sim /tmp/forever.json --no-window --log /tmp/senal.log > /tmp/senal.out 2>&1 &
pid=$!
sleep 2
kill -INT "$pid"
wait "$pid"; echo "estado de salida: $?"
cat /tmp/senal.out
python3 /src/tools/check_log.py /tmp/senal.log --config /tmp/forever.json 2>&1 | tail -n 3
echo
echo "=== 3. estres de averias simultaneas ==="
./breakdown_stress_test /tmp/stress.log 2>&1; echo "estado de salida: $?"
python3 /src/tools/check_log.py /tmp/stress.log 2>&1 | tail -n 3
echo
echo "=== resumen ==="
INNER
docker run --rm -v "$root":/src:ro -v "$root/tools/reports/.asan_inner.sh":/inner.sh:ro "$image" sh /inner.sh > "$report" 2>&1
rm -f "$root/tools/reports/.asan_inner.sh"
sed -i.bak -e "s|/tmp/|<tmp>/|g" "$report" && rm -f "$report.bak"

errors=$(grep -c "ERROR: AddressSanitizer\|ERROR: LeakSanitizer" "$report")
bad=$(grep -c "estado de salida: [^0]" "$report")
errs=$(grep -c "check_log: [1-9]" "$report")
{
    echo "errores de AddressSanitizer o LeakSanitizer: $errors"
    echo "salidas con estado distinto de 0: $bad"
    echo "logs con errores del verificador: $errs"
} >> "$report"
echo "reporte: $report"
tail -n 3 "$report"
[ "$errors" -eq 0 ] && [ "$bad" -eq 0 ] && [ "$errs" -eq 0 ]
