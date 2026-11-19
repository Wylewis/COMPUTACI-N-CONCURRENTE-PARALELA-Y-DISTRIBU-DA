#!/usr/bin/env python3
"""Arma la configuracion de ciudad para las mediciones de GPU.

Uso:
    python3 tools/make_city.py

Descarga de OpenStreetMap un rectangulo de unos 6 x 6 km de Santa Cruz de la
Sierra (del centro al Cuarto Anillo norte, con Equipetrol adentro) en cuatro
cuadrantes, porque la API `map` no devuelve mas de 50 000 nodos por llamada, y
construye la red con las mismas funciones que tools/extract_network.py. La zona
de la simulacion es ese rectangulo con un margen de unos 200 m hacia adentro.
Se queda con la componente fuertemente conexa mayor, asi todo par de nodos
tiene camino respetando los sentidos.

Escribe config/ciudad.json: varios miles de intersecciones, los restaurantes
reales de la zona (cada uno en un nodo distinto), una flota de miles de motos y
pedidos con rafagas grandes. La imagen del mapa se genera aparte con
tools/make_map.py usando los mismos limites.

Las respuestas crudas quedan en tools/osm_cache/ para repetir sin red.
Solo usa la libreria estandar de Python.
Datos: (c) OpenStreetMap contributors, licencia ODbL.
"""

import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import extract_network as osm  # noqa: E402

DOWNLOAD_NORTH, DOWNLOAD_SOUTH, DOWNLOAD_WEST, DOWNLOAD_EAST = -17.745, -17.800, -63.215, -63.160
NORTH, SOUTH, WEST, EAST = -17.747, -17.798, -63.213, -63.162
TILES = 2
OUT = "config/ciudad.json"
IMAGE = "ciudad.png"
MAX_RESTAURANTS = 200

FLEET = {"couriers": 3000, "bagCapacity": 3, "speedKmh": 30}
ORDERS = {"meanIntervalMs": 4_000, "burstMax": 400, "maxPending": 4000, "seed": 42}
DISPATCH = {"quoteTimeoutMs": 200, "acceptTimeoutMs": 600_000}
INCIDENTS = {"breakdownProbability": 0.02}
SIMULATION = {"durationS": 600, "timeScale": 60}


def main():
    coords, node_tags, ways_by_id = {}, {}, {}
    dlat, dlon = (DOWNLOAD_NORTH - DOWNLOAD_SOUTH) / TILES, (DOWNLOAD_EAST - DOWNLOAD_WEST) / TILES
    for r in range(TILES):
        for c in range(TILES):
            south, west = DOWNLOAD_SOUTH + r * dlat, DOWNLOAD_WEST + c * dlon
            tile = [[south + dlat, west], [south + dlat, west + dlon], [south, west + dlon], [south, west]]
            print(f"cuadrante {r},{c}...", file=sys.stderr)
            root = osm.fetch_map(tile, f"tools/osm_cache/ciudad_{r}{c}.xml")
            tc, tt, tw = osm.parse_osm(root)
            coords.update(tc)
            node_tags.update(tt)
            for w in tw:
                ways_by_id[w["id"]] = w
    ways = list(ways_by_id.values())
    print(f"  {len(coords)} nodos y {len(ways)} vias en el rectangulo", file=sys.stderr)

    polygon = [[NORTH, WEST], [NORTH, EAST], [SOUTH, EAST], [SOUTH, WEST]]
    nodes, streets, _, stats = osm.build_network(coords, ways, polygon, 12.0, True)
    candidates = osm.restaurant_candidates(coords, node_tags, ways, polygon, nodes)

    restaurants, used = [], set()
    for cand in candidates:
        if cand["nearestNode"] in used:
            continue
        used.add(cand["nearestNode"])
        i = len(restaurants)
        restaurants.append({"id": f"r{i}", "name": cand["name"], "node": cand["nearestNode"],
                            "pickupSlots": 1 + i % 3, "prepTimeMs": [300_000, 900_000]})
        if len(restaurants) == MAX_RESTAURANTS:
            break

    mid_lat, mid_lon = (NORTH + SOUTH) / 2, (EAST + WEST) / 2
    start = min(nodes, key=lambda n: osm.haversine_m(mid_lat, mid_lon, n["lat"], n["lon"]))["id"]

    config = {
        "map": {"image": "../data/" + IMAGE, "attribution": "© OpenStreetMap contributors",
                "bounds": {"north": NORTH, "south": SOUTH, "west": WEST, "east": EAST}},
        "nodes": nodes,
        "streets": streets,
        "restaurants": restaurants,
        "fleet": dict(FLEET, startNode=start),
        "orders": ORDERS,
        "dispatch": DISPATCH,
        "incidents": INCIDENTS,
        "simulation": SIMULATION,
    }
    with open(OUT, "w") as f:
        json.dump(config, f, separators=(",", ":"), ensure_ascii=False)
        f.write("\n")
    print(f"{OUT}: {len(nodes)} nodos, {len(streets)} calles ({stats['oneWayStreets']} de un sentido), "
          f"{len(restaurants)} restaurantes, {FLEET['couriers']} motos, startNode {start}", file=sys.stderr)
    print(f"imagen: python3 tools/make_map.py --north {NORTH} --south {SOUTH} --west {WEST} --east {EAST} "
          f"--zoom 15 --out data/{IMAGE}", file=sys.stderr)


if __name__ == "__main__":
    main()
