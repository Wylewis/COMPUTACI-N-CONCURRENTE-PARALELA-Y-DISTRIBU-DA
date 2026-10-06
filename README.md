# Equipetrol Delivery

Simulación concurrente de despacho de pedidos en la Zona Equipetrol, Santa Cruz de la Sierra.
Proyecto final del curso LIA-221 Computación Concurrente, Paralela y Distribuida.

## Compilación

```sh
mkdir build && cd build
cmake .. && make
./delivery_sim ../config/equipetrol.json
```

El único flag opcional es `--log <ruta>`, que cambia el archivo del log de eventos (por defecto `events.log`).

### Validación del archivo de configuración

El programa rechaza el archivo, con un mensaje en stderr y estado de salida distinto de cero y sin
iniciar ninguna simulación, cuando:

- falta el argumento, el archivo no se puede leer o el JSON está mal formado;
- falta una propiedad obligatoria o tiene un tipo inválido;
- `nodes` está vacío, o una calle, un restaurante o `fleet.startNode` apunta a un nodo que no existe;
- un valor sale del rango que declara la tabla de la sección 5: `fleet.couriers`, `fleet.bagCapacity`,
  `orders.burstMax` y `pickupSlots` deben ser ≥ 1, y `breakdownProbability` estar entre 0 y 1;
- `prepTimeMs` no es un array `[min, max]`;
- la imagen `map.image` no se puede leer.

No se comprueba nada más, para no rechazar archivos válidos que sigan el formato del enunciado.
Un archivo mínimo y válido está en `tests/config/minimal.json`; los casos inválidos están en
`tests/invalid/` y `ctest` los ejecuta con `tests/run_config_tests.sh`.

Con ThreadSanitizer:

```sh
cmake -DENABLE_TSAN=ON .. && make
```

Pruebas unitarias (se construyen con `make` dentro de `build/`):

```sh
ctest --output-on-failure
```

## Dependencias

- cmake ≥ 3.11
- make
- g++ ≥ 7 o clang equivalente, con soporte C++17
- nlohmann/json 3.12.0 (incluida en `third_party/nlohmann/`, licencia MIT; no requiere instalación)

## Mapa

La zona de trabajo de la simulación es la franja de Equipetrol a lo largo de la Av. San
Martín, desde el Segundo Anillo (Av. Cristóbal de Mendoza) hasta el Cuarto Anillo (Av.
Antonio Vaca Díez), cruzada por el Tercer Anillo (Av. Noel Kempff Mercado) y limitada al
este por la Av. La Salle. Está definida por un polígono de 58 vértices en `data/equipetrol_zone.json`.

La imagen de fondo `data/equipetrol.png` es el recorte de OpenStreetMap a ese polígono:
todo lo que queda fuera de la zona está pintado de blanco. Tamaño: 712 × 991 píxeles,
zoom 16.

Bounding box geográfico de la imagen, igual al del polígono (grados decimales, también en
`data/equipetrol_bounds.json`):

| Lado  | Valor       |
|-------|-------------|
| north | -17.752704  |
| south | -17.772971  |
| west  | -63.203708  |
| east  | -63.188420  |

La imagen se genera con `tools/make_map.py`, que solo necesita Python 3 (sin librerías
externas): descarga los tiles de OpenStreetMap que cubren el bounding box del polígono, los
une, recorta el resultado exactamente a esos límites y pinta de blanco el exterior del
polígono. Cualquier latitud y longitud dentro de la zona se convierte a píxel con una
interpolación lineal sobre los cuatro valores de arriba.

```sh
python3 tools/make_map.py --zone data/equipetrol_zone.json --zoom 16 --out data/equipetrol.png
```

## Atribución del mapa

Los datos y la imagen del mapa provienen de OpenStreetMap, publicados bajo la licencia ODbL.
© OpenStreetMap contributors
