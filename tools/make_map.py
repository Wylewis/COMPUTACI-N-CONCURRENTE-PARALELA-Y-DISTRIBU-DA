#!/usr/bin/env python3
"""Genera la imagen de fondo del mapa a partir de tiles de OpenStreetMap.

Uso:
    python3 tools/make_map.py --north -17.745 --south -17.776 \
        --west -63.215 --east -63.184 --zoom 16 --out data/equipetrol.png

    python3 tools/make_map.py --zone data/equipetrol_zone.json --zoom 16 \
        --out data/equipetrol.png

Descarga los tiles que cubren el bounding box, los une y recorta la imagen
exactamente al bounding box pedido, de modo que los limites de la imagen
coinciden con los valores --north/--south/--west/--east. Esos cuatro valores
son los que van en map.bounds del archivo de configuracion.

Con --zone se lee un poligono de vertices [lat, lon]: el bounding box se toma
del poligono (salvo que se pasen los cuatro limites a mano) y todo lo que queda
fuera del poligono se pinta de blanco. Asi la zona de trabajo es exactamente
la dibujada.

Solo usa la libreria estandar de Python (sin Pillow ni requests): el PNG se
decodifica y se codifica a mano.

Datos: (c) OpenStreetMap contributors, licencia ODbL.
Politica de uso de tiles: https://operations.osmfoundation.org/policies/tiles/
"""

import argparse
import json
import math
import os
import struct
import sys
import time
import urllib.request
import zlib

TILE_URL = "https://tile.openstreetmap.org/{z}/{x}/{y}.png"
TILE_SIZE = 256
USER_AGENT = "delivery-sim-student-project/0.1 (LIA-221 course project)"


# ---------------------------------------------------------------------------
# Matematica de tiles (proyeccion Web Mercator, "slippy map")
# ---------------------------------------------------------------------------

def lonlat_to_tile_xy(lon, lat, zoom):
    """Coordenadas fraccionarias de tile para (lon, lat) en un zoom dado."""
    n = 2 ** zoom
    x = (lon + 180.0) / 360.0 * n
    lat_rad = math.radians(lat)
    y = (1.0 - math.log(math.tan(lat_rad) + 1.0 / math.cos(lat_rad)) / math.pi) / 2.0 * n
    return x, y


# ---------------------------------------------------------------------------
# Decodificacion minima de PNG (suficiente para los tiles de OSM)
# ---------------------------------------------------------------------------

def _paeth(a, b, c):
    p = a + b - c
    pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
    if pa <= pb and pa <= pc:
        return a
    if pb <= pc:
        return b
    return c


def decode_png(data):
    """Devuelve (ancho, alto, filas) con cada fila como bytearray RGB."""
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError("no es un PNG")
    pos = 8
    width = height = None
    color_type = bit_depth = interlace = None
    palette = None
    idat = bytearray()
    while pos < len(data):
        length = struct.unpack(">I", data[pos:pos + 4])[0]
        ctype = data[pos + 4:pos + 8]
        body = data[pos + 8:pos + 8 + length]
        pos += 12 + length
        if ctype == b"IHDR":
            width, height, bit_depth, color_type, _, _, interlace = struct.unpack(">IIBBBBB", body)
        elif ctype == b"PLTE":
            palette = [tuple(body[i:i + 3]) for i in range(0, len(body), 3)]
        elif ctype == b"IDAT":
            idat += body
        elif ctype == b"IEND":
            break
    if bit_depth != 8:
        raise ValueError(f"profundidad de bits no soportada: {bit_depth}")
    if interlace != 0:
        raise ValueError("PNG entrelazado no soportado")
    channels = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}[color_type]
    raw = zlib.decompress(bytes(idat))
    stride = width * channels
    rows = []
    prev = bytearray(stride)
    offset = 0
    for _ in range(height):
        filter_type = raw[offset]
        offset += 1
        cur = bytearray(raw[offset:offset + stride])
        offset += stride
        bpp = channels
        if filter_type == 1:
            for i in range(bpp, stride):
                cur[i] = (cur[i] + cur[i - bpp]) & 0xFF
        elif filter_type == 2:
            for i in range(stride):
                cur[i] = (cur[i] + prev[i]) & 0xFF
        elif filter_type == 3:
            for i in range(stride):
                left = cur[i - bpp] if i >= bpp else 0
                cur[i] = (cur[i] + ((left + prev[i]) >> 1)) & 0xFF
        elif filter_type == 4:
            for i in range(stride):
                left = cur[i - bpp] if i >= bpp else 0
                up_left = prev[i - bpp] if i >= bpp else 0
                cur[i] = (cur[i] + _paeth(left, prev[i], up_left)) & 0xFF
        elif filter_type != 0:
            raise ValueError(f"filtro PNG desconocido: {filter_type}")
        prev = cur
        # Convertir la fila a RGB
        if color_type == 2:
            rgb = cur
        elif color_type == 6:
            rgb = bytearray(width * 3)
            for i in range(width):
                rgb[3 * i:3 * i + 3] = cur[4 * i:4 * i + 3]
        elif color_type == 3:
            rgb = bytearray(width * 3)
            for i in range(width):
                rgb[3 * i:3 * i + 3] = palette[cur[i]]
        elif color_type == 0:
            rgb = bytearray(width * 3)
            for i in range(width):
                rgb[3 * i] = rgb[3 * i + 1] = rgb[3 * i + 2] = cur[i]
        elif color_type == 4:
            rgb = bytearray(width * 3)
            for i in range(width):
                rgb[3 * i] = rgb[3 * i + 1] = rgb[3 * i + 2] = cur[2 * i]
        rows.append(rgb)
    return width, height, rows


def encode_png(width, height, rows):
    """Codifica filas RGB (bytearray de 3*width) en un PNG."""
    def chunk(ctype, body):
        return (struct.pack(">I", len(body)) + ctype + body
                + struct.pack(">I", zlib.crc32(ctype + body) & 0xFFFFFFFF))

    raw = bytearray()
    for row in rows:
        raw.append(0)  # filtro None
        raw += row
    ihdr = struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", ihdr)
            + chunk(b"IDAT", zlib.compress(bytes(raw), 9)) + chunk(b"IEND", b""))


# ---------------------------------------------------------------------------
# Descarga de tiles con cache local
# ---------------------------------------------------------------------------

def fetch_tile(z, x, y, cache_dir):
    path = os.path.join(cache_dir, f"{z}_{x}_{y}.png")
    if os.path.exists(path):
        with open(path, "rb") as f:
            return f.read()
    req = urllib.request.Request(TILE_URL.format(z=z, x=x, y=y),
                                 headers={"User-Agent": USER_AGENT})
    with urllib.request.urlopen(req, timeout=30) as resp:
        data = resp.read()
    os.makedirs(cache_dir, exist_ok=True)
    with open(path, "wb") as f:
        f.write(data)
    time.sleep(0.2)  # ser amable con el servidor de tiles
    return data


# ---------------------------------------------------------------------------
# Recorte por poligono
# ---------------------------------------------------------------------------

def apply_zone_mask(rows, width, height, polygon_px, fill=(255, 255, 255)):
    """Pinta con `fill` todo pixel cuyo centro queda fuera del poligono.

    polygon_px: lista de vertices (x, y) en pixeles de la imagen recortada.
    Relleno por barrido de lineas (scanline): para cada fila se calculan las
    intersecciones con las aristas y se conserva lo que queda entre pares.
    """
    n = len(polygon_px)
    fill_row = bytearray(fill) * width
    for y in range(height):
        cy = y + 0.5
        xs = []
        for i in range(n):
            (x1, y1), (x2, y2) = polygon_px[i], polygon_px[(i + 1) % n]
            if (y1 <= cy < y2) or (y2 <= cy < y1):
                xs.append(x1 + (cy - y1) * (x2 - x1) / (y2 - y1))
        xs.sort()
        new = bytearray(fill_row)
        for j in range(0, len(xs) - 1, 2):
            a = max(0, int(math.ceil(xs[j] - 0.5)))
            b = min(width, int(math.floor(xs[j + 1] + 0.5)))
            if b > a:
                new[3 * a:3 * b] = rows[y][3 * a:3 * b]
        rows[y] = new


# ---------------------------------------------------------------------------
# Programa principal
# ---------------------------------------------------------------------------

def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--north", type=float)
    ap.add_argument("--south", type=float)
    ap.add_argument("--west", type=float)
    ap.add_argument("--east", type=float)
    ap.add_argument("--zone", help="JSON con 'polygon': [[lat, lon], ...]")
    ap.add_argument("--zoom", type=int, default=16)
    ap.add_argument("--out", default="data/equipetrol.png")
    ap.add_argument("--cache", default="tools/tile_cache")
    args = ap.parse_args()

    polygon = None
    if args.zone:
        with open(args.zone) as f:
            polygon = json.load(f)["polygon"]
        lats = [p[0] for p in polygon]
        lons = [p[1] for p in polygon]
        if args.north is None:
            args.north = max(lats)
        if args.south is None:
            args.south = min(lats)
        if args.west is None:
            args.west = min(lons)
        if args.east is None:
            args.east = max(lons)

    if None in (args.north, args.south, args.west, args.east):
        sys.exit("faltan limites: pasar --north/--south/--west/--east o --zone")
    if not (args.south < args.north and args.west < args.east):
        sys.exit("bounding box invalido: se requiere south < north y west < east")

    z = args.zoom
    x0f, y0f = lonlat_to_tile_xy(args.west, args.north, z)   # esquina noroeste
    x1f, y1f = lonlat_to_tile_xy(args.east, args.south, z)   # esquina sureste
    tx0, ty0 = int(math.floor(x0f)), int(math.floor(y0f))
    tx1, ty1 = int(math.floor(x1f)), int(math.floor(y1f))
    n_tx, n_ty = tx1 - tx0 + 1, ty1 - ty0 + 1
    print(f"zoom {z}: {n_tx} x {n_ty} tiles = {n_tx * n_ty}", file=sys.stderr)

    mosaic_w, mosaic_h = n_tx * TILE_SIZE, n_ty * TILE_SIZE
    mosaic = [bytearray(mosaic_w * 3) for _ in range(mosaic_h)]

    for ty in range(ty0, ty1 + 1):
        for tx in range(tx0, tx1 + 1):
            print(f"  tile {z}/{tx}/{ty}", file=sys.stderr)
            w, h, rows = decode_png(fetch_tile(z, tx, ty, args.cache))
            if (w, h) != (TILE_SIZE, TILE_SIZE):
                sys.exit(f"tile de tamano inesperado: {w}x{h}")
            ox, oy = (tx - tx0) * TILE_SIZE, (ty - ty0) * TILE_SIZE
            for r in range(h):
                mosaic[oy + r][ox * 3:(ox + w) * 3] = rows[r]

    # Recorte exacto al bounding box pedido
    px0 = int(round((x0f - tx0) * TILE_SIZE))
    py0 = int(round((y0f - ty0) * TILE_SIZE))
    px1 = int(round((x1f - tx0) * TILE_SIZE))
    py1 = int(round((y1f - ty0) * TILE_SIZE))
    out_w, out_h = px1 - px0, py1 - py0
    cropped = [mosaic[y][px0 * 3:px1 * 3] for y in range(py0, py1)]

    if polygon:
        # Vertices del poligono en pixeles de la imagen recortada
        polygon_px = []
        for lat, lon in polygon:
            fx, fy = lonlat_to_tile_xy(lon, lat, z)
            polygon_px.append(((fx - tx0) * TILE_SIZE - px0, (fy - ty0) * TILE_SIZE - py0))
        apply_zone_mask(cropped, out_w, out_h, polygon_px)
        print(f"zona: {len(polygon)} vertices, exterior pintado de blanco", file=sys.stderr)

    os.makedirs(os.path.dirname(args.out) or ".", exist_ok=True)
    with open(args.out, "wb") as f:
        f.write(encode_png(out_w, out_h, cropped))

    print(f"imagen: {args.out} ({out_w} x {out_h} px)", file=sys.stderr)
    print("map.bounds para el archivo de configuracion:")
    print(f'  {{ "north": {args.north}, "south": {args.south}, '
          f'"west": {args.west}, "east": {args.east} }}')


if __name__ == "__main__":
    main()
