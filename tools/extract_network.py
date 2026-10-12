#!/usr/bin/env python3
"""Extrae la red de calles de la zona de trabajo desde OpenStreetMap.

Uso:
    python3 tools/extract_network.py --zone data/equipetrol_zone.json \
        --out data/equipetrol_network.json

Descarga de la API de OpenStreetMap (llamada `map`) todos los elementos del
rectangulo envolvente del poligono de la zona, se queda con las vias
transitables en moto cuyos nodos caen dentro del poligono, y produce:

  - nodes:   intersecciones (y quiebres de calles curvas) con lat/lon, en el
             formato de la seccion 5 del enunciado;
  - streets: segmentos entre nodos consecutivos, con oneWay segun la etiqueta
             `oneway` de OpenStreetMap (y rotondas, siempre de un solo sentido);
  - restaurantCandidates: restaurantes reales de la zona, con su nodo mas
             cercano, para elegir los del archivo de configuracion.

Solo se conserva la componente conexa mas grande, asi todo nodo es alcanzable
desde cualquier otro ignorando sentidos. La respuesta cruda de la API se guarda
en tools/osm_cache/ para poder repetir la extraccion sin red.

Solo usa la libreria estandar de Python.
Datos: (c) OpenStreetMap contributors, licencia ODbL.
"""

import argparse
import json
import math
import os
import sys
import urllib.request
import xml.etree.ElementTree as ET
from collections import defaultdict, deque

OSM_API_MAP = "https://api.openstreetmap.org/api/0.6/map?bbox={west},{south},{east},{north}"
USER_AGENT = "delivery-sim-student-project/0.1 (LIA-221 course project)"

# Tipos de via por los que circula una motocicleta de reparto.
HIGHWAY_TYPES = {"trunk", "trunk_link", "primary", "primary_link", "secondary", "secondary_link",
                 "tertiary", "tertiary_link", "residential", "unclassified", "living_street"}
RESTAURANT_TYPES = {"restaurant", "fast_food"}

EARTH_RADIUS_M = 6_371_000.0


# ---------------------------------------------------------------------------
# Geometria
# ---------------------------------------------------------------------------

def haversine_m(lat1, lon1, lat2, lon2):
    p1, p2 = math.radians(lat1), math.radians(lat2)
    dp = p2 - p1
    dl = math.radians(lon2 - lon1)
    a = math.sin(dp / 2) ** 2 + math.cos(p1) * math.cos(p2) * math.sin(dl / 2) ** 2
    return 2 * EARTH_RADIUS_M * math.asin(math.sqrt(a))


def point_in_polygon(lat, lon, polygon):
    """Ray casting. polygon: lista de [lat, lon]."""
    inside = False
    n = len(polygon)
    for i in range(n):
        lat1, lon1 = polygon[i]
        lat2, lon2 = polygon[(i + 1) % n]
        if (lat1 > lat) != (lat2 > lat):
            x = lon1 + (lat - lat1) * (lon2 - lon1) / (lat2 - lat1)
            if lon < x:
                inside = not inside
    return inside


def local_xy(lat, lon, lat0):
    """Proyeccion equirectangular local, en metros."""
    k = math.pi / 180.0 * EARTH_RADIUS_M
    return (lon * k * math.cos(math.radians(lat0)), lat * k)


def douglas_peucker(points, eps_m, lat0):
    """Indices de los puntos a conservar en una polilinea [(lat, lon), ...]."""
    if len(points) < 3:
        return list(range(len(points)))
    xy = [local_xy(lat, lon, lat0) for lat, lon in points]

    def rec(a, b, keep):
        (x1, y1), (x2, y2) = xy[a], xy[b]
        dx, dy = x2 - x1, y2 - y1
        length = math.hypot(dx, dy) or 1e-9
        best, bi = 0.0, a
        for i in range(a + 1, b):
            px, py = xy[i]
            d = abs(dy * px - dx * py + x2 * y1 - y2 * x1) / length
            if d > best:
                best, bi = d, i
        if best > eps_m:
            rec(a, bi, keep)
            keep.append(bi)
            rec(bi, b, keep)

    keep = [0, len(points) - 1]
    rec(0, len(points) - 1, keep)
    return sorted(set(keep))


# ---------------------------------------------------------------------------
# Descarga y lectura de OpenStreetMap
# ---------------------------------------------------------------------------

def fetch_map(polygon, cache_path):
    """Descarga (o lee de la cache) el XML de la API `map` para el rectangulo envolvente."""
    if not os.path.exists(cache_path):
        lats = [p[0] for p in polygon]
        lons = [p[1] for p in polygon]
        url = OSM_API_MAP.format(west=min(lons), south=min(lats), east=max(lons), north=max(lats))
        req = urllib.request.Request(url, headers={"User-Agent": USER_AGENT})
        with urllib.request.urlopen(req, timeout=180) as resp:
            data = resp.read()
        os.makedirs(os.path.dirname(cache_path) or ".", exist_ok=True)
        with open(cache_path, "wb") as f:
            f.write(data)
    return ET.parse(cache_path).getroot()


def parse_osm(root):
    """Devuelve (coords, ways): coords[id] = (lat, lon); ways = [{id, nodes, tags}]."""
    coords = {}
    node_tags = {}
    for n in root.findall("node"):
        nid = int(n.get("id"))
        coords[nid] = (float(n.get("lat")), float(n.get("lon")))
        tags = {t.get("k"): t.get("v") for t in n.findall("tag")}
        if tags:
            node_tags[nid] = tags
    ways = []
    for w in root.findall("way"):
        ways.append({
            "id": int(w.get("id")),
            "nodes": [int(nd.get("ref")) for nd in w.findall("nd")],
            "tags": {t.get("k"): t.get("v") for t in w.findall("tag")},
        })
    return coords, node_tags, ways


# ---------------------------------------------------------------------------
# Construccion de la red
# ---------------------------------------------------------------------------

def is_oneway(tags):
    """Devuelve 'forward', 'backward' o None."""
    if tags.get("junction") in ("roundabout", "circular"):
        return "forward"
    v = tags.get("oneway", "no")
    if v in ("yes", "true", "1"):
        return "forward"
    if v == "-1":
        return "backward"
    return None


def build_network(coords, ways, polygon, tolerance_m, prune_unreachable=False):
    highways = [w for w in ways if w["tags"].get("highway") in HIGHWAY_TYPES]
    lat0 = sum(p[0] for p in polygon) / len(polygon)
    inside = {}

    def is_inside(nid):
        if nid not in inside:
            lat, lon = coords[nid]
            inside[nid] = point_in_polygon(lat, lon, polygon)
        return inside[nid]

    # 1. Tramos de cada via que caen dentro del poligono (nodos consecutivos adentro)
    runs = []  # (way_id, [node ids], tags)
    for w in highways:
        current = []
        for nid in w["nodes"]:
            if nid in coords and is_inside(nid):
                current.append(nid)
            else:
                if len(current) >= 2:
                    runs.append((w["id"], current, w["tags"]))
                current = []
        if len(current) >= 2:
            runs.append((w["id"], current, w["tags"]))

    # 2. Intersecciones: nodos que aparecen en mas de un tramo, mas los extremos
    usage = defaultdict(int)
    for _, nodes, _ in runs:
        for nid in set(nodes):
            usage[nid] += 1
    key = set()
    for _, nodes, _ in runs:
        key.add(nodes[0])
        key.add(nodes[-1])
        for nid in nodes:
            if usage[nid] >= 2:
                key.add(nid)

    # 3. Quiebres: entre dos nodos clave consecutivos, conservar los puntos donde
    #    la calle se aparta mas de `tolerance_m` de la recta
    for _, nodes, _ in runs:
        anchors = [i for i, nid in enumerate(nodes) if nid in key]
        for a, b in zip(anchors, anchors[1:]):
            sub = nodes[a:b + 1]
            for i in douglas_peucker([coords[n] for n in sub], tolerance_m, lat0):
                key.add(sub[i])

    # 4. Segmentos entre nodos clave consecutivos
    streets = []
    seen = set()
    for way_id, nodes, tags in runs:
        direction = is_oneway(tags)
        anchors = [nid for nid in nodes if nid in key]
        for k, (a, b) in enumerate(zip(anchors, anchors[1:])):
            if a == b:
                continue
            frm, to = (b, a) if direction == "backward" else (a, b)
            sig = (frm, to) if direction else tuple(sorted((frm, to)))
            if sig in seen:
                continue
            seen.add(sig)
            streets.append({"id": f"w{way_id}_{k}", "from": frm, "to": to,
                            "oneWay": direction is not None, "name": tags.get("name", "")})

    # 5. Componente conexa mas grande (ignorando sentidos)
    adj = defaultdict(set)
    for s in streets:
        adj[s["from"]].add(s["to"])
        adj[s["to"]].add(s["from"])
    visited = set()
    best = set()
    for start in adj:
        if start in visited:
            continue
        comp = set()
        q = deque([start])
        visited.add(start)
        while q:
            n = q.popleft()
            comp.add(n)
            for m in adj[n]:
                if m not in visited:
                    visited.add(m)
                    q.append(m)
        if len(comp) > len(best):
            best = comp
    dropped_nodes = len(adj) - len(best)
    streets = [s for s in streets if s["from"] in best and s["to"] in best]

    # 6. Componente fuertemente conexa mas grande (respetando sentidos): nodos
    #    desde los que se puede ir a cualquier otro y volver. Los que quedan
    #    fuera son callejones de un solo sentido cortados por el borde de la zona.
    strong = largest_strong_component(best, streets)
    dropped_strong = len(best) - len(strong)
    if prune_unreachable:
        best = strong
        streets = [s for s in streets if s["from"] in best and s["to"] in best]

    nodes_out = [{"id": f"n{nid}", "lat": round(coords[nid][0], 6), "lon": round(coords[nid][1], 6)}
                 for nid in sorted(best)]
    streets_out = [{"id": s["id"], "from": f"n{s['from']}", "to": f"n{s['to']}", "oneWay": s["oneWay"]}
                   for s in streets]
    names = {s["id"]: s["name"] for s in streets if s["name"]}

    stats = {
        "osmHighwayWays": len(highways),
        "runsInsideZone": len(runs),
        "nodes": len(nodes_out),
        "streets": len(streets_out),
        "oneWayStreets": sum(1 for s in streets_out if s["oneWay"]),
        "droppedNodesOutsideMainComponent": dropped_nodes,
        "nodesWithoutRoundTrip": dropped_strong,
        "prunedToStronglyConnected": prune_unreachable,
    }
    return nodes_out, streets_out, names, stats


def largest_strong_component(node_ids, streets):
    """Kosaraju sobre el grafo dirigido: devuelve el conjunto de nodos de la SCC mayor."""
    adj = defaultdict(list)
    radj = defaultdict(list)
    for s in streets:
        adj[s["from"]].append(s["to"])
        radj[s["to"]].append(s["from"])
        if not s["oneWay"]:
            adj[s["to"]].append(s["from"])
            radj[s["from"]].append(s["to"])
    order, seen = [], set()
    for start in node_ids:
        if start in seen:
            continue
        seen.add(start)
        stack = [(start, iter(adj[start]))]
        while stack:
            v, it = stack[-1]
            for w in it:
                if w not in seen:
                    seen.add(w)
                    stack.append((w, iter(adj[w])))
                    break
            else:
                order.append(v)
                stack.pop()
    seen = set()
    best = set()
    for start in reversed(order):
        if start in seen:
            continue
        comp = set()
        stack = [start]
        seen.add(start)
        while stack:
            v = stack.pop()
            comp.add(v)
            for w in radj[v]:
                if w not in seen:
                    seen.add(w)
                    stack.append(w)
        if len(comp) > len(best):
            best = comp
    return best


def restaurant_candidates(coords, node_tags, ways, polygon, nodes_out):
    places = []
    for nid, tags in node_tags.items():
        if tags.get("amenity") in RESTAURANT_TYPES:
            places.append((coords[nid], tags))
    for w in ways:
        if w["tags"].get("amenity") in RESTAURANT_TYPES:
            pts = [coords[n] for n in w["nodes"] if n in coords]
            if pts:
                center = (sum(p[0] for p in pts) / len(pts), sum(p[1] for p in pts) / len(pts))
                places.append((center, w["tags"]))

    out = []
    for (lat, lon), tags in places:
        name = tags.get("name")
        if not name or not point_in_polygon(lat, lon, polygon):
            continue
        nearest, dist = None, float("inf")
        for n in nodes_out:
            d = haversine_m(lat, lon, n["lat"], n["lon"])
            if d < dist:
                nearest, dist = n["id"], d
        out.append({"name": name, "amenity": tags.get("amenity", ""), "lat": round(lat, 6),
                    "lon": round(lon, 6), "nearestNode": nearest, "distanceM": round(dist)})
    out.sort(key=lambda r: (r["distanceM"], r["name"]))
    return out


# ---------------------------------------------------------------------------
# Programa principal
# ---------------------------------------------------------------------------

def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--zone", default="data/equipetrol_zone.json")
    ap.add_argument("--out", default="data/equipetrol_network.json")
    ap.add_argument("--tolerance", type=float, default=12.0,
                    help="metros de desvio a partir de los que una calle curva gana un nodo (12)")
    ap.add_argument("--cache", default="tools/osm_cache/map.xml")
    ap.add_argument("--prune-unreachable", action="store_true",
                    help="conservar solo los nodos con ida y vuelta (componente fuertemente conexa mayor)")
    args = ap.parse_args()

    with open(args.zone) as f:
        polygon = json.load(f)["polygon"]

    print("descargando el rectangulo de OpenStreetMap...", file=sys.stderr)
    root = fetch_map(polygon, args.cache)
    coords, node_tags, ways = parse_osm(root)
    print(f"  {len(coords)} nodos y {len(ways)} vias en el rectangulo", file=sys.stderr)

    nodes, streets, names, stats = build_network(coords, ways, polygon, args.tolerance,
                                                 args.prune_unreachable)
    candidates = restaurant_candidates(coords, node_tags, ways, polygon, nodes)
    stats["restaurantCandidates"] = len(candidates)

    result = {
        "source": "OpenStreetMap, (c) OpenStreetMap contributors, ODbL",
        "zone": args.zone,
        "toleranceM": args.tolerance,
        "stats": stats,
        "nodes": nodes,
        "streets": streets,
        "streetNames": names,
        "restaurantCandidates": candidates,
    }
    os.makedirs(os.path.dirname(args.out) or ".", exist_ok=True)
    with open(args.out, "w") as f:
        json.dump(result, f, indent=1, ensure_ascii=False)
        f.write("\n")

    for k, v in stats.items():
        print(f"  {k}: {v}", file=sys.stderr)
    print(f"escrito {args.out}", file=sys.stderr)


if __name__ == "__main__":
    main()
