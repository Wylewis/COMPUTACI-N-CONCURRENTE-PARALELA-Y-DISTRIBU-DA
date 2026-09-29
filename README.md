# Equipetrol Delivery

Simulación concurrente de despacho de pedidos en la Zona Equipetrol, Santa Cruz de la Sierra.
Proyecto final del curso LIA-221 Computación Concurrente, Paralela y Distribuida.

## Compilación

```sh
mkdir build && cd build
cmake .. && make
./delivery_sim ../config/equipetrol.json
```

Con ThreadSanitizer:

```sh
cmake -DENABLE_TSAN=ON .. && make
```

## Dependencias

- cmake ≥ 3.11
- make
- g++ ≥ 7 o clang equivalente, con soporte C++17

## Mapa

La imagen de fondo `data/equipetrol.png` cubre el cuadrante noroeste de Santa Cruz de la
Sierra, desde el Segundo Anillo (Av. Cristóbal de Mendoza) hasta el Cuarto Anillo, con el
Tercer Anillo interno y externo y la Av. San Martín completa. Tamaño: 1771 × 1859 píxeles,
zoom 16 de OpenStreetMap.

Bounding box geográfico de la imagen (grados decimales, también en `data/equipetrol_bounds.json`):

| Lado  | Valor    |
|-------|----------|
| north | -17.746  |
| south | -17.784  |
| west  | -63.218  |
| east  | -63.180  |

La imagen se genera con `tools/make_map.py`, que solo necesita Python 3 (sin librerías
externas): descarga los tiles de OpenStreetMap que cubren el bounding box, los une y recorta
el resultado exactamente a esos límites, de modo que cualquier latitud y longitud dentro de
la zona se convierte a píxel con una interpolación lineal sobre los cuatro valores de arriba.

```sh
python3 tools/make_map.py --north -17.746 --south -17.784 --west -63.218 --east -63.180 \
    --zoom 16 --out data/equipetrol.png
```

## Atribución del mapa

Los datos y la imagen del mapa provienen de OpenStreetMap, publicados bajo la licencia ODbL.
© OpenStreetMap contributors
