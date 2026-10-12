#!/usr/bin/env python3
"""Arma config/equipetrol.json a partir de la red de calles extraida.

Uso:
    python3 tools/make_config.py

Toma los nodos y calles de data/equipetrol_network.json, los limites del mapa
de data/equipetrol_bounds.json, y los restaurantes de la lista RESTAURANTS:
restaurantes reales de la Av. San Martin que aparecen entre los candidatos de
la red (por nombre), cada uno ubicado en el nodo mas cercano que pertenece a la
avenida. El punto de partida de la flota es el nodo de la avenida mas cercano a
su punto medio.

Los valores de flota, pedidos, despacho, incidentes y simulacion son los del
ejemplo de la seccion 5 del enunciado.

Solo usa la libreria estandar de Python.
"""

import json
import math
import os
import sys

NETWORK = "data/equipetrol_network.json"
BOUNDS = "data/equipetrol_bounds.json"
OUT = "config/equipetrol.json"
AVENUE = "Avenida San Martín"

# (nombre en OpenStreetMap, puestos de recogida, preparacion [min, max] en ms simulados)
RESTAURANTS = [
    ("Pizza Hut",         2, [300_000, 600_000]),
    ("Jardín de Asia",    3, [600_000, 1_200_000]),
    ("Bali Parrilla Bar", 2, [600_000, 1_080_000]),
    ("BOTANICA",          1, [480_000, 900_000]),
    ("Puerto Milanesa",   2, [420_000, 780_000]),
    ("Slatkis",           2, [360_000, 720_000]),
    ("Tony Roma's",       3, [600_000, 1_200_000]),
    ("Pícolo",            1, [420_000, 840_000]),
    ("RestoBar",          1, [480_000, 900_000]),
]

FLEET = {"couriers": 8, "bagCapacity": 3, "speedKmh": 30}
ORDERS = {"meanIntervalMs": 20_000, "burstMax": 12, "maxPending": 50, "seed": 42}
DISPATCH = {"quoteTimeoutMs": 200, "acceptTimeoutMs": 600_000}
INCIDENTS = {"breakdownProbability": 0.02}
SIMULATION = {"durationS": 3600, "timeScale": 60}


def haversine_m(lat1, lon1, lat2, lon2):
    p1, p2 = math.radians(lat1), math.radians(lat2)
    a = (math.sin((p2 - p1) / 2) ** 2
         + math.cos(p1) * math.cos(p2) * math.sin(math.radians(lon2 - lon1) / 2) ** 2)
    return 2 * 6_371_000.0 * math.asin(math.sqrt(a))


def main():
    with open(NETWORK) as f:
        net = json.load(f)
    with open(BOUNDS) as f:
        bounds = json.load(f)

    pos = {n["id"]: (n["lat"], n["lon"]) for n in net["nodes"]}
    avenue_nodes = set()
    for s in net["streets"]:
        if net["streetNames"].get(s["id"]) == AVENUE:
            avenue_nodes.add(s["from"])
            avenue_nodes.add(s["to"])
    if not avenue_nodes:
        sys.exit(f"no hay calles llamadas '{AVENUE}' en la red")

    def nearest_avenue_node(lat, lon):
        return min(avenue_nodes, key=lambda n: haversine_m(lat, lon, *pos[n]))

    candidates = {}
    for c in net["restaurantCandidates"]:
        candidates.setdefault(c["name"], c)  # el primero (mas cercano a la red) si hay repetidos

    restaurants = []
    used_nodes = set()
    for i, (name, slots, prep) in enumerate(RESTAURANTS):
        if name not in candidates:
            sys.exit(f"el restaurante '{name}' no esta entre los candidatos de {NETWORK}")
        c = candidates[name]
        node = nearest_avenue_node(c["lat"], c["lon"])
        if node in used_nodes:
            sys.exit(f"dos restaurantes caerian en el mismo nodo {node}: '{name}'")
        used_nodes.add(node)
        restaurants.append({"id": f"r{i}", "name": name, "node": node,
                            "pickupSlots": slots, "prepTimeMs": prep})

    # Punto de partida: nodo de la avenida mas cercano a su punto medio
    south = min(avenue_nodes, key=lambda n: pos[n][0])
    north = max(avenue_nodes, key=lambda n: pos[n][0])
    mid = ((pos[south][0] + pos[north][0]) / 2, (pos[south][1] + pos[north][1]) / 2)
    start = nearest_avenue_node(*mid)

    config = {
        "map": {
            "image": "../data/" + bounds["image"],
            "attribution": bounds["attribution"],
            "bounds": bounds["bounds"],
        },
        "nodes": net["nodes"],
        "streets": [{k: s[k] for k in ("id", "from", "to", "oneWay")} for s in net["streets"]],
        "restaurants": restaurants,
        "fleet": dict(FLEET, startNode=start),
        "orders": ORDERS,
        "dispatch": DISPATCH,
        "incidents": INCIDENTS,
        "simulation": SIMULATION,
    }

    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    with open(OUT, "w") as f:
        json.dump(config, f, indent=1, ensure_ascii=False)
        f.write("\n")

    print(f"{OUT}: {len(config['nodes'])} nodos, {len(config['streets'])} calles, "
          f"{len(restaurants)} restaurantes, startNode {start}", file=sys.stderr)
    for r in restaurants:
        print(f"  {r['id']}: {r['name']} en {r['node']}", file=sys.stderr)


if __name__ == "__main__":
    main()
